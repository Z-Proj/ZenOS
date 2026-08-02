APP      = notepad
SYSROOT  ?= ../../sysroot
MLIBC_SYSROOT := $(abspath $(SYSROOT))
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN   ?= ../../../fat_man
VHD      ?= ../../../ZenOS.vhd
INSTALL_PATH ?= /bin/$(APP)

CC  := clang
LD  := ld.lld

SRCS = nk_impl.c notepad.c
OBJS = nk_impl.o notepad.o

COMMON_CPPFLAGS := \
	--sysroot=$(MLIBC_SYSROOT) \
	-isystem $(MLIBC_USR)/include \
	-I ../.. -I . -I ../nk_widgets

COMMON_CFLAGS := \
	-m64 -O3 -fPIE -fomit-frame-pointer -flto -ffast-math \
	-fno-stack-protector -mno-red-zone -nostdlib \
	-fvisibility=hidden -g0 \
	-fmerge-all-constants \
	-fno-unwind-tables \
	-fno-asynchronous-unwind-tables

COMMON_LDFLAGS := \
	-m elf_x86_64 -pie -e _start \
	-z max-page-size=0x1000 \
	-dynamic-linker /mnt/drv0/usr/lib/ld.so \
	-rpath /mnt/drv0/usr/lib \
	-L$(MLIBC_USR)/lib

all: $(APP)

nk_impl.o: nk_impl.c
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -fvisibility=default -c $< -o $@

%.o: %.c
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(APP): $(OBJS)
	$(LD) $(COMMON_LDFLAGS) \
		$(MLIBC_USR)/lib/crti.o $(MLIBC_USR)/lib/crt0.o \
		$(OBJS) -l:libc.so -l:libm.so \
		$(MLIBC_USR)/lib/libfont.a \
		$(MLIBC_USR)/lib/libfreetype.a \
		$(MLIBC_USR)/lib/crtn.o -o $(APP).elf

install: $(APP)
	$(FATMAN) $(VHD) mkdir /bin || true
	$(FATMAN) $(VHD) delete $(INSTALL_PATH) || true
	$(FATMAN) $(VHD) import $(APP).elf $(INSTALL_PATH)

clean:
	rm -f $(OBJS) $(APP).elf
