#pragma once

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_VARARGS
#include "nuklear.h"

#include "../../include/harp_api.h"
#include "libfont.h"

#define NK_FONT_SIZE 14
#define NK_BASELINE(top) ((top) + NK_FONT_SIZE)

typedef struct {
    harp_window_t  *win;
    uint32_t       *backbuf;        /* off-screen render target */
    font_face_t    *font;
    int             font_size;
    struct nk_user_font nk_font;
    struct nk_context   ctx;
    int             close_req;
} nk_harp_t;

static nk_harp_t *g_nh = NULL;

static inline uint32_t nk_harp_blend_px(uint32_t dst, uint32_t src)
{
    uint32_t a = src >> 24;
    if (a == 0) return dst;
    if (a == 255) return src;
    uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    uint32_t ia = 255 - a;
    return 0xFF000000
        | (((sr * a + dr * ia) / 255) << 16)
        | (((sg * a + dg * ia) / 255) << 8)
        |  ((sb * a + db * ia) / 255);
}

static float nk_harp_text_width(nk_handle handle, float height, const char *text, int len)
{
    (void)handle;
    nk_harp_t *nh = g_nh;
    if (!nh || !text || len <= 0) return 0;
    int sz = (int)height;
    if (sz < 8) sz = 8;
    char tmp[512];
    int copy = len < 511 ? len : 511;
    memcpy(tmp, text, copy);
    tmp[copy] = '\0';
    return (float)font_measure(nh->font, sz, tmp);
}

static void nk_harp_fill_rect(nk_harp_t *nh, int x, int y, int w, int h, uint32_t col)
{
    if (w <= 0 || h <= 0) return;
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + w > nh->win->w ? nh->win->w : x + w;
    int y2 = y + h > nh->win->h ? nh->win->h : y + h;
    if (x1 >= x2 || y1 >= y2) return;
    uint8_t a = (col >> 24) & 0xFF;
    for (int row = y1; row < y2; row++) {
        uint32_t *line = nh->backbuf + row * nh->win->w + x1;
        if (a == 255) {
            for (int i = 0; i < x2 - x1; i++) line[i] = col;
        } else {
            for (int i = 0; i < x2 - x1; i++) line[i] = nk_harp_blend_px(line[i], col);
        }
    }
}

static void nk_harp_fill_rrect(nk_harp_t *nh, int x, int y, int w, int h, int r, uint32_t col)
{
    if (w <= 0 || h <= 0) return;
    if (r < 1) { nk_harp_fill_rect(nh, x, y, w, h, col); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int row = 0; row < h; row++) {
        int x0 = x, x1 = x + w;
        if (row < r) {
            int dy = r - row - 1, dx = 0;
            while (dx * dx + dy * dy < r * r) dx++;
            int cut = r - dx;
            x0 = x + cut; x1 = x + w - cut;
        } else if (row >= h - r) {
            int dy = row - (h - r), dx = 0;
            while (dx * dx + dy * dy < r * r) dx++;
            int cut = r - dx;
            x0 = x + cut; x1 = x + w - cut;
        }
        nk_harp_fill_rect(nh, x0, y + row, x1 - x0, 1, col);
    }
}

static void nk_harp_stroke_rect(nk_harp_t *nh, int x, int y, int w, int h, int t, uint32_t col)
{
    nk_harp_fill_rect(nh, x,         y,         w, t, col);
    nk_harp_fill_rect(nh, x,         y + h - t, w, t, col);
    nk_harp_fill_rect(nh, x,         y,         t, h, col);
    nk_harp_fill_rect(nh, x + w - t, y,         t, h, col);
}

static void nk_harp_fill_circle(nk_harp_t *nh, int cx, int cy, int r, uint32_t col)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        nk_harp_fill_rect(nh, cx - dx, cy + dy, dx * 2, 1, col);
    }
}

static void nk_harp_stroke_line(nk_harp_t *nh, int x0, int y0, int x1, int y1, int t, uint32_t col)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (steps == 0) { nk_harp_fill_rect(nh, x0, y0, t, t, col); return; }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        nk_harp_fill_rect(nh, x, y, t, t, col);
    }
}

