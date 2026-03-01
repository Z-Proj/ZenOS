#ifndef USERLIB_H
#define USERLIB_H
#include "stdint.h"

typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int32_t pid_t;

#define NULL ((void*)0)

// ==================== SYSCALL INTERFACE ====================

static inline uint64_t syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "rdi", "rsi", "rdx", "r10", "r8", "r9", "memory");
    return ret;
}

static inline uint64_t syscall1(uint64_t num, uint64_t arg1) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1) : "rcx", "r11", "rsi", "rdx", "r10", "r8", "r9", "memory");
    return ret;
}

static inline uint64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2) : "rcx", "r11", "rdx", "r10", "r8", "r9", "memory");
    return ret;
}

static inline uint64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "r10", "r8", "r9", "memory");
    return ret;
}

static inline uint64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = arg4;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10) : "rcx", "r11", "r8", "r9", "memory");
    return ret;
}

static inline uint64_t syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = arg4;
    register uint64_t r8 __asm__("r8") = arg5;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8) : "rcx", "r11", "r9", "memory");
    return ret;
}

typedef struct {
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    uint64_t st_size;
    uint64_t st_blksize;
    uint64_t st_blocks;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
} stat_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} timeval_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} timespec_t;

typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
} utsname_t;

#define SOCKET_NAME_MAX 64
typedef struct {
    char name[SOCKET_NAME_MAX];
    uint8_t *data;
    uint32_t capacity;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t available;
    uint8_t in_use;
} socket_file_t;

typedef struct {
    uint64_t addr;
    uint64_t width;
    uint64_t height;
    uint8_t  bpp;
    uint32_t pitch; 
} fb_info_t;

static inline int exec(const char *filename) {
    return (int)syscall3(0, (uint64_t)filename, 0, 0);
}

static inline int execv(const char *filename, char *const argv[]) {
    int argc = 0;
    if (argv) { while (argv[argc]) argc++; }
    return (int)syscall3(0, (uint64_t)filename, (uint64_t)argc, (uint64_t)argv);
}

static inline void exit(int code) {
    (void)code;
    syscall0(1);
    while(1);
}

static inline pid_t getpid(void) {
    return (pid_t)syscall0(2);
}

static inline void yield(void) {
    syscall0(43);
}

static inline char getkey(void) {
    return (char)syscall0(3);
}

static inline void prints(const char *str) {
    if (!str) return;
    uint32_t len = 0;
    while (str[len]) len++;
    syscall2(4, (uint64_t)str, len);
}

static inline void putchar(char c) {
    char buf[2] = {c, 0};
    prints(buf);
}

static inline uint32_t mouse_x(void) {
    return (uint32_t)syscall0(5);
}

static inline uint32_t mouse_y(void) {
    return (uint32_t)syscall0(6);
}

static inline uint8_t mouse_button(void) {
    return (uint8_t)syscall0(7);
}

static inline void speaker_play(uint32_t hz) {
    syscall1(8, hz);
}

static inline void speaker_stop(void) {
    syscall0(9);
}

static inline int open(const char *filename, int write_mode) {
    return (int)syscall2(10, (uint64_t)filename, (uint64_t)write_mode);
}

static inline ssize_t read(int fd, void *buffer, size_t size) {
    uint32_t bytes_read = 0;
    int ret = (int)syscall4(11, (uint64_t)fd, (uint64_t)buffer, size, (uint64_t)&bytes_read);
    if (ret < 0) return ret;
    return bytes_read;
}

static inline int write(int fd, const void *buffer, size_t size) {
    return syscall3(12, (uint64_t)fd, (uint64_t)buffer, size);
}

static inline int close(int fd) {
    return (int)syscall1(13, (uint64_t)fd);
}

static inline off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)syscall3(14, (uint64_t)fd, (uint64_t)offset, whence);
}

static inline int create(const char *filename) {
    return (int)syscall1(15, (uint64_t)filename);
}

static inline int delete(const char *filename) {
    return (int)syscall1(16, (uint64_t)filename);
}

static inline int stat(const char *path, stat_t *statbuf) {
    return (int)syscall2(17, (uint64_t)path, (uint64_t)statbuf);
}

