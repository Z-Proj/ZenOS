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

extern void syscall_entry(void);

#define SHM_VIRT_BASE 0x0000500000000000ULL
#define FB_VIRT_BASE  0x0000600000000000ULL
#define USER_MMAP_START 0x0000200000000000ULL
#define USER_MMAP_END   SHM_VIRT_BASE

typedef struct {
    char     name[SHM_NAME_MAX];
    uint64_t phys;
    size_t   pages;
    uint32_t refs;
    bool     in_use;
} shm_region_t;

static shm_region_t shm_table[SHM_MAX];

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

typedef struct {
    uint16_t family;
    char path[108];
} sa_un_t;

static inline void write_msr64(uint32_t msr, uint64_t value)
{
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

void syscall_retain_special_user_mappings(page_table_t *pml4)
{
    if (!pml4)
        return;

    for (int i = 0; i < SHM_MAX; i++) {
        if (!shm_table[i].in_use)
            continue;
        uint64_t virt = SHM_VIRT_BASE + (uint64_t)i * 0x10000000ULL;
        if (virt_to_phys(pml4, virt) == shm_table[i].phys)
            shm_table[i].refs++;
    }
}

void syscall_release_special_user_mappings(page_table_t *pml4)
{
    if (!pml4)
        return;

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
}

static int unix_sock_copy_user_path(task_t *task, const sa_un_t *addr, char *out, size_t out_len)
{
    if (!task || !addr || !out || out_len == 0)
        return -22;

    if (addr->family != UNIX_AF_LOCAL)
        return -22;

    if (addr->path[0] == '/')
    {
        strncpy(out, addr->path, out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }

    if (strcmp(task->cwd, "/") != 0)
        return -22;

    out[0] = '/';
    strncpy(out + 1, addr->path, out_len - 2);
    out[out_len - 1] = '\0';
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

static int write_user_u64(task_t *task, uint64_t uaddr, uint64_t value)
{
    if (!task || !task->pml4)
        return -14;

    uint64_t page0 = uaddr & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t off0 = uaddr & (PAGE_SIZE - 1);
    uint64_t phys0 = virt_to_phys(task->pml4, page0);
    if (!phys0)
        return -14;

    uint8_t bytes[sizeof(uint64_t)];
    memcpy(bytes, &value, sizeof(uint64_t));

    uint8_t *dst0 = (uint8_t *)(phys0 + KERNEL_VIRT_OFFSET + off0);
    size_t first = PAGE_SIZE - off0;
    if (first >= sizeof(uint64_t))
    {
        memcpy(dst0, bytes, sizeof(uint64_t));
        return 0;
    }

    memcpy(dst0, bytes, first);

    uint64_t page1 = page0 + PAGE_SIZE;
    uint64_t phys1 = virt_to_phys(task->pml4, page1);
    if (!phys1)
        return -14;

    uint8_t *dst1 = (uint8_t *)(phys1 + KERNEL_VIRT_OFFSET);
    memcpy(dst1, bytes + first, sizeof(uint64_t) - first);
    return 0;
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
        const char *filename = (const char *)arg1;
        int argc = (int)arg2;
        char **argv = (char **)arg3;
        char **envp = (char **)arg4;
        if (!filename)
            return -1;
        return elf_execve_replace(filename, argc, argv, envp);
    }

    case SYSCALL_SPAWN:
    {
        const char *filename = (const char *)arg1;
        int argc = (int)arg2;
        char **argv = (char **)arg3;
        task_t *current = sched_current_task();
        if (!filename || !current)
            return -1;
        return elf_spawn(filename, argc, argv, current);
    }

    case SYSCALL_EXIT:
    {
        task_t *current = sched_current_task();
        if (current)
        {
            current->exit_code = (int)arg1;
            current->state = TASK_DEAD;
        }
        sched_yield();
        return 0;
    }

    case SYSCALL_GETPID:
    {
        task_t *current = sched_current_task();
        return current ? current->pid : 0;
    }

    case SYSCALL_YIELD:
    {
        sched_yield();
        return 0;
    }

    case SYSCALL_LS:
    {
        char *buf = (char *)arg1;
        size_t buf_size = (size_t)arg2;
        if (!buf || buf_size == 0)
            return -1;
        return vfs_list(buf, buf_size);
    }

    case SYSCALL_GETKEY:
        return (uint64_t)get_key();

    case SYSCALL_PRINTS:
    {
        const char *str = (const char *)arg1;
        uint32_t len = arg2;
        if (!str || len > 4096)
            return -1;
        for (uint32_t i = 0; i < len && str[i]; i++){
            printc(str[i]);
            //serial_write_char(str[i]);
        }
        return 0;
    }

    case SYSCALL_MOUSE_X:
        return mouse_x();

    case SYSCALL_MOUSE_Y:
        return mouse_y();

    case SYSCALL_MOUSE_BTN:
        return mouse_button();

    case SYSCALL_SPEAKER:
        speaker_play((uint32_t)arg1);
        return 0;

    case SYSCALL_SPEAKER_OFF:
        speaker_pause();
        return 0;

    case SYSCALL_OPEN:
    {
        const char *filename = (const char *)arg1;
        int flags = (int)arg2;
        if (!filename)
            return -1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        int write_mode = 0;
        if (flags & 0x01)
            write_mode = 1;
        if (flags & 0x02)
            write_mode = 1;
        if (flags & 0x200)
            write_mode = 2;
        if (flags & 0x40)
            vfs_create(filename);
        int fd = fd_alloc(cur->fd_table);
        if (fd < 0)
            return -1;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (vfs_open_entry(filename, write_mode, e) < 0)
            return -1;
        return fd;
    }

    case SYSCALL_READ:
    {
        int fd = (int)arg1;
        void *buffer = (void *)arg2;
        uint32_t size = (uint32_t)arg3;
        if (!buffer)
            return -1;
        task_t *cur = sched_current_task();
        fd_entry_t *e = NULL;
        if (cur && cur->fd_table && fd >= 0 && fd < TASK_MAX_FDS)
        {
            fd_entry_t *cand = &cur->fd_table->entries[fd];
            if (cand->used)
                e = cand;
        }
        if (fd == 0 && !e)
            return console_read_fallback(buffer, size);
        if (!e)
            return -1;
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
                return -1;
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
                memcpy((uint8_t *)buffer + total, kbuf, got);
                total += got;
            }
            kfree(kbuf);
            return (int64_t)total;
        }
        if (e->type != FD_FILE)
            return -1;
        if (size == 0)
            return 0;
        uint8_t *ubuf = (uint8_t *)buffer;
        uint8_t *kbuf = (uint8_t *)kmalloc(4096);
        if (!kbuf)
            return -1;
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
                return (total > 0) ? (int64_t)total : -1;
            }
            if (bytes_read == 0)
                break;
            memcpy(ubuf + total, kbuf, bytes_read);
            total += bytes_read;
            if (bytes_read < chunk)
                break;
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
            return -1;
        task_t *cur = sched_current_task();
        fd_entry_t *e = NULL;
        if (cur && cur->fd_table && fd >= 0 && fd < TASK_MAX_FDS)
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
            return -1;
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
                return -1;
            if (d->write(buffer, size) < 0)
                return -1;
            return (int64_t)size;
        }
        if (e->type != FD_FILE)
            return -1;
        if (size == 0)
            return 0;
        const uint8_t *ubuf = (const uint8_t *)buffer;
        uint8_t *kbuf = (uint8_t *)kmalloc(4096);
        if (!kbuf)
            return -1;
        uint32_t total = 0;
        while (total < size)
        {
            uint32_t chunk = size - total;
            if (chunk > 4096)
                chunk = 4096;
            memcpy(kbuf, ubuf + total, chunk);
            int ret = vfs_write_entry(e, kbuf, chunk);
            if (ret < 0)
            {
                kfree(kbuf);
                return (total > 0) ? (int64_t)total : -1;
            }
            total += chunk;
        }
        kfree(kbuf);
        return (int64_t)total;
    }

    case SYSCALL_CLOSE:
    {
        int fd = (int)arg1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        if (fd < 0 || fd >= TASK_MAX_FDS)
            return -1;
        if (fd < 3 && !cur->fd_table->entries[fd].used)
            return 0;
        return fd_close(cur->fd_table, fd);
    }

    case SYSCALL_LSEEK:
    {
        int fd = (int)arg1;
        int32_t offset = (int32_t)arg2;
        int whence = (int)arg3;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        if (fd < 0 || fd >= TASK_MAX_FDS)
            return -1;
        if (fd == 0 || fd == 1 || fd == 2)
            return -29;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used)
            return -1;
        if (e->type != FD_FILE)
            return -29;
        return vfs_lseek_entry(e, offset, whence);
    }

    case SYSCALL_CREATE:
    {
        const char *filename = (const char *)arg1;
        if (!filename)
            return -1;
        return vfs_create(filename);
    }

    case SYSCALL_DELETE:
    {
        const char *filename = (const char *)arg1;
        if (!filename)
            return -1;
        return vfs_delete(filename);
    }

    case SYSCALL_RENAME:
    {
        const char *old_path = (const char *)arg1;
        const char *new_path = (const char *)arg2;
        if (!old_path || !new_path)
            return -1;
        return vfs_rename(old_path, new_path);
    }

    case SYSCALL_FTRUNCATE:
    {
        int fd = (int)arg1;
        uint32_t size = (uint32_t)arg2;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        if (fd < 0 || fd >= TASK_MAX_FDS)
            return -1;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_FILE)
            return -1;
        return vfs_truncate_entry(e, size);
    }

    case SYSCALL_FSYNC:
    {
        int fd = (int)arg1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        if (fd < 0 || fd >= TASK_MAX_FDS)
            return -1;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_FILE)
            return -1;
        return vfs_sync_entry(e);
    }

    case SYSCALL_STAT:
    {
        const char *path = (const char *)arg1;
        uint8_t *buf = (uint8_t *)arg2;
        if (!path || !buf)
            return -1;
        if (unix_sock_path_exists(path))
        {
            memset(buf, 0, 88);
            *(uint32_t *)(buf + 16) = 0140666;
            *(uint32_t *)(buf + 20) = 1;
            *(uint64_t *)(buf + 48) = 512;
            return 0;
        }
        fd_entry_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        int is_dir = 0;
        if (vfs_open_entry(path, 0, &tmp) < 0)
        {
            if (vfs_opendir_entry(path, &tmp) < 0)
                return -1;
            is_dir = 1;
        }
        uint32_t sz = is_dir ? 0 : vfs_size_entry(&tmp);
        int64_t  mt = is_dir ? 0 : vfs_mtime_entry(&tmp);
        if (is_dir) vfs_closedir_entry(&tmp);
        else        vfs_close_entry(&tmp);
       
        memset(buf, 0, 88);
        *(uint32_t *)(buf + 16) = is_dir ? 0040755 : 0100644;
        *(uint32_t *)(buf + 20) = 1;                          
        *(int64_t  *)(buf + 40) = (int64_t)sz;                
        *(uint64_t *)(buf + 48) = 512;                        
        *(int64_t  *)(buf + 56) = (int64_t)((sz + 511) / 512);
        *(int64_t  *)(buf + 64) = mt;                          
        *(int64_t  *)(buf + 72) = mt;                          
        *(int64_t  *)(buf + 80) = mt;                          
        return 0;
    }

    case SYSCALL_FSTAT:
    {
        int fd = (int)arg1;
        uint8_t *buf = (uint8_t *)arg2;
        if (!buf)
            return -1;
        memset(buf, 0, 88);
        if (fd == 0 || fd == 1 || fd == 2)
        {
            *(uint32_t *)(buf + 16) = 0020666;
            return 0;
        }
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        if (fd < 3 || fd >= TASK_MAX_FDS)
            return -1;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used)
            return -1;
        if (e->type == FD_PTY_MASTER || e->type == FD_PTY_SLAVE ||
            e->type == FD_PIPE_READ || e->type == FD_PIPE_WRITE ||
            e->type == FD_DEV)
        {
            *(uint32_t *)(buf + 16) = 0020666;
            *(uint32_t *)(buf + 20) = 1;
            return 0;
        }
        if (e->type == FD_UNIX_SOCK)
        {
            *(uint32_t *)(buf + 16) = 0140666;
            *(uint32_t *)(buf + 20) = 1;
            return 0;
        }
        if (e->type == FD_DIR)
        {
            *(uint32_t *)(buf + 16) = 0040755;
            *(uint32_t *)(buf + 20) = 1;
            return 0;
        }
        if (e->type != FD_FILE)
            return -1;
        uint32_t sz = vfs_size_entry(e);
        *(uint32_t *)(buf + 16) = 0100644;
        *(uint32_t *)(buf + 20) = 1;
        *(int64_t  *)(buf + 40) = (int64_t)sz;
        *(uint64_t *)(buf + 48) = 512;
        *(int64_t  *)(buf + 56) = (int64_t)((sz + 511) / 512);
        return 0;
    }

    case SYSCALL_CHDIR:
    {
        const char *path = (const char *)arg1;
        if (!path)
            return -1;
        return vfs_chdir(path);
    }

    case SYSCALL_GETCWD:
    {
        char *buffer = (char *)arg1;
        size_t size = (size_t)arg2;
        if (!buffer)
            return -1;
        vfs_getcwd(buffer, size);
        return 0;
    }

    case SYSCALL_MKDIR:
    {
        const char *path = (const char *)arg1;
        if (!path)
            return -1;
        return vfs_mkdir(path);
    }

    case SYSCALL_RMDIR:
    {
        const char *path = (const char *)arg1;
        if (!path)
            return -1;
        return vfs_rmdir(path);
    }

    case SYSCALL_BRK:
    {
        uint64_t new_brk = arg1;
        task_t *current = sched_current_task();
        if (!current || !current->pml4)
            return -1;

        if (new_brk < USER_HEAP_START)
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
        uint64_t new_brk = current->heap_brk + increment;

        if (new_brk < USER_HEAP_START)
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
        if (!current || !current->pml4 || length == 0)
            return -1;

        size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t virt_start = 0;
        uint64_t page_flags = user_page_flags_from_prot(prot);

        if (flags & 0x10)
        {
            if (!addr || ((uint64_t)addr & (PAGE_SIZE - 1)))
                return -1;
            virt_start = (uint64_t)addr;
            if (virt_start < USER_MMAP_START || virt_start + (uint64_t)pages * PAGE_SIZE > USER_MMAP_END)
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

        if (((uint64_t)addr & (PAGE_SIZE - 1)) || length == 0)
            return -1;

        uint64_t virt_start = (uint64_t)addr;
        size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
        user_unmap_range(current, virt_start, pages);
        return 0;
    }

    case SYSCALL_MPROTECT:
    {
        void *addr = (void *)arg1;
        size_t length = (size_t)arg2;
        int prot = (int)arg3;

        task_t *current = sched_current_task();
        if (!current || !current->pml4 || ((uint64_t)addr & (PAGE_SIZE - 1)) || length == 0)
            return -1;

        size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t page_flags = user_page_flags_from_prot(prot);

        for (size_t i = 0; i < pages; i++)
        {
            if (protect_page(current->pml4, (uint64_t)addr + i * PAGE_SIZE, page_flags) < 0)
                return -1;
        }

        return 0;
    }

    case SYSCALL_GETTIMEOFDAY:
    {
        timeval_t *tv = (timeval_t *)arg1;
        if (!tv)
            return -1;

        static const uint16_t days_before_month[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
        rtc_time_t t = rtc_get_time();
        uint32_t year = 2000 + t.year;
        uint32_t yday = days_before_month[t.month - 1] + t.day - 1;
        if (t.month > 2 && (year % 4 == 0))
            yday++;
        uint32_t days = (year - 1970) * 365 + (year - 1969) / 4 + yday;
        tv->tv_sec  = (uint64_t)days * 86400ULL + t.hours * 3600ULL + t.minutes * 60ULL + t.seconds;
        tv->tv_usec = (uint64_t)t.milliseconds * 1000ULL;
        return 0;
    }

    case SYSCALL_CLOCK_GETTIME:
    {
        int clk_id = (int)arg1;
        timespec_t *tp = (timespec_t *)arg2;
        if (!tp)
            return -1;

        if (clk_id == 0 || clk_id == 5 || clk_id == 11)
        {
            static const uint16_t days_before_month[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
            rtc_time_t t = rtc_get_time();
            uint32_t year = 2000 + t.year;
            uint32_t yday = days_before_month[t.month - 1] + t.day - 1;
            if (t.month > 2 && (year % 4 == 0))
                yday++;
            uint32_t days = (year - 1970) * 365 + (year - 1969) / 4 + yday;
            tp->tv_sec  = (uint64_t)days * 86400ULL + t.hours * 3600ULL + t.minutes * 60ULL + t.seconds;
            tp->tv_nsec = (uint64_t)t.milliseconds * 1000000ULL;
        }
        else
        {
            uint64_t ns = hpet_monotonic_ns();
            tp->tv_sec  = ns / 1000000000ULL;
            tp->tv_nsec = ns % 1000000000ULL;
        }
        return 0;
    }

    case SYSCALL_NANOSLEEP:
    {
        const timespec_t *req = (const timespec_t *)arg1;
        if (!req)
            return -1;

        uint32_t ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
        sleep(ms);
        return 0;
    }

    case SYSCALL_SLEEP:
    {
        uint32_t ms = (uint32_t)arg1;
        sleep(ms);
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
        utsname_t *buf = (utsname_t *)arg1;
        if (!buf)
            return -1;

        const char *sysname = "ZenOS";
        const char *machine = "x86_64";
        const char *nodename = "Zen";

        int i = 0;
        while (sysname[i] && i < 64)
        {
            buf->sysname[i] = sysname[i];
            i++;
        }
        buf->sysname[i] = '\0';

        i = 0;
        while (nodename[i] && i < 64)
        {
            buf->nodename[i] = nodename[i];
            i++;
        }
        buf->nodename[i] = '\0';

        i = 0;
        int space_pos = -1;
        while (os_version[i] && i < 64)
        {
            if (os_version[i] == ' ')
            {
                space_pos = i;
                break;
            }
            buf->release[i] = os_version[i];
            i++;
        }
        buf->release[i] = '\0';

        if (space_pos >= 0)
        {
            i = 0;
            int j = space_pos + 1;
            while (os_version[j] && i < 64)
            {
                buf->version[i] = os_version[j];
                i++;
                j++;
            }
            buf->version[i] = '\0';
        }
        else
        {
            buf->version[0] = '\0';
        }

        i = 0;
        while (machine[i] && i < 64)
        {
            buf->machine[i] = machine[i];
            i++;
        }
        buf->machine[i] = '\0';

        return 0;
    }

    case SYSCALL_LOG:
    {
        const char *msg = (const char *)arg1;
        uint32_t level = (uint32_t)arg2;
        uint32_t visibility = (uint32_t)arg3;
        if (!msg || level > 4)
            return -1;
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
        fb_info_t *info = (fb_info_t *)arg1;
        if (!info)
            return -1;

        task_t *current = sched_current_task();
        if (!current || !current->pml4)
            return -1;
        uint64_t fb_phys = virt_to_phys(get_kernel_pml4(), (uint64_t)framebuffer_addr);
        if (!fb_phys)
            return -1;

        uint64_t fb_size = framebuffer_pitch * framebuffer_height;
        size_t pages = (fb_size + PAGE_SIZE - 1) / PAGE_SIZE;

        uint64_t user_fb_vaddr = 0x0000600000000000ULL;

        for (size_t i = 0; i < pages; i++)
        {
            uint64_t virt = user_fb_vaddr + i * PAGE_SIZE;
            uint64_t phys = fb_phys + i * PAGE_SIZE;
            map_page(current->pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }

        info->addr = user_fb_vaddr;
        info->width = framebuffer_width;
        info->height = framebuffer_height;
        info->bpp = framebuffer_bpp;
        info->pitch = framebuffer_pitch;

        return 0;
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
        task_info_t *infos = (task_info_t *)arg1;
        uint32_t max_count = (uint32_t)arg2;
        return sched_list_tasks(infos, max_count);
    }

    case SYSCALL_FORK:
    {
        return (uint64_t)sched_fork(arg5);
    }

    case SYSCALL_PIPE:
    {
        int *pipefd = (int *)arg1;
        if (!pipefd)
            return -1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        int rfd = fd_alloc(cur->fd_table);
        if (rfd < 0)
            return -1;
        cur->fd_table->entries[rfd].used = 1;
        int wfd = fd_alloc(cur->fd_table);
        if (wfd < 0)
        {
            cur->fd_table->entries[rfd].used = 0;
            return -1;
        }
        pipe_buf_t *pb = (pipe_buf_t *)kmalloc(sizeof(pipe_buf_t));
        if (!pb)
        {
            cur->fd_table->entries[rfd].used = 0;
            cur->fd_table->entries[wfd].used = 0;
            return -1;
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
        pipefd[0] = rfd;
        pipefd[1] = wfd;
        return 0;
    }

    case SYSCALL_DUP:
    {
        int oldfd = (int)arg1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        if (oldfd < 0 || oldfd >= TASK_MAX_FDS)
            return -1;
        fd_entry_t *src = &cur->fd_table->entries[oldfd];
        if (!src->used)
            return -1;
        int nfd = fd_alloc(cur->fd_table);
        if (nfd < 0)
            return -1;
        if (fd_entry_clone(&cur->fd_table->entries[nfd], src, 0) < 0)
            return -1;
        return nfd;
    }

    case SYSCALL_DUP2:
    {
        int oldfd = (int)arg1;
        int newfd = (int)arg2;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        if (oldfd < 0 || oldfd >= TASK_MAX_FDS)
            return -1;
        if (newfd < 0 || newfd >= TASK_MAX_FDS)
            return -1;
        fd_entry_t *src = &cur->fd_table->entries[oldfd];
        if (!src->used)
            return -1;
        if (oldfd == newfd)
            return newfd;
        fd_entry_t *dst = &cur->fd_table->entries[newfd];
        if (dst->used)
            fd_close(cur->fd_table, newfd);
        if (fd_entry_clone(dst, src, 0) < 0)
            return -1;
        return newfd;
    }

    case SYSCALL_OPENDIR:
    {
        const char *path = (const char *)arg1;
        if (!path)
            return -1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        int fd = fd_alloc(cur->fd_table);
        if (fd < 0)
            return -1;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (vfs_opendir_entry(path, e) < 0)
            return -1;
        return fd;
    }

    case SYSCALL_READDIR:
    {
        int fd = (int)arg1;
        zen_dirent_t *dent = (zen_dirent_t *)arg2;
        if (!dent)
            return -1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        if (fd < 3 || fd >= TASK_MAX_FDS)
            return -1;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_DIR)
            return -1;
        int is_dir = 0;
        int ret = vfs_readdir_entry(e, dent->d_name, &is_dir);
        if (ret <= 0)
            return ret;
        dent->d_ino = 1;
        dent->d_type = is_dir ? 4 : 8;
        return 1;
    }

    case SYSCALL_CLOSEDIR:
    {
        int fd = (int)arg1;
        if (fd < 3 || fd >= TASK_MAX_FDS)
            return -1;
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table)
            return -1;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_DIR)
            return -1;
        vfs_closedir_entry(e);
        memset(e, 0, sizeof(fd_entry_t));
        return 0;
    }

    case SYSCALL_SIGACTION:
    {
        int sig = (int)arg1;
        zen_sigaction_t *act = (zen_sigaction_t *)arg2;
        zen_sigaction_t *oldact = (zen_sigaction_t *)arg3;
        if (sig <= 0 || sig >= NSIG)
            return -1;
        if (sig == SIGKILL || sig == SIGSTOP)
            return -1;
        task_t *cur = sched_current_task();
        if (!cur)
            return -1;
        if (oldact)
            *oldact = cur->sighandlers[sig];
        if (act)
            cur->sighandlers[sig] = *act;
        return 0;
    }

    case SYSCALL_SIGRETURN:
    {
        task_t *cur = sched_current_task();
        if (!cur)
            return 0;
        uint64_t usp = cur->regs.userrsp;
        uint64_t page_base = usp & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t page_off = usp & (PAGE_SIZE - 1);
        uint64_t phys = virt_to_phys(cur->pml4, page_base);
        if (phys)
        {
            uint64_t saved_rip = *(uint64_t *)(phys + KERNEL_VIRT_OFFSET + page_off);
            cur->regs.userrsp += sizeof(uint64_t);
            cur->regs.rip = saved_rip;
        }
        dispatch_pending_signals(cur);
        return 0;
    }

    case SYSCALL_SIGPROCMASK:
    {
        int how = (int)arg1;
        uint64_t *set = (uint64_t *)arg2;
        uint64_t *oldset = (uint64_t *)arg3;
        task_t *cur = sched_current_task();
        if (!cur)
            return -1;
        if (oldset)
            *oldset = cur->sig_mask;
        if (set)
        {
            if (how == 0)
                cur->sig_mask |= *set;
            else if (how == 1)
                cur->sig_mask &= ~(*set);
            else if (how == 2)
                cur->sig_mask = *set;
        }
        return 0;
    }

    case SYSCALL_HALT:
    {
        sched_yield();
        return 0;
    }

    case SYSCALL_NET_CONNECT:
    {
        net_connect_args_t *args = (net_connect_args_t *)arg1;
        if (!args)
            return (uint64_t)-1;
        return (uint64_t)(int64_t)tcp_connect(args->ip, args->port);
    }

    case SYSCALL_NET_SEND:
    {
        int id = (int)arg1;
        const void *buf = (const void *)arg2;
        size_t len = (size_t)arg3;
        if (!buf)
            return (uint64_t)-1;
        return (uint64_t)(int64_t)tcp_send(id, buf, len);
    }

    case SYSCALL_NET_RECV:
    {
        int id = (int)arg1;
        void *buf = (void *)arg2;
        size_t len = (size_t)arg3;
        if (!buf)
            return (uint64_t)-1;
        return (uint64_t)(int64_t)tcp_recv(id, buf, len);
    }

    case SYSCALL_NET_CLOSE:
    {
        int id = (int)arg1;
        tcp_close(id);
        return 0;
    }

    case SYSCALL_NET_POLL:
    {
        net_poll();
        return 0;
    }

    case SYSCALL_DNS_RESOLVE:
    {
        const char *hostname = (const char *)arg1;
        uint8_t *ip_out = (uint8_t *)arg2;
        if (!hostname || !ip_out)
            return -1;
        return (uint64_t)(int64_t)dns_resolve(hostname, ip_out);
    }

    case SYSCALL_FUTEX:
    {
        volatile uint32_t *uaddr = (volatile uint32_t *)(uintptr_t)arg1;
        int op = (int)arg2;
        uint32_t val = (uint32_t)arg3;

        if (op == FUTEX_WAIT)
            return (uint64_t)(int64_t)sched_futex_wait(uaddr, val);
        if (op == FUTEX_WAKE)
            return (uint64_t)sched_futex_wake(uaddr, val);
        return (uint64_t)(int64_t)-22;
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

        *slave_fd_out = sfd;
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
                pty_export_termios(pty, (zen_termios_t *)argp);
                return 0;
            }
            if (req == ZEN_TCSETS || req == ZEN_TCSETSW)
            {
                pty_import_termios(pty, (const zen_termios_t *)argp, 0);
                return 0;
            }
            if (req == ZEN_TCSETSF)
            {
                pty_import_termios(pty, (const zen_termios_t *)argp, 1);
                return 0;
            }
            if (req == ZEN_TIOCGWINSZ && argp)
            {
                zen_winsize_t *ws = (zen_winsize_t *)argp;
                ws->ws_row    = pty->ws_rows;
                ws->ws_col    = pty->ws_cols;
                ws->ws_xpixel = pty->ws_xpixel;
                ws->ws_ypixel = pty->ws_ypixel;
                return 0;
            }
            if (req == ZEN_TIOCSWINSZ && argp)
            {
                zen_winsize_t *ws = (zen_winsize_t *)argp;
                pty->ws_rows   = ws->ws_row;
                pty->ws_cols   = ws->ws_col;
                pty->ws_xpixel = ws->ws_xpixel;
                pty->ws_ypixel = ws->ws_ypixel;
                return 0;
            }
            if (req == ZEN_FIONREAD && argp)
            {
                int *out = (int *)argp;
                if (e->type == FD_PTY_MASTER)
                    *out = (int)pty->s2m_count;
                else if (pty->lflag & ZEN_ICANON)
                    *out = (int)pty->m2s_ready;
                else
                    *out = (int)pty->m2s_count;
                return 0;
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
        const char *name = (const char *)arg1;
        size_t size      = (size_t)arg2;
        shm_info_t *info = (shm_info_t *)arg3;
        if (!name || size == 0 || !info)
            return -1;

        for (int i = 0; i < SHM_MAX; i++)
            if (shm_table[i].in_use && strcmp(shm_table[i].name, name) == 0)
                return -1;

        int slot = -1;
        for (int i = 0; i < SHM_MAX; i++)
            if (!shm_table[i].in_use) { slot = i; break; }
        if (slot < 0)
            return -1;

        size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t phys = alloc_pages(pages);
        if (!phys)
            return -1;

        task_t *current = sched_current_task();
        if (!current || !current->pml4) { free_pages(phys, pages); return -1; }

        uint64_t virt = SHM_VIRT_BASE + (uint64_t)slot * 0x10000000ULL;
        for (size_t i = 0; i < pages; i++)
        {
            if (map_user_page_checked(current->pml4, virt + i * PAGE_SIZE, phys + i * PAGE_SIZE,
                                      PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0)
            {
                user_unmap_range(current, virt, i);
                free_pages(phys, pages);
                return -1;
            }
        }

        strncpy(shm_table[slot].name, name, SHM_NAME_MAX - 1);
        shm_table[slot].name[SHM_NAME_MAX - 1] = '\0';
        shm_table[slot].phys   = phys;
        shm_table[slot].pages  = pages;
        shm_table[slot].refs   = 1;
        shm_table[slot].in_use = true;

        info->addr = virt;
        info->size = pages * PAGE_SIZE;
        return 0;
    }

    case SYSCALL_SHM_OPEN:
    {
        const char *name = (const char *)arg1;
        shm_info_t *info = (shm_info_t *)arg2;
        if (!name || !info)
            return -1;

        int slot = -1;
        for (int i = 0; i < SHM_MAX; i++)
            if (shm_table[i].in_use && strcmp(shm_table[i].name, name) == 0)
                { slot = i; break; }
        if (slot < 0)
            return -1;

        task_t *current = sched_current_task();
        if (!current || !current->pml4)
            return -1;

        uint64_t virt = SHM_VIRT_BASE + (uint64_t)slot * 0x10000000ULL;
        for (size_t i = 0; i < shm_table[slot].pages; i++)
            map_page(current->pml4, virt + i * PAGE_SIZE,
                     shm_table[slot].phys + i * PAGE_SIZE,
                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

        shm_table[slot].refs++;
        info->addr = virt;
        info->size = shm_table[slot].pages * PAGE_SIZE;
        return 0;
    }

    case SYSCALL_SHM_CLOSE:
    {
        const char *name = (const char *)arg1;
        if (!name)
            return -1;

        for (int i = 0; i < SHM_MAX; i++)
        {
            if (shm_table[i].in_use && strcmp(shm_table[i].name, name) == 0)
            {
                task_t *current = sched_current_task();
                if (!current || !current->pml4)
                    return -1;

                uint64_t virt = SHM_VIRT_BASE + (uint64_t)i * 0x10000000ULL;
                for (size_t p = 0; p < shm_table[i].pages; p++)
                    unmap_page(current->pml4, virt + p * PAGE_SIZE);

                if (shm_table[i].refs > 0)
                    shm_table[i].refs--;

                if (shm_table[i].refs == 0)
                {
                    free_pages(shm_table[i].phys, shm_table[i].pages);
                    memset(&shm_table[i], 0, sizeof(shm_table[i]));
                }
                return 0;
            }
        }
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
        const sa_un_t *addr = (const sa_un_t *)arg2;
        char path[UNIX_PATH_MAX];
        if (!addr) return -22;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        int r = unix_sock_copy_user_path(cur, addr, path, sizeof(path));
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
        sa_un_t *addr = (sa_un_t *)arg2;
        uint32_t *addrlen = (uint32_t *)arg3;
        int flags = (int)arg4;
        int nonblock = !!(flags & UNIX_SOCK_NONBLOCK);
        int cloexec = !!(flags & UNIX_SOCK_CLOEXEC);
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        unix_sock_t *conn = unix_sock_accept(e->usock);
        if (!conn) return -11;
        int newfd = fd_alloc(cur->fd_table);
        if (newfd < 0) { unix_sock_free(conn); return -24; }
        fd_entry_t *ne = &cur->fd_table->entries[newfd];
        ne->used  = 1;
        ne->type  = FD_UNIX_SOCK;
        ne->cloexec = cloexec;
        ne->usock = conn;
        if (nonblock)
            unix_sock_set_nonblock(conn, 1);
        if (addr && addrlen)
        {
            uint32_t max_len = *addrlen;
            if (max_len >= sizeof(sa_un_t))
            {
                addr->family = UNIX_AF_LOCAL;
                unix_sock_getpeername(conn, addr->path, sizeof(addr->path));
            }
            *addrlen = sizeof(sa_un_t);
        }
        return newfd;
    }

    case SYSCALL_UNIX_CONNECT:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        const sa_un_t *addr = (const sa_un_t *)arg2;
        char path[UNIX_PATH_MAX];
        if (!addr) return -22;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        int r = unix_sock_copy_user_path(cur, addr, path, sizeof(path));
        if (r < 0) return r;
        return unix_sock_connect(e->usock, path) < 0 ? -111 : 0;
    }

    case SYSCALL_UNIX_SEND:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd          = (int)arg1;
        const void *buf = (const void *)arg2;
        uint32_t len    = (uint32_t)arg3;
        const sa_un_t *addr = (const sa_un_t *)arg4;
        char path[UNIX_PATH_MAX];
        const char *path_ptr = NULL;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        if (addr)
        {
            int rr = unix_sock_copy_user_path(cur, addr, path, sizeof(path));
            if (rr < 0) return rr;
            path_ptr = path;
        }
        int r = unix_sock_sendto(e->usock, buf, len, path_ptr);
        return r < 0 ? -32 : r;
    }

    case SYSCALL_UNIX_RECV:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd       = (int)arg1;
        void *buf    = (void *)arg2;
        uint32_t len = (uint32_t)arg3;
        sa_un_t *addr = (sa_un_t *)arg4;
        uint32_t *addrlen = (uint32_t *)arg5;
        char path[UNIX_PATH_MAX];
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        int r = unix_sock_recvfrom(e->usock, buf, len, path, sizeof(path));
        if (r >= 0 && addr && addrlen)
        {
            uint32_t max_len = *addrlen;
            if (max_len < sizeof(sa_un_t))
                return -22;
            addr->family = UNIX_AF_LOCAL;
            strncpy(addr->path, path, sizeof(addr->path) - 1);
            addr->path[sizeof(addr->path) - 1] = '\0';
            *addrlen = sizeof(sa_un_t);
        }
        return r < 0 ? -11 : r;
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
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        sa_un_t *addr = (sa_un_t *)arg2;
        uint32_t max_len = (uint32_t)arg3;
        uint32_t *actual_len = (uint32_t *)arg4;
        if (!addr) return -22;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        if (max_len < sizeof(sa_un_t)) return -22;
        addr->family = 1;
        unix_sock_getsockname(e->usock, addr->path, sizeof(addr->path));
        if (actual_len) *actual_len = sizeof(sa_un_t);
        return 0;
    }

    case SYSCALL_GETPEERNAME:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int fd = (int)arg1;
        sa_un_t *addr = (sa_un_t *)arg2;
        uint32_t max_len = (uint32_t)arg3;
        uint32_t *actual_len = (uint32_t *)arg4;
        if (!addr) return -22;
        if (fd < 0 || fd >= TASK_MAX_FDS) return -9;
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used || e->type != FD_UNIX_SOCK) return -88;
        if (max_len < sizeof(sa_un_t)) return -22;
        addr->family = 1;
        unix_sock_getpeername(e->usock, addr->path, sizeof(addr->path));
        if (actual_len) *actual_len = sizeof(sa_un_t);
        return 0;
    }

    case SYSCALL_UNIX_SOCKETPAIR:
    {
        task_t *cur = sched_current_task();
        if (!cur || !cur->fd_table) return -1;
        int domain = (int)arg1;
        int type = (int)arg2;
        int *sv = (int *)arg3;
        int base_type = type & 0xF;
        int nonblock = !!(type & UNIX_SOCK_NONBLOCK);
        int cloexec = !!(type & UNIX_SOCK_CLOEXEC);
        if (!sv) return -22;
        if (domain != UNIX_AF_LOCAL) return -97;
        if (base_type != UNIX_SOCK_STREAM && base_type != UNIX_SOCK_DGRAM) return -93;
        int fd0 = fd_alloc(cur->fd_table);
        if (fd0 < 0) return -24;
        cur->fd_table->entries[fd0].used = 1;
        int fd1 = fd_alloc(cur->fd_table);
        if (fd1 < 0)
        {
            cur->fd_table->entries[fd0].used = 0;
            return -24;
        }
        cur->fd_table->entries[fd1].used = 1;
        unix_sock_t *a = NULL;
        unix_sock_t *b = NULL;
        if (unix_sock_socketpair(base_type, nonblock, &a, &b) < 0)
        {
            cur->fd_table->entries[fd0].used = 0;
            cur->fd_table->entries[fd1].used = 0;
            return -12;
        }
        fd_entry_t *e0 = &cur->fd_table->entries[fd0];
        fd_entry_t *e1 = &cur->fd_table->entries[fd1];
        memset(e0, 0, sizeof(*e0));
        memset(e1, 0, sizeof(*e1));
        e0->used = 1;
        e0->type = FD_UNIX_SOCK;
        e0->cloexec = cloexec;
        e0->usock = a;
        e1->used = 1;
        e1->type = FD_UNIX_SOCK;
        e1->cloexec = cloexec;
        e1->usock = b;
        sv[0] = fd0;
        sv[1] = fd1;
        return 0;
    }

    default:
        log("Unknown syscall: %lu", 2, 0, num);
        return -1;
    }
}
