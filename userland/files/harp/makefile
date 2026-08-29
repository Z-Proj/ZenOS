APP      = harp
SYSROOT  ?= ../../sysroot
MLIBC_SYSROOT := $(abspath $(SYSROOT))
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN   ?= ../../../fat_man
VHD      ?= ../../../ZenOS.vhd
INSTALL_PATH ?= /bin/$(APP)

CC  := clang
LD  := ld.lld

SRCS = harp_main.c harp_wm.c harp_draw.c harp_input.c harp_stb_image_impl.c harp_stb_image_write_impl.c
OBJS = $(SRCS:.c=.o)

COMMON_CPPFLAGS := \
	--sysroot=$(MLIBC_SYSROOT) \
	-isystem $(MLIBC_USR)/include \
	-I ../.. -I .

COMMON_CFLAGS := \
	-m64 -O3 -fPIE -fomit-frame-pointer -flto -ffast-math \
	-fno-stack-protector -mno-red-zone -nostdlib -finline-functions \
	-fvisibility=hidden \
	-fmerge-all-constants \
	-fno-unwind-tables \
	-fno-asynchronous-unwind-tables

COMMON_LDFLAGS := \
	-m elf_x86_64 -pie -e _start -s \
	-z max-page-size=0x1000 \
	-dynamic-linker /mnt/drv0/usr/lib/ld.so \
	-rpath /mnt/drv0/usr/lib \
	-L$(MLIBC_USR)/lib

all: $(APP)

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
	$(FATMAN) $(VHD) mkdir /lib || true
	$(FATMAN) $(VHD) mkdir /lib/harp || true
	$(FATMAN) $(VHD) mkdir /lib/harp/scrshot || true
	$(FATMAN) $(VHD) mkdir /lib/fonts || true
	$(FATMAN) $(VHD) delete /lib/fonts/default.ttf || true
	$(FATMAN) $(VHD) import default.ttf /lib/fonts/default.ttf
	$(FATMAN) $(VHD) import bg.png /lib/harp/bg.png
	$(FATMAN) $(VHD) import bg2.jpg /lib/harp/bg2.jpg
	$(FATMAN) $(VHD) import bg3.jpg /lib/harp/bg3.jpg
	$(FATMAN) $(VHD) import splash.tga /sys/splash.tga
	$(FATMAN) $(VHD) mkdir sys/crash
	$(FATMAN) $(VHD) import crash/AC.tga sys/crash/AC.tga
	$(FATMAN) $(VHD) import crash/DF.tga sys/crash/DF.tga
	$(FATMAN) $(VHD) import crash/DIV0.tga sys/crash/DIV0.tga
	$(FATMAN) $(VHD) import crash/EXCEPTION.tga sys/crash/EXCEPTION.tga
	$(FATMAN) $(VHD) import crash/GPF.tga sys/crash/GPF.tga
	$(FATMAN) $(VHD) import crash/MC.tga sys/crash/MC.tga
	$(FATMAN) $(VHD) import crash/PF.tga sys/crash/PF.tga
	$(FATMAN) $(VHD) import crash/SIMD.tga sys/crash/SIMD.tga
	$(FATMAN) $(VHD) import crash/SNP.tga sys/crash/SNP.tga
	$(FATMAN) $(VHD) import crash/SS.tga sys/crash/SS.tga
	$(FATMAN) $(VHD) import crash/TSS.tga sys/crash/TSS.tga
	$(FATMAN) $(VHD) import crash/UD.tga sys/crash/UD.tga

clean:
	rm -f $(OBJS) $(APP).elf
