#include <asm/ioctls.h>
#include <bits/ensure.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>
#include <netinet/in.h>

#include <frg/manual_box.hpp>
#include <mlibc/all-sysdeps.hpp>
#include <mlibc/debug.hpp>
#include <mlibc/fsfd_target.hpp>

#include "syscall.h"

namespace {

static bool inet_sockets[64];

static bool is_inet_socket(int fd) {
	return fd >= 0 && static_cast<size_t>(fd) < (sizeof(inet_sockets) / sizeof(inet_sockets[0])) && inet_sockets[fd];
}

static void set_inet_socket(int fd, bool value) {
	if(fd >= 0 && static_cast<size_t>(fd) < (sizeof(inet_sockets) / sizeof(inet_sockets[0])))
		inet_sockets[fd] = value;
}

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

static void translate_statvfs(const zenos_statfs &in, struct statvfs *out) {
	memset(out, 0, sizeof(*out));
	out->f_bsize = in.f_bsize;
	out->f_frsize = in.f_frsize ? in.f_frsize : in.f_bsize;
	out->f_blocks = in.f_blocks;
	out->f_bfree = in.f_bfree;
	out->f_bavail = in.f_bavail;
	out->f_files = in.f_files;
	out->f_ffree = in.f_ffree;
	out->f_favail = in.f_ffree;
	out->f_fsid = in.f_fsid;
	out->f_flag = in.f_flags;
	out->f_namemax = in.f_namelen;
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
	if(is_inet_socket(fd)) {
		long ret = zenos_do_syscall4(ZENOS_SYSCALL_RECV, fd, reinterpret_cast<long>(buf), count, 0);
		int e = sc_error(ret);
		if(e)
			return e;
		*bytes_read = ret;
		return 0;
	}
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_READ, fd, reinterpret_cast<long>(buf), count);
	int e = sc_error(ret);
	if(e)
		return e;
	*bytes_read = ret;
	return 0;
}

int sys_write(int fd, const void *buf, size_t count, ssize_t *bytes_written) {
	if(is_inet_socket(fd)) {
		long ret = zenos_do_syscall4(ZENOS_SYSCALL_SEND, fd, reinterpret_cast<long>(buf), count, 0);
		int e = sc_error(ret);
		if(e)
			return e;
		if(bytes_written)
			*bytes_written = ret;
		return 0;
	}
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
	if(is_inet_socket(fd)) {
		long ret = zenos_do_syscall1(ZENOS_SYSCALL_CLOSESOCKET, fd);
		set_inet_socket(fd, false);
		return sc_error(ret);
	}
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

int sys_ttyname(int fd, char *buf, size_t size) {
	static constexpr const char path[] = "/dev/tty";
	if(size < sizeof(path))
		return ERANGE;
	if(int e = sys_isatty(fd); e)
		return e;
	memcpy(buf, path, sizeof(path));
	return 0;
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

int sys_rename(const char *path, const char *new_path) {
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_RENAME,
			reinterpret_cast<long>(path), reinterpret_cast<long>(new_path));
	return sc_error(ret);
}

int sys_renameat(int olddirfd, const char *old_path, int newdirfd, const char *new_path) {
	if(olddirfd != AT_FDCWD || newdirfd != AT_FDCWD)
		return ENOSYS;
	return sys_rename(old_path, new_path);
}

int sys_access(const char *path, int mode) {
	(void)mode;
	struct stat st{};
	return sys_stat(fsfd_target::path, -1, path, 0, &st);
}

int sys_faccessat(int dirfd, const char *pathname, int mode, int flags) {
	(void)flags;
	if(dirfd != AT_FDCWD)
		return ENOSYS;
	return sys_access(pathname, mode);
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

int sys_statvfs(const char *path, struct statvfs *out) {
	zenos_statfs zst{};
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_STATFS,
			reinterpret_cast<long>(path), reinterpret_cast<long>(&zst));
	int e = sc_error(ret);
	if(e)
		return e;
	translate_statvfs(zst, out);
	return 0;
}

int sys_fstatvfs(int fd, struct statvfs *out) {
	zenos_statfs zst{};
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_FSTATFS,
			fd, reinterpret_cast<long>(&zst));
	int e = sc_error(ret);
	if(e)
		return e;
	translate_statvfs(zst, out);
	return 0;
}

