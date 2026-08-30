/**
 * 
 * @file : harp_wm.h
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

#pragma once
#include <stdint.h>
#include "harp_proto.h"
#include "../../userlib.h"

#define MAX_WINDOWS 16
#define MAX_LAUNCH_APPS 8
#define TITLEBAR_H  28
#define WIN_R        12

typedef struct {
    int active;
    char path[128];
    uint32_t color;
    char label[4];
    char args[96];
} launcher_app_t;

typedef struct {
    int      active;
    int      minimized;
    int      dirty_valid;
    uint32_t pid;
    int32_t  x, y, w, h;
    int32_t  dirty_x, dirty_y, dirty_w, dirty_h;
    int      read_index;  /* which shm half (0/1) currently holds the ready frame */
    char     title[64];
    char     shmname[32];
    char     evname[32];
    uint8_t       *shmbuf;
    socket_file_t *evsock;
} window_t;

extern window_t windows[MAX_WINDOWS];
extern int      zstack[MAX_WINDOWS];
extern int      zcount;
extern int      focused_win;
extern int      captured_win;
extern int      drag_win;
extern int      drag_start_x, drag_start_y;
extern int      drag_base_x, drag_base_y;
extern int      dragging;
extern uint32_t SCR_W, SCR_H;
extern launcher_app_t launcher_apps[MAX_LAUNCH_APPS];
extern int launcher_app_count;

void wm_init(void);
void z_raise(int i);
void z_remove(int i);
int  z_top(void);
void set_focused_window(int idx);
void clamp_window(window_t *w);
int  win_at(int x, int y);
int  client_window_at(int x, int y);
int  titlebar_at(int idx, int x, int y);
int  close_btn_at(int idx, int x, int y);
void send_window_event(window_t *w, const harp_event_t *ev);
void mark_window_dirty(window_t *w, int x, int y, int w_dirty, int h_dirty);
void send_mouse_event(int idx, uint16_t type, uint16_t code, int32_t value, int x, int y, uint32_t mods);
void send_mouse_raw_event(int idx, int32_t dx, int32_t dy, uint32_t mods);
void send_key_event(int idx, uint16_t code, int32_t value, uint32_t mods, int key);
void send_close_req(int idx);
void wm_close_window(int idx);
int  dash_area_top(void);
int  screenshot_dash_layout(int *x, int *y, int *w);
int  power_dash_layout(int *x, int *y, int *w);
int  launcher_dash_layout(int *x, int *y, int *w);
int  running_dash_layout(int *x, int *y, int *w, int *visible_count);
int  screenshot_btn_at(int x, int y);
int  power_btn_at(int x, int y);
int  launcher_btn_at(int x, int y);
int  dash_btn_at(int x, int y);