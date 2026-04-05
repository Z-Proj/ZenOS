MW_SRC        = microwindows
CONFIG        = $(MW_SRC)/Configs/config.zenos
SYSROOT      ?= ../../sysroot
MLIBC_SYSROOT := $(abspath $(SYSROOT))
MLIBC_USR    := $(MLIBC_SYSROOT)/usr
FATMAN       ?= ../../../fat_man
VHD          ?= ../../../ZenOS.vhd

CC    := clang
CXX   := clang++
LD    := ld.lld
AR    := llvm-ar

export CC CXX LD AR
export MW_DIR_SRC := $(abspath $(MW_SRC))
export MW_DIR_OBJ := $(abspath $(MW_SRC))

all:
	$(MAKE) -C $(MW_SRC) CONFIG=$(abspath $(CONFIG)) \
		MLIBC_SYSROOT=$(abspath $(SYSROOT)) \
		MLIBC_USR=$(abspath $(SYSROOT))/usr
install: all
	$(FATMAN) $(VHD) mkdir /usr || true
	$(FATMAN) $(VHD) mkdir /usr/lib || true
	$(FATMAN) $(VHD) mkdir /usr/include || true
	$(FATMAN) $(VHD) mkdir /usr/include/microwin || true
	$(FATMAN) $(VHD) delete /usr/lib/libmwin.a || true
	$(FATMAN) $(VHD) import $(MW_SRC)/lib/libmwin.a /usr/lib/libmwin.a || true
	$(FATMAN) $(VHD) delete /usr/lib/libnano-X.a || true
	$(FATMAN) $(VHD) import $(MW_SRC)/lib/libnano-X.a /usr/lib/libnano-X.a || true
	@for hdr in $(MW_SRC)/include/*.h; do \
		base=$$(basename $$hdr); \
		$(FATMAN) $(VHD) delete /usr/include/microwin/$$base || true; \
		$(FATMAN) $(VHD) import $$hdr /usr/include/microwin/$$base; \
	done

clean:
	$(MAKE) -C $(MW_SRC) CONFIG=$(abspath $(CONFIG)) clean