int sys_dup(int fd, int flags, int *newfd) {
	(void)flags;
	return finish_fd(zenos_do_syscall2(ZENOS_SYSCALL_DUP, fd, 0), newfd);
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
			return finish_fd(zenos_do_syscall2(ZENOS_SYSCALL_DUP, fd, minfd), result);
		}
		default:
			return ENOSYS;
	}
}

int sys_poll(struct pollfd *fds, nfds_t count, int timeout, int *num_events) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_POLL,
			reinterpret_cast<long>(fds), count, timeout);
	if(int e = sc_error(ret))
		return e;
	*num_events = static_cast<int>(ret);
	return 0;
}

int sys_readv(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_read) {
	ssize_t total = 0;
	for(int i = 0; i < iovc; i++) {
		ssize_t chunk = 0;
		int e = sys_read(fd, iovs[i].iov_base, iovs[i].iov_len, &chunk);
		if(e)
			return e;
		total += chunk;
		if((size_t)chunk < iovs[i].iov_len)
			break;
	}
	*bytes_read = total;
	return 0;
}

int sys_writev(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_written) {
	ssize_t total = 0;
	for(int i = 0; i < iovc; i++) {
		ssize_t chunk = 0;
		int e = sys_write(fd, iovs[i].iov_base, iovs[i].iov_len, &chunk);
		if(e)
			return e;
		total += chunk;
		if((size_t)chunk < iovs[i].iov_len)
			break;
	}
	*bytes_written = total;
	return 0;
}

int sys_pread(int fd, void *buf, size_t n, off_t off, ssize_t *bytes_read) {
	off_t old_off = 0;
	int e = sys_seek(fd, 0, SEEK_CUR, &old_off);
	if(e)
		return e;
	off_t target = 0;
	e = sys_seek(fd, off, SEEK_SET, &target);
	if(e)
		return e;
	e = sys_read(fd, buf, n, bytes_read);
	off_t ignored = 0;
	sys_seek(fd, old_off, SEEK_SET, &ignored);
	return e;
}

int sys_pwrite(int fd, const void *buf, size_t n, off_t off, ssize_t *bytes_written) {
	off_t old_off = 0;
	int e = sys_seek(fd, 0, SEEK_CUR, &old_off);
	if(e)
		return e;
	off_t target = 0;
	e = sys_seek(fd, off, SEEK_SET, &target);
	if(e)
		return e;
	e = sys_write(fd, buf, n, bytes_written);
	off_t ignored = 0;
	sys_seek(fd, old_off, SEEK_SET, &ignored);
	return e;
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

int sys_tcdrain(int fd) {
	return sys_isatty(fd);
}

int sys_tcflow(int fd, int action) {
	(void)action;
	return sys_isatty(fd);
}

int sys_tcflush(int fd, int queue) {
	(void)queue;
	return sys_isatty(fd);
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

pid_t sys_getppid() {
	return zenos_do_syscall0(ZENOS_SYSCALL_GETPPID);
}

int sys_getpgid(pid_t pid, pid_t *pgid) {
	*pgid = pid ? pid : sys_getpid();
	return 0;
}

int sys_getsid(pid_t pid, pid_t *sid) {
	*sid = pid ? pid : sys_getpid();
	return 0;
}

int sys_setpgid(pid_t pid, pid_t pgid) {
	(void)pid;
	(void)pgid;
	return 0;
}

uid_t sys_getuid() {
	return zenos_do_syscall0(ZENOS_SYSCALL_GETUID);
}

gid_t sys_getgid() {
	return zenos_do_syscall0(ZENOS_SYSCALL_GETGID);
}

uid_t sys_geteuid() {
	return zenos_do_syscall0(ZENOS_SYSCALL_GETEUID);
}

gid_t sys_getegid() {
	return zenos_do_syscall0(ZENOS_SYSCALL_GETEGID);
}

int sys_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask) {
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_SCHED_GETAFFINITY,
			pid, cpusetsize, reinterpret_cast<long>(mask));
	return sc_error(ret);
}

int sys_sysinfo(struct sysinfo *info) {
	zenos_sysinfo zi{};
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_SYSINFO, reinterpret_cast<long>(&zi));
	int e = sc_error(ret);
	if(e)
		return e;

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

int sys_kill(int pid, int signal) {
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_KILL, pid, signal);
	return sc_error(ret);
}

