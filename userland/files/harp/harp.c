#define SSFN_IMPLEMENTATION
#define SSFN_MAXLINES 4096
#define SSFN_memcmp memcmp
#define SSFN_memset memset
#define SSFN_memcpy memcpy
#define SSFN_realloc realloc
#define SSFN_free free
#include "ssfn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <linux/input.h>
#include "../../userlib.h"
#include "../../include/harp_api.h"

extern char _binary_FreeSansB_sfn_start;

#define MAX_WINDOWS 16
#define FONT_SIZE 12
#define BASELINE(y) ((y) + FONT_SIZE + 1)

#define TITLEBAR_H 24
#define WIN_R 6

#define DASH_H 36
#define DASH_MARGIN 8
#define DASH_PAD 10
#define DASH_R 10
#define BTN_H 24
#define BTN_W 96
#define BTN_R 5
#define BTN_GAP 6
#define LEFT_W 120

#define C_BG 0xFF0E0E0E
#define C_DASH 0xFF1A1A1A
#define C_TITLEBAR 0xFF181818
#define C_TITLEBAR_F 0xFF232323
#define C_TITLE_DIM 0xFF666666
#define C_TITLE_ACT 0xFFDDDDDD
#define C_CLOSE 0xFF3A1010
#define C_CLOSE_F 0xFFCC3333
#define C_BTN 0xFF242424
#define C_BTN_F 0xFF303030
#define C_BTN_TXT 0xFF777777
#define C_BTN_TXT_F 0xFFEEEEEE
#define C_CLOCK 0xFFAAAAAA
#define C_DRAG 0xFF3A3A3A
#define C_WHITE 0xFFFFFFFF

#define BLUR_R 2
#define DARKEN_PCT 80
#define TILE_ALPHA 128

#pragma pack(push, 1)
typedef struct
{
    uint8_t id_len;
    uint8_t cmap_type;
    uint8_t data_type;
    uint16_t cmap_origin;
    uint16_t cmap_length;
    uint8_t cmap_depth;
    uint16_t x_origin;
    uint16_t y_origin;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint8_t descriptor;
} tga_hdr_t;
#pragma pack(pop)

static fb_info_t fb;
static uint32_t *backbuf;
static uint32_t SCR_W, SCR_H;

static uint32_t *bgbuf = NULL;
static uint32_t *dash_backdrop = NULL;
static int dash_backdrop_y = 0;
static int dash_backdrop_w = 0;

static inline uint32_t tga_decode_pixel(const uint8_t *p, int bpp)
{
    if (bpp == 1)
    {
        uint8_t v = p[0];
        return 0xFF000000 | ((uint32_t)v << 16) | ((uint32_t)v << 8) | v;
    }
    uint8_t b = p[0], g = p[1], r = p[2];
    uint8_t a = (bpp == 4) ? p[3] : 0xFF;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint32_t *tga_load(const char *path, int *out_w, int *out_h)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        zen_log("tga: cannot open file", 2, 1);
        return NULL;
    }
    tga_hdr_t h;
    if (fread(&h, sizeof(h), 1, f) != 1)
    {
        fclose(f);
        zen_log("tga: header read failed", 2, 1);
        return NULL;
    }
    if (h.id_len)
        fseek(f, h.id_len, SEEK_CUR);

    uint32_t *cmap = NULL;
    if (h.cmap_type && h.cmap_length > 0)
    {
        int ce = h.cmap_depth >> 3;
        cmap = (uint32_t *)malloc(h.cmap_length * 4);
        if (cmap)
        {
            uint8_t ce_buf[4];
            for (int i = 0; i < h.cmap_length; i++)
            {
                fread(ce_buf, ce, 1, f);
                cmap[i] = tga_decode_pixel(ce_buf, ce);
            }
        }
        else
        {
            fseek(f, (long)h.cmap_length * (h.cmap_depth >> 3), SEEK_CUR);
        }
    }

    int iw = h.width, ih = h.height;
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "tga: w=%d h=%d bpp=%d dtype=%d cmap=%d",
                 iw, ih, h.bpp, h.data_type, h.cmap_length);
        zen_log(dbg, 1, 1);
    }

    int is_cmap = (h.data_type == 1 || h.data_type == 9);
    int img_bpp = is_cmap ? 1 : (h.bpp >> 3);
    if (iw <= 0 || ih <= 0 || (img_bpp != 1 && img_bpp != 3 && img_bpp != 4))
    {
        if (cmap)
            free(cmap);
        fclose(f);
        zen_log("tga: unsupported format", 2, 1);
        return NULL;
    }

    int flip_v = !(h.descriptor & 0x20);
    int sw = (int)SCR_W, sh = (int)SCR_H;
    int crop_w = iw < sw ? iw : sw;
    int crop_h = ih < sh ? ih : sh;
    int src_x0 = (iw - crop_w) / 2;
    int src_y0 = (ih - crop_h) / 2;
    int src_y1 = src_y0 + crop_h - 1;

    uint32_t *px = (uint32_t *)malloc(crop_w * crop_h * 4);
    if (!px)
    {
        if (cmap)
            free(cmap);
        fclose(f);
        zen_log("tga: malloc failed", 2, 1);
        return NULL;
    }
    uint8_t *row = (uint8_t *)malloc(iw * img_bpp);
    if (!row)
    {
        free(px);
        if (cmap)
            free(cmap);
        fclose(f);
        zen_log("tga: malloc row failed", 2, 1);
        return NULL;
    }

    if (h.data_type == 1 || h.data_type == 2 || h.data_type == 3)
    {
        long row_bytes = (long)iw * img_bpp;
        for (int y = 0; y < ih; y++)
        {
            int src_y = flip_v ? (ih - 1 - y) : y;
            if (src_y < src_y0 || src_y > src_y1)
            {
                fseek(f, row_bytes, SEEK_CUR);
                continue;
            }
            fread(row, img_bpp, iw, f);
            int dst_y = src_y - src_y0;
            for (int x = 0; x < crop_w; x++)
            {
                uint8_t *p = row + (src_x0 + x) * img_bpp;
                uint32_t c;
                if (is_cmap && cmap)
                    c = cmap[p[0] < h.cmap_length ? p[0] : 0];
                else
                    c = tga_decode_pixel(p, img_bpp);
                px[dst_y * crop_w + x] = c;
            }
        }
    }
    else if (h.data_type == 9 || h.data_type == 10 || h.data_type == 11)
    {
        uint32_t *full = (uint32_t *)malloc(iw * ih * 4);
        if (!full)
        {
            free(px);
            free(row);
            if (cmap)
                free(cmap);
            fclose(f);
            zen_log("tga: rle malloc failed", 2, 1);
            return NULL;
        }
        int total = iw * ih, i = 0;
        uint8_t tmp[5];
        while (i < total)
        {
            fread(tmp, 1, 1, f);
            int rep = (tmp[0] & 0x7F) + 1;
            if (tmp[0] & 0x80)
            {
                fread(tmp + 1, img_bpp, 1, f);
                uint32_t c = (is_cmap && cmap) ? cmap[tmp[1] < h.cmap_length ? tmp[1] : 0]
                                               : tga_decode_pixel(tmp + 1, img_bpp);
                for (int k = 0; k < rep && i < total; k++, i++)
                {
                    int fy = flip_v ? (ih - 1 - i / iw) : i / iw;
                    full[fy * iw + (i % iw)] = c;
                }
            }
            else
            {
                for (int k = 0; k < rep && i < total; k++, i++)
                {
                    fread(tmp + 1, img_bpp, 1, f);
                    uint32_t c = (is_cmap && cmap) ? cmap[tmp[1] < h.cmap_length ? tmp[1] : 0]
                                                   : tga_decode_pixel(tmp + 1, img_bpp);
                    int fy = flip_v ? (ih - 1 - i / iw) : i / iw;
                    full[fy * iw + (i % iw)] = c;
                }
            }
        }
        for (int dy = 0; dy < crop_h; dy++)
            for (int dx = 0; dx < crop_w; dx++)
                px[dy * crop_w + dx] = full[(src_y0 + dy) * iw + (src_x0 + dx)];
        free(full);
    }
    else
    {
        free(px);
        free(row);
        if (cmap)
            free(cmap);
        fclose(f);
        zen_log("tga: unsupported data_type", 2, 1);
        return NULL;
    }
    free(row);
    if (cmap)
        free(cmap);
    fclose(f);
    zen_log("tga: loaded ok", 1, 1);
    *out_w = crop_w;
    *out_h = crop_h;
    return px;
}

