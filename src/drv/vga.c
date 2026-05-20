/**
 * 
 * @file : /src/drv/vga.c
 * @brief : Framebuffer and terminal output via flanterm with font switching.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#include <stdint.h>
#include <stddef.h>
#include "../libk/core/mem.h"
#include "../libk/debug/log.h"
#include "../libk/string.h"
#include "../libk/spinlock.h"
#include "../libk/limine.h"
#include "vga.h"
#include "disk/vfs.h"
#include "term/flanterm.h"
#include "term/flanterm_backends/fb.h"
#include "../libk/gfx/font_8x16.h"
#include "../libk/gfx/font1_8x16.h"
#include "../libk/gfx/font2_8x16.h"

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b, 0x9d5827dcd881dd75, 0xa3148604f6fab11b},
    .revision = 0};

static struct limine_framebuffer *fb;
uint8_t *framebuffer_addr;
uint64_t framebuffer_width, framebuffer_height, framebuffer_pitch;
uint8_t framebuffer_bpp;
static spinlock_t vga_lock;
bool flanterm = false;
static struct flanterm_context *ft_ctx = NULL;
uint8_t red_shift = 0, green_shift = 0, blue_shift = 0;
uint8_t red_size = 0, green_size = 0, blue_size = 0;

#pragma pack(push, 1)
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

static void flanterm_kfree_wrapper(void *ptr, size_t size)
{
    (void)size;
    kfree(ptr);
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= framebuffer_width || y >= framebuffer_height)
        return;

    uint32_t offset = y * framebuffer_pitch + x * (framebuffer_bpp / 8);

    switch (framebuffer_bpp)
    {
    case 32:
        *(uint32_t *)(framebuffer_addr + offset) = color;
        break;
    case 24:
        framebuffer_addr[offset] = color & 0xFF;
        framebuffer_addr[offset + 1] = (color >> 8) & 0xFF;
        framebuffer_addr[offset + 2] = (color >> 16) & 0xFF;
        break;
    case 16:
    {
        uint16_t rgb565 = (((color >> 16) & 0xF8) << 8) |
                          (((color >> 8) & 0xFC) << 3) |
                          ((color & 0xF8) >> 3);
        *(uint16_t *)(framebuffer_addr + offset) = rgb565;
        break;
    }
    }
}

static uint32_t blend_argb(uint32_t dst, uint32_t src)
{
    uint32_t a = src >> 24;
    if (a == 0)
        return dst;
    if (a == 255)
        return src;

    uint32_t sr = (src >> 16) & 0xff;
    uint32_t sg = (src >> 8) & 0xff;
    uint32_t sb = src & 0xff;
    uint32_t dr = (dst >> 16) & 0xff;
    uint32_t dg = (dst >> 8) & 0xff;
    uint32_t db = dst & 0xff;
    uint32_t ia = 255 - a;
    return 0xff000000
        | (((sr * a + dr * ia) / 255) << 16)
        | (((sg * a + dg * ia) / 255) << 8)
        |  ((sb * a + db * ia) / 255);
}

static void splash_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= framebuffer_width || y >= framebuffer_height)
        return;
    put_pixel(x, y, blend_argb(get_pixel_at(x, y), color));
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    for (uint32_t yy = 0; yy < h; yy++)
        for (uint32_t xx = 0; xx < w; xx++)
            put_pixel(x + xx, y + yy, color);
}

static void draw_boot_fallback(const char *status)
{
    if (!framebuffer_addr || !framebuffer_width || !framebuffer_height)
        return;
    fill_rect(0, 0, framebuffer_width, framebuffer_height, 0x000000);

    uint32_t cx = (uint32_t)(framebuffer_width / 2);
    uint32_t cy = (uint32_t)(framebuffer_height / 2);

    uint32_t box_w = framebuffer_width > 560 ? 520 : (uint32_t)(framebuffer_width - 40);
    uint32_t box_h = 136;

    uint32_t box_x = cx - box_w / 2;
    uint32_t box_y = cy - box_h / 2;
    fill_rect(box_x, box_y, box_w, box_h, 0xff1b1f24);
    fill_rect(box_x, box_y, box_w, 2, 0xff6ea8c7);
    fill_rect(box_x, box_y + box_h - 1, box_w, 1, 0xff2b3138);
    fill_rect(box_x, box_y, 1, box_h, 0xff2b3138);
    fill_rect(box_x + box_w - 1, box_y, 1, box_h, 0xff2b3138);
    draw_text_at(
        "ZenOS",
        box_x + 32,
        box_y + 34,
        0xfff5f7fa
    );
    draw_text_at(
        "Zen",
        box_x + 33,
        box_y + 34,
        0xfff5f7fa
    );
    draw_text_at(
        "ZenOS",
        box_x + 32,
        box_y + 35,
        0xfff5f7fa
    );
    draw_text_at(
        "Zen",
        box_x + 33,
        box_y + 35,
        0xfff5f7fa
    );
    draw_text_at(
        status ? status : "Booting system...",
        box_x + 32,
        box_y + 74,
        0xffa7b0ba
    );
    uint32_t bar_x = box_x + 32;
    uint32_t bar_y = box_y + 104;
    uint32_t bar_w = box_w - 64;
    fill_rect(bar_x, bar_y, bar_w, 6, 0xff2a3036);
    fill_rect(bar_x, bar_y, bar_w / 2, 6, 0xff7fb4d6);
}

void vga_boot_splash_show(const char *status)
{
    spinlock_acquire(&vga_lock);
    draw_boot_fallback(status);
    spinlock_release(&vga_lock);
}

void vga_boot_splash_status(const char *status)
{
    if (!status)
        return;

    spinlock_acquire(&vga_lock);

    uint32_t box_w = framebuffer_width > 560
        ? 520
        : (uint32_t)(framebuffer_width - 40);

    uint32_t box_x = (uint32_t)(framebuffer_width / 2) - box_w / 2;
    uint32_t box_y = (uint32_t)(framebuffer_height / 2) - 68;

    fill_rect(
        box_x + 28,
        box_y + 66,
        box_w - 56,
        25,
        0xff1b1f24
    );

    draw_text_at(
        status,
        box_x + 32,
        box_y + 74,
        0xfff5f7fa
    );

    spinlock_release(&vga_lock);
}

static uint32_t tga_decode_pixel_kernel(const uint8_t *p, int bytes)
{
    if (bytes == 1) {
        uint8_t v = p[0];
        return 0xff000000 | ((uint32_t)v << 16) | ((uint32_t)v << 8) | v;
    }

    uint8_t b = p[0];
    uint8_t g = p[1];
    uint8_t r = p[2];
    uint8_t a = bytes == 4 ? p[3] : 0xff;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static int tga_draw_from_memory(const uint8_t *data, size_t size, int centered, int boot_fallback)
{
    if (!data || size < sizeof(tga_hdr_t))
        return -1;

    const tga_hdr_t *h = (const tga_hdr_t *)data;
    int iw = h->width;
    int ih = h->height;
    int is_cmap = (h->data_type == 1 || h->data_type == 9);
    int pixel_bytes = is_cmap ? 1 : (h->bpp >> 3);
    if (iw <= 0 || ih <= 0 || (pixel_bytes != 1 && pixel_bytes != 3 && pixel_bytes != 4))
        return -1;

    size_t pos = sizeof(tga_hdr_t) + h->id_len;
    if (pos > size)
        return -1;

    uint32_t *cmap = NULL;
    if (h->cmap_type && h->cmap_length > 0) {
        int cmap_bytes = h->cmap_depth >> 3;
        if (cmap_bytes != 1 && cmap_bytes != 3 && cmap_bytes != 4)
            return -1;
        size_t cmap_size = (size_t)h->cmap_length * (size_t)cmap_bytes;
        if (pos + cmap_size > size)
            return -1;
        cmap = (uint32_t *)kmalloc((size_t)h->cmap_length * sizeof(uint32_t));
        if (!cmap)
            return -1;
        for (uint32_t i = 0; i < h->cmap_length; i++)
            cmap[i] = tga_decode_pixel_kernel(data + pos + i * cmap_bytes, cmap_bytes);
        pos += cmap_size;
    }

    if (is_cmap && !cmap)
        return -1;

    int crop_w = iw < (int)framebuffer_width ? iw : (int)framebuffer_width;
    int crop_h = ih < (int)framebuffer_height ? ih : (int)framebuffer_height;
    int src_x0 = centered ? (iw - crop_w) / 2 : 0;
    int src_y0 = centered ? (ih - crop_h) / 2 : 0;
    int dst_x0 = centered ? ((int)framebuffer_width - crop_w) / 2 : 0;
    int dst_y0 = centered ? ((int)framebuffer_height - crop_h) / 2 : 0;
    int flip_v = !(h->descriptor & 0x20);

    if (boot_fallback)
        draw_boot_fallback("Loading");
    else
        fill_rect(0, 0, framebuffer_width, framebuffer_height, 0x000000);

    if (h->data_type == 1 || h->data_type == 2 || h->data_type == 3) {
        size_t row_bytes = (size_t)iw * (size_t)pixel_bytes;
        if (pos + row_bytes * (size_t)ih > size) {
            if (cmap) kfree(cmap);
            return -1;
        }

        for (int y = 0; y < ih; y++) {
            int sy = flip_v ? (ih - 1 - y) : y;
            const uint8_t *row = data + pos + (size_t)y * row_bytes;
            if (sy < src_y0 || sy >= src_y0 + crop_h)
                continue;
            for (int x = 0; x < crop_w; x++) {
                const uint8_t *p = row + (size_t)(src_x0 + x) * pixel_bytes;
                uint32_t color = is_cmap ? cmap[p[0] < h->cmap_length ? p[0] : 0]
                                         : tga_decode_pixel_kernel(p, pixel_bytes);
                splash_put_pixel((uint32_t)(dst_x0 + x), (uint32_t)(dst_y0 + sy - src_y0), color);
            }
        }
    } else if (h->data_type == 9 || h->data_type == 10 || h->data_type == 11) {
        int total = iw * ih;
        int i = 0;
        while (i < total && pos < size) {
            uint8_t packet = data[pos++];
            int run = (packet & 0x7f) + 1;
            if (packet & 0x80) {
                if (pos + (size_t)pixel_bytes > size)
                    break;
                uint32_t color = is_cmap ? cmap[data[pos] < h->cmap_length ? data[pos] : 0]
                                         : tga_decode_pixel_kernel(data + pos, pixel_bytes);
                pos += pixel_bytes;
                for (int k = 0; k < run && i < total; k++, i++) {
                    int sx = i % iw;
                    int sy = flip_v ? (ih - 1 - i / iw) : i / iw;
                    if (sx >= src_x0 && sx < src_x0 + crop_w && sy >= src_y0 && sy < src_y0 + crop_h)
                        splash_put_pixel((uint32_t)(dst_x0 + sx - src_x0), (uint32_t)(dst_y0 + sy - src_y0), color);
                }
            } else {
                for (int k = 0; k < run && i < total; k++, i++) {
                    if (pos + (size_t)pixel_bytes > size)
                        break;
                    uint32_t color = is_cmap ? cmap[data[pos] < h->cmap_length ? data[pos] : 0]
                                             : tga_decode_pixel_kernel(data + pos, pixel_bytes);
                    pos += pixel_bytes;
                    int sx = i % iw;
                    int sy = flip_v ? (ih - 1 - i / iw) : i / iw;
                    if (sx >= src_x0 && sx < src_x0 + crop_w && sy >= src_y0 && sy < src_y0 + crop_h)
                        splash_put_pixel((uint32_t)(dst_x0 + sx - src_x0), (uint32_t)(dst_y0 + sy - src_y0), color);
                }
            }
        }
    } else {
        if (cmap) kfree(cmap);
        return -1;
    }

    if (cmap)
        kfree(cmap);
    return 0;
}

int vga_boot_splash_load_tga(const char *path)
{
    if (!path)
        return -1;

    fd_entry_t file;
    memset(&file, 0, sizeof(file));
    if (vfs_open_entry(path, 0, &file) < 0)
        return -1;

    uint32_t size = vfs_size_entry(&file);
    if (size < sizeof(tga_hdr_t) || size > 16 * 1024 * 1024) {
        vfs_close_entry(&file);
        return -1;
    }

    uint8_t *buf = (uint8_t *)kmalloc(size);
    if (!buf) {
        vfs_close_entry(&file);
        return -1;
    }

    uint32_t bytes_read = 0;
    int ret = vfs_read_entry(&file, buf, size, &bytes_read);
    vfs_close_entry(&file);
    if (ret < 0 || bytes_read != size) {
        kfree(buf);
        return -1;
    }

    spinlock_acquire(&vga_lock);
    ret = tga_draw_from_memory(buf, bytes_read, 1, 1);
    spinlock_release(&vga_lock);

    kfree(buf);
    return ret;
}

static int vga_load_tga_unlocked(const char *path, int centered, int boot_fallback)
{
    if (!path)
        return -1;

    fd_entry_t file;
    memset(&file, 0, sizeof(file));
    if (vfs_open_entry(path, 0, &file) < 0)
        return -1;

    uint32_t size = vfs_size_entry(&file);
    if (size < sizeof(tga_hdr_t) || size > 16 * 1024 * 1024) {
        vfs_close_entry(&file);
        return -1;
    }

    uint8_t *buf = (uint8_t *)kmalloc(size);
    if (!buf) {
        vfs_close_entry(&file);
        return -1;
    }

    uint32_t bytes_read = 0;
    int ret = vfs_read_entry(&file, buf, size, &bytes_read);
    vfs_close_entry(&file);
    if (ret < 0 || bytes_read != size) {
        kfree(buf);
        return -1;
    }

    ret = tga_draw_from_memory(buf, bytes_read, centered, boot_fallback);
    kfree(buf);
    return ret;
}

static uint32_t text_line_count(const char *str)
{
    if (!str || !str[0])
        return 1;
    uint32_t lines = 1;
    for (size_t i = 0; str[i]; i++)
        if (str[i] == '\n')
            lines++;
    return lines;
}

void vga_crash_screen(const char *name, const char *title, const char *info)
{
    if (!framebuffer_addr || !framebuffer_width || !framebuffer_height)
        return;

    char path[96];
    int loaded = -1;
    if (name && name[0]) {
        snprintf(path, sizeof(path), "/mnt/drv0/sys/crash/%s.tga", name);
        loaded = vga_load_tga_unlocked(path, 0, 0);
    }

    if (loaded < 0) {
        ft_run(true);
        if (flanterm) {
            setcolor(0xffffff, 0x000000);
            clr();
            prints("ZenOS kernel crash\n\n");
            prints(title ? title : "Unknown exception");
            prints("\n\n");
            if (info)
                prints(info);
        } else {
            fill_rect(0, 0, framebuffer_width, framebuffer_height, 0x000000);
            draw_text_at("ZenOS kernel crash", 16, 16, 0xffffffff);
            draw_text_at(title ? title : "Unknown exception", 16, 40, 0xffffffff);
            if (info)
                draw_text_at(info, 16, 72, 0xffffffff);
        }
        return;
    }

    ft_run(false);
    uint32_t lines = text_line_count(info) + 1;
    uint32_t panel_h = lines * 16 + 12;
    uint32_t y = framebuffer_height > panel_h ? (uint32_t)framebuffer_height - panel_h : 0;
    fill_rect(0, y, framebuffer_width, panel_h, 0x000000);
    draw_text_at(title ? title : "Unknown exception", 8, y + 4, 0xffffffff);
    if (info)
        draw_text_at(info, 8, y + 20, 0xffffffff);
}

uint32_t get_pixel_at(uint32_t x, uint32_t y)
{
    if (x >= framebuffer_width || y >= framebuffer_height)
        return 0;

    uint32_t offset = y * framebuffer_pitch + x * (framebuffer_bpp / 8);

    switch (framebuffer_bpp)
    {
    case 32:
        return *(uint32_t *)(framebuffer_addr + offset);
    case 24:
        return framebuffer_addr[offset] |
               (framebuffer_addr[offset + 1] << 8) |
               (framebuffer_addr[offset + 2] << 16);
    case 16:
    {
        uint16_t rgb565 = *(uint16_t *)(framebuffer_addr + offset);
        uint8_t r = (rgb565 >> 11) & 0x1F;
        uint8_t g = (rgb565 >> 5) & 0x3F;
        uint8_t b = rgb565 & 0x1F;
        return ((r << 3) << 16) | ((g << 2) << 8) | (b << 3);
    }
    }
    return 0;
}

void vga_init(void)
{
    spinlock_init(&vga_lock);
    if (!framebuffer_request.response || !framebuffer_request.response->framebuffer_count)
    {
        log("No framebuffer available", 3, 1);
        return;
    }

    fb = framebuffer_request.response->framebuffers[0];
    framebuffer_addr = (uint8_t *)fb->address;
    framebuffer_width = fb->width;
    framebuffer_height = fb->height;
    framebuffer_pitch = fb->pitch;
    framebuffer_bpp = fb->bpp;

    if (framebuffer_bpp != 16 && framebuffer_bpp != 24 && framebuffer_bpp != 32)
    {
        log("Unsupported BPP: %i", 3, 1, framebuffer_bpp);
        return;
    }

    if (framebuffer_bpp == 32)
    {
        red_shift = 16;
        green_shift = 8;
        blue_shift = 0;
        red_size = 8;
        green_size = 8;
        blue_size = 8;
    }
    else if (framebuffer_bpp == 24)
    {
        red_shift = 16;
        green_shift = 8;
        blue_shift = 0;
        red_size = 8;
        green_size = 8;
        blue_size = 8;
    }
    else if (framebuffer_bpp == 16)
    {
        red_shift = 11;
        green_shift = 5;
        blue_shift = 0;
        red_size = 5;
        green_size = 6;
        blue_size = 5;
    }

    ft_ctx = flanterm_fb_init(
        kmalloc,
        flanterm_kfree_wrapper,
        (uint32_t *)framebuffer_addr,
        framebuffer_width,
        framebuffer_height,
        framebuffer_pitch,
        red_size, red_shift,
        green_size, green_shift,
        blue_size, blue_shift,
        NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL,
        8, 16,
        0,
        0, 0,
        0,
        0
    );

    if (!ft_ctx)
    {
        log("Failed to initialize flanterm", 3, 0);
        return;
    }
    
    flanterm = true;
    log("Framebuffer initialized: %ix%i, %i bpp", 4, 0,
        framebuffer_width, framebuffer_height, framebuffer_bpp);
}

void font(uint32_t num)
{
    if (!ft_ctx || !flanterm)
        return;

    spinlock_acquire(&vga_lock);
    flanterm_write(ft_ctx, "\033[2J\033[H", 7);
    flanterm_deinit(ft_ctx, flanterm_kfree_wrapper);
        ft_ctx = flanterm_fb_init(
        kmalloc,
        flanterm_kfree_wrapper,
        (uint32_t *)framebuffer_addr,
        framebuffer_width,
        framebuffer_height,
        framebuffer_pitch,
        red_size, red_shift,
        green_size, green_shift,
        blue_size, blue_shift,
        NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        (num==0?font_8x16:(num==1?font1_8x16:(num==2?font2_8x16:NULL))),
        8, 16,
        0,
        0, 0,
        0,
        0
    );
    spinlock_release(&vga_lock);
}

void plotchar(char c, uint32_t x, uint32_t y, uint32_t fg)
{
    if ((uint8_t)c > 255)
        return;

    const uint8_t *glyph = font_8x16[(uint8_t)c];
    for (int row = 0; row < 16; row++)
    {
        uint8_t line = glyph[row];
        for (int col = 0; col < 8; col++)
        {
            if (line & (1 << (7 - col)))
                put_pixel(x + col, y + row, fg);
        }
    }
}

void draw_text_at(const char *str, uint32_t x, uint32_t y, uint32_t color)
{
    if (!str)
        return;

    uint32_t start_x = x;

    for (size_t i = 0; str[i] != '\0'; i++)
    {
        if (str[i] == '\n')
        {
            y += 16;
            x = start_x;
            continue;
        }

        plotchar(str[i], x, y, color);
        x += 8;
    }
}

void prints(const char *str)
{
    if (!ft_ctx || !flanterm)
        return;

    spinlock_acquire(&vga_lock);
    flanterm_write(ft_ctx, str, strlen(str));
    spinlock_release(&vga_lock);
}

void printc(char c)
{
    if (!ft_ctx || !flanterm)
        return;

    spinlock_acquire(&vga_lock);
    flanterm_write(ft_ctx, &c, 1);
    spinlock_release(&vga_lock);
}

void clr(void)
{
    if (!ft_ctx || !flanterm)
        return;

    spinlock_acquire(&vga_lock);
    flanterm_write(ft_ctx, "\033[2J\033[H", 7);
    spinlock_release(&vga_lock);
}

void ft_run(bool set){
    flanterm = set;
}

void setcolor(uint32_t fg, uint32_t bg)
{
    if (!ft_ctx || !flanterm)
        return;

    uint8_t fr = (fg >> 16) & 0xFF;
    uint8_t fg_g = (fg >> 8) & 0xFF;
    uint8_t fb = fg & 0xFF;

    uint8_t br = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bb = bg & 0xFF;

    char color_seq[64];
    snprintf(color_seq, sizeof(color_seq),
             "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm",
             fr, fg_g, fb, br, bg_g, bb);

    spinlock_acquire(&vga_lock);
    flanterm_write(ft_ctx, color_seq, strlen(color_seq));
    spinlock_release(&vga_lock);
}
