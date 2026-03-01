#ifndef USERLIB_H
#define USERLIB_H
#include "stdint.h"

typedef uint64_t size_t;
typedef int64_t  ssize_t;
typedef int64_t  off_t;
typedef int32_t  pid_t;
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint32_t nlink_t;
typedef uint64_t blksize_t;
typedef int64_t  blkcnt_t;
typedef int64_t  time_t;
typedef uint64_t uintptr_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREAT   0x040
#define O_TRUNC   0x200
#define O_APPEND  0x400
#define O_NONBLOCK 0x800

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void*)-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IRUSR  0000400
#define S_IWUSR  0000200
#define S_IXUSR  0000100
#define S_IRGRP  0000040
#define S_IWGRP  0000020
#define S_IXGRP  0000010
#define S_IROTH  0000004
#define S_IWOTH  0000002
#define S_IXOTH  0000001
#define S_IRWXU  0000700
#define S_IRWXG  0000070
#define S_IRWXO  0000007

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFEXITED(s)   (((s) & 0xff) == 0)
#define WIFSIGNALED(s) (((s) & 0xff) != 0 && ((s) & 0xff) != 0x7f)
#define WTERMSIG(s)    ((s) & 0x7f)

#define SIGKILL 9
#define SIGTERM 15
#define SIGINT  2

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

#define SOCKET_NAME_MAX 64

int errno __attribute__((weak));

static inline uint64_t _syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "rdi", "rsi", "rdx", "r10", "r8", "r9", "memory");
    return ret;
}
static inline uint64_t _syscall1(uint64_t num, uint64_t a1) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "rsi", "rdx", "r10", "r8", "r9", "memory");
    return ret;
}
static inline uint64_t _syscall2(uint64_t num, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2) : "rcx", "r11", "rdx", "r10", "r8", "r9", "memory");
    return ret;
}
static inline uint64_t _syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "r10", "r8", "r9", "memory");
    return ret;
}
static inline uint64_t _syscall4(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "r8", "r9", "memory");
    return ret;
}
static inline uint64_t _syscall5(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8  __asm__("r8")  = a5;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx", "r11", "r9", "memory");
    return ret;
}

static inline int64_t _sc_ret(uint64_t r) {
    int64_t v = (int64_t)r;
    if (v < 0) { errno = (int)(-v); return -1; }
    return v;
}

typedef struct {
    dev_t     st_dev;
    ino_t     st_ino;
    mode_t    st_mode;
    nlink_t   st_nlink;
    uid_t     st_uid;
    gid_t     st_gid;
    dev_t     st_rdev;
    off_t     st_size;
    blksize_t st_blksize;
    blkcnt_t  st_blocks;
    time_t    st_atime;
    time_t    st_mtime;
    time_t    st_ctime;
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

typedef struct {
    char     name[SOCKET_NAME_MAX];
    uint8_t *data;
    uint32_t capacity;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t available;
    uint8_t  in_use;
} socket_file_t;

typedef struct {
    uint64_t addr;
    uint64_t width;
    uint64_t height;
    uint8_t  bpp;
    uint32_t pitch;
} fb_info_t;

typedef struct {
    uint64_t pid;
    char name[64];
} task_info_t;

static inline int execv(const char *path, char *const argv[]) {
    int argc = 0;
    if (argv) while (argv[argc]) argc++;
    return (int)_sc_ret(_syscall3(0, (uint64_t)path, (uint64_t)argc, (uint64_t)argv));
}

static inline int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)envp;
    return execv(path, argv);
}

static inline void _exit(int code) {
    _syscall1(1, (uint64_t)(unsigned int)code);
    while(1);
}

static inline void exit(int code) { _exit(code); }

static inline pid_t getpid(void) { return (pid_t)_syscall0(2); }

static inline void sched_yield(void) { _syscall0(43); }

static inline int open(const char *path, int flags, ...) {
    return (int)_sc_ret(_syscall3(10, (uint64_t)path, (uint64_t)(unsigned int)flags, 0));
}

static inline ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)_sc_ret(_syscall3(11, (uint64_t)(unsigned int)fd, (uint64_t)buf, count));
}

static inline ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)_sc_ret(_syscall3(12, (uint64_t)(unsigned int)fd, (uint64_t)buf, count));
}

