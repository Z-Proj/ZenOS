/**
 * 
 * @file : /src/libk/core/syscall.c
 * @brief : Syscall dispatcher.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#include "syscall.h"
#include "elf.h"
#include "../debug/log.h"
#include "../../drv/net/net.h"
#include "../../drv/keyboard.h"
#include "../../drv/keyboard.h"
#include "../../drv/mouse.h"
#include "../../drv/speaker.h"
#include "../../drv/vga.h"
#include "../../drv/disk/fat.h"
#include "../../drv/disk/vfs.h"
#include "../../drv/disk/devfs.h"
#include "fd.h"
#include "../../kernel/signal.h"
#include "../../kernel/sched.h"
#include "../../drv/hpet.h"
#include "../../cpu/acpi/acpi.h"
#include "../../cpu/gdt.h"
#include "../../cpu/smp.h"
#include "../string.h"
#include "mem.h"
#include "socket.h"
#include "unix_sock.h"
#include "../spinlock.h"

extern void syscall_entry(void);

#define SHM_VIRT_BASE 0x0000500000000000ULL
#define FB_VIRT_BASE  0x0000600000000000ULL
#define USER_MMAP_START 0x0000200000000000ULL
#define USER_MMAP_END   SHM_VIRT_BASE

#define ZEN_PR_SET_NAME          15
#define ZEN_PR_GET_NAME          16
#define ZEN_PR_CAPBSET_READ      23
#define ZEN_PR_CAPBSET_DROP      24
#define ZEN_PR_SET_NO_NEW_PRIVS  38
#define ZEN_PR_GET_NO_NEW_PRIVS  39
#define ZEN_PR_CAP_AMBIENT       47

typedef struct {
    char     name[SHM_NAME_MAX];
    uint64_t phys;
    size_t   pages;
    uint32_t refs;
    bool     in_use;
} shm_region_t;

static shm_region_t shm_table[SHM_MAX];
static spinlock_t shm_lock = {0};

#define MSR_EFER            0xC0000080
#define MSR_STAR            0xC0000081
#define MSR_LSTAR           0xC0000082
#define MSR_FMASK           0xC0000084
#define MSR_FS_BASE         0xC0000100
#define MSR_KERNEL_GS_BASE  0xC0000101
#define MSR_GS_BASE         0xC0000102

#define UNIX_AF_LOCAL       1
#define UNIX_SOCK_STREAM    1
#define UNIX_SOCK_DGRAM     2
#define UNIX_SOCK_NONBLOCK  04000
#define UNIX_SOCK_CLOEXEC   02000000

#define ZEN_POLLIN          0x0001
#define ZEN_POLLOUT         0x0004
#define ZEN_POLLERR         0x0008
#define ZEN_POLLHUP         0x0010
#define ZEN_POLLNVAL        0x0020

typedef struct {
    uint16_t family;
    char path[108];
} sa_un_t;

typedef struct {
    int fd;
    int16_t events;
    int16_t revents;
} pollfd_t;

/*
 * Keep long user sleeps cooperative. Timer interrupts that arrive while a
 * task is inside this syscall return to the same kernel context, so a single
 * long hlt-based sleep would otherwise hold the CPU away from other ready
 * tasks for the entire interval. Yield at least once per millisecond.
 */
static void sleep_with_scheduler(uint32_t ms)
{
    while (ms > 0)
    {
        sleep_ms(1);
        sched_yield();
        ms--;
    }
}

static inline void write_msr64(uint32_t msr, uint64_t value)
{
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

void syscall_retain_special_user_mappings(page_table_t *pml4)
{
    if (!pml4)
        return;

    spinlock_acquire(&shm_lock);
    for (int i = 0; i < SHM_MAX; i++) {
        if (!shm_table[i].in_use)
            continue;
        uint64_t virt = SHM_VIRT_BASE + (uint64_t)i * 0x10000000ULL;
        if (virt_to_phys(pml4, virt) == shm_table[i].phys)
            shm_table[i].refs++;
    }
    spinlock_release(&shm_lock);
}

void syscall_release_special_user_mappings(page_table_t *pml4)
{
    if (!pml4)
        return;

    spinlock_acquire(&shm_lock);
    for (int i = 0; i < SHM_MAX; i++) {
        if (!shm_table[i].in_use)
            continue;
        uint64_t virt = SHM_VIRT_BASE + (uint64_t)i * 0x10000000ULL;
        if (virt_to_phys(pml4, virt) != shm_table[i].phys)
            continue;
        if (shm_table[i].refs > 0)
            shm_table[i].refs--;
        if (shm_table[i].refs == 0) {
            free_pages(shm_table[i].phys, shm_table[i].pages);
            memset(&shm_table[i], 0, sizeof(shm_table[i]));
        }
    }
    spinlock_release(&shm_lock);
}

static int copy_from_user(task_t *task, void *dst, uint64_t src, size_t len);
static int copy_to_user(task_t *task, uint64_t dst, const void *src, size_t len);

static int unix_sock_copy_user_path(task_t *task, uint64_t user_addr, char *out, size_t out_len)
{
    sa_un_t addr;
    if (!task || !user_addr || !out || out_len < 2)
        return -22;
    if (copy_from_user(task, &addr, user_addr, sizeof(addr)) < 0)
        return -14;
    if (addr.family != UNIX_AF_LOCAL)
        return -22;

    size_t path_len = 0;
    while (path_len < sizeof(addr.path) && addr.path[path_len] != '\0')
        path_len++;
    if (path_len == 0 || path_len == sizeof(addr.path))
        return -22;

    if (addr.path[0] == '/')
    {
        if (path_len >= out_len)
            return -36;
        memcpy(out, addr.path, path_len + 1);
        return 0;
    }

    if (strcmp(task->cwd, "/") != 0 || path_len + 2 > out_len)
        return -22;
    out[0] = '/';
    memcpy(out + 1, addr.path, path_len + 1);
    return 0;
}

static uint64_t user_page_flags_from_prot(int prot)
{
    uint64_t flags = PAGE_PRESENT;
    if (prot != 0)
        flags |= PAGE_USER;
    if (prot & 0x2)
        flags |= PAGE_WRITABLE;
    return flags;
}

static int user_range_is_free(page_table_t *pml4, uint64_t start, size_t pages)
{
    if (!pml4)
        return 0;

    for (size_t i = 0; i < pages; i++)
    {
        if (virt_to_phys(pml4, start + i * PAGE_SIZE))
            return 0;
    }

    return 1;
}

static void user_unmap_range(task_t *task, uint64_t start, size_t pages)
{
    if (!task || !task->pml4)
        return;

    for (size_t i = 0; i < pages; i++)
    {
        uint64_t virt = start + i * PAGE_SIZE;
        uint64_t phys = virt_to_phys(task->pml4, virt);
        if (phys)
        {
            free_page(phys);
            unmap_page(task->pml4, virt);
        }
    }
}

static uint64_t user_find_mmap_range(task_t *task, uint64_t hint, size_t pages)
{
    if (!task || !task->pml4 || !pages)
        return 0;

    uint64_t size = (uint64_t)pages * PAGE_SIZE;
    uint64_t start = hint ? (hint & ~(uint64_t)(PAGE_SIZE - 1)) : task->mmap_base;
    if (start < USER_MMAP_START)
        start = USER_MMAP_START;

    for (uint64_t base = start; base + size <= USER_MMAP_END; base += PAGE_SIZE)
    {
        if (user_range_is_free(task->pml4, base, pages))
            return base;
    }

    for (uint64_t base = USER_MMAP_START; base + size <= start; base += PAGE_SIZE)
    {
        if (user_range_is_free(task->pml4, base, pages))
            return base;
    }

    return 0;
}

static int user_copy_validate(task_t *task, uint64_t uaddr, size_t len, int write_access)
{
    if (!len)
        return 0;
    if (!task || !task->pml4 || uaddr < PAGE_SIZE ||
        uaddr >= 0x0000800000000000ULL ||
        len - 1 > 0x00007FFFFFFFFFFFULL - uaddr)
        return -14;

    uint64_t end = uaddr + len;
    for (uint64_t page = uaddr & ~(uint64_t)(PAGE_SIZE - 1); page < end; page += PAGE_SIZE)
    {
        uint64_t pml4e = task->pml4->entries[(page >> 39) & 0x1FF];
        if (!(pml4e & PAGE_PRESENT) || !(pml4e & PAGE_USER))
            return -14;
        page_table_t *pdpt = (page_table_t *)((pml4e & 0x000FFFFFFFFFF000ULL) + KERNEL_VIRT_OFFSET);
        uint64_t pdpte = pdpt->entries[(page >> 30) & 0x1FF];
        if (!(pdpte & PAGE_PRESENT) || !(pdpte & PAGE_USER) ||
            (write_access && !(pdpte & PAGE_WRITABLE)))
            return -14;
        if (pdpte & (1ULL << 7))
            return -14;
        page_table_t *pd = (page_table_t *)((pdpte & 0x000FFFFFFFFFF000ULL) + KERNEL_VIRT_OFFSET);
        uint64_t pde = pd->entries[(page >> 21) & 0x1FF];
        if (!(pde & PAGE_PRESENT) || !(pde & PAGE_USER))
            return -14;
        if (pde & (1ULL << 7))
        {
            if (write_access && !(pde & PAGE_WRITABLE))
                return -14;
        }
        else
        {
            page_table_t *pt = (page_table_t *)((pde & 0x000FFFFFFFFFF000ULL) + KERNEL_VIRT_OFFSET);
            uint64_t pte = pt->entries[(page >> 12) & 0x1FF];
            if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER) ||
                (write_access && !(pte & PAGE_WRITABLE)))
                return -14;
        }
        if (write_access && !(pml4e & PAGE_WRITABLE))
            return -14;
    }
    return 0;
}

