#ifndef USERLIB_H
#define USERLIB_H

typedef unsigned long uint64_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef long int64_t;
typedef int int32_t;
typedef short int16_t;
typedef char int8_t;

typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int32_t pid_t;

#define NULL ((void*)0)

// ==================== SYSCALL INTERFACE ====================

static inline uint64_t syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall1(uint64_t num, uint64_t arg1) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = arg4;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = arg4;
    register uint64_t r8 __asm__("r8") = arg5;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return ret;
}

// ==================== STRUCTURES ====================

// File stat structure
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

// Time structures
typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} timeval_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} timespec_t;

// System info
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
} utsname_t;

// ZFS file structure (matches kernel)
#define ZFS_MAX_FILENAME 28
typedef struct {
    char name[ZFS_MAX_FILENAME];
    uint32_t size;
    uint32_t start_block;
    uint32_t position;
    uint8_t entry_index;
    uint8_t is_open;
} zfs_file_t;

// Socket file structure (matches kernel)
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

// ==================== PROCESS MANAGEMENT ====================

static inline int exec(const char *filename) {
    return (int)syscall1(0, (uint64_t)filename);
}

static inline int execv(const char *filename, char *const argv[]) {
    return (int)syscall3(0, (uint64_t)filename, 0, (uint64_t)argv);
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

// ==================== INPUT/OUTPUT ====================

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

// ==================== MOUSE ====================

static inline uint32_t mouse_x(void) {
    return (uint32_t)syscall0(5);
}

static inline uint32_t mouse_y(void) {
    return (uint32_t)syscall0(6);
}

static inline uint8_t mouse_button(void) {
    return (uint8_t)syscall0(7);
}

// ==================== SPEAKER ====================

static inline void speaker_play(uint32_t hz) {
    syscall1(8, hz);
}

static inline void speaker_stop(void) {
    syscall0(9);
}

// ==================== FILE OPERATIONS ====================

static inline int open(const char *filename, zfs_file_t *file) {
    return (int)syscall2(10, (uint64_t)filename, (uint64_t)file);
}

static inline ssize_t read(zfs_file_t *file, void *buffer, size_t size) {
    uint32_t bytes_read = 0;
    int ret = (int)syscall4(11, (uint64_t)file, (uint64_t)buffer, size, (uint64_t)&bytes_read);
    if (ret < 0) return ret;
    return bytes_read;
}

static inline ssize_t write(zfs_file_t *file, const void *buffer, size_t size) {
    return (ssize_t)syscall3(12, (uint64_t)file, (uint64_t)buffer, size);
}

static inline int close(zfs_file_t *file) {
    return (int)syscall1(13, (uint64_t)file);
}

static inline off_t lseek(zfs_file_t *file, off_t offset, int whence) {
    return (off_t)syscall3(14, (uint64_t)file, offset, whence);
}

static inline int create(const char *filename, uint32_t size) {
    return (int)syscall2(15, (uint64_t)filename, size);
}

static inline int delete(const char *filename) {
    return (int)syscall1(16, (uint64_t)filename);
}

static inline int stat(const char *path, stat_t *statbuf) {
    return (int)syscall2(17, (uint64_t)path, (uint64_t)statbuf);
}

static inline int fstat(zfs_file_t *file, stat_t *statbuf) {
    return (int)syscall2(18, (uint64_t)file, (uint64_t)statbuf);
}

// ==================== DIRECTORY OPERATIONS ====================

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

// ==================== MEMORY MANAGEMENT ====================

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

// mmap flags
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

// ==================== TIME ====================

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

// ==================== IPC - SOCKET (your custom system) ====================

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

// ==================== SYSTEM INFO ====================

static inline int uname(utsname_t *buf) {
    return (int)syscall1(39, (uint64_t)buf);
}

// ==================== LOGGING AND SYSTEM ====================

static inline void log(const char *msg, uint32_t level, uint32_t visibility) {
    syscall3(40, (uint64_t)msg, level, visibility);
}

static inline void shutdown(void) {
    syscall0(41);
}

static inline void reboot(void) {
    syscall0(42);
}

// ==================== HELPER FUNCTIONS ====================

// Simple strlen
static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

// Simple memcpy
static inline void* memcpy(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

// Simple memset
static inline void* memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

// Simple strcmp
static inline int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// Simple strcpy
static inline char* strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

// Simple strncpy
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

// Simple strcat
static inline char* strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

// Simple memcmp
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

// Simple atoi
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