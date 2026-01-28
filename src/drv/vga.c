#include <stdint.h>
#include <stddef.h>
#include "../libk/core/mem.h"
#include "../libk/debug/log.h"
#include "../libk/string.h"
#include "../libk/spinlock.h"
#include "../libk/limine.h"
#include "vga.h"
#include "term/flanterm.h"
#include "term/flanterm_backends/fb.h"
#include "../libk/gfx/font_8x16.h"

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
    
    uint8_t red_shift = 0, green_shift = 0, blue_shift = 0;
    uint8_t red_size = 0, green_size = 0, blue_size = 0;

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
        font_8x16,
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