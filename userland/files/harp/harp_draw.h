#pragma once
#include <stdint.h>
#include "harp_wm.h"
#define SSFN_MAXLINES 4096
#define SSFN_memcmp memcmp
#define SSFN_memset memset
#define SSFN_memcpy memcpy
#define SSFN_realloc realloc
#define SSFN_free free
#include "ssfn.h"

void draw_init(uint32_t *backbuf, uint32_t *bgbuf, uint32_t *dash_backdrop,
               int dash_backdrop_y, int dash_backdrop_w, void *ssfn_ctx);
void draw_desktop(void);
void draw_window(int idx);
void draw_dash(void);
void draw_drag_outline(void);
void push_to_fb(uint32_t *fb_addr, uint32_t ptr_x, uint32_t ptr_y, uint32_t pitch);

typedef enum {
    ROUND_NONE   = 0x00,
    ROUND_TOP    = 0x03,
    ROUND_BOTTOM = 0x0C,
    ROUND_ALL    = 0x0F,
} round_corners_t;

void bb_rect(int x, int y, int w, int h, uint32_t c);
void bb_rrect(int x, int y, int w, int h, int r, uint32_t c);
void bb_rrect_ex(int x, int y, int w, int h, int r, uint32_t c, round_corners_t corners);
void bb_rrect_alpha(int x, int y, int w, int h, int r, uint32_t c, uint32_t a);
void bb_rrect_outline(int x, int y, int w, int h, int r, uint32_t c);
void bb_text(int x, int y, uint32_t fg, uint32_t bg, const char *str);
int  text_w(const char *str);

#define DTILE   32
#define MAX_DTX 80
#define MAX_DTY 60

extern uint8_t  dirty[MAX_DTY][MAX_DTX];
extern int      dtx_count, dty_count;
extern int      dirty_full;

void dirty_mark(int x, int y, int w, int h);
void dirty_all(void);