static void nk_harp_fill_triangle(nk_harp_t *nh,
    int ax, int ay, int bx, int by, int cx, int cy, uint32_t col)
{
    int ymin = ay < by ? ay : by; if (cy < ymin) ymin = cy;
    int ymax = ay > by ? ay : by; if (cy > ymax) ymax = cy;
    for (int y = ymin; y <= ymax; y++) {
        int xs = nh->win->w, xe = 0;
        int pts[3][4] = {
            {ax, ay, bx, by}, {bx, by, cx, cy}, {cx, cy, ax, ay}
        };
        for (int e = 0; e < 3; e++) {
            int x0 = pts[e][0], y0 = pts[e][1];
            int x1 = pts[e][2], y1 = pts[e][3];
            if ((y0 <= y && y < y1) || (y1 <= y && y < y0)) {
                int xi = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
                if (xi < xs) xs = xi;
                if (xi > xe) xe = xi;
            }
        }
        if (xs <= xe) nk_harp_fill_rect(nh, xs, y, xe - xs + 1, 1, col);
    }
}

static void nk_harp_draw_text(nk_harp_t *nh,
    int x, int y, int w, int h,
    const char *text, int len,
    float font_height,
    uint32_t fg, uint32_t bg)
{
    (void)w; (void)h;
    if (!text || len <= 0) return;
    int sz = (int)font_height;
    if (sz < 8) sz = 8;
    char tmp[512];
    int copy = len < 511 ? len : 511;
    memcpy(tmp, text, copy);
    tmp[copy] = '\0';
    font_draw(nh->font, nh->backbuf, nh->win->w, nh->win->h,
              x, y + sz, sz,
              fg | 0xFF000000, bg,
              tmp);
}

static void nk_harp_set_scissor(nk_harp_t *nh, int x, int y, int w, int h)
{
    (void)nh; (void)x; (void)y; (void)w; (void)h;
}

static inline uint32_t nk_color_to_u32(struct nk_color c)
{
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) |
           ((uint32_t)c.g << 8)  |  (uint32_t)c.b;
}

static void nk_harp_render(nk_harp_t *nh)
{
    /* clear backbuf once — full frame drawn before anything hits win->buf */
    memset(nh->backbuf, 0, (size_t)nh->win->w * nh->win->h * sizeof(uint32_t));

    const struct nk_command *cmd;
    nk_foreach(cmd, &nh->ctx) {
        switch (cmd->type) {
        case NK_COMMAND_NOP: break;

        case NK_COMMAND_SCISSOR: {
            const struct nk_command_scissor *s = (const struct nk_command_scissor *)cmd;
            nk_harp_set_scissor(nh, s->x, s->y, s->w, s->h);
        } break;

        case NK_COMMAND_LINE: {
            const struct nk_command_line *l = (const struct nk_command_line *)cmd;
            nk_harp_stroke_line(nh, l->begin.x, l->begin.y, l->end.x, l->end.y,
                l->line_thickness, nk_color_to_u32(l->color));
        } break;

        case NK_COMMAND_RECT: {
            const struct nk_command_rect *r = (const struct nk_command_rect *)cmd;
            nk_harp_stroke_rect(nh, r->x, r->y, r->w, r->h,
                r->line_thickness, nk_color_to_u32(r->color));
        } break;

        case NK_COMMAND_RECT_FILLED: {
            const struct nk_command_rect_filled *r = (const struct nk_command_rect_filled *)cmd;
            nk_harp_fill_rrect(nh, r->x, r->y, r->w, r->h,
                r->rounding, nk_color_to_u32(r->color));
        } break;

        case NK_COMMAND_CIRCLE_FILLED: {
            const struct nk_command_circle_filled *c = (const struct nk_command_circle_filled *)cmd;
            nk_harp_fill_circle(nh, c->x + c->w / 2, c->y + c->h / 2,
                c->w / 2, nk_color_to_u32(c->color));
        } break;

        case NK_COMMAND_CIRCLE: {
            const struct nk_command_circle *c = (const struct nk_command_circle *)cmd;
            int cx = c->x + c->w / 2, cy = c->y + c->h / 2, r = c->w / 2;
            uint32_t col = nk_color_to_u32(c->color);
            int t = c->line_thickness;
            for (int i = 0; i < 64; i++) {
                float a0 = (float)i       * 3.14159f * 2.0f / 64.0f;
                float a1 = (float)(i + 1) * 3.14159f * 2.0f / 64.0f;
                nk_harp_stroke_line(nh,
                    cx + (int)(cosf(a0) * r), cy + (int)(sinf(a0) * r),
                    cx + (int)(cosf(a1) * r), cy + (int)(sinf(a1) * r),
                    t, col);
            }
        } break;

        case NK_COMMAND_TRIANGLE_FILLED: {
            const struct nk_command_triangle_filled *t = (const struct nk_command_triangle_filled *)cmd;
            nk_harp_fill_triangle(nh,
                t->a.x, t->a.y, t->b.x, t->b.y, t->c.x, t->c.y,
                nk_color_to_u32(t->color));
        } break;

        case NK_COMMAND_TRIANGLE: {
            const struct nk_command_triangle *t = (const struct nk_command_triangle *)cmd;
            uint32_t col = nk_color_to_u32(t->color);
            int th = t->line_thickness;
            nk_harp_stroke_line(nh, t->a.x, t->a.y, t->b.x, t->b.y, th, col);
            nk_harp_stroke_line(nh, t->b.x, t->b.y, t->c.x, t->c.y, th, col);
            nk_harp_stroke_line(nh, t->c.x, t->c.y, t->a.x, t->a.y, th, col);
        } break;

        case NK_COMMAND_TEXT: {
            const struct nk_command_text *t = (const struct nk_command_text *)cmd;
            nk_harp_draw_text(nh, t->x, t->y, t->w, t->h,
                t->string, t->length, t->height,
                nk_color_to_u32(t->foreground),
                nk_color_to_u32(t->background));
        } break;

        default: break;
        }
    }
    nk_clear(&nh->ctx);

    /* blit the completed backbuffer to the window buffer in one shot */
    memcpy(nh->win->buf, nh->backbuf, (size_t)nh->win->w * nh->win->h * sizeof(uint32_t));
}

