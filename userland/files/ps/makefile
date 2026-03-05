APP        = ps
CRT0_SRC   = ../../crt0.asm
NEWLIB_INC = ../../libs/include
NEWLIB_LIB = ../../libs
LDSCRIPT   = ../../userelf.ld
FATMAN     = ../../../fat_man
VHD        = ../../../ZenOS.vhd

CFLAGS = -m64 -ffreestanding -fno-stack-protector -mno-red-zone \
         -nostdlib -fPIC -O1 \
         -isystem $(NEWLIB_INC) -I $(NEWLIB_LIB) -I ../.. -I .

all: crt0.o $(APP).o $(APP)

crt0.o: $(CRT0_SRC)
	nasm -f elf64 $(CRT0_SRC) -o crt0.o

$(APP).o: $(APP).c
	clang $(CFLAGS) -c $(APP).c -o $(APP).o

$(APP): crt0.o $(APP).o
	ld.lld -m elf_x86_64 -e _start -T $(LDSCRIPT) \
		crt0.o $(APP).o -L$(NEWLIB_LIB) -lc -lm -o $(APP)

install: $(APP)
	$(FATMAN) $(VHD) delete /bin/$(APP) || true
	$(FATMAN) $(VHD) import $(APP) /bin/$(APP)

clean:
	rm -f crt0.o $(APP).o $(APP)
