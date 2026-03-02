/*
 * sys/mman.h -- ZenOS stub for newlib
 * mmap/munmap are implemented in syscalls.c
 * Constants match Linux/POSIX values used by ZenOS kernel
 */
#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <sys/types.h>

#define PROT_NONE   0x00
#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define PROT_EXEC   0x04

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS

#define MAP_FAILED  ((void *)-1)

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int   munmap(void *addr, size_t len);

#endif /* _SYS_MMAN_H */
