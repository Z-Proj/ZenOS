#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYSCALL_EXEC             0
#define SYSCALL_EXIT             1
#define SYSCALL_GETPID           2
#define SYSCALL_GETKEY           3
#define SYSCALL_PRINTS           4
#define SYSCALL_MOUSE_X          5
#define SYSCALL_MOUSE_Y          6
#define SYSCALL_MOUSE_BTN        7
#define SYSCALL_SPEAKER          8
#define SYSCALL_SPEAKER_OFF      9
#define SYSCALL_OPEN             10
#define SYSCALL_READ             11
#define SYSCALL_WRITE            12
#define SYSCALL_CLOSE            13
#define SYSCALL_LSEEK            14
#define SYSCALL_CREATE           15
#define SYSCALL_DELETE           16
#define SYSCALL_STAT             17
#define SYSCALL_FSTAT            18
#define SYSCALL_CHDIR            19
#define SYSCALL_GETCWD           20
#define SYSCALL_MKDIR            21
#define SYSCALL_RMDIR            22
#define SYSCALL_BRK              23
#define SYSCALL_SBRK             24
#define SYSCALL_MMAP             25
#define SYSCALL_MUNMAP           26
#define SYSCALL_GETTIMEOFDAY     27
#define SYSCALL_CLOCK_GETTIME    28
#define SYSCALL_NANOSLEEP        29
#define SYSCALL_SLEEP            30
#define SYSCALL_SOCKET_CREATE    31
#define SYSCALL_SOCKET_OPEN      32
#define SYSCALL_SOCKET_READ      33
#define SYSCALL_SOCKET_WRITE     34
#define SYSCALL_SOCKET_CLOSE     35
#define SYSCALL_SOCKET_DELETE    36
#define SYSCALL_SOCKET_EXISTS    37
#define SYSCALL_SOCKET_AVAILABLE 38
#define SYSCALL_UNAME            39
#define SYSCALL_LOG              40
#define SYSCALL_SHUTDOWN         41
#define SYSCALL_REBOOT           42
#define SYSCALL_YIELD            43
#define SYSCALL_LS               44
#define SYSCALL_GET_FRAMEBUFFER  45
#define SYSCALL_IS_FOCUSED       46
#define SYSCALL_KILL             47
#define SYSCALL_WAIT_PID         48
#define SYSCALL_LIST_TASKS       49
#define SYSCALL_HALT             50
#define SYSCALL_FORK             51
#define SYSCALL_PIPE             52
#define SYSCALL_DUP              53
#define SYSCALL_DUP2             54
#define SYSCALL_OPENDIR          55
#define SYSCALL_READDIR          56
#define SYSCALL_CLOSEDIR         57
#define SYSCALL_SIGACTION        58
#define SYSCALL_SIGRETURN        59
#define SYSCALL_SIGPROCMASK      60
#define SYSCALL_NET_CONNECT      61
#define SYSCALL_NET_SEND         62
#define SYSCALL_NET_RECV         63
#define SYSCALL_NET_CLOSE        64
#define SYSCALL_NET_POLL         65
#define SYSCALL_DNS_RESOLVE      66
#define SYSCALL_FUTEX            67
#define SYSCALL_PTY_OPEN         68  
#define SYSCALL_IOCTL            69  
#define SYSCALL_SHM_CREATE       70
#define SYSCALL_SHM_OPEN         71
#define SYSCALL_SHM_CLOSE        72
#define ZEN_TIOCGWINSZ  0x5413  
#define ZEN_TIOCSWINSZ  0x5414  
#define ZEN_TIOCSPTYGID 0x5420  
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

typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} zen_winsize_t;

#define FUTEX_WAIT               0  
#define FUTEX_WAKE               1  

typedef struct {
    uint8_t  ip[4];
    uint16_t port;
} net_connect_args_t;


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
    uint64_t addr;
    uint64_t width;
    uint64_t height;
    uint8_t  bpp;
    uint32_t pitch; 
} fb_info_t;

typedef struct {
    uint64_t d_ino;
    uint8_t  d_type;
    char     d_name[256];
} zen_dirent_t;

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
    uint64_t pid;
    char name[64];
} task_info_t;

#define SHM_NAME_MAX 64
#define SHM_MAX      32

typedef struct {
    uint64_t addr;
    uint64_t size;
} shm_info_t;

void init_syscalls(void);
void syscall_prepare_user_return(uint64_t gs_base);
void syscall_prepare_sysret_return(uint64_t gs_base);
uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

#endif
