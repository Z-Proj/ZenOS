/**
 * 
 * @file : harp_main.c
 * @brief : Harp - Modern, sleek compositor and window manager.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <linux/input.h>

#include "../../userlib.h"
#include "harp_proto.h"
#include "harp_wm.h"
#include "libfont.h"
#include "harp_draw.h"
#include "harp_input.h"
#include "../nk_widgets/stb_image.h"
#include "stb_image_write.h"


#define FONT_SIZE 14
#define BASELINE(top) ((top) + FONT_SIZE)

#define BLUR_R      6
#define DARKEN_PCT 255
#define DASH_H     36
#define DASH_MARGIN 8
#define TITLEBAR_H 28

static font_face_t *s_font;
static uint32_t  *backbuf      = NULL;
static uint32_t  *bgbuf        = NULL;
static uint32_t  *dash_backdrop = NULL;
static int        dash_bd_y    = 0;
static int        dash_bd_w    = 0;
static fb_info_t  fb;
static socket_file_t *ev_sock  = NULL;
static int        pending_full_redraw = 0;
static int        pending_from_z = MAX_WINDOWS;
static int        pending_dash_redraw = 0;
static char* dir;
static char       drive_root[32];
static char       launcher_cfg_path[64];
static char       font_path[64];
static char       screenshot_dir[80];

#define TASK_SNAPSHOT_MAX 128

static const char *path_basename(const char *path)
{
    const char *base = path;
    while (*path) {
        if (*path == '/' || *path == '\\')
            base = path + 1;
        path++;
    }
    return base;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char upper_ascii(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 'A');
    return c;
}

static char *trim_ws(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
    return s;
}

static void set_launcher_label(char out[4], const char *src)
{
    int j = 0;
    while (src && *src && j < 3) {
        char c = *src++;
        if (c == ' ' || c == '\t')
            continue;
        out[j++] = upper_ascii(c);
    }
    if (j == 0) {
        out[0] = '?';
        j = 1;
    }
    out[j] = 0;
}

static uint32_t fallback_launcher_color(const char *seed)
{
    uint32_t h = 2166136261u;
    while (seed && *seed) {
        h ^= (uint8_t)*seed++;
        h *= 16777619u;
    }
    uint32_t r = 32 + ((h >>  0) & 0x3F);
    uint32_t g = 32 + ((h >>  8) & 0x3F);
    uint32_t b = 32 + ((h >> 16) & 0x3F);
    return 0xFF000000U | (r << 16) | (g << 8) | b;
}

static int parse_launcher_color(const char *s, uint32_t *out)
{
    if (!s || !*s)
        return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    uint32_t value = 0;
    int digits = 0;
    while (*s) {
        int v = hex_nibble(*s++);
        if (v < 0)
            return 0;
        value = (value << 4) | (uint32_t)v;
        digits++;
    }
    if (digits == 6) {
        *out = 0xFF000000U | value;
        return 1;
    }
    if (digits == 8) {
        *out = value;
        return 1;
    }
    return 0;
}

static void get_drive_root_from_cwd(void)
{
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(drive_root, "/mnt/drv0");
        return;
    }
    if (strncmp(cwd, "/mnt/drv", 8) != 0) {
        strcpy(drive_root, "/mnt/drv0");
        return;
    }
    char *slash = cwd + 8;
    while (*slash && *slash != '/')
        slash++;
    size_t len = (size_t)(slash - cwd);
    if (len == 0 || len >= sizeof(drive_root)) {
        strcpy(drive_root, "/mnt/drv0");
        return;
    }
    memcpy(drive_root, cwd, len);
    drive_root[len] = 0;
}

static void ensure_launcher_cfg(void)
{
    snprintf(launcher_cfg_path, sizeof(launcher_cfg_path), "%s/sys/harp.cfg", drive_root);
    FILE *fp = fopen(launcher_cfg_path, "r");
    if (fp) {
        fclose(fp);
        return;
    }
    char sys_dir[48];
    snprintf(sys_dir, sizeof(sys_dir), "%s/sys", drive_root);
    mkdir(sys_dir, 0755);
    fp = fopen(launcher_cfg_path, "w");
    if (!fp)
        return;
    fprintf(fp, "%s/bin/terminal; 0xF0F0F0; TRM\n", drive_root);
    fprintf(fp, "%s/bin/imgview; 0x88C0D0; IMG\n", drive_root);
    fprintf(fp, "%s/bin/doom; 0xD08770; DUM\n", drive_root);
    fclose(fp);
}

static void build_font_path(void)
{
    snprintf(font_path, sizeof(font_path), "%s/lib/fonts/default.ttf", drive_root);
}

static void build_screenshot_dir(void)
{
    snprintf(screenshot_dir, sizeof(screenshot_dir), "%s/lib/harp/scrshot", drive_root);
}

static void ensure_screenshot_dir(void)
{
    char lib_dir[48];
    char harp_dir[64];
    snprintf(lib_dir, sizeof(lib_dir), "%s/lib", drive_root);
    snprintf(harp_dir, sizeof(harp_dir), "%s/lib/harp", drive_root);
    mkdir(lib_dir, 0755);
    mkdir(harp_dir, 0755);
    mkdir(screenshot_dir, 0755);
}

static int save_screenshot(void)
{
    ensure_screenshot_dir();

    char path[112];
    struct stat st;
    int slot = 1;
    do {
        snprintf(path, sizeof(path), "%s/scrshot%d.png", screenshot_dir, slot++);
    } while (slot < 100000 && stat(path, &st) == 0);

    uint8_t *rgba = (uint8_t *)malloc((size_t)fb.width * (size_t)fb.height * 4);
    if (!rgba)
        return 0;

    for (uint32_t y = 0; y < fb.height; y++) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)(uintptr_t)fb.addr + (size_t)y * fb.pitch);
        uint8_t *dst = rgba + (size_t)y * (size_t)fb.width * 4;
        for (uint32_t x = 0; x < fb.width; x++) {
            uint32_t p = src[x];
            dst[x * 4 + 0] = (uint8_t)((p >> 16) & 0xFF);
            dst[x * 4 + 1] = (uint8_t)((p >> 8) & 0xFF);
            dst[x * 4 + 2] = (uint8_t)(p & 0xFF);
            dst[x * 4 + 3] = (uint8_t)((p >> 24) & 0xFF);
        }
    }

    int ok = stbi_write_png(path, (int)fb.width, (int)fb.height, 4, rgba, (int)fb.width * 4);
    free(rgba);
    return ok;
}

static void load_launcher_cfg(void)
{
    launcher_app_count = 0;
    memset(launcher_apps, 0, sizeof(launcher_apps));

    FILE *fp = fopen(launcher_cfg_path, "r");
    if (!fp)
        return;

    char line[256];
    while (launcher_app_count < MAX_LAUNCH_APPS && fgets(line, sizeof(line), fp)) {
        char *p = trim_ws(line);
        if (*p == 0 || *p == '#')
            continue;

        char *sep1 = strchr(p, ';');
        if (!sep1)
            continue;
        *sep1++ = 0;
        char *sep2 = strchr(sep1, ';');
        if (!sep2)
            continue;
        *sep2++ = 0;

        char *path = trim_ws(p);
        char *color = trim_ws(sep1);
        char *label = trim_ws(sep2);
        if (*path == 0)
            continue;

        launcher_app_t *app = &launcher_apps[launcher_app_count];
        memset(app, 0, sizeof(*app));
        strncpy(app->path, path, sizeof(app->path) - 1);
        if (!parse_launcher_color(color, &app->color))
            app->color = fallback_launcher_color(app->path);
        if (*label)
            set_launcher_label(app->label, label);
        else
            set_launcher_label(app->label, path_basename(app->path));
        app->active = 1;
        launcher_app_count++;
    }

    fclose(fp);
}

static void launch_launcher_app(int idx)
{
    if (idx < 0 || idx >= launcher_app_count || !launcher_apps[idx].active)
        return;
    char *argv[] = { launcher_apps[idx].path, NULL };
    int pid = zen_spawn(launcher_apps[idx].path, argv);
    if (pid < 0)
        return;
    zen_set_focus(pid);
}

static void full_redraw(void)
{
    pending_full_redraw = 0;
    pending_from_z = MAX_WINDOWS;
    pending_dash_redraw = 0;
    dirty_all();
    draw_desktop();
    for (int i = 0; i < zcount; i++) {
        if (dragging && zstack[i] == drag_win) continue;
        draw_window(zstack[i], 1);
    }
    if (dragging) draw_drag_outline();
    draw_dash();
    push_to_fb((uint32_t *)(uintptr_t)fb.addr, input_ptr_x(), input_ptr_y(), fb.pitch);
}

static int z_pos_of(int idx)
{
    for (int i = 0; i < zcount; i++)
        if (zstack[i] == idx)
            return i;
    return -1;
}

static void request_full_redraw(void)
{
    pending_full_redraw = 1;
    pending_from_z = 0;
    pending_dash_redraw = 1;
}

static void request_window_redraw(int idx, int redraw_dash)
{
    int pos = z_pos_of(idx);
    if (pos < 0) {
        request_full_redraw();
        return;
    }
    if (pending_from_z > pos)
        pending_from_z = pos;
    if (redraw_dash)
        pending_dash_redraw = 1;
}

static void flush_pending_redraw(uint32_t mx, uint32_t my)
{
    if (dragging)
        return;
    if (pending_full_redraw) {
        full_redraw();
        return;
    }
    if (pending_from_z >= zcount)
        return;
    for (int i = pending_from_z; i < zcount; i++)
        draw_window(zstack[i], i != pending_from_z);
    if (pending_dash_redraw)
        draw_dash();
    push_to_fb((uint32_t *)(uintptr_t)fb.addr, mx, my, fb.pitch);
    pending_from_z = MAX_WINDOWS;
    pending_dash_redraw = 0;
}

static void box_blur_h(uint32_t *src, uint32_t *dst, int w, int h, int r)
{
    int diam = 2 * r + 1;
    for (int y = 0; y < h; y++) {
        uint32_t *s = src + y * w, *d = dst + y * w;
        uint32_t sr = 0, sg = 0, sb = 0;
        for (int x = -r; x <= r; x++) {
            int sx = x < 0 ? 0 : (x >= w ? w - 1 : x);
            uint32_t p = s[sx];
            sr += (p >> 16) & 0xFF; sg += (p >> 8) & 0xFF; sb += p & 0xFF;
        }
        for (int x = 0; x < w; x++) {
            d[x] = 0xFF000000 | ((sr / diam) << 16) | ((sg / diam) << 8) | (sb / diam);
            int xl = x - r; if (xl < 0) xl = 0;
            int xr = x + r + 1; if (xr >= w) xr = w - 1;
            uint32_t pl = s[xl], pr = s[xr];
            sr += ((pr >> 16) & 0xFF) - ((pl >> 16) & 0xFF);
            sg += ((pr >>  8) & 0xFF) - ((pl >>  8) & 0xFF);
            sb += (pr & 0xFF) - (pl & 0xFF);
        }
    }
}

static void box_blur_v(uint32_t *src, uint32_t *dst, int w, int h, int r)
{
    int diam = 2 * r + 1;
    for (int x = 0; x < w; x++) {
        uint32_t sr = 0, sg = 0, sb = 0;
        for (int y = -r; y <= r; y++) {
            int sy = y < 0 ? 0 : (y >= h ? h - 1 : y);
            uint32_t p = src[sy * w + x];
            sr += (p >> 16) & 0xFF; sg += (p >> 8) & 0xFF; sb += p & 0xFF;
        }
        for (int y = 0; y < h; y++) {
            dst[y * w + x] = 0xFF000000 | ((sr / diam) << 16) | ((sg / diam) << 8) | (sb / diam);
            int yt = y - r; if (yt < 0) yt = 0;
            int yb = y + r + 1; if (yb >= h) yb = h - 1;
            uint32_t pt = src[yt * w + x], pb = src[yb * w + x];
            sr += ((pb >> 16) & 0xFF) - ((pt >> 16) & 0xFF);
            sg += ((pb >>  8) & 0xFF) - ((pt >>  8) & 0xFF);
            sb += (pb & 0xFF) - (pt & 0xFF);
        }
    }
}

#pragma pack(push,1)
typedef struct {
    uint8_t  id_len;
    uint8_t  cmap_type;
    uint8_t  data_type;
    uint16_t cmap_origin;
    uint16_t cmap_length;
    uint8_t  cmap_depth;
    uint16_t x_origin;
    uint16_t y_origin;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  descriptor;
} tga_hdr_t;
#pragma pack(pop)

static inline uint32_t tga_decode_pixel(const uint8_t *p, int bpp)
{
    if (bpp == 1) {
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
    if (!f) return NULL;

    tga_hdr_t h;
    if (fread(&h, sizeof(h), 1, f) != 1) { fclose(f); return NULL; }
    if (h.id_len) fseek(f, h.id_len, SEEK_CUR);

    uint32_t *cmap = NULL;
    if (h.cmap_type && h.cmap_length > 0) {
        int ce = h.cmap_depth >> 3;
        cmap = (uint32_t *)malloc(h.cmap_length * 4);
        if (cmap) {
            uint8_t ce_buf[4];
            for (int i = 0; i < h.cmap_length; i++) {
                fread(ce_buf, ce, 1, f);
                cmap[i] = tga_decode_pixel(ce_buf, ce);
            }
        } else {
            fseek(f, (long)h.cmap_length * (h.cmap_depth >> 3), SEEK_CUR);
        }
    }

    int iw = (int)h.width, ih = (int)h.height;

    int is_cmap = (h.data_type == 1 || h.data_type == 9);
    int img_bpp = is_cmap ? 1 : (h.bpp >> 3);
    if (iw <= 0 || ih <= 0 || (img_bpp != 1 && img_bpp != 3 && img_bpp != 4)) {
        if (cmap) free(cmap);
        fclose(f);
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
    uint8_t  *row = (uint8_t *)malloc(iw * img_bpp);
    if (!px || !row) { free(px); free(row); if (cmap) free(cmap); fclose(f); return NULL; }

    if (h.data_type == 1 || h.data_type == 2 || h.data_type == 3) {
        long row_bytes = (long)iw * img_bpp;
        for (int y = 0; y < ih; y++) {
            int src_y = flip_v ? (ih - 1 - y) : y;
            if (src_y < src_y0 || src_y > src_y1) { fseek(f, row_bytes, SEEK_CUR); continue; }
            fread(row, img_bpp, iw, f);
            int dst_y = src_y - src_y0;
            for (int x = 0; x < crop_w; x++) {
                uint8_t *p = row + (src_x0 + x) * img_bpp;
                uint32_t c = (is_cmap && cmap) ? cmap[p[0] < h.cmap_length ? p[0] : 0]
                                               : tga_decode_pixel(p, img_bpp);
                px[dst_y * crop_w + x] = c;
            }
        }
    } else if (h.data_type == 9 || h.data_type == 10 || h.data_type == 11) {
        uint32_t *full = (uint32_t *)malloc(iw * ih * 4);
        if (!full) { free(px); free(row); if (cmap) free(cmap); fclose(f); return NULL; }
        int total = iw * ih, i = 0;
        uint8_t tmp[5];
        while (i < total) {
            fread(tmp, 1, 1, f);
            int rep = (tmp[0] & 0x7F) + 1;
            if (tmp[0] & 0x80) {
                fread(tmp + 1, img_bpp, 1, f);
                uint32_t c = (is_cmap && cmap) ? cmap[tmp[1] < h.cmap_length ? tmp[1] : 0]
                                               : tga_decode_pixel(tmp + 1, img_bpp);
                for (int k = 0; k < rep && i < total; k++, i++) {
                    int fy = flip_v ? (ih - 1 - i / iw) : i / iw;
                    full[fy * iw + (i % iw)] = c;
                }
            } else {
                for (int k = 0; k < rep && i < total; k++, i++) {
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
    } else {
        free(px); free(row); if (cmap) free(cmap); fclose(f); return NULL;
    }

    free(row);
    if (cmap) free(cmap);
    fclose(f);
    *out_w = crop_w;
    *out_h = crop_h;
    return px;
}

static uint32_t *image_load(const char *path, int *out_w, int *out_h)
{
    const char *ext = strrchr(path, '.');
    if (ext && (!strcmp(ext, ".tga") || !strcmp(ext, ".TGA")))
        return tga_load(path, out_w, out_h);

    int iw = 0;
    int ih = 0;
    int comp = 0;
    uint8_t *rgba = stbi_load(path, &iw, &ih, &comp, 4);
    if (!rgba)
        return NULL;

    int sw = (int)SCR_W;
    int sh = (int)SCR_H;
    int crop_w = iw < sw ? iw : sw;
    int crop_h = ih < sh ? ih : sh;
    int src_x0 = (iw - crop_w) / 2;
    int src_y0 = (ih - crop_h) / 2;

    uint32_t *px = (uint32_t *)malloc(crop_w * crop_h * 4);
    if (!px) {
        stbi_image_free(rgba);
        return NULL;
    }

    for (int y = 0; y < crop_h; y++) {
        const uint8_t *src = rgba + ((size_t)(src_y0 + y) * (size_t)iw + (size_t)src_x0) * 4;
        uint32_t *dst = px + (size_t)y * (size_t)crop_w;
        for (int x = 0; x < crop_w; x++) {
            const uint8_t *p = src + (size_t)x * 4;
            uint32_t a = p[3];
            uint32_t r = p[0];
            uint32_t g = p[1];
            uint32_t b = p[2];
            if (a != 255) {
                r = (r * a) / 255;
                g = (g * a) / 255;
                b = (b * a) / 255;
            }
            dst[x] = 0xFF000000U | (r << 16) | (g << 8) | b;
        }
    }

    stbi_image_free(rgba);
    *out_w = crop_w;
    *out_h = crop_h;
    return px;
}

static void bake_bgbuf(void)
{
    int bw = 0, bh = 0;
    uint32_t *img = image_load(dir, &bw, &bh);
    bgbuf = (uint32_t *)malloc(SCR_W * SCR_H * 4);
    if (!bgbuf) { if (img) free(img); return; }
    for (uint32_t i = 0; i < SCR_W * SCR_H; i++) bgbuf[i] = 0xFF0E0E0E;
    if (img) {
        int ox = ((int)SCR_W - bw) / 2, oy = ((int)SCR_H - bh) / 2;
        for (int y = 0; y < bh; y++) {
            int dy = oy + y;
            if (dy < 0 || (uint32_t)dy >= SCR_H) continue;
            for (int x = 0; x < bw; x++) {
                int dx = ox + x;
                if (dx < 0 || (uint32_t)dx >= SCR_W) continue;
                bgbuf[dy * SCR_W + dx] = img[y * bw + x] | 0xFF000000;
            }
        }
        free(img);
    }
}

static void bake_dash_backdrop(void)
{
    int dt = (int)SCR_H - DASH_MARGIN - DASH_H;
    int bw = (int)SCR_W, bh = DASH_H;
    if (!bgbuf || dt < 0 || dt + bh > (int)SCR_H) return;
    uint32_t *strip = (uint32_t *)malloc(bw * bh * 4);
    uint32_t *tmp   = (uint32_t *)malloc(bw * bh * 4);
    if (!strip || !tmp) { free(strip); free(tmp); return; }
    for (int y = 0; y < bh; y++)
        memcpy(strip + y * bw, bgbuf + (dt + y) * SCR_W, bw * 4);
    box_blur_h(strip, tmp, bw, bh, BLUR_R);
    box_blur_v(tmp, strip, bw, bh, BLUR_R);
    box_blur_h(strip, tmp, bw, bh, BLUR_R);
    box_blur_v(tmp, strip, bw, bh, BLUR_R);
    for (int i = 0; i < bw * bh; i++) {
        uint32_t p = strip[i];
        uint32_t r = ((p >> 16) & 0xFF) * DARKEN_PCT / 255;
        uint32_t g = ((p >>  8) & 0xFF) * DARKEN_PCT / 255;
        uint32_t b = (p & 0xFF) * DARKEN_PCT / 255;
        strip[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    free(tmp);
    dash_backdrop = strip;
    dash_bd_y     = dt;
    dash_bd_w     = bw;
}

static void poll_ipc(void)
{
    if (!ev_sock) return;
    wm_msg_t msg;
    uint32_t got = 0;
    while (socket_available(ev_sock) >= sizeof(msg)) {
        socket_read(ev_sock, &msg, sizeof(msg), &got);
        if (got < sizeof(msg)) break;

        if (msg.type == WM_MSG_REGISTER) {
            if (msg.w <= 0 || msg.h <= 0) continue;
            int slot = -1;
            for (int i = 0; i < MAX_WINDOWS; i++)
                if (!windows[i].active) { slot = i; break; }
            if (slot < 0) continue;
            window_t *w = &windows[slot];
            memset(w, 0, sizeof(*w));
            w->active = 1;
            w->pid = msg.pid;
            w->x = msg.x; w->y = msg.y;
            w->w = msg.w; w->h = msg.h;
            strncpy(w->title, msg.title, 63); w->title[63] = 0;
            snprintf(w->shmname, sizeof(w->shmname), "wm:shm_%u", msg.pid);
            snprintf(w->evname,  sizeof(w->evname),  "wm:ev_%u",  msg.pid);
            shm_info_t si;
            if (zen_shm_open(w->shmname, &si) == 0 && si.addr &&
                si.size >= (uint64_t)(msg.w * msg.h * 4))
                w->shmbuf = (uint8_t *)si.addr;
            socket_open(w->evname, &w->evsock);
            w->dirty_valid = 0;
            clamp_window(w);
            z_raise(slot);
            set_focused_window(slot);
            request_full_redraw();

        } else if (msg.type == WM_MSG_DIRTY) {
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (!windows[i].active || windows[i].pid != msg.pid) continue;
                mark_window_dirty(&windows[i], msg.x, msg.y, msg.w, msg.h);
                request_window_redraw(i, 0);
                break;
            }

        } else if (msg.type == WM_MSG_RETITLE) {
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (!windows[i].active || windows[i].pid != msg.pid) continue;
                strncpy(windows[i].title, msg.title, 63);
                windows[i].title[63] = 0;
                request_full_redraw();
                break;
            }

        } else if (msg.type == WM_MSG_UNREGISTER) {
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (!windows[i].active || windows[i].pid != msg.pid) continue;
                wm_close_window(i);
                request_full_redraw();
                break;
            }
        }
    }
}

static int task_alive(uint32_t pid)
{
    task_info_t infos[TASK_SNAPSHOT_MAX];
    int count = zen_list_tasks(infos, TASK_SNAPSHOT_MAX);

    if (count <= 0)
        return 0;

    for (int i = 0; i < count; i++) {
        if (infos[i].pid == pid)
            return 1;
    }

    return 0;
}

static void reap_dead_windows(void)
{
    int changed = 0;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active)
            continue;
        if (task_alive(windows[i].pid))
            continue;
        wm_close_window(i);
        changed = 1;
    }

    if (changed)
        request_full_redraw();
}

static void reap_spawned_children(void)
{
    int status = 0;
    while (waitpid(-1, &status, WNOHANG) > 0) {
    }
}

int main(int argc, char* argv[])
{
    dir = "";
    if (argc > 2) {
        printf("Invalid number of arguments passed.\n1 || 2 args expected.");
        return 1;
    }
    if (argc < 2) dir = "/mnt/drv0/lib/harp/bg.png";
    else dir = argv[1];
    get_drive_root_from_cwd();
    build_font_path();
    build_screenshot_dir();
    ensure_launcher_cfg();
    load_launcher_cfg();
    if (zen_fbinfo(&fb) != 0) return 1;
    SCR_W = (uint32_t)fb.width;
    SCR_H = (uint32_t)fb.height;
    if (SCR_W == 0 || SCR_H == 0) return 1;

    backbuf = (uint32_t *)malloc(SCR_W * SCR_H * 4);
    if (!backbuf) return 1;

    wm_init();

    if (socket_create(WM_SOCK) < 0) { free(backbuf); write(STDERR_FILENO, "Harp is already running.\nMultiple instances cannot be run.", 58); return 1; }
    if (socket_open(WM_SOCK, &ev_sock) < 0 || !ev_sock) {
        socket_delete(WM_SOCK);
        free(backbuf);
        return 1;
    }

    bake_bgbuf();
    bake_dash_backdrop();

    s_font = font_load(font_path);
    if (!s_font && strcmp(font_path, "/mnt/drv0/lib/fonts/default.ttf") != 0)
        s_font = font_load("/mnt/drv0/lib/fonts/default.ttf");
    if (!s_font) {
        fprintf(stderr, "harp: failed to load font: %s\n", font_path);
        socket_close(ev_sock); socket_delete(WM_SOCK); free(backbuf);
        return 1;
    }
    font_prime_ascii(s_font, 11);
    font_prime_ascii(s_font, 16);

    draw_init(backbuf, bgbuf, dash_backdrop,
              dash_bd_y, dash_bd_w, s_font);

    int kbd_fd   = open("/dev/input/event0", O_RDONLY);
    int mouse_fd = open("/dev/input/event1", O_RDONLY);
    if (kbd_fd < 0 || mouse_fd < 0) {
        socket_close(ev_sock); socket_delete(WM_SOCK); free(backbuf); return 1;
    }
    input_init(kbd_fd, mouse_fd, SCR_W / 2, SCR_H / 2);

    full_redraw();

    uint32_t prev_btn = 0;
    int prev_mx = -1, prev_my = -1;
    int clock_tick = 0;
    int reap_tick = 0;

    while (1) {
        reap_spawned_children();
        poll_ipc();
        input_pump_keyboard();
        input_pump_mouse();

        uint32_t mx  = input_ptr_x();
        uint32_t my  = input_ptr_y();
        uint8_t  btn = input_ptr_btn();
        uint32_t mods = input_modifiers();
        int moved = ((int)mx != prev_mx || (int)my != prev_my);

        if (input_consume_tab()) {
            int start = -1;
            for (int i = 0; i < zcount; i++)
                if (zstack[i] == focused_win) { start = i; break; }
            for (int n = 1; n < zcount; n++) {
                int i = (start + n) % zcount;
                int idx = zstack[i];
                if (windows[idx].active && !windows[idx].minimized) {
                    z_raise(idx);
                    set_focused_window(idx);
                    request_full_redraw();
                    break;
                }
            }
        }

        if ((btn & 1) && !(prev_btn & 1)) {
            drag_win = -1;
            int clicked = win_at((int)mx, (int)my);
            int sb = screenshot_btn_at((int)mx, (int)my);
            int pb = power_btn_at((int)mx, (int)my);
            int lb = launcher_btn_at((int)mx, (int)my);
            int db = dash_btn_at((int)mx, (int)my);

            if (sb >= 0) {
                save_screenshot();
            } else if (pb >= 0) {
                zen_shutdown();
            } else if (lb >= 0) {
                launch_launcher_app(lb);
            } else if (db >= 0) {
                if (db == focused_win) {
                    windows[db].minimized ^= 1;
                    if (windows[db].minimized) set_focused_window(z_top());
                } else {
                    windows[db].minimized = 0;
                    z_raise(db);
                    set_focused_window(db);
                }
                request_full_redraw();
            } else if (clicked >= 0) {
                int already = (clicked == focused_win);
                z_raise(clicked);
                set_focused_window(clicked);
                request_full_redraw();
                if (close_btn_at(clicked, (int)mx, (int)my) && already) {
                    send_close_req(clicked);
                } else if (titlebar_at(clicked, (int)mx, (int)my) && already) {
                    drag_win     = clicked;
                    drag_start_x = (int)mx; drag_start_y = (int)my;
                    drag_base_x  = windows[clicked].x;
                    drag_base_y  = windows[clicked].y;
                }
            }
        }

        if ((btn & 1) && drag_win >= 0 && moved) {
            int limit_y = dash_area_top() - TITLEBAR_H;
            int nx = drag_base_x + ((int)mx - drag_start_x);
            int ny = drag_base_y + ((int)my - drag_start_y);
            if (ny < 0) ny = 0;
            if (ny > limit_y) ny = limit_y;
            windows[drag_win].x = nx;
            windows[drag_win].y = ny;
            dragging = 1;
            dirty_all();
            draw_desktop();
            for (int i = 0; i < zcount; i++)
                if (zstack[i] != drag_win) draw_window(zstack[i], 1);
            draw_drag_outline();
            draw_dash();
            push_to_fb((uint32_t *)(uintptr_t)fb.addr, mx, my, fb.pitch);
        }

        if (!(btn & 1) && (prev_btn & 1)) {
            if (drag_win >= 0) {
                clamp_window(&windows[drag_win]);
                request_full_redraw();
            }
            drag_win = -1; dragging = 0;
        }

        int cw = client_window_at((int)mx, (int)my);
        if (!dragging && moved && cw >= 0)
            send_mouse_event(cw, HARP_EVENT_MOUSE_MOVE, 0, 0, (int)mx, (int)my, mods);
        if (!dragging && cw >= 0) {
            uint8_t changed = btn ^ prev_btn;
            if (changed & 1) send_mouse_event(cw, HARP_EVENT_MOUSE_BUTTON, BTN_LEFT,   (btn&1)?1:0, (int)mx, (int)my, mods);
            if (changed & 2) send_mouse_event(cw, HARP_EVENT_MOUSE_BUTTON, BTN_MIDDLE, (btn&2)?1:0, (int)mx, (int)my, mods);
            if (changed & 4) send_mouse_event(cw, HARP_EVENT_MOUSE_BUTTON, BTN_RIGHT,  (btn&4)?1:0, (int)mx, (int)my, mods);
        }

        flush_pending_redraw(mx, my);

        if (!dragging && ++clock_tick >= 200) {
            clock_tick = 0;
            draw_dash();
            push_to_fb((uint32_t *)(uintptr_t)fb.addr, mx, my, fb.pitch);
        } else if (!dragging && moved) {
            uint32_t *fb_addr = (uint32_t *)(uintptr_t)fb.addr;
            push_to_fb(fb_addr, mx, my, fb.pitch);
        }

        prev_btn = btn;
        prev_mx  = (int)mx;
        prev_my  = (int)my;

        if (++reap_tick >= 64) {
            reap_tick = 0;
            reap_dead_windows();
        }
    }
    return 0;
}
