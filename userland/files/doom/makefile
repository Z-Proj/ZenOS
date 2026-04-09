APP      = doom
SYSROOT  ?= ../../sysroot
MLIBC_SYSROOT := $(abspath $(SYSROOT))
MLIBC_USR := $(MLIBC_SYSROOT)/usr
FATMAN   ?= ../../../fat_man
VHD      ?= ../../../ZenOS.vhd
INSTALL_PATH ?= /bin/$(APP)

CC  := clang
LD  := ld.lld

DOOM_DIR = doomgeneric

DOOM_CORE_SRCS := \
	$(DOOM_DIR)/am_map.c \
	$(DOOM_DIR)/d_event.c \
	$(DOOM_DIR)/d_items.c \
	$(DOOM_DIR)/d_iwad.c \
	$(DOOM_DIR)/d_loop.c \
	$(DOOM_DIR)/d_main.c \
	$(DOOM_DIR)/d_mode.c \
	$(DOOM_DIR)/d_net.c \
	$(DOOM_DIR)/doomgeneric.c \
	$(DOOM_DIR)/doomstat.c \
	$(DOOM_DIR)/dstrings.c \
	$(DOOM_DIR)/dummy.c \
	$(DOOM_DIR)/f_finale.c \
	$(DOOM_DIR)/f_wipe.c \
	$(DOOM_DIR)/g_game.c \
	$(DOOM_DIR)/hu_lib.c \
	$(DOOM_DIR)/hu_stuff.c \
	$(DOOM_DIR)/i_cdmus.c \
	$(DOOM_DIR)/i_endoom.c \
	$(DOOM_DIR)/i_input.c \
	$(DOOM_DIR)/i_joystick.c \
	$(DOOM_DIR)/i_scale.c \
	$(DOOM_DIR)/i_sound.c \
	$(DOOM_DIR)/i_system.c \
	$(DOOM_DIR)/i_timer.c \
	$(DOOM_DIR)/i_video.c \
	$(DOOM_DIR)/info.c \
	$(DOOM_DIR)/m_argv.c \
	$(DOOM_DIR)/m_bbox.c \
	$(DOOM_DIR)/m_cheat.c \
	$(DOOM_DIR)/m_config.c \
	$(DOOM_DIR)/m_controls.c \
	$(DOOM_DIR)/m_fixed.c \
	$(DOOM_DIR)/m_menu.c \
	$(DOOM_DIR)/m_misc.c \
	$(DOOM_DIR)/m_random.c \
	$(DOOM_DIR)/memio.c \
	$(DOOM_DIR)/p_ceilng.c \
	$(DOOM_DIR)/p_doors.c \
	$(DOOM_DIR)/p_enemy.c \
	$(DOOM_DIR)/p_floor.c \
	$(DOOM_DIR)/p_inter.c \
	$(DOOM_DIR)/p_lights.c \
	$(DOOM_DIR)/p_map.c \
	$(DOOM_DIR)/p_maputl.c \
	$(DOOM_DIR)/p_mobj.c \
	$(DOOM_DIR)/p_plats.c \
	$(DOOM_DIR)/p_pspr.c \
	$(DOOM_DIR)/p_saveg.c \
	$(DOOM_DIR)/p_setup.c \
	$(DOOM_DIR)/p_sight.c \
	$(DOOM_DIR)/p_spec.c \
	$(DOOM_DIR)/p_switch.c \
	$(DOOM_DIR)/p_telept.c \
	$(DOOM_DIR)/p_tick.c \
	$(DOOM_DIR)/p_user.c \
	$(DOOM_DIR)/r_bsp.c \
	$(DOOM_DIR)/r_data.c \
	$(DOOM_DIR)/r_draw.c \
	$(DOOM_DIR)/r_main.c \
	$(DOOM_DIR)/r_plane.c \
	$(DOOM_DIR)/r_segs.c \
	$(DOOM_DIR)/r_sky.c \
	$(DOOM_DIR)/r_things.c \
	$(DOOM_DIR)/s_sound.c \
	$(DOOM_DIR)/sha1.c \
	$(DOOM_DIR)/sounds.c \
	$(DOOM_DIR)/st_lib.c \
	$(DOOM_DIR)/st_stuff.c \
	$(DOOM_DIR)/statdump.c \
	$(DOOM_DIR)/tables.c \
	$(DOOM_DIR)/v_video.c \
	$(DOOM_DIR)/w_checksum.c \
	$(DOOM_DIR)/w_file.c \
	$(DOOM_DIR)/w_file_stdc.c \
	$(DOOM_DIR)/w_main.c \
	$(DOOM_DIR)/w_wad.c \
	$(DOOM_DIR)/wi_stuff.c \
	$(DOOM_DIR)/z_zone.c \
	$(DOOM_DIR)/doomgeneric_zenos.c

OBJS := $(DOOM_CORE_SRCS:.c=.o)

COMMON_CPPFLAGS := \
	--sysroot=$(MLIBC_SYSROOT) \
	-isystem $(MLIBC_USR)/include \
	-I ../.. \
	-I $(DOOM_DIR)

COMMON_CFLAGS := \
	-m64 -O2 -fPIE \
	-fno-stack-protector -mno-red-zone -nostdlib \
	-DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE \
	-Wno-implicit-function-declaration \
	-Wno-unused-function \
	-Wno-unused-variable \
	-Wno-deprecated-non-prototype \
	-Wno-unused-but-set-variable \
	-Wno-pointer-sign \
	-Wno-int-conversion

COMMON_LDFLAGS := \
	-m elf_x86_64 -pie -e _start \
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
		$(MLIBC_USR)/lib/crtn.o -o $(APP).elf

install: $(APP)
	$(FATMAN) $(VHD) mkdir /bin || true
	$(FATMAN) $(VHD) delete $(INSTALL_PATH) || true
	$(FATMAN) $(VHD) import $(APP).elf $(INSTALL_PATH)
	$(FATMAN) $(VHD) import doom.wad doom.wad

clean:
	rm -f $(OBJS) $(APP).elf
