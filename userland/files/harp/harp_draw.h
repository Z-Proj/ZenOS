/**
 * 
 * @file : harp_draw.h
 * @brief : Drawing primitives and pixman integration.
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
 * The above licensing applies to all other parts of the software (Harp).
 * 
 * @author : Rishies2010
 * @copyright (c) 2026
 * 
 */

#pragma once
#include <stdint.h>
#include "harp_wm.h"
#include "libfont.h"

void draw_init(uint32_t *backbuf, uint32_t *bgbuf, uint32_t *dash_backdrop,
               int dash_backdrop_y, int dash_backdrop_w, font_face_t *font);
void draw_desktop(void);
void draw_window(int idx, int full_frame);
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