static void box_blur_h(uint32_t *src, uint32_t *dst, int w, int h, int r)
{
    int diam = 2 * r + 1;
    for (int y = 0; y < h; y++)
    {
        uint32_t *s = src + y * w;
        uint32_t *d = dst + y * w;
        uint32_t sr = 0, sg = 0, sb = 0;
        for (int x = -r; x <= r; x++)
        {
            int sx = x < 0 ? 0 : (x >= w ? w - 1 : x);
            uint32_t p = s[sx];
            sr += (p >> 16) & 0xFF;
            sg += (p >> 8) & 0xFF;
            sb += p & 0xFF;
        }
        for (int x = 0; x < w; x++)
        {
            d[x] = 0xFF000000 | ((sr / diam) << 16) | ((sg / diam) << 8) | (sb / diam);
            int xl = x - r;
            if (xl < 0)
                xl = 0;
            int xr = x + r + 1;
            if (xr >= w)
                xr = w - 1;
            uint32_t pl = s[xl], pr = s[xr];
            sr += ((pr >> 16) & 0xFF) - ((pl >> 16) & 0xFF);
            sg += ((pr >> 8) & 0xFF) - ((pl >> 8) & 0xFF);
            sb += (pr & 0xFF) - (pl & 0xFF);
        }
    }
}

static void box_blur_v(uint32_t *src, uint32_t *dst, int w, int h, int r)
{
    int diam = 2 * r + 1;
    for (int x = 0; x < w; x++)
    {
        uint32_t sr = 0, sg = 0, sb = 0;
        for (int y = -r; y <= r; y++)
        {
            int sy = y < 0 ? 0 : (y >= h ? h - 1 : y);
            uint32_t p = src[sy * w + x];
            sr += (p >> 16) & 0xFF;
            sg += (p >> 8) & 0xFF;
            sb += p & 0xFF;
        }
        for (int y = 0; y < h; y++)
        {
            dst[y * w + x] = 0xFF000000 | ((sr / diam) << 16) | ((sg / diam) << 8) | (sb / diam);
            int yt = y - r;
            if (yt < 0)
                yt = 0;
            int yb = y + r + 1;
            if (yb >= h)
                yb = h - 1;
            uint32_t pt = src[yt * w + x], pb = src[yb * w + x];
            sr += ((pb >> 16) & 0xFF) - ((pt >> 16) & 0xFF);
            sg += ((pb >> 8) & 0xFF) - ((pt >> 8) & 0xFF);
            sb += (pb & 0xFF) - (pt & 0xFF);
        }
    }
}

static void bake_bgbuf(void)
{
    int bg_w = 0, bg_h = 0;
    uint32_t *tga = tga_load("/mnt/drv0/bg.tga", &bg_w, &bg_h);
    bgbuf = (uint32_t *)malloc(SCR_W * SCR_H * 4);
    if (!bgbuf)
    {
        if (tga)
            free(tga);
        return;
    }
    for (uint32_t i = 0; i < SCR_W * SCR_H; i++)
        bgbuf[i] = 0xFF0E0E0E;
    if (tga)
    {
        int ox = ((int)SCR_W - bg_w) / 2;
        int oy = ((int)SCR_H - bg_h) / 2;
        for (int y = 0; y < bg_h; y++)
        {
            int dy = oy + y;
            if (dy < 0 || (uint32_t)dy >= SCR_H)
                continue;
            for (int x = 0; x < bg_w; x++)
            {
                int dx = ox + x;
                if (dx < 0 || (uint32_t)dx >= SCR_W)
                    continue;
                bgbuf[dy * SCR_W + dx] = tga[y * bg_w + x] | 0xFF000000;
            }
        }
        free(tga);
    }
}

