# ZenOS Makefile

CFLAGS = -mcmodel=kernel -m64 -ffreestanding -fno-stack-protector -Wall -Wextra -c -fno-pie -fno-pic -Wno-missing-braces
LDFLAGS = -Wl,-T,linker.ld -fuse-ld=lld -nostdlib -no-pie
ASFLAGS = -f elf64

SRC_DIR = src
BUILD_DIR = build
ISO_DIR = iso
USER_DIR = userland

LIMINE_DIR = limine
LIMINE_BINARIES = $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin

C_SOURCES = $(shell find $(SRC_DIR) -name "*.c")
ASM_SOURCES = $(shell find $(SRC_DIR) -name "*.asm")

OBJ = $(C_SOURCES:.c=.o) $(ASM_SOURCES:.asm=.o)

KERNEL = $(BUILD_DIR)/kernel.bin

ISO_IMAGE = ZenOS.iso

all: clean deps fat $(ISO_IMAGE) user init

%.o: %.c
	clang $(CFLAGS) $< -o $@

%.o: %.asm
	nasm $(ASFLAGS) $< -o $@

$(KERNEL): $(OBJ)
	@mkdir -p $(BUILD_DIR)
	clang $(LDFLAGS) $(OBJ) -o $@

$(ISO_IMAGE): $(KERNEL) $(LIMINE_BINARIES)
	@mkdir -p $(ISO_DIR)/boot
	cp $(KERNEL) $(ISO_DIR)/boot/
	cp $(SRC_DIR)/boot/limine.conf $(ISO_DIR)/boot/
	cp $(LIMINE_BINARIES) $(ISO_DIR)/boot/
	xorriso -as mkisofs \
		-b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-o $(ISO_IMAGE) $(ISO_DIR)
	$(LIMINE_DIR)/limine bios-install $(ISO_IMAGE)

user:
	$(USER_DIR)/build_elf.sh

init:
	@printf "/mnt/drv0/bin/shell\n" > init.run
	./fat_man ZenOS.vhd mkdir /sys
	./fat_man ZenOS.vhd import init.run /sys/init.run
	@rm init.run
	@echo "init.run imported successfully."

help:
	@echo "The ZenOS Project - Makefile Help Menu"
	@echo "Available commands:"
	@echo "  make all       : Build the entire OS and initialize."
	@echo "  make $(ISO_IMAGE) : Build just the kernel."
	@echo "  make user      : Build userspace and import to disk."
	@echo "  make qemu      : Run the OS in QEMU."
	@echo "  make run       : Run in VBox (Requires you to set up a VM called ZenOS in VBox)."
	@echo "  make clean     : Clean all build files (Doesn't affect $(ISO_IMAGE) / ZenOS.vhd)."
	@echo "  make funcs     : Generate a text file with all the functions defined in ZenOS"
	@echo "  make deps      : Check whether you have tools required to build ZenOS"
	@echo "  make fat       : Build the FAT32 Linux Manager for ZenOS.vhd."

funcs:
	@ctags -R --languages=C --c-kinds=f src && \
	sed -n 's@^[^\t]*\t[^\t]*\t/\^\(.*\)\$$/;".*@\1;@p' tags | \
	grep -v '^static ' | \
	sed -E 's/\s*\{\s*;$$/;/; s/\s*\{$$/;/; s/^\s*([^;]+)\($$/\1;/;' | \
	sort -u > funcs.txt
	rm tags
	@echo "✓ Generated funcs.txt"

fat:
	clang fat_man.c \
	src/drv/disk/fatfs/ff.c \
	src/drv/disk/fatfs/ffunicode.c \
	-Isrc/drv/disk/fatfs \
	-DFF_FS_REENTRANT=0 \
	-DFF_USE_LFN=2 \
	-DFF_MAX_LFN=255 \
	-o fat_man 2>&1

clean:
	rm -rf $(OBJ) $(KERNEL) $(ISO_IMAGE) $(ISO_DIR) $(BUILD_DIR) src/cpu/ap.bin

deps:
	@echo "Checking ZenOS build dependencies..."
	@missing=0; \
	check() { \
		if command -v $$1 >/dev/null 2>&1; then \
			printf "  [OK]      %s\n" $$1; \
		else \
			printf "  [NO] %s\n" $$1; \
			missing=1; \
		fi; \
	}; \
	check clang; \
	check ld.lld; \
	check nasm; \
	check xorriso; \
	check qemu-system-x86_64; \
	check gdb; \
	check socat; \
	check basename; \
	check rm; \
	if [ $$missing -ne 0 ]; then \
		echo ""; \
		echo "Some dependencies are missing."; \
		echo "Please install them before building ZenOS."; \
		exit 1; \
	else \
		echo ""; \
		echo "All dependencies satisfied."; \
	fi


run:
	virtualboxvm --startvm "ZenOS" &

qemu:
	qemu-system-x86_64 \
	-cdrom ZenOS.iso \
	-audiodev pa,id=snd0 \
	-machine pcspk-audiodev=snd0 \
	-m 128M \
	-drive file=ZenOS.vhd,if=ide,index=0 \
	-drive file=Storage.vhd,if=ide,index=1 \
	-boot d \
	-smp 2 \
	-serial stdio \
	-netdev user,id=net0 \
	-device e1000,netdev=net0 \
	-display gtk,gl=on \
	-enable-kvm \
	-cpu host

stop:
	VBoxManage controlvm "ZenOS" poweroff

out:
	socat - UNIX-CONNECT:/tmp/zenos

gdb:
	gdb build/kernel.bin

.PHONY: all clean run qemu out stop gdb fat funcs
