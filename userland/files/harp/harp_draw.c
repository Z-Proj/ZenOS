#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

#include "harp_draw.h"
#include "harp_wm.h"

#define FONT_SIZE    16
#define BASELINE(y)  ((y) + FONT_SIZE + 1)
#define LAUNCH_FONT_SIZE 11
#define LAUNCH_BASELINE(y) ((y) + LAUNCH_FONT_SIZE + 1)
#define TITLEBAR_H   28
#define WIN_R         12
#define DASH_H       36
#define DASH_MARGIN   8
#define DASH_GAP      8
#define DASH_PAD     10
#define DASH_R       10
#define LAUNCH_PAD    6
#define LAUNCH_ICON  24
#define LAUNCH_GAP    6
#define LAUNCH_R      7
#define BTN_H        24
#define BTN_W        96
#define BTN_R         6
#define BTN_GAP       6
#define LEFT_W      120

#define C_BG         0xFF0E0E0E
#define C_DASH       0xFF0A0A0A
#define C_TITLEBAR   0xFF0A0A0A
#define C_TITLEBAR_F 0xFF111111
#define C_TITLE_DIM  0xFF666666
#define C_TITLE_ACT  0xFFDDDDDD
#define C_CLOSE      0xFF3A1010
#define C_CLOSE_F    0xFFCC3333
#define C_BTN        0xFF242424
#define C_BTN_F      0xFF303030
#define C_BTN_TXT    0xFF777777
#define C_BTN_TXT_F  0xFFEEEEEE
#define C_CLOCK      0xFFAAAAAA
#define C_DRAG       0xFF3A3A3A
#define C_WHITE      0xFFFFFFFF
#define C_SHOT       0xFF646B76
#define TILE_ALPHA   120

static uint32_t    *s_backbuf;
static uint32_t    *s_bgbuf;
static uint32_t    *s_dash_backdrop;
static int          s_dash_backdrop_y;
static int          s_dash_backdrop_w;
static font_face_t *s_font;

uint8_t dirty[MAX_DTY][MAX_DTX];
int     dtx_count, dty_count;
int     dirty_full = 1;

void dirty_mark(int x, int y, int w, int h)
{
    if (dirty_full || w <= 0 || h <= 0) return;
    int tx0 = x / DTILE; if (tx0 < 0) tx0 = 0;
    int ty0 = y / DTILE; if (ty0 < 0) ty0 = 0;
    int tx1 = (x + w - 1) / DTILE; if (tx1 >= dtx_count) tx1 = dtx_count - 1;
    int ty1 = (y + h - 1) / DTILE; if (ty1 >= dty_count) ty1 = dty_count - 1;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++)
            dirty[ty][tx] = 1;
}

void dirty_all(void) { dirty_full = 1; }

void draw_init(uint32_t *backbuf, uint32_t *bgbuf, uint32_t *dash_backdrop,
               int dash_backdrop_y, int dash_backdrop_w, font_face_t *font)
{
    s_backbuf         = backbuf;
    s_bgbuf           = bgbuf;
    s_dash_backdrop   = dash_backdrop;
    s_dash_backdrop_y = dash_backdrop_y;
    s_dash_backdrop_w = dash_backdrop_w;
    s_font            = font;

    dtx_count = ((int)SCR_W + DTILE - 1) / DTILE;
    if (dtx_count > MAX_DTX) dtx_count = MAX_DTX;
    dty_count = ((int)SCR_H + DTILE - 1) / DTILE;
    if (dty_count > MAX_DTY) dty_count = MAX_DTY;
}

static inline void bb_px(int x, int y, uint32_t c)
{
    if ((uint32_t)x < SCR_W && (uint32_t)y < SCR_H)
        s_backbuf[y * SCR_W + x] = c;
}

static inline uint32_t blend_px(uint32_t dst, uint32_t src)
{
    uint32_t a = src >> 24;
    if (a == 0) return dst;
    if (a == 255) return src;
    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >> 8)  & 0xFF;
    uint32_t sb =  src        & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8)  & 0xFF;
    uint32_t db =  dst        & 0xFF;
    uint32_t ia = 255 - a;
    return 0xFF000000
        | (((sr * a + dr * ia) / 255) << 16)
        | (((sg * a + dg * ia) / 255) << 8)
        |  ((sb * a + db * ia) / 255);
}

