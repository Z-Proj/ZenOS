APP        = cc
CRT0_SRC   = ../../crt0.asm
NEWLIB_INC = ../../libs/include
NEWLIB_LIB = ../../libs
LDSCRIPT   = ../../userelf.ld
FATMAN     = ../../../fat_man
VHD        = ../../../ZenOS.vhd

CLANG_RES := $(shell clang -print-resource-dir 2>/dev/null)/include
GCC_RES   := $(shell gcc -print-file-name=include 2>/dev/null)
COMPILER_INC := $(if $(wildcard $(CLANG_RES)/stdarg.h),$(CLANG_RES),$(GCC_RES))

CFLAGS = -m64 -ffreestanding -fno-stack-protector -mno-red-zone \
         -nostdlib -nostdinc -fPIC -O1 \
         -I $(COMPILER_INC) \
         -I $(NEWLIB_INC) -I $(NEWLIB_LIB) -I ../.. -I . \
         -DONE_SOURCE=1 \
         -DTCC_TARGET_X86_64=1 \
         -DTCC_TARGET_ZENOS=1 \
         -DCONFIG_TCC_STATIC=1 \
         -Wno-unused-function -Wno-unused-variable \
         -Wno-implicit-function-declaration

CC_HOST := $(shell which clang 2>/dev/null || which gcc)

all: crt0.o $(APP).o $(APP)

crt0.o: $(CRT0_SRC)
	nasm -f elf64 $(CRT0_SRC) -o crt0.o

$(APP).o: tcc.c
	$(CC_HOST) $(CFLAGS) -c tcc.c -o $(APP).o

$(APP): crt0.o $(APP).o
	ld.lld -m elf_x86_64 -e _start -T $(LDSCRIPT) \
		crt0.o $(APP).o -L$(NEWLIB_LIB) -lc -lm -o $(APP)

install: $(APP)
	$(FATMAN) $(VHD) delete /bin/$(APP) || true
	$(FATMAN) $(VHD) import $(APP) /bin/$(APP)
	$(FATMAN) $(VHD) mkdir /usr || true
	$(FATMAN) $(VHD) mkdir /usr/include || true
	$(FATMAN) $(VHD) mkdir /usr/include/sys || true
	$(FATMAN) $(VHD) mkdir /usr/include/machine || true
	$(FATMAN) $(VHD) mkdir /usr/include/ssp || true
	$(FATMAN) $(VHD) mkdir /usr/lib || true
	$(FATMAN) $(VHD) mkdir /lib || true
	$(FATMAN) $(VHD) mkdir /lib/tcc || true
	$(FATMAN) $(VHD) mkdir /lib/tcc/include || true
	$(FATMAN) $(VHD) import $(NEWLIB_LIB)/libc.a /usr/lib/libc.a
	$(FATMAN) $(VHD) import $(NEWLIB_LIB)/libm.a /usr/lib/libm.a
	$(FATMAN) $(VHD) import crt0.o /usr/lib/crt0.o
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/_ansi.h /usr/include/_ansi.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/_newlib_version.h /usr/include/_newlib_version.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/alloca.h /usr/include/alloca.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/assert.h /usr/include/assert.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/ctype.h /usr/include/ctype.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/dirent.h /usr/include/dirent.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/errno.h /usr/include/errno.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/fcntl.h /usr/include/fcntl.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/libgen.h /usr/include/libgen.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/limits.h /usr/include/limits.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/math.h /usr/include/math.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/newlib.h /usr/include/newlib.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/setjmp.h /usr/include/setjmp.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/signal.h /usr/include/signal.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/stdint.h /usr/include/stdint.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/stdio.h /usr/include/stdio.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/stdlib.h /usr/include/stdlib.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/string.h /usr/include/string.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/strings.h /usr/include/strings.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/time.h /usr/include/time.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/unistd.h /usr/include/unistd.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/_default_types.h /usr/include/machine/_default_types.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/_endian.h /usr/include/machine/_endian.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/_time.h /usr/include/machine/_time.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/_types.h /usr/include/machine/_types.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/endian.h /usr/include/machine/endian.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/fastmath.h /usr/include/machine/fastmath.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/ieeefp.h /usr/include/machine/ieeefp.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/setjmp.h /usr/include/machine/setjmp.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/stdlib.h /usr/include/machine/stdlib.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/time.h /usr/include/machine/time.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/machine/types.h /usr/include/machine/types.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/ssp/ssp.h /usr/include/ssp/ssp.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/ssp/stdio.h /usr/include/ssp/stdio.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/ssp/stdlib.h /usr/include/ssp/stdlib.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/ssp/string.h /usr/include/ssp/string.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/ssp/strings.h /usr/include/ssp/strings.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/_default_fcntl.h /usr/include/sys/_default_fcntl.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/_intsup.h /usr/include/sys/_intsup.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/_locale.h /usr/include/sys/_locale.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/_pthreadtypes.h /usr/include/sys/_pthreadtypes.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/_sigset.h /usr/include/sys/_sigset.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/_stdint.h /usr/include/sys/_stdint.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/_timespec.h /usr/include/sys/_timespec.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/_timeval.h /usr/include/sys/_timeval.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/_types.h /usr/include/sys/_types.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/cdefs.h /usr/include/sys/cdefs.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/config.h /usr/include/sys/config.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/custom_file.h /usr/include/sys/custom_file.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/dirent.h /usr/include/sys/dirent.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/errno.h /usr/include/sys/errno.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/fcntl.h /usr/include/sys/fcntl.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/features.h /usr/include/sys/features.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/lock.h /usr/include/sys/lock.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/reent.h /usr/include/sys/reent.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/sched.h /usr/include/sys/sched.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/select.h /usr/include/sys/select.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/signal.h /usr/include/sys/signal.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/stat.h /usr/include/sys/stat.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/stdio.h /usr/include/sys/stdio.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/string.h /usr/include/sys/string.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/syslimits.h /usr/include/sys/syslimits.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/time.h /usr/include/sys/time.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/timespec.h /usr/include/sys/timespec.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/types.h /usr/include/sys/types.h || true
	$(FATMAN) $(VHD) import $(NEWLIB_INC)/sys/wait.h /usr/include/sys/wait.h || true
	$(FATMAN) $(VHD) import include/stddef.h      /lib/tcc/include/stddef.h      || true
	$(FATMAN) $(VHD) import include/stdarg.h      /lib/tcc/include/stdarg.h      || true
	$(FATMAN) $(VHD) import include/stdbool.h     /lib/tcc/include/stdbool.h     || true
	$(FATMAN) $(VHD) import include/float.h       /lib/tcc/include/float.h       || true
	$(FATMAN) $(VHD) import include/stdalign.h    /lib/tcc/include/stdalign.h    || true
	$(FATMAN) $(VHD) import include/varargs.h     /lib/tcc/include/varargs.h     || true
	$(FATMAN) $(VHD) import include/stdnoreturn.h /lib/tcc/include/stdnoreturn.h || true
	$(FATMAN) $(VHD) import include/stdatomic.h   /lib/tcc/include/stdatomic.h   || true
	$(FATMAN) $(VHD) import include/tgmath.h      /lib/tcc/include/tgmath.h      || true
	$(FATMAN) $(VHD) import include/tccdefs.h     /lib/tcc/include/tccdefs.h     || true

clean:
	rm -f crt0.o $(APP).o $(APP)
