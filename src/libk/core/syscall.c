#include "syscall.h"
#include "elf.h"
#include "../debug/log.h"
#include "../../drv/keyboard.h"
#include "../../drv/mouse.h"
#include "../../drv/speaker.h"
#include "../../drv/vga.h"
#include "../../drv/disk/zfs.h"
#include "../../kernel/sched.h"
#include "../../drv/rtc.h"
#include "../../cpu/acpi/acpi.h"
#include "mem.h"
#include "socket.h"

extern void syscall_entry(void);
extern void AcpiReboot(void);
extern char *os_version;

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
    
    log("Syscalls initialized (STAR=0x%lx LSTAR=0x%lx)", 4, 0, star, lstar);
}

uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    (void)arg4;
    (void)arg5;
    
    switch(num) {
        // ==================== PROCESS MANAGEMENT ====================
        
        case SYSCALL_EXEC: {
            const char *filename = (const char*)arg1;
            int argc = (int)arg2;
            char **argv = (char**)arg3;
            if (!filename) return -1;
            return elf_exec(filename, argc, argv);
        }
        
        case SYSCALL_EXIT: {
            log("Task exiting.", 1, 0);
            task_t *current = sched_current_task();
            if (current) {
                current->state = TASK_DEAD;
            }
            sched_yield();
            return 0;
        }
        
        case SYSCALL_GETPID: {
            task_t *current = sched_current_task();
            return current ? current->pid : 0;
        }
        
        case SYSCALL_YIELD: {
            sched_yield();
            return 0;
        }
        
        // ==================== INPUT/OUTPUT ====================
        
        case SYSCALL_GETKEY:
            return (uint64_t)get_key();
        
        case SYSCALL_PRINTS: {
            const char *str = (const char*)arg1;
            uint32_t len = arg2;
            if (!str || len > 4096) return -1;
            for (uint32_t i = 0; i < len && str[i]; i++) {
                printc(str[i]);
            }
            return 0;
        }
        
        // ==================== MOUSE ====================
        
        case SYSCALL_MOUSE_X:
            return mouse_x();
        
        case SYSCALL_MOUSE_Y:
            return mouse_y();
        
        case SYSCALL_MOUSE_BTN:
            return mouse_button();
        
        // ==================== SPEAKER ====================
        
        case SYSCALL_SPEAKER:
            speaker_play((uint32_t)arg1);
            return 0;
        
        case SYSCALL_SPEAKER_OFF:
            speaker_pause();
            return 0;
        
        // ==================== FILE OPERATIONS ====================
        
        case SYSCALL_OPEN: {
            const char *filename = (const char*)arg1;
            zfs_file_t *file = (zfs_file_t*)arg2;
            if (!filename || !file) return ZFS_ERR_INVALID_PARAM;
            return zfs_open(filename, file);
        }
        
        case SYSCALL_READ: {
            zfs_file_t *file = (zfs_file_t*)arg1;
            void *buffer = (void*)arg2;
            uint32_t size = (uint32_t)arg3;
            uint32_t *bytes_read = (uint32_t*)arg4;
            if (!file || !buffer) return ZFS_ERR_INVALID_PARAM;
            return zfs_read(file, buffer, size, bytes_read);
        }
        
        case SYSCALL_WRITE: {
            zfs_file_t *file = (zfs_file_t*)arg1;
            const void *buffer = (const void*)arg2;
            uint32_t size = (uint32_t)arg3;
            if (!file || !buffer) return ZFS_ERR_INVALID_PARAM;
            return zfs_write(file, buffer, size);
        }
        
        case SYSCALL_CLOSE: {
            zfs_file_t *file = (zfs_file_t*)arg1;
            if (!file) return ZFS_ERR_INVALID_PARAM;
            return zfs_close(file);
        }
        
        case SYSCALL_LSEEK: {
            zfs_file_t *file = (zfs_file_t*)arg1;
            uint32_t offset = (uint32_t)arg2;
            // int whence = (int)arg3; // SEEK_SET, SEEK_CUR, SEEK_END
            if (!file) return ZFS_ERR_INVALID_PARAM;
            return zfs_seek(file, offset);
        }
        
        case SYSCALL_CREATE: {
            const char *filename = (const char*)arg1;
            uint32_t size = (uint32_t)arg2;
            if (!filename) return ZFS_ERR_INVALID_PARAM;
            return zfs_create(filename, size);
        }
        
        case SYSCALL_DELETE: {
            const char *filename = (const char*)arg1;
            if (!filename) return ZFS_ERR_INVALID_PARAM;
            return zfs_delete(filename);
        }
        
        case SYSCALL_STAT: {
            const char *path = (const char*)arg1;
            stat_t *statbuf = (stat_t*)arg2;
            if (!path || !statbuf) return -1;
            
            // Open file to get info
            zfs_file_t file;
            zfs_error_t err = zfs_open(path, &file);
            if (err != ZFS_OK) return -1;
            
            statbuf->st_size = file.size;
            statbuf->st_mode = 0644; // basic permissions for now
            statbuf->st_nlink = 1;
            statbuf->st_blksize = 4096; // ZFS_BLOCK_SIZE
            statbuf->st_blocks = (file.size + 4096 - 1) / 4096;
            
            zfs_close(&file);
            return 0;
        }
        
        case SYSCALL_FSTAT: {
            zfs_file_t *file = (zfs_file_t*)arg1;
            stat_t *statbuf = (stat_t*)arg2;
            if (!file || !statbuf) return -1;
            
            statbuf->st_size = file->size;
            statbuf->st_mode = 0644;
            statbuf->st_nlink = 1;
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = (file->size + 4096 - 1) / 4096;
            return 0;
        }
        
        // ==================== DIRECTORY OPERATIONS ====================
        
        case SYSCALL_CHDIR: {
            const char *path = (const char*)arg1;
            if (!path) return -1;
            return zfs_chdir(path);
        }
        
        case SYSCALL_GETCWD: {
            char *buffer = (char*)arg1;
            size_t size = (size_t)arg2;
            if (!buffer) return -1;
            zfs_get_cwd(buffer, size);
            return 0;
        }
        
        case SYSCALL_MKDIR: {
            const char *path = (const char*)arg1;
            if (!path) return -1;
            return zfs_mkdir(path);
        }
        
        case SYSCALL_RMDIR: {
            const char *path = (const char*)arg1;
            if (!path) return -1;
            return zfs_rmdir(path);
        }
        
        // ==================== MEMORY MANAGEMENT ====================
        
        case SYSCALL_BRK: {
            uint64_t new_brk = arg1;
            
            // Simple brk: allocate pages in the USER_HEAP_START region
            if (new_brk < USER_HEAP_START) return -1;
            
            // Round up to page boundary
            uint64_t pages_needed = (new_brk - USER_HEAP_START + PAGE_SIZE - 1) / PAGE_SIZE;
            
            // Map pages in kernel pml4
            page_table_t *kernel_pml4 = get_kernel_pml4();
            
            for (uint64_t i = 0; i < pages_needed; i++) {
                uint64_t virt = USER_HEAP_START + (i * PAGE_SIZE);
                if (virt_to_phys(kernel_pml4, virt) == 0) {
                    uint64_t phys = alloc_page();
                    if (!phys) return -1;
                    map_page(kernel_pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
                }
            }
            
            return new_brk;
        }
        
        case SYSCALL_SBRK: {
            int64_t increment = (int64_t)arg1;
            
            // TODO: Store heap_end in task struct
            // For now, use a static variable (shared across all tasks since we use kernel pml4)
            static uint64_t current_brk = USER_HEAP_START;
            uint64_t old_brk = current_brk;
            
            if (increment == 0) return old_brk;
            
            uint64_t new_brk = old_brk + increment;
            page_table_t *kernel_pml4 = get_kernel_pml4();
            
            if (increment > 0) {
                // Allocate more pages
                uint64_t start_page = (old_brk + PAGE_SIZE - 1) / PAGE_SIZE;
                uint64_t end_page = (new_brk + PAGE_SIZE - 1) / PAGE_SIZE;
                
                for (uint64_t page = start_page; page < end_page; page++) {
                    uint64_t virt = page * PAGE_SIZE;
                    if (virt < USER_HEAP_START) continue;
                    
                    if (virt_to_phys(kernel_pml4, virt) == 0) {
                        uint64_t phys = alloc_page();
                        if (!phys) return -1;
                        map_page(kernel_pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
                    }
                }
            } else if (increment < 0) {
                // TODO: Free pages when shrinking heap
            }
            
            current_brk = new_brk;
            return old_brk;
        }
        
        case SYSCALL_MMAP: {
            void *addr = (void*)arg1;
            size_t length = (size_t)arg2;
            int prot = (int)arg3;
            int flags = (int)arg4;
            (void)flags;
            
            // Simple mmap: allocate pages in kernel pml4
            uint64_t virt_start = addr ? (uint64_t)addr : USER_HEAP_START + 0x10000000;
            size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
            
            uint64_t page_flags = PAGE_PRESENT | PAGE_USER;
            if (prot & 0x2) page_flags |= PAGE_WRITABLE; // PROT_WRITE
            
            page_table_t *kernel_pml4 = get_kernel_pml4();
            
            for (size_t i = 0; i < pages; i++) {
                uint64_t virt = virt_start + (i * PAGE_SIZE);
                uint64_t phys = alloc_page();
                if (!phys) return -1;
                map_page(kernel_pml4, virt, phys, page_flags);
            }
            
            return virt_start;
        }
        
        case SYSCALL_MUNMAP: {
            void *addr = (void*)arg1;
            size_t length = (size_t)arg2;
            
            uint64_t virt_start = (uint64_t)addr;
            size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
            
            page_table_t *kernel_pml4 = get_kernel_pml4();
            
            for (size_t i = 0; i < pages; i++) {
                uint64_t virt = virt_start + (i * PAGE_SIZE);
                uint64_t phys = virt_to_phys(kernel_pml4, virt);
                if (phys) {
                    free_page(phys);
                    unmap_page(kernel_pml4, virt);
                }
            }
            return 0;
        }
        
        // ==================== TIME ====================
        
        case SYSCALL_GETTIMEOFDAY: {
            timeval_t *tv = (timeval_t*)arg1;
            if (!tv) return -1;
            
            rtc_time_t time = rtc_get_time();
            // Simple conversion
            tv->tv_sec = time.seconds + time.minutes * 60 + time.hours * 3600;
            tv->tv_usec = time.milliseconds * 1000;
            return 0;
        }
        
        case SYSCALL_CLOCK_GETTIME: {
            int clk_id = (int)arg1;
            timespec_t *tp = (timespec_t*)arg2;
            (void)clk_id;
            if (!tp) return -1;
            
            uint64_t ticks = rtc_get_ticks();
            tp->tv_sec = ticks / 1000;
            tp->tv_nsec = (ticks % 1000) * 1000000;
            return 0;
        }
        
        case SYSCALL_NANOSLEEP: {
            const timespec_t *req = (const timespec_t*)arg1;
            if (!req) return -1;
            
            uint32_t ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
            sleep(ms);
            return 0;
        }
        
        case SYSCALL_SLEEP: {
            uint32_t ms = (uint32_t)arg1;
            sleep(ms);
            return 0;
        }
        
        // ==================== IPC - SOCKET (your custom system) ====================
        
        case SYSCALL_SOCKET_CREATE: {
            const char *name = (const char*)arg1;
            if (!name) return -1;
            return socket_create(name);
        }
        
        case SYSCALL_SOCKET_OPEN: {
            const char *name = (const char*)arg1;
            socket_file_t **file = (socket_file_t**)arg2;
            if (!name || !file) return -1;
            return socket_open(name, file);
        }
        
        case SYSCALL_SOCKET_READ: {
            socket_file_t *file = (socket_file_t*)arg1;
            void *buffer = (void*)arg2;
            uint32_t size = (uint32_t)arg3;
            uint32_t *bytes_read = (uint32_t*)arg4;
            if (!file || !buffer) return -1;
            return socket_read(file, buffer, size, bytes_read);
        }
        
        case SYSCALL_SOCKET_WRITE: {
            socket_file_t *file = (socket_file_t*)arg1;
            const void *buffer = (const void*)arg2;
            uint32_t size = (uint32_t)arg3;
            if (!file || !buffer) return -1;
            return socket_write(file, buffer, size);
        }
        
        case SYSCALL_SOCKET_CLOSE: {
            socket_file_t *file = (socket_file_t*)arg1;
            if (!file) return -1;
            return socket_close(file);
        }
        
        case SYSCALL_SOCKET_DELETE: {
            const char *name = (const char*)arg1;
            if (!name) return -1;
            return socket_delete(name);
        }
        
        case SYSCALL_SOCKET_EXISTS: {
            const char *name = (const char*)arg1;
            if (!name) return -1;
            return socket_exists(name) ? 1 : 0;
        }
        
        case SYSCALL_SOCKET_AVAILABLE: {
            socket_file_t *file = (socket_file_t*)arg1;
            if (!file) return -1;
            return socket_available(file);
        }
        
        // ==================== SYSTEM INFO ====================
        
        case SYSCALL_UNAME: {
            utsname_t *buf = (utsname_t*)arg1;
            if (!buf) return -1;
            
            // Fill in system info from log.c
            const char *sysname = "ZenOS";
            const char *machine = "x86_64";
            const char *nodename = "zen";
            
            // Copy sysname
            int i = 0;
            while (sysname[i] && i < 64) {
                buf->sysname[i] = sysname[i];
                i++;
            }
            buf->sysname[i] = '\0';
            
            // Copy nodename
            i = 0;
            while (nodename[i] && i < 64) {
                buf->nodename[i] = nodename[i];
                i++;
            }
            buf->nodename[i] = '\0';
            
            // Copy release and version from os_version
            // os_version format: "0.90.0 DEBUG_ENABLED" or "0.90.0 Unstable"
            // Split on space to get release and version
            i = 0;
            int space_pos = -1;
            while (os_version[i] && i < 64) {
                if (os_version[i] == ' ') {
                    space_pos = i;
                    break;
                }
                buf->release[i] = os_version[i];
                i++;
            }
            buf->release[i] = '\0';
            
            // Copy version part (after space)
            if (space_pos >= 0) {
                i = 0;
                int j = space_pos + 1;
                while (os_version[j] && i < 64) {
                    buf->version[i] = os_version[j];
                    i++;
                    j++;
                }
                buf->version[i] = '\0';
            } else {
                buf->version[0] = '\0';
            }
            
            // Copy machine
            i = 0;
            while (machine[i] && i < 64) {
                buf->machine[i] = machine[i];
                i++;
            }
            buf->machine[i] = '\0';
            
            return 0;
        }
        
        // ==================== LOGGING AND SYSTEM ====================
        
        case SYSCALL_LOG: {
            const char *msg = (const char*)arg1;
            uint32_t level = (uint32_t)arg2;
            uint32_t visibility = (uint32_t)arg3;
            if (!msg || level > 4) return -1;
            log("%s", level, visibility, msg);
            return 0;
        }
        
        case SYSCALL_SHUTDOWN: {
            shutdown();
            for(;;) {
                __asm__ __volatile__("cli; hlt");
            }
        }
        
        case SYSCALL_REBOOT: {
            AcpiReboot();
            for(;;) {
                __asm__ __volatile__("cli; hlt");
            }
        }
        
        default:
            log("Unknown syscall: %lu", 2, 0, num);
            return -1;
    }
}