static inline int close(int fd) {
    if (fd < 3) return 0;
    return (int)_sc_ret(_syscall1(13, (uint64_t)(unsigned int)fd));
}

static inline off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)_sc_ret(_syscall3(14, (uint64_t)(unsigned int)fd, (uint64_t)offset, (uint64_t)(unsigned int)whence));
}

static inline int stat(const char *path, stat_t *buf) {
    return (int)_sc_ret(_syscall2(17, (uint64_t)path, (uint64_t)buf));
}

static inline int fstat(int fd, stat_t *buf) {
    return (int)_sc_ret(_syscall2(18, (uint64_t)(unsigned int)fd, (uint64_t)buf));
}

static inline int chdir(const char *path) {
    return (int)_sc_ret(_syscall1(19, (uint64_t)path));
}

static inline char *getcwd(char *buf, size_t size) {
    return _syscall2(20, (uint64_t)buf, size) == 0 ? buf : NULL;
}

static inline int mkdir(const char *path, mode_t mode) {
    (void)mode;
    return (int)_sc_ret(_syscall1(21, (uint64_t)path));
}

static inline int rmdir(const char *path) {
    return (int)_sc_ret(_syscall1(22, (uint64_t)path));
}

static inline int unlink(const char *path) {
    return (int)_sc_ret(_syscall1(16, (uint64_t)path));
}

static inline int rename(const char *old, const char *newp) {
    (void)old; (void)newp; errno = 38; return -1;
}

static inline int brk(void *addr) {
    return (int)_sc_ret(_syscall1(23, (uint64_t)addr));
}

static inline void *sbrk(intptr_t inc) {
    return (void*)_syscall1(24, (uint64_t)inc);
}

static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    (void)fd; (void)off;
    uint64_t r = _syscall4(25, (uint64_t)addr, len, (uint64_t)(unsigned int)prot, (uint64_t)(unsigned int)flags);
    if ((int64_t)r < 0) { errno = (int)(-(int64_t)r); return MAP_FAILED; }
    return (void*)r;
}

static inline int munmap(void *addr, size_t len) {
    return (int)_sc_ret(_syscall2(26, (uint64_t)addr, len));
}

static inline int gettimeofday(timeval_t *tv, void *tz) {
    (void)tz;
    return (int)_sc_ret(_syscall1(27, (uint64_t)tv));
}

static inline int clock_gettime(int clk, timespec_t *tp) {
    return (int)_sc_ret(_syscall2(28, (uint64_t)(unsigned int)clk, (uint64_t)tp));
}

static inline int nanosleep(const timespec_t *req, timespec_t *rem) {
    (void)rem;
    return (int)_sc_ret(_syscall1(29, (uint64_t)req));
}

static inline int kill(pid_t pid, int sig) {
    return (int)_sc_ret(_syscall2(47, (uint64_t)(unsigned int)pid, (uint64_t)(unsigned int)sig));
}

static inline pid_t waitpid(pid_t pid, int *wstatus, int opts) {
    (void)opts;
    int r = (int)_sc_ret(_syscall2(48, (uint64_t)(unsigned int)pid, (uint64_t)wstatus));
    if (r < 0) return -1;
    return pid;
}

static inline pid_t wait(int *wstatus) {
    return waitpid(-1, wstatus, 0);
}

static inline int uname(utsname_t *buf) {
    return (int)_sc_ret(_syscall1(39, (uint64_t)buf));
}

static inline int isatty(int fd) {
    return (fd==STDIN_FILENO||fd==STDOUT_FILENO||fd==STDERR_FILENO) ? 1 : 0;
}

static inline uid_t getuid(void)  { return 0; }
static inline gid_t getgid(void)  { return 0; }
static inline uid_t geteuid(void) { return 0; }
static inline gid_t getegid(void) { return 0; }

static inline int access(const char *path, int mode) {
    (void)mode; stat_t st; return stat(path, &st);
}

