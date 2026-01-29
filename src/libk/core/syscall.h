#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

// Process Management
#define SYSCALL_EXEC        0
#define SYSCALL_EXIT        1
#define SYSCALL_GETPID      2

// Input/Output
#define SYSCALL_GETKEY      3
#define SYSCALL_PRINTS      4

// Mouse
#define SYSCALL_MOUSE_X     5
#define SYSCALL_MOUSE_Y     6
#define SYSCALL_MOUSE_BTN   7

// Speaker
#define SYSCALL_SPEAKER     8
#define SYSCALL_SPEAKER_OFF 9

// File Operations
#define SYSCALL_OPEN        10
#define SYSCALL_READ        11
#define SYSCALL_WRITE       12
#define SYSCALL_CLOSE       13
#define SYSCALL_LSEEK       14
#define SYSCALL_CREATE      15
#define SYSCALL_DELETE      16
#define SYSCALL_STAT        17
#define SYSCALL_FSTAT       18

// Directory Operations
#define SYSCALL_CHDIR       19
#define SYSCALL_GETCWD      20
#define SYSCALL_MKDIR       21
#define SYSCALL_RMDIR       22

// Memory Management
#define SYSCALL_BRK         23
#define SYSCALL_SBRK        24
#define SYSCALL_MMAP        25
#define SYSCALL_MUNMAP      26

// Time
#define SYSCALL_GETTIMEOFDAY 27
#define SYSCALL_CLOCK_GETTIME 28
#define SYSCALL_NANOSLEEP   29
#define SYSCALL_SLEEP       30

// IPC - Socket (your existing socket.h)
#define SYSCALL_SOCKET_CREATE 31
#define SYSCALL_SOCKET_OPEN   32
#define SYSCALL_SOCKET_READ   33
#define SYSCALL_SOCKET_WRITE  34
#define SYSCALL_SOCKET_CLOSE  35
#define SYSCALL_SOCKET_DELETE 36
#define SYSCALL_SOCKET_EXISTS 37
#define SYSCALL_SOCKET_AVAILABLE 38

// System Info
#define SYSCALL_UNAME       39

// Logging and System
#define SYSCALL_LOG         40
#define SYSCALL_SHUTDOWN    41
#define SYSCALL_REBOOT      42

// Yield
#define SYSCALL_YIELD       43

// stat structure for file info
typedef struct {
    uint32_t st_dev;        // device ID
    uint32_t st_ino;        // inode number
    uint32_t st_mode;       // file mode (type & permissions)
    uint32_t st_nlink;      // number of hard links
    uint32_t st_uid;        // user ID
    uint32_t st_gid;        // group ID
    uint32_t st_rdev;       // device ID (if special file)
    uint64_t st_size;       // file size in bytes
    uint64_t st_blksize;    // block size
    uint64_t st_blocks;     // number of blocks
    uint64_t st_atime;      // last access time
    uint64_t st_mtime;      // last modification time
    uint64_t st_ctime;      // last status change time
} stat_t;

// timeval structure
typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} timeval_t;

// timespec structure
typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} timespec_t;

// system info
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
} utsname_t;

void init_syscalls(void);
uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

#endif