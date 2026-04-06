#include "harp_wm.h"
#include <string.h>
#include <stdlib.h>

#define DASH_H      36
#define DASH_MARGIN  8
#define BTN_W       96
#define BTN_H       24
#define BTN_GAP      6
#define DASH_PAD    10
#define LEFT_W     120

window_t windows[MAX_WINDOWS];
int      zstack[MAX_WINDOWS];
int      zcount      = 0;
int      focused_win = -1;
int      drag_win    = -1;
int      drag_start_x, drag_start_y;
int      drag_base_x, drag_base_y;
int      dragging    = 0;

uint32_t SCR_W, SCR_H;

void wm_init(void)
{
    memset(windows, 0, sizeof(windows));
    zcount = 0;
    focused_win = -1;
    drag_win = -1;
    dragging = 0;
}

void z_remove(int i)
{
    for (int j = 0; j < zcount; j++) {
        if (zstack[j] != i) continue;
        for (int k = j; k < zcount - 1; k++)
            zstack[k] = zstack[k + 1];
        zcount--;
        return;
    }
}

void z_raise(int i)
{
    z_remove(i);
    if (zcount < MAX_WINDOWS)
        zstack[zcount++] = i;
}

int z_top(void)
{
    for (int i = zcount - 1; i >= 0; i--)
        if (windows[zstack[i]].active && !windows[zstack[i]].minimized)
            return zstack[i];
    return -1;
}

void send_window_event(window_t *w, const harp_event_t *ev)
{
    if (!w || !w->active || !w->evsock || !ev) return;
    socket_write(w->evsock, ev, sizeof(*ev));
}

void set_focused_window(int idx)
{
    if (idx >= 0 && (!windows[idx].active || windows[idx].minimized))
        idx = -1;
    if (focused_win == idx) return;

    int prev = focused_win;
    focused_win = idx;

    if (prev >= 0 && windows[prev].active) {
        harp_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = HARP_EVENT_BLUR;
        send_window_event(&windows[prev], &ev);
    }
    if (idx >= 0 && windows[idx].active) {
        harp_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = HARP_EVENT_FOCUS;
        send_window_event(&windows[idx], &ev);
    }
}

int dash_area_top(void)
{
    return (int)SCR_H - DASH_MARGIN - DASH_H;
}

void clamp_window(window_t *w)
{
    if (w->w <= 0 || w->h <= 0) return;
    int limit_y = dash_area_top() - (w->h + TITLEBAR_H);
    if (limit_y < 0) limit_y = 0;
    if (w->y > limit_y) w->y = limit_y;
    if (w->y < 0) w->y = 0;
    if (w->x + w->w < 8) w->x = 8 - w->w;
    if (w->x > (int)SCR_W - 8) w->x = (int)SCR_W - 8;
}

int win_at(int x, int y)
{
    for (int i = zcount - 1; i >= 0; i--) {
        int idx = zstack[i];
        window_t *w = &windows[idx];
        if (!w->active || w->minimized) continue;
        if (x >= w->x && x < w->x + w->w &&
            y >= w->y && y < w->y + w->h + TITLEBAR_H)
            return idx;
    }
    return -1;
}

int client_window_at(int x, int y)
{
    for (int i = zcount - 1; i >= 0; i--) {
        int idx = zstack[i];
        window_t *w = &windows[idx];
        if (!w->active || w->minimized) continue;
        if (x < w->x || x >= w->x + w->w) continue;
        if (y < w->y + TITLEBAR_H || y >= w->y + TITLEBAR_H + w->h) continue;
        return idx;
    }
    return -1;
}

int titlebar_at(int idx, int x, int y)
{
    window_t *w = &windows[idx];
    return (x >= w->x && x < w->x + w->w &&
            y >= w->y && y < w->y + TITLEBAR_H);
}

int close_btn_at(int idx, int x, int y)
{
    window_t *w = &windows[idx];
    int bsz = 14;
    int bcx = w->x + w->w - bsz - 5;
    int bcy = w->y + (TITLEBAR_H - bsz) / 2;
    return (x >= bcx && x < bcx + bsz && y >= bcy && y < bcy + bsz);
}

void send_mouse_event(int idx, uint16_t type, uint16_t code, int32_t value,
                      int x, int y, uint32_t mods)
{
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    window_t *w = &windows[idx];
    if (!w->active || w->minimized) return;
    harp_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type      = type;
    ev.code      = code;
    ev.value     = value;
    ev.x         = x - w->x;
    ev.y         = y - (w->y + TITLEBAR_H);
    ev.modifiers = mods;
    send_window_event(w, &ev);
}

void send_key_event(int idx, uint16_t code, int32_t value, uint32_t mods, int key)
{
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    window_t *w = &windows[idx];
    if (!w->active || w->minimized) return;
    harp_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type      = HARP_EVENT_KEY;
    ev.code      = code;
    ev.value     = value;
    ev.modifiers = mods;
    ev.key       = key;
    send_window_event(w, &ev);
}

void send_close_req(int idx)
{
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    window_t *w = &windows[idx];
    if (!w->active) return;
    harp_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = HARP_EVENT_CLOSE_REQ;
    send_window_event(w, &ev);
}

void wm_close_window(int idx)
{
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    window_t *w = &windows[idx];
    if (!w->active) return;
    if (w->evsock) socket_close(w->evsock);
    w->evsock = NULL;
    socket_delete(w->evname);
    zen_shm_close(w->shmname);
    w->active  = 0;
    w->shmbuf  = NULL;
    if (drag_win == idx) { drag_win = -1; dragging = 0; }
    if (focused_win == idx) focused_win = -1;
    z_remove(idx);
    set_focused_window(z_top());
}

int dash_btn_at(int x, int y)
{
    int dt = dash_area_top(), ry = dt;
    int btn_count = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active) btn_count++;
    if (btn_count == 0) return -1;
    int total_w = btn_count * (BTN_W + BTN_GAP) - BTN_GAP + DASH_PAD * 2;
    int rx = (int)SCR_W - DASH_MARGIN - total_w;
    if (x < rx || x >= rx + total_w || y < ry || y >= ry + DASH_H) return -1;
    int bx = rx + DASH_PAD;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) continue;
        if (x >= bx && x < bx + BTN_W) return i;
        bx += BTN_W + BTN_GAP;
    }
    return -1;
}
