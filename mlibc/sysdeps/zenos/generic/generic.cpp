#include <asm/ioctls.h>
#include <bits/ensure.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <termios.h>

#include <frg/manual_box.hpp>
#include <mlibc/all-sysdeps.hpp>
#include <mlibc/debug.hpp>
#include <mlibc/fsfd_target.hpp>

#include "syscall.h"

namespace {

static int sc_error(long ret) {
	return ret < 0 ? static_cast<int>(-ret) : 0;
}

static int finish_fd(long ret, int *fd) {
	int e = sc_error(ret);
	if(e)
		return e;
	*fd = static_cast<int>(ret);
	return 0;
}

static int translate_status(long kernel_ret, int *status, pid_t *ret_pid) {
	int e = sc_error(kernel_ret);
	if(e)
		return e;
	if(ret_pid)
		*ret_pid = static_cast<pid_t>(kernel_ret);
	if(status && kernel_ret > 0)
		*status &= 0xffff;
	return 0;
}

static void translate_stat(const zenos_stat &in, struct stat *out) {
	memset(out, 0, sizeof(*out));
	out->st_dev = in.st_dev;
	out->st_ino = in.st_ino;
	out->st_mode = in.st_mode;
	out->st_nlink = in.st_nlink;
	out->st_uid = in.st_uid;
	out->st_gid = in.st_gid;
	out->st_rdev = in.st_rdev;
	out->st_size = in.st_size;
	out->st_blksize = in.st_blksize;
	out->st_blocks = in.st_blocks;
	out->st_atim.tv_sec = in.atime_sec;
	out->st_mtim.tv_sec = in.mtime_sec;
	out->st_ctim.tv_sec = in.ctime_sec;
}

static int compute_argc(char *const argv[]) {
	if(!argv)
		return 0;
	int argc = 0;
	while(argv[argc])
		argc++;
	return argc;
}

static size_t bounded_length(const char *s, size_t limit) {
	size_t n = 0;
	while(n < limit && s[n])
		n++;
	return n;
}

}

namespace mlibc {

void sys_libc_log(const char *message) {
	zenos_do_syscall3(ZENOS_SYSCALL_LOG, reinterpret_cast<long>(message), 1, 0);
}

[[noreturn]] void sys_libc_panic() {
	sys_libc_log("mlibc panic");
	sys_exit(1);
}

[[noreturn]] void sys_exit(int status) {
	zenos_do_syscall1(ZENOS_SYSCALL_EXIT, status);
	__builtin_unreachable();
}

int sys_tcb_set(void *pointer) {
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_ARCH_PRCTL,
			ARCH_SET_FS, reinterpret_cast<long>(pointer));
	return sc_error(ret);
}

int sys_futex_wait(int *pointer, int expected, const struct timespec *time) {
	(void)time;
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_FUTEX,
			reinterpret_cast<long>(pointer), ZENOS_FUTEX_WAIT, expected);
	return sc_error(ret);
}

int sys_futex_wake(int *pointer) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_FUTEX,
			reinterpret_cast<long>(pointer), ZENOS_FUTEX_WAKE, 1);
	return sc_error(ret);
}

int sys_vm_map(void *hint, size_t size, int prot, int flags, int fd, off_t offset, void **window) {
	if(fd != -1 || offset)
		return ENOSYS;
	long ret = zenos_do_syscall4(ZENOS_SYSCALL_MMAP, reinterpret_cast<long>(hint),
			size, prot, flags);
	int e = sc_error(ret);
	if(e)
		return e;
	*window = reinterpret_cast<void *>(ret);
	return 0;
}

int sys_vm_unmap(void *pointer, size_t size) {
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_MUNMAP, reinterpret_cast<long>(pointer), size);
	return sc_error(ret);
}

int sys_anon_allocate(size_t size, void **pointer) {
	return sys_vm_map(nullptr, size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0, pointer);
}

