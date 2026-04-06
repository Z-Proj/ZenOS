APP      = harp
SYSROOT  ?= ../../sysroot
MLIBC_SYSROOT := $(abspath $(SYSROOT))
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN   ?= ../../../fat_man
VHD      ?= ../../../ZenOS.vhd
INSTALL_PATH ?= /bin/$(APP)

CC  := clang
LD  := ld.lld

SRCS = harp_main.c harp_wm.c harp_draw.c harp_input.c
OBJS = $(SRCS:.c=.o) default.o

PIXMAN_INC := $(MLIBC_USR)/include
PIXMAN_LIB := $(MLIBC_USR)/lib/libpixman.a

COMMON_CPPFLAGS := \
	--sysroot=$(MLIBC_SYSROOT) \
	-isystem $(MLIBC_USR)/include \
	-I ../.. -I . \
	-I $(PIXMAN_INC)

COMMON_CFLAGS := \
    -m64 -O3 -fPIE -fomit-frame-pointer -flto -ffast-math \
    -fno-stack-protector -mno-red-zone -nostdlib -finline-functions \
    -fvisibility=hidden

COMMON_LDFLAGS := \
	-m elf_x86_64 -pie -e _start -s \
	-z max-page-size=0x1000 \
	-dynamic-linker /mnt/drv0/usr/lib/ld.so \
	-rpath /mnt/drv0/usr/lib \
	-L$(MLIBC_USR)/lib

all: $(APP)

%.o: %.c
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -c $< -o $@

%.o: %.sfn
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(APP): $(OBJS) $(PIXMAN_LIB)
	$(LD) $(COMMON_LDFLAGS) \
		$(MLIBC_USR)/lib/crti.o $(MLIBC_USR)/lib/crt0.o \
		$(OBJS) $(PIXMAN_LIB) -l:libc.so -l:libm.so \
		$(MLIBC_USR)/lib/crtn.o -o $@

install: $(APP)
	$(FATMAN) $(VHD) mkdir /bin || true
	$(FATMAN) $(VHD) delete $(INSTALL_PATH) || true
	$(FATMAN) $(VHD) import $(APP) $(INSTALL_PATH)
	$(FATMAN) $(VHD) mkdir /lib
	$(FATMAN) $(VHD) mkdir /lib/harp
	$(FATMAN) $(VHD) import bg1.tga /lib/harp/bg1.tga
	$(FATMAN) $(VHD) import bg2.tga /lib/harp/bg2.tga
	$(FATMAN) $(VHD) import bg3.tga /lib/harp/bg3.tga

clean:
	rm -f $(OBJS) $(APP)
