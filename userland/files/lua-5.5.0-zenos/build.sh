#!/bin/bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SYSROOT="$ROOT/userland/sysroot"
USR="$SYSROOT/usr"

clang --sysroot="$SYSROOT" -isystem "$USR/include" -I ../../ -O2 -std=c99 \
  -DLUA_USE_ZENOS -fno-stack-protector -fno-common -ffreestanding -mno-red-zone \
  -m64 -nostdlib -fPIE \
  lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c \
  llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c \
  ltable.c ltm.c lundump.c lvm.c lzio.c \
  lauxlib.c lbaselib.c ldblib.c lcorolib.c liolib.c lmathlib.c \
  ltablib.c lstrlib.c lutf8lib.c loadlib.c linit.c loslib.c lua.c \
  "$USR/lib/crti.o" "$USR/lib/crt0.o" "$USR/lib/crtn.o" \
  -fuse-ld=lld -Wl,-m,elf_x86_64 -Wl,-pie -Wl,-e,_start \
  -Wl,-z,max-page-size=0x1000 -Wl,-dynamic-linker,/mnt/drv0/usr/lib/ld.so \
  -Wl,-rpath,/mnt/drv0/usr/lib -L"$USR/lib" -lc -lm \
  -o lua

"$ROOT/fat_man" "$ROOT/ZenOS.vhd" delete /bin/lua || true
"$ROOT/fat_man" "$ROOT/ZenOS.vhd" import lua /bin/lua