static inline void fill_span(uint32_t *dst, int count, uint32_t c)
{
    for (int i = 0; i < count; i++) dst[i] = c;
}

static inline void blend_span(uint32_t *dst, int count, uint32_t src)
{
    uint32_t a = src >> 24;
    if (a == 255) { fill_span(dst, count, src); return; }
    for (int i = 0; i < count; i++) dst[i] = blend_px(dst[i], src);
}

void bb_rect(int x, int y, int w, int h, uint32_t c)
{
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + w > (int)SCR_W ? (int)SCR_W : x + w;
    int y2 = y + h > (int)SCR_H ? (int)SCR_H : y + h;
    if (x1 >= x2 || y1 >= y2) return;
    for (int row = y1; row < y2; row++)
        fill_span(s_backbuf + row * SCR_W + x1, x2 - x1, c);
}

void bb_rrect_ex(int x, int y, int w, int h, int r, uint32_t c, round_corners_t corners)
{
    if (w <= 0 || h <= 0) return;
    if (r < 1 || corners == ROUND_NONE) { bb_rect(x, y, w, h, c); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int tl = corners & 0x01, tr = corners & 0x02;
    int bl = corners & 0x04, br = corners & 0x08;
    for (int row = 0; row < h; row++) {
        int ay = y + row;
        if (ay < 0 || (uint32_t)ay >= SCR_H) continue;
        int x0 = x, x1 = x + w;
        if (row < r) {
            int dy = r - row - 1, dx = 0;
            while (dx * dx + dy * dy < r * r) dx++;
            int cut = r - dx;
            if (tl) x0 = x + cut;
            if (tr) x1 = x + w - cut;
        } else if (row >= h - r) {
            int dy = row - (h - r), dx = 0;
            while (dx * dx + dy * dy < r * r) dx++;
            int cut = r - dx;
            if (bl) x0 = x + cut;
            if (br) x1 = x + w - cut;
        }
        if (x0 < 0) x0 = 0;
        if (x1 > (int)SCR_W) x1 = (int)SCR_W;
        if (x0 >= x1) continue;
        uint32_t *line = s_backbuf + ay * SCR_W;
        for (int ax = x0; ax < x1; ax++) line[ax] = c;
    }
}

void bb_rrect(int x, int y, int w, int h, int r, uint32_t c)
{
    bb_rrect_ex(x, y, w, h, r, c, ROUND_ALL);
}

void bb_rrect_alpha(int x, int y, int w, int h, int r, uint32_t c, uint32_t a)
{
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    uint32_t src_color = (a << 24) | (c & 0x00FFFFFF);
    for (int row = 0; row < h; row++) {
        int ay = y + row;
        if (ay < 0 || (uint32_t)ay >= SCR_H) continue;
        int x0 = x, x1 = x + w;
        if (row < r || row >= h - r) {
            int dy = row < r ? (r - row - 1) : (row - (h - r));
            int dx = 0;
            while (dx * dx + dy * dy < r * r) dx++;
            int cut = r - dx;
            x0 = x + cut; x1 = x + w - cut;
        }
        if (x0 < 0) x0 = 0;
        if (x1 > (int)SCR_W) x1 = (int)SCR_W;
        if (x0 >= x1) continue;
        blend_span(s_backbuf + ay * SCR_W + x0, x1 - x0, src_color);
    }
}

void bb_rrect_outline(int x, int y, int w, int h, int r, uint32_t c)
{
    if (w <= 0 || h <= 0) return;
    if (r < 1) {
        for (int i = x; i < x + w; i++) { bb_px(i, y, c); bb_px(i, y + h - 1, c); }
        for (int i = y; i < y + h; i++) { bb_px(x, i, c); bb_px(x + w - 1, i, c); }
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int i = x + r; i < x + w - r; i++) { bb_px(i, y, c); bb_px(i, y + h - 1, c); }
    for (int i = y + r; i < y + h - r; i++) { bb_px(x, i, c); bb_px(x + w - 1, i, c); }
    for (int dy = 0; dy < r; dy++) {
        int dx = r - 1;
        while (dx * dx + dy * dy > r * r) dx--;
        int inner = r - 1;
        while (inner >= 0 && inner * inner + dy * dy > (r - 1) * (r - 1)) inner--;
        for (int i = inner + 1; i <= dx; i++) {
            bb_px(x + r - 1 - i, y + r - 1 - dy, c);
            bb_px(x + w - r + i, y + r - 1 - dy, c);
            bb_px(x + r - 1 - i, y + h - r + dy, c);
            bb_px(x + w - r + i, y + h - r + dy, c);
        }
    }
}

static void draw_x(int cx, int cy, int sz, uint32_t c)
{
    int x1 = cx + 3, y1 = cy + 3, x2 = cx + sz - 4, len = x2 - x1;
    if (len < 0) return;
    for (int i = 0; i <= len; i++) {
        bb_px(x1 + i,     y1 + i,     c);
        bb_px(x1 + i + 1, y1 + i,     c);
        bb_px(x1 + i,     y1 + i + 1, c);
        bb_px(x2 - i,     y1 + i,     c);
        bb_px(x2 - i - 1, y1 + i,     c);
        bb_px(x2 - i,     y1 + i + 1, c);
    }
}

static void draw_plus_icon(int x, int y, int sz)
{
    int arm = sz / 3;
    int thick = sz / 7;
    if (thick < 2) thick = 2;
    int cx = x + sz / 2;
    int cy = y + sz / 2;
    bb_rrect_alpha(x, y, sz, sz, LAUNCH_R, C_SHOT, 208);
    bb_rect(cx - thick / 2, y + (sz - arm) / 2, thick, arm, C_WHITE);
    bb_rect(x + (sz - arm) / 2, cy - thick / 2, arm, thick, C_WHITE);
}

void bb_text(int x, int y, uint32_t fg, uint32_t bg, const char *str)
{
    if (!str || !*str) return;
    font_draw(s_font, s_backbuf, (int)SCR_W, (int)SCR_H,
              x, y, FONT_SIZE, fg, bg, str);
}

static void bb_text_sz(int x, int y, int size, uint32_t fg, uint32_t bg, const char *str)
{
    if (!str || !*str) return;
    font_draw(s_font, s_backbuf, (int)SCR_W, (int)SCR_H,
              x, y, size, fg, bg, str);
}

int text_w(const char *str)
{
    return font_measure(s_font, FONT_SIZE, str);
}

static int text_w_sz(const char *str, int size)
{
    return font_measure(s_font, size, str);
}

static void fit_text(char *dst, size_t dst_sz, const char *src, int max_w)
{
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    while (n > 0 && text_w(dst) > max_w)
        dst[--n] = 0;
}

void draw_desktop(void)
{
    if (s_bgbuf)
        memcpy(s_backbuf, s_bgbuf, SCR_W * SCR_H * 4);
    else
        for (uint32_t i = 0; i < SCR_W * SCR_H; i++) s_backbuf[i] = C_BG;
}

void bb_rrect_alpha_top(int x, int y, int w, int h, int r, uint32_t c, uint32_t a)
{
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    uint32_t src_color = (a << 24) | (c & 0x00FFFFFF);
    for (int row = 0; row < h; row++) {
        int ay = y + row;
        if (ay < 0 || (uint32_t)ay >= SCR_H) continue;
        int x0 = x, x1 = x + w;
        if (row < r) {
            int dy = r - row - 1, dx = 0;
            while (dx * dx + dy * dy < r * r) dx++;
            int cut = r - dx;
            x0 = x + cut; x1 = x + w - cut;
        }
        if (x0 < 0) x0 = 0;
        if (x1 > (int)SCR_W) x1 = (int)SCR_W;
        if (x0 >= x1) continue;
        blend_span(s_backbuf + ay * SCR_W + x0, x1 - x0, src_color);
    }
}

static void bb_round_clip_bottom(int x, int y, int w, int h, int r)
{
    if (r < 1 || w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int row = h - r; row < h; row++) {
        int ay = y + row;
        if (ay < 0 || (uint32_t)ay >= SCR_H) continue;
        int dy = row - (h - r), dx = 0;
        while (dx * dx + dy * dy < r * r) dx++;
        int cut = r - dx;
        for (int i = 0; i < cut; i++) {
            int lx = x + i, rx2 = x + w - 1 - i;
            if ((uint32_t)lx < SCR_W)
                s_backbuf[ay * SCR_W + lx] = s_bgbuf
                    ? s_bgbuf[ay * SCR_W + lx]
                    : (uint32_t)C_BG;
            if ((uint32_t)rx2 < SCR_W)
                s_backbuf[ay * SCR_W + rx2] = s_bgbuf
                    ? s_bgbuf[ay * SCR_W + rx2]
                    : (uint32_t)C_BG;
        }
    }
}

static void draw_close_btn(int cx, int cy, int r, uint32_t fill, int focused)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int px = cx + dx, py = cy + dy;
            if ((uint32_t)px >= SCR_W || (uint32_t)py >= SCR_H) continue;
            float dist = r - __builtin_sqrtf(dx*dx + dy*dy);
            if (dist < 0.0f) continue;
            uint32_t a = dist >= 1.0f ? 255 : (uint32_t)(dist * 255.0f);
            uint32_t bg = s_backbuf[py * SCR_W + px];
            uint32_t br = (bg >> 16) & 0xFF, bg2 = (bg >> 8) & 0xFF, bb2 = bg & 0xFF;
            uint32_t fr = (fill >> 16) & 0xFF, fg2 = (fill >> 8) & 0xFF, fb2 = fill & 0xFF;
            uint32_t or2 = (fr * a + br * (255 - a)) / 255;
            uint32_t og  = (fg2 * a + bg2 * (255 - a)) / 255;
            uint32_t ob  = (fb2 * a + bb2 * (255 - a)) / 255;
            s_backbuf[py * SCR_W + px] = 0xFF000000 | (or2 << 16) | (og << 8) | ob;
        }
    }
    if (!focused) return;
    float xr = r * 0.35f;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            float fdx = dx, fdy = dy;
            float d1 = __builtin_fabsf(fdx - fdy) / 1.414f;
            float d2 = __builtin_fabsf(fdx + fdy) / 1.414f;
            float d = d1 < d2 ? d1 : d2;
            if (d > 1.2f) continue;
            if (fdx*fdx + fdy*fdy > xr*xr*2.0f) continue;
            float circle_dist = r - __builtin_sqrtf(dx*dx + dy*dy);
            if (circle_dist < 0.0f) continue;
            uint32_t a = d < 0.2f ? 220 : (uint32_t)((1.2f - d) * 180.0f);
            int px = cx + dx, py = cy + dy;
            if ((uint32_t)px >= SCR_W || (uint32_t)py >= SCR_H) continue;
            uint32_t bg = s_backbuf[py * SCR_W + px];
            uint32_t br = (bg >> 16) & 0xFF, bg2 = (bg >> 8) & 0xFF, bb2 = bg & 0xFF;
            uint32_t or2 = (0xFF * a + br * (255 - a)) / 255;
            uint32_t og  = (0xFF * a + bg2 * (255 - a)) / 255;
            uint32_t ob  = (0xFF * a + bb2 * (255 - a)) / 255;
            s_backbuf[py * SCR_W + px] = 0xFF000000 | (or2 << 16) | (og << 8) | ob;
        }
    }
}

