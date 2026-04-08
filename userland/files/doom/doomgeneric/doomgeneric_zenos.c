#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <linux/input.h>

#define LINUX_KEY_ENTER KEY_ENTER
#define LINUX_KEY_ESC KEY_ESC
#define LINUX_KEY_BACKSPACE KEY_BACKSPACE
#define LINUX_KEY_TAB KEY_TAB
#define LINUX_KEY_LEFT KEY_LEFT
#define LINUX_KEY_RIGHT KEY_RIGHT
#define LINUX_KEY_UP KEY_UP
#define LINUX_KEY_DOWN KEY_DOWN
#define LINUX_KEY_SPACE KEY_SPACE
#define LINUX_KEY_F1 KEY_F1
#define LINUX_KEY_F2 KEY_F2
#define LINUX_KEY_F3 KEY_F3
#define LINUX_KEY_F4 KEY_F4
#define LINUX_KEY_F5 KEY_F5
#define LINUX_KEY_F6 KEY_F6
#define LINUX_KEY_F7 KEY_F7
#define LINUX_KEY_F8 KEY_F8
#define LINUX_KEY_F9 KEY_F9
#define LINUX_KEY_F10 KEY_F10
#define LINUX_KEY_F11 KEY_F11
#define LINUX_KEY_F12 KEY_F12

#undef KEY_ENTER
#undef KEY_TAB
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#undef KEY_BACKSPACE
#undef KEY_MINUS
#undef KEY_CAPSLOCK

#include "doomkeys.h"
#include "doomgeneric.h"

#include "../../../include/harp_api.h"

#define KEYQUEUE_SIZE 64

static harp_window_t *s_win = NULL;

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int   s_KeyQueueWriteIndex = 0;
static unsigned int   s_KeyQueueReadIndex  = 0;

static void addKeyToQueue(int pressed, int harpKey, uint32_t mods);

static void addRawKeyToQueue(int pressed, unsigned char doomKey)
{
    unsigned short keyData;

    if (doomKey == 0)
        return;

    keyData = (unsigned short)((pressed << 8) | doomKey);
    s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
    s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
}

static void pumpEvents(void)
{
    harp_event_t ev;
    while (harp_poll_event(s_win, &ev)) {
        if (ev.type == HARP_EVENT_CLOSE_REQ) {
            harp_close(s_win);
            _exit(0);
        } else if (ev.type == HARP_EVENT_KEY && ev.code != 0) {
            int pressed = (ev.value != 0);
            int hk = ev.key;

            if (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL) {
                addRawKeyToQueue(pressed, KEY_FIRE);
            } else if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                addRawKeyToQueue(pressed, KEY_RSHIFT);
            } else if (ev.code == KEY_LEFTALT || ev.code == KEY_RIGHTALT) {
                addRawKeyToQueue(pressed, KEY_LALT);
            } else if (ev.code == LINUX_KEY_ENTER) {
                addRawKeyToQueue(pressed, KEY_ENTER);
            } else if (ev.code == LINUX_KEY_ESC) {
                addRawKeyToQueue(pressed, KEY_ESCAPE);
            } else if (ev.code == LINUX_KEY_BACKSPACE) {
                addRawKeyToQueue(pressed, KEY_BACKSPACE);
            } else if (ev.code == LINUX_KEY_TAB) {
                addRawKeyToQueue(pressed, KEY_TAB);
            } else if (ev.code == LINUX_KEY_LEFT) {
                addRawKeyToQueue(pressed, KEY_LEFTARROW);
            } else if (ev.code == LINUX_KEY_RIGHT) {
                addRawKeyToQueue(pressed, KEY_RIGHTARROW);
            } else if (ev.code == LINUX_KEY_UP) {
                addRawKeyToQueue(pressed, KEY_UPARROW);
            } else if (ev.code == LINUX_KEY_DOWN) {
                addRawKeyToQueue(pressed, KEY_DOWNARROW);
            } else if (ev.code == LINUX_KEY_SPACE) {
                addRawKeyToQueue(pressed, KEY_USE);
            } else if (ev.code == LINUX_KEY_F1) {
                addRawKeyToQueue(pressed, KEY_F1);
            } else if (ev.code == LINUX_KEY_F2) {
                addRawKeyToQueue(pressed, KEY_F2);
            } else if (ev.code == LINUX_KEY_F3) {
                addRawKeyToQueue(pressed, KEY_F3);
            } else if (ev.code == LINUX_KEY_F4) {
                addRawKeyToQueue(pressed, KEY_F4);
            } else if (ev.code == LINUX_KEY_F5) {
                addRawKeyToQueue(pressed, KEY_F5);
            } else if (ev.code == LINUX_KEY_F6) {
                addRawKeyToQueue(pressed, KEY_F6);
            } else if (ev.code == LINUX_KEY_F7) {
                addRawKeyToQueue(pressed, KEY_F7);
            } else if (ev.code == LINUX_KEY_F8) {
                addRawKeyToQueue(pressed, KEY_F8);
            } else if (ev.code == LINUX_KEY_F9) {
                addRawKeyToQueue(pressed, KEY_F9);
            } else if (ev.code == LINUX_KEY_F10) {
                addRawKeyToQueue(pressed, KEY_F10);
            } else if (ev.code == LINUX_KEY_F11) {
                addRawKeyToQueue(pressed, KEY_F11);
            } else if (ev.code == LINUX_KEY_F12) {
                addRawKeyToQueue(pressed, KEY_F12);
            } else if (hk != 0) {
                addKeyToQueue(pressed, hk, ev.modifiers);
            }
        }
    }
}

