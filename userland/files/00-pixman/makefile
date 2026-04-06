LIB      = build/libpixman.a
BUILD_DIR = build
SYSROOT  ?= ../../sysroot
MLIBC_SYSROOT := $(abspath $(SYSROOT))
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN   ?= ../../../fat_man
VHD      ?= ../../../ZenOS.vhd
AR       := $(shell command -v llvm-ar 2>/dev/null || command -v ar)

SRC_DIR  = pixman

CORE_SRCS := \
	$(SRC_DIR)/pixman.c \
	$(SRC_DIR)/pixman-access.c \
	$(SRC_DIR)/pixman-access-accessors.c \
	$(SRC_DIR)/pixman-bits-image.c \
	$(SRC_DIR)/pixman-combine32.c \
	$(SRC_DIR)/pixman-combine-float.c \
	$(SRC_DIR)/pixman-conical-gradient.c \
	$(SRC_DIR)/pixman-edge.c \
	$(SRC_DIR)/pixman-edge-accessors.c \
	$(SRC_DIR)/pixman-fast-path.c \
	$(SRC_DIR)/pixman-filter.c \
	$(SRC_DIR)/pixman-general.c \
	$(SRC_DIR)/pixman-glyph.c \
	$(SRC_DIR)/pixman-gradient-walker.c \
	$(SRC_DIR)/pixman-image.c \
	$(SRC_DIR)/pixman-mmx.c \
	$(SRC_DIR)/pixman-implementation.c \
	$(SRC_DIR)/pixman-linear-gradient.c \
	$(SRC_DIR)/pixman-matrix.c \
	$(SRC_DIR)/pixman-noop.c \
	$(SRC_DIR)/pixman-radial-gradient.c \
	$(SRC_DIR)/pixman-region16.c \
	$(SRC_DIR)/pixman-region32.c \
	$(SRC_DIR)/pixman-region64f.c \
	$(SRC_DIR)/pixman-solid-fill.c \
	$(SRC_DIR)/pixman-trap.c \
	$(SRC_DIR)/pixman-utils.c \
	$(SRC_DIR)/pixman-x86.c

SSE2_SRCS  := $(SRC_DIR)/pixman-sse2.c
SSSE3_SRCS := $(SRC_DIR)/pixman-ssse3.c

CORE_OBJS  := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CORE_SRCS))
SSE2_OBJS  := $(BUILD_DIR)/pixman-sse2.o
SSSE3_OBJS := $(BUILD_DIR)/pixman-ssse3.o

MMX_SRCS  := $(SRC_DIR)/pixman-mmx.c
MMX_OBJS  := $(BUILD_DIR)/pixman-mmx.o

ALL_OBJS := $(CORE_OBJS) $(MMX_OBJS) $(SSE2_OBJS) $(SSSE3_OBJS)

$(BUILD_DIR)/pixman-mmx.o: $(SRC_DIR)/pixman-mmx.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -mmmx -msse -c $< -o $@

CC  ?= clang
LD  ?= ld.lld

COMMON_CPPFLAGS := \
	--sysroot=$(MLIBC_SYSROOT) \
	-isystem $(MLIBC_USR)/include \
	-isystem /usr/include \
	-I $(SRC_DIR) \
	-include $(SRC_DIR)/pixman-config.h

COMMON_CFLAGS := \
	-m64 -O2 -fPIE \
	-fno-stack-protector -mno-red-zone -nostdlib \
	-fvisibility=hidden \
	-fno-math-errno \
	-fno-trapping-math \
	-fmerge-all-constants \
	-DHAVE_CONFIG_H

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(BUILD_DIR)/pixman-sse2.o: $(SRC_DIR)/pixman-sse2.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -msse2 -c $< -o $@

$(BUILD_DIR)/pixman-ssse3.o: $(SRC_DIR)/pixman-ssse3.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -mssse3 -c $< -o $@

$(LIB): $(ALL_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(AR) rcs $@ $^

install: $(LIB)
	@mkdir -p $(MLIBC_USR)/lib $(MLIBC_USR)/include/pixman-1
	cp $(LIB) $(MLIBC_USR)/lib/libpixman.a
	cp $(SRC_DIR)/pixman.h $(MLIBC_USR)/include/pixman-1/pixman.h
	cp $(SRC_DIR)/pixman-version.h $(MLIBC_USR)/include/pixman-1/pixman-version.h
	cp $(SRC_DIR)/pixman-version.h $(MLIBC_USR)/include/pixman-version.h
	$(FATMAN) $(VHD) mkdir /usr/lib
	$(FATMAN) $(VHD) mkdir /usr/include/pixman-1
	$(FATMAN) $(VHD) import $(LIB) /usr/lib/libpixman.a
	$(FATMAN) $(VHD) import $(SRC_DIR)/pixman.h /usr/include/pixman-1/pixman.h
	$(FATMAN) $(VHD) import $(SRC_DIR)/pixman-version.h /usr/include/pixman-1/pixman-version.h
	$(FATMAN) $(VHD) import $(SRC_DIR)/pixman-version.h /usr/include/pixman-version.h

clean:
	rm -rf $(BUILD_DIR)

all: $(LIB)