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

extern void syscall_entry(void);

#define SHM_VIRT_BASE 0x0000500000000000ULL

typedef struct {
    char     name[SHM_NAME_MAX];
    uint64_t phys;
    size_t   pages;
    bool     in_use;
} shm_region_t;

static shm_region_t shm_table[SHM_MAX];

void init_syscalls(void)
{
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x13 << 48);
    uint32_t star_lo = star & 0xFFFFFFFF;
    uint32_t star_hi = star >> 32;
    __asm__ volatile("wrmsr" : : "c"(0xC0000081), "a"(star_lo), "d"(star_hi));

    uint64_t lstar = (uint64_t)&syscall_entry;
    uint32_t lstar_lo = lstar & 0xFFFFFFFF;
    uint32_t lstar_hi = lstar >> 32;
    __asm__ volatile("wrmsr" : : "c"(0xC0000082), "a"(lstar_lo), "d"(lstar_hi));

    uint64_t fmask = 0x200;
    __asm__ volatile("wrmsr" : : "c"(0xC0000084), "a"(fmask), "d"(0));

    uint32_t efer_lo, efer_hi;
    __asm__ volatile("rdmsr" : "=a"(efer_lo), "=d"(efer_hi) : "c"(0xC0000080));
    efer_lo |= 1;
    __asm__ volatile("wrmsr" : : "c"(0xC0000080), "a"(efer_lo), "d"(efer_hi));

    __asm__ volatile("wrmsr" : : "c"(0xC0000101), "a"(0), "d"(0));

    tss_t *cpu_tss = gdt_get_tss(smp_current_cpu_index());
    if (!cpu_tss)
        cpu_tss = gdt_get_tss(smp_bsp_cpu_index());

    uint64_t kernel_gs_base = (uint64_t)cpu_tss;
    uint32_t gs_lo = kernel_gs_base & 0xFFFFFFFF;
    uint32_t gs_hi = kernel_gs_base >> 32;
    __asm__ volatile("wrmsr" : : "c"(0xC0000102), "a"(gs_lo), "d"(gs_hi));

    log("Syscalls initialized.", 4, 0);
}

void syscall_prepare_user_return(uint64_t gs_base)
{
    tss_t *cpu_tss = gdt_get_tss(smp_current_cpu_index());
    if (!cpu_tss)
        cpu_tss = gdt_get_tss(smp_bsp_cpu_index());

    uint32_t gs_lo = (uint32_t)(gs_base & 0xFFFFFFFF);
    uint32_t gs_hi = (uint32_t)(gs_base >> 32);
    __asm__ volatile("wrmsr" : : "c"(0xC0000101), "a"(gs_lo), "d"(gs_hi));

    uint64_t kernel_gs_base = (uint64_t)cpu_tss;
    uint32_t kgs_lo = (uint32_t)(kernel_gs_base & 0xFFFFFFFF);
    uint32_t kgs_hi = (uint32_t)(kernel_gs_base >> 32);
    __asm__ volatile("wrmsr" : : "c"(0xC0000102), "a"(kgs_lo), "d"(kgs_hi));
}

void syscall_prepare_sysret_return(uint64_t gs_base)
{
    tss_t *cpu_tss = gdt_get_tss(smp_current_cpu_index());
    if (!cpu_tss)
        cpu_tss = gdt_get_tss(smp_bsp_cpu_index());

    uint64_t kernel_gs_base = (uint64_t)cpu_tss;
    uint32_t kgs_lo = (uint32_t)(kernel_gs_base & 0xFFFFFFFF);
    uint32_t kgs_hi = (uint32_t)(kernel_gs_base >> 32);
    __asm__ volatile("wrmsr" : : "c"(0xC0000101), "a"(kgs_lo), "d"(kgs_hi));

    uint32_t gs_lo = (uint32_t)(gs_base & 0xFFFFFFFF);
    uint32_t gs_hi = (uint32_t)(gs_base >> 32);
    __asm__ volatile("wrmsr" : : "c"(0xC0000102), "a"(gs_lo), "d"(gs_hi));
}

