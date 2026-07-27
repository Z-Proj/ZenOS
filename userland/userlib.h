/**
 * 
 * @file : /userland/userlib.h
 * @brief : Userspace syscall wrappers and types.
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

#ifndef USERLIB_H
#define USERLIB_H

#include <stdint.h>
#include <stddef.h>

typedef long ssize_t;
typedef long off_t;

static inline uint64_t _syscall0(uint64_t num)
{
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num)
                     : "rcx", "r11", "rdi", "rsi", "rdx", "r10", "r8", "r9", "cc", "memory");
    return ret;
}
static inline uint64_t _syscall1(uint64_t num, uint64_t a1)
{
    uint64_t ret;
    register uint64_t rdi __asm__("rdi") = a1;
    __asm__ volatile("syscall" : "=a"(ret), "+D"(rdi) : "a"(num)
                     : "rcx", "r11", "rsi", "rdx", "r10", "r8", "r9", "cc", "memory");
    return ret;
}
static inline uint64_t _syscall2(uint64_t num, uint64_t a1, uint64_t a2)
{
    uint64_t ret;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    __asm__ volatile("syscall" : "=a"(ret), "+D"(rdi), "+S"(rsi) : "a"(num)
                     : "rcx", "r11", "rdx", "r10", "r8", "r9", "cc", "memory");
    return ret;
}
static inline uint64_t _syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3)
{
    uint64_t ret;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    __asm__ volatile("syscall" : "=a"(ret), "+D"(rdi), "+S"(rsi), "+d"(rdx) : "a"(num)
                     : "rcx", "r11", "r10", "r8", "r9", "cc", "memory");
    return ret;
}
static inline uint64_t _syscall4(uint64_t num, uint64_t a1, uint64_t a2,
                                 uint64_t a3, uint64_t a4)
{
    uint64_t ret;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    register uint64_t r10 __asm__("r10") = a4;
    __asm__ volatile("syscall" : "=a"(ret), "+D"(rdi), "+S"(rsi),
                                             "+d"(rdx), "+r"(r10)
                     : "a"(num)
                     : "rcx", "r11", "r8", "r9", "cc", "memory");
    return ret;
}
static inline uint64_t _syscall5(uint64_t num, uint64_t a1, uint64_t a2,
                                 uint64_t a3, uint64_t a4, uint64_t a5)
{
    uint64_t ret;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8 __asm__("r8") = a5;
    __asm__ volatile("syscall" : "=a"(ret), "+D"(rdi), "+S"(rsi),
                                             "+d"(rdx), "+r"(r10), "+r"(r8)
                     : "a"(num)
                     : "rcx", "r11", "r9", "cc", "memory");
    return ret;
}

#include <errno.h>
static inline int64_t _sc_ret(uint64_t r)
{
    int64_t v = (int64_t)r;
    if (v < 0)
    {
        errno = (int)(-v);
        return -1;
    }
    return v;
}

#define PROT_NONE 0x00
#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define PROT_EXEC 0x04

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void *)-1)

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t len);
int sched_yield(void);

#define SOCKET_NAME_MAX 64

#define KEY_ARROW_UP 0x01
#define KEY_ARROW_DOWN 0x02
#define KEY_ARROW_LEFT 0x03
#define KEY_ARROW_RIGHT 0x04

typedef struct
{
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    int64_t st_size;
    uint64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime_sec;
    int64_t st_mtime_sec;
    int64_t st_ctime_sec;
} stat_t;

typedef struct
{
    int64_t tv_sec;
    int64_t tv_usec;
} timeval_t;

typedef struct
{
    int64_t tv_sec;
    int64_t tv_nsec;
} timespec_t;

typedef struct
{
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
} utsname_t;

int uname(utsname_t *buf);

typedef struct
{
    char name[SOCKET_NAME_MAX];
    uint8_t *data;
    uint32_t capacity;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t available;
    uint8_t in_use;
} socket_file_t;

typedef struct
{
    uint64_t addr;
    uint64_t width;
    uint64_t height;
    uint8_t bpp;
    uint32_t pitch;
} fb_info_t;

typedef struct
{
    uint64_t pid;
    char name[64];
} task_info_t;

static inline int zen_create(const char *path)
{
    return (int)_sc_ret(_syscall1(15, (uint64_t)path));
}

static inline int socket_create(const char *name)
{
    return (int)_sc_ret(_syscall1(31, (uint64_t)name));
}
static inline int socket_open(const char *name, socket_file_t **file)
{
    return (int)_sc_ret(_syscall2(32, (uint64_t)name, (uint64_t)file));
}
static inline ssize_t socket_read(socket_file_t *file, void *buf,
                                  uint32_t size, uint32_t *bytes_read)
{
    return (ssize_t)_sc_ret(_syscall4(33, (uint64_t)file, (uint64_t)buf,
                                      size, (uint64_t)bytes_read));
}
static inline ssize_t socket_write(socket_file_t *file, const void *buf,
                                   uint32_t size)
{
    return (ssize_t)_sc_ret(_syscall3(34, (uint64_t)file,
                                      (uint64_t)buf, size));
}
static inline int socket_close(socket_file_t *file) { return (int)_sc_ret(_syscall1(35, (uint64_t)file)); }
static inline int socket_delete(const char *name) { return (int)_sc_ret(_syscall1(36, (uint64_t)name)); }
static inline int socket_exists(const char *name) { return (int)_sc_ret(_syscall1(37, (uint64_t)name)); }
static inline uint32_t socket_available(socket_file_t *file) { return (uint32_t)_syscall1(38, (uint64_t)file); }

static inline int zen_gethostbyname4(const char *host, uint8_t ip_out[4])
{
    return (int)_sc_ret(_syscall2(66, (uint64_t)host, (uint64_t)ip_out));
}

static inline int zen_getkey(void) { return (int)_syscall0(3); }
static inline uint32_t zen_mouse_x(void) { return (uint32_t)_syscall0(5); }
static inline uint32_t zen_mouse_y(void) { return (uint32_t)_syscall0(6); }
static inline uint8_t zen_mouse_btn(void) { return (uint8_t)_syscall0(7); }
static inline void zen_mouse_set_pos(uint32_t x, uint32_t y) { _syscall2(103, x, y); }
static inline void zen_speaker(uint32_t hz) { _syscall1(8, hz); }
static inline void zen_speaker_off(void) { _syscall0(9); }
static inline int zen_ls(char *buf, size_t sz) { return (int)_sc_ret(_syscall2(44, (uint64_t)buf, sz)); }
static inline int zen_fbinfo(fb_info_t *fb) { return (int)_sc_ret(_syscall1(45, (uint64_t)fb)); }
static inline int zen_is_focused(void) { return (int)_syscall0(46); }
static inline int zen_list_tasks(task_info_t *infos, uint32_t max)
{
    return (int)_sc_ret(_syscall2(49, (uint64_t)infos, max));
}
static inline void zen_shutdown(void) { _syscall0(41); }
static inline void zen_reboot(void) { _syscall0(42); }
static inline void zen_halt(void) { _syscall0(50); }
static inline int zen_fork(void) { return (int)_sc_ret(_syscall0(51)); }
static inline int zen_spawn(const char *path, char *const argv[])
{
    int argc = 0;
    if (argv)
        while (argv[argc])
            argc++;
    return (int)_sc_ret(_syscall3(85, (uint64_t)path, (uint64_t)argc, (uint64_t)argv));
}
static inline int zen_pipe(int pfd[2]) { return (int)_sc_ret(_syscall1(52, (uint64_t)pfd)); }
static inline int zen_dup(int fd) { return (int)_sc_ret(_syscall1(53, (uint64_t)(unsigned int)fd)); }
static inline int zen_dup2(int o, int n) { return (int)_sc_ret(_syscall2(54, (uint64_t)(unsigned int)o, (uint64_t)(unsigned int)n)); }
static inline char **zen_getenvp(void)             { (void)0; return NULL; }
static inline int zen_setenvp(char **envp)         { (void)envp; return 0; }
static inline int zen_kill(int pid, int sig)   { return (int)_sc_ret(_syscall2(47, (uint64_t)(unsigned int)pid, (uint64_t)(unsigned int)sig)); }
static inline int zen_sigaction(int sig, void *act, void *old) { return (int)_sc_ret(_syscall3(58, (uint64_t)(unsigned int)sig, (uint64_t)act, (uint64_t)old)); }
static inline int zen_sigreturn(void)              { return (int)_sc_ret(_syscall0(59)); }
static inline int zen_opendir(const char *path)                          { return (int)_sc_ret(_syscall1(55, (uint64_t)path)); }
static inline int zen_readdir(int fd, void *dent)                        { return (int)_sc_ret(_syscall2(56, (uint64_t)(unsigned int)fd, (uint64_t)dent)); }
static inline int zen_closedir(int fd)                                   { return (int)_sc_ret(_syscall1(57, (uint64_t)(unsigned int)fd)); }
static inline void zen_sleep_ms(uint32_t ms) { _syscall1(30, ms); }
static inline void zen_log(const char *msg, uint32_t level, uint32_t vis)
{
    _syscall3(40, (uint64_t)msg, level, vis);
}


static inline int zen_pty_open(int *sfd)
{
    return (int)_sc_ret(_syscall1(68, (uint64_t)sfd));
}


static inline int zen_ioctl(int fd, unsigned long req, void *argp)
{
    return (int)_sc_ret(_syscall3(69, (uint64_t)(unsigned int)fd,
                                  (uint64_t)req, (uint64_t)argp));
}

#define ZEN_TIOCGWINSZ  0x5413
#define ZEN_TIOCSWINSZ  0x5414
#define ZEN_TCGETS      0x5401
#define ZEN_TCSETS      0x5402
#define ZEN_TCSETSW     0x5403
#define ZEN_TCSETSF     0x5404
#define ZEN_FIONREAD    0x541B

#define ZEN_NCCS        19

#define ZEN_IGNBRK      0x00000001
#define ZEN_BRKINT      0x00000002
#define ZEN_IGNPAR      0x00000004
#define ZEN_PARMRK      0x00000008
#define ZEN_INPCK       0x00000010
#define ZEN_ISTRIP      0x00000020
#define ZEN_INLCR       0x00000040
#define ZEN_IGNCR       0x00000080
#define ZEN_ICRNL       0x00000100
#define ZEN_IXON        0x00000400

#define ZEN_OPOST       0x00000001
#define ZEN_ONLCR       0x00000004

#define ZEN_CS8         0x00000030
#define ZEN_CREAD       0x00000080

#define ZEN_ISIG        0x00000001
#define ZEN_ICANON      0x00000002
#define ZEN_ECHO        0x00000008
#define ZEN_ECHOE       0x00000010
#define ZEN_ECHOK       0x00000020
#define ZEN_IEXTEN      0x00008000

#define ZEN_VINTR       0
#define ZEN_VQUIT       1
#define ZEN_VERASE      2
#define ZEN_VKILL       3
#define ZEN_VEOF        4
#define ZEN_VTIME       5
#define ZEN_VMIN        6
#define ZEN_VSTART      8
#define ZEN_VSTOP       9
#define ZEN_VSUSP       10

#define ZEN_TCSANOW     0
#define ZEN_TCSADRAIN   1
#define ZEN_TCSAFLUSH   2

typedef struct {
    uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel;
} zen_winsize_t;

typedef struct {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[ZEN_NCCS];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
} zen_termios_t;

static inline int zen_tcgetattr(int fd, zen_termios_t *tio)
{
    return zen_ioctl(fd, ZEN_TCGETS, tio);
}

static inline int zen_tcsetattr(int fd, int actions, const zen_termios_t *tio)
{
    unsigned long req = ZEN_TCSETS;
    if (actions == ZEN_TCSADRAIN)
        req = ZEN_TCSETSW;
    else if (actions == ZEN_TCSAFLUSH)
        req = ZEN_TCSETSF;
    return zen_ioctl(fd, req, (void *)tio);
}

#define SYSCALL_SHM_CREATE 70
#define SYSCALL_SHM_OPEN   71
#define SYSCALL_SHM_CLOSE  72
#define SYSCALL_SET_FOCUS  73

typedef struct {
    uint64_t addr;
    uint64_t size;
} shm_info_t;

static inline int zen_shm_create(const char *name, size_t size, shm_info_t *out)
{
    return (int)_sc_ret(_syscall3(SYSCALL_SHM_CREATE, (uint64_t)name, (uint64_t)size, (uint64_t)out));
}

static inline int zen_shm_open(const char *name, shm_info_t *out)
{
    return (int)_sc_ret(_syscall2(SYSCALL_SHM_OPEN, (uint64_t)name, (uint64_t)out));
}

static inline int zen_shm_close(const char *name)
{
    return (int)_sc_ret(_syscall1(SYSCALL_SHM_CLOSE, (uint64_t)name));
}

static inline int zen_set_focus(int pid)
{
    return (int)_sc_ret(_syscall1(SYSCALL_SET_FOCUS, (uint64_t)(uint32_t)pid));
}

#endif