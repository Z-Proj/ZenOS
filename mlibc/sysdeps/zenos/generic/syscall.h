#ifndef MLIBC_ZENOS_SYSCALL_H
#define MLIBC_ZENOS_SYSCALL_H

#include <stdint.h>

#define ZENOS_SYSCALL_EXEC 0
#define ZENOS_SYSCALL_EXIT 1
#define ZENOS_SYSCALL_GETPID 2
#define ZENOS_SYSCALL_OPEN 10
#define ZENOS_SYSCALL_READ 11
#define ZENOS_SYSCALL_WRITE 12
#define ZENOS_SYSCALL_CLOSE 13
#define ZENOS_SYSCALL_LSEEK 14
#define ZENOS_SYSCALL_DELETE 16
#define ZENOS_SYSCALL_STAT 17
#define ZENOS_SYSCALL_FSTAT 18
#define ZENOS_SYSCALL_CHDIR 19
#define ZENOS_SYSCALL_GETCWD 20
#define ZENOS_SYSCALL_MKDIR 21
#define ZENOS_SYSCALL_RMDIR 22
#define ZENOS_SYSCALL_MMAP 25
#define ZENOS_SYSCALL_MUNMAP 26
#define ZENOS_SYSCALL_CLOCK_GETTIME 28
#define ZENOS_SYSCALL_NANOSLEEP 29
#define ZENOS_SYSCALL_UNAME 39
#define ZENOS_SYSCALL_LOG 40
#define ZENOS_SYSCALL_YIELD 43
#define ZENOS_SYSCALL_KILL 47
#define ZENOS_SYSCALL_WAIT_PID 48
#define ZENOS_SYSCALL_HALT 50
#define ZENOS_SYSCALL_FORK 51
#define ZENOS_SYSCALL_PIPE 52
#define ZENOS_SYSCALL_DUP 53
#define ZENOS_SYSCALL_DUP2 54
#define ZENOS_SYSCALL_OPENDIR 55
#define ZENOS_SYSCALL_READDIR 56
#define ZENOS_SYSCALL_CLOSEDIR 57
#define ZENOS_SYSCALL_SIGACTION 58
#define ZENOS_SYSCALL_SIGRETURN 59
#define ZENOS_SYSCALL_SIGPROCMASK 60
#define ZENOS_SYSCALL_FUTEX 67
#define ZENOS_SYSCALL_IOCTL 69
#define ZENOS_SYSCALL_ARCH_PRCTL 74
#define ZENOS_SYSCALL_MPROTECT 75
#define ZENOS_SYSCALL_UNIX_SOCKET    76
#define ZENOS_SYSCALL_UNIX_BIND      77
#define ZENOS_SYSCALL_UNIX_LISTEN    78
#define ZENOS_SYSCALL_UNIX_ACCEPT    79
#define ZENOS_SYSCALL_UNIX_CONNECT   80
#define ZENOS_SYSCALL_UNIX_SEND      81
#define ZENOS_SYSCALL_UNIX_RECV      82
#define ZENOS_SYSCALL_UNIX_SHUTDOWN  83
#define ZENOS_SYSCALL_GETSOCKNAME    84
#define ZENOS_SYSCALL_GETPEERNAME    86
#define ZENOS_SYSCALL_UNIX_SOCKETPAIR 87

#define ZENOS_FUTEX_WAIT 0
#define ZENOS_FUTEX_WAKE 1

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

struct zenos_stat {
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
	uint64_t atime_sec;
	uint64_t mtime_sec;
	uint64_t ctime_sec;
};

struct zenos_dirent {
	uint64_t d_ino;
	uint8_t d_type;
	char d_name[256];
};

struct zenos_sigaction {
	uint64_t handler;
	uint64_t flags;
	uint64_t mask;
};

static inline long zenos_do_syscall0(long num) {
	long ret;
	asm volatile("syscall"
		: "=a"(ret)
		: "a"(num)
		: "rcx", "r11", "rdi", "rsi", "rdx", "r10", "r8", "r9", "cc", "memory");
	return ret;
}

static inline long zenos_do_syscall1(long num, long a1) {
	long ret;
	register long rdi asm("rdi") = a1;
	asm volatile("syscall"
		: "=a"(ret), "+D"(rdi)
		: "a"(num)
		: "rcx", "r11", "rsi", "rdx", "r10", "r8", "r9", "cc", "memory");
	return ret;
}

static inline long zenos_do_syscall2(long num, long a1, long a2) {
	long ret;
	register long rdi asm("rdi") = a1;
	register long rsi asm("rsi") = a2;
	asm volatile("syscall"
		: "=a"(ret), "+D"(rdi), "+S"(rsi)
		: "a"(num)
		: "rcx", "r11", "rdx", "r10", "r8", "r9", "cc", "memory");
	return ret;
}

static inline long zenos_do_syscall3(long num, long a1, long a2, long a3) {
	long ret;
	register long rdi asm("rdi") = a1;
	register long rsi asm("rsi") = a2;
	register long rdx asm("rdx") = a3;
	asm volatile("syscall"
		: "=a"(ret), "+D"(rdi), "+S"(rsi), "+d"(rdx)
		: "a"(num)
		: "rcx", "r11", "r10", "r8", "r9", "cc", "memory");
	return ret;
}

static inline long zenos_do_syscall4(long num, long a1, long a2, long a3, long a4) {
	long ret;
	register long rdi asm("rdi") = a1;
	register long rsi asm("rsi") = a2;
	register long rdx asm("rdx") = a3;
	register long r10 asm("r10") = a4;
	asm volatile("syscall"
		: "=a"(ret), "+D"(rdi), "+S"(rsi), "+d"(rdx), "+r"(r10)
		: "a"(num)
		: "rcx", "r11", "r8", "r9", "cc", "memory");
	return ret;
}

static inline long zenos_do_syscall5(long num, long a1, long a2, long a3, long a4, long a5) {
	long ret;
	register long rdi asm("rdi") = a1;
	register long rsi asm("rsi") = a2;
	register long rdx asm("rdx") = a3;
	register long r10 asm("r10") = a4;
	register long r8 asm("r8") = a5;
	asm volatile("syscall"
		: "=a"(ret), "+D"(rdi), "+S"(rsi), "+d"(rdx), "+r"(r10), "+r"(r8)
		: "a"(num)
		: "rcx", "r11", "r9", "cc", "memory");
	return ret;
}

#endif