void draw_window(int idx, int full_frame)
{
    window_t *w = &windows[idx];
    if (!w->active || w->minimized || w->w <= 0 || w->h <= 0) return;

    int fx = w->x, fy = w->y, fw = w->w, fh = w->h + TITLEBAR_H;
    if (fx >= (int)SCR_W || fy >= (int)SCR_H || fx + fw <= 0 || fy + fh <= 0) return;

    int use_partial = !full_frame && w->dirty_valid;
    if (use_partial && !w->shmbuf)
        use_partial = 0;

    if (use_partial) {
        int client_y = fy + TITLEBAR_H;
        int dst_x0 = fx + w->dirty_x;
        int dst_y0 = client_y + w->dirty_y;
        int dst_x1 = dst_x0 + w->dirty_w;
        int dst_y1 = dst_y0 + w->dirty_h;
        if (dst_x0 < 0) dst_x0 = 0;
        if (dst_y0 < 0) dst_y0 = 0;
        if (dst_x1 > (int)SCR_W) dst_x1 = (int)SCR_W;
        if (dst_y1 > (int)SCR_H) dst_y1 = (int)SCR_H;
        if (dst_x0 < dst_x1 && dst_y0 < dst_y1) {
            int src_x0 = dst_x0 - fx;
            int src_y0 = dst_y0 - client_y;
            for (int row = dst_y0; row < dst_y1; row++) {
                memcpy(s_backbuf + row * SCR_W + dst_x0,
                       (uint32_t *)w->shmbuf + (src_y0 + row - dst_y0) * w->w + src_x0,
                       (dst_x1 - dst_x0) * 4);
            }
            bb_round_clip_bottom(fx, fy + TITLEBAR_H, fw, w->h, WIN_R);
            dirty_mark(dst_x0, dst_y0, dst_x1 - dst_x0, dst_y1 - dst_y0);
            if (w->dirty_y + w->dirty_h > w->h - WIN_R)
                dirty_mark(fx, fy + TITLEBAR_H + w->h - WIN_R, fw, WIN_R);
        }
        w->dirty_valid = 0;
        return;
    }

    int focused = (idx == focused_win);
    uint32_t tc = focused ? C_TITLEBAR_F : C_TITLEBAR;

    bb_rrect_alpha_top(fx, fy, fw, TITLEBAR_H, WIN_R, tc, 150);
    uint32_t corner_save[WIN_R * WIN_R * 2];
    int cs_idx = 0;
    for (int row = 0; row < WIN_R; row++) {
        int ay = fy + TITLEBAR_H + w->h - WIN_R + row;
        if (ay < 0 || (uint32_t)ay >= SCR_H) continue;
        int dy = row, dx = 0;
        while (dx*dx + dy*dy < WIN_R*WIN_R) dx++;
        int cut = WIN_R - dx;
        for (int i = 0; i < cut && i < WIN_R; i++) {
            int lx2 = fx + i, rx2 = fx + fw - 1 - i;
            corner_save[cs_idx++] = (uint32_t)lx2 < SCR_W ? s_backbuf[ay * SCR_W + lx2] : 0;
            corner_save[cs_idx++] = (uint32_t)rx2 < SCR_W ? s_backbuf[ay * SCR_W + rx2] : 0;
        }
    }
    if (w->shmbuf) {
        int dst_x0 = fx < 0 ? 0 : fx;
        int dst_y0 = fy + TITLEBAR_H < 0 ? 0 : fy + TITLEBAR_H;
        int dst_x1 = fx + fw > (int)SCR_W ? (int)SCR_W : fx + fw;
        int dst_y1 = fy + TITLEBAR_H + w->h > (int)SCR_H ? (int)SCR_H : fy + TITLEBAR_H + w->h;
        int src_x0 = dst_x0 - fx;
        int src_y0 = dst_y0 - (fy + TITLEBAR_H);
        if (dst_x0 < dst_x1 && dst_y0 < dst_y1)
            for (int row = dst_y0; row < dst_y1; row++)
                memcpy(s_backbuf + row * SCR_W + dst_x0,
                       (uint32_t *)w->shmbuf + (src_y0 + row - dst_y0) * w->w + src_x0,
                       (dst_x1 - dst_x0) * 4);
        bb_round_clip_bottom(fx, fy + TITLEBAR_H, fw, w->h, WIN_R);
    } else {
        bb_rect(fx, fy + TITLEBAR_H, fw, w->h, 0xFF0D0D0D);
    }
    cs_idx = 0;
    for (int row = 0; row < WIN_R; row++) {
        int ay = fy + TITLEBAR_H + w->h - WIN_R + row;
        if (ay < 0 || (uint32_t)ay >= SCR_H) continue;
        int dy = row, dx = 0;
        while (dx*dx + dy*dy < WIN_R*WIN_R) dx++;
        int cut = WIN_R - dx;
        for (int i = 0; i < cut && i < WIN_R; i++) {
            int lx2 = fx + i, rx2 = fx + fw - 1 - i;
            if ((uint32_t)lx2 < SCR_W) s_backbuf[ay * SCR_W + lx2] = corner_save[cs_idx];
            cs_idx++;
            if ((uint32_t)rx2 < SCR_W) s_backbuf[ay * SCR_W + rx2] = corner_save[cs_idx];
            cs_idx++;
        }
    }
    char lbl[48];
    __builtin_strncpy(lbl, w->title, 47); lbl[47] = 0;
    int tw = text_w(lbl);
    int lx = fx + (fw - tw) / 2;
    if (lx < fx + 4) lx = fx + 4;
    uint32_t title_bg = s_backbuf[(fy + TITLEBAR_H/2) * SCR_W + fx + fw/2];
    bb_text(lx, BASELINE(fy + 5), focused ? C_TITLE_ACT : C_TITLE_DIM, title_bg, lbl);

    int br = 10;
    int bcx = fx + fw - br - 5;
    int bcy = fy + TITLEBAR_H / 2;
    draw_close_btn(bcx, bcy, br, focused ? C_CLOSE_F : C_CLOSE, focused);

    w->dirty_valid = 0;
    dirty_mark(fx, fy, fw, fh);
}

