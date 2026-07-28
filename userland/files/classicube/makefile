APP = classicube
SRCS = \
	src/Animations.c \
	src/Audio.c \
	src/Audio_Null.c \
	src/AxisLinesRenderer.c \
	src/Bitmap.c \
	src/Block.c \
	src/BlockPhysics.c \
	src/Builder.c \
	src/Camera.c \
	src/Chat.c \
	src/Commands.c \
	src/Deflate.c \
	src/Drawer.c \
	src/Drawer2D.c \
	src/Entity.c \
	src/EntityComponents.c \
	src/EntityRenderers.c \
	src/EnvRenderer.c \
	src/Event.c \
	src/ExtMath.c \
	src/FancyLighting.c \
	src/Formats.c \
	src/Game.c \
	src/GameVersion.c \
	src/Generator.c \
	src/Graphics_SoftGPU.c \
	src/Gui.c \
	src/HeldBlockRenderer.c \
	src/Http_Worker.c \
	src/Input.c \
	src/InputHandler.c \
	src/Inventory.c \
	src/IsometricDrawer.c \
	src/LBackend.c \
	src/LScreens.c \
	src/LWeb.c \
	src/LWidgets.c \
	src/Launcher.c \
	src/Lighting.c \
	src/Logger.c \
	src/MapRenderer.c \
	src/MenuOptions.c \
	src/Menus.c \
	src/Model.c \
	src/Options.c \
	src/PackedCol.c \
	src/Particle.c \
	src/Physics.c \
	src/Picking.c \
	src/PluginAPI.c \
	src/Protocol.c \
	src/Queue.c \
	src/Resources.c \
	src/Screens.c \
	src/SelOutlineRenderer.c \
	src/SelectionBox.c \
	src/Server.c \
	src/Stream.c \
	src/String.c \
	src/SystemFonts.c \
	src/TexturePack.c \
	src/TouchUI.c \
	src/Utils.c \
	src/Vectors.c \
	src/Vorbis.c \
	src/Widgets.c \
	src/Window_ZenOS.c \
	src/World.c \
	src/Platform_ZenOS.c
OBJS ?= $(SRCS:.c=.o)

USERLAND_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SYSROOT  ?= ../../sysroot
MLIBC_SYSROOT := $(abspath $(SYSROOT))
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN   ?= ../../../fat_man
VHD      ?= ../../../ZenOS.vhd
INSTALL_PATH ?= /bin/$(APP)

ifeq ($(origin CC), default)
CC := clang
endif

USER_OPT ?= -O3 -flto=auto
USER_OPTIMIZATIONS := -fomit-frame-pointer -funroll-loops \
                      -ftree-vectorize \
                      -fno-semantic-interposition \
                      -fno-plt -fno-stack-protector

CFLAGS = -std=gnu99 -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-missing-braces -Wno-pointer-sign -Wno-int-conversion -Wno-deprecated-non-prototype
USER_OPT = -O2
CPPFLAGS = -I src -DCC_BUILD_MANUAL -DCC_BUILD_ZENOS
LDFLAGS ?=
LDLIBS ?=
INSTALL_HOOK ?= :

COMMON_CPPFLAGS := --sysroot=$(MLIBC_SYSROOT) -isystem $(MLIBC_USR)/include -I ../.. -I .
COMMON_CFLAGS := -m64 -g0 -ffreestanding -mno-red-zone \
                 -nostdlib -fPIE $(USER_OPT) $(USER_OPTIMIZATIONS) \
                 -fno-exceptions -fno-rtti -fno-unwind-tables \
                 -fno-asynchronous-unwind-tables

COMMON_LDFLAGS := -m elf_x86_64 -pie -e _start -z max-page-size=0x1000 \
                  -dynamic-linker /mnt/drv0/usr/lib/ld.so -rpath /mnt/drv0/usr/lib \
                  -L$(MLIBC_USR)/lib \
                  --as-needed \
                  -O3 \
                  -z combreloc -z now -z relro \
                  --gc-sections

MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 4)

all: $(APP)

%.o: %.c
	$(CC) $(COMMON_CPPFLAGS) $(CPPFLAGS) $(COMMON_CFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.sfn
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(APP): $(OBJS)
	ld.lld $(COMMON_LDFLAGS) $(LDFLAGS) \
		$(MLIBC_USR)/lib/crti.o $(MLIBC_USR)/lib/crt0.o \
		$(OBJS) $(LDLIBS) -l:libc.so -l:libm.so \
		$(MLIBC_USR)/lib/crtn.o -o $(APP).elf
	strip -R .comment -R .note -R .debug_info -R .debug_abbrev \
	      -R .debug_line -R .debug_str -R .debug_ranges $(APP).elf 2>/dev/null || true

install: $(APP)
	$(FATMAN) $(VHD) mkdir /texpacks || true
	$(FATMAN) $(VHD) import default.zip /texpacks/default.zip || true
	$(FATMAN) $(VHD) delete $(INSTALL_PATH) || true
	$(FATMAN) $(VHD) import $(APP).elf $(INSTALL_PATH)
	@$(INSTALL_HOOK)

clean:
	rm -f $(OBJS) $(APP).elf

size: $(APP)
	@size $(APP).elf

profile: $(APP)
	perf record ./$(APP)
	perf report