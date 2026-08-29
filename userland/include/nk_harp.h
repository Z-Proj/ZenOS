#pragma once

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_VARARGS
#include "nuklear.h"

#include "harp_api.h"
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
    int             backbuf_cleared;
    int             scx, scy, scw, sch;
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
    if (x1 < nh->scx) x1 = nh->scx;
    if (y1 < nh->scy) y1 = nh->scy;
    if (x2 > nh->scx + nh->scw) x2 = nh->scx + nh->scw;
    if (y2 > nh->scy + nh->sch) y2 = nh->scy + nh->sch;
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
    if (y + sz + 2 <= nh->scy || y >= nh->scy + nh->sch) return;
    char tmp[512];
    int copy = len < 511 ? len : 511;
    memcpy(tmp, text, copy);
    tmp[copy] = '\0';
    int avail = nh->scx + nh->scw - x;
    if (avail <= 0) return;
    while (tmp[0] && font_measure(nh->font, sz, tmp) > avail) {
        int n = (int)strlen(tmp);
        if (n <= 1) { tmp[0] = '\0'; break; }
        tmp[n - 1] = '\0';
    }
    if (!tmp[0]) return;
    font_draw(nh->font, nh->backbuf, nh->win->w, nh->win->h,
              x, y + sz, sz,
              fg | 0xFF000000, bg,
              tmp);
}

static void nk_harp_set_scissor(nk_harp_t *nh, int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > nh->win->w) w = nh->win->w - x;
    if (y + h > nh->win->h) h = nh->win->h - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    nh->scx = x; nh->scy = y; nh->scw = w; nh->sch = h;
}

static inline uint32_t nk_color_to_u32(struct nk_color c)
{
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) |
           ((uint32_t)c.g << 8)  |  (uint32_t)c.b;
}

/* Draws the current command queue onto the persistent backbuffer and blits
 * it to the window. Does NOT clear the backbuffer first - each widget's own
 * filled background command paints over whatever was there last frame, which
 * avoids a full-screen clear (and the flicker that comes with it) on every
 * single frame. Use nk_harp_render_full() instead when you actually need a
 * clean slate (first frame, after a resize, or after the window shrinks and
 * leaves stale pixels exposed). */