static nk_harp_t *nk_harp_init(const char *title, int x, int y, int w, int h,
                                const char *font_path)
{
    nk_harp_t *nh = (nk_harp_t *)calloc(1, sizeof(nk_harp_t));
    if (!nh) return NULL;

    nh->win = harp_open(title, x, y, w, h);
    if (!nh->win) { free(nh); return NULL; }

    nh->backbuf = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
    if (!nh->backbuf) { harp_close(nh->win); free(nh); return NULL; }

    nh->font = font_load(font_path);
    if (!nh->font) { harp_close(nh->win); free(nh); return NULL; }

    nh->font_size = NK_FONT_SIZE;
    g_nh = nh;

    nh->nk_font.userdata = nk_handle_ptr(nh);
    nh->nk_font.height   = (float)NK_FONT_SIZE;
    nh->nk_font.width    = nk_harp_text_width;

    nk_init_default(&nh->ctx, &nh->nk_font);
    nk_style_default(&nh->ctx);

    struct nk_style *s = &nh->ctx.style;
    s->window.background       = nk_rgb(18, 18, 24);
    s->window.fixed_background = nk_style_item_color(nk_rgb(18, 18, 24));
    s->window.border_color     = nk_rgb(60, 60, 80);
    s->window.border           = 1.0f;
    s->window.padding          = nk_vec2(8, 8);
    s->window.spacing          = nk_vec2(4, 4);

    s->button.normal           = nk_style_item_color(nk_rgb(40, 40, 55));
    s->button.hover            = nk_style_item_color(nk_rgb(55, 55, 75));
    s->button.active           = nk_style_item_color(nk_rgb(70, 70, 95));
    s->button.border_color     = nk_rgb(80, 80, 110);
    s->button.text_normal      = nk_rgb(220, 220, 230);
    s->button.text_hover       = nk_rgb(255, 255, 255);
    s->button.text_active      = nk_rgb(200, 200, 255);
    s->button.rounding         = 4.0f;
    s->button.border           = 1.0f;

    s->slider.bar_normal       = nk_rgb(35, 35, 50);
    s->slider.bar_active       = nk_rgb(80, 120, 200);
    s->slider.bar_filled       = nk_rgb(80, 120, 200);
    s->slider.cursor_normal    = nk_style_item_color(nk_rgb(120, 160, 240));
    s->slider.cursor_hover     = nk_style_item_color(nk_rgb(150, 190, 255));
    s->slider.cursor_active    = nk_style_item_color(nk_rgb(100, 140, 220));

    s->progress.normal         = nk_style_item_color(nk_rgb(30, 30, 45));
    s->progress.cursor_normal  = nk_style_item_color(nk_rgb(80, 180, 120));
    s->progress.cursor_hover   = nk_style_item_color(nk_rgb(100, 210, 140));
    s->progress.cursor_active  = nk_style_item_color(nk_rgb(60, 160, 100));

    s->checkbox.normal         = nk_style_item_color(nk_rgb(30, 30, 45));
    s->checkbox.hover          = nk_style_item_color(nk_rgb(45, 45, 65));
    s->checkbox.active         = nk_style_item_color(nk_rgb(55, 55, 80));
    s->checkbox.cursor_normal  = nk_style_item_color(nk_rgb(80, 120, 200));
    s->checkbox.cursor_hover   = nk_style_item_color(nk_rgb(100, 150, 230));
    s->checkbox.text_normal    = nk_rgb(220, 220, 230);

    s->edit.normal             = nk_style_item_color(nk_rgb(25, 25, 38));
    s->edit.hover              = nk_style_item_color(nk_rgb(30, 30, 45));
    s->edit.active             = nk_style_item_color(nk_rgb(20, 20, 35));
    s->edit.border_color       = nk_rgb(70, 70, 100);
    s->edit.text_normal        = nk_rgb(220, 220, 230);
    s->edit.text_hover         = nk_rgb(230, 230, 240);
    s->edit.text_active        = nk_rgb(240, 240, 255);
    s->edit.cursor_normal      = nk_rgb(120, 160, 240);

    s->selectable.normal       = nk_style_item_color(nk_rgb(18, 18, 24));
    s->selectable.hover        = nk_style_item_color(nk_rgb(35, 35, 55));
    s->selectable.pressed      = nk_style_item_color(nk_rgb(50, 50, 75));
    s->selectable.normal_active= nk_style_item_color(nk_rgb(50, 80, 140));
    s->selectable.text_normal  = nk_rgb(200, 200, 210);
    s->selectable.text_hover   = nk_rgb(230, 230, 240);
    s->selectable.text_pressed = nk_rgb(255, 255, 255);

    nh->close_req = 0;
    return nh;
}

