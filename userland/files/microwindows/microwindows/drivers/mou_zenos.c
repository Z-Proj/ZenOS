#include <stdio.h>
#include <stdint.h>
#include "device.h"

static inline uint64_t _syscall0(uint64_t num)
{
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num)
                     : "rcx", "r11", "rdi", "rsi", "rdx", "r10", "r8", "r9", "cc", "memory");
    return ret;
}

#define SYSCALL_MOUSE_X   5
#define SYSCALL_MOUSE_Y   6
#define SYSCALL_MOUSE_BTN 7

static int  zenos_mou_open(MOUSEDEVICE *pmd);
static void zenos_mou_close(void);
static int  zenos_mou_getbtninfo(void);
static void zenos_mou_getdefaccel(int *pscale, int *pthresh);
static int  zenos_mou_read(MWCOORD *dx, MWCOORD *dy, MWCOORD *dz, int *bp);

MOUSEDEVICE mousedev = {
    zenos_mou_open,
    zenos_mou_close,
    zenos_mou_getbtninfo,
    zenos_mou_getdefaccel,
    zenos_mou_read,
    NULL,
    MOUSE_NORMAL
};

static uint32_t last_x = 0, last_y = 0;
static uint8_t  last_btn = 0;

static int zenos_mou_open(MOUSEDEVICE *pmd)
{
    (void)pmd;
    last_x   = (uint32_t)_syscall0(SYSCALL_MOUSE_X);
    last_y   = (uint32_t)_syscall0(SYSCALL_MOUSE_Y);
    last_btn = (uint8_t)_syscall0(SYSCALL_MOUSE_BTN);
    return 1;
}

static void zenos_mou_close(void) {}

static int zenos_mou_getbtninfo(void)
{
    return MWBUTTON_L | MWBUTTON_M | MWBUTTON_R;
}

static void zenos_mou_getdefaccel(int *pscale, int *pthresh)
{
    *pscale  = 3;
    *pthresh = 5;
}

static int zenos_mou_read(MWCOORD *dx, MWCOORD *dy, MWCOORD *dz, int *bp)
{
    uint32_t cx  = (uint32_t)_syscall0(SYSCALL_MOUSE_X);
    uint32_t cy  = (uint32_t)_syscall0(SYSCALL_MOUSE_Y);
    uint8_t  btn = (uint8_t)_syscall0(SYSCALL_MOUSE_BTN);

    *dx = (MWCOORD)((int32_t)cx - (int32_t)last_x);
    *dy = (MWCOORD)((int32_t)cy - (int32_t)last_y);
    *dz = 0;

    switch (btn) {
    case 1:  *bp = MWBUTTON_L; break;
    case 2:  *bp = MWBUTTON_M; break;
    case 3:  *bp = MWBUTTON_R; break;
    default: *bp = 0;          break;
    }

    int changed = (*dx != 0 || *dy != 0 || btn != last_btn);
    last_x   = cx;
    last_y   = cy;
    last_btn = btn;

    return changed ? 1 : 0;
}
