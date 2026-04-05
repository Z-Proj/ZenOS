APP = blend2d-demo
LIB = build/libblend2d.a
BUILD_DIR = build
SYSROOT ?= ../../sysroot
MLIBC_SYSROOT := $(abspath $(SYSROOT))
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN ?= ../../../fat_man
VHD ?= ../../../ZenOS.vhd
INSTALL_PATH ?= /bin/$(APP)
AR := $(shell command -v llvm-ar 2>/dev/null || command -v ar)
HOST_CXX_BASE := $(shell ls -d /usr/include/c++/* 2>/dev/null | sort -V | tail -n 1)
HOST_CXX_TARGET := $(shell find $(HOST_CXX_BASE) -mindepth 1 -maxdepth 1 -type d -name '*-*' | sort | tail -n 1)

BLEND2D_SRCS := $(shell find blend2d -type f -name '*.cpp' ! -name '*_test.cpp' | sort) zenos_runtime.cpp
BLEND2D_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(BLEND2D_SRCS))
APP_SRC = demo.cpp
APP_OBJ = $(BUILD_DIR)/demo.o
PUBLIC_HDRS := $(shell find blend2d -type f -name '*.h' ! -name '*_p.h' | sort)

CXX ?= g++-14
LD ?= ld.lld

COMMON_CPPFLAGS := --sysroot=$(MLIBC_SYSROOT) -isystem $(MLIBC_USR)/include -isystem $(HOST_CXX_BASE) -isystem $(HOST_CXX_TARGET) -isystem /usr/include -I .
COMMON_CXXFLAGS := -m64 -fno-stack-protector -mno-red-zone -nostdlib -fPIE -fvisibility=hidden -fno-math-errno -fno-threadsafe-statics -fno-exceptions -fno-rtti -fno-semantic-interposition -fno-trapping-math -fno-finite-math-only -O2 -fmerge-all-constants -fno-enforce-eh-specs -ftree-vectorize -DBL_BUILD_NO_TLS -DBL_BUILD_NO_FUTEX -DBL_BUILD_NO_JIT -DBL_BUILD_OPT_AVX512 -DBL_STATIC
WARNFLAGS := -Wall -Wextra -Wconversion -Wdouble-promotion -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wlogical-not-parentheses -Wrestrict -Wbidi-chars=any
COMMON_LDFLAGS := -m elf_x86_64 -pie -e _start -z max-page-size=0x1000 -dynamic-linker /mnt/drv0/usr/lib/ld.so -rpath /mnt/drv0/usr/lib -L$(MLIBC_USR)/lib

all: $(APP) $(LIB)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@extra=""; \
	case "$<" in \
		*matrix_avx.cpp|*pngops_avx.cpp) extra="-mpopcnt -mpclmul -mavx" ;; \
		*pixelconverter_avx2.cpp|*deflatedecoderfast_avx2.cpp|*otglyf_avx2.cpp|*interpolation_avx2.cpp) extra="-mpopcnt -mpclmul -mbmi -mbmi2 -mavx2 -DBL_TARGET_OPT_POPCNT -DBL_TARGET_OPT_BMI2" ;; \
		*pixelconverter_ssse3.cpp) extra="-mssse3" ;; \
		*checksum_sse4_2.cpp|*otglyf_sse4_2.cpp) extra="-mpopcnt -mpclmul -msse4.2" ;; \
	esac; \
	$(CXX) $(COMMON_CPPFLAGS) $(COMMON_CXXFLAGS) $(WARNFLAGS) $$extra -c "$<" -o "$@"

$(LIB): $(BLEND2D_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(APP): $(APP_OBJ) $(LIB)
	$(LD) $(COMMON_LDFLAGS) \
		$(MLIBC_USR)/lib/crti.o $(MLIBC_USR)/lib/crt0.o \
		$(APP_OBJ) $(LIB) -lc -lm -lpthread \
		$(MLIBC_USR)/lib/crtn.o -o $@

install: $(APP) $(LIB)
	@mkdir -p $(MLIBC_USR)/include/blend2d $(MLIBC_USR)/include/blend2d/core $(MLIBC_USR)/lib
	@find $(MLIBC_USR)/include/blend2d -type f -delete 2>/dev/null || true
	@for hdr in $(PUBLIC_HDRS); do \
		dest="$(MLIBC_USR)/include/$$hdr"; \
		mkdir -p "$$(dirname "$$dest")"; \
		cp "$$hdr" "$$dest"; \
	done
	cp $(LIB) $(MLIBC_USR)/lib/libblend2d.a
	$(FATMAN) $(VHD) mkdir /bin || true
	$(FATMAN) $(VHD) delete $(INSTALL_PATH) || true
	$(FATMAN) $(VHD) import $(APP) $(INSTALL_PATH)
	$(FATMAN) $(VHD) mkdir /usr || true
	$(FATMAN) $(VHD) mkdir /usr/include || true
	$(FATMAN) $(VHD) mkdir /usr/include/blend2d || true
	$(FATMAN) $(VHD) mkdir /usr/include/blend2d/core || true
	$(FATMAN) $(VHD) mkdir /usr/lib || true
	$(FATMAN) $(VHD) delete /usr/lib/libblend2d.a || true
	$(FATMAN) $(VHD) import $(LIB) /usr/lib/libblend2d.a
	@for hdr in $(PUBLIC_HDRS); do \
		dest="/usr/include/$$hdr"; \
		$(FATMAN) $(VHD) delete "$$dest" || true; \
		$(FATMAN) $(VHD) import "$$hdr" "$$dest"; \
	done

clean:
	rm -rf $(BUILD_DIR) $(APP)