static void dispatch_pending_signals(task_t *task)
{
    if (!task || !task->sig_pending)
        return;

    for (int sig = 1; sig < NSIG; sig++)
    {
        if (!(task->sig_pending & (1u << sig)))
            continue;
        if (task->sig_mask & (1u << sig))
            continue;

        task->sig_pending &= ~(1u << sig);
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

static int pty_push_m2s(pty_buf_t *pty, uint8_t ch)
{
    if (pty->m2s_count >= PTY_BUF_SIZE)
        return 0;
    pty->m2s_data[pty->m2s_write] = ch;
    pty->m2s_write = (pty->m2s_write + 1) % PTY_BUF_SIZE;
    pty->m2s_count++;
    return 1;
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
    while (pty->s2m_count >= PTY_BUF_SIZE)
    {
        if (!pty->master_open)
            return 0;
        sched_yield();
    }
    return pty_push_s2m_nowait(pty, ch);
}

static void pty_write_echo(pty_buf_t *pty, uint8_t ch)
{
    pty_push_s2m_nowait(pty, ch);
}

static void pty_defaults(pty_buf_t *pty)
{
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
    memset(t, 0, sizeof(*t));
    t->c_iflag = pty->iflag;
    t->c_oflag = pty->oflag;
    t->c_cflag = pty->cflag;
    t->c_lflag = pty->lflag;
    memcpy(t->c_cc, pty->cc, sizeof(pty->cc));
    t->c_ispeed = pty->ispeed;
    t->c_ospeed = pty->ospeed;
}

static void pty_import_termios(pty_buf_t *pty, const zen_termios_t *t, int flush_input)
{
    if (!pty || !t)
        return;
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
}

static int64_t pty_slave_read(pty_buf_t *pty, uint8_t *dst, uint32_t size)
{
    uint32_t n = 0;
    if (size == 0)
        return 0;

    if (pty->lflag & ZEN_ICANON)
    {
        while (n < size)
        {
            while (pty->m2s_ready == 0)
            {
                if (pty->eof_pending)
                {
                    pty->eof_pending = 0;
                    return (int64_t)n;
                }
                if (!pty->master_open)
                    return (int64_t)n;
                sched_yield();
            }
            uint8_t ch = 0;
            if (!pty_pop_m2s(pty, &ch))
                continue;
            dst[n++] = ch;
            if (ch == '\n')
                break;
        }
        return (int64_t)n;
    }

    uint8_t vmin = pty->cc[ZEN_VMIN];
    if (vmin == 0)
    {
        while (n < size)
        {
            uint8_t ch = 0;
            if (!pty_pop_m2s(pty, &ch))
                break;
            dst[n++] = ch;
        }
        return (int64_t)n;
    }

    while (pty->m2s_count < vmin)
    {
        if (!pty->master_open)
        {
            if (pty->m2s_count == 0)
                return 0;
            break;
        }
        sched_yield();
    }

    while (n < size)
    {
        uint8_t ch = 0;
        if (!pty_pop_m2s(pty, &ch))
            break;
        dst[n++] = ch;
    }
    return (int64_t)n;
}

static int64_t pty_master_read(pty_buf_t *pty, uint8_t *dst, uint32_t size)
{
    uint32_t n = 0;
    while (n < size && pty->s2m_count > 0)
    {
        dst[n++] = pty->s2m_data[pty->s2m_read];
        pty->s2m_read = (pty->s2m_read + 1) % PTY_BUF_SIZE;
        pty->s2m_count--;
    }
    return (int64_t)n;
}

static int64_t pty_master_write(pty_buf_t *pty, const uint8_t *src, uint32_t size)
{
    uint32_t written = 0;
    if (!pty->slave_open)
        return -1;

    for (uint32_t i = 0; i < size; i++)
    {
        uint8_t ch = src[i];

        if (pty->iflag & ZEN_IGNCR)
        {
            if (ch == '\r')
            {
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
                written++;
                continue;
            }
            if (pty->esc_state == 2)
            {
                if (ch >= 0x40 && ch <= 0x7E)
                    pty->esc_state = 0;
                written++;
                continue;
            }
            if (ch == 0x1B)
            {
                pty->esc_state = 1;
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
                written++;
                continue;
            }

            if (ch == vkill)
            {
                while (pty_pop_m2s_tail_unready(pty)) {}
                if ((pty->lflag & ZEN_ECHO) && (pty->lflag & ZEN_ECHOK))
                    pty_write_echo(pty, '\n');
                written++;
                continue;
            }

            if (ch == veof)
            {
                if (pty->m2s_count == pty->m2s_ready)
                    pty->eof_pending = 1;
                else
                    pty->m2s_ready = pty->m2s_count;
                written++;
                continue;
            }
        }

        while (!pty_push_m2s(pty, ch))
        {
            if (!pty->slave_open)
                return written ? (int64_t)written : -1;
            sched_yield();
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

        written++;
    }

    return (int64_t)written;
}

static int64_t pty_slave_write(pty_buf_t *pty, const uint8_t *src, uint32_t size)
{
    uint32_t written = 0;
    if (!pty->master_open)
        return -1;

    for (uint32_t i = 0; i < size; i++)
    {
        uint8_t ch = src[i];
        if ((pty->oflag & ZEN_OPOST) && (pty->oflag & ZEN_ONLCR) && ch == '\n')
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
            // serial_write_char(str[i]);
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
        {
            char *cbuf = (char *)buffer;
            for (uint32_t i = 0; i < size; i++)
            {
                char c = 0;
                while (!c)
                {
                    __asm__ volatile("sti; hlt; cli" ::: "memory");
                    c = get_key();
                }
                cbuf[i] = c;
                if (c == '\n')
                    return (int64_t)(i + 1);
            }
            return (int64_t)size;
        }
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
        if (e->type == FD_PIPE_READ)
        {
            uint8_t *cbuf = (uint8_t *)buffer;
            uint32_t n = 0;
            while (n < size)
            {
                if (e->pipe->count == 0)
                {
                    if (e->pipe->write_closed)
                        break;
                    sched_yield();
                    continue;
                }
                cbuf[n++] = e->pipe->data[e->pipe->read_pos];
                e->pipe->read_pos = (e->pipe->read_pos + 1) % 4096;
                e->pipe->count--;
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
                // serial_write_char(p[i]);
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
        if (e->type == FD_PIPE_WRITE)
        {
            const uint8_t *cbuf = (const uint8_t *)buffer;
            for (uint32_t i = 0; i < size; i++)
            {
                while (e->pipe->count >= 4096)
                    sched_yield();
                e->pipe->data[e->pipe->write_pos] = cbuf[i];
                e->pipe->write_pos = (e->pipe->write_pos + 1) % 4096;
                e->pipe->count++;
            }
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
        fd_entry_t *e = &cur->fd_table->entries[fd];
        if (!e->used)
            return -1;
        if (e->type != FD_FILE)
            return -1;
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

    case SYSCALL_STAT:
    {
        const char *path = (const char *)arg1;
        uint8_t *buf = (uint8_t *)arg2;
        if (!path || !buf)
            return -1;
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

            for (uint64_t virt = start; virt < end; virt += PAGE_SIZE)
            {
                uint64_t phys = alloc_page();
                if (!phys)
                    return -1;
                map_page(current->pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            }
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

            for (uint64_t virt = start; virt < end; virt += PAGE_SIZE)
            {
                uint64_t phys = alloc_page();
                if (!phys)
                    return -1;
                map_page(current->pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            }
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
        (void)flags;

        task_t *current = sched_current_task();
        if (!current || !current->pml4)
            return -1;

        uint64_t virt_start = addr ? (uint64_t)addr : USER_HEAP_START + 0x10000000;
        size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;

        uint64_t page_flags = PAGE_PRESENT | PAGE_USER;
        if (prot & 0x2)
            page_flags |= PAGE_WRITABLE;

        for (size_t i = 0; i < pages; i++)
        {
            uint64_t virt = virt_start + (i * PAGE_SIZE);
            uint64_t phys = alloc_page();
            if (!phys)
                return -1;
            map_page(current->pml4, virt, phys, page_flags);
        }

        return virt_start;
    }

    case SYSCALL_MUNMAP:
    {
        void *addr = (void *)arg1;
        size_t length = (size_t)arg2;

        task_t *current = sched_current_task();
        if (!current || !current->pml4)
            return -1;

        uint64_t virt_start = (uint64_t)addr;
        size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;

        for (size_t i = 0; i < pages; i++)
        {
            uint64_t virt = virt_start + (i * PAGE_SIZE);
            uint64_t phys = virt_to_phys(current->pml4, virt);
            if (phys)
            {
                free_page(phys);
                unmap_page(current->pml4, virt);
            }
        }
        return 0;
    }

    case SYSCALL_GETTIMEOFDAY:
    {
        timeval_t *tv = (timeval_t *)arg1;
        if (!tv)
            return -1;

        rtc_time_t time = rtc_get_time();
        tv->tv_sec = time.seconds + time.minutes * 60 + time.hours * 3600;
        tv->tv_usec = time.milliseconds * 1000;
        return 0;
    }

    case SYSCALL_CLOCK_GETTIME:
    {
        int clk_id = (int)arg1;
        timespec_t *tp = (timespec_t *)arg2;
        (void)clk_id;
        if (!tp)
            return -1;
        uint64_t ns = hpet_monotonic_ns();
        tp->tv_sec = ns / 1000000000ULL;
        tp->tv_nsec = ns % 1000000000ULL;
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
            __asm__ __volatile__("cli; hlt");
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
        task_t *current = sched_current_task();
        if (!current)
            return 0;
        return (current->pid == kbd_get_focused_pid()) ? 1 : 0;
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
        cur->fd_table->entries[nfd] = *src;
        if (src->type == FD_PIPE_READ || src->type == FD_PIPE_WRITE)
        {
            src->pipe->refcount++;
            if (src->type == FD_PIPE_READ)
                src->pipe->readers++;
            if (src->type == FD_PIPE_WRITE)
                src->pipe->writers++;
        }
        else if (src->type == FD_PTY_MASTER)
        {
            src->pty->refcount++;
            src->pty->master_refs++;
            src->pty->master_open = 1;
        }
        else if (src->type == FD_PTY_SLAVE)
        {
            src->pty->refcount++;
            src->pty->slave_refs++;
            src->pty->slave_open = 1;
        }
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
        *dst = *src;
        if (src->type == FD_PIPE_READ || src->type == FD_PIPE_WRITE)
        {
            src->pipe->refcount++;
            if (src->type == FD_PIPE_READ)
                src->pipe->readers++;
            if (src->type == FD_PIPE_WRITE)
                src->pipe->writers++;
        }
        else if (src->type == FD_PTY_MASTER)
        {
            src->pty->refcount++;
            src->pty->master_refs++;
            src->pty->master_open = 1;
        }
        else if (src->type == FD_PTY_SLAVE)
        {
            src->pty->refcount++;
            src->pty->slave_refs++;
            src->pty->slave_open = 1;
        }
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
        uint32_t *set = (uint32_t *)arg2;
        uint32_t *oldset = (uint32_t *)arg3;
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
        asm volatile("sti; hlt");
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

        if (!e) return (uint64_t)(int64_t)-1;

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
        if (!current || !current->pml4) { free_page(phys); return -1; }

        uint64_t virt = SHM_VIRT_BASE + (uint64_t)slot * 0x10000000ULL;
        for (size_t i = 0; i < pages; i++)
            map_page(current->pml4, virt + i * PAGE_SIZE, phys + i * PAGE_SIZE,
                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

        strncpy(shm_table[slot].name, name, SHM_NAME_MAX - 1);
        shm_table[slot].name[SHM_NAME_MAX - 1] = '\0';
        shm_table[slot].phys   = phys;
        shm_table[slot].pages  = pages;
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
                for (size_t p = 0; p < shm_table[i].pages; p++)
                    free_page(shm_table[i].phys + p * PAGE_SIZE);
                shm_table[i].in_use = false;
                return 0;
            }
        }
        return -1;
    }

    default:
        log("Unknown syscall: %lu", 2, 0, num);
        return -1;
    }
}
