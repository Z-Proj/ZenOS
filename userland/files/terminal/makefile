APP  = terminal
SRCS = terminal.c

USER_OPT = -O3
CPPFLAGS += -I ../02-libfont -I ../01-freetype/freetype/include -I ../01-freetype/freetype/include/freetype/config
CFLAGS += -flto -ffast-math -fvisibility=hidden -finline-functions -fmerge-all-constants \
	-fno-unwind-tables -fno-asynchronous-unwind-tables
LDFLAGS += -s
LDLIBS += $(MLIBC_USR)/lib/libfont.a $(MLIBC_USR)/lib/libfreetype.a
INSTALL_HOOK = \
	$(FATMAN) $(VHD) mkdir /lib || true; \
	$(FATMAN) $(VHD) mkdir /lib/fonts || true; \
	$(FATMAN) $(VHD) delete /lib/fonts/monospace.ttf || true; \
	$(FATMAN) $(VHD) import monospace.ttf /lib/fonts/monospace.ttf

include ../../mlibc_app.mk