static void nk_harp_feed_events(nk_harp_t *nh)
{
    nk_input_begin(&nh->ctx);
    harp_event_t ev;
    while (harp_poll_event(nh->win, &ev)) {
        if (ev.type == HARP_EVENT_CLOSE_REQ) {
            nh->close_req = 1;
        } else if (ev.type == HARP_EVENT_MOUSE_MOVE) {
            nk_input_motion(&nh->ctx, ev.x, ev.y);
        } else if (ev.type == HARP_EVENT_MOUSE_BUTTON) {
            int down = (ev.value != 0);
            if      (ev.code == BTN_LEFT)   nk_input_button(&nh->ctx, NK_BUTTON_LEFT,   ev.x, ev.y, down);
            else if (ev.code == BTN_RIGHT)  nk_input_button(&nh->ctx, NK_BUTTON_RIGHT,  ev.x, ev.y, down);
            else if (ev.code == BTN_MIDDLE) nk_input_button(&nh->ctx, NK_BUTTON_MIDDLE, ev.x, ev.y, down);
        } else if (ev.type == HARP_EVENT_KEY && ev.value != 0 && ev.key != 0) {
            int k = ev.key;
            if      (k == '\b')           nk_input_key(&nh->ctx, NK_KEY_BACKSPACE, 1);
            else if (k == '\n' || k == '\r') nk_input_key(&nh->ctx, NK_KEY_ENTER, 1);
            else if (k == KEY_ARROW_LEFT) nk_input_key(&nh->ctx, NK_KEY_LEFT, 1);
            else if (k == KEY_ARROW_RIGHT)nk_input_key(&nh->ctx, NK_KEY_RIGHT, 1);
            else if (k >= 0x20 && k < 0x7F) nk_input_char(&nh->ctx, (char)k);
        }
    }
    nk_input_end(&nh->ctx);
}

static void nk_harp_free(nk_harp_t *nh)
{
    if (!nh) return;
    nk_free(&nh->ctx);
    font_free(nh->font);
    harp_close(nh->win);
    free(nh->backbuf);
    free(nh);
    g_nh = NULL;
}