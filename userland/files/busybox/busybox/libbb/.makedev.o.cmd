cmd_libbb/makedev.o := /home/rishies2010/OSDev/ZenOS/userland/files/busybox/zenos-clang -Wp,-MD,libbb/.makedev.o.d  -std=gnu99 -Iinclude -Ilibbb  -include include/autoconf.h -D_GNU_SOURCE -DNDEBUG -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64 -DBB_VER='"1.37.0"' -Wall -Wshadow -Wwrite-strings -Wundef -Wstrict-prototypes -Wunused -Wunused-parameter -Wunused-function -Wunused-value -Wmissing-prototypes -Wmissing-declarations -Wno-format-security -Wdeclaration-after-statement -Wold-style-definition -fno-builtin-strlen -fomit-frame-pointer -ffunction-sections -fdata-sections -funsigned-char -falign-functions=1 -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-builtin-printf -Wno-string-plus-int -Wno-constant-logical-operand -Oz    -DKBUILD_BASENAME='"makedev"'  -DKBUILD_MODNAME='"makedev"' -c -o libbb/makedev.o libbb/makedev.c

deps_libbb/makedev.o := \
  libbb/makedev.c \
  include/platform.h \
    $(wildcard include/config/werror.h) \
    $(wildcard include/config/big/endian.h) \
    $(wildcard include/config/little/endian.h) \
    $(wildcard include/config/nommu.h) \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/limits.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/limits.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/byteswap.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/endian.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/stdint.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/types.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/wchar.h \
  /usr/bin/../lib/clang/21/include/stdbool.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/unistd.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/mlibc-config.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/size_t.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/ssize_t.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/bits/off_t.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/access.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/uid_t.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/gid_t.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/pid_t.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/abi-bits/seek-whence.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/features.h \
  /home/rishies2010/OSDev/ZenOS/userland/files/busybox/../../sysroot/usr/include/sys/sysmacros.h \

libbb/makedev.o: $(deps_libbb/makedev.o)

$(deps_libbb/makedev.o):
