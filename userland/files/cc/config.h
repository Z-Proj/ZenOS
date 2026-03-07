#define TCC_VERSION "0.9.28rc"

#define CC_NAME CC_gcc
#define GCC_MAJOR 15
#define GCC_MINOR 2

#if !(TCC_TARGET_I386 || TCC_TARGET_X86_64 || TCC_TARGET_ARM || TCC_TARGET_ARM64 || TCC_TARGET_RISCV64 || TCC_TARGET_C67)
#define TCC_TARGET_X86_64 1
#define CONFIG_LDDIR "lib"
#endif

#define TCC_TARGET_ZENOS 1
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_SEMLOCK 0
#undef CONFIG_TCC_BACKTRACE
#undef CONFIG_TCC_BCHECK

#ifndef CONFIG_TCCDIR
#define CONFIG_TCCDIR "/mnt/drv0/lib/tcc"
#endif

#define CONFIG_TCC_PREDEFS 1
#define CONFIG_TCC_SYSINCLUDEPATHS "{B}/include:/mnt/drv0/usr/include"
#define CONFIG_TCC_LIBPATHS "/mnt/drv0/usr/lib"
#define CONFIG_TCC_CRTPREFIX "/mnt/drv0/usr/lib"

#define TCC_LIBTCC1 ""