static void dash_blit_backdrop(int x, int w)
{
    if (!s_dash_backdrop) return;
    int dt = (int)SCR_H - DASH_MARGIN - DASH_H;
    int r = DASH_R;
    if (r > w / 2) r = w / 2;
    if (r > DASH_H / 2) r = DASH_H / 2;
    for (int row = 0; row < DASH_H; row++) {
        int sy = dt + row;
        if (sy < 0 || (uint32_t)sy >= SCR_H) continue;
        int x0 = x, x1 = x + w;
        if (row < r || row >= DASH_H - r) {
            int dy = row < r ? (r - row - 1) : (row - (DASH_H - r));
            int dx = 0;
            while (dx * dx + dy * dy < r * r) dx++;
            int cut = r - dx;
            x0 = x + cut; x1 = x + w - cut;
        }
        if (x0 < 0) x0 = 0;
        if (x1 > (int)SCR_W) x1 = (int)SCR_W;
        if (x0 >= x1) continue;
        memcpy(s_backbuf + sy * SCR_W + x0,
               s_dash_backdrop + row * s_dash_backdrop_w + x0,
               (x1 - x0) * 4);
    }
}

void draw_dash(void)
{
    int dt = dash_area_top();
    int lx = DASH_MARGIN, ly = dt;

    dash_blit_backdrop(lx, LEFT_W);
    bb_rrect_alpha(lx, ly, LEFT_W, DASH_H, DASH_R, C_DASH, TILE_ALPHA);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int sec = (int)(tv.tv_sec % 86400);
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", sec / 3600, (sec % 3600) / 60, sec % 60);
    int tw = text_w(tbuf);
    int ty_center = ly - 3 + (DASH_H - FONT_SIZE) / 2;
    uint32_t clock_bg = s_backbuf[(ly + DASH_H / 2) * SCR_W + lx + LEFT_W / 2];
    bb_text(lx + (LEFT_W - tw) / 2, BASELINE(ty_center), C_CLOCK, clock_bg, tbuf);

    dirty_mark(lx, ly, LEFT_W, DASH_H);

    int shot_x = 0, shot_y = 0, shot_w = 0;
    if (screenshot_dash_layout(&shot_x, &shot_y, &shot_w)) {
        dash_blit_backdrop(shot_x, shot_w);
        bb_rrect_alpha(shot_x, shot_y, shot_w, DASH_H, DASH_R, C_DASH, TILE_ALPHA);
        int ix = shot_x + LAUNCH_PAD;
        int iy = shot_y + (DASH_H - LAUNCH_ICON) / 2;
        draw_plus_icon(ix, iy, LAUNCH_ICON);
        dirty_mark(shot_x, shot_y, shot_w, DASH_H);
    }

    int launch_x = 0, launch_y = 0, launch_w = 0;
    if (launcher_dash_layout(&launch_x, &launch_y, &launch_w)) {
        dash_blit_backdrop(launch_x, launch_w);
        bb_rrect_alpha(launch_x, launch_y, launch_w, DASH_H, DASH_R, C_DASH, TILE_ALPHA);
        int ix = launch_x + LAUNCH_PAD;
        int iy = launch_y + (DASH_H - LAUNCH_ICON) / 2;
        for (int i = 0; i < launcher_app_count; i++) {
            bb_rrect_alpha(ix, iy, LAUNCH_ICON, LAUNCH_ICON, LAUNCH_R, launcher_apps[i].color, 208);
            int ltw = text_w_sz(launcher_apps[i].label, LAUNCH_FONT_SIZE);
            int tx = ix + (LAUNCH_ICON - ltw) / 2;
            int ty = iy + (LAUNCH_ICON - LAUNCH_FONT_SIZE) / 2 - 1;
            uint32_t txt_bg = s_backbuf[(iy + LAUNCH_ICON / 2) * SCR_W + ix + LAUNCH_ICON / 2];
            bb_text_sz(tx, LAUNCH_BASELINE(ty), LAUNCH_FONT_SIZE, C_WHITE, txt_bg, launcher_apps[i].label);
            ix += LAUNCH_ICON + LAUNCH_GAP;
        }
        dirty_mark(launch_x, launch_y, launch_w, DASH_H);
    }

    int rx = 0, ry = 0, total_w = 0, visible = 0;
    if (!running_dash_layout(&rx, &ry, &total_w, &visible)) return;

    dash_blit_backdrop(rx, total_w);
    bb_rrect_alpha(rx, ry, total_w, DASH_H, DASH_R, C_DASH, TILE_ALPHA);

    int bx = rx + DASH_PAD;
    int top = z_top();
    int shown = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) continue;
        if (shown >= visible) break;
        if (bx + BTN_W > rx + total_w - DASH_PAD) break;
        int is_top = (i == top);
        int by = ry + (DASH_H - BTN_H) / 2;
        bb_rrect_alpha(bx, by, BTN_W, BTN_H, BTN_R, is_top ? C_BTN_F : C_BTN, TILE_ALPHA);
        char lbl[64];
        fit_text(lbl, sizeof(lbl), windows[i].title, BTN_W - 14);
        int ltw = text_w(lbl);
        int ty_c = ly + (DASH_H - FONT_SIZE) / 2;
        uint32_t txt_bg = s_backbuf[(by + BTN_H / 2) * SCR_W + bx + BTN_W / 2];
        bb_text(bx + (BTN_W - ltw) / 2, BASELINE(ty_c) - 3,
                is_top ? C_BTN_TXT_F : C_BTN_TXT, txt_bg, lbl);
        bx += BTN_W + BTN_GAP;
        shown++;
    }
    dirty_mark(rx, ry, total_w, DASH_H);
}

