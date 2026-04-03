#ifndef HARP_API_H
#define HARP_API_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include "../userlib.h"

#define WM_SOCK "wm:events"
#define WM_MSG_REGISTER 1
#define WM_MSG_DIRTY 2
#define WM_MSG_UNREGISTER 3

#define HARP_EVENT_FOCUS 1
#define HARP_EVENT_BLUR 2
#define HARP_EVENT_MOUSE_MOVE 3
#define HARP_EVENT_MOUSE_BUTTON 4
#define HARP_EVENT_KEY 5

#define HARP_MOD_SHIFT 0x01
#define HARP_MOD_CTRL 0x02
#define HARP_MOD_ALT 0x04
#define HARP_MOD_CAPS 0x08

typedef struct {
    uint8_t type;
    uint32_t pid;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    char title[64];
} wm_msg_t;

typedef struct {
    uint16_t type;
    uint16_t code;
    int32_t value;
    int32_t x;
    int32_t y;
    uint32_t modifiers;
    int32_t key;
} harp_event_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t *buf;
    uint32_t pid;
    char shmname[32];
    char evname[32];
    socket_file_t *sock;
    socket_file_t *event_sock;
    int focused;
} harp_window_t;

static inline harp_window_t *harp_open(const char *title, int x, int y, int w, int h)
{
    int wm_ready = 0;
    for (int retry = 0; retry < 3000; retry++)
    {
        if (socket_exists(WM_SOCK))
        {
            wm_ready = 1;
            break;
        }
        zen_sleep_ms(10);
    }
    if (!wm_ready)
        return NULL;

    harp_window_t *gw = NULL;
    for (int retry = 0; retry < 10 && !gw; retry++)
    {
        gw = (harp_window_t *)malloc(sizeof(harp_window_t));
        if (!gw)
            zen_sleep_ms(10);
    }
    if (!gw)
        return NULL;

    memset(gw, 0, sizeof(*gw));
    gw->x = x;
    gw->y = y;
    gw->w = w;
    gw->h = h;
    gw->pid = (uint32_t)getpid();

    snprintf(gw->shmname, sizeof(gw->shmname), "wm:shm_%u", gw->pid);
    snprintf(gw->evname, sizeof(gw->evname), "wm:ev_%u", gw->pid);

    shm_info_t si;
    if (zen_shm_create(gw->shmname, (size_t)(w * h * 4), &si) < 0)
    {
        free(gw);
        return NULL;
    }

    gw->buf = (uint32_t *)si.addr;
    memset(gw->buf, 0, (size_t)(w * h * 4));

    socket_delete(gw->evname);
    if (socket_create(gw->evname) < 0)
    {
        zen_shm_close(gw->shmname);
        free(gw);
        return NULL;
    }
    if (socket_open(gw->evname, &gw->event_sock) < 0 || !gw->event_sock)
    {
        socket_delete(gw->evname);
        zen_shm_close(gw->shmname);
        free(gw);
        return NULL;
    }
    gw->sock = NULL;
    for (int retry = 0; retry < 3000; retry++)
    {
        if (socket_open(WM_SOCK, &gw->sock) == 0 && gw->sock)
            break;
        zen_sleep_ms(10);
    }
    if (!gw->sock)
    {
        socket_close(gw->event_sock);
        socket_delete(gw->evname);
        zen_shm_close(gw->shmname);
        free(gw);
        return NULL;
    }

    wm_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WM_MSG_REGISTER;
    msg.pid = gw->pid;
    msg.x = x;
    msg.y = y;
    msg.w = w;
    msg.h = h;
    strncpy(msg.title, title, sizeof(msg.title) - 1);
    socket_write(gw->sock, &msg, sizeof(msg));

    return gw;
}

static inline int harp_poll_event(harp_window_t *gw, harp_event_t *event)
{
    if (!gw || !gw->event_sock || !event)
        return 0;
    if (socket_available(gw->event_sock) < sizeof(*event))
        return 0;
    uint32_t got = 0;
    if (socket_read(gw->event_sock, event, sizeof(*event), &got) < 0 || got < sizeof(*event))
        return 0;
    if (event->type == HARP_EVENT_FOCUS)
        gw->focused = 1;
    else if (event->type == HARP_EVENT_BLUR)
        gw->focused = 0;
    return 1;
}

static inline void harp_flush(harp_window_t *gw)
{
    if (!gw || !gw->sock)
        return;

    wm_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WM_MSG_DIRTY;
    msg.pid = gw->pid;
    socket_write(gw->sock, &msg, sizeof(msg));
}

static inline void harp_close(harp_window_t *gw)
{
    if (!gw)
        return;

    if (gw->sock)
    {
        wm_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = WM_MSG_UNREGISTER;
        msg.pid = gw->pid;
        socket_write(gw->sock, &msg, sizeof(msg));
        socket_close(gw->sock);
    }
    if (gw->event_sock)
        socket_close(gw->event_sock);

    socket_delete(gw->evname);
    zen_shm_close(gw->shmname);
    free(gw);
}

static inline void harp_pixel(harp_window_t *gw, int x, int y, uint32_t c)
{
    if (!gw || x < 0 || y < 0 || x >= gw->w || y >= gw->h)
        return;
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
    for (int i = x; i < x + w; i++)
        harp_pixel(gw, i, y, c);
}

static inline void harp_vline(harp_window_t *gw, int x, int y, int h, uint32_t c)
{
    for (int i = y; i < y + h; i++)
        harp_pixel(gw, x, i, c);
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