static unsigned char convertToDoomKey(int harpKey, uint32_t mods)
{
    switch (harpKey) {
    case '\n': case '\r':   return KEY_ENTER;
    case 27:                return KEY_ESCAPE;
    case KEY_ARROW_LEFT:    return KEY_LEFTARROW;
    case KEY_ARROW_RIGHT:   return KEY_RIGHTARROW;
    case KEY_ARROW_UP:      return KEY_UPARROW;
    case KEY_ARROW_DOWN:    return KEY_DOWNARROW;
    case ' ':               return KEY_USE;
    case '\b':              return KEY_BACKSPACE;
    default:
        if (harpKey >= 0x20 && harpKey < 0x7F)
            return (unsigned char)harpKey;
        return 0;
    }
}

static void addKeyToQueue(int pressed, int harpKey, uint32_t mods)
{
    unsigned char dk = convertToDoomKey(harpKey, mods);
    if (dk == 0) return;
    unsigned short keyData = (unsigned short)((pressed << 8) | dk);
    s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
    s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
}

void DG_Init()
{
    s_win = harp_open("DOOM", 40, 30, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    if (!s_win) {
        write(2, "DG_Init: harp_open failed\n", 26);
        _exit(1);
    }
    memset(s_win->buf, 0, (size_t)(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4));
}

void DG_DrawFrame()
{
    if (!s_win) return;

    uint32_t *src = DG_ScreenBuffer;
    uint32_t *dst = s_win->buf;
    int n = DOOMGENERIC_RESX * DOOMGENERIC_RESY;
    for (int i = 0; i < n; i++) {
        uint32_t p = src[i];
        uint8_t r = (p >> 16) & 0xFF;
        uint8_t g = (p >>  8) & 0xFF;
        uint8_t b =  p        & 0xFF;
        dst[i] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    pumpEvents();

    harp_flush(s_win);
}

void DG_SleepMs(uint32_t ms)
{
    uint32_t start = DG_GetTicksMs();
    do {
        pumpEvents();
        sched_yield();
    } while ((DG_GetTicksMs() - start) < ms);
}

uint32_t DG_GetTicksMs()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return (uint32_t)(tp.tv_sec * 1000 + tp.tv_nsec / 1000000);
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    pumpEvents();
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
        return 0;
    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;
    *pressed = keyData >> 8;
    *doomKey  = keyData & 0xFF;
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    if (s_win)
        harp_retitle(s_win, title);
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    while (1) {
        doomgeneric_Tick();
    }
    return 0;
}
