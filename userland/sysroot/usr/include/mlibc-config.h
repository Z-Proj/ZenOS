#ifndef _MLIBC_CONFIG_H
#define _MLIBC_CONFIG_H

#ifdef _GNU_SOURCE
#	undef _DEFAULT_SOURCE
#	define _DEFAULT_SOURCE 1
#endif

#if (defined(_DEFAULT_SOURCE) || (!defined(__STRICT_ANSI__) && !defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)))
#	undef _DEFAULT_SOURCE
#	define _DEFAULT_SOURCE	1
#endif

#define __MLIBC_BSD_OPTION 0
#define __MLIBC_POSIX_OPTION 1
#define __MLIBC_LINUX_OPTION 0
#define __MLIBC_GLIBC_OPTION 0
#define __MLIBC_SYSDEP_HAS_BITS_SYSCALL_H 0

#endif /* _MLIBC_CONFIG_H */
