cmd_findutils/grep.o := /home/nqetm/Documents/ZenOS/userland/files/busybox/zenos-clang -Wp,-MD,findutils/.grep.o.d  -std=gnu99 -Iinclude -Ilibbb  -include include/autoconf.h -D_GNU_SOURCE -DNDEBUG -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64 -DBB_VER='"1.37.0"' -Wall -Wshadow -Wwrite-strings -Wundef -Wstrict-prototypes -Wunused -Wunused-parameter -Wunused-function -Wunused-value -Wmissing-prototypes -Wmissing-declarations -Wno-format-security -Wdeclaration-after-statement -Wold-style-definition -fno-builtin-strlen -fomit-frame-pointer -ffunction-sections -fdata-sections -funsigned-char -falign-functions=1 -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-builtin-printf -Wno-string-plus-int -Wno-constant-logical-operand -Oz    -DKBUILD_BASENAME='"grep"'  -DKBUILD_MODNAME='"grep"' -c -o findutils/grep.o findutils/grep.c

deps_findutils/grep.o := \
  findutils/grep.c \
    $(wildcard include/config/grep.h) \
    $(wildcard include/config/egrep.h) \
    $(wildcard include/config/fgrep.h) \
    $(wildcard include/config/extra/compat.h) \
    $(wildcard include/config/feature/grep/context.h) \
    $(wildcard include/config/feature/clean/up.h) \
  include/libbb.h \
    $(wildcard include/config/feature/shadowpasswds.h) \
    $(wildcard include/config/use/bb/shadow.h) \
    $(wildcard include/config/selinux.h) \
    $(wildcard include/config/feature/utmp.h) \
    $(wildcard include/config/locale/support.h) \
    $(wildcard include/config/use/bb/pwd/grp.h) \
    $(wildcard include/config/lfs.h) \
    $(wildcard include/config/feature/buffers/go/on/stack.h) \
    $(wildcard include/config/feature/buffers/go/in/bss.h) \
    $(wildcard include/config/extra/cflags.h) \
    $(wildcard include/config/variable/arch/pagesize.h) \
    $(wildcard include/config/feature/verbose.h) \
    $(wildcard include/config/feature/etc/services.h) \
    $(wildcard include/config/feature/ipv6.h) \
    $(wildcard include/config/feature/seamless/xz.h) \
    $(wildcard include/config/feature/seamless/lzma.h) \
    $(wildcard include/config/feature/seamless/bz2.h) \
    $(wildcard include/config/feature/seamless/gz.h) \
    $(wildcard include/config/feature/seamless/z.h) \
    $(wildcard include/config/float/duration.h) \
    $(wildcard include/config/feature/check/names.h) \
    $(wildcard include/config/feature/prefer/applets.h) \
    $(wildcard include/config/long/opts.h) \
    $(wildcard include/config/feature/pidfile.h) \
    $(wildcard include/config/feature/syslog.h) \
    $(wildcard include/config/feature/syslog/info.h) \
    $(wildcard include/config/warn/simple/msg.h) \
    $(wildcard include/config/feature/individual.h) \
    $(wildcard include/config/shell/ash.h) \
    $(wildcard include/config/shell/hush.h) \
    $(wildcard include/config/echo.h) \
    $(wildcard include/config/sleep.h) \
    $(wildcard include/config/ash/sleep.h) \
    $(wildcard include/config/printf.h) \
    $(wildcard include/config/test.h) \
    $(wildcard include/config/test1.h) \
    $(wildcard include/config/test2.h) \
    $(wildcard include/config/kill.h) \
    $(wildcard include/config/killall.h) \
    $(wildcard include/config/killall5.h) \
    $(wildcard include/config/chown.h) \
    $(wildcard include/config/ls.h) \
    $(wildcard include/config/xxx.h) \
    $(wildcard include/config/route.h) \
    $(wildcard include/config/feature/hwib.h) \
    $(wildcard include/config/desktop.h) \
    $(wildcard include/config/feature/crond/d.h) \
    $(wildcard include/config/feature/setpriv/capabilities.h) \
    $(wildcard include/config/run/init.h) \
    $(wildcard include/config/feature/securetty.h) \
    $(wildcard include/config/pam.h) \
    $(wildcard include/config/use/bb/crypt.h) \
    $(wildcard include/config/feature/adduser/to/group.h) \
    $(wildcard include/config/feature/del/user/from/group.h) \
    $(wildcard include/config/ioctl/hex2str/error.h) \
    $(wildcard include/config/feature/editing.h) \
    $(wildcard include/config/feature/editing/history.h) \
    $(wildcard include/config/feature/tab/completion.h) \
    $(wildcard include/config/feature/username/completion.h) \
    $(wildcard include/config/feature/editing/fancy/prompt.h) \
    $(wildcard include/config/feature/editing/savehistory.h) \
    $(wildcard include/config/feature/editing/vi.h) \
    $(wildcard include/config/feature/editing/save/on/exit.h) \
    $(wildcard include/config/pmap.h) \
    $(wildcard include/config/feature/show/threads.h) \
    $(wildcard include/config/feature/ps/additional/columns.h) \
    $(wildcard include/config/feature/topmem.h) \
    $(wildcard include/config/feature/top/smp/process.h) \
    $(wildcard include/config/pgrep.h) \
    $(wildcard include/config/pkill.h) \
    $(wildcard include/config/pidof.h) \
    $(wildcard include/config/sestatus.h) \
    $(wildcard include/config/unicode/support.h) \
    $(wildcard include/config/feature/mtab/support.h) \
    $(wildcard include/config/feature/devfs.h) \
  include/platform.h \
    $(wildcard include/config/werror.h) \
    $(wildcard include/config/big/endian.h) \
    $(wildcard include/config/little/endian.h) \
    $(wildcard include/config/nommu.h) \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/limits.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/limits.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/byteswap.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/endian.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/stdint.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/types.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/wchar.h \
  /usr/bin/../lib/clang/22/include/stdbool.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/unistd.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/mlibc-config.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/size_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/ssize_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/off_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/access.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/uid_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/gid_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/pid_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/seek-whence.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/ctype.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/posix_ctype.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/locale_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/dirent.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/ino_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/errno.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/errno.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/fcntl.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/fcntl.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/mode_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/iovec.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/inttypes.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/wchar_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/netdb.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/in_port_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/in_addr_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/socklen_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/setjmp.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/machine.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/signal.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/sigevent.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/sigval.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/signal.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/posix_signal.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/ansi/time_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/ansi/timespec.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/pthread_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/threads.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/clockid_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/cpu_set.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/sigset_t.h \
  /usr/bin/../lib/clang/22/include/stddef.h \
  /usr/bin/../lib/clang/22/include/__stddef_header_macro.h \
  /usr/bin/../lib/clang/22/include/__stddef_ptrdiff_t.h \
  /usr/bin/../lib/clang/22/include/__stddef_size_t.h \
  /usr/bin/../lib/clang/22/include/__stddef_wchar_t.h \
  /usr/bin/../lib/clang/22/include/__stddef_null.h \
  /usr/bin/../lib/clang/22/include/__stddef_offsetof.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/paths.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/stdio.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/null.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/file.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/posix_stdio.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/stdlib.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/alloca.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/wait.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/posix_stdlib.h \
  /usr/bin/../lib/clang/22/include/stdarg.h \
  /usr/bin/../lib/clang/22/include/__stdarg_header_macro.h \
  /usr/bin/../lib/clang/22/include/__stdarg___gnuc_va_list.h \
  /usr/bin/../lib/clang/22/include/__stdarg_va_list.h \
  /usr/bin/../lib/clang/22/include/__stdarg_va_arg.h \
  /usr/bin/../lib/clang/22/include/__stdarg___va_copy.h \
  /usr/bin/../lib/clang/22/include/__stdarg_va_copy.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/string.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/strings.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/posix_string.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/libgen.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/poll.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/poll.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/poll.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/ioctl.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/ioctls.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/asm/ioctls.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/asm/ioctl.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/winsize.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/mman.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/vm-flags.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/resource.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/resource.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/timeval.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/suseconds_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/rlim_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/id_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/socket.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/socket.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/stat.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/stat.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/stat.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/dev_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/blksize_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/blkcnt_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/nlink_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/time.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/time.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/timer_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/select.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/fd_set.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/types.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/fsblkcnt_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/fsfilcnt_t.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/sysmacros.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/wait.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/termios.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/termios.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/ttydefaults.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/time.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/posix/posix_time.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/param.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/pwd.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/grp.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/mntent.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/statfs.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/arpa/inet.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/netinet/in.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/in.h \
  include/xatonum.h \
  include/common_bufsiz.h \
  include/xregex.h \
  /home/nqetm/Documents/ZenOS/userland/files/busybox/../../sysroot/usr/include/regex.h \

findutils/grep.o: $(deps_findutils/grep.o)

$(deps_findutils/grep.o):
