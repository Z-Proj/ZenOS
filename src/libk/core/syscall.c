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

extern void syscall_entry(void);

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
        case SYSCALL_EXIT: {
            log("Task exiting.", 1, 0);
            task_t *current = sched_current_task();
            if (current) {
                current->state = TASK_DEAD;
            }
            sched_yield();
            return 0;
        }
        
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
        
        case SYSCALL_LOG: {
            const char *msg = (const char*)arg1;
            uint32_t level = (uint32_t)arg2;
            uint32_t visibility = (uint32_t)arg3;
            if (!msg || level > 4) return -1;
            log("%s", level, visibility, msg);
            return 0;
        }
        
        case SYSCALL_SLEEP: {
            uint32_t ms = (uint32_t)arg1;
            sleep(ms);
            return 0;
        }
        
        case SYSCALL_EXEC: {
            const char *filename = (const char*)arg1;
            if (!filename) return -1;
            int result = elf_exec(filename, 0, NULL);
            return result;
        }
        
        case SYSCALL_SHUTDOWN: {
            shutdown();
            for(;;){
                __asm__ __volatile__("cli; hlt");
            }
        }

        default:
            log("Unknown syscall: %lu", 2, 0, num);
            return -1;
    }
}