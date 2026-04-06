#pragma once
#include <stdint.h>
#include "harp_proto.h"
#include "../../userlib.h"

#define MAX_WINDOWS 16
#define TITLEBAR_H  28
#define WIN_R        12

typedef struct {
    int      active;
    int      minimized;
    uint32_t pid;
    int32_t  x, y, w, h;
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
extern int      drag_win;
extern int      drag_start_x, drag_start_y;
extern int      drag_base_x, drag_base_y;
extern int      dragging;
extern uint32_t SCR_W, SCR_H;

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
void send_mouse_event(int idx, uint16_t type, uint16_t code, int32_t value, int x, int y, uint32_t mods);
void send_key_event(int idx, uint16_t code, int32_t value, uint32_t mods, int key);
void send_close_req(int idx);
void wm_close_window(int idx);
int  dash_area_top(void);
int  dash_btn_at(int x, int y);
