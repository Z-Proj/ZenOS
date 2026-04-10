#
# @file : /userland/mlibc_app.mk
# @brief : Userspace build template for mlibc dynamic app.
#
# This file is a part of the Zen (ZenOS)
# Operating System build process, and is
# released under the terms of the MIT
# Licensing : Read LICENSE at the root of
# the repository.
#
# @copyright (c) 2026
# @author : Rishies2010
#

APP ?=
SRCS ?= $(APP).c
OBJS ?= $(SRCS:.c=.o)

USERLAND_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
MLIBC_SYSROOT := $(USERLAND_DIR)/sysroot
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN ?= ../../../fat_man
VHD ?= ../../../ZenOS.vhd
INSTALL_PATH ?= /bin/$(APP)

ifeq ($(origin CC), default)
CC := clang
endif

USER_OPT ?= -O3 -flto=auto
USER_OPTIMIZATIONS := -fomit-frame-pointer -funroll-loops \
                      -ftree-vectorize \
                      -fno-semantic-interposition \
                      -fno-plt -fno-stack-protector

CFLAGS ?=
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
INSTALL_HOOK ?= :

COMMON_CPPFLAGS := --sysroot=$(MLIBC_SYSROOT) -isystem $(MLIBC_USR)/include -I ../.. -I .
COMMON_CFLAGS := -m64 -g0 -ffreestanding -mno-red-zone \
                 -nostdlib -fPIE $(USER_OPT) $(USER_OPTIMIZATIONS) \
                 -fno-exceptions -fno-rtti -fno-unwind-tables \
                 -fno-asynchronous-unwind-tables

COMMON_LDFLAGS := -m elf_x86_64 -pie -e _start -z max-page-size=0x1000 \
                  -dynamic-linker /mnt/drv0/usr/lib/ld.so -rpath /mnt/drv0/usr/lib \
                  -L$(MLIBC_USR)/lib \
                  --as-needed \
                  -O3 \
                  -z combreloc -z now -z relro \
                  --gc-sections

MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 4)

all: $(APP)

%.o: %.c
	$(CC) $(COMMON_CPPFLAGS) $(CPPFLAGS) $(COMMON_CFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.sfn
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(APP): $(OBJS)
	ld.lld $(COMMON_LDFLAGS) $(LDFLAGS) \
		$(MLIBC_USR)/lib/crti.o $(MLIBC_USR)/lib/crt0.o \
		$(OBJS) $(LDLIBS) -l:libc.so -l:libm.so \
		$(MLIBC_USR)/lib/crtn.o -o $(APP).elf
	strip -R .comment -R .note -R .debug_info -R .debug_abbrev \
	      -R .debug_line -R .debug_str -R .debug_ranges $(APP).elf 2>/dev/null || true

install: $(APP)
	$(FATMAN) $(VHD) delete $(INSTALL_PATH) || true
	$(FATMAN) $(VHD) import $(APP).elf $(INSTALL_PATH)
	@$(INSTALL_HOOK)

clean:
	rm -f $(OBJS) $(APP).elf

size: $(APP)
	@size $(APP).elf

profile: $(APP)
	perf record ./$(APP)
	perf report