void draw_drag_outline(void)
{
    if (!dragging || drag_win < 0) return;
    window_t *w = &windows[drag_win];
    if (!w->active) return;
    bb_rrect_outline(w->x, w->y, w->w, w->h + TITLEBAR_H, WIN_R, C_DRAG);
    dirty_mark(w->x, w->y, w->w, w->h + TITLEBAR_H);
}

#define CUR_W 12
#define CUR_H 19
static const uint8_t cursor_shape[CUR_H][CUR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,2,1,0,1,2,2,1,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,0,1,2,2,1,0,0},
    {1,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,2,1,0,0},
    {0,0,0,0,0,0,0,0,1,1,0,0},
};

static int prev_cur_x = -1, prev_cur_y = -1;
static int cur_sx = -1, cur_sy = -1;

static void cursor_stamp(uint32_t *fb_addr, int mx, int my, uint32_t pitch)
{
    cur_sx = mx; cur_sy = my;
    for (int cy = 0; cy < CUR_H; cy++) {
        int fy = my + cy;
        if (fy < 0 || (uint32_t)fy >= SCR_H) continue;
        for (int cx = 0; cx < CUR_W; cx++) {
            int fx = mx + cx;
            if (fx < 0 || (uint32_t)fx >= SCR_W) continue;
            uint8_t s = cursor_shape[cy][cx];
            if (!s) continue;
            *((uint32_t*)((uint8_t*)fb_addr + fy * pitch) + fx) = (s == 1) ? 0xFF000000 : 0xFFFFFFFF;
        }
    }
}

