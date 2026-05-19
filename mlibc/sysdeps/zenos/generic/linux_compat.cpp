#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <sched.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include "syscall.h"

namespace {

int finish(long ret) {
	if(ret < 0) {
		errno = static_cast<int>(-ret);
		return -1;
	}
	return static_cast<int>(ret);
}

void fill_statfs(struct statfs *dst, const zenos_statfs &src) {
	memset(dst, 0, sizeof(*dst));
	dst->f_type = static_cast<long>(src.f_type);
	dst->f_bsize = static_cast<long>(src.f_bsize);
	dst->f_blocks = src.f_blocks;
	dst->f_bfree = src.f_bfree;
	dst->f_bavail = src.f_bavail;
	dst->f_files = src.f_files;
	dst->f_ffree = src.f_ffree;
	dst->f_fsid.__val[0] = static_cast<int>(src.f_fsid & 0xffffffffUL);
	dst->f_fsid.__val[1] = static_cast<int>((src.f_fsid >> 32) & 0xffffffffUL);
	dst->f_namelen = static_cast<long>(src.f_namelen);
	dst->f_frsize = static_cast<long>(src.f_frsize);
	dst->f_flags = static_cast<long>(src.f_flags);
}

}

extern "C" int statfs(const char *path, struct statfs *buf) {
	if(!buf) {
		errno = EINVAL;
		return -1;
	}
	zenos_statfs zst{};
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_STATFS,
			reinterpret_cast<long>(path), reinterpret_cast<long>(&zst));
	if(ret < 0) {
		errno = static_cast<int>(-ret);
		return -1;
	}
	fill_statfs(buf, zst);
	return 0;
}

extern "C" int fstatfs(int fd, struct statfs *buf) {
	if(!buf) {
		errno = EINVAL;
		return -1;
	}
	zenos_statfs zst{};
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_FSTATFS,
			fd, reinterpret_cast<long>(&zst));
	if(ret < 0) {
		errno = static_cast<int>(-ret);
		return -1;
	}
	fill_statfs(buf, zst);
	return 0;
}

extern "C" int fstatfs64(int fd, statfs64 *buf) {
	return fstatfs(fd, reinterpret_cast<struct statfs *>(buf));
}

extern "C" int prctl(int option, ...) {
	va_list ap;
	va_start(ap, option);
	unsigned long arg2 = va_arg(ap, unsigned long);
	unsigned long arg3 = va_arg(ap, unsigned long);
	unsigned long arg4 = va_arg(ap, unsigned long);
	unsigned long arg5 = va_arg(ap, unsigned long);
	va_end(ap);

	long ret = zenos_do_syscall5(ZENOS_SYSCALL_PRCTL, option, arg2, arg3, arg4, arg5);
	return finish(ret);
}

extern "C" int sysinfo(struct sysinfo *info) {
	if(!info) {
		errno = EINVAL;
		return -1;
	}

	zenos_sysinfo zi{};
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_SYSINFO, reinterpret_cast<long>(&zi));
	if(ret < 0) {
		errno = static_cast<int>(-ret);
		return -1;
	}

	memset(info, 0, sizeof(*info));
	info->uptime = zi.uptime;
	info->loads[0] = zi.loads[0];
	info->loads[1] = zi.loads[1];
	info->loads[2] = zi.loads[2];
	info->totalram = zi.totalram;
	info->freeram = zi.freeram;
	info->sharedram = zi.sharedram;
	info->bufferram = zi.bufferram;
	info->totalswap = zi.totalswap;
	info->freeswap = zi.freeswap;
	info->procs = zi.procs;
	info->totalhigh = zi.totalhigh;
	info->freehigh = zi.freehigh;
	info->mem_unit = zi.mem_unit;
	return 0;
}

extern "C" int get_nprocs(void) {
	cpu_set_t mask{};
	if(sched_getaffinity(0, sizeof(mask), &mask) < 0)
		return 1;

	int count = 0;
	const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&mask);
	for(size_t i = 0; i < sizeof(mask); i++) {
		unsigned char byte = bytes[i];
		while(byte) {
			count += byte & 1U;
			byte >>= 1;
		}
	}
	return count > 0 ? count : 1;
}

extern "C" int get_nprocs_conf(void) {
	return get_nprocs();
}

extern "C" int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_SCHED_GETAFFINITY,
			pid, cpusetsize, reinterpret_cast<long>(mask));
	return finish(ret);
}

extern "C" int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_SCHED_SETAFFINITY,
			pid, cpusetsize, reinterpret_cast<long>(mask));
	return finish(ret);
}