static inline int fstat(int fd, stat_t *statbuf) {
    return (int)syscall2(18, (uint64_t)fd, (uint64_t)statbuf);
}

static inline int chdir(const char *path) {
    return (int)syscall1(19, (uint64_t)path);
}

static inline char* getcwd(char *buf, size_t size) {
    int ret = (int)syscall2(20, (uint64_t)buf, size);
    return ret == 0 ? buf : NULL;
}

static inline int mkdir(const char *pathname) {
    return (int)syscall1(21, (uint64_t)pathname);
}

static inline int rmdir(const char *pathname) {
    return (int)syscall1(22, (uint64_t)pathname);
}

static inline int ls(char *buf, size_t buf_size) {
    return (int)syscall2(44, (uint64_t)buf, buf_size);
}

static inline int brk(void *addr) {
    return (int)syscall1(23, (uint64_t)addr);
}

static inline void* sbrk(int64_t increment) {
    return (void*)syscall1(24, increment);
}

static inline void* mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)fd;
    (void)offset;
    return (void*)syscall4(25, (uint64_t)addr, length, prot, flags);
}

static inline int munmap(void *addr, size_t length) {
    return (int)syscall2(26, (uint64_t)addr, length);
}

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

static inline int gettimeofday(timeval_t *tv, void *tz) {
    (void)tz;
    return (int)syscall1(27, (uint64_t)tv);
}

static inline int clock_gettime(int clk_id, timespec_t *tp) {
    return (int)syscall2(28, clk_id, (uint64_t)tp);
}

static inline int nanosleep(const timespec_t *req, timespec_t *rem) {
    (void)rem;
    return (int)syscall1(29, (uint64_t)req);
}

static inline void sleep(uint32_t ms) {
    syscall1(30, ms);
}

static inline int socket_create(const char *name) {
    return (int)syscall1(31, (uint64_t)name);
}

static inline int socket_open(const char *name, socket_file_t **file) {
    return (int)syscall2(32, (uint64_t)name, (uint64_t)file);
}

static inline ssize_t socket_read(socket_file_t *file, void *buffer, uint32_t size, uint32_t *bytes_read) {
    return (ssize_t)syscall4(33, (uint64_t)file, (uint64_t)buffer, size, (uint64_t)bytes_read);
}

static inline ssize_t socket_write(socket_file_t *file, const void *buffer, uint32_t size) {
    return (ssize_t)syscall3(34, (uint64_t)file, (uint64_t)buffer, size);
}

static inline int socket_close(socket_file_t *file) {
    return (int)syscall1(35, (uint64_t)file);
}

static inline int socket_delete(const char *name) {
    return (int)syscall1(36, (uint64_t)name);
}

static inline int socket_exists(const char *name) {
    return (int)syscall1(37, (uint64_t)name);
}

static inline uint32_t socket_available(socket_file_t *file) {
    return (uint32_t)syscall1(38, (uint64_t)file);
}

static inline int uname(utsname_t *buf) {
    return (int)syscall1(39, (uint64_t)buf);
}

static inline void log(const char *msg, uint32_t level, uint32_t visibility) {
    syscall3(40, (uint64_t)msg, level, visibility);
}

static inline void shutdown(void) {
    syscall0(41);
}

static inline void reboot(void) {
    syscall0(42);
}

static inline int fbinfo(fb_info_t *fb) {
    return (int)syscall1(45, (uint64_t)fb);
}

static inline int is_focused(void) {
    return (int)syscall0(46);
}

typedef struct {
    uint64_t pid;
    char name[64];
} task_info_t;

static inline int list_tasks(task_info_t *infos, uint32_t max_count) {
    return (int)syscall2(49, (uint64_t)infos, max_count);
}

static inline int kill(pid_t pid) {
    return (int)syscall1(47, (uint64_t)pid);
}

static inline int wait_pid(pid_t pid) {
    return (int)syscall1(48, (uint64_t)pid);
}

static inline void halt(void){
    syscall0(50);
    return;
}


#define _UHEAP_ALIGN   16
#define _UHEAP_MINSZ   _UHEAP_ALIGN
#define _UBLK_HDR_SZ   sizeof(_ublk_t)
#define _UBLK_FOOT_SZ  sizeof(size_t)
#define _UBLK_OVERHEAD (_UBLK_HDR_SZ + _UBLK_FOOT_SZ)