int sys_anon_free(void *pointer, size_t size) {
	return sys_vm_unmap(pointer, size);
}

int sys_vm_protect(void *pointer, size_t size, int prot) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_MPROTECT,
			reinterpret_cast<long>(pointer), size, prot);
	return sc_error(ret);
}

int sys_openat(int dirfd, const char *path, int flags, mode_t mode, int *fd) {
	(void)mode;
	if(dirfd != AT_FDCWD)
		return ENOSYS;
	return finish_fd(zenos_do_syscall2(ZENOS_SYSCALL_OPEN,
			reinterpret_cast<long>(path), flags), fd);
}

int sys_open(const char *path, int flags, mode_t mode, int *fd) {
	return sys_openat(AT_FDCWD, path, flags, mode, fd);
}

int sys_open_dir(const char *path, int *handle) {
	return finish_fd(zenos_do_syscall1(ZENOS_SYSCALL_OPENDIR,
			reinterpret_cast<long>(path)), handle);
}

int sys_read_entries(int handle, void *buffer, size_t max_size, size_t *bytes_read) {
	size_t written = 0;

	while(written + offsetof(struct dirent, d_name) + 2 <= max_size) {
		zenos_dirent dent{};
		long ret = zenos_do_syscall2(ZENOS_SYSCALL_READDIR, handle, reinterpret_cast<long>(&dent));
		int e = sc_error(ret);
		if(e)
			return e;
		if(ret <= 0)
			break;

		auto ent = reinterpret_cast<struct dirent *>(reinterpret_cast<char *>(buffer) + written);
		size_t name_len = bounded_length(dent.d_name, sizeof(dent.d_name));
		size_t reclen = offsetof(struct dirent, d_name) + name_len + 1;
		reclen = (reclen + alignof(struct dirent) - 1) & ~(alignof(struct dirent) - 1);
		if(written + reclen > max_size)
			break;

		memset(ent, 0, reclen);
		ent->d_ino = dent.d_ino;
		ent->d_off = 0;
		ent->d_reclen = reclen;
		ent->d_type = dent.d_type;
		memcpy(ent->d_name, dent.d_name, name_len);
		ent->d_name[name_len] = '\0';
		written += reclen;
	}

	*bytes_read = written;
	return 0;
}

int sys_read(int fd, void *buf, size_t count, ssize_t *bytes_read) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_READ, fd, reinterpret_cast<long>(buf), count);
	int e = sc_error(ret);
	if(e)
		return e;
	*bytes_read = ret;
	return 0;
}

int sys_write(int fd, const void *buf, size_t count, ssize_t *bytes_written) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_WRITE, fd, reinterpret_cast<long>(buf), count);
	int e = sc_error(ret);
	if(e)
		return e;
	if(bytes_written)
		*bytes_written = ret;
	return 0;
}

int sys_seek(int fd, off_t offset, int whence, off_t *new_offset) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_LSEEK, fd, offset, whence);
	int e = sc_error(ret);
	if(e)
		return e;
	*new_offset = ret;
	return 0;
}

int sys_close(int fd) {
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_CLOSE, fd);
	return sc_error(ret);
}

int sys_clock_get(int clock, time_t *secs, long *nanos) {
	struct timespec ts{};
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_CLOCK_GETTIME, clock, reinterpret_cast<long>(&ts));
	int e = sc_error(ret);
	if(e)
		return e;
	*secs = ts.tv_sec;
	*nanos = ts.tv_nsec;
	return 0;
}

int sys_clock_getres(int clock, time_t *secs, long *nanos) {
	(void)clock;
	*secs = 0;
	*nanos = 1;
	return 0;
}

int sys_sleep(time_t *secs, long *nanos) {
	struct timespec req{};
	req.tv_sec = *secs;
	req.tv_nsec = *nanos;
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_NANOSLEEP, reinterpret_cast<long>(&req));
	int e = sc_error(ret);
	if(e)
		return e;
	*secs = 0;
	*nanos = 0;
	return 0;
}