int sys_uname(struct utsname *buf) {
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_UNAME, reinterpret_cast<long>(buf));
	return sc_error(ret);
}

int sys_gethostname(char *buffer, size_t bufsize) {
	struct utsname uts{};
	int e = sys_uname(&uts);
	if(e)
		return e;
	size_t len = bounded_length(uts.nodename, sizeof(uts.nodename));
	if(len + 1 > bufsize)
		return ENAMETOOLONG;
	memcpy(buffer, uts.nodename, len);
	buffer[len] = '\0';
	return 0;
}

int sys_mkdirat(int dirfd, const char *path, mode_t mode) {
	if(dirfd != AT_FDCWD)
		return ENOSYS;
	return sys_mkdir(path, mode);
}

int sys_ftruncate(int fd, size_t size) {
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_FTRUNCATE, fd, size);
	return sc_error(ret);
}

int sys_fsync(int fd) {
	long ret = zenos_do_syscall1(ZENOS_SYSCALL_FSYNC, fd);
	return sc_error(ret);
}

int sys_fdatasync(int fd) {
	return sys_fsync(fd);
}

int sys_chmod(const char *pathname, mode_t mode) {
	(void)mode;
	struct stat st{};
	return sys_stat(fsfd_target::path, -1, pathname, 0, &st);
}

int sys_fchmod(int fd, mode_t mode) {
	(void)mode;
	struct stat st{};
	return sys_stat(fsfd_target::fd, fd, nullptr, 0, &st);
}

int sys_fchmodat(int fd, const char *pathname, mode_t mode, int flags) {
	(void)flags;
	if(fd != AT_FDCWD)
		return ENOSYS;
	return sys_chmod(pathname, mode);
}

int sys_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags) {
	(void)owner;
	(void)group;
	(void)flags;
	if(dirfd != AT_FDCWD)
		return ENOSYS;
	struct stat st{};
	return sys_stat(fsfd_target::path, -1, pathname, 0, &st);
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

int sys_socket(int domain, int type, int protocol, int *fd) {
	int base_type = type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK);
	if(domain == AF_INET) {
		if(base_type != SOCK_STREAM)
			return EPROTONOSUPPORT;
		if(protocol != 0 && protocol != IPPROTO_TCP)
			return EPROTONOSUPPORT;
		long ret = zenos_do_syscall3(ZENOS_SYSCALL_SOCKET, domain, base_type, protocol);
		int e = finish_fd(ret, fd);
		if(!e)
			set_inet_socket(*fd, true);
		return e;
	}
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_UNIX_SOCKET, domain, type);
	return finish_fd(ret, fd);
}

int sys_bind(int fd, const struct sockaddr *addr, socklen_t addrlen) {
	(void)addrlen;
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_UNIX_BIND,
			fd, reinterpret_cast<long>(addr));
	return sc_error(ret);
}

int sys_listen(int fd, int backlog) {
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_UNIX_LISTEN, fd, backlog);
	return sc_error(ret);
}

int sys_accept(int fd, int *newfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
	long ret = zenos_do_syscall4(ZENOS_SYSCALL_UNIX_ACCEPT, fd,
			reinterpret_cast<long>(addr), reinterpret_cast<long>(addrlen), flags);
	return finish_fd(ret, newfd);
}

int sys_connect(int fd, const struct sockaddr *addr, socklen_t addrlen) {
	if((addr && addr->sa_family == AF_INET) || is_inet_socket(fd)) {
		if(!addr || addrlen < sizeof(struct sockaddr_in))
			return EINVAL;
		long ret = zenos_do_syscall3(ZENOS_SYSCALL_CONNECT,
				fd, reinterpret_cast<long>(addr), addrlen);
		int e = sc_error(ret);
		if(!e)
			set_inet_socket(fd, true);
		return e;
	}
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_UNIX_CONNECT,
			fd, reinterpret_cast<long>(addr));
	return sc_error(ret);
}

