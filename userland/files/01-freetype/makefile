LIB       = build/libfreetype.a
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

FT := freetype

SRCS = \
	$(FT)/src/base/ftsystem.c \
	$(FT)/src/base/ftinit.c \
	$(FT)/src/base/ftdebug.c \
	$(FT)/src/base/ftbase.c \
	$(FT)/src/base/ftbbox.c \
	$(FT)/src/base/ftbitmap.c \
	$(FT)/src/base/ftgasp.c \
	$(FT)/src/base/ftglyph.c \
	$(FT)/src/base/ftlcdfil.c \
	$(FT)/src/base/ftmm.c \
	$(FT)/src/base/ftstroke.c \
	$(FT)/src/gzip/ftgzip.c \
	$(FT)/src/truetype/truetype.c \
	$(FT)/src/sfnt/sfnt.c \
	$(FT)/src/autofit/autofit.c \
	$(FT)/src/smooth/smooth.c \
	$(FT)/src/raster/raster.c \
	$(FT)/src/psnames/psnames.c \
	$(FT)/src/pshinter/pshinter.c \
	$(FT)/src/psaux/psaux.c

OBJS = $(patsubst $(FT)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

COMMON_CPPFLAGS := \
	--sysroot=$(MLIBC_SYSROOT) \
	-isystem $(MLIBC_USR)/include \
	-I $(FT)/include \
	-I $(FT)/include/freetype/config \
	-DFT2_BUILD_LIBRARY \
	-UFT_CONFIG_OPTION_USE_PNG

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
	-fno-asynchronous-unwind-tables \
	-Wno-unused-parameter

$(BUILD_DIR)/%.o: $(FT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(AR) rcs $@ $^
	@echo "[freetype] built $@"

install: $(LIB)
	@mkdir -p $(MLIBC_USR)/lib
	@mkdir -p $(MLIBC_USR)/include
	cp $(LIB) $(MLIBC_USR)/lib/libfreetype.a
	cp -r $(FT)/include/freetype $(MLIBC_USR)/include/freetype
	cp $(FT)/include/ft2build.h $(MLIBC_USR)/include/ft2build.h
	@echo "[freetype] installed headers + lib to sysroot"

clean:
	rm -rf $(BUILD_DIR)

all: $(LIB)