static void bake_dash_backdrop(void)
{
    int dt = (int)SCR_H - DASH_MARGIN - DASH_H;
    int bw = (int)SCR_W;
    int bh = DASH_H;
    if (!bgbuf || dt < 0 || dt + bh > (int)SCR_H)
        return;

    uint32_t *strip = (uint32_t *)malloc(bw * bh * 4);
    uint32_t *tmp = (uint32_t *)malloc(bw * bh * 4);
    if (!strip || !tmp)
    {
        free(strip);
        free(tmp);
        return;
    }

    for (int y = 0; y < bh; y++)
        memcpy(strip + y * bw, bgbuf + (dt + y) * SCR_W, bw * 4);

    box_blur_h(strip, tmp, bw, bh, BLUR_R);
    box_blur_v(tmp, strip, bw, bh, BLUR_R);
    box_blur_h(strip, tmp, bw, bh, BLUR_R);
    box_blur_v(tmp, strip, bw, bh, BLUR_R);

    for (int i = 0; i < bw * bh; i++)
    {
        uint32_t p = strip[i];
        uint32_t r = ((p >> 16) & 0xFF) * DARKEN_PCT / 100;
        uint32_t g = ((p >> 8) & 0xFF) * DARKEN_PCT / 100;
        uint32_t b = (p & 0xFF) * DARKEN_PCT / 100;
        strip[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    free(tmp);
    free(dash_backdrop);
    dash_backdrop = strip;
    dash_backdrop_y = dt;
    dash_backdrop_w = bw;
}

typedef enum
{
    ROUND_ALL = 0x0F,
    ROUND_TOP = 0x03,
    ROUND_BOTTOM = 0x0C,
    ROUND_NONE = 0x00,
} round_corners_t;

static inline int in_rounded_rect(int col, int row, int w, int h, int r,
                                  round_corners_t corners)
{
    int tl = corners & 0x01, tr = corners & 0x02;
    int bl = corners & 0x04, br = corners & 0x08;
    if (tl && col < r && row < r)
    {
        int dx = r - col - 1, dy = r - row - 1;
        if (dx * dx + dy * dy >= r * r)
            return 0;
    }
    if (tr && col >= w - r && row < r)
    {
        int dx = col - (w - r), dy = r - row - 1;
        if (dx * dx + dy * dy >= r * r)
            return 0;
    }
    if (bl && col < r && row >= h - r)
    {
        int dx = r - col - 1, dy = row - (h - r);
        if (dx * dx + dy * dy >= r * r)
            return 0;
    }
    if (br && col >= w - r && row >= h - r)
    {
        int dx = col - (w - r), dy = row - (h - r);
        if (dx * dx + dy * dy >= r * r)
            return 0;
    }
    return 1;
}

static inline uint32_t alpha_blend(uint32_t dst, uint32_t src, uint32_t a) {
    uint32_t ia = 255 - a;
    uint32_t r = ((((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * ia) + 128) >> 8;
    uint32_t g = ((((src >>  8) & 0xFF) * a + ((dst >>  8) & 0xFF) * ia) + 128) >> 8;
    uint32_t b = (((src & 0xFF) * a + (dst & 0xFF) * ia) + 128) >> 8;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void bb_rrect_alpha(int x, int y, int w, int h, int r, uint32_t c, uint32_t a)
{
    if (w <= 0 || h <= 0)
        return;
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    for (int row = 0; row < h; row++)
    {
        int ay = y + row;
        if (ay < 0 || (uint32_t)ay >= SCR_H)
            continue;
        int x0 = x, x1 = x + w;
        if (row < r || row >= h - r)
        {
            int dy = row < r ? (r - row - 1) : (row - (h - r));
            int dx = 0;
            while (dx * dx + dy * dy < r * r)
                dx++;
            int cut = r - dx;
            x0 = x + cut;
            x1 = x + w - cut;
        }
        if (x0 < 0)
            x0 = 0;
        if (x1 > (int)SCR_W)
            x1 = (int)SCR_W;
        if (x0 >= x1)
            continue;
        uint32_t *line = backbuf + ay * SCR_W;
        for (int ax = x0; ax < x1; ax++)
            line[ax] = alpha_blend(line[ax], c, a);
    }
}

typedef struct
{
    int active, minimized;
    uint32_t pid;
    int32_t x, y, w, h;
    char title[64], shmname[32], evname[32];
    uint8_t *shmbuf;
    socket_file_t *evsock;
} window_t;

static window_t windows[MAX_WINDOWS];
static int zstack[MAX_WINDOWS], zcount = 0;

static fb_info_t fb;
static ssfn_t ssfn_ctx;
static ssfn_buf_t ssfn_buf;

static int focused_win = -1;
static int drag_win = -1;
static int drag_start_x = 0, drag_start_y = 0;
static int drag_base_x = 0, drag_base_y = 0;
static int dragging = 0;

static socket_file_t *ev_sock = NULL;
static int kbd_fd = -1;
static int mouse_fd = -1;
static uint32_t pointer_x = 0;
static uint32_t pointer_y = 0;
static uint8_t pointer_btn = 0;
static int tab_pressed = 0;
static uint32_t key_modifiers = 0;

#define DTILE 32
#define MAX_DTX 80
#define MAX_DTY 60
static uint8_t dirty[MAX_DTY][MAX_DTX];
static int dtx_count, dty_count, dirty_full = 1;

static void dirty_mark(int x, int y, int w, int h)
{
    if (dirty_full)
        return;
    if (w <= 0 || h <= 0)
        return;
    int tx0 = x / DTILE;
    if (tx0 < 0)
        tx0 = 0;
    int ty0 = y / DTILE;
    if (ty0 < 0)
        ty0 = 0;
    int tx1 = (x + w - 1) / DTILE;
    if (tx1 >= dtx_count)
        tx1 = dtx_count - 1;
    int ty1 = (y + h - 1) / DTILE;
    if (ty1 >= dty_count)
        ty1 = dty_count - 1;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++)
            dirty[ty][tx] = 1;
}
static void dirty_all(void) { dirty_full = 1; }

static void z_remove(int i)
{
    for (int j = 0; j < zcount; j++)
    {
        if (zstack[j] != i)
            continue;
        for (int k = j; k < zcount - 1; k++)
            zstack[k] = zstack[k + 1];
        zcount--;
        return;
    }
}
static void z_raise(int i)
{
    z_remove(i);
    if (zcount < MAX_WINDOWS)
        zstack[zcount++] = i;
}
static int z_top(void)
{
    for (int i = zcount - 1; i >= 0; i--)
        if (windows[zstack[i]].active && !windows[zstack[i]].minimized)
            return zstack[i];
    return -1;
}

static inline void bb_px(int x, int y, uint32_t c)
{
    if ((uint32_t)x < SCR_W && (uint32_t)y < SCR_H)
        backbuf[y * SCR_W + x] = c;
}

static void bb_rect(int x, int y, int w, int h, uint32_t c)
{
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + w > (int)SCR_W ? (int)SCR_W : x + w;
    int y2 = y + h > (int)SCR_H ? (int)SCR_H : y + h;
    for (int row = y1; row < y2; row++)
    {
        uint32_t *l = backbuf + row * SCR_W;
        for (int col = x1; col < x2; col++)
            l[col] = c;
    }
}

static void bb_hline(int x, int y, int w, uint32_t c)
{
    for (int i = x; i < x + w; i++)
        bb_px(i, y, c);
}

static void bb_vline(int x, int y, int h, uint32_t c)
{
    for (int i = y; i < y + h; i++)
        bb_px(x, i, c);
}

static void bb_rrect_ex(int x, int y, int w, int h, int r, uint32_t c, round_corners_t corners)
{
    if (w <= 0 || h <= 0)
        return;
    if (r < 1 || corners == ROUND_NONE)
    {
        bb_rect(x, y, w, h, c);
        return;
    }
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    int tl = corners & 0x01, tr = corners & 0x02;
    int bl = corners & 0x04, br = corners & 0x08;
    for (int row = 0; row < h; row++)
    {
        int ay = y + row;
        if (ay < 0 || (uint32_t)ay >= SCR_H)
            continue;
        int x0 = x, x1 = x + w;
        if (row < r)
        {
            int dy = r - row - 1;
            int dx = 0;
            while (dx * dx + dy * dy < r * r)
                dx++;
            int cut = r - dx;
            if (tl)
                x0 = x + cut;
            if (tr)
                x1 = x + w - cut;
        }
        else if (row >= h - r)
        {
            int dy = row - (h - r);
            int dx = 0;
            while (dx * dx + dy * dy < r * r)
                dx++;
            int cut = r - dx;
            if (bl)
                x0 = x + cut;
            if (br)
                x1 = x + w - cut;
        }
        if (x0 < 0)
            x0 = 0;
        if (x1 > (int)SCR_W)
            x1 = (int)SCR_W;
        if (x0 >= x1)
            continue;
        uint32_t *line = backbuf + ay * SCR_W;
        for (int ax = x0; ax < x1; ax++)
            line[ax] = c;
    }
}

static void bb_rrect(int x, int y, int w, int h, int r, uint32_t c)
{
    bb_rrect_ex(x, y, w, h, r, c, ROUND_ALL);
}

static void bb_rrect_outline(int x, int y, int w, int h, int r, uint32_t c)
{
    if (w <= 0 || h <= 0)
        return;
    if (r < 1)
    {
        bb_hline(x, y, w, c);
        bb_hline(x, y + h - 1, w, c);
        bb_vline(x, y, h, c);
        bb_vline(x + w - 1, y, h, c);
        return;
    }
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    bb_hline(x + r, y, w - 2 * r, c);
    bb_hline(x + r, y + h - 1, w - 2 * r, c);
    bb_vline(x, y + r, h - 2 * r, c);
    bb_vline(x + w - 1, y + r, h - 2 * r, c);
    for (int dy = 0; dy < r; dy++)
    {
        int dx = r - 1;
        while (dx * dx + dy * dy > r * r)
            dx--;
        int inner = r - 1;
        while (inner >= 0 && inner * inner + dy * dy > (r - 1) * (r - 1))
            inner--;
        for (int i = inner + 1; i <= dx; i++)
        {
            bb_px(x + r - 1 - i, y + r - 1 - dy, c);
            bb_px(x + w - r + i, y + r - 1 - dy, c);
            bb_px(x + r - 1 - i, y + h - r + dy, c);
            bb_px(x + w - r + i, y + h - r + dy, c);
        }
    }
}

static void bb_text(int x, int y, uint32_t fg, uint32_t bg, const char *str)
{
    if (!str || !*str)
        return;
    ssfn_select(&ssfn_ctx, SSFN_FAMILY_ANY, NULL, SSFN_STYLE_REGULAR, FONT_SIZE);
    ssfn_buf.ptr = (uint8_t *)backbuf;
    ssfn_buf.w = (int)SCR_W;
    ssfn_buf.h = (int)SCR_H;
    ssfn_buf.p = (int)(SCR_W * 4);
    ssfn_buf.x = x;
    ssfn_buf.y = y;
    ssfn_buf.fg = fg;
    ssfn_buf.bg = bg;
    const char *s = str;
    while (*s && ssfn_buf.x < (int)SCR_W)
    {
        int r = ssfn_render(&ssfn_ctx, &ssfn_buf, s);
        if (r <= 0)
            break;
        s += r;
    }
}

static int text_w(const char *str)
{
    ssfn_select(&ssfn_ctx, SSFN_FAMILY_ANY, NULL, SSFN_STYLE_REGULAR, FONT_SIZE);
    ssfn_buf_t d;
    memset(&d, 0, sizeof(d));
    d.ptr = NULL;
    d.w = 0x7FFF;
    d.h = 0x7FFF;
    d.p = 0x7FFE;
    d.x = 0;
    d.y = FONT_SIZE;
    d.fg = 0xFFFFFFFF;
    const char *s = str;
    while (*s)
    {
        int r = ssfn_render(&ssfn_ctx, &d, s);
        if (r <= 0)
            break;
        s += r;
    }
    return d.x;
}

static void draw_x(int cx, int cy, int sz, uint32_t c)
{
    int x1 = cx + 3, y1 = cy + 3, x2 = cx + sz - 4, len = x2 - x1;
    if (len < 0)
        return;
    for (int i = 0; i <= len; i++)
    {
        bb_px(x1 + i, y1 + i, c);
        bb_px(x1 + i + 1, y1 + i, c);
        bb_px(x1 + i, y1 + i + 1, c);
        bb_px(x2 - i, y1 + i, c);
        bb_px(x2 - i - 1, y1 + i, c);
        bb_px(x2 - i, y1 + i + 1, c);
    }
}

static int dash_area_top(void) { return (int)SCR_H - DASH_MARGIN - DASH_H; }

static void draw_desktop(void)
{
    uint32_t n = SCR_W * SCR_H;
    if (bgbuf)
    {
        memcpy(backbuf, bgbuf, n * 4);
    }
    else
    {
        for (uint32_t i = 0; i < n; i++)
            backbuf[i] = C_BG;
    }
}

static void draw_window(int idx)
{
    window_t *w = &windows[idx];
    if (!w->active || w->minimized)
        return;
    if (w->w <= 0 || w->h <= 0)
        return;

    int fx = w->x, fy = w->y, fw = w->w, fh = w->h + TITLEBAR_H;
    if (fx >= (int)SCR_W || fy >= (int)SCR_H || fx + fw <= 0 || fy + fh <= 0)
        return;

    int focused = (idx == focused_win);
    uint32_t tc = focused ? C_TITLEBAR_F : C_TITLEBAR;

    bb_rrect_ex(fx, fy, fw, TITLEBAR_H, WIN_R, tc, ROUND_TOP);

    if (w->shmbuf)
        bb_rect(fx, fy + TITLEBAR_H, fw, w->h, 0xFF0D0D0D);

    char lbl[48];
    strncpy(lbl, w->title, 47);
    lbl[47] = 0;
    int tw = text_w(lbl);
    int lx = fx + (fw - tw) / 2;
    if (lx < fx + 4)
        lx = fx + 4;
    bb_text(lx, BASELINE(fy + 5), focused ? C_TITLE_ACT : C_TITLE_DIM, tc, lbl);

    int bsz = 14, bcx = fx + fw - bsz - 5, bcy = fy + (TITLEBAR_H - bsz) / 2;
    bb_rrect(bcx, bcy, bsz, bsz, 3, focused ? C_CLOSE_F : C_CLOSE);
    draw_x(bcx, bcy, bsz, C_WHITE);

    if (w->shmbuf)
    {
        uint32_t *src = (uint32_t *)w->shmbuf;
        int iy = fy + TITLEBAR_H;
        for (int row = 0; row < w->h; row++)
        {
            int dy = iy + row;
            if (dy < 0)
                continue;
            if ((uint32_t)dy >= SCR_H)
                break;
            int sx0 = 0, dx0 = fx;
            if (dx0 < 0)
            {
                sx0 = -dx0;
                dx0 = 0;
            }
            int copy = w->w - sx0;
            if (dx0 + copy > (int)SCR_W)
                copy = (int)SCR_W - dx0;
            if (copy <= 0)
                continue;
            uint32_t *dst_row = backbuf + dy * SCR_W + dx0;
            uint32_t *bg_row = bgbuf + dy * SCR_W + dx0;
            uint32_t *src_row = src + row * w->w + sx0;
            for (int i = 0; i < copy; i++)
                dst_row[i] = alpha_blend(bg_row[i], src_row[i], 230);
        }
        bb_rrect_outline(fx, fy + TITLEBAR_H, fw, w->h, WIN_R, 0xFF1E1E1E);
    }

    dirty_mark(fx, fy, fw, fh);
}

static void dash_blit_backdrop(int x, int w)
{
    if (!dash_backdrop)
        return;
    int dt = dash_area_top();
    int r = DASH_R;
    if (r > w / 2)
        r = w / 2;
    if (r > DASH_H / 2)
        r = DASH_H / 2;
    for (int row = 0; row < DASH_H; row++)
    {
        int sy = dt + row;
        if (sy < 0 || (uint32_t)sy >= SCR_H)
            continue;
        int x0 = x, x1 = x + w;
        if (row < r || row >= DASH_H - r)
        {
            int dy = row < r ? (r - row - 1) : (row - (DASH_H - r));
            int dx = 0;
            while (dx * dx + dy * dy < r * r)
                dx++;
            int cut = r - dx;
            x0 = x + cut;
            x1 = x + w - cut;
        }
        if (x0 < 0)
            x0 = 0;
        if (x1 > (int)SCR_W)
            x1 = (int)SCR_W;
        if (x0 >= x1)
            continue;
        memcpy(backbuf + sy * SCR_W + x0,
               dash_backdrop + row * dash_backdrop_w + x0,
               (x1 - x0) * 4);
    }
}

static void draw_dash(void)
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
    int ty_center = ly + (DASH_H - FONT_SIZE) / 2;
    uint32_t clock_bg = backbuf[(ly + DASH_H / 2) * SCR_W + lx + LEFT_W / 2];
    bb_text(lx + (LEFT_W - tw) / 2, BASELINE(ty_center), C_CLOCK, clock_bg, tbuf);

    dirty_mark(lx, ly, LEFT_W, DASH_H);

    int btn_count = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active)
            btn_count++;
    if (btn_count == 0)
        return;

    int total_w = btn_count * (BTN_W + BTN_GAP) - BTN_GAP + DASH_PAD * 2;
    int rx = (int)SCR_W - DASH_MARGIN - total_w;
    if (rx < lx + LEFT_W + DASH_MARGIN)
        rx = lx + LEFT_W + DASH_MARGIN;
    int ry = dt;

    dash_blit_backdrop(rx, total_w);
    bb_rrect_alpha(rx, ry, total_w, DASH_H, DASH_R, C_DASH, TILE_ALPHA);

    int bx = rx + DASH_PAD;
    int top = z_top();
    for (int i = 0; i < MAX_WINDOWS; i++)
    {
        if (!windows[i].active)
            continue;
        if (bx + BTN_W > rx + total_w - DASH_PAD)
            break;
        int is_top = (i == top);
        int by = ry + (DASH_H - BTN_H) / 2;
        uint32_t btn_c = is_top ? C_BTN_F : C_BTN;
        bb_rrect_alpha(bx, by, BTN_W, BTN_H, BTN_R, btn_c, TILE_ALPHA);
        char lbl[16];
        strncpy(lbl, windows[i].title, 15);
        lbl[15] = 0;
        int ltw = text_w(lbl);
        int tx = bx + (BTN_W - ltw) / 2;
        int btn_tc = ly + (DASH_H - FONT_SIZE) / 2;
        uint32_t txt_bg = backbuf[(by + BTN_H / 2) * SCR_W + bx + BTN_W / 2];
        bb_text(tx, BASELINE(btn_tc), is_top ? C_BTN_TXT_F : C_BTN_TXT, txt_bg, lbl);
        bx += BTN_W + BTN_GAP;
    }

    dirty_mark(rx, ry, total_w, DASH_H);
}

static void draw_drag_outline(void)
{
    if (!dragging || drag_win < 0)
        return;
    window_t *w = &windows[drag_win];
    if (!w->active)
        return;
    bb_rrect_outline(w->x, w->y, w->w, w->h + TITLEBAR_H, WIN_R, C_DRAG);
    dirty_mark(w->x, w->y, w->w, w->h + TITLEBAR_H);
}

#define CUR_W 12
#define CUR_H 19
static const uint8_t cursor_shape[CUR_H][CUR_W] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0},
    {1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1},
    {1, 2, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0},
    {1, 2, 2, 1, 0, 1, 2, 2, 1, 0, 0, 0},
    {1, 2, 1, 0, 0, 1, 2, 2, 1, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0},
    {1, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0},
};
static int cur_sx = -1, cur_sy = -1;

