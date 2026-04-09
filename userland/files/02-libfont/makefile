LIB       = build/libfont.a
BUILD_DIR = build
SYSROOT  ?= ../../sysroot
MLIBC_SYSROOT := $(abspath $(SYSROOT))
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN   ?= ../../../fat_man
VHD      ?= ../../../ZenOS.vhd
AR       := $(shell command -v llvm-ar 2>/dev/null || command -v ar)

ifeq ($(origin CC), default)
CC := clang
endif

SRCS = libfont.c
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

COMMON_CPPFLAGS := \
	--sysroot=$(MLIBC_SYSROOT) \
	-isystem $(MLIBC_USR)/include \
	-I . \
	-I ../01-freetype/freetype/include \
	-I ../01-freetype/freetype/include/freetype/config

COMMON_CFLAGS := \
	-m64 -O3 -fPIE \
	-fomit-frame-pointer \
	-flto \
	-ffast-math \
	-fno-stack-protector \
	-mno-red-zone \
	-nostdlib \
	-fvisibility=hidden \
	-finline-functions \
	-fmerge-all-constants \
	-fno-unwind-tables \
	-fno-asynchronous-unwind-tables

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(AR) rcs $@ $^
	@echo "[libfont] built $@"

install: $(LIB)
	@mkdir -p $(MLIBC_USR)/lib $(MLIBC_USR)/include
	cp $(LIB) $(MLIBC_USR)/lib/libfont.a
	cp libfont.h $(MLIBC_USR)/include/libfont.h
	@echo "[libfont] installed"

clean:
	rm -rf $(BUILD_DIR)

all: $(LIB)