static void blit_region(uint32_t *fb_addr, uint32_t pitch, int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)SCR_W) w = (int)SCR_W - x;
    if (y + h > (int)SCR_H) h = (int)SCR_H - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        int fy = y + row;
        memcpy((uint8_t*)fb_addr + fy * pitch + x * 4,
               s_backbuf + fy * SCR_W + x, w * 4);
    }
}

static int dirty_overlaps_rect(int x, int y, int w, int h)
{
    if (dirty_full)
        return 1;
    if (w <= 0 || h <= 0)
        return 0;
    int tx0 = x / DTILE; if (tx0 < 0) tx0 = 0;
    int ty0 = y / DTILE; if (ty0 < 0) ty0 = 0;
    int tx1 = (x + w - 1) / DTILE; if (tx1 >= dtx_count) tx1 = dtx_count - 1;
    int ty1 = (y + h - 1) / DTILE; if (ty1 >= dty_count) ty1 = dty_count - 1;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++)
            if (dirty[ty][tx])
                return 1;
    return 0;
}

void push_to_fb(uint32_t *fb_addr, uint32_t ptr_x, uint32_t ptr_y, uint32_t pitch)
{
    int new_cur_x = (int)ptr_x;
    int new_cur_y = (int)ptr_y;
    int moved = (prev_cur_x != new_cur_x || prev_cur_y != new_cur_y);
    int cursor_hit = dirty_overlaps_rect(new_cur_x, new_cur_y, CUR_W, CUR_H);

    if (dirty_full) {
        for (int y = 0; y < (int)SCR_H; y++)
            memcpy((uint8_t*)fb_addr + y * pitch, s_backbuf + y * SCR_W, SCR_W * 4);
        dirty_full = 0;
        memset(dirty, 0, sizeof(dirty));
        prev_cur_x = new_cur_x;
        prev_cur_y = new_cur_y;
        cursor_stamp(fb_addr, new_cur_x, new_cur_y, pitch);
        return;
    } else {
        if (moved && prev_cur_x >= 0)
            blit_region(fb_addr, pitch, prev_cur_x, prev_cur_y, CUR_W, CUR_H);
        for (int ty = 0; ty < dty_count; ty++) {
            for (int tx = 0; tx < dtx_count; tx++) {
                if (!dirty[ty][tx]) continue;
                dirty[ty][tx] = 0;
                int px = tx * DTILE, py = ty * DTILE;
                int pw = px + DTILE > (int)SCR_W ? (int)SCR_W - px : DTILE;
                int ph = py + DTILE > (int)SCR_H ? (int)SCR_H - py : DTILE;
                for (int row = 0; row < ph; row++) {
                    int y = py + row;
                    memcpy((uint8_t*)fb_addr + y * pitch + px * 4,
                           s_backbuf + y * SCR_W + px, pw * 4);
                }
            }
        }
    }
    if (moved || prev_cur_x < 0 || cursor_hit)
        cursor_stamp(fb_addr, new_cur_x, new_cur_y, pitch);
    prev_cur_x = new_cur_x;
    prev_cur_y = new_cur_y;
}