static void nk_harp_render_ex(nk_harp_t *nh, int full_clear)
{
    if (full_clear) {
        memset(nh->backbuf, 0, (size_t)nh->win->w * nh->win->h * sizeof(uint32_t));
        nh->backbuf_cleared = 1;
    }
    nh->scx = 0; nh->scy = 0; nh->scw = nh->win->w; nh->sch = nh->win->h;

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

/* Normal frame: draw over the existing backbuffer, no clear. Call this every
 * frame in your main loop. */
static void nk_harp_render(nk_harp_t *nh)
{
    nk_harp_render_ex(nh, 0);
}

/* Full frame: clears the backbuffer to transparent black before drawing.
 * Call this once right after nk_harp_init(), and again any time the window
 * is resized or you otherwise need to guarantee no stale pixels remain
 * (e.g. after HARP_EVENT_RESIZE, or when a previously-drawn widget/region
 * disappears and nothing else will repaint over it this frame). */
static void nk_harp_render_full(nk_harp_t *nh)
{
    nk_harp_render_ex(nh, 1);
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
    nh->backbuf_cleared = 0;

    nh->font = font_load(font_path);
    if (!nh->font) { harp_close(nh->win); free(nh); return NULL; }

    nh->font_size = NK_FONT_SIZE;
    g_nh = nh;

    nh->nk_font.userdata = nk_handle_ptr(nh);
    nh->nk_font.height   = (float)NK_FONT_SIZE;
    nh->nk_font.width    = nk_harp_text_width;

    nk_init_default(&nh->ctx, &nh->nk_font);
    nk_style_default(&nh->ctx);

    /* ZenOS / harp house style: flat, near-black neutrals (matches the WM's
     * C_BG/C_DASH palette), generously rounded corners to match harp's
     * 12px window radius, and a single soft-blue accent used consistently
     * across every interactive element. Panel backgrounds carry a touch of
     * alpha so they read as translucent, echoing the dash's frosted tiles. */
    struct nk_style *s = &nh->ctx.style;

    #define ZEN_BG        nk_rgba(15, 15, 16, 235)
    #define ZEN_BG_SOLID  nk_rgb(15, 15, 16)
    #define ZEN_SURFACE   nk_rgb(26, 26, 28)
    #define ZEN_SURFACE_2 nk_rgb(36, 36, 39)
    #define ZEN_SURFACE_3 nk_rgb(48, 48, 52)
    #define ZEN_BORDER    nk_rgba(255, 255, 255, 14)
    #define ZEN_ACCENT    nk_rgb(10, 132, 255)
    #define ZEN_ACCENT_HI nk_rgb(50, 156, 255)
    #define ZEN_ACCENT_LO nk_rgb(0, 106, 220)
    #define ZEN_TEXT      nk_rgb(230, 230, 235)
    #define ZEN_TEXT_HI   nk_rgb(255, 255, 255)
    #define ZEN_TEXT_DIM  nk_rgb(142, 142, 150)

    s->window.background        = ZEN_BG_SOLID;
    s->window.fixed_background  = nk_style_item_color(ZEN_BG);
    s->window.border_color      = ZEN_BORDER;
    s->window.border            = 1.0f;
    s->window.rounding          = 10.0f;
    s->window.padding           = nk_vec2(8, 8);
    s->window.spacing           = nk_vec2(6, 6);
    s->window.combo_border_color     = ZEN_BORDER;
    s->window.contextual_border_color= ZEN_BORDER;
    s->window.menu_border_color      = ZEN_BORDER;
    s->window.group_border_color     = ZEN_BORDER;
    s->window.tooltip_border_color   = ZEN_BORDER;
    s->window.popup_border_color     = ZEN_BORDER;
    s->window.group_padding          = nk_vec2(6, 6);

    s->text.color               = ZEN_TEXT;

    s->button.normal            = nk_style_item_color(ZEN_SURFACE_2);
    s->button.hover             = nk_style_item_color(ZEN_SURFACE_3);
    s->button.active            = nk_style_item_color(ZEN_ACCENT_LO);
    s->button.border_color      = ZEN_BORDER;
    s->button.text_normal       = ZEN_TEXT;
    s->button.text_hover        = ZEN_TEXT_HI;
    s->button.text_active       = ZEN_TEXT_HI;
    s->button.rounding          = 3.0f;
    s->button.border            = 1.0f;
    s->button.padding           = nk_vec2(3, 2);

    s->slider.bar_normal        = ZEN_SURFACE_2;
    s->slider.bar_hover         = ZEN_SURFACE_2;
    s->slider.bar_active        = ZEN_ACCENT;
    s->slider.bar_filled        = ZEN_ACCENT;
    s->slider.cursor_normal     = nk_style_item_color(ZEN_TEXT_HI);
    s->slider.cursor_hover      = nk_style_item_color(ZEN_TEXT_HI);
    s->slider.cursor_active     = nk_style_item_color(ZEN_TEXT_HI);
    s->slider.rounding          = 8.0f;

    s->progress.normal          = nk_style_item_color(ZEN_SURFACE_2);
    s->progress.cursor_normal   = nk_style_item_color(ZEN_ACCENT);
    s->progress.cursor_hover    = nk_style_item_color(ZEN_ACCENT_HI);
    s->progress.cursor_active   = nk_style_item_color(ZEN_ACCENT_LO);
    s->progress.rounding        = 8.0f;
    s->progress.cursor_rounding = 8.0f;

    s->checkbox.normal          = nk_style_item_color(ZEN_SURFACE_2);
    s->checkbox.hover           = nk_style_item_color(ZEN_SURFACE_3);
    s->checkbox.active          = nk_style_item_color(ZEN_SURFACE_3);
    s->checkbox.border_color    = ZEN_BORDER;
    s->checkbox.cursor_normal   = nk_style_item_color(ZEN_ACCENT);
    s->checkbox.cursor_hover    = nk_style_item_color(ZEN_ACCENT_HI);
    s->checkbox.text_normal     = ZEN_TEXT;
    s->checkbox.text_hover      = ZEN_TEXT_HI;
    s->checkbox.padding         = nk_vec2(2, 2);

    /* radio buttons reuse the toggle/checkbox style struct in nuklear;
     * nuklear already draws NK_TOGGLE_OPTION as a filled circle, so this
     * alone gives the macOS-style round radio dot for free. */
    s->option.normal            = nk_style_item_color(ZEN_SURFACE_2);
    s->option.hover             = nk_style_item_color(ZEN_SURFACE_3);
    s->option.active            = nk_style_item_color(ZEN_SURFACE_3);
    s->option.border_color      = ZEN_BORDER;
    s->option.cursor_normal     = nk_style_item_color(ZEN_ACCENT);
    s->option.cursor_hover      = nk_style_item_color(ZEN_ACCENT_HI);
    s->option.text_normal       = ZEN_TEXT;
    s->option.text_hover        = ZEN_TEXT_HI;
    s->option.padding           = nk_vec2(3, 3);

    s->edit.normal               = nk_style_item_color(ZEN_SURFACE);
    s->edit.hover                = nk_style_item_color(ZEN_SURFACE);
    s->edit.active                = nk_style_item_color(ZEN_SURFACE);
    s->edit.border_color         = ZEN_BORDER;
    s->edit.rounding             = 6.0f;
    s->edit.padding               = nk_vec2(6, 4);
    s->edit.text_normal          = ZEN_TEXT;
    s->edit.text_hover           = ZEN_TEXT;
    s->edit.text_active          = ZEN_TEXT_HI;
    s->edit.selected_normal      = ZEN_ACCENT_LO;
    s->edit.selected_hover       = ZEN_ACCENT_LO;
    s->edit.selected_text_normal = ZEN_TEXT_HI;
    s->edit.selected_text_hover  = ZEN_TEXT_HI;
    s->edit.cursor_normal        = ZEN_ACCENT_HI;
    s->edit.cursor_hover         = ZEN_ACCENT_HI;

    s->selectable.normal        = nk_style_item_color(ZEN_BG_SOLID);
    s->selectable.hover         = nk_style_item_color(ZEN_SURFACE_2);
    s->selectable.pressed       = nk_style_item_color(ZEN_SURFACE_3);
    s->selectable.normal_active = nk_style_item_color(ZEN_ACCENT_LO);
    s->selectable.hover_active  = nk_style_item_color(ZEN_ACCENT);
    s->selectable.pressed_active= nk_style_item_color(ZEN_ACCENT);
    s->selectable.rounding      = 6.0f;
    s->selectable.text_normal   = ZEN_TEXT_DIM;
    s->selectable.text_hover    = ZEN_TEXT;
    s->selectable.text_pressed  = ZEN_TEXT_HI;
    s->selectable.text_normal_active = ZEN_TEXT_HI;
    s->selectable.text_hover_active  = ZEN_TEXT_HI;
    s->selectable.text_pressed_active= ZEN_TEXT_HI;

    s->combo.normal              = nk_style_item_color(ZEN_SURFACE_2);
    s->combo.hover               = nk_style_item_color(ZEN_SURFACE_3);
    s->combo.active              = nk_style_item_color(ZEN_SURFACE_3);
    s->combo.border_color        = ZEN_BORDER;
    s->combo.rounding            = 7.0f;
    s->combo.content_padding     = nk_vec2(6, 4);
    s->combo.label_normal        = ZEN_TEXT;
    s->combo.label_hover         = ZEN_TEXT_HI;
    s->combo.label_active        = ZEN_TEXT_HI;
    s->combo.button              = s->button;

    s->scrollh.normal            = nk_style_item_hide();
    s->scrollh.hover             = nk_style_item_hide();
    s->scrollh.active            = nk_style_item_hide();
    s->scrollh.cursor_normal     = nk_style_item_color(nk_rgba(255, 255, 255, 60));
    s->scrollh.cursor_hover      = nk_style_item_color(nk_rgba(255, 255, 255, 110));
    s->scrollh.cursor_active     = nk_style_item_color(nk_rgba(255, 255, 255, 140));
    s->scrollh.rounding          = 6.0f;
    s->scrollh.rounding_cursor   = 6.0f;
    s->scrollh.border             = 0.0f;
    s->scrollh.padding            = nk_vec2(2, 2);
    s->scrollv                   = s->scrollh;

    /* combo dropdown chevron + group panels (e.g. the sidebar/content split
     * in settings) - kept in the same family so nothing looks unstyled if
     * an app reaches for a widget the current 5 don't happen to use yet. */
    s->combo.symbol_normal       = ZEN_TEXT_DIM;
    s->combo.symbol_hover        = ZEN_TEXT;
    s->combo.symbol_active       = ZEN_TEXT_HI;

    s->window.group_padding      = nk_vec2(6, 6);
    s->window.group_border       = 1.0f;
    s->window.group_border_color = ZEN_BORDER;

    s->window.popup_padding      = nk_vec2(6, 6);
    s->window.popup_border       = 1.0f;
    s->window.combo_padding      = nk_vec2(6, 4);
    s->window.combo_border       = 1.0f;
    s->window.contextual_padding = nk_vec2(4, 4);
    s->window.contextual_border  = 1.0f;
    s->window.menu_padding       = nk_vec2(4, 4);
    s->window.menu_border        = 1.0f;
    s->window.tooltip_padding    = nk_vec2(6, 4);
    s->window.tooltip_border     = 1.0f;
    s->window.scrollbar_size     = nk_vec2(10, 10);
    s->window.min_row_height_padding = 6.0f;

    /* nuklear's own title bar (only drawn if an app passes NK_WINDOW_TITLE -
     * harp's WM already draws the real chrome, so most apps skip this, but
     * it's themed for anything that does use it, e.g. the widgets demo). */
    s->window.header.normal      = nk_style_item_color(ZEN_SURFACE);
    s->window.header.hover       = nk_style_item_color(ZEN_SURFACE);
    s->window.header.active      = nk_style_item_color(ZEN_SURFACE);
    s->window.header.label_normal= ZEN_TEXT;
    s->window.header.label_hover = ZEN_TEXT_HI;
    s->window.header.label_active= ZEN_TEXT_HI;
    s->window.header.padding     = nk_vec2(8, 6);
    s->window.header.label_padding = nk_vec2(4, 2);
    s->window.header.close_button    = s->button;
    s->window.header.minimize_button = s->button;

    /* contextual menus / menu bars share the combo's popup chain internally
     * (nk_contextual_end etc.), so keep their item buttons visually in
     * family with everything else even though the 5 apps don't call the
     * public menu API directly today. */
    s->contextual_button.normal       = nk_style_item_color(ZEN_SURFACE);
    s->contextual_button.hover        = nk_style_item_color(ZEN_SURFACE_2);
    s->contextual_button.active       = nk_style_item_color(ZEN_ACCENT_LO);
    s->contextual_button.border_color = ZEN_BORDER;
    s->contextual_button.text_normal  = ZEN_TEXT;
    s->contextual_button.text_hover   = ZEN_TEXT_HI;
    s->contextual_button.text_active  = ZEN_TEXT_HI;
    s->contextual_button.rounding     = 3.0f;
    s->contextual_button.border       = 0.0f;
    s->contextual_button.padding      = nk_vec2(6, 4);

    s->menu_button = s->contextual_button;

    #undef ZEN_BG
    #undef ZEN_BG_SOLID
    #undef ZEN_SURFACE
    #undef ZEN_SURFACE_2
    #undef ZEN_SURFACE_3
    #undef ZEN_BORDER
    #undef ZEN_ACCENT
    #undef ZEN_ACCENT_HI
    #undef ZEN_ACCENT_LO
    #undef ZEN_TEXT
    #undef ZEN_TEXT_HI
    #undef ZEN_TEXT_DIM

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