static inline int dup(int fd)              { (void)fd; errno=38; return -1; }
static inline int dup2(int o, int n)       { (void)o; (void)n; errno=38; return -1; }
static inline int pipe(int pfd[2])         { (void)pfd; errno=38; return -1; }
static inline int link(const char *o, const char *n) { (void)o; (void)n; errno=38; return -1; }
static inline int chmod(const char *p, mode_t m) { (void)p; (void)m; return 0; }
static inline int chown(const char *p, uid_t u, gid_t g) { (void)p; (void)u; (void)g; return 0; }
static inline int fchmod(int fd, mode_t m) { (void)fd; (void)m; return 0; }
static inline int ftruncate(int fd, off_t len) { (void)fd; (void)len; errno=38; return -1; }
static inline int truncate(const char *p, off_t len) { (void)p; (void)len; errno=38; return -1; }
static inline int fsync(int fd) { (void)fd; return 0; }
static inline int fdatasync(int fd) { (void)fd; return 0; }
static inline int sync(void) { return 0; }
static inline unsigned int sleep(unsigned int s) {
    timespec_t t = {(int64_t)s, 0};
    nanosleep(&t, NULL);
    return 0;
}
static inline int usleep(unsigned long us) {
    timespec_t t = {(int64_t)(us/1000000), (int64_t)((us%1000000)*1000)};
    return nanosleep(&t, NULL);
}

static inline int socket_create(const char *name) {
    return (int)_sc_ret(_syscall1(31, (uint64_t)name));
}
static inline int socket_open(const char *name, socket_file_t **file) {
    return (int)_sc_ret(_syscall2(32, (uint64_t)name, (uint64_t)file));
}
static inline ssize_t socket_read(socket_file_t *file, void *buf, uint32_t size, uint32_t *bytes_read) {
    return (ssize_t)_sc_ret(_syscall4(33, (uint64_t)file, (uint64_t)buf, size, (uint64_t)bytes_read));
}
static inline ssize_t socket_write(socket_file_t *file, const void *buf, uint32_t size) {
    return (ssize_t)_sc_ret(_syscall3(34, (uint64_t)file, (uint64_t)buf, size));
}
static inline int socket_close(socket_file_t *file) {
    return (int)_sc_ret(_syscall1(35, (uint64_t)file));
}
static inline int socket_delete(const char *name) {
    return (int)_sc_ret(_syscall1(36, (uint64_t)name));
}
static inline int socket_exists(const char *name) {
    return (int)_sc_ret(_syscall1(37, (uint64_t)name));
}
static inline uint32_t socket_available(socket_file_t *file) {
    return (uint32_t)_syscall1(38, (uint64_t)file);
}

static inline int zen_getkey(void)            { return (int)_syscall0(3); }
static inline uint32_t zen_mouse_x(void)      { return (uint32_t)_syscall0(5); }
static inline uint32_t zen_mouse_y(void)      { return (uint32_t)_syscall0(6); }
static inline uint8_t  zen_mouse_btn(void)    { return (uint8_t)_syscall0(7); }
static inline void     zen_speaker(uint32_t hz){ _syscall1(8, hz); }
static inline void     zen_speaker_off(void)  { _syscall0(9); }
static inline int      zen_ls(char *buf, size_t sz) { return (int)_sc_ret(_syscall2(44, (uint64_t)buf, sz)); }
static inline int      zen_fbinfo(fb_info_t *fb)    { return (int)_sc_ret(_syscall1(45, (uint64_t)fb)); }
static inline int      zen_is_focused(void)   { return (int)_syscall0(46); }
static inline int      zen_list_tasks(task_info_t *infos, uint32_t max) { return (int)_sc_ret(_syscall2(49, (uint64_t)infos, max)); }
static inline void     zen_shutdown(void)     { _syscall0(41); }
static inline void     zen_reboot(void)       { _syscall0(42); }
static inline void     zen_halt(void)         { _syscall0(50); }
static inline void     zen_sleep_ms(uint32_t ms) { _syscall1(30, ms); }
static inline void     zen_log(const char *msg, uint32_t level, uint32_t vis) { _syscall3(40, (uint64_t)msg, level, vis); }
static inline int      zen_create(const char *path) { return (int)_sc_ret(_syscall1(15, (uint64_t)path)); }

#define KEY_ARROW_UP    0x01
#define KEY_ARROW_DOWN  0x02
#define KEY_ARROW_LEFT  0x03
#define KEY_ARROW_RIGHT 0x04

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
static inline void _uwrtags(_ublk_t *b) { *_ufoot(b) = b->size; }