int sys_isatty(int fd) {
	struct winsize ws{};
	int ignored = 0;
	if(!sys_ioctl(fd, TIOCGWINSZ, &ws, &ignored))
		return 0;
	return ENOTTY;
}

int sys_getcwd(char *buffer, size_t size) {
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_GETCWD, reinterpret_cast<long>(buffer), size);
	return sc_error(ret);
}

int sys_chdir(const char *path) {
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_CHDIR, reinterpret_cast<long>(path));
	return sc_error(ret);
}

int sys_mkdir(const char *path, mode_t mode) {
	(void)mode;
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_MKDIR, reinterpret_cast<long>(path));
	return sc_error(ret);
}

int sys_rmdir(const char *path) {
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_RMDIR, reinterpret_cast<long>(path));
	return sc_error(ret);
}

int sys_unlinkat(int dirfd, const char *path, int flags) {
	if(dirfd != AT_FDCWD)
		return ENOSYS;
	long ret = flags & AT_REMOVEDIR
		? zenos_do_syscall1(ZENOS_SYSCALL_RMDIR, reinterpret_cast<long>(path))
		: zenos_do_syscall1(ZENOS_SYSCALL_DELETE, reinterpret_cast<long>(path));
	return sc_error(ret);
}

int sys_access(const char *path, int mode) {
	(void)mode;
	struct stat st{};
	return sys_stat(fsfd_target::path, -1, path, 0, &st);
}

int sys_stat(fsfd_target fsfdt, int fd, const char *path, int flags, struct stat *statbuf) {
	(void)flags;
	zenos_stat zst{};
	long ret = -1;

	switch(fsfdt) {
		case fsfd_target::path:
			ret = zenos_do_syscall2(ZENOS_SYSCALL_STAT, reinterpret_cast<long>(path), reinterpret_cast<long>(&zst));
			break;
		case fsfd_target::fd:
			ret = zenos_do_syscall2(ZENOS_SYSCALL_FSTAT, fd, reinterpret_cast<long>(&zst));
			break;
		default:
			return ENOSYS;
	}

	int e = sc_error(ret);
	if(e)
		return e;
	translate_stat(zst, statbuf);
	return 0;
}

int sys_dup(int fd, int flags, int *newfd) {
	(void)flags;
	return finish_fd(zenos_do_syscall1(ZENOS_SYSCALL_DUP, fd), newfd);
}

int sys_dup2(int fd, int flags, int newfd) {
	(void)flags;
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_DUP2, fd, newfd);
	return sc_error(ret);
}

int sys_fcntl(int fd, int request, va_list args, int *result) {
	switch(request) {
		case F_GETFD:
			*result = 0;
			return 0;
		case F_SETFD:
			(void)va_arg(args, int);
			*result = 0;
			return 0;
		case F_GETFL:
			*result = 0;
			return 0;
		case F_SETFL:
			(void)va_arg(args, int);
			*result = 0;
			return 0;
		case F_DUPFD:
		case F_DUPFD_CLOEXEC: {
			int minfd = va_arg(args, int);
			if(minfd > 0)
				return ENOSYS;
			int newfd;
			int e = sys_dup(fd, 0, &newfd);
			if(e)
				return e;
			*result = newfd;
			return 0;
		}
		default:
			return ENOSYS;
	}
}

int sys_ioctl(int fd, unsigned long request, void *arg, int *result) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_IOCTL, fd, request, reinterpret_cast<long>(arg));
	int e = sc_error(ret);
	if(e)
		return e;
	*result = ret;
	return 0;
}

int sys_tcgetattr(int fd, struct termios *attr) {
	int result = 0;
	int e = sys_ioctl(fd, TCGETS, attr, &result);
	if(e)
		return e;
	return 0;
}

