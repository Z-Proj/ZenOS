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
CFLAGS ?=
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
INSTALL_HOOK ?= :

COMMON_CPPFLAGS := --sysroot=$(MLIBC_SYSROOT) -isystem $(MLIBC_USR)/include -I ../.. -I .
COMMON_CFLAGS := -m64 -ffreestanding -fno-stack-protector -mno-red-zone -nostdlib -fPIE -O1
COMMON_LDFLAGS := -m elf_x86_64 -pie -e _start -z max-page-size=0x1000 \
	-dynamic-linker /mnt/drv0/usr/lib/ld.so -rpath /mnt/drv0/usr/lib \
	-L$(MLIBC_USR)/lib \
	--as-needed

all: $(APP)

%.o: %.c
	$(CC) $(COMMON_CPPFLAGS) $(CPPFLAGS) $(COMMON_CFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.sfn
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(APP): $(OBJS)
	ld.lld $(COMMON_LDFLAGS) $(LDFLAGS) \
		$(MLIBC_USR)/lib/crti.o $(MLIBC_USR)/lib/crt0.o \
		$(OBJS) $(LDLIBS) -l:libc.so -l:libm.so \
		$(MLIBC_USR)/lib/crtn.o -o $(APP)

install: $(APP)
	$(FATMAN) $(VHD) delete $(INSTALL_PATH) || true
	$(FATMAN) $(VHD) import $(APP) $(INSTALL_PATH)
	@$(INSTALL_HOOK)

clean:
	rm -f $(OBJS) $(APP)
