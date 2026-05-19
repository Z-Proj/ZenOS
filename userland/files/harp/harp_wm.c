/**
 * 
 * @file : harp_wm.c
 * @brief : Harp window manager - Z-order, focus, dirty regions, and dash layout.
 * 
 * MIT License
 * 
 * Copyright (c) 2026 Rishies2010
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * @author : Rishies2010
 * @copyright (c) 2026
 * 
 */

#include "harp_wm.h"
#include <string.h>
#include <stdlib.h>

#define DASH_H      36
#define DASH_MARGIN  8
#define DASH_GAP     8
#define LAUNCH_PAD   6
#define LAUNCH_ICON  24
#define LAUNCH_GAP   6
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
launcher_app_t launcher_apps[MAX_LAUNCH_APPS];
int launcher_app_count = 0;

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

void mark_window_dirty(window_t *w, int x, int y, int w_dirty, int h_dirty)
{
    if (!w || !w->active || w_dirty <= 0 || h_dirty <= 0)
        return;

    if (x < 0) {
        w_dirty += x;
        x = 0;
    }
    if (y < 0) {
        h_dirty += y;
        y = 0;
    }
    if (x + w_dirty > w->w)
        w_dirty = w->w - x;
    if (y + h_dirty > w->h)
        h_dirty = w->h - y;
    if (w_dirty <= 0 || h_dirty <= 0)
        return;

    if (!w->dirty_valid) {
        w->dirty_valid = 1;
        w->dirty_x = x;
        w->dirty_y = y;
        w->dirty_w = w_dirty;
        w->dirty_h = h_dirty;
        return;
    }

    int x0 = w->dirty_x < x ? w->dirty_x : x;
    int y0 = w->dirty_y < y ? w->dirty_y : y;
    int x1 = w->dirty_x + w->dirty_w;
    int y1 = w->dirty_y + w->dirty_h;
    int nx1 = x + w_dirty;
    int ny1 = y + h_dirty;
    if (nx1 > x1) x1 = nx1;
    if (ny1 > y1) y1 = ny1;
    w->dirty_x = x0;
    w->dirty_y = y0;
    w->dirty_w = x1 - x0;
    w->dirty_h = y1 - y0;
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

int screenshot_dash_layout(int *x, int *y, int *w)
{
    int width = LAUNCH_PAD * 2 + LAUNCH_ICON;
    if (x)
        *x = DASH_MARGIN + LEFT_W + DASH_GAP;
    if (y)
        *y = dash_area_top();
    if (w)
        *w = width;
    return 1;
}

int power_dash_layout(int *x, int *y, int *w)
{
    int width = LAUNCH_PAD * 2 + LAUNCH_ICON;
    if (x)
        *x = (int)SCR_W - DASH_MARGIN - width;
    if (y)
        *y = dash_area_top();
    if (w)
        *w = width;
    return 1;
}

int launcher_dash_layout(int *x, int *y, int *w)
{
    if (launcher_app_count <= 0)
        return 0;
    int width = LAUNCH_PAD * 2 + launcher_app_count * LAUNCH_ICON + (launcher_app_count - 1) * LAUNCH_GAP;
    int shot_x = 0;
    int shot_w = 0;
    screenshot_dash_layout(&shot_x, NULL, &shot_w);
    if (x)
        *x = shot_x + shot_w + DASH_GAP;
    if (y)
        *y = dash_area_top();
    if (w)
        *w = width;
    return 1;
}

int running_dash_layout(int *x, int *y, int *w, int *visible_count)
{
    int active_count = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active)
            active_count++;

    if (y)
        *y = dash_area_top();
    int shot_x = 0;
    int shot_w = 0;
    int launcher_x = 0;
    int launcher_w = 0;
    int left_limit = DASH_MARGIN + LEFT_W + DASH_MARGIN;
    if (screenshot_dash_layout(&shot_x, NULL, &shot_w))
        left_limit = shot_x + shot_w + DASH_GAP;
    if (launcher_dash_layout(&launcher_x, NULL, &launcher_w))
        left_limit = launcher_x + launcher_w + DASH_GAP;

    int power_x = (int)SCR_W - DASH_MARGIN;
    int power_w = 0;
    if (power_dash_layout(&power_x, NULL, &power_w))
        power_x -= DASH_GAP;

    if (active_count == 0) {
        if (x)
            *x = power_x;
        if (w)
            *w = 0;
        if (visible_count)
            *visible_count = 0;
        return 0;
    }

    int available = power_x - left_limit;
    if (available < DASH_PAD * 2 + BTN_W) {
        if (x)
            *x = power_x;
        if (w)
            *w = 0;
        if (visible_count)
            *visible_count = 0;
        return 0;
    }

    int max_visible = (available - DASH_PAD * 2 + BTN_GAP) / (BTN_W + BTN_GAP);
    if (max_visible < 1)
        max_visible = 1;
    if (max_visible > active_count)
        max_visible = active_count;

    int total_w = DASH_PAD * 2 + max_visible * BTN_W + (max_visible - 1) * BTN_GAP;
    int rx = power_x - total_w;
    if (rx < left_limit)
        rx = left_limit;

    if (x)
        *x = rx;
    if (w)
        *w = total_w;
    if (visible_count)
        *visible_count = max_visible;
    return 1;
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
    w->dirty_valid = 0;
    w->shmbuf  = NULL;
    if (drag_win == idx) { drag_win = -1; dragging = 0; }
    if (focused_win == idx) focused_win = -1;
    z_remove(idx);
    set_focused_window(z_top());
}