int sys_tcsetattr(int fd, int optional_action, const struct termios *attr) {
	unsigned long req = TCSETS;
	switch(optional_action) {
		case TCSANOW:
			req = TCSETS;
			break;
		case TCSADRAIN:
			req = TCSETSW;
			break;
		case TCSAFLUSH:
			req = TCSETSF;
			break;
		default:
			return EINVAL;
	}
	int result = 0;
	return sys_ioctl(fd, req, const_cast<struct termios *>(attr), &result);
}

int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
	zenos_sigaction kact{};
	zenos_sigaction kold{};
	if(act) {
		kact.handler = act->sa_flags & SA_SIGINFO
			? reinterpret_cast<uint64_t>(act->sa_sigaction)
			: reinterpret_cast<uint64_t>(act->sa_handler);
		kact.flags = act->sa_flags;
		kact.mask = act->sa_mask;
	}

	long ret = zenos_do_syscall3(ZENOS_SYSCALL_SIGACTION, sig,
			act ? reinterpret_cast<long>(&kact) : 0,
			oldact ? reinterpret_cast<long>(&kold) : 0);
	int e = sc_error(ret);
	if(e)
		return e;

	if(oldact) {
		memset(oldact, 0, sizeof(*oldact));
		oldact->sa_flags = kold.flags;
		oldact->sa_mask = kold.mask;
		if(kold.flags & SA_SIGINFO)
			oldact->sa_sigaction = reinterpret_cast<void (*)(int, siginfo_t *, void *)>(kold.handler);
		else
			oldact->sa_handler = reinterpret_cast<void (*)(int)>(kold.handler);
	}
	return 0;
}

int sys_sigprocmask(int how, const sigset_t *set, sigset_t *retrieve) {
	uint64_t new_mask = set ? static_cast<uint64_t>(*set) : 0;
	uint64_t old_mask = 0;
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_SIGPROCMASK, how,
			set ? reinterpret_cast<long>(&new_mask) : 0,
			retrieve ? reinterpret_cast<long>(&old_mask) : 0);
	int e = sc_error(ret);
	if(e)
		return e;
	if(retrieve)
		*retrieve = static_cast<sigset_t>(old_mask);
	return 0;
}

void sys_yield() {
	zenos_do_syscall0(ZENOS_SYSCALL_YIELD);
}

int sys_pipe(int *fds, int flags) {
	if(flags)
		return ENOSYS;
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_PIPE, reinterpret_cast<long>(fds));
	return sc_error(ret);
}

int sys_fork(pid_t *child) {
	long ret = zenos_do_syscall0(ZENOS_SYSCALL_FORK);
	int e = sc_error(ret);
	if(e)
		return e;
	*child = ret;
	return 0;
}

int sys_execve(const char *path, char *const argv[], char *const envp[]) {
	long ret = zenos_do_syscall4(ZENOS_SYSCALL_EXEC, reinterpret_cast<long>(path),
			compute_argc(argv), reinterpret_cast<long>(argv), reinterpret_cast<long>(envp));
	return sc_error(ret);
}

int sys_waitpid(pid_t pid, int *status, int flags, struct rusage *ru, pid_t *ret_pid) {
	(void)ru;
	if(status)
		*status = 0;
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_WAIT_PID, pid, reinterpret_cast<long>(status), flags);
	return translate_status(ret, status, ret_pid);
}

pid_t sys_getpid() {
	return zenos_do_syscall0(ZENOS_SYSCALL_GETPID);
}

int sys_kill(int pid, int signal) {
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_KILL, pid, signal);
	return sc_error(ret);
}

int sys_uname(struct utsname *buf) {
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_UNAME, reinterpret_cast<long>(buf));
	return sc_error(ret);
}

int sys_clone(void *tcb, pid_t *pid_out, void *stack) {
	(void)tcb;
	(void)pid_out;
	(void)stack;
	return ENOSYS;
}

[[noreturn]] void sys_thread_exit() {
	zenos_do_syscall1(ZENOS_SYSCALL_EXIT, 0);
	__builtin_unreachable();
}

} // namespace mlibc
