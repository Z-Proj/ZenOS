#!/bin/bash
nasm -f elf64 ../../crt0.asm -o crt0.o

clang -O2 -std=c99 -DLUA_USE_ZENOS -fno-stack-protector -fno-common \
  -ffreestanding -fno-pic -fno-pie -mno-red-zone -m64 -nostdlib \
  -isystem ../../libs/include -I ../../libs -I ../../ \
  lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c \
  llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c \
  ltable.c ltm.c lundump.c lvm.c lzio.c \
  lauxlib.c lbaselib.c ldblib.c lcorolib.c liolib.c lmathlib.c \
  ltablib.c lstrlib.c lutf8lib.c loadlib.c linit.c loslib.c lua.c \
  crt0.o -L../../libs -lc -lm \
  -fuse-ld=lld -Wl,-m,elf_x86_64 -Wl,-e,_start \
  -Wl,-T,../../userelf.ld \
  -Wl,--allow-multiple-definition \
  -o lua

../../../fat_man ../../../ZenOS.vhd delete /bin/lua || true
../../../fat_man ../../../ZenOS.vhd import lua /bin/lua
