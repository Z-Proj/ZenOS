#include <errno.h>
#include <stdarg.h>
#include <sys/ioctl.h>

#include <bits/ensure.h>
#include <mlibc/posix-sysdeps.hpp>

int ioctl(int fd, unsigned long request, ...) {
	va_list args;
	va_start(args, request);
	void *arg = va_arg(args, void *);
	va_end(args);

	int result;
	MLIBC_CHECK_OR_ENOSYS(mlibc::sys_ioctl, -1);
	if(int e = mlibc::sys_ioctl(fd, request, arg, &result); e) {
		errno = e;
		return -1;
	}
	return result;
}
