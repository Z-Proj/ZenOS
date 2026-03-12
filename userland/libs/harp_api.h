#ifndef HARP_H
#define HARP_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "../userlib.h"
#ifndef HARP_H
#define HARP_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "../../userlib.h"

#define WM_SOCK         "wm:events"
#define WM_MSG_REGISTER   1
#define WM_MSG_DIRTY      2
#define WM_MSG_UNREGISTER 3

typedef struct {
    uint8_t  type;
    uint32_t pid;
    int32_t  x, y, w, h;
    char     title[64];
} wm_msg_t;

typedef struct {
    int32_t   x, y, w, h;
    uint32_t *buf;
    uint32_t  pid;
    char      shmname[32];
    socket_file_t *sock;
} harp_window_t;

static inline harp_window_t *harp_open(const char *title, int x, int y, int w, int h)
{
    zen_log("harp_open: enter", 1, 0);
    if (!socket_exists(WM_SOCK)) return NULL;
    zen_log("harp_open: socket exists", 1, 0);

    harp_window_t *gw = NULL;
    for (int retry = 0; retry < 10 && !gw; retry++) {
        gw = (harp_window_t *)malloc(sizeof(harp_window_t));
        if (!gw) zen_sleep_ms(10);
    }
    if (!gw) return NULL;
    zen_log("harp_open: malloc ok gw=%p", 1, 0, gw);
    memset(gw, 0, sizeof(*gw));

    gw->x   = x;
    gw->y   = y;
    gw->w   = w;
    gw->h   = h;
    gw->pid = (uint32_t)getpid();
    zen_log("harp_open: pid=%u", 1, 0, gw->pid);

    snprintf(gw->shmname, sizeof(gw->shmname), "wm:shm_%u", gw->pid);
    zen_log("harp_open: shm_create start size=%d", 1, 0, w * h * 4);
    shm_info_t si;
    if (zen_shm_create(gw->shmname, (size_t)(w * h * 4), &si) < 0) {
        zen_log("harp_open: shm_create failed", 2, 1);
        free(gw);
        return NULL;
    }
    zen_log("harp_open: shm ok addr=%p", 1, 0, (void*)si.addr);
    gw->buf = (uint32_t *)si.addr;
    memset(gw->buf, 0, (size_t)(w * h * 4));
    zen_log("harp_open: buf zeroed", 1, 0);

    zen_log("harp_open: socket_open", 1, 0);
    socket_open(WM_SOCK, &gw->sock);
    zen_log("harp_open: sock=%p", 1, 0, gw->sock);

    wm_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WM_MSG_REGISTER;
    msg.pid  = gw->pid;
    msg.x    = x;
    msg.y    = y;
    msg.w    = w;
    msg.h    = h;
    strncpy(msg.title, title, 63);
    zen_log("harp_open: socket_write", 1, 0);
    socket_write(gw->sock, &msg, sizeof(msg));
    zen_log("harp_open: done", 1, 0);

    return gw;
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
    zen_shm_close(gw->shmname);
    free(gw);
}

static inline void harp_pixel(harp_window_t *gw, int x, int y, uint32_t c)
{
    if (!gw || x < 0 || y < 0 || x >= gw->w || y >= gw->h) return;
    gw->buf[y * gw->w + x] = c;
}

static inline void harp_rect(harp_window_t *gw, int x, int y, int w, int h, uint32_t c)
{
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            harp_pixel(gw, col, row, c);
}

static inline void harp_hline(harp_window_t *gw, int x, int y, int w, uint32_t c)
{
    for (int i = x; i < x + w; i++) harp_pixel(gw, i, y, c);
}

static inline void harp_vline(harp_window_t *gw, int x, int y, int h, uint32_t c)
{
    for (int i = y; i < y + h; i++) harp_pixel(gw, x, i, c);
}

static inline void harp_rect_outline(harp_window_t *gw, int x, int y, int w, int h, uint32_t c)
{
    harp_hline(gw, x, y, w, c);
    harp_hline(gw, x, y + h - 1, w, c);
    harp_vline(gw, x, y, h, c);
    harp_vline(gw, x + w - 1, y, h, c);
}

static inline void harp_fill(harp_window_t *gw, uint32_t c)
{
    harp_rect(gw, 0, 0, gw->w, gw->h, c);
}

#endif
#define WM_SOCK         "wm:events"
#define WM_MSG_REGISTER   1
#define WM_MSG_DIRTY      2
#define WM_MSG_UNREGISTER 3

typedef struct {
    uint8_t  type;
    uint32_t pid;
    int32_t  x, y, w, h;
    char     title[64];
} wm_msg_t;

typedef struct {
    int32_t   x, y, w, h;
    uint32_t *buf;
    uint32_t  pid;
    char      shmname[32];
    socket_file_t *sock;
} harp_window_t;

static inline harp_window_t *harp_open(const char *title, int x, int y, int w, int h)
{
    if (!socket_exists(WM_SOCK)) return NULL;

    harp_window_t *gw = NULL;
    for (int retry = 0; retry < 10 && !gw; retry++) {
        gw = (harp_window_t *)malloc(sizeof(harp_window_t));
        if (!gw) zen_sleep_ms(10);
    }
    if (!gw) return NULL;
    memset(gw, 0, sizeof(*gw));

    gw->x   = x;
    gw->y   = y;
    gw->w   = w;
    gw->h   = h;
    gw->pid = (uint32_t)getpid();

    snprintf(gw->shmname, sizeof(gw->shmname), "wm:shm_%u", gw->pid);
    shm_info_t si;
    if (zen_shm_create(gw->shmname, (size_t)(w * h * 4), &si) < 0) {
        free(gw);
        return NULL;
    }
    gw->buf = (uint32_t *)si.addr;
    memset(gw->buf, 0, (size_t)(w * h * 4));

    socket_open(WM_SOCK, &gw->sock);

    wm_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WM_MSG_REGISTER;
    msg.pid  = gw->pid;
    msg.x    = x;
    msg.y    = y;
    msg.w    = w;
    msg.h    = h;
    strncpy(msg.title, title, 63);
    socket_write(gw->sock, &msg, sizeof(msg));

    return gw;
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
    zen_shm_close(gw->shmname);
    free(gw);
}

static inline void harp_pixel(harp_window_t *gw, int x, int y, uint32_t c)
{
    if (!gw || x < 0 || y < 0 || x >= gw->w || y >= gw->h) return;
    gw->buf[y * gw->w + x] = c;
}

static inline void harp_rect(harp_window_t *gw, int x, int y, int w, int h, uint32_t c)
{
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            harp_pixel(gw, col, row, c);
}

static inline void harp_hline(harp_window_t *gw, int x, int y, int w, uint32_t c)
{
    for (int i = x; i < x + w; i++) harp_pixel(gw, i, y, c);
}

static inline void harp_vline(harp_window_t *gw, int x, int y, int h, uint32_t c)
{
    for (int i = y; i < y + h; i++) harp_pixel(gw, x, i, c);
}

static inline void harp_rect_outline(harp_window_t *gw, int x, int y, int w, int h, uint32_t c)
{
    harp_hline(gw, x, y, w, c);
    harp_hline(gw, x, y + h - 1, w, c);
    harp_vline(gw, x, y, h, c);
    harp_vline(gw, x + w - 1, y, h, c);
}

static inline void harp_fill(harp_window_t *gw, uint32_t c)
{
    harp_rect(gw, 0, 0, gw->w, gw->h, c);
}

#endif