static void cursor_restore_from_backbuf(void)
{
    if (cur_sx < 0)
        return;
    uint32_t *dst = (uint32_t *)(uintptr_t)fb.addr;
    for (int cy = 0; cy < CUR_H; cy++)
    {
        int fy = cur_sy + cy;
        if (fy < 0 || (uint32_t)fy >= SCR_H)
            continue;
        for (int cx = 0; cx < CUR_W; cx++)
        {
            int fx = cur_sx + cx;
            if (fx < 0 || (uint32_t)fx >= SCR_W)
                continue;
            if (cursor_shape[cy][cx])
                dst[fy * SCR_W + fx] = backbuf[fy * SCR_W + fx];
        }
    }
    cur_sx = -1;
}

static void cursor_stamp(int mx, int my)
{
    uint32_t *dst = (uint32_t *)(uintptr_t)fb.addr;
    cur_sx = mx;
    cur_sy = my;
    for (int cy = 0; cy < CUR_H; cy++)
    {
        int fy = my + cy;
        if (fy < 0 || (uint32_t)fy >= SCR_H)
            continue;
        for (int cx = 0; cx < CUR_W; cx++)
        {
            int fx = mx + cx;
            if (fx < 0 || (uint32_t)fx >= SCR_W)
                continue;
            uint8_t s = cursor_shape[cy][cx];
            if (!s)
                continue;
            dst[fy * SCR_W + fx] = (s == 1) ? 0xFF000000 : 0xFFFFFFFF;
        }
    }
}

static void push_to_fb(void)
{
    uint32_t mx = pointer_x, my = pointer_y;
    uint32_t *dst = (uint32_t *)(uintptr_t)fb.addr;
    cursor_restore_from_backbuf();
    if (dirty_full)
    {
        uint32_t n = SCR_W * SCR_H;
        for (uint32_t i = 0; i < n; i++)
            dst[i] = backbuf[i];
        dirty_full = 0;
        memset(dirty, 0, sizeof(dirty));
    }
    else
    {
        for (int ty = 0; ty < dty_count; ty++)
        {
            for (int tx = 0; tx < dtx_count; tx++)
            {
                if (!dirty[ty][tx])
                    continue;
                dirty[ty][tx] = 0;
                int px = tx * DTILE, py = ty * DTILE;
                int pw = px + DTILE > (int)SCR_W ? (int)SCR_W - px : DTILE;
                int ph = py + DTILE > (int)SCR_H ? (int)SCR_H - py : DTILE;
                for (int row = 0; row < ph; row++)
                {
                    int y = py + row;
                    memcpy(dst + y * SCR_W + px, backbuf + y * SCR_W + px, pw * 4);
                }
            }
        }
    }
    cursor_stamp((int)mx, (int)my);
}

static void full_redraw(void)
{
    dirty_all();
    draw_desktop();
    for (int i = 0; i < zcount; i++)
    {
        if (dragging && zstack[i] == drag_win)
            continue;
        draw_window(zstack[i]);
    }
    if (dragging)
        draw_drag_outline();
    draw_dash();
    push_to_fb();
}

static int win_at(int x, int y)
{
    for (int i = zcount - 1; i >= 0; i--)
    {
        int idx = zstack[i];
        window_t *w = &windows[idx];
        if (!w->active || w->minimized)
            continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h + TITLEBAR_H)
            return idx;
    }
    return -1;
}

static int dash_btn_at(int x, int y)
{
    int dt = dash_area_top(), ry = dt;
    int btn_count = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active)
            btn_count++;
    if (btn_count == 0)
        return -1;
    int total_w = btn_count * (BTN_W + BTN_GAP) - BTN_GAP + DASH_PAD * 2;
    int rx = (int)SCR_W - DASH_MARGIN - total_w;
    if (x < rx || x >= rx + total_w || y < ry || y >= ry + DASH_H)
        return -1;
    int bx = rx + DASH_PAD;
    for (int i = 0; i < MAX_WINDOWS; i++)
    {
        if (!windows[i].active)
            continue;
        if (x >= bx && x < bx + BTN_W)
            return i;
        bx += BTN_W + BTN_GAP;
    }
    return -1;
}

static void clamp_window(window_t *w)
{
    if (w->w <= 0 || w->h <= 0)
        return;
    int limit_y = dash_area_top() - (w->h + TITLEBAR_H);
    if (limit_y < 0)
        limit_y = 0;
    if (w->y > limit_y)
        w->y = limit_y;
    if (w->y < 0)
        w->y = 0;
    if (w->x + w->w < 8)
        w->x = 8 - w->w;
    if (w->x > (int)SCR_W - 8)
        w->x = (int)SCR_W - 8;
}

static int pending_redraw = 0;

static void send_window_event(window_t *w, const harp_event_t *event)
{
    if (!w || !w->active || !w->evsock || !event)
        return;
    socket_write(w->evsock, event, sizeof(*event));
}

static void set_focused_window(int idx)
{
    if (idx >= 0 && (!windows[idx].active || windows[idx].minimized))
        idx = -1;
    if (focused_win == idx)
        return;

    int prev = focused_win;
    focused_win = idx;

    if (prev >= 0 && windows[prev].active && windows[prev].evsock)
    {
        harp_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = HARP_EVENT_BLUR;
        send_window_event(&windows[prev], &event);
    }

    if (idx >= 0 && windows[idx].active && windows[idx].evsock)
    {
        harp_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = HARP_EVENT_FOCUS;
        send_window_event(&windows[idx], &event);
    }
}

static int client_window_at(int x, int y)
{
    for (int i = zcount - 1; i >= 0; i--)
    {
        int idx = zstack[i];
        window_t *w = &windows[idx];
        if (!w->active || w->minimized)
            continue;
        if (x < w->x || x >= w->x + w->w)
            continue;
        if (y < w->y + TITLEBAR_H || y >= w->y + TITLEBAR_H + w->h)
            continue;
        return idx;
    }
    return -1;
}

static uint32_t current_key_modifiers(void)
{
    return key_modifiers;
}

static void update_key_modifiers(uint16_t code, int32_t value)
{
    switch (code)
    {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
        if (value)
            key_modifiers |= HARP_MOD_SHIFT;
        else
            key_modifiers &= ~HARP_MOD_SHIFT;
        break;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
        if (value)
            key_modifiers |= HARP_MOD_CTRL;
        else
            key_modifiers &= ~HARP_MOD_CTRL;
        break;
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
        if (value)
            key_modifiers |= HARP_MOD_ALT;
        else
            key_modifiers &= ~HARP_MOD_ALT;
        break;
    case KEY_CAPSLOCK:
        if (value == 1)
            key_modifiers ^= HARP_MOD_CAPS;
        break;
    }
}

static int translate_key(uint16_t code, uint32_t modifiers, int32_t value)
{
    if (value == 0)
        return 0;

    int shift = (modifiers & HARP_MOD_SHIFT) != 0;
    int caps = (modifiers & HARP_MOD_CAPS) != 0;

    switch (code)
    {
    case KEY_UP: return KEY_ARROW_UP;
    case KEY_DOWN: return KEY_ARROW_DOWN;
    case KEY_LEFT: return KEY_ARROW_LEFT;
    case KEY_RIGHT: return KEY_ARROW_RIGHT;
    case KEY_ENTER: return '\n';
    case KEY_BACKSPACE: return '\b';
    case KEY_ESC: return 27;
    case KEY_TAB: return '\t';
    case KEY_SPACE: return ' ';
    case KEY_A: return (shift != caps) ? 'A' : 'a';
    case KEY_B: return (shift != caps) ? 'B' : 'b';
    case KEY_C: return (shift != caps) ? 'C' : 'c';
    case KEY_D: return (shift != caps) ? 'D' : 'd';
    case KEY_E: return (shift != caps) ? 'E' : 'e';
    case KEY_F: return (shift != caps) ? 'F' : 'f';
    case KEY_G: return (shift != caps) ? 'G' : 'g';
    case KEY_H: return (shift != caps) ? 'H' : 'h';
    case KEY_I: return (shift != caps) ? 'I' : 'i';
    case KEY_J: return (shift != caps) ? 'J' : 'j';
    case KEY_K: return (shift != caps) ? 'K' : 'k';
    case KEY_L: return (shift != caps) ? 'L' : 'l';
    case KEY_M: return (shift != caps) ? 'M' : 'm';
    case KEY_N: return (shift != caps) ? 'N' : 'n';
    case KEY_O: return (shift != caps) ? 'O' : 'o';
    case KEY_P: return (shift != caps) ? 'P' : 'p';
    case KEY_Q: return (shift != caps) ? 'Q' : 'q';
    case KEY_R: return (shift != caps) ? 'R' : 'r';
    case KEY_S: return (shift != caps) ? 'S' : 's';
    case KEY_T: return (shift != caps) ? 'T' : 't';
    case KEY_U: return (shift != caps) ? 'U' : 'u';
    case KEY_V: return (shift != caps) ? 'V' : 'v';
    case KEY_W: return (shift != caps) ? 'W' : 'w';
    case KEY_X: return (shift != caps) ? 'X' : 'x';
    case KEY_Y: return (shift != caps) ? 'Y' : 'y';
    case KEY_Z: return (shift != caps) ? 'Z' : 'z';
    case KEY_1: return shift ? '!' : '1';
    case KEY_2: return shift ? '@' : '2';
    case KEY_3: return shift ? '#' : '3';
    case KEY_4: return shift ? '$' : '4';
    case KEY_5: return shift ? '%' : '5';
    case KEY_6: return shift ? '^' : '6';
    case KEY_7: return shift ? '&' : '7';
    case KEY_8: return shift ? '*' : '8';
    case KEY_9: return shift ? '(' : '9';
    case KEY_0: return shift ? ')' : '0';
    case KEY_MINUS: return shift ? '_' : '-';
    case KEY_EQUAL: return shift ? '+' : '=';
    case KEY_LEFTBRACE: return shift ? '{' : '[';
    case KEY_RIGHTBRACE: return shift ? '}' : ']';
    case KEY_BACKSLASH: return shift ? '|' : '\\';
    case KEY_SEMICOLON: return shift ? ':' : ';';
    case KEY_APOSTROPHE: return shift ? '"' : '\'';
    case KEY_GRAVE: return shift ? '~' : '`';
    case KEY_COMMA: return shift ? '<' : ',';
    case KEY_DOT: return shift ? '>' : '.';
    case KEY_SLASH: return shift ? '?' : '/';
    default: return 0;
    }
}

static void send_mouse_event(int idx, uint16_t type, uint16_t code, int32_t value, int x, int y)
{
    if (idx < 0 || idx >= MAX_WINDOWS)
        return;
    window_t *w = &windows[idx];
    if (!w->active || w->minimized)
        return;
    harp_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;
    event.x = x - w->x;
    event.y = y - (w->y + TITLEBAR_H);
    event.modifiers = current_key_modifiers();
    send_window_event(w, &event);
}

static void send_key_event(int idx, uint16_t code, int32_t value)
{
    if (idx < 0 || idx >= MAX_WINDOWS)
        return;
    window_t *w = &windows[idx];
    if (!w->active || w->minimized)
        return;
    harp_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = HARP_EVENT_KEY;
    event.code = code;
    event.value = value;
    event.modifiers = current_key_modifiers();
    event.key = translate_key(code, event.modifiers, value);
    send_window_event(w, &event);
}

static void dirty_blit_window(int idx)
{
    window_t *w = &windows[idx];
    if (!w->active || w->minimized || !w->shmbuf)
        return;
    (void)idx;
    pending_redraw = 1;
}

static void poll_events(void)
{
    if (!ev_sock)
        return;
    wm_msg_t msg;
    uint32_t got = 0;
    while (socket_available(ev_sock) >= sizeof(msg))
    {
        socket_read(ev_sock, &msg, sizeof(msg), &got);
        if (got < sizeof(msg))
            break;

        if (msg.type == WM_MSG_REGISTER)
        {
            if (msg.w <= 0 || msg.h <= 0)
                continue;
            int slot = -1;
            for (int i = 0; i < MAX_WINDOWS; i++)
                if (!windows[i].active)
                {
                    slot = i;
                    break;
                }
            if (slot < 0)
                continue;
            window_t *w = &windows[slot];
            memset(w, 0, sizeof(*w));
            w->active = 1;
            w->pid = msg.pid;
            w->x = msg.x;
            w->y = msg.y;
            w->w = msg.w;
            w->h = msg.h;
            strncpy(w->title, msg.title, 63);
            w->title[63] = 0;
            snprintf(w->shmname, sizeof(w->shmname), "wm:shm_%u", msg.pid);
            snprintf(w->evname, sizeof(w->evname), "wm:ev_%u", msg.pid);
            shm_info_t si;
            if (zen_shm_open(w->shmname, &si) == 0 && si.addr && si.size >= (uint64_t)(msg.w * msg.h * 4))
                w->shmbuf = (uint8_t *)si.addr;
            socket_open(w->evname, &w->evsock);
            clamp_window(w);
            z_raise(slot);
            set_focused_window(slot);
            full_redraw();
        }
        else if (msg.type == WM_MSG_DIRTY)
        {
            for (int i = 0; i < MAX_WINDOWS; i++)
            {
                if (!windows[i].active || windows[i].pid != msg.pid)
                    continue;
                dirty_blit_window(i);
                push_to_fb();
                break;
            }
        }
        else if (msg.type == WM_MSG_UNREGISTER)
        {
            for (int i = 0; i < MAX_WINDOWS; i++)
            {
                if (!windows[i].active || windows[i].pid != msg.pid)
                    continue;
                if (windows[i].evsock)
                    socket_close(windows[i].evsock);
                windows[i].evsock = NULL;
                socket_delete(windows[i].evname);
                zen_shm_close(windows[i].shmname);
                windows[i].active = 0;
                windows[i].shmbuf = NULL;
                if (drag_win == i)
                {
                    drag_win = -1;
                    dragging = 0;
                }
                if (focused_win == i)
                    focused_win = -1;
                z_remove(i);
                set_focused_window(z_top());
                full_redraw();
                break;
            }
        }
    }
}

static void clamp_pointer(void)
{
    if (pointer_x >= SCR_W)
        pointer_x = SCR_W ? SCR_W - 1 : 0;
    if (pointer_y >= SCR_H)
        pointer_y = SCR_H ? SCR_H - 1 : 0;
}

static void pump_keyboard_events(void)
{
    if (kbd_fd < 0)
        return;

    int avail = 0;
    if (zen_ioctl(kbd_fd, ZEN_FIONREAD, &avail) < 0)
        return;

    while (avail >= (int)sizeof(struct input_event))
    {
        struct input_event event;
        int n = read(kbd_fd, &event, sizeof(event));
        if (n != (int)sizeof(event))
            break;
        if (event.type == EV_KEY)
        {
            update_key_modifiers((uint16_t)event.code, event.value);
            if (event.code == KEY_TAB && event.value == 1 &&
                (current_key_modifiers() & HARP_MOD_ALT))
                tab_pressed = 1;
            else if (focused_win >= 0)
                send_key_event(focused_win, (uint16_t)event.code, event.value);
        }
        avail -= (int)sizeof(event);
    }
}

static void pump_mouse_events(void)
{
    if (mouse_fd < 0)
        return;

    int avail = 0;
    if (zen_ioctl(mouse_fd, ZEN_FIONREAD, &avail) < 0)
        return;

    while (avail >= (int)sizeof(struct input_event))
    {
        struct input_event event;
        int n = read(mouse_fd, &event, sizeof(event));
        if (n != (int)sizeof(event))
            break;

        if (event.type == EV_REL)
        {
            if (event.code == REL_X)
            {
                int32_t next_x = (int32_t)pointer_x + event.value;
                if (next_x < 0) next_x = 0;
                if (next_x >= (int32_t)SCR_W) next_x = (int32_t)SCR_W - 1;
                pointer_x = (uint32_t)next_x;
            }
            else if (event.code == REL_Y)
            {
                int32_t next_y = (int32_t)pointer_y + event.value;
                if (next_y < 0) next_y = 0;
                if (next_y >= (int32_t)SCR_H) next_y = (int32_t)SCR_H - 1;
                pointer_y = (uint32_t)next_y;
            }
            clamp_pointer();
        }
        else if (event.type == EV_KEY)
        {
            if (event.code == BTN_LEFT)
            {
                if (event.value)
                    pointer_btn |= 1;
                else
                    pointer_btn &= (uint8_t)~1u;
            }
            else if (event.code == BTN_MIDDLE)
            {
                if (event.value)
                    pointer_btn |= 2;
                else
                    pointer_btn &= (uint8_t)~2u;
            }
            else if (event.code == BTN_RIGHT)
            {
                if (event.value)
                    pointer_btn |= 4;
                else
                    pointer_btn &= (uint8_t)~4u;
            }
        }

        avail -= (int)sizeof(event);
    }
}

int main(void)
{
    if (zen_fbinfo(&fb) < 0)
        return 1;
    SCR_W = (uint32_t)fb.width;
    SCR_H = (uint32_t)fb.height;
    if (SCR_W == 0 || SCR_H == 0)
        return 1;
    pointer_x = SCR_W / 2;
    pointer_y = SCR_H / 2;
    pointer_btn = 0;

    dtx_count = ((int)SCR_W + DTILE - 1) / DTILE;
    if (dtx_count > MAX_DTX)
        dtx_count = MAX_DTX;
    dty_count = ((int)SCR_H + DTILE - 1) / DTILE;
    if (dty_count > MAX_DTY)
        dty_count = MAX_DTY;

    backbuf = (uint32_t *)malloc(SCR_W * SCR_H * 4);
    if (!backbuf)
        return 1;

    memset(windows, 0, sizeof(windows));
    zcount = 0;

    socket_delete(WM_SOCK);
    if (socket_create(WM_SOCK) < 0)
    {
        free(backbuf);
        return 1;
    }
    if (socket_open(WM_SOCK, &ev_sock) < 0 || !ev_sock)
    {
        socket_delete(WM_SOCK);
        free(backbuf);
        return 1;
    }

    bake_bgbuf();
    bake_dash_backdrop();

    memset(&ssfn_ctx, 0, sizeof(ssfn_ctx));
    if (ssfn_load(&ssfn_ctx, &_binary_FreeSansB_sfn_start) != SSFN_OK)
    {
        socket_close(ev_sock);
        socket_delete(WM_SOCK);
        free(backbuf);
        return 1;
    }

    kbd_fd = open("/dev/input/event0", O_RDONLY);
    mouse_fd = open("/dev/input/event1", O_RDONLY);
    if (kbd_fd < 0 || mouse_fd < 0)
    {
        socket_close(ev_sock);
        socket_delete(WM_SOCK);
        free(backbuf);
        return 1;
    }

    full_redraw();

    uint32_t prev_btn = 0;
    int prev_mx = -1, prev_my = -1, clock_tick = 0;

    while (1)
    {
        poll_events();
        pump_keyboard_events();
        pump_mouse_events();
        uint32_t mx = pointer_x, my = pointer_y;
        uint8_t btn = pointer_btn;
        int moved = ((int)mx != prev_mx || (int)my != prev_my);

        if (tab_pressed)
        {
            tab_pressed = 0;
            int start = -1;
            for (int i = 0; i < zcount; i++)
                if (zstack[i] == focused_win)
                {
                    start = i;
                    break;
                }
            for (int n = 1; n < zcount; n++)
            {
                int i = (start + n) % zcount;
                int idx = zstack[i];
                if (windows[idx].active && !windows[idx].minimized)
                {
                    z_raise(idx);
                    set_focused_window(idx);
                    full_redraw();
                    break;
                }
            }
        }

        if ((btn & 1) && !(prev_btn & 1))
        {
            drag_win = -1;
            int clicked_win = -1;
            int click_on_titlebar = 0;
            int click_on_close = 0;

            for (int i = zcount - 1; i >= 0; i--)
            {
                int idx = zstack[i];
                window_t *wp = &windows[idx];
                if (!wp->active || wp->minimized)
                    continue;
                int in_win = ((int)mx >= wp->x && (int)mx < wp->x + wp->w &&
                              (int)my >= wp->y && (int)my < wp->y + wp->h + TITLEBAR_H);
                if (!in_win)
                    continue;
                clicked_win = idx;
                click_on_titlebar = ((int)my >= wp->y && (int)my < wp->y + TITLEBAR_H);
                if (click_on_titlebar)
                {
                    int bsz = 14;
                    int bcx = wp->x + wp->w - bsz - 5;
                    int bcy = wp->y + (TITLEBAR_H - bsz) / 2;
                    click_on_close = ((int)mx >= bcx && (int)mx < bcx + bsz &&
                                      (int)my >= bcy && (int)my < bcy + bsz);
                }
                break;
            }

            int db = dash_btn_at((int)mx, (int)my);
            if (db >= 0)
            {
                if (db == focused_win)
                {
                    windows[db].minimized ^= 1;
                    if (windows[db].minimized)
                        set_focused_window(z_top());
                }
                else
                {
                    windows[db].minimized = 0;
                    z_raise(db);
                    set_focused_window(db);
                }
                full_redraw();
            }
            else if (clicked_win >= 0)
            {
                int already_focused = (clicked_win == focused_win);
                z_raise(clicked_win);
                set_focused_window(clicked_win);
                window_t *wp = &windows[clicked_win];
                if (click_on_close && already_focused)
                {
                    zen_kill((int)wp->pid, 9);
                    if (wp->evsock)
                        socket_close(wp->evsock);
                    wp->evsock = NULL;
                    socket_delete(wp->evname);
                    zen_shm_close(wp->shmname);
                    wp->active = 0;
                    wp->shmbuf = NULL;
                    focused_win = -1;
                    z_remove(clicked_win);
                    set_focused_window(z_top());
                }
                else if (click_on_titlebar && already_focused)
                {
                    drag_win = clicked_win;
                    drag_start_x = (int)mx;
                    drag_start_y = (int)my;
                    drag_base_x = wp->x;
                    drag_base_y = wp->y;
                }
                full_redraw();
            }
        }

        if ((btn & 1) && drag_win >= 0 && moved)
        {
            int limit_y = dash_area_top() - TITLEBAR_H;
            int nx = drag_base_x + ((int)mx - drag_start_x);
            int ny = drag_base_y + ((int)my - drag_start_y);
            if (ny < 0)
                ny = 0;
            if (ny > limit_y)
                ny = limit_y;
            windows[drag_win].x = nx;
            windows[drag_win].y = ny;
            dragging = 1;
            dirty_all();
            draw_desktop();
            for (int i = 0; i < zcount; i++)
                if (zstack[i] != drag_win)
                    draw_window(zstack[i]);
            draw_drag_outline();
            draw_dash();
            push_to_fb();
        }

        if (!(btn & 1) && (prev_btn & 1))
        {
            if (drag_win >= 0)
            {
                clamp_window(&windows[drag_win]);
                full_redraw();
            }
            drag_win = -1;
            dragging = 0;
        }

        int client_win = client_window_at((int)mx, (int)my);
        if (!dragging && moved && client_win >= 0)
            send_mouse_event(client_win, HARP_EVENT_MOUSE_MOVE, 0, 0, (int)mx, (int)my);
        if (!dragging && client_win >= 0)
        {
            uint8_t changed = btn ^ prev_btn;
            if (changed & 1)
                send_mouse_event(client_win, HARP_EVENT_MOUSE_BUTTON, BTN_LEFT, (btn & 1) ? 1 : 0, (int)mx, (int)my);
            if (changed & 2)
                send_mouse_event(client_win, HARP_EVENT_MOUSE_BUTTON, BTN_MIDDLE, (btn & 2) ? 1 : 0, (int)mx, (int)my);
            if (changed & 4)
                send_mouse_event(client_win, HARP_EVENT_MOUSE_BUTTON, BTN_RIGHT, (btn & 4) ? 1 : 0, (int)mx, (int)my);
        }

        if (pending_redraw && !dragging)
        {
            pending_redraw = 0;
            full_redraw();
        }

        if (!dragging && ++clock_tick >= 200)
        {
            clock_tick = 0;
            draw_dash();
            push_to_fb();
        }
        else if (!dragging && moved)
        {
            cursor_restore_from_backbuf();
            cursor_stamp((int)mx, (int)my);
        }

        prev_btn = btn;
        prev_mx = (int)mx;
        prev_my = (int)my;
        zen_halt();
    }
    return 0;
}
