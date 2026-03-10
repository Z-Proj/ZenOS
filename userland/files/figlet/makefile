APP        = figlet
CRT0_SRC   = ../../crt0.asm
NEWLIB_INC = ../../libs/include
NEWLIB_LIB = ../../libs
LDSCRIPT   = ../../userelf.ld
FATMAN     = ../../../fat_man
VHD        = ../../../ZenOS.vhd

CFLAGS = -m64 -ffreestanding -fno-stack-protector -mno-red-zone \
         -nostdlib -fno-pic -O1 \
         -isystem $(NEWLIB_INC) -I $(NEWLIB_LIB) -I ../.. -I . \
         -Wno-implicit-function-declaration \
         -Wno-deprecated-non-prototype \
         -Wno-unused-variable \
         -Wno-unused-but-set-variable

all: crt0.o figlet.o crc.o getopt.o $(APP)

crt0.o: $(CRT0_SRC)
	nasm -f elf64 $(CRT0_SRC) -o crt0.o

figlet.o: figlet.c fonts_embedded.c
	clang $(CFLAGS) -c figlet.c -o figlet.o

crc.o: crc.c
	clang $(CFLAGS) -c crc.c -o crc.o

getopt.o: getopt.c
	clang $(CFLAGS) -c getopt.c -o getopt.o

$(APP): crt0.o figlet.o crc.o getopt.o
	ld.lld -m elf_x86_64 -e _start -T $(LDSCRIPT) \
		crt0.o figlet.o crc.o getopt.o -L$(NEWLIB_LIB) -lc -lm -o $(APP)

install: $(APP)
	$(FATMAN) $(VHD) delete /bin/$(APP) || true
	$(FATMAN) $(VHD) import $(APP) /bin/$(APP)

clean:
	rm -f crt0.o figlet.o crc.o getopt.o $(APP)