int sys_msg_send(int fd, const struct msghdr *msg, int flags, ssize_t *bytes_written) {
	if (!msg || !msg->msg_iov || msg->msg_iovlen == 0)
		return EINVAL;
	ssize_t total = 0;
	if(is_inet_socket(fd)) {
		for (size_t i = 0; i < msg->msg_iovlen; i++) {
			long ret = zenos_do_syscall4(ZENOS_SYSCALL_SEND,
					fd,
					reinterpret_cast<long>(msg->msg_iov[i].iov_base),
					msg->msg_iov[i].iov_len,
					flags);
			int e = sc_error(ret);
			if(e) return e;
			total += ret;
			if((size_t)ret < msg->msg_iov[i].iov_len)
				break;
		}
		*bytes_written = total;
		return 0;
	}
	(void)flags;
	for (size_t i = 0; i < msg->msg_iovlen; i++) {
		long ret = zenos_do_syscall4(ZENOS_SYSCALL_UNIX_SEND,
				fd,
				reinterpret_cast<long>(msg->msg_iov[i].iov_base),
				msg->msg_iov[i].iov_len,
				reinterpret_cast<long>(i == 0 ? msg->msg_name : nullptr));
		int e = sc_error(ret);
		if (e) return e;
		total += ret;
	}
	*bytes_written = total;
	return 0;
}

int sys_msg_recv(int fd, struct msghdr *msg, int flags, ssize_t *bytes_read) {
	if (!msg || !msg->msg_iov || msg->msg_iovlen == 0)
		return EINVAL;
	ssize_t total = 0;
	if(is_inet_socket(fd)) {
		for (size_t i = 0; i < msg->msg_iovlen; i++) {
			long ret = zenos_do_syscall4(ZENOS_SYSCALL_RECV,
					fd,
					reinterpret_cast<long>(msg->msg_iov[i].iov_base),
					msg->msg_iov[i].iov_len,
					flags);
			int e = sc_error(ret);
			if(e) return e;
			total += ret;
			if((size_t)ret < msg->msg_iov[i].iov_len)
				break;
		}
		*bytes_read = total;
		return 0;
	}
	(void)flags;
	for (size_t i = 0; i < msg->msg_iovlen; i++) {
		long ret = zenos_do_syscall5(ZENOS_SYSCALL_UNIX_RECV,
				fd,
				reinterpret_cast<long>(msg->msg_iov[i].iov_base),
				msg->msg_iov[i].iov_len,
				reinterpret_cast<long>(i == 0 ? msg->msg_name : nullptr),
				reinterpret_cast<long>(i == 0 ? &msg->msg_namelen : nullptr));
		int e = sc_error(ret);
		if (e) return e;
		total += ret;
		if ((size_t)ret < msg->msg_iov[i].iov_len)
			break;
	}
	*bytes_read = total;
	return 0;
}

int sys_sockname(int fd, struct sockaddr *addr, socklen_t max_addr_length,
		socklen_t *actual_length) {
	long ret = zenos_do_syscall4(ZENOS_SYSCALL_GETSOCKNAME,
			fd, reinterpret_cast<long>(addr), max_addr_length,
			reinterpret_cast<long>(actual_length));
	int e = sc_error(ret);
	return e;
}

int sys_getsockopt(int fd, int layer, int number, void *buffer, socklen_t *size) {
	(void)fd; (void)layer; (void)number; (void)buffer; (void)size;
	return ENOSYS;
}

int sys_setsockopt(int fd, int layer, int number, const void *buffer, socklen_t size) {
	(void)fd; (void)layer; (void)number; (void)buffer; (void)size;
	return 0;
}

int sys_shutdown(int fd, int how) {
	if(is_inet_socket(fd)) {
		long ret = zenos_do_syscall2(ZENOS_SYSCALL_CLOSESOCKET, fd, how);
		return sc_error(ret);
	}
	long ret = zenos_do_syscall2(ZENOS_SYSCALL_UNIX_SHUTDOWN, fd, how);
	return sc_error(ret);
}

int sys_peername(int fd, struct sockaddr *addr, socklen_t max_addr_length,
		socklen_t *actual_length) {
	long ret = zenos_do_syscall4(ZENOS_SYSCALL_GETPEERNAME,
			fd, reinterpret_cast<long>(addr), max_addr_length,
			reinterpret_cast<long>(actual_length));
	return sc_error(ret);
}

int sys_socketpair(int domain, int type_and_flags, int proto, int *fds) {
	(void)proto;
	long ret = zenos_do_syscall3(ZENOS_SYSCALL_UNIX_SOCKETPAIR, domain,
			type_and_flags, reinterpret_cast<long>(fds));
	return sc_error(ret);
}

} // namespace mlibc
