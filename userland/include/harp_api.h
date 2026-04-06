#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include "../userlib.h"

#define WM_SOCK "wm:events"

#define WM_MSG_REGISTER   1
#define WM_MSG_DIRTY      2
#define WM_MSG_UNREGISTER 3
#define WM_MSG_RETITLE    4
#define WM_MSG_RESIZE_REQ 5

#define HARP_EVENT_FOCUS        1
#define HARP_EVENT_BLUR         2
#define HARP_EVENT_MOUSE_MOVE   3
#define HARP_EVENT_MOUSE_BUTTON 4
#define HARP_EVENT_KEY          5
#define HARP_EVENT_RESIZE       6
#define HARP_EVENT_CLOSE_REQ    7
#define HARP_EVENT_EXPOSE       8

#define HARP_MOD_SHIFT 0x01
#define HARP_MOD_CTRL  0x02
#define HARP_MOD_ALT   0x04
#define HARP_MOD_CAPS  0x08

#pragma pack(push, 1)
typedef struct {
    uint8_t  type;
    uint32_t pid;
    int32_t  x, y, w, h;
    char     title[64];
} wm_msg_t;

typedef struct {
    uint16_t type;
    uint16_t code;
    int32_t  value;
    int32_t  x, y;
    uint32_t modifiers;
    int32_t  key;
    int32_t  w, h;
} harp_event_t;
#pragma pack(pop)

typedef struct {
    int32_t   x, y, w, h;
    uint32_t *buf;
    uint32_t  pid;
    char      shmname[32];
    char      evname[32];
    socket_file_t *sock;
    socket_file_t *event_sock;
    int        focused;
} harp_window_t;

static inline harp_window_t *harp_open(const char *title, int x, int y, int w, int h)
{
    for (int retry = 0; retry < 3000; retry++) {
        if (socket_exists(WM_SOCK)) break;
        zen_sleep_ms(10);
        if (retry == 2999) return NULL;
    }

    harp_window_t *gw = (harp_window_t *)malloc(sizeof(harp_window_t));
    if (!gw) return NULL;
    memset(gw, 0, sizeof(*gw));
    gw->x = x; gw->y = y; gw->w = w; gw->h = h;
    gw->pid = (uint32_t)getpid();

    snprintf(gw->shmname, sizeof(gw->shmname), "wm:shm_%u", gw->pid);
    snprintf(gw->evname,  sizeof(gw->evname),  "wm:ev_%u",  gw->pid);

    shm_info_t si;
    if (zen_shm_create(gw->shmname, (size_t)(w * h * 4), &si) < 0) {
        free(gw); return NULL;
    }
    gw->buf = (uint32_t *)si.addr;
    memset(gw->buf, 0, (size_t)(w * h * 4));

    socket_delete(gw->evname);
    if (socket_create(gw->evname) < 0) {
        zen_shm_close(gw->shmname); free(gw); return NULL;
    }
    if (socket_open(gw->evname, &gw->event_sock) < 0 || !gw->event_sock) {
        socket_delete(gw->evname); zen_shm_close(gw->shmname); free(gw); return NULL;
    }

    for (int retry = 0; retry < 3000; retry++) {
        if (socket_open(WM_SOCK, &gw->sock) == 0 && gw->sock) break;
        zen_sleep_ms(10);
        if (retry == 2999) {
            socket_close(gw->event_sock); socket_delete(gw->evname);
            zen_shm_close(gw->shmname); free(gw); return NULL;
        }
    }

    wm_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WM_MSG_REGISTER;
    msg.pid  = gw->pid;
    msg.x = x; msg.y = y; msg.w = w; msg.h = h;
    strncpy(msg.title, title, sizeof(msg.title) - 1);
    socket_write(gw->sock, &msg, sizeof(msg));

    return gw;
}

static inline int harp_poll_event(harp_window_t *gw, harp_event_t *ev)
{
    if (!gw || !gw->event_sock || !ev) return 0;
    if (socket_available(gw->event_sock) < sizeof(*ev)) return 0;
    uint32_t got = 0;
    if (socket_read(gw->event_sock, ev, sizeof(*ev), &got) < 0 || got < sizeof(*ev)) return 0;
    if      (ev->type == HARP_EVENT_FOCUS) gw->focused = 1;
    else if (ev->type == HARP_EVENT_BLUR)  gw->focused = 0;
    return 1;
}

static inline void harp_flush(harp_window_t *gw)
{
    if (!gw || !gw->sock) return;
    wm_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WM_MSG_DIRTY;
    msg.pid  = gw->pid;
    socket_write(gw->sock, &msg, sizeof(msg));
}

static inline void harp_retitle(harp_window_t *gw, const char *title)
{
    if (!gw || !gw->sock) return;
    wm_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WM_MSG_RETITLE;
    msg.pid  = gw->pid;
    strncpy(msg.title, title, sizeof(msg.title) - 1);
    socket_write(gw->sock, &msg, sizeof(msg));
}

static inline void harp_close(harp_window_t *gw)
{
    if (!gw) return;
    if (gw->sock) {
        wm_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = WM_MSG_UNREGISTER;
        msg.pid  = gw->pid;
        socket_write(gw->sock, &msg, sizeof(msg));
        socket_close(gw->sock);
    }
    if (gw->event_sock) socket_close(gw->event_sock);
    socket_delete(gw->evname);
    zen_shm_close(gw->shmname);
    free(gw);
}

static inline void harp_pixel(harp_window_t *gw, int x, int y, uint32_t c)
{
    if (!gw || x < 0 || y < 0 || x >= gw->w || y >= gw->h) return;
    gw->buf[y * gw->w + x] = c;
}

static inline void harp_fill(harp_window_t *gw, uint32_t c)
{
    if (!gw) return;
    for (int i = 0; i < gw->w * gw->h; i++) gw->buf[i] = c;
}

static inline void harp_rect(harp_window_t *gw, int x, int y, int w, int h, uint32_t c)
{
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + w > gw->w ? gw->w : x + w;
    int y2 = y + h > gw->h ? gw->h : y + h;
    for (int row = y1; row < y2; row++) {
        uint32_t *line = gw->buf + row * gw->w;
        for (int col = x1; col < x2; col++) line[col] = c;
    }
}

static inline void harp_hline(harp_window_t *gw, int x, int y, int w, uint32_t c)
{
    harp_rect(gw, x, y, w, 1, c);
}

static inline void harp_vline(harp_window_t *gw, int x, int y, int h, uint32_t c)
{
    harp_rect(gw, x, y, 1, h, c);
}

static inline void harp_rect_outline(harp_window_t *gw, int x, int y, int w, int h, uint32_t c)
{
    harp_hline(gw, x, y, w, c);
    harp_hline(gw, x, y + h - 1, w, c);
    harp_vline(gw, x, y, h, c);
    harp_vline(gw, x + w - 1, y, h, c);
}
