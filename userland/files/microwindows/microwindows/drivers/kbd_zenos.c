#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "device.h"

static inline uint64_t _syscall0(uint64_t num)
{
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num)
                     : "rcx", "r11", "rdi", "rsi", "rdx", "r10", "r8", "r9", "cc", "memory");
    return ret;
}

#define SYSCALL_GETKEY  3
#define KEY_ARROW_UP    0x01
#define KEY_ARROW_DOWN  0x02
#define KEY_ARROW_LEFT  0x03
#define KEY_ARROW_RIGHT 0x04

static int  zenos_kbd_open(KBDDEVICE *pkd);
static void zenos_kbd_close(void);
static void zenos_kbd_getmodinfo(MWKEYMOD *modifiers, MWKEYMOD *curmodifiers);
static int  zenos_kbd_read(MWKEY *buf, MWKEYMOD *modifiers, MWSCANCODE *scancode);

KBDDEVICE kbddev = {
    zenos_kbd_open,
    zenos_kbd_close,
    zenos_kbd_getmodinfo,
    zenos_kbd_read,
    NULL
};

static int zenos_kbd_open(KBDDEVICE *pkd)
{
    (void)pkd;
    return 1;
}

static void zenos_kbd_close(void) {}

static void zenos_kbd_getmodinfo(MWKEYMOD *modifiers, MWKEYMOD *curmodifiers)
{
    if (modifiers)    *modifiers    = 0;
    if (curmodifiers) *curmodifiers = 0;
}

static int zenos_kbd_read(MWKEY *buf, MWKEYMOD *modifiers, MWSCANCODE *scancode)
{
    int c = (int)(int64_t)_syscall0(SYSCALL_GETKEY);
    if (c <= 0)
        return 0;

    *scancode  = 0;
    *modifiers = 0;

    switch (c) {
    case KEY_ARROW_UP:    *buf = MWKEY_UP;       break;
    case KEY_ARROW_DOWN:  *buf = MWKEY_DOWN;     break;
    case KEY_ARROW_LEFT:  *buf = MWKEY_LEFT;     break;
    case KEY_ARROW_RIGHT: *buf = MWKEY_RIGHT;    break;
    case '\n':            *buf = MWKEY_ENTER;    break;
    case '\t':            *buf = MWKEY_TAB;      break;
    case 8:               *buf = MWKEY_BACKSPACE; break;
    case 27:              *buf = MWKEY_ESCAPE;   break;
    default:
        *buf = (c >= 32 && c <= 126) ? (MWKEY)c : MWKEY_UNKNOWN;
        break;
    }

    return 1;
}