static int copy_from_user(task_t *task, void *dst, uint64_t src, size_t len)
{
    if (!dst || user_copy_validate(task, src, len, 0) < 0)
        return -14;
    uint8_t *out = (uint8_t *)dst;
    while (len)
    {
        size_t offset = (size_t)(src & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - offset;
        if (chunk > len)
            chunk = len;
        uint64_t phys = virt_to_phys(task->pml4, src);
        if (!phys)
            return -14;
        memcpy(out, (void *)(phys + KERNEL_VIRT_OFFSET), chunk);
        out += chunk;
        src += chunk;
        len -= chunk;
    }
    return 0;
}

static int copy_to_user(task_t *task, uint64_t dst, const void *src, size_t len)
{
    if (!src || user_copy_validate(task, dst, len, 1) < 0)
        return -14;
    const uint8_t *in = (const uint8_t *)src;
    while (len)
    {
        size_t offset = (size_t)(dst & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - offset;
        if (chunk > len)
            chunk = len;
        uint64_t phys = virt_to_phys(task->pml4, dst);
        if (!phys)
            return -14;
        memcpy((void *)(phys + KERNEL_VIRT_OFFSET), in, chunk);
        in += chunk;
        dst += chunk;
        len -= chunk;
    }
    return 0;
}

static int copy_user_string(task_t *task, uint64_t src, char *dst, size_t capacity)
{
    if (!dst || capacity == 0)
        return -22;
    for (size_t i = 0; i < capacity; i++)
    {
        if (copy_from_user(task, &dst[i], src + i, 1) < 0)
            return -14;
        if (dst[i] == '\0')
            return 0;
    }
    dst[capacity - 1] = '\0';
    return -36;
}

static int write_user_u64(task_t *task, uint64_t uaddr, uint64_t value)
{
    return copy_to_user(task, uaddr, &value, sizeof(value));
}

static int map_user_page_checked(page_table_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags)
{
    if (!pml4 || !phys)
        return -1;

    map_page(pml4, virt, phys, flags);
    if (virt_to_phys(pml4, virt) != phys)
        return -1;

    return 0;
}

static int poll_entry_once(task_t *task, pollfd_t *pfd)
{
    if (!pfd)
        return 0;

    pfd->revents = 0;
    if (pfd->fd < 0)
        return 0;

    if (pfd->fd < 3) {
        pfd->revents = pfd->events & (ZEN_POLLIN | ZEN_POLLOUT);
        return pfd->revents != 0;
    }

    if (!task || !task->fd_table || pfd->fd >= TASK_MAX_FDS ||
        !task->fd_table->entries[pfd->fd].used) {
        pfd->revents = ZEN_POLLNVAL;
        return 1;
    }

    fd_entry_t *e = &task->fd_table->entries[pfd->fd];
    switch (e->type) {
    case FD_FILE:
        pfd->revents = pfd->events & (ZEN_POLLIN | ZEN_POLLOUT);
        break;
    case FD_DIR:
        pfd->revents = pfd->events & ZEN_POLLIN;
        break;
    case FD_DEV:
        pfd->revents = pfd->events & (ZEN_POLLIN | ZEN_POLLOUT);
        break;
    case FD_STDIO:
        pfd->revents = pfd->events & (ZEN_POLLIN | ZEN_POLLOUT);
        break;
    case FD_PIPE_READ: {
        uint64_t flags = spinlock_acquire_irqsave(&e->pipe->lock);
        if ((pfd->events & ZEN_POLLIN) && (e->pipe->count > 0 || e->pipe->write_closed))
            pfd->revents |= ZEN_POLLIN;
        if (e->pipe->write_closed)
            pfd->revents |= ZEN_POLLHUP;
        spinlock_release_irqrestore(&e->pipe->lock, flags);
        break;
    }
    case FD_PIPE_WRITE: {
        uint64_t flags = spinlock_acquire_irqsave(&e->pipe->lock);
        if (e->pipe->read_closed)
            pfd->revents |= ZEN_POLLERR;
        else if ((pfd->events & ZEN_POLLOUT) && e->pipe->count < 4096)
            pfd->revents |= ZEN_POLLOUT;
        spinlock_release_irqrestore(&e->pipe->lock, flags);
        break;
    }
    case FD_PTY_MASTER: {
        uint64_t flags = spinlock_acquire_irqsave(&e->pty->lock);
        if ((pfd->events & ZEN_POLLIN) && e->pty->s2m_count > 0)
            pfd->revents |= ZEN_POLLIN;
        if ((pfd->events & ZEN_POLLOUT) && e->pty->slave_open)
            pfd->revents |= ZEN_POLLOUT;
        if (!e->pty->slave_open)
            pfd->revents |= ZEN_POLLHUP;
        spinlock_release_irqrestore(&e->pty->lock, flags);
        break;
    }
    case FD_PTY_SLAVE: {
        uint64_t flags = spinlock_acquire_irqsave(&e->pty->lock);
        if ((pfd->events & ZEN_POLLIN) && (e->pty->m2s_count > 0 || e->pty->eof_pending))
            pfd->revents |= ZEN_POLLIN;
        if ((pfd->events & ZEN_POLLOUT) && e->pty->master_open)
            pfd->revents |= ZEN_POLLOUT;
        if (!e->pty->master_open)
            pfd->revents |= ZEN_POLLHUP;
        spinlock_release_irqrestore(&e->pty->lock, flags);
        break;
    }
    case FD_UNIX_SOCK: {
        uint64_t flags = spinlock_acquire_irqsave(&e->usock->lock);
        if ((pfd->events & ZEN_POLLIN) && (e->usock->rcount > 0 || e->usock->read_closed))
            pfd->revents |= ZEN_POLLIN;
        if ((pfd->events & ZEN_POLLOUT) && e->usock->peer && !e->usock->peer->read_closed)
            pfd->revents |= ZEN_POLLOUT;
        if (e->usock->write_closed || e->usock->state == US_CLOSED)
            pfd->revents |= ZEN_POLLHUP;
        spinlock_release_irqrestore(&e->usock->lock, flags);
        break;
    }
    default:
        pfd->revents = ZEN_POLLNVAL;
        break;
    }

    return pfd->revents != 0;
}

static int poll_fds_once(pollfd_t *fds, size_t count)
{
    task_t *task = sched_current_task();
    int ready = 0;
    for (size_t i = 0; i < count; i++)
        ready += poll_entry_once(task, &fds[i]);
    return ready;
}

void init_syscalls(void)
{
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x13 << 48);
    write_msr64(MSR_STAR, star);

    uint64_t lstar = (uint64_t)&syscall_entry;
    write_msr64(MSR_LSTAR, lstar);

    uint64_t fmask = 0x200;
    write_msr64(MSR_FMASK, fmask);

    uint32_t efer_lo, efer_hi;
    __asm__ volatile("rdmsr" : "=a"(efer_lo), "=d"(efer_hi) : "c"(MSR_EFER));
    efer_lo |= 1;
    write_msr64(MSR_EFER, ((uint64_t)efer_hi << 32) | efer_lo);

    write_msr64(MSR_FS_BASE, 0);
    write_msr64(MSR_KERNEL_GS_BASE, 0);

    tss_t *cpu_tss = gdt_get_tss(smp_current_cpu_index());
    if (!cpu_tss)
        cpu_tss = gdt_get_tss(smp_bsp_cpu_index());

    uint64_t kernel_gs_base = (uint64_t)cpu_tss;
    write_msr64(MSR_GS_BASE, kernel_gs_base);

    log("Syscalls initialized.", 4, 0);
}

void syscall_prepare_user_return(uint64_t gs_base)
{
    task_t *current = sched_current_task();
    tss_t *cpu_tss = gdt_get_tss(smp_current_cpu_index());
    if (!cpu_tss)
        cpu_tss = gdt_get_tss(smp_bsp_cpu_index());

    write_msr64(MSR_FS_BASE, current ? current->user_fs_base : 0);
    write_msr64(MSR_KERNEL_GS_BASE, gs_base);

    uint64_t kernel_gs_base = (uint64_t)cpu_tss;
    write_msr64(MSR_GS_BASE, kernel_gs_base);
}

void syscall_prepare_sysret_return(uint64_t gs_base)
{
    task_t *current = sched_current_task();
    tss_t *cpu_tss = gdt_get_tss(smp_current_cpu_index());
    if (!cpu_tss)
        cpu_tss = gdt_get_tss(smp_bsp_cpu_index());

    write_msr64(MSR_FS_BASE, current ? current->user_fs_base : 0);
    uint64_t kernel_gs_base = (uint64_t)cpu_tss;
    write_msr64(MSR_KERNEL_GS_BASE, kernel_gs_base);
    write_msr64(MSR_GS_BASE, gs_base);
}

static void dispatch_pending_signals(task_t *task)
{
    if (!task || !task->sig_pending)
        return;

    for (int sig = 1; sig < NSIG; sig++)
    {
        if (!(task->sig_pending & (1ull << sig)))
            continue;
        if (task->sig_mask & (1ull << sig))
            continue;

        task->sig_pending &= ~(1ull << sig);
        zen_sigaction_t *sa = &task->sighandlers[sig];

        if (sa->handler == SIG_DFL || sa->handler == 0)
        {
            if (sig == SIGCHLD || sig == SIGCONT)
                continue;
            task->exit_code = sig;
            task->state = TASK_DEAD;
            sched_yield();
            return;
        }

        if (sa->handler == SIG_IGN)
            continue;

        uint64_t usp = task->regs.userrsp;
        usp -= sizeof(uint64_t);
        uint64_t page_base = usp & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t page_off = usp & (PAGE_SIZE - 1);
        uint64_t phys = virt_to_phys(task->pml4, page_base);
        if (phys)
        {
            uint64_t *slot = (uint64_t *)(phys + KERNEL_VIRT_OFFSET + page_off);
            *slot = task->regs.rip;
        }
        task->regs.userrsp = usp;
        task->regs.rdi = (uint64_t)sig;
        task->regs.rip = sa->handler;
        if (task->sig_trampoline)
            task->regs.userrsp -= sizeof(uint64_t);
        break;
    }
}

static int64_t console_read_fallback(void *buffer, uint32_t size)
{
    if (!buffer)
        return -1;
    if (!size)
        return 0;

    char *cbuf = (char *)buffer;
    uint32_t total = 0;
    while (total < size)
    {
        char c = 0;
        while (!c)
        {
            c = get_key();
            if (!c)
                sched_yield();
        }
        cbuf[total++] = c;
        if (c == '\n' || kbd_pending_chars() == 0)
            break;
    }
    return (int64_t)total;
}

static int64_t console_ioctl_fallback(unsigned long req, void *argp)
{
    if (req == ZEN_TCGETS)
    {
        if (!argp)
            return -1;
        zen_termios_t *tio = (zen_termios_t *)argp;
        memset(tio, 0, sizeof(*tio));
        tio->c_iflag = ZEN_ICRNL | ZEN_IXON;
        tio->c_oflag = ZEN_OPOST | ZEN_ONLCR;
        tio->c_cflag = ZEN_CS8 | ZEN_CREAD;
        tio->c_lflag = ZEN_ISIG | ZEN_ICANON | ZEN_ECHO | ZEN_ECHOE | ZEN_ECHOK | ZEN_IEXTEN;
        tio->c_cc[ZEN_VINTR] = 3;
        tio->c_cc[ZEN_VQUIT] = 28;
        tio->c_cc[ZEN_VERASE] = 127;
        tio->c_cc[ZEN_VKILL] = 21;
        tio->c_cc[ZEN_VEOF] = 4;
        tio->c_cc[ZEN_VTIME] = 0;
        tio->c_cc[ZEN_VMIN] = 1;
        tio->c_cc[ZEN_VSTART] = 17;
        tio->c_cc[ZEN_VSTOP] = 19;
        tio->c_cc[ZEN_VSUSP] = 26;
        return 0;
    }
    if (req == ZEN_TCSETS || req == ZEN_TCSETSW || req == ZEN_TCSETSF)
        return 0;
    if (req == ZEN_TIOCGWINSZ)
    {
        if (!argp)
            return -1;
        zen_winsize_t *ws = (zen_winsize_t *)argp;
        ws->ws_col = framebuffer_width ? (uint16_t)(framebuffer_width / 8) : 80;
        ws->ws_row = framebuffer_height ? (uint16_t)(framebuffer_height / 16) : 25;
        ws->ws_xpixel = (uint16_t)framebuffer_width;
        ws->ws_ypixel = (uint16_t)framebuffer_height;
        return 0;
    }
    if (req == ZEN_FIONREAD)
    {
        if (!argp)
            return -1;
        *(int *)argp = (int)kbd_pending_chars();
        return 0;
    }
    return -1;
}

static int pty_push_m2s(pty_buf_t *pty, uint8_t ch)
{
    if (pty->m2s_count >= PTY_BUF_SIZE)
        return 0;
    pty->m2s_data[pty->m2s_write] = ch;
    pty->m2s_write = (pty->m2s_write + 1) % PTY_BUF_SIZE;
    pty->m2s_count++;
    return 1;
}

static uint32_t pipe_read_locked(pipe_buf_t *pipe, uint8_t *dst, uint32_t size)
{
    uint32_t first = 4096 - pipe->read_pos;
    if (first > size)
        first = size;
    memcpy(dst, pipe->data + pipe->read_pos, first);
    pipe->read_pos = (pipe->read_pos + first) % 4096;
    if (first == size)
        return first;
    uint32_t second = size - first;
    memcpy(dst + first, pipe->data + pipe->read_pos, second);
    pipe->read_pos = (pipe->read_pos + second) % 4096;
    return size;
}

static uint32_t pipe_write_locked(pipe_buf_t *pipe, const uint8_t *src, uint32_t size)
{
    uint32_t first = 4096 - pipe->write_pos;
    if (first > size)
        first = size;
    memcpy(pipe->data + pipe->write_pos, src, first);
    pipe->write_pos = (pipe->write_pos + first) % 4096;
    if (first == size)
        return first;
    uint32_t second = size - first;
    memcpy(pipe->data + pipe->write_pos, src + first, second);
    pipe->write_pos = (pipe->write_pos + second) % 4096;
    return size;
}

static int pty_pop_m2s(pty_buf_t *pty, uint8_t *out)
{
    if (pty->m2s_count == 0)
        return 0;
    if (out)
        *out = pty->m2s_data[pty->m2s_read];
    pty->m2s_read = (pty->m2s_read + 1) % PTY_BUF_SIZE;
    pty->m2s_count--;
    if (pty->m2s_ready > 0)
        pty->m2s_ready--;
    return 1;
}

static int pty_pop_m2s_tail_unready(pty_buf_t *pty)
{
    if (pty->m2s_count <= pty->m2s_ready || pty->m2s_count == 0)
        return 0;
    pty->m2s_write = (pty->m2s_write + PTY_BUF_SIZE - 1) % PTY_BUF_SIZE;
    pty->m2s_count--;
    return 1;
}

static int pty_push_s2m_nowait(pty_buf_t *pty, uint8_t ch)
{
    if (pty->s2m_count >= PTY_BUF_SIZE)
        return 0;
    pty->s2m_data[pty->s2m_write] = ch;
    pty->s2m_write = (pty->s2m_write + 1) % PTY_BUF_SIZE;
    pty->s2m_count++;
    return 1;
}

static int pty_push_s2m_wait(pty_buf_t *pty, uint8_t ch)
{
    while (1)
    {
        uint64_t rflags = spinlock_acquire_irqsave(&pty->lock);
        if (pty->s2m_count < PTY_BUF_SIZE)
        {
            int ok = pty_push_s2m_nowait(pty, ch);
            spinlock_release_irqrestore(&pty->lock, rflags);
            return ok;
        }
        if (!pty->master_open)
        {
            spinlock_release_irqrestore(&pty->lock, rflags);
            return 0;
        }
        spinlock_release_irqrestore(&pty->lock, rflags);
        sched_yield();
    }
}

static void pty_write_echo(pty_buf_t *pty, uint8_t ch)
{
    pty_push_s2m_nowait(pty, ch);
}

static void pty_defaults(pty_buf_t *pty)
{
    spinlock_init(&pty->lock);
    pty->iflag = ZEN_ICRNL | ZEN_IXON;
    pty->oflag = ZEN_OPOST | ZEN_ONLCR;
    pty->cflag = ZEN_CREAD | ZEN_CS8;
    pty->lflag = ZEN_ISIG | ZEN_ICANON | ZEN_ECHO | ZEN_ECHOE | ZEN_ECHOK | ZEN_IEXTEN;
    memset(pty->cc, 0, sizeof(pty->cc));
    pty->cc[ZEN_VINTR] = 3;
    pty->cc[ZEN_VQUIT] = 28;
    pty->cc[ZEN_VERASE] = 127;
    pty->cc[ZEN_VKILL] = 21;
    pty->cc[ZEN_VEOF] = 4;
    pty->cc[ZEN_VTIME] = 0;
    pty->cc[ZEN_VMIN] = 1;
    pty->cc[ZEN_VSTART] = 17;
    pty->cc[ZEN_VSTOP] = 19;
    pty->cc[ZEN_VSUSP] = 26;
    pty->ispeed = 38400;
    pty->ospeed = 38400;
}

static void pty_export_termios(pty_buf_t *pty, zen_termios_t *t)
{
    if (!pty || !t)
        return;
    uint64_t rflags = spinlock_acquire_irqsave(&pty->lock);
    memset(t, 0, sizeof(*t));
    t->c_iflag = pty->iflag;
    t->c_oflag = pty->oflag;
    t->c_cflag = pty->cflag;
    t->c_lflag = pty->lflag;
    memcpy(t->c_cc, pty->cc, sizeof(pty->cc));
    t->c_ispeed = pty->ispeed;
    t->c_ospeed = pty->ospeed;
    spinlock_release_irqrestore(&pty->lock, rflags);
}

static void pty_import_termios(pty_buf_t *pty, const zen_termios_t *t, int flush_input)
{
    if (!pty || !t)
        return;
    uint64_t rflags = spinlock_acquire_irqsave(&pty->lock);
    uint32_t old_lflag = pty->lflag;
    pty->iflag = t->c_iflag;
    pty->oflag = t->c_oflag;
    pty->cflag = t->c_cflag;
    pty->lflag = t->c_lflag;
    memcpy(pty->cc, t->c_cc, sizeof(pty->cc));
    pty->ispeed = t->c_ispeed;
    pty->ospeed = t->c_ospeed;
    if (flush_input)
    {
        pty->m2s_read = 0;
        pty->m2s_write = 0;
        pty->m2s_count = 0;
        pty->m2s_ready = 0;
        pty->eof_pending = 0;
        pty->esc_state = 0;
    }
    if ((old_lflag & ZEN_ICANON) == 0 || (pty->lflag & ZEN_ICANON) == 0)
    {
        pty->m2s_ready = pty->m2s_count;
        if ((pty->lflag & ZEN_ICANON) == 0)
        {
            pty->eof_pending = 0;
            pty->esc_state = 0;
        }
    }
    spinlock_release_irqrestore(&pty->lock, rflags);
}

static int64_t pty_slave_read(pty_buf_t *pty, uint8_t *dst, uint32_t size)
{
    uint32_t n = 0;
    if (size == 0)
        return 0;

    uint64_t rflags = spinlock_acquire_irqsave(&pty->lock);
    int canonical = (pty->lflag & ZEN_ICANON) != 0;
    uint8_t vmin = pty->cc[ZEN_VMIN];
    spinlock_release_irqrestore(&pty->lock, rflags);

    if (canonical)
    {
        while (n < size)
        {
            while (1)
            {
                rflags = spinlock_acquire_irqsave(&pty->lock);
                if (pty->m2s_ready > 0)
                {
                    spinlock_release_irqrestore(&pty->lock, rflags);
                    break;
                }
                if (pty->eof_pending)
                {
                    pty->eof_pending = 0;
                    spinlock_release_irqrestore(&pty->lock, rflags);
                    return (int64_t)n;
                }
                if (!pty->master_open)
                {
                    spinlock_release_irqrestore(&pty->lock, rflags);
                    return (int64_t)n;
                }
                spinlock_release_irqrestore(&pty->lock, rflags);
                sched_yield();
            }
            uint8_t ch = 0;
            rflags = spinlock_acquire_irqsave(&pty->lock);
            if (!pty_pop_m2s(pty, &ch))
            {
                spinlock_release_irqrestore(&pty->lock, rflags);
                continue;
            }
            spinlock_release_irqrestore(&pty->lock, rflags);
            dst[n++] = ch;
            if (ch == '\n')
                break;
        }
        return (int64_t)n;
    }

    if (vmin == 0)
    {
        while (n < size)
        {
            uint8_t ch = 0;
            rflags = spinlock_acquire_irqsave(&pty->lock);
            if (!pty_pop_m2s(pty, &ch))
            {
                spinlock_release_irqrestore(&pty->lock, rflags);
                break;
            }
            spinlock_release_irqrestore(&pty->lock, rflags);
            dst[n++] = ch;
        }
        return (int64_t)n;
    }

    while (1)
    {
        rflags = spinlock_acquire_irqsave(&pty->lock);
        if (pty->m2s_count >= vmin)
        {
            spinlock_release_irqrestore(&pty->lock, rflags);
            break;
        }
        if (!pty->master_open)
        {
            int empty = (pty->m2s_count == 0);
            spinlock_release_irqrestore(&pty->lock, rflags);
            if (empty)
                return 0;
            break;
        }
        spinlock_release_irqrestore(&pty->lock, rflags);
        sched_yield();
    }

    while (n < size)
    {
        uint8_t ch = 0;
        rflags = spinlock_acquire_irqsave(&pty->lock);
        if (!pty_pop_m2s(pty, &ch))
        {
            spinlock_release_irqrestore(&pty->lock, rflags);
            break;
        }
        spinlock_release_irqrestore(&pty->lock, rflags);
        dst[n++] = ch;
    }
    return (int64_t)n;
}

static int64_t pty_master_read(pty_buf_t *pty, uint8_t *dst, uint32_t size)
{
    uint32_t n = 0;
    while (n < size)
    {
        uint64_t rflags = spinlock_acquire_irqsave(&pty->lock);
        if (pty->s2m_count == 0)
        {
            spinlock_release_irqrestore(&pty->lock, rflags);
            break;
        }
        dst[n++] = pty->s2m_data[pty->s2m_read];
        pty->s2m_read = (pty->s2m_read + 1) % PTY_BUF_SIZE;
        pty->s2m_count--;
        spinlock_release_irqrestore(&pty->lock, rflags);
    }
    return (int64_t)n;
}

static int64_t pty_master_write(pty_buf_t *pty, const uint8_t *src, uint32_t size)
{
    uint32_t written = 0;

    for (uint32_t i = 0; i < size; i++)
    {
        uint8_t ch = src[i];
        uint64_t rflags = spinlock_acquire_irqsave(&pty->lock);
        if (!pty->slave_open)
        {
            spinlock_release_irqrestore(&pty->lock, rflags);
            return written ? (int64_t)written : -1;
        }

        if (pty->iflag & ZEN_IGNCR)
        {
            if (ch == '\r')
            {
                spinlock_release_irqrestore(&pty->lock, rflags);
                written++;
                continue;
            }
        }
        else if ((pty->iflag & ZEN_ICRNL) && ch == '\r')
        {
            ch = '\n';
        }
        else if ((pty->iflag & ZEN_INLCR) && ch == '\n')
        {
            ch = '\r';
        }

        if (pty->lflag & ZEN_ICANON)
        {
            uint8_t verase = pty->cc[ZEN_VERASE] ? pty->cc[ZEN_VERASE] : 127;
            uint8_t vkill = pty->cc[ZEN_VKILL] ? pty->cc[ZEN_VKILL] : 21;
            uint8_t veof = pty->cc[ZEN_VEOF] ? pty->cc[ZEN_VEOF] : 4;

            if (pty->esc_state == 1)
            {
                if (ch == '[' || ch == 'O')
                    pty->esc_state = 2;
                else
                    pty->esc_state = 0;
                spinlock_release_irqrestore(&pty->lock, rflags);
                written++;
                continue;
            }
            if (pty->esc_state == 2)
            {
                if (ch >= 0x40 && ch <= 0x7E)
                    pty->esc_state = 0;
                spinlock_release_irqrestore(&pty->lock, rflags);
                written++;
                continue;
            }
            if (ch == 0x1B)
            {
                pty->esc_state = 1;
                spinlock_release_irqrestore(&pty->lock, rflags);
                written++;
                continue;
            }

            if (ch != veof)
                pty->eof_pending = 0;

            if (ch == verase || ch == '\b')
            {
                if (pty_pop_m2s_tail_unready(pty) && (pty->lflag & ZEN_ECHO))
                {
                    if (pty->lflag & ZEN_ECHOE)
                    {
                        pty_write_echo(pty, '\b');
                        pty_write_echo(pty, ' ');
                        pty_write_echo(pty, '\b');
                    }
                    else
                    {
                        pty_write_echo(pty, ch);
                    }
                }
                spinlock_release_irqrestore(&pty->lock, rflags);
                written++;
                continue;
            }

            if (ch == vkill)
            {
                while (pty_pop_m2s_tail_unready(pty)) {}
                if ((pty->lflag & ZEN_ECHO) && (pty->lflag & ZEN_ECHOK))
                    pty_write_echo(pty, '\n');
                spinlock_release_irqrestore(&pty->lock, rflags);
                written++;
                continue;
            }

            if (ch == veof)
            {
                if (pty->m2s_count == pty->m2s_ready)
                    pty->eof_pending = 1;
                else
                    pty->m2s_ready = pty->m2s_count;
                spinlock_release_irqrestore(&pty->lock, rflags);
                written++;
                continue;
            }
        }

        while (!pty_push_m2s(pty, ch))
        {
            if (!pty->slave_open)
            {
                spinlock_release_irqrestore(&pty->lock, rflags);
                return written ? (int64_t)written : -1;
            }
            spinlock_release_irqrestore(&pty->lock, rflags);
            sched_yield();
            rflags = spinlock_acquire_irqsave(&pty->lock);
        }

        if ((pty->lflag & ZEN_ICANON) && ch == '\n')
            pty->m2s_ready = pty->m2s_count;
        else if ((pty->lflag & ZEN_ICANON) == 0)
            pty->m2s_ready = pty->m2s_count;

        if (pty->lflag & ZEN_ECHO)
        {
            if (ch == '\n')
            {
                pty_write_echo(pty, '\r');
                pty_write_echo(pty, '\n');
            }
            else if (ch == '\t')
            {
                pty_write_echo(pty, ch);
            }
            else if (ch >= 32 && ch != 127)
            {
                pty_write_echo(pty, ch);
            }
        }

        spinlock_release_irqrestore(&pty->lock, rflags);
        written++;
    }

    return (int64_t)written;
}

static int64_t pty_slave_write(pty_buf_t *pty, const uint8_t *src, uint32_t size)
{
    uint32_t written = 0;

    for (uint32_t i = 0; i < size; i++)
    {
        uint8_t ch = src[i];
        uint64_t rflags = spinlock_acquire_irqsave(&pty->lock);
        int master_open = pty->master_open;
        uint32_t oflag = pty->oflag;
        spinlock_release_irqrestore(&pty->lock, rflags);
        if (!master_open)
            return written ? (int64_t)written : -1;
        if ((oflag & ZEN_OPOST) && (oflag & ZEN_ONLCR) && ch == '\n')
        {
            if (!pty_push_s2m_wait(pty, '\r'))
                break;
        }
        if (!pty_push_s2m_wait(pty, ch))
            break;
        written++;
    }
    return (int64_t)written;
}

uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    switch (num)
    {
    case SYSCALL_EXEC:
    {
        task_t *current = sched_current_task();
        char filename[256];
        if (!current || copy_user_string(current, arg1, filename, sizeof(filename)) < 0)
            return -14;
        return elf_execve_replace(filename, (int)arg2, (char **)arg3, (char **)arg4);
    }

    case SYSCALL_SPAWN:
    {
        task_t *current = sched_current_task();
        char filename[256];
        if (!current || copy_user_string(current, arg1, filename, sizeof(filename)) < 0)
            return -14;
        return elf_spawn(filename, (int)arg2, (char **)arg3, current);
    }

    case SYSCALL_EXIT:
    {
        sched_abort_current((int)arg1);
    }

    case SYSCALL_GETPID:
    {
        task_t *current = sched_current_task();
        return current ? current->pid : 0;
    }

    case SYSCALL_GETPPID:
    {
        task_t *current = sched_current_task();
        if (!current || current->parent_pid == TASK_NO_PARENT)
            return 1;
        return current->parent_pid;
    }

    case SYSCALL_YIELD:
    {
        sched_yield();
        return 0;
    }

    case SYSCALL_LS:
    {
        task_t *cur = sched_current_task();
        size_t buf_size = (size_t)arg2;
        if (!cur || buf_size == 0 || buf_size > 65536)
            return -1;
        char *buf = (char *)kmalloc(buf_size);
        if (!buf)
            return -12;
        int ret = vfs_list(buf, buf_size);
        if (ret >= 0 && copy_to_user(cur, arg1, buf, buf_size) < 0)
            ret = -14;
        kfree(buf);
        return ret;
    }

    case SYSCALL_GETKEY:
        return (uint64_t)get_key();

    case SYSCALL_PRINTS:
    {
        task_t *cur = sched_current_task();
        uint32_t len = (uint32_t)arg2;
        char str[4096];
        if (!cur || len > sizeof(str))
            return -1;
        if (copy_from_user(cur, str, arg1, len) < 0)
            return -14;
        for (uint32_t i = 0; i < len && str[i]; i++)
            printc(str[i]);
        return 0;
    }

    case SYSCALL_MOUSE_X:
        return mouse_x();

    case SYSCALL_MOUSE_Y:
        return mouse_y();

    case SYSCALL_MOUSE_BTN:
        return mouse_button();

    case SYSCALL_MOUSE_SET_POS:
        mouse_set_pos((uint32_t)arg1, (uint32_t)arg2);
        return 0;

    case SYSCALL_SPEAKER:
        speaker_play((uint32_t)arg1);
        return 0;

    case SYSCALL_SPEAKER_OFF:
        speaker_pause();
        return 0;

    case SYSCALL_OPEN:
    {
        char filename[256];
        int flags = (int)arg2;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -9;
        if (copy_user_string(cur, arg1, filename, sizeof(filename)) < 0)
            return -14;

        if (flags & 0200000)
        {
            if (vfs_stat(filename) < 0)
                return -2;
            int fd = fd_alloc(cur->fd_table);
            if (fd < 0)
                return -24;
            fd_entry_t *e = &cur->fd_table->entries[fd];
            if (vfs_opendir_entry(filename, e) < 0)
                return -20;
            return fd;
        }

        int exists = vfs_stat(filename) == 0;
        if ((flags & 0x40) && (flags & 0x80) && exists)
            return -17;

        int write_mode = 0;
        if (flags & 0x01)
            write_mode = 1;
        if (flags & 0x02)
            write_mode = 1;
        if (flags & 0x200)
            write_mode = 2;
        if ((flags & 0x40) && !exists)
        {
            if (vfs_create(filename) < 0)
                return -2;
            exists = 1;
        }
        if (!exists)
            return -2;
        int fd = fd_alloc(cur->fd_table);
        if (fd < 0)
            return -24;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (vfs_open_entry(filename, write_mode, e) < 0) {
            fd_entry_t dir_tmp;
            memset(&dir_tmp, 0, sizeof(dir_tmp));
            if (vfs_opendir_entry(filename, &dir_tmp) == 0) {
                vfs_closedir_entry(&dir_tmp);
                return -21;
            }
            return -2;
        }
        return fd;
    }

    case SYSCALL_READ:
    {
        int fd = (int)arg1;
        void *buffer = (void *)arg2;
        uint32_t size = (uint32_t)arg3;
        if (!buffer)
            return -22;
        task_t *cur = sched_current_task();
        if (!cur || user_copy_validate(cur, (uint64_t)buffer, size, 1) < 0)
            return -14;
        fd_entry_t *e = NULL;
        if (cur->fd_table && fd >= 0 && fd < TASK_MAX_FDS)
        {
            fd_entry_t *cand = &cur->fd_table->entries[fd];
            if (cand->used)
                e = cand;
        }
        if (fd == 0 && !e)
            return console_read_fallback(buffer, size);
        if (!e)
            return -9;
        if (e->type == FD_STDIO)
        {
            if (e->stdio_fd == 0)
                return console_read_fallback(buffer, size);
            return -9;
        }
        if (e->type == FD_PTY_SLAVE)
        {
            return pty_slave_read(e->pty, (uint8_t *)buffer, size);
        }
        if (e->type == FD_PTY_MASTER)
        {
            return pty_master_read(e->pty, (uint8_t *)buffer, size);
        }
        if (e->type == FD_UNIX_SOCK)
        {
            int r = unix_sock_read(e->usock, buffer, size);
            return r < 0 ? -11 : r;
        }
        if (e->type == FD_PIPE_READ)
        {
            uint8_t *cbuf = (uint8_t *)buffer;
            uint32_t n = 0;
            while (n < size)
            {
                uint64_t rflags = spinlock_acquire_irqsave(&e->pipe->lock);
                if (e->pipe->count == 0)
                {
                    int write_closed = e->pipe->write_closed;
                    spinlock_release_irqrestore(&e->pipe->lock, rflags);
                    if (write_closed)
                        break;
                    sched_yield();
                    continue;
                }
                uint32_t chunk = size - n;
                if (chunk > e->pipe->count)
                    chunk = e->pipe->count;
                n += pipe_read_locked(e->pipe, cbuf + n, chunk);
                e->pipe->count -= chunk;
                spinlock_release_irqrestore(&e->pipe->lock, rflags);
            }
            return (int64_t)n;
        }
        if (e->type == FD_DEV)
        {
            struct dev_entry *d = e->dev_ops;
            if (!d || !d->read)
                return 0;
            uint8_t *kbuf = (uint8_t *)kmalloc(4096);
            if (!kbuf)
                return -12;
            uint32_t total = 0;
            while (total < size)
            {
                uint32_t chunk = size - total;
                if (chunk > 4096)
                    chunk = 4096;
                uint32_t got = 0;
                d->read(kbuf, chunk, &got);
                if (got == 0)
                    break;
                if (copy_to_user(cur, (uint64_t)(uintptr_t)buffer + total, kbuf, got) < 0) {
                    kfree(kbuf);
                    return total > 0 ? (int64_t)total : -14;
                }
                total += got;
            }
            kfree(kbuf);
            return (int64_t)total;
        }
        if (e->type != FD_FILE)
            return e->type == FD_DIR ? -21 : -9;
        if (size == 0)
            return 0;
        uint8_t *ubuf = (uint8_t *)buffer;
        uint8_t *kbuf = (uint8_t *)kmalloc(4096);
        if (!kbuf)
            return -12;
        uint32_t total = 0;
        while (total < size)
        {
            uint32_t chunk = size - total;
            if (chunk > 4096)
                chunk = 4096;
            uint32_t bytes_read = 0;
            int ret = vfs_read_entry(e, kbuf, chunk, &bytes_read);
            if (ret < 0)
            {
                kfree(kbuf);
                return (total > 0) ? (int64_t)total : -5;
            }
            if (bytes_read == 0)
                break;
            if (copy_to_user(cur, (uint64_t)(uintptr_t)ubuf + total, kbuf, bytes_read) < 0) {
                kfree(kbuf);
                return total > 0 ? (int64_t)total : -14;
            }
            total += bytes_read;
            if (bytes_read < chunk)
                break;
            if (total < size)
                sched_yield();
        }
        kfree(kbuf);
        return (int64_t)total;
    }

    case SYSCALL_WRITE:
    {
        int fd = (int)arg1;
        const void *buffer = (const void *)arg2;
        uint32_t size = (uint32_t)arg3;
        if (!buffer)
            return -22;
        task_t *cur = sched_current_task();
        if (!cur || user_copy_validate(cur, (uint64_t)buffer, size, 0) < 0)
            return -14;
        fd_entry_t *e = NULL;
        if (cur->fd_table && fd >= 0 && fd < TASK_MAX_FDS)
        {
            fd_entry_t *cand = &cur->fd_table->entries[fd];
            if (cand->used)
                e = cand;
        }
        if ((fd == 1 || fd == 2) && !e)
        {
            const char *p = (const char *)buffer;
            for (uint32_t i = 0; i < size; i++){
                printc(p[i]);
                //serial_write_char(p[i]);
            }
            return (int64_t)size;
        }
        if (!e)
            return -9;
        if (e->type == FD_STDIO)
        {
            if (e->stdio_fd == 1 || e->stdio_fd == 2)
            {
                const char *p = (const char *)buffer;
                for (uint32_t i = 0; i < size; i++)
                    printc(p[i]);
                return (int64_t)size;
            }
            return -9;
        }
        if (e->type == FD_PTY_SLAVE)
        {
            return pty_slave_write(e->pty, (const uint8_t *)buffer, size);
        }
        if (e->type == FD_PTY_MASTER)
        {
            return pty_master_write(e->pty, (const uint8_t *)buffer, size);
        }
        if (e->type == FD_UNIX_SOCK)
        {
            int r = unix_sock_write(e->usock, buffer, size);
            return r < 0 ? -32 : r;
        }
        if (e->type == FD_PIPE_WRITE)
        {
            const uint8_t *cbuf = (const uint8_t *)buffer;
            uint32_t total = 0;
            while (total < size)
            {
                while (1)
                {
                    uint64_t rflags = spinlock_acquire_irqsave(&e->pipe->lock);
                    uint32_t space = 4096 - e->pipe->count;
                    if (space)
                    {
                        uint32_t chunk = size - total;
                        if (chunk > space)
                            chunk = space;
                        total += pipe_write_locked(e->pipe, cbuf + total, chunk);
                        e->pipe->count += chunk;
                        spinlock_release_irqrestore(&e->pipe->lock, rflags);
                        break;
                    }
                    spinlock_release_irqrestore(&e->pipe->lock, rflags);
                    sched_yield();
                }
            }
            return (int64_t)total;
        }
        if (e->type == FD_DEV)
        {
            struct dev_entry *d = e->dev_ops;
            if (!d || !d->write)
                return -9;
            if (d->write(buffer, size) < 0)
                return -5;
            return (int64_t)size;
        }
        if (e->type != FD_FILE)
            return e->type == FD_DIR ? -21 : -9;
        if (size == 0)
            return 0;
        const uint8_t *ubuf = (const uint8_t *)buffer;
        uint8_t *kbuf = (uint8_t *)kmalloc(4096);
        if (!kbuf)
            return -12;
        uint32_t total = 0;
        while (total < size)
        {
            uint32_t chunk = size - total;
            if (chunk > 4096)
                chunk = 4096;
            if (copy_from_user(cur, kbuf, (uint64_t)(uintptr_t)ubuf + total, chunk) < 0) {
                kfree(kbuf);
                return total > 0 ? (int64_t)total : -14;
            }
            int ret = vfs_write_entry(e, kbuf, chunk);
            if (ret < 0)
            {
                kfree(kbuf);
                return (total > 0) ? (int64_t)total : -5;
            }
            total += chunk;
            if (total < size)
                sched_yield();
        }
        kfree(kbuf);
        return (int64_t)total;
    }

    case SYSCALL_CLOSE:
    {
        int fd = (int)arg1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -9;
        if (fd < 0 || fd >= TASK_MAX_FDS)
            return -9;
        if (fd < 3 && !cur->fd_table->entries[fd].used)
            return 0;
        if (!cur->fd_table->entries[fd].used)
            return -9;
        return fd_close(cur->fd_table, fd);
    }

    case SYSCALL_LSEEK:
    {
        int fd = (int)arg1;
        int32_t offset = (int32_t)arg2;
        int whence = (int)arg3;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -9;
        if (fd < 0 || fd >= TASK_MAX_FDS)
            return -9;
        if (fd == 0 || fd == 1 || fd == 2)
            return -29;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used)
            return -9;
        if (e->type != FD_FILE)
            return -29;
        return vfs_lseek_entry(e, offset, whence);
    }

    case SYSCALL_CREATE:
    {
        task_t *cur = sched_current_task();
        char filename[256];
        if (!cur || copy_user_string(cur, arg1, filename, sizeof(filename)) < 0)
            return -14;
        if (vfs_stat(filename) == 0)
            return -17;
        return vfs_create(filename) < 0 ? -2 : 0;
    }

    case SYSCALL_DELETE:
    {
        task_t *cur = sched_current_task();
        char filename[256];
        if (!cur || copy_user_string(cur, arg1, filename, sizeof(filename)) < 0)
            return -14;
        int ret = vfs_delete(filename);
        if (ret == 0)
            return 0;
        fd_entry_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        if (vfs_opendir_entry(filename, &tmp) == 0) {
            vfs_closedir_entry(&tmp);
            return -21;
        }
        return -2;
    }

    case SYSCALL_RENAME:
    {
        task_t *cur = sched_current_task();
        char old_path[256], new_path[256];
        if (!cur || copy_user_string(cur, arg1, old_path, sizeof(old_path)) < 0 ||
            copy_user_string(cur, arg2, new_path, sizeof(new_path)) < 0)
            return -14;
        int ret = vfs_rename(old_path, new_path);
        if (ret == 0 || ret < -1)
            return ret;
        return vfs_stat(old_path) < 0 ? -2 : -5;
    }

    case SYSCALL_FTRUNCATE:
    {
        int fd = (int)arg1;
        uint32_t size = (uint32_t)arg2;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -9;
        if (fd < 0 || fd >= TASK_MAX_FDS)
            return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_FILE)
            return -9;
        return vfs_truncate_entry(e, size) < 0 ? -5 : 0;
    }

    case SYSCALL_FSYNC:
    {
        int fd = (int)arg1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -9;
        if (fd < 0 || fd >= TASK_MAX_FDS)
            return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_FILE)
            return -9;
        return vfs_sync_entry(e) < 0 ? -5 : 0;
    }

    case SYSCALL_STATFS:
    {
        task_t *cur = sched_current_task();
        char path[256];
        if (!cur || copy_user_string(cur, arg1, path, sizeof(path)) < 0)
            return -14;

        vfs_statfs_t st;
        memset(&st, 0, sizeof(st));
        if (vfs_statfs(path, &st) < 0)
            return -2;

        statfs_t out;
        memset(&out, 0, sizeof(out));
        out.f_type = st.f_type;
        out.f_bsize = st.f_bsize;
        out.f_blocks = st.f_blocks;
        out.f_bfree = st.f_bfree;
        out.f_bavail = st.f_bavail;
        out.f_files = st.f_files;
        out.f_ffree = st.f_ffree;
        out.f_fsid = st.f_fsid;
        out.f_namelen = st.f_namelen;
        out.f_frsize = st.f_frsize;
        out.f_flags = st.f_flags;
        return copy_to_user(cur, arg2, &out, sizeof(out));
    }

    case SYSCALL_FSTATFS:
    {
        int fd = (int)arg1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table || fd < 0 || fd >= TASK_MAX_FDS)
            return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used)
            return -9;

        statfs_t out;
        memset(&out, 0, sizeof(out));
        if (e->type == FD_FILE || e->type == FD_DIR)
        {
            uint64_t bsize = 0, blocks = 0, free_blocks = 0;
            if (fat_statfs_vol(0, &bsize, &blocks, &free_blocks) < 0)
                return -5;
            out.f_type = 0x4D44;
            out.f_bsize = bsize;
            out.f_frsize = bsize;
            out.f_blocks = blocks;
            out.f_bfree = free_blocks;
            out.f_bavail = free_blocks;
            out.f_fsid = 0;
            out.f_namelen = 255;
        }
        else if (e->type == FD_DEV || e->type == FD_PTY_MASTER || e->type == FD_PTY_SLAVE)
        {
            out.f_type = 0xDEAF;
            out.f_bsize = 4096;
            out.f_frsize = 4096;
            out.f_namelen = 255;
        }
        else
            return -95;
        return copy_to_user(cur, arg2, &out, sizeof(out));
    }

    case SYSCALL_PRCTL:
    {
        int option = (int)arg1;
        task_t *cur = sched_current_task();
        if (!cur)
            return -3;

        switch (option)
        {
        case ZEN_PR_GET_NAME:
        {
            char name[16];
            strncpy(name, cur->name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            return copy_to_user(cur, arg2, name, sizeof(name));
        }
        case ZEN_PR_SET_NAME:
        {
            char name[sizeof(cur->name)];
            if (copy_user_string(cur, arg2, name, sizeof(name)) < 0)
                return -14;
            strncpy(cur->name, name, sizeof(cur->name) - 1);
            cur->name[sizeof(cur->name) - 1] = '\0';
            return 0;
        }
        case ZEN_PR_GET_NO_NEW_PRIVS:
            return 0;
        case ZEN_PR_SET_NO_NEW_PRIVS:
            return 0;
        case ZEN_PR_CAPBSET_READ:
        case ZEN_PR_CAPBSET_DROP:
        case ZEN_PR_CAP_AMBIENT:
            return 0;
        default:
            return -38;
        }
    }

    case SYSCALL_SYSINFO:
    {
        task_t *current = sched_current_task();
        if (!current)
            return -14;
        sysinfo_t info;
        memset(&info, 0, sizeof(info));
        info.uptime = (int64_t)(hpet_monotonic_ns() / 1000000000ULL);
        info.totalram = get_total_memory();
        info.freeram = get_free_memory();
        info.mem_unit = 1;
        task_info_t tasks[64];
        info.procs = (uint16_t)sched_list_tasks(tasks, 64);
        return copy_to_user(current, arg1, &info, sizeof(info));
    }

    case SYSCALL_SCHED_GETAFFINITY:
    {
        pid_t pid = (pid_t)arg1;
        size_t cpusetsize = (size_t)arg2;
        task_t *current = sched_current_task();
        uint8_t mask[128] = {0};
        if (!current || cpusetsize == 0 || cpusetsize > sizeof(mask))
            return -22;
        if (pid < 0)
            return -3;
        uint32_t cpu_count = smp_cpu_count();
        if (cpu_count == 0)
            cpu_count = 1;
        size_t max_bits = cpusetsize * 8;
        for (uint32_t i = 0; i < cpu_count && i < max_bits; i++)
            mask[i / 8] |= (uint8_t)(1U << (i % 8));
        return copy_to_user(current, arg3, mask, cpusetsize);
    }

    case SYSCALL_SCHED_SETAFFINITY:
    {
        pid_t pid = (pid_t)arg1;
        size_t cpusetsize = (size_t)arg2;
        task_t *cur = sched_current_task();
        uint8_t mask[128];
        if (!cur || cpusetsize == 0 || cpusetsize > sizeof(mask))
            return -22;
        if (pid != 0 && (uint64_t)pid != cur->pid)
            return -38;
        if (copy_from_user(cur, mask, arg3, cpusetsize) < 0)
            return -14;

        uint32_t cpu_count = smp_cpu_count();
        if (cpu_count == 0)
            cpu_count = 1;
        int32_t selected = -1;
        for (uint32_t i = 0; i < cpu_count && i < cpusetsize * 8; i++)
        {
            if (mask[i / 8] & (uint8_t)(1U << (i % 8)))
            {
                selected = (int32_t)i;
                break;
            }
        }
        if (selected < 0)
            return -22;
        cur->pinned_cpu = selected;
        return 0;
    }

    case SYSCALL_GETUID:
    case SYSCALL_GETGID:
    case SYSCALL_GETEUID:
    case SYSCALL_GETEGID:
        return 0;

    case SYSCALL_STAT:
    {
        task_t *cur = sched_current_task();
        char path[256];
        stat_t st;
        if (!cur || copy_user_string(cur, arg1, path, sizeof(path)) < 0)
            return -14;
        memset(&st, 0, sizeof(st));
        if (unix_sock_path_exists(path))
        {
            st.st_mode = 0140666;
            st.st_nlink = 1;
            st.st_blksize = 512;
            return copy_to_user(cur, arg2, &st, sizeof(st));
        }
        fd_entry_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        int is_dir = 0;
        if (vfs_open_entry(path, 0, &tmp) < 0)
        {
            if (vfs_opendir_entry(path, &tmp) < 0)
                return -2;
            is_dir = 1;
        }
        uint32_t sz = is_dir ? 0 : vfs_size_entry(&tmp);
        int64_t mt = is_dir ? 0 : vfs_mtime_entry(&tmp);
        if (is_dir) vfs_closedir_entry(&tmp);
        else vfs_close_entry(&tmp);

        st.st_mode = is_dir ? 0040755 : 0100644;
        st.st_nlink = 1;
        st.st_size = sz;
        st.st_blksize = 512;
        st.st_blocks = (sz + 511) / 512;
        st.st_atime = mt;
        st.st_mtime = mt;
        st.st_ctime = mt;
        return copy_to_user(cur, arg2, &st, sizeof(st));
    }

    case SYSCALL_FSTAT:
    {
        int fd = (int)arg1;
        task_t *cur = sched_current_task();
        stat_t st;
        if (!cur || !cur->fd_table)
            return -9;
        memset(&st, 0, sizeof(st));
        if (fd == 0 || fd == 1 || fd == 2)
        {
            st.st_mode = 0020666;
            st.st_nlink = 1;
            st.st_blksize = 512;
            return copy_to_user(cur, arg2, &st, sizeof(st));
        }
        if (fd < 3 || fd >= TASK_MAX_FDS)
            return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used)
            return -9;
        if (e->type == FD_PTY_MASTER || e->type == FD_PTY_SLAVE ||
            e->type == FD_PIPE_READ || e->type == FD_PIPE_WRITE ||
            e->type == FD_DEV || e->type == FD_STDIO)
        {
            st.st_mode = 0020666;
            st.st_nlink = 1;
            st.st_blksize = 512;
        }
        else if (e->type == FD_UNIX_SOCK)
        {
            st.st_mode = 0140666;
            st.st_nlink = 1;
            st.st_blksize = 512;
        }
        else if (e->type == FD_DIR)
        {
            st.st_mode = 0040755;
            st.st_nlink = 1;
            st.st_blksize = 512;
        }
        else if (e->type == FD_FILE)
        {
            uint32_t sz = vfs_size_entry(e);
            int64_t mt = vfs_mtime_entry(e);
            st.st_mode = 0100644;
            st.st_nlink = 1;
            st.st_size = sz;
            st.st_blksize = 512;
            st.st_blocks = (sz + 511) / 512;
            st.st_atime = mt;
            st.st_mtime = mt;
            st.st_ctime = mt;
        }
        else
            return -9;
        return copy_to_user(cur, arg2, &st, sizeof(st));
    }

    case SYSCALL_CHDIR:
    {
        task_t *cur = sched_current_task();
        char path[256];
        if (!cur || copy_user_string(cur, arg1, path, sizeof(path)) < 0)
            return -14;
        if (vfs_chdir(path) == 0)
            return 0;
        return vfs_stat(path) < 0 ? -2 : -20;
    }

    case SYSCALL_GETCWD:
    {
        task_t *cur = sched_current_task();
        size_t size = (size_t)arg2;
        if (!cur || size == 0 || size > 4096)
            return -22;
        char buffer[4096];
        vfs_getcwd(buffer, size);
        return copy_to_user(cur, arg1, buffer, size);
    }

    case SYSCALL_MKDIR:
    {
        task_t *cur = sched_current_task();
        char path[256];
        if (!cur || copy_user_string(cur, arg1, path, sizeof(path)) < 0)
            return -14;
        if (vfs_mkdir(path) == 0)
            return 0;
        return vfs_stat(path) == 0 ? -17 : -2;
    }

    case SYSCALL_RMDIR:
    {
        task_t *cur = sched_current_task();
        char path[256];
        if (!cur || copy_user_string(cur, arg1, path, sizeof(path)) < 0)
            return -14;
        if (vfs_rmdir(path) == 0)
            return 0;
        if (vfs_stat(path) < 0)
            return -2;
        fd_entry_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        if (vfs_open_entry(path, 0, &tmp) == 0) {
            vfs_close_entry(&tmp);
            return -20;
        }
        return -39;
    }

    case SYSCALL_BRK:
    {
        uint64_t new_brk = arg1;
        task_t *current = sched_current_task();
        if (!current || !current->pml4)
            return -1;

        if (new_brk < USER_HEAP_START || new_brk >= USER_MMAP_START ||
            new_brk > UINT64_MAX - (PAGE_SIZE - 1) ||
            current->heap_brk < USER_HEAP_START ||
            current->heap_brk >= USER_MMAP_START)
            return -1;

        uint64_t old_brk = current->heap_brk;

        if (new_brk > current->heap_brk)
        {
            uint64_t start = (current->heap_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t end = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t rollback_end = start;

            for (uint64_t virt = start; virt < end; virt += PAGE_SIZE)
            {
                uint64_t phys = alloc_page();
                if (!phys)
                {
                    user_unmap_range(current, start, (size_t)((rollback_end - start) / PAGE_SIZE));
                    return -1;
                }
                if (map_user_page_checked(current->pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0)
                {
                    free_page(phys);
                    user_unmap_range(current, start, (size_t)((rollback_end - start) / PAGE_SIZE));
                    return -1;
                }
                rollback_end = virt + PAGE_SIZE;
            }
        }
        else if (new_brk < current->heap_brk)
        {
            uint64_t start = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t end = (current->heap_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (end > start)
                user_unmap_range(current, start, (size_t)((end - start) / PAGE_SIZE));
        }

        current->heap_brk = new_brk;
        return old_brk;
    }

    case SYSCALL_SBRK:
    {
        int64_t increment = (int64_t)arg1;
        task_t *current = sched_current_task();
        if (!current || !current->pml4)
            return -1;

        uint64_t old_brk = current->heap_brk;
        if (old_brk < USER_HEAP_START || old_brk >= USER_MMAP_START)
            return -1;

        uint64_t new_brk;
        if (increment > 0) {
            uint64_t delta = (uint64_t)increment;
            if (delta >= USER_MMAP_START - old_brk)
                return -1;
            new_brk = old_brk + delta;
        } else if (increment < 0) {
            uint64_t delta = (uint64_t)(-(increment + 1)) + 1;
            if (delta > old_brk - USER_HEAP_START)
                return -1;
            new_brk = old_brk - delta;
        } else {
            new_brk = old_brk;
        }

        if (new_brk > UINT64_MAX - (PAGE_SIZE - 1))
            return -1;

        if (increment > 0)
        {
            uint64_t start = (current->heap_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t end = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t rollback_end = start;

            for (uint64_t virt = start; virt < end; virt += PAGE_SIZE)
            {
                uint64_t phys = alloc_page();
                if (!phys)
                {
                    user_unmap_range(current, start, (size_t)((rollback_end - start) / PAGE_SIZE));
                    return -1;
                }
                if (map_user_page_checked(current->pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0)
                {
                    free_page(phys);
                    user_unmap_range(current, start, (size_t)((rollback_end - start) / PAGE_SIZE));
                    return -1;
                }
                rollback_end = virt + PAGE_SIZE;
            }
        }
        else if (increment < 0)
        {
            uint64_t start = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t end = (current->heap_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (end > start)
                user_unmap_range(current, start, (size_t)((end - start) / PAGE_SIZE));
        }

        current->heap_brk = new_brk;
        return old_brk;
    }

    case SYSCALL_MMAP:
    {
        void *addr = (void *)arg1;
        size_t length = (size_t)arg2;
        int prot = (int)arg3;
        int flags = (int)arg4;

        task_t *current = sched_current_task();
        if (!current || !current->pml4 || length == 0 ||
            length > SIZE_MAX - (PAGE_SIZE - 1))
            return -1;

        size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
        if (pages == 0 ||
            pages > (USER_MMAP_END - USER_MMAP_START) / PAGE_SIZE)
            return -1;
        uint64_t virt_start = 0;
        uint64_t page_flags = user_page_flags_from_prot(prot);

        if (flags & 0x10)
        {
            if (!addr || ((uint64_t)addr & (PAGE_SIZE - 1)))
                return -1;
            virt_start = (uint64_t)addr;
            uint64_t map_size = (uint64_t)pages * PAGE_SIZE;
            if (virt_start < USER_MMAP_START || virt_start >= USER_MMAP_END ||
                map_size > USER_MMAP_END - virt_start)
                return -1;
            user_unmap_range(current, virt_start, pages);
        }
        else
        {
            virt_start = user_find_mmap_range(current, (uint64_t)addr, pages);
            if (!virt_start)
                return -1;
        }

        size_t mapped_pages = 0;
        for (size_t i = 0; i < pages; i++)
        {
            uint64_t virt = virt_start + (i * PAGE_SIZE);
            uint64_t phys = alloc_page();
            if (!phys)
            {
                user_unmap_range(current, virt_start, mapped_pages);
                return -1;
            }
            memset((void *)(phys + KERNEL_VIRT_OFFSET), 0, PAGE_SIZE);
            if (map_user_page_checked(current->pml4, virt, phys, page_flags) < 0)
            {
                free_page(phys);
                user_unmap_range(current, virt_start, mapped_pages);
                return -1;
            }
            mapped_pages++;
        }

        if (!(flags & 0x10))
            current->mmap_base = virt_start + (uint64_t)pages * PAGE_SIZE;
        return virt_start;
    }

    case SYSCALL_MUNMAP:
    {
        void *addr = (void *)arg1;
        size_t length = (size_t)arg2;

        task_t *current = sched_current_task();
        if (!current || !current->pml4)
            return -1;

        if (((uint64_t)addr & (PAGE_SIZE - 1)) || length == 0 ||
            length > SIZE_MAX - (PAGE_SIZE - 1))
            return -1;

        uint64_t virt_start = (uint64_t)addr;
        size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
        if (pages == 0 ||
            pages > (USER_MMAP_END - USER_MMAP_START) / PAGE_SIZE ||
            virt_start < USER_MMAP_START || virt_start >= USER_MMAP_END ||
            (uint64_t)pages * PAGE_SIZE > USER_MMAP_END - virt_start)
            return -1;

        user_unmap_range(current, virt_start, pages);
        return 0;
    }

    case SYSCALL_MPROTECT:
    {
        void *addr = (void *)arg1;
        size_t length = (size_t)arg2;
        int prot = (int)arg3;

        task_t *current = sched_current_task();
        if (!current || !current->pml4 ||
            ((uint64_t)addr & (PAGE_SIZE - 1)) || length == 0 ||
            length > SIZE_MAX - (PAGE_SIZE - 1))
            return -1;

        uint64_t virt_start = (uint64_t)addr;
        size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
        if (pages == 0 ||
            pages > (USER_MMAP_END - USER_MMAP_START) / PAGE_SIZE ||
            virt_start < USER_MMAP_START || virt_start >= USER_MMAP_END ||
            (uint64_t)pages * PAGE_SIZE > USER_MMAP_END - virt_start)
            return -1;
        uint64_t page_flags = user_page_flags_from_prot(prot);

        for (size_t i = 0; i < pages; i++)
        {
            if (protect_page(current->pml4, virt_start + i * PAGE_SIZE, page_flags) < 0)
                return -1;
        }

        return 0;
    }

    case SYSCALL_GETTIMEOFDAY:
    {
        task_t *current = sched_current_task();
        timeval_t tv;
        if (!current)
            return -14;

        static const uint16_t days_before_month[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
        rtc_time_t t = rtc_get_time();
        if (t.month < 1 || t.month > 12 || t.day < 1)
            return -5;
        uint32_t year = 2000 + t.year;
        uint32_t yday = days_before_month[t.month - 1] + t.day - 1;
        if (t.month > 2 && (year % 4 == 0))
            yday++;
        uint32_t days = (year - 1970) * 365 + (year - 1969) / 4 + yday;
        tv.tv_sec  = (uint64_t)days * 86400ULL + t.hours * 3600ULL + t.minutes * 60ULL + t.seconds;
        tv.tv_usec = (uint64_t)t.milliseconds * 1000ULL;
        return copy_to_user(current, arg1, &tv, sizeof(tv));
    }

    case SYSCALL_CLOCK_GETTIME:
    {
        int clk_id = (int)arg1;
        task_t *current = sched_current_task();
        timespec_t tp;
        if (!current)
            return -14;

        if (clk_id == 0 || clk_id == 5 || clk_id == 11)
        {
            static const uint16_t days_before_month[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
            rtc_time_t t = rtc_get_time();
            if (t.month < 1 || t.month > 12 || t.day < 1)
                return -5;
            uint32_t year = 2000 + t.year;
            uint32_t yday = days_before_month[t.month - 1] + t.day - 1;
            if (t.month > 2 && (year % 4 == 0))
                yday++;
            uint32_t days = (year - 1970) * 365 + (year - 1969) / 4 + yday;
            tp.tv_sec  = (uint64_t)days * 86400ULL + t.hours * 3600ULL + t.minutes * 60ULL + t.seconds;
            tp.tv_nsec = (uint64_t)t.milliseconds * 1000000ULL;
        }
        else
        {
            uint64_t ns = hpet_monotonic_ns();
            tp.tv_sec  = ns / 1000000000ULL;
            tp.tv_nsec = ns % 1000000000ULL;
        }
        return copy_to_user(current, arg2, &tp, sizeof(tp));
    }

    case SYSCALL_NANOSLEEP:
    {
        task_t *current = sched_current_task();
        timespec_t req;
        if (!current || copy_from_user(current, &req, arg1, sizeof(req)) < 0)
            return -14;
        if (req.tv_nsec >= 1000000000ULL)
            return -22;
        if (req.tv_sec > UINT32_MAX / 1000ULL)
            return -22;
        uint64_t ms64 = req.tv_sec * 1000ULL + req.tv_nsec / 1000000ULL;
        if (ms64 > UINT32_MAX)
            return -22;
        sleep_with_scheduler((uint32_t)ms64);
        return 0;
    }

    case SYSCALL_SLEEP:
    {
        uint32_t ms = (uint32_t)arg1;
        sleep_with_scheduler(ms);
        return 0;
    }

    case SYSCALL_SOCKET_CREATE:
    {
        const char *name = (const char *)arg1;
        if (!name)
            return -1;
        return socket_create(name);
    }

    case SYSCALL_SOCKET_OPEN:
    {
        const char *name = (const char *)arg1;
        socket_file_t **file = (socket_file_t **)arg2;
        if (!name || !file)
            return -1;
        return socket_open(name, file);
    }

    case SYSCALL_SOCKET_READ:
    {
        socket_file_t *file = (socket_file_t *)arg1;
        void *buffer = (void *)arg2;
        uint32_t size = (uint32_t)arg3;
        uint32_t *bytes_read = (uint32_t *)arg4;
        if (!file || !buffer)
            return -1;
        return socket_read(file, buffer, size, bytes_read);
    }

    case SYSCALL_SOCKET_WRITE:
    {
        socket_file_t *file = (socket_file_t *)arg1;
        const void *buffer = (const void *)arg2;
        uint32_t size = (uint32_t)arg3;
        if (!file || !buffer)
            return -1;
        return socket_write(file, buffer, size);
    }

    case SYSCALL_SOCKET_CLOSE:
    {
        socket_file_t *file = (socket_file_t *)arg1;
        if (!file)
            return -1;
        return socket_close(file);
    }

    case SYSCALL_SOCKET_DELETE:
    {
        const char *name = (const char *)arg1;
        if (!name)
            return -1;
        return socket_delete(name);
    }

    case SYSCALL_SOCKET_EXISTS:
    {
        const char *name = (const char *)arg1;
        if (!name)
            return -1;
        return socket_exists(name) ? 1 : 0;
    }

    case SYSCALL_SOCKET_AVAILABLE:
    {
        socket_file_t *file = (socket_file_t *)arg1;
        if (!file)
            return -1;
        return socket_available(file);
    }

    case SYSCALL_UNAME:
    {
        task_t *current = sched_current_task();
        if (!current)
            return -14;
        utsname_t value;
        memset(&value, 0, sizeof(value));
        strncpy(value.sysname, "ZenOS", sizeof(value.sysname) - 1);
        strncpy(value.nodename, "Zen", sizeof(value.nodename) - 1);
        size_t version_len = 0;
        while (os_version[version_len] && os_version[version_len] != ' ' &&
               version_len < sizeof(value.release) - 1)
        {
            value.release[version_len] = os_version[version_len];
            version_len++;
        }
        if (os_version[version_len] == ' ')
            strncpy(value.version, os_version + version_len + 1, sizeof(value.version) - 1);
        strncpy(value.machine, "x86_64", sizeof(value.machine) - 1);
        return copy_to_user(current, arg1, &value, sizeof(value));
    }

    case SYSCALL_LOG:
    {
        task_t *cur = sched_current_task();
        char msg[256];
        uint32_t level = (uint32_t)arg2;
        uint32_t visibility = (uint32_t)arg3;
        if (!cur || level > 4 || copy_user_string(cur, arg1, msg, sizeof(msg)) < 0)
            return -14;
        log("%s", level, visibility, msg);
        return 0;
    }

    case SYSCALL_SHUTDOWN:
    {
        shutdown();
        for (;;)
        {
            __asm__ __volatile__("sti; hlt; cli");
        }
    }

    case SYSCALL_REBOOT:
    {
        AcpiReboot();
        for (;;)
        {
            __asm__ __volatile__("cli; hlt");
        }
    }

    case SYSCALL_GET_FRAMEBUFFER:
    {
        task_t *current = sched_current_task();
        if (!current || !current->pml4)
            return -1;

        uint64_t fb_phys = virt_to_phys(get_kernel_pml4(), (uint64_t)framebuffer_addr);
        uint64_t fb_size = (uint64_t)framebuffer_pitch * framebuffer_height;
        if (!fb_phys || !fb_size)
            return -1;

        uint64_t fb_offset = fb_phys & (PAGE_SIZE - 1);
        uint64_t fb_page_phys = fb_phys & ~(uint64_t)(PAGE_SIZE - 1);
        if (fb_size > UINT64_MAX - fb_offset - (PAGE_SIZE - 1))
            return -1;
        size_t pages = (size_t)((fb_offset + fb_size + PAGE_SIZE - 1) / PAGE_SIZE);
        const uint64_t user_fb_vaddr = 0x0000600000000000ULL;
        if (!pages || pages > (0x0000800000000000ULL - user_fb_vaddr) / PAGE_SIZE)
            return -1;

        size_t mapped = 0;
        for (; mapped < pages; mapped++)
        {
            uint64_t virt = user_fb_vaddr + mapped * PAGE_SIZE;
            uint64_t phys = fb_page_phys + mapped * PAGE_SIZE;
            map_page(current->pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            if (virt_to_phys(current->pml4, virt) != phys)
                break;
        }
        if (mapped != pages)
        {
            while (mapped > 0)
            {
                mapped--;
                unmap_page(current->pml4, user_fb_vaddr + mapped * PAGE_SIZE);
            }
            return -1;
        }

        fb_info_t value;
        value.addr = user_fb_vaddr + fb_offset;
        value.width = framebuffer_width;
        value.height = framebuffer_height;
        value.bpp = framebuffer_bpp;
        value.pitch = framebuffer_pitch;
        return copy_to_user(current, arg1, &value, sizeof(value));
    }

    case SYSCALL_IS_FOCUSED:
    {
        return 1;
    }

    case SYSCALL_KILL:
    {
        uint64_t pid = arg1;
        int sig = (int)arg2;
        if (sig == 0)
            return 0;
        return sched_signal(pid, sig);
    }

    case SYSCALL_WAIT_PID:
    {
        int64_t pid = (int64_t)arg1;
        int *wstatus = (int *)arg2;
        int options = (int)arg3;
        return sched_wait_pid(pid, wstatus, options);
    }

    case SYSCALL_LIST_TASKS:
    {
        task_t *current = sched_current_task();
        uint32_t max_count = (uint32_t)arg2;
        if (!current)
            return -14;
        if (max_count > 64)
            max_count = 64;
        if (max_count == 0)
            return 0;
        task_info_t infos[64];
        uint32_t count = sched_list_tasks(infos, max_count);
        if (copy_to_user(current, arg1, infos, (size_t)count * sizeof(infos[0])) < 0)
            return -14;
        return count;
    }

    case SYSCALL_FORK:
    {
        return (uint64_t)sched_fork(arg5);
    }

    case SYSCALL_PIPE:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;

        int user_fds[2];
        int rfd = fd_alloc(cur->fd_table);
        if (rfd < 0)
            return -24;

        int wfd = fd_alloc(cur->fd_table);
        if (wfd < 0)
        {
            memset(&cur->fd_table->entries[rfd], 0, sizeof(fd_entry_t));
            return -24;
        }

        pipe_buf_t *pb = (pipe_buf_t *)kmalloc(sizeof(pipe_buf_t));
        if (!pb)
        {
            memset(&cur->fd_table->entries[rfd], 0, sizeof(fd_entry_t));
            memset(&cur->fd_table->entries[wfd], 0, sizeof(fd_entry_t));
            return -12;
        }

        memset(pb, 0, sizeof(pipe_buf_t));
        spinlock_init(&pb->lock);
        pb->readers = 1;
        pb->writers = 1;
        pb->refcount = 2;

        fd_entry_t *re = &cur->fd_table->entries[rfd];
        fd_entry_t *we = &cur->fd_table->entries[wfd];
        memset(re, 0, sizeof(fd_entry_t));
        memset(we, 0, sizeof(fd_entry_t));
        re->type = FD_PIPE_READ;
        re->used = 1;
        re->pipe = pb;
        we->type = FD_PIPE_WRITE;
        we->used = 1;
        we->pipe = pb;

        user_fds[0] = rfd;
        user_fds[1] = wfd;
        if (copy_to_user(cur, arg1, user_fds, sizeof(user_fds)) < 0)
        {
            memset(re, 0, sizeof(*re));
            memset(we, 0, sizeof(*we));
            kfree(pb);
            return -14;
        }
        return 0;
    }

    case SYSCALL_DUP:
    {
        int oldfd = (int)arg1;
        int minfd = (int)arg2;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -9;
        if (minfd < 0)
            return -22;
        if (oldfd < 0 || oldfd >= TASK_MAX_FDS)
            return -9;
        fd_entry_t *src = &cur->fd_table->entries[oldfd];
        if (!src->used && oldfd >= 3)
            return -9;
        int nfd = fd_alloc_from(cur->fd_table, minfd);
        if (nfd < 0)
            return -24;
        if (!src->used)
        {
            fd_entry_t *dst = &cur->fd_table->entries[nfd];
            memset(dst, 0, sizeof(*dst));
            dst->type = FD_STDIO;
            dst->used = 1;
            dst->stdio_fd = oldfd;
        }
        else if (fd_entry_clone(&cur->fd_table->entries[nfd], src, 0) < 0)
        {
            return -9;
        }
        return nfd;
    }

    case SYSCALL_DUP2:
    {
        int oldfd = (int)arg1;
        int newfd = (int)arg2;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -9;
        if (oldfd < 0 || oldfd >= TASK_MAX_FDS)
            return -9;
        if (newfd < 0 || newfd >= TASK_MAX_FDS)
            return -9;
        fd_entry_t *src = &cur->fd_table->entries[oldfd];
        if (!src->used && oldfd >= 3)
            return -9;
        if (oldfd == newfd)
            return newfd;
        fd_entry_t *dst = &cur->fd_table->entries[newfd];
        if (dst->used)
            fd_close(cur->fd_table, newfd);
        if (!src->used)
        {
            memset(dst, 0, sizeof(*dst));
            dst->type = FD_STDIO;
            dst->used = 1;
            dst->stdio_fd = oldfd;
        }
        else if (fd_entry_clone(dst, src, 0) < 0)
        {
            return -9;
        }
        return newfd;
    }

    case SYSCALL_OPENDIR:
    {
        task_t *cur = sched_current_task();
        char path[256];
        if (!cur || !cur->fd_table)
            return -9;
        if (copy_user_string(cur, arg1, path, sizeof(path)) < 0)
            return -14;
        if (vfs_stat(path) < 0)
            return -2;
        int fd = fd_alloc(cur->fd_table);
        if (fd < 0)
            return -24;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (vfs_opendir_entry(path, e) < 0)
            return -20;
        return fd;
    }

    case SYSCALL_READDIR:
    {
        int fd = (int)arg1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -9;
        if (fd < 3 || fd >= TASK_MAX_FDS)
            return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_DIR)
            return -20;
        zen_dirent_t dent;
        int is_dir = 0;
        int ret = vfs_readdir_entry(e, dent.d_name, &is_dir);
        if (ret <= 0)
            return ret;
        dent.d_ino = 1;
        dent.d_type = is_dir ? 4 : 8;
        if (copy_to_user(cur, arg2, &dent, sizeof(dent)) < 0)
            return -14;
        return 1;
    }

    case SYSCALL_CLOSEDIR:
    {
        int fd = (int)arg1;
        if (fd < 3 || fd >= TASK_MAX_FDS)
            return -9;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_DIR)
            return -20;
        vfs_closedir_entry(e);
        memset(e, 0, sizeof(fd_entry_t));
        return 0;
    }

    case SYSCALL_SIGACTION:
    {
        int sig = (int)arg1;
        task_t *cur = sched_current_task();
        zen_sigaction_t new_action;
        if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP || !cur)
            return -1;
        if (arg2 && copy_from_user(cur, &new_action, arg2, sizeof(new_action)) < 0)
            return -14;
        if (arg3 && copy_to_user(cur, arg3, &cur->sighandlers[sig], sizeof(cur->sighandlers[sig])) < 0)
            return -14;
        if (arg2)
            cur->sighandlers[sig] = new_action;
        return 0;
    }

    case SYSCALL_SIGRETURN:
    {
        task_t *cur = sched_current_task();
        if (!cur)
            return 0;
        uint64_t usp = cur->regs.userrsp;
        uint64_t saved_rip = 0;
        if (copy_from_user(cur, &saved_rip, usp, sizeof(saved_rip)) == 0)
        {
            cur->regs.userrsp += sizeof(uint64_t);
            cur->regs.rip = saved_rip;
        }
        else
        {
            cur->state = TASK_DEAD;
            cur->exit_code = 128 + SIGSEGV;
        }
        dispatch_pending_signals(cur);
        return 0;
    }

    case SYSCALL_SIGPROCMASK:
    {
        int how = (int)arg1;
        task_t *cur = sched_current_task();
        uint64_t set_value = 0;
        if (!cur || ((how < 0 || how > 2) && arg2))
            return -1;
        if (arg2 && copy_from_user(cur, &set_value, arg2, sizeof(set_value)) < 0)
            return -14;
        if (arg3 && copy_to_user(cur, arg3, &cur->sig_mask, sizeof(cur->sig_mask)) < 0)
            return -14;
        if (arg2)
        {
            if (how == 0)
                cur->sig_mask |= set_value;
            else if (how == 1)
                cur->sig_mask &= ~set_value;
            else
                cur->sig_mask = set_value;
        }
        return 0;
    }

    case SYSCALL_HALT:
    {
        sched_yield();
        return 0;
    }

    case SYSCALL_SOCKET:
    {
        int domain = (int)arg1;
        int type = (int)arg2;
        int protocol = (int)arg3;
        if (domain != 2 || type != 1)
            return (uint64_t)(int64_t)-97;
        if (protocol != 0 && protocol != 6)
            return (uint64_t)(int64_t)-93;
        return (uint64_t)(int64_t)tcp_socket();
    }

    case SYSCALL_CONNECT:
    {
        int id = (int)arg1;
        uint32_t addrlen = (uint32_t)arg3;
        zen_sockaddr_in_t addr;
        task_t *cur = sched_current_task();
        if (!cur || !arg2 || addrlen < sizeof(addr))
            return (uint64_t)(int64_t)-22;
        if (copy_from_user(cur, &addr, arg2, sizeof(addr)) < 0)
            return (uint64_t)(int64_t)-14;
        if (addr.sin_family != 2)
            return (uint64_t)(int64_t)-97;
        return (uint64_t)(int64_t)tcp_connect_socket(id, addr.sin_addr, ntohs(addr.sin_port));
    }

    case SYSCALL_SEND:
    {
        int id = (int)arg1;
        size_t len = (size_t)arg3;
        task_t *cur = sched_current_task();
        if (!cur || !arg2 || len == 0 || len > UNIX_BUF_SIZE)
            return (uint64_t)(int64_t)-22;
        void *buffer = kmalloc(len);
        if (!buffer)
            return (uint64_t)(int64_t)-12;
        if (copy_from_user(cur, buffer, arg2, len) < 0)
        {
            kfree(buffer);
            return (uint64_t)(int64_t)-14;
        }
        int result = tcp_send(id, buffer, len);
        kfree(buffer);
        return (uint64_t)(int64_t)result;
    }

    case SYSCALL_RECV:
    {
        int id = (int)arg1;
        size_t len = (size_t)arg3;
        task_t *cur = sched_current_task();
        if (!cur || !arg2 || len == 0 || len > UNIX_BUF_SIZE)
            return (uint64_t)(int64_t)-22;
        if (user_copy_validate(cur, arg2, len, 1) < 0)
            return (uint64_t)(int64_t)-14;
        void *buffer = kmalloc(len);
        if (!buffer)
            return (uint64_t)(int64_t)-12;
        int result = tcp_recv(id, buffer, len);
        if (result > 0 && copy_to_user(cur, arg2, buffer, (size_t)result) < 0)
            result = -14;
        kfree(buffer);
        return (uint64_t)(int64_t)result;
    }

    case SYSCALL_CLOSESOCKET:
    {
        int id = (int)arg1;
        tcp_close(id);
        return 0;
    }

    case SYSCALL_POLL:
    {
        size_t count = (size_t)arg2;
        int timeout = (int)arg3;
        task_t *current = sched_current_task();
        if (count > 1024 || (!arg1 && count))
            return -22;

        pollfd_t local_fds[1024];
        if (count && copy_from_user(current, local_fds, arg1,
                                    count * sizeof(local_fds[0])) < 0)
            return -14;

        int ready = poll_fds_once(local_fds, count);
        if (!ready && timeout != 0)
        {
            if (timeout > 0)
                sleep_ms((uint32_t)(timeout > 10 ? 10 : timeout));
            else
                sched_yield();
            ready = poll_fds_once(local_fds, count);
        }

        if (count && copy_to_user(current, arg1, local_fds,
                                  count * sizeof(local_fds[0])) < 0)
            return -14;
        return ready;
    }

    case SYSCALL_GETHOSTBYNAME:
    {
        task_t *current = sched_current_task();
        char hostname[256];
        uint8_t ip_out[4];
        if (!current || copy_user_string(current, arg1, hostname, sizeof(hostname)) < 0)
            return -14;
        int result = dns_resolve(hostname, ip_out);
        if (result < 0)
            return (uint64_t)(int64_t)result;
        if (copy_to_user(current, arg2, ip_out, sizeof(ip_out)) < 0)
            return -14;
        return result;
    }

    case SYSCALL_FUTEX:
    {
        task_t *current = sched_current_task();
        uint64_t uaddr = arg1;
        int op = (int)arg2;
        uint32_t val = (uint32_t)arg3;

        if (!current || (uaddr & (sizeof(uint32_t) - 1)) ||
            (op != FUTEX_WAIT && op != FUTEX_WAKE) ||
            user_copy_validate(current, uaddr, sizeof(uint32_t), 0) < 0)
            return (uint64_t)(int64_t)-14;

        if (op == FUTEX_WAIT)
            return (uint64_t)(int64_t)sched_futex_wait((volatile uint32_t *)(uintptr_t)uaddr, val);
        return (uint64_t)(int64_t)sched_futex_wake((volatile uint32_t *)(uintptr_t)uaddr, val);
    }

    case SYSCALL_PTY_OPEN:
    {
       
        int *slave_fd_out = (int *)(uintptr_t)arg1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table || !slave_fd_out)
            return (uint64_t)(int64_t)-1;

        pty_buf_t *pty = (pty_buf_t *)kmalloc(sizeof(pty_buf_t));
        if (!pty) return (uint64_t)(int64_t)-1;
        memset(pty, 0, sizeof(pty_buf_t));

        pty->ws_rows   = 24;
        pty->ws_cols   = 80;
        pty->ws_xpixel = 0;
        pty->ws_ypixel = 0;
        pty->master_open = 1;
        pty->master_refs = 1;
        pty->slave_open  = 1;
        pty->slave_refs  = 1;
        pty->refcount    = 2;
        pty_defaults(pty);

        int mfd = fd_alloc(cur->fd_table);
        if (mfd < 0) { kfree(pty); return (uint64_t)(int64_t)-1; }
        cur->fd_table->entries[mfd].used = 1;

        int sfd = fd_alloc(cur->fd_table);
        if (sfd < 0)
        {
            cur->fd_table->entries[mfd].used = 0;
            kfree(pty);
            return (uint64_t)(int64_t)-1;
        }

        fd_entry_t *me = &cur->fd_table->entries[mfd];
        memset(me, 0, sizeof(*me));
        me->used = 1;
        me->type = FD_PTY_MASTER;
        me->pty = pty;

        fd_entry_t *se = &cur->fd_table->entries[sfd];
        memset(se, 0, sizeof(*se));
        se->used = 1;
        se->type = FD_PTY_SLAVE;
        se->pty = pty;

        if (copy_to_user(cur, arg1, &sfd, sizeof(sfd)) < 0)
        {
            fd_close(cur->fd_table, mfd);
            fd_close(cur->fd_table, sfd);
            return (uint64_t)(int64_t)-14;
        }
        return (uint64_t)(unsigned int)mfd;
    }

    case SYSCALL_IOCTL:
    {
        int fd          = (int)arg1;
        unsigned long req = (unsigned long)arg2;
        void *argp      = (void *)(uintptr_t)arg3;

        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return (uint64_t)(int64_t)-1;

        fd_entry_t *e = NULL;
        if (fd >= 0 && fd < TASK_MAX_FDS && cur->fd_table->entries[fd].used)
            e = &cur->fd_table->entries[fd];

        if (!e)
        {
            if (fd == 0 || fd == 1 || fd == 2)
                return (uint64_t)console_ioctl_fallback(req, argp);
            return (uint64_t)(int64_t)-1;
        }

        if (e->type == FD_PTY_MASTER || e->type == FD_PTY_SLAVE)
        {
            pty_buf_t *pty = e->pty;
            if ((req == ZEN_TCGETS || req == ZEN_TCSETS || req == ZEN_TCSETSW || req == ZEN_TCSETSF) && !argp)
                return (uint64_t)(int64_t)-1;
            if (req == ZEN_TCGETS)
            {
                zen_termios_t value;
                pty_export_termios(pty, &value);
                return copy_to_user(cur, arg3, &value, sizeof(value));
            }
            if (req == ZEN_TCSETS || req == ZEN_TCSETSW || req == ZEN_TCSETSF)
            {
                zen_termios_t value;
                if (copy_from_user(cur, &value, arg3, sizeof(value)) < 0)
                    return -14;
                pty_import_termios(pty, &value, req == ZEN_TCSETSF);
                return 0;
            }
            if (req == ZEN_TIOCGWINSZ)
            {
                zen_winsize_t value = {
                    .ws_row = pty->ws_rows,
                    .ws_col = pty->ws_cols,
                    .ws_xpixel = pty->ws_xpixel,
                    .ws_ypixel = pty->ws_ypixel
                };
                return copy_to_user(cur, arg3, &value, sizeof(value));
            }
            if (req == ZEN_TIOCSWINSZ)
            {
                zen_winsize_t value;
                if (copy_from_user(cur, &value, arg3, sizeof(value)) < 0)
                    return -14;
                pty->ws_rows   = value.ws_row;
                pty->ws_cols   = value.ws_col;
                pty->ws_xpixel = value.ws_xpixel;
                pty->ws_ypixel = value.ws_ypixel;
                return 0;
            }
            if (req == ZEN_FIONREAD)
            {
                int value;
                if (e->type == FD_PTY_MASTER)
                    value = (int)pty->s2m_count;
                else if (pty->lflag & ZEN_ICANON)
                    value = (int)pty->m2s_ready;
                else
                    value = (int)pty->m2s_count;
                return copy_to_user(cur, arg3, &value, sizeof(value));
            }
            if (req == ZEN_TIOCSPTYGID)
                return 0;
            return 0;
        }
        if (e->type == FD_DEV)
        {
            struct dev_entry *d = e->dev_ops;
            if (!d || !d->ioctl)
                return (uint64_t)(int64_t)-1;
            return (uint64_t)(int64_t)d->ioctl(req, argp);
        }
        return 0;
    }

    case SYSCALL_SHM_CREATE:
    {
        task_t *current = sched_current_task();
        char name[SHM_NAME_MAX];
        size_t size = (size_t)arg2;
        if (!current || copy_user_string(current, arg1, name, sizeof(name)) < 0 ||
            size == 0 || size > SIZE_MAX - (PAGE_SIZE - 1))
            return -14;

        size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        spinlock_acquire(&shm_lock);

        for (int i = 0; i < SHM_MAX; i++) {
            if (shm_table[i].in_use && strcmp(shm_table[i].name, name) == 0) {
                spinlock_release(&shm_lock);
                return -1;
            }
        }

        int slot = -1;
        for (int i = 0; i < SHM_MAX; i++) {
            if (!shm_table[i].in_use) { slot = i; break; }
        }
        if (slot < 0) {
            spinlock_release(&shm_lock);
            return -1;
        }

        uint64_t phys = alloc_pages(pages);
        if (!phys) {
            spinlock_release(&shm_lock);
            return -1;
        }

        if (!current->pml4) {
            free_pages(phys, pages);
            spinlock_release(&shm_lock);
            return -1;
        }

        uint64_t virt = SHM_VIRT_BASE + (uint64_t)slot * 0x10000000ULL;
        size_t mapped = 0;
        for (; mapped < pages; mapped++) {
            if (map_user_page_checked(current->pml4, virt + mapped * PAGE_SIZE,
                                      phys + mapped * PAGE_SIZE,
                                      PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0)
                break;
        }
        if (mapped != pages) {
            for (size_t i = 0; i < mapped; i++)
                unmap_page(current->pml4, virt + i * PAGE_SIZE);
            free_pages(phys, pages);
            spinlock_release(&shm_lock);
            return -1;
        }

        shm_info_t info = { .addr = virt, .size = pages * PAGE_SIZE };
        if (copy_to_user(current, arg3, &info, sizeof(info)) < 0) {
            for (size_t i = 0; i < mapped; i++)
                unmap_page(current->pml4, virt + i * PAGE_SIZE);
            free_pages(phys, pages);
            spinlock_release(&shm_lock);
            return -14;
        }

        strncpy(shm_table[slot].name, name, SHM_NAME_MAX - 1);
        shm_table[slot].name[SHM_NAME_MAX - 1] = '\0';
        shm_table[slot].phys   = phys;
        shm_table[slot].pages  = pages;
        shm_table[slot].refs   = 1;
        shm_table[slot].in_use = true;

        spinlock_release(&shm_lock);
        return 0;
    }

    case SYSCALL_SHM_OPEN:
    {
        task_t *current = sched_current_task();
        char name[SHM_NAME_MAX];
        if (!current || copy_user_string(current, arg1, name, sizeof(name)) < 0)
            return -14;

        spinlock_acquire(&shm_lock);
        int slot = -1;
        for (int i = 0; i < SHM_MAX; i++) {
            if (shm_table[i].in_use && strcmp(shm_table[i].name, name) == 0)
                { slot = i; break; }
        }
        if (slot < 0) {
            spinlock_release(&shm_lock);
            return -1;
        }

        if (!current->pml4) {
            spinlock_release(&shm_lock);
            return -1;
        }

        uint64_t virt = SHM_VIRT_BASE + (uint64_t)slot * 0x10000000ULL;
        size_t mapped = 0;
        for (; mapped < shm_table[slot].pages; mapped++) {
            if (map_user_page_checked(current->pml4, virt + mapped * PAGE_SIZE,
                                      shm_table[slot].phys + mapped * PAGE_SIZE,
                                      PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0)
                break;
        }
        if (mapped != shm_table[slot].pages) {
            for (size_t i = 0; i < mapped; i++)
                unmap_page(current->pml4, virt + i * PAGE_SIZE);
            spinlock_release(&shm_lock);
            return -1;
        }

        shm_info_t info = { .addr = virt, .size = shm_table[slot].pages * PAGE_SIZE };
        if (copy_to_user(current, arg2, &info, sizeof(info)) < 0) {
            for (size_t i = 0; i < mapped; i++)
                unmap_page(current->pml4, virt + i * PAGE_SIZE);
            spinlock_release(&shm_lock);
            return -14;
        }

        shm_table[slot].refs++;
        spinlock_release(&shm_lock);
        return 0;
    }

    case SYSCALL_SHM_CLOSE:
    {
        task_t *current = sched_current_task();
        char name[SHM_NAME_MAX];
        if (!current || copy_user_string(current, arg1, name, sizeof(name)) < 0)
            return -14;

        spinlock_acquire(&shm_lock);
        for (int i = 0; i < SHM_MAX; i++) {
            if (shm_table[i].in_use && strcmp(shm_table[i].name, name) == 0) {
                if (!current->pml4) {
                    spinlock_release(&shm_lock);
                    return -1;
                }

                uint64_t virt = SHM_VIRT_BASE + (uint64_t)i * 0x10000000ULL;
                for (size_t p = 0; p < shm_table[i].pages; p++)
                    unmap_page(current->pml4, virt + p * PAGE_SIZE);

                if (shm_table[i].refs > 0)
                    shm_table[i].refs--;

                if (shm_table[i].refs == 0) {
                    free_pages(shm_table[i].phys, shm_table[i].pages);
                    memset(&shm_table[i], 0, sizeof(shm_table[i]));
                }
                spinlock_release(&shm_lock);
                return 0;
            }
        }
        spinlock_release(&shm_lock);
        return -1;
    }

    case SYSCALL_SET_FOCUS:
    {
        (void)arg1;
        return 0;
    }

    case SYSCALL_ARCH_PRCTL:
    {
        task_t *current = sched_current_task();
        if (!current || current->is_kernel_task)
            return -38;

        switch (arg1)
        {
        case ARCH_SET_FS:
            current->user_fs_base = arg2;
            write_msr64(MSR_FS_BASE, arg2);
            return 0;
        case ARCH_GET_FS:
            return write_user_u64(current, arg2, current->user_fs_base);
        case ARCH_SET_GS:
            current->user_gs_base = arg2;
            write_msr64(MSR_KERNEL_GS_BASE, arg2);
            return 0;
        case ARCH_GET_GS:
            return write_user_u64(current, arg2, current->user_gs_base);
        default:
            return -22;
        }
    }

    case SYSCALL_UNIX_SOCKET:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int domain = (int)arg1;
        int type   = (int)arg2;
        int base_type = type & 0xF;
        int nonblock = !!(type & UNIX_SOCK_NONBLOCK);
        int cloexec = !!(type & UNIX_SOCK_CLOEXEC);
        if (domain != UNIX_AF_LOCAL) return -97;
        if (base_type != UNIX_SOCK_STREAM && base_type != UNIX_SOCK_DGRAM) return -93;
        unix_sock_t *s = unix_sock_alloc();
        if (!s) return -12;
        int fd = fd_alloc(cur->fd_table);
        if (fd < 0) { unix_sock_free(s); return -24; }
        fd_entry_t *e = &cur->fd_table->entries[fd];
        e->used  = 1;
        e->type  = FD_UNIX_SOCK;
        e->cloexec = cloexec;
        e->usock = s;
        s->domain = domain;
        s->type = base_type;
        unix_sock_set_nonblock(s, nonblock);
        return fd;
    }

    case SYSCALL_UNIX_BIND:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        char path[UNIX_PATH_MAX];
        if (!arg2) return -22;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        int r = unix_sock_copy_user_path(cur, arg2, path, sizeof(path));
        if (r < 0) return r;
        return unix_sock_bind(e->usock, path) < 0 ? -98 : 0;
    }

    case SYSCALL_UNIX_LISTEN:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd      = (int)arg1;
        int backlog = (int)arg2;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        return unix_sock_listen(e->usock, backlog) < 0 ? -22 : 0;
    }

    case SYSCALL_UNIX_ACCEPT:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        int flags = (int)arg4;
        int nonblock = !!(flags & UNIX_SOCK_NONBLOCK);
        int cloexec = !!(flags & UNIX_SOCK_CLOEXEC);
        uint32_t max_len = 0;
        if (!!arg2 != !!arg3) return -22;
        if (arg2 && (copy_from_user(cur, &max_len, arg3, sizeof(max_len)) < 0 ||
                     max_len < sizeof(sa_un_t) ||
                     user_copy_validate(cur, arg2, sizeof(sa_un_t), 1) < 0 ||
                     user_copy_validate(cur, arg3, sizeof(max_len), 1) < 0))
            return -14;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK || !e->usock) return -88;
        unix_sock_t *conn = unix_sock_accept(e->usock);
        if (!conn) return -11;
        int newfd = fd_alloc(cur->fd_table);
        if (newfd < 0) { unix_sock_free(conn); return -24; }
        fd_entry_t *ne = &cur->fd_table->entries[newfd];
        memset(ne, 0, sizeof(*ne));
        ne->used = 1;
        ne->type = FD_UNIX_SOCK;
        ne->cloexec = cloexec;
        ne->usock = conn;
        if (nonblock) unix_sock_set_nonblock(conn, 1);
        if (arg2)
        {
            sa_un_t address;
            uint32_t actual_len = sizeof(sa_un_t);
            memset(&address, 0, sizeof(address));
            address.family = UNIX_AF_LOCAL;
            unix_sock_getpeername(conn, address.path, sizeof(address.path));
            if (copy_to_user(cur, arg2, &address, sizeof(address)) < 0 ||
                copy_to_user(cur, arg3, &actual_len, sizeof(actual_len)) < 0)
            {
                memset(ne, 0, sizeof(*ne));
                unix_sock_free(conn);
                return -14;
            }
        }
        return newfd;
    }

    case SYSCALL_UNIX_CONNECT:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        char path[UNIX_PATH_MAX];
        if (!arg2) return -22;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        int r = unix_sock_copy_user_path(cur, arg2, path, sizeof(path));
        if (r < 0) return r;
        return unix_sock_connect(e->usock, path) < 0 ? -111 : 0;
    }

    case SYSCALL_UNIX_SEND:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        uint32_t len = (uint32_t)arg3;
        char path[UNIX_PATH_MAX];
        const char *path_ptr = NULL;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        if (!arg2 || len == 0 || len > UNIX_BUF_SIZE) return -22;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK || !e->usock) return -88;
        if (arg4)
        {
            int rr = unix_sock_copy_user_path(cur, arg4, path, sizeof(path));
            if (rr < 0) return rr;
            path_ptr = path;
        }
        void *buffer = kmalloc(len);
        if (!buffer) return -12;
        if (copy_from_user(cur, buffer, arg2, len) < 0)
        {
            kfree(buffer);
            return -14;
        }
        int r = unix_sock_sendto(e->usock, buffer, len, path_ptr);
        kfree(buffer);
        return r < 0 ? -32 : r;
    }

    case SYSCALL_UNIX_RECV:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        uint32_t len = (uint32_t)arg3;
        char path[UNIX_PATH_MAX];
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        if (!arg2 || len == 0 || len > UNIX_BUF_SIZE) return -22;
        if (user_copy_validate(cur, arg2, len, 1) < 0) return -14;
        if (!!arg4 != !!arg5) return -22;
        uint32_t max_len = 0;
        if (arg4)
        {
            if (copy_from_user(cur, &max_len, arg5, sizeof(max_len)) < 0 ||
                max_len < sizeof(sa_un_t) ||
                user_copy_validate(cur, arg4, sizeof(sa_un_t), 1) < 0 ||
                user_copy_validate(cur, arg5, sizeof(max_len), 1) < 0)
                return -14;
        }
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK || !e->usock) return -88;
        void *buffer = kmalloc(len);
        if (!buffer) return -12;
        int r = unix_sock_recvfrom(e->usock, buffer, len, path, sizeof(path));
        if (r >= 0)
        {
            sa_un_t address;
            if (copy_to_user(cur, arg2, buffer, (size_t)r) < 0)
                r = -14;
            else if (arg4)
            {
                memset(&address, 0, sizeof(address));
                address.family = UNIX_AF_LOCAL;
                strncpy(address.path, path, sizeof(address.path) - 1);
                if (copy_to_user(cur, arg4, &address, sizeof(address)) < 0 ||
                    copy_to_user(cur, arg5, &(uint32_t){sizeof(sa_un_t)}, sizeof(uint32_t)) < 0)
                    r = -14;
            }
        }
        kfree(buffer);
        return r < 0 ? r : r;
    }

    case SYSCALL_UNIX_SHUTDOWN:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        unix_sock_shutdown(e->usock);
        return 0;
    }

    case SYSCALL_GETSOCKNAME:
    case SYSCALL_GETPEERNAME:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        uint32_t max_len = (uint32_t)arg3;
        if (!arg2 || max_len < sizeof(sa_un_t)) return -22;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        if (user_copy_validate(cur, arg2, sizeof(sa_un_t), 1) < 0 ||
            (arg4 && user_copy_validate(cur, arg4, sizeof(uint32_t), 1) < 0))
            return -14;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK || !e->usock) return -88;
        sa_un_t address;
        memset(&address, 0, sizeof(address));
        address.family = UNIX_AF_LOCAL;
        if (num == SYSCALL_GETSOCKNAME)
            unix_sock_getsockname(e->usock, address.path, sizeof(address.path));
        else
            unix_sock_getpeername(e->usock, address.path, sizeof(address.path));
        uint32_t actual_len = sizeof(sa_un_t);
        if (copy_to_user(cur, arg2, &address, sizeof(address)) < 0 ||
            (arg4 && copy_to_user(cur, arg4, &actual_len, sizeof(actual_len)) < 0))
            return -14;
        return 0;
    }

    case SYSCALL_UNIX_SOCKETPAIR:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int domain = (int)arg1;
        int type = (int)arg2;
        int base_type = type & 0xF;
        int nonblock = !!(type & UNIX_SOCK_NONBLOCK);
        int cloexec = !!(type & UNIX_SOCK_CLOEXEC);
        if (!arg3 || user_copy_validate(cur, arg3, sizeof(int) * 2, 1) < 0) return -14;
        if (domain != UNIX_AF_LOCAL) return -97;
        if (base_type != UNIX_SOCK_STREAM && base_type != UNIX_SOCK_DGRAM) return -93;
        int fd0 = fd_alloc(cur->fd_table);
        if (fd0 < 0) return -24;
        int fd1 = fd_alloc(cur->fd_table);
        if (fd1 < 0) { memset(&cur->fd_table->entries[fd0], 0, sizeof(fd_entry_t)); return -24; }
        unix_sock_t *a = NULL;
        unix_sock_t *b = NULL;
        if (unix_sock_socketpair(base_type, nonblock, &a, &b) < 0)
        {
            memset(&cur->fd_table->entries[fd0], 0, sizeof(fd_entry_t));
            memset(&cur->fd_table->entries[fd1], 0, sizeof(fd_entry_t));
            return -12;
        }
        fd_entry_t *e0 = &cur->fd_table->entries[fd0];
        fd_entry_t *e1 = &cur->fd_table->entries[fd1];
        memset(e0, 0, sizeof(*e0));
        memset(e1, 0, sizeof(*e1));
        e0->used = 1; e0->type = FD_UNIX_SOCK; e0->cloexec = cloexec; e0->usock = a;
        e1->used = 1; e1->type = FD_UNIX_SOCK; e1->cloexec = cloexec; e1->usock = b;
        int pair[2] = {fd0, fd1};
        if (copy_to_user(cur, arg3, pair, sizeof(pair)) < 0)
        {
            memset(e0, 0, sizeof(*e0));
            memset(e1, 0, sizeof(*e1));
            unix_sock_free(a);
            unix_sock_free(b);
            return -14;
        }
        return 0;
    }

    default:
        log("Unknown syscall: %lu", 2, 0, num);
        return -1;
    }
}