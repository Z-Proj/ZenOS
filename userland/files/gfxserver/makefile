APP        = gfxserver
CRT0_SRC   = ../../crt0.asm
NEWLIB_INC = ../../libs/include
NEWLIB_LIB = ../../libs
LDSCRIPT   = ../../userelf.ld
FATMAN     = ../../../fat_man
VHD        = ../../../ZenOS.vhd

CFLAGS = -m64 -ffreestanding -fno-stack-protector -mno-red-zone \
         -nostdlib -fno-pic -O1 \
         -isystem $(NEWLIB_INC) -I $(NEWLIB_LIB) -I ../.. -I .

all: crt0.o FreeSansB.o $(APP).o $(APP)

FreeSansB.o: FreeSansB.sfn
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 FreeSansB.sfn FreeSansB.o

crt0.o: $(CRT0_SRC)
	nasm -f elf64 $(CRT0_SRC) -o crt0.o

$(APP).o: $(APP).c
	clang $(CFLAGS) -c $(APP).c -o $(APP).o

$(APP): crt0.o $(APP).o FreeSansB.o
	ld.lld -m elf_x86_64 -e _start -T $(LDSCRIPT) \
		crt0.o $(APP).o FreeSansB.o -L$(NEWLIB_LIB) -lc -lm -o $(APP)

install: $(APP)
	$(FATMAN) $(VHD) delete /bin/$(APP) || true
	$(FATMAN) $(VHD) import $(APP) /bin/$(APP)

clean:
	rm -f crt0.o $(APP).o FreeSansB.o $(APP)
