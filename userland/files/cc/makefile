APP = cc
SRCS = tcc.c

CLANG_RES := $(shell clang -print-resource-dir 2>/dev/null)/include
GCC_RES := $(shell gcc -print-file-name=include 2>/dev/null)
COMPILER_INC := $(if $(wildcard $(CLANG_RES)/stdarg.h),$(CLANG_RES),$(GCC_RES))
CC_HOST := $(shell which clang 2>/dev/null || which gcc)

CC := $(CC_HOST)
CFLAGS += -nostdinc -I $(COMPILER_INC) -DONE_SOURCE=1 -DTCC_TARGET_X86_64=1 \
	-DTCC_TARGET_ZENOS=1 -DCONFIG_TCC_STATIC=1 -Wno-unused-function \
	-Wno-unused-variable -Wno-implicit-function-declaration
INSTALL_HOOK = $(FATMAN) $(VHD) mkdir /lib || true; \
	$(FATMAN) $(VHD) mkdir /lib/tcc || true; \
	$(FATMAN) $(VHD) mkdir /lib/tcc/include || true; \
	for hdr in include/*; do \
		$(FATMAN) $(VHD) import $$hdr /lib/tcc/include/$$(basename $$hdr) || true; \
	done

include ../../mlibc_app.mk
