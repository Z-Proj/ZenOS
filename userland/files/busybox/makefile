APP = busybox.elf
BIN = busybox
SRC_DIR = busybox
CONFIG_SRC = zenos.config
CC_WRAPPER = zenos-clang
CONFIG_MERGER = merge-config.sh
USERLAND_DIR := $(abspath ../..)
MLIBC_SYSROOT := $(USERLAND_DIR)/sysroot
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN ?= ../../../fat_man
VHD ?= ../../../ZenOS.vhd

all: $(APP)

$(SRC_DIR)/.config: $(CONFIG_SRC) $(CONFIG_MERGER)
	yes "" | $(MAKE) -C $(SRC_DIR) allnoconfig >/dev/null
	./$(CONFIG_MERGER) $(SRC_DIR)/.config $(CONFIG_SRC)
	$(MAKE) -C $(SRC_DIR) silentoldconfig

config: $(SRC_DIR)/.config

$(APP): $(SRC_DIR)/.config $(CC_WRAPPER)
	$(MAKE) -C $(SRC_DIR) CC=$(abspath $(CC_WRAPPER)) SKIP_STRIP=y $(BIN)
	cp $(SRC_DIR)/$(BIN) $(APP)
	strip -R .comment -R .note -R .debug_info -R .debug_abbrev \
	      -R .debug_line -R .debug_str -R .debug_ranges $(APP) 2>/dev/null || true

install: $(APP)
	$(FATMAN) $(VHD) mkdir /bin || true
	$(FATMAN) $(VHD) delete /bin/busybox || true
	$(FATMAN) $(VHD) delete /bin/sh || true
	$(FATMAN) $(VHD) delete /bin/ash || true
	$(FATMAN) $(VHD) import $(APP) /bin/busybox
	$(FATMAN) $(VHD) import $(APP) /bin/sh
	$(FATMAN) $(VHD) import $(APP) /bin/ash

clean:
	$(MAKE) -C $(SRC_DIR) clean
	rm -f $(APP)