typedef struct _ublk {
    size_t       size;
    int          used;
    struct _ublk *prev;
    struct _ublk *next;
} _ublk_t;

static _ublk_t *_uheap_head = (void*)0;
static _ublk_t *_uheap_tail = (void*)0;

static inline size_t *_ufoot(_ublk_t *b) {
    return (size_t*)((uint8_t*)b + _UBLK_HDR_SZ + b->size);
}

static inline void _uwrtags(_ublk_t *b) {
    *_ufoot(b) = b->size;
}

static _ublk_t *_ucoalesce(_ublk_t *b) {
    if (b->prev && !b->prev->used) {
        b->prev->size += _UBLK_OVERHEAD + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
        else _uheap_tail = b->prev;
        b = b->prev;
        _uwrtags(b);
    }
    if (b->next && !b->next->used) {
        b->size += _UBLK_OVERHEAD + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
        else _uheap_tail = b;
        _uwrtags(b);
    }
    return b;
}

static void *malloc(size_t size) {
    if (size == 0) return (void*)0;
    size_t aligned = (size + _UHEAP_ALIGN - 1) & ~(size_t)(_UHEAP_ALIGN - 1);
    if (aligned < _UHEAP_MINSZ) aligned = _UHEAP_MINSZ;

    for (_ublk_t *cur = _uheap_head; cur; cur = cur->next) {
        if (!cur->used && cur->size >= aligned) {
            if (cur->size >= aligned + _UBLK_OVERHEAD + _UHEAP_MINSZ) {
                _ublk_t *split = (_ublk_t*)((uint8_t*)cur + _UBLK_HDR_SZ + aligned + _UBLK_FOOT_SZ);
                split->size = cur->size - aligned - _UBLK_OVERHEAD;
                split->used = 0;
                split->prev = cur;
                split->next = cur->next;
                if (cur->next) cur->next->prev = split;
                else _uheap_tail = split;
                cur->next = split;
                cur->size = aligned;
                _uwrtags(split);
            }
            cur->used = 1;
            _uwrtags(cur);
            return (void*)((uint8_t*)cur + _UBLK_HDR_SZ);
        }
    }

    size_t need = _UBLK_OVERHEAD + aligned;
    _ublk_t *blk = (_ublk_t*)sbrk((int64_t)need);
    if (blk == (void*)-1) return (void*)0;
    blk->size = aligned;
    blk->used = 1;
    blk->next = (void*)0;
    blk->prev = _uheap_tail;
    if (_uheap_tail) _uheap_tail->next = blk;
    else _uheap_head = blk;
    _uheap_tail = blk;
    _uwrtags(blk);
    return (void*)((uint8_t*)blk + _UBLK_HDR_SZ);
}

static void free(void *ptr) {
    if (!ptr) return;
    _ublk_t *b = (_ublk_t*)((uint8_t*)ptr - _UBLK_HDR_SZ);
    b->used = 0;
    _ucoalesce(b);
}

static void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return (void*)0; }
    _ublk_t *b = (_ublk_t*)((uint8_t*)ptr - _UBLK_HDR_SZ);
    size_t aligned = (size + _UHEAP_ALIGN - 1) & ~(size_t)(_UHEAP_ALIGN - 1);
    if (b->size >= aligned) return ptr;
    void *n = malloc(size);
    if (!n) return (void*)0;
    size_t copy = b->size < aligned ? b->size : aligned;
    for (size_t i = 0; i < copy; i++)
        ((uint8_t*)n)[i] = ((uint8_t*)ptr)[i];
    free(ptr);
    return n;
}

#define KEY_ARROW_UP    0x01
#define KEY_ARROW_DOWN  0x02
#define KEY_ARROW_LEFT  0x03
#define KEY_ARROW_RIGHT 0x04

static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline void* memcpy(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

static inline void* memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

static inline int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static inline char* strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

static inline char* strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

static inline char* strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

static inline int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

static inline int atoi(const char *str) {
    int result = 0;
    int sign = 1;
    
    while (*str == ' ') str++;
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return result * sign;
}

#endif