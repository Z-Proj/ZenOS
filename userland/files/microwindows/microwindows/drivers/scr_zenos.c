#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "device.h"
#include "genfont.h"
#include "genmem.h"
#include "fb.h"

typedef struct {
    uint64_t addr;
    uint64_t width;
    uint64_t height;
    uint8_t  bpp;
    uint32_t pitch;
} fb_info_t;

static inline uint64_t _syscall1(uint64_t num, uint64_t a1)
{
    uint64_t ret;
    register uint64_t rdi __asm__("rdi") = a1;
    __asm__ volatile("syscall" : "=a"(ret), "+D"(rdi) : "a"(num)
                     : "rcx", "r11", "rsi", "rdx", "r10", "r8", "r9", "cc", "memory");
    return ret;
}

#define SYSCALL_GET_FRAMEBUFFER 45

static PSD  zenos_open(PSD psd);
static void zenos_close(PSD psd);
static void zenos_setpalette(PSD psd, int first, int count, MWPALENTRY *palette);

SCREENDEVICE scrdev = {
    0, 0, 0, 0, 0, 0, 0, NULL, 0, NULL, 0, 0, 0, 0, 0, 0,
    gen_fonts,
    zenos_open,
    zenos_close,
    zenos_setpalette,
    gen_getscreeninfo,
    gen_allocatememgc,
    gen_mapmemgc,
    gen_freememgc,
    gen_setportrait,
    NULL,
    NULL
};

static PSD zenos_open(PSD psd)
{
    fb_info_t info;
    PSUBDRIVER subdriver;

    if ((int64_t)_syscall1(SYSCALL_GET_FRAMEBUFFER, (uint64_t)&info) < 0) {
        EPRINTF("zenos: SYSCALL_GET_FRAMEBUFFER failed\n");
        return NULL;
    }

    psd->portrait  = MWPORTRAIT_NONE;
    psd->xres      = psd->xvirtres = (int)info.width;
    psd->yres      = psd->yvirtres = (int)info.height;
    psd->bpp       = info.bpp;
    psd->pitch     = info.pitch;
    psd->size      = info.pitch * (uint32_t)info.height;
    psd->planes    = 1;
    psd->ncolors   = 1 << 24;
    psd->flags     = PSF_SCREEN;
    psd->pixtype   = MWPF_TRUECOLORARGB;
    psd->addr      = (void *)info.addr;

    psd->data_format = set_data_format(psd);

    subdriver = select_fb_subdriver(psd);
    if (!subdriver) {
        EPRINTF("zenos: no subdriver for bpp %d\n", psd->bpp);
        return NULL;
    }

    set_subdriver(psd, subdriver);
    return psd;
}

static void zenos_close(PSD psd)
{
    (void)psd;
}

static void zenos_setpalette(PSD psd, int first, int count, MWPALENTRY *palette)
{
    (void)psd; (void)first; (void)count; (void)palette;
}
