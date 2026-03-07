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
#include "../string.h"
#include "../debug/serial.h"
#include "mem.h"
#include "socket.h"

extern void syscall_entry(void);
extern tss_t tss;

void init_syscalls(void)
{
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x10 << 48);
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

    uint64_t kernel_gs_base = (uint64_t)&tss;
    uint32_t gs_lo = kernel_gs_base & 0xFFFFFFFF;
    uint32_t gs_hi = kernel_gs_base >> 32;
    __asm__ volatile("wrmsr" : : "c"(0xC0000102), "a"(gs_lo), "d"(gs_hi));

    log("Syscalls initialized.", 4, 0);
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
            for (uint32_t i = 0; i < size; i++)
                printc(p[i]);
            return (int64_t)size;
        }
        if (!e)
            return -1;
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
        stat_t *statbuf = (stat_t *)arg2;
        if (!path || !statbuf)
            return -1;
        fd_entry_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        if (vfs_open_entry(path, 0, &tmp) < 0)
            return -1;
        uint32_t sz = vfs_size_entry(&tmp);
        vfs_close_entry(&tmp);
        statbuf->st_size = sz;
        statbuf->st_mode = 0100644;
        statbuf->st_nlink = 1;
        statbuf->st_blksize = 512;
        statbuf->st_blocks = (sz + 511) / 512;
        return 0;
    }

    case SYSCALL_FSTAT:
    {
        int fd = (int)arg1;
        stat_t *statbuf = (stat_t *)arg2;
        if (!statbuf)
            return -1;
        if (fd == 0 || fd == 1 || fd == 2)
        {
            memset(statbuf, 0, sizeof(stat_t));
            statbuf->st_mode = 0020666;
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
        uint32_t sz = vfs_size_entry(e);
        statbuf->st_size = sz;
        statbuf->st_mode = 0100644;
        statbuf->st_nlink = 1;
        statbuf->st_blksize = 512;
        statbuf->st_blocks = (sz + 511) / 512;
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
        return sched_wait_pid(pid, wstatus);
    }

    case SYSCALL_LIST_TASKS:
    {
        task_info_t *infos = (task_info_t *)arg1;
        uint32_t max_count = (uint32_t)arg2;
        if (!infos || max_count == 0)
            return 0;
        task_t *head = sched_get_task_list();
        if (!head)
            return 0;
        uint32_t count = 0;
        task_t *t = head;
        do
        {
            if (count >= max_count)
                break;
            if (t->state != TASK_DEAD)
            {
                infos[count].pid = t->pid;
                strncpy(infos[count].name, t->name, 63);
                infos[count].name[63] = '\0';
                count++;
            }
            t = t->next;
        } while (t != head);
        return count;
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

    default:
        log("Unknown syscall: %lu", 2, 0, num);
        return -1;
    }
}