static _ublk_t *_ucoalesce(_ublk_t *b) {
    if (b->prev && !b->prev->used) {
        b->prev->size += _UBLK_OVERHEAD + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev; else _uheap_tail = b->prev;
        b = b->prev; _uwrtags(b);
    }
    if (b->next && !b->next->used) {
        b->size += _UBLK_OVERHEAD + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b; else _uheap_tail = b;
        _uwrtags(b);
    }
    return b;
}

static void *malloc(size_t size) {
    if (!size) return NULL;
    size_t aligned = (size + _UHEAP_ALIGN - 1) & ~(size_t)(_UHEAP_ALIGN - 1);
    if (aligned < _UHEAP_MINSZ) aligned = _UHEAP_MINSZ;
    for (_ublk_t *cur = _uheap_head; cur; cur = cur->next) {
        if (!cur->used && cur->size >= aligned) {
            if (cur->size >= aligned + _UBLK_OVERHEAD + _UHEAP_MINSZ) {
                _ublk_t *split = (_ublk_t*)((uint8_t*)cur + _UBLK_HDR_SZ + aligned + _UBLK_FOOT_SZ);
                split->size = cur->size - aligned - _UBLK_OVERHEAD;
                split->used = 0; split->prev = cur; split->next = cur->next;
                if (cur->next) cur->next->prev = split; else _uheap_tail = split;
                cur->next = split; cur->size = aligned; _uwrtags(split);
            }
            cur->used = 1; _uwrtags(cur);
            return (void*)((uint8_t*)cur + _UBLK_HDR_SZ);
        }
    }
    size_t need = _UBLK_OVERHEAD + aligned;
    _ublk_t *blk = (_ublk_t*)sbrk((intptr_t)need);
    if (blk == (void*)-1) return NULL;
    blk->size = aligned; blk->used = 1; blk->next = NULL; blk->prev = _uheap_tail;
    if (_uheap_tail) _uheap_tail->next = blk; else _uheap_head = blk;
    _uheap_tail = blk; _uwrtags(blk);
    return (void*)((uint8_t*)blk + _UBLK_HDR_SZ);
}

static void free(void *ptr) {
    if (!ptr) return;
    _ublk_t *b = (_ublk_t*)((uint8_t*)ptr - _UBLK_HDR_SZ);
    b->used = 0; _ucoalesce(b);
}

static void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (!size) { free(ptr); return NULL; }
    _ublk_t *b = (_ublk_t*)((uint8_t*)ptr - _UBLK_HDR_SZ);
    size_t aligned = (size + _UHEAP_ALIGN - 1) & ~(size_t)(_UHEAP_ALIGN - 1);
    if (b->size >= aligned) return ptr;
    void *n = malloc(size);
    if (!n) return NULL;
    size_t copy = b->size < aligned ? b->size : aligned;
    for (size_t i = 0; i < copy; i++) ((uint8_t*)n)[i] = ((uint8_t*)ptr)[i];
    free(ptr); return n;
}

static inline void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) { uint8_t *b=(uint8_t*)p; for(size_t i=0;i<total;i++) b[i]=0; }
    return p;
}

static inline size_t strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }

static inline void *memcpy(void *d, const void *s, size_t n) {
    char *dd=(char*)d; const char *ss=(const char*)s;
    for(size_t i=0;i<n;i++) dd[i]=ss[i]; return d;
}
static inline void *memset(void *s, int c, size_t n) {
    unsigned char *p=(unsigned char*)s;
    for(size_t i=0;i<n;i++) p[i]=(unsigned char)c; return s;
}
static inline int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1=(const unsigned char*)s1, *p2=(const unsigned char*)s2;
    for(size_t i=0;i<n;i++) if(p1[i]!=p2[i]) return p1[i]-p2[i]; return 0;
}
static inline void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d=(uint8_t*)dest; const uint8_t *s=(const uint8_t*)src;
    if(d<s) { for(size_t i=0;i<n;i++) d[i]=s[i]; }
    else { for(size_t i=n;i>0;i--) d[i-1]=s[i-1]; }
    return dest;
}
static inline void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p=(const uint8_t*)s;
    for(size_t i=0;i<n;i++) if(p[i]==(uint8_t)c) return (void*)(p+i); return NULL;
}
static inline int strcmp(const char *s1, const char *s2) {
    while(*s1 && (*s1==*s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}
static inline int strncmp(const char *s1, const char *s2, size_t n) {
    for(size_t i=0;i<n;i++) {
        if(s1[i]!=s2[i]) return (unsigned char)s1[i]-(unsigned char)s2[i];
        if(!s1[i]) return 0;
    } return 0;
}
static inline char *strcpy(char *d, const char *s) {
    char *r=d; while((*d++=*s++)) {} return r;
}
static inline char *strncpy(char *d, const char *s, size_t n) {
    size_t i; for(i=0;i<n&&s[i];i++) d[i]=s[i]; for(;i<n;i++) d[i]='\0'; return d;
}
static inline char *strcat(char *d, const char *s) { char *r=d; while(*d) d++; while((*d++=*s++)) {} return r; }
static inline char *strncat(char *d, const char *s, size_t n) {
    char *r=d; while(*d) d++; size_t i; for(i=0;i<n&&s[i];i++) d[i]=s[i]; d[i]='\0'; return r;
}
static inline char *strchr(const char *s, int c) {
    while(*s) { if(*s==(char)c) return (char*)s; s++; }
    return (c=='\0') ? (char*)s : NULL;
}
static inline char *strrchr(const char *s, int c) {
    const char *l=NULL; while(*s) { if(*s==(char)c) l=s; s++; } return (char*)l;
}
static inline char *strstr(const char *h, const char *n) {
    size_t nl=strlen(n); if(!nl) return (char*)h;
    while(*h) { if(!strncmp(h,n,nl)) return (char*)h; h++; } return NULL;
}
static inline size_t strnlen(const char *s, size_t max) { size_t i=0; while(i<max&&s[i]) i++; return i; }
static inline char *strdup(const char *s) {
    size_t l=strlen(s)+1; char *p=(char*)malloc(l); if(p) memcpy(p,s,l); return p;
}
static inline char *strndup(const char *s, size_t n) {
    size_t l=strnlen(s,n); char *p=(char*)malloc(l+1);
    if(p) { memcpy(p,s,l); p[l]='\0'; } return p;
}

static inline int atoi(const char *s) {
    int r=0,sign=1; while(*s==' ') s++;
    if(*s=='-'){sign=-1;s++;} else if(*s=='+') s++;
    while(*s>='0'&&*s<='9'){r=r*10+(*s-'0');s++;}
    return r*sign;
}
static inline long strtol(const char *s, char **end, int base) {
    while(*s==' '||*s=='\t') s++;
    long r=0; int sign=1;
    if(*s=='-'){sign=-1;s++;} else if(*s=='+') s++;
    if(base==0) {
        if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;s+=2;}
        else if(s[0]=='0'){base=8;s++;}
        else base=10;
    } else if(base==16&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')) s+=2;
    const char *start=s;
    while(1) {
        int d; char c=*s;
        if(c>='0'&&c<='9') d=c-'0';
        else if(c>='a'&&c<='z') d=c-'a'+10;
        else if(c>='A'&&c<='Z') d=c-'A'+10;
        else break;
        if(d>=base) break;
        r=r*base+d; s++;
    }
    if(end) *end=(char*)(s==start?s:s);
    return sign*r;
}
static inline unsigned long strtoul(const char *s, char **e, int b) { return (unsigned long)strtol(s,e,b); }
static inline long long strtoll(const char *s, char **e, int b)     { return (long long)strtol(s,e,b); }
static inline unsigned long long strtoull(const char *s, char **e, int b) { return (unsigned long long)strtol(s,e,b); }
static inline long atol(const char *s)  { return strtol(s,NULL,10); }
static inline long long atoll(const char *s) { return (long long)strtol(s,NULL,10); }

static inline int isdigit(int c) { return c>='0'&&c<='9'; }
static inline int isalpha(int c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
static inline int isalnum(int c) { return isdigit(c)||isalpha(c); }
static inline int isspace(int c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
static inline int isupper(int c) { return c>='A'&&c<='Z'; }
static inline int islower(int c) { return c>='a'&&c<='z'; }
static inline int isprint(int c) { return c>=0x20&&c<0x7f; }
static inline int iscntrl(int c) { return (c>=0&&c<0x20)||c==0x7f; }
static inline int ispunct(int c) { return isprint(c)&&!isalnum(c)&&c!=' '; }
static inline int isxdigit(int c){ return isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
static inline int toupper(int c) { return islower(c)?c-32:c; }
static inline int tolower(int c) { return isupper(c)?c+32:c; }

#endif