int dash_btn_at(int x, int y)
{
    int rx = 0;
    int ry = 0;
    int total_w = 0;
    int visible = 0;
    if (!running_dash_layout(&rx, &ry, &total_w, &visible))
        return -1;
    if (x < rx || x >= rx + total_w || y < ry || y >= ry + DASH_H)
        return -1;
    int bx = rx + DASH_PAD;
    int shown = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active)
            continue;
        if (shown >= visible)
            break;
        if (x >= bx && x < bx + BTN_W)
            return i;
        bx += BTN_W + BTN_GAP;
        shown++;
    }
    return -1;
}

int launcher_btn_at(int x, int y)
{
    int lx = 0;
    int ly = 0;
    int lw = 0;
    if (!launcher_dash_layout(&lx, &ly, &lw))
        return -1;
    if (x < lx || x >= lx + lw || y < ly || y >= ly + DASH_H)
        return -1;
    int ix = lx + LAUNCH_PAD;
    int iy = ly + (DASH_H - LAUNCH_ICON) / 2;
    for (int i = 0; i < launcher_app_count; i++) {
        if (x >= ix && x < ix + LAUNCH_ICON && y >= iy && y < iy + LAUNCH_ICON)
            return i;
        ix += LAUNCH_ICON + LAUNCH_GAP;
    }
    return -1;
}

int screenshot_btn_at(int x, int y)
{
    int sx = 0;
    int sy = 0;
    int sw = 0;
    if (!screenshot_dash_layout(&sx, &sy, &sw))
        return -1;
    if (x < sx || x >= sx + sw || y < sy || y >= sy + DASH_H)
        return -1;
    int ix = sx + LAUNCH_PAD;
    int iy = sy + (DASH_H - LAUNCH_ICON) / 2;
    return (x >= ix && x < ix + LAUNCH_ICON && y >= iy && y < iy + LAUNCH_ICON) ? 0 : -1;
}

int power_btn_at(int x, int y)
{
    int px = 0;
    int py = 0;
    int pw = 0;
    if (!power_dash_layout(&px, &py, &pw))
        return -1;
    if (x < px || x >= px + pw || y < py || y >= py + DASH_H)
        return -1;
    int ix = px + LAUNCH_PAD;
    int iy = py + (DASH_H - LAUNCH_ICON) / 2;
    return (x >= ix && x < ix + LAUNCH_ICON && y >= iy && y < iy + LAUNCH_ICON) ? 0 : -1;
}
