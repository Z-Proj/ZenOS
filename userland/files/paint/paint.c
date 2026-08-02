#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../../userlib.h"
#include "nk_harp.h"

#define WIN_W     640
#define WIN_H     520
#define CANVAS_X  10
#define CANVAS_Y  70
#define CANVAS_W  620
#define CANVAS_H  440

static uint32_t canvas[CANVAS_H][CANVAS_W];
static uint32_t cur_color = 0xFFFFFF;
static float    brush = 4.0f;

static const uint32_t palette[8] = {
    0xFFFFFF, 0x000000, 0xE33E3E, 0x3EA0E3,
    0x3EE35C, 0xE3D33E, 0xA23EE3, 0xE38A3E
};

static inline uint32_t blend_rgb(uint32_t dst, uint32_t src_rgb, int a)
{
    if (a <= 0) return dst;
    if (a >= 255) return 0xFF000000u | src_rgb;
    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    uint32_t sr = (src_rgb >> 16) & 0xFF, sg = (src_rgb >> 8) & 0xFF, sb = src_rgb & 0xFF;
    uint32_t r = (sr * (uint32_t)a + dr * (uint32_t)(255 - a)) / 255;
    uint32_t g = (sg * (uint32_t)a + dg * (uint32_t)(255 - a)) / 255;
    uint32_t b = (sb * (uint32_t)a + db * (uint32_t)(255 - a)) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void paint_dab(float cx, float cy)
{
    float edge0 = brush - 1.0f;
    float edge1 = brush + 0.75f;
    int ix0 = (int)(cx - edge1) - 1, ix1 = (int)(cx + edge1) + 1;
    int iy0 = (int)(cy - edge1) - 1, iy1 = (int)(cy + edge1) + 1;
    for (int y = iy0; y <= iy1; y++) {
        if (y < 0 || y >= CANVAS_H) continue;
        for (int x = ix0; x <= ix1; x++) {
            if (x < 0 || x >= CANVAS_W) continue;
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            if (d >= edge1) continue;
            float t = d <= edge0 ? 1.0f : 1.0f - (d - edge0) / (edge1 - edge0);
            t = t * t * (3.0f - 2.0f * t);
            canvas[y][x] = blend_rgb(canvas[y][x], cur_color, (int)(t * 255.0f));
        }
    }
}

static void paint_stroke(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    int steps = (int)(dist / (brush * 0.33f)) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        paint_dab(x0 + dx * t, y0 + dy * t);
    }
}

int main(void)
{

    float prev_x = 0, prev_y = 0;
    int   was_down = 0;
    /* background color used for canvas (initialized after nk_harp_init) */
    uint32_t bg_col = 0;

    nk_harp_t *nh = nk_harp_init("Paint", 90, 60, WIN_W, WIN_H,
                                  "/mnt/drv0/lib/fonts/default.ttf");
    if (!nh) return 1;

    /* initialize canvas background to nuklear window background color to avoid flicker */
    bg_col = nk_color_to_u32(nh->ctx.style.window.background);
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            canvas[y][x] = bg_col;

    while (!nh->close_req) {
        nk_harp_feed_events(nh);

        if (nk_begin(&nh->ctx, "Paint",
            nk_rect(0, 0, WIN_W, WIN_H),
            NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {

            /* color palette + Clear button */
            nk_layout_row_static(&nh->ctx, 26, 26, 9);
            for (int i = 0; i < 8; i++) {
                struct nk_color c;
                c.a = 255;
                c.r = (palette[i] >> 16) & 0xFF;
                c.g = (palette[i] >> 8) & 0xFF;
                c.b = palette[i] & 0xFF;
                if (nk_button_color(&nh->ctx, c))
                    cur_color = palette[i];
            }
            /* place Clear on the right of the color buttons */
            if (nk_button_label(&nh->ctx, "Clear")) {
                for (int y = 0; y < CANVAS_H; y++)
                    for (int x = 0; x < CANVAS_W; x++)
                        canvas[y][x] = bg_col;
            }

            nk_layout_row_dynamic(&nh->ctx, 24, 3);
            if (nk_option_label(&nh->ctx, "Small", brush == 2.5f))  brush = 2.5f;
            if (nk_option_label(&nh->ctx, "Medium", brush == 5.0f)) brush = 5.0f;
            if (nk_option_label(&nh->ctx, "Large", brush == 9.0f))  brush = 9.0f;

            nk_layout_row_dynamic(&nh->ctx, 4, 1);
            nk_spacing(&nh->ctx, 1);

            nk_layout_row_static(&nh->ctx, CANVAS_H, CANVAS_W, 1);
            nk_spacing(&nh->ctx, 1);
        }
        nk_end(&nh->ctx);

        if (nk_input_is_mouse_down(&nh->ctx.input, NK_BUTTON_LEFT)) {
            float mx = nh->ctx.input.mouse.pos.x - CANVAS_X;
            float my = nh->ctx.input.mouse.pos.y - CANVAS_Y;
            if (mx >= -brush && my >= -brush && mx < CANVAS_W + brush && my < CANVAS_H + brush) {
                if (was_down)
                    paint_stroke(prev_x, prev_y, mx, my);
                else
                    paint_dab(mx, my);
                prev_x = mx; prev_y = my;
                was_down = 1;
            } else {
                was_down = 0;
            }
        } else {
            was_down = 0;
        }

        nk_harp_render(nh);

        for (int y = 0; y < CANVAS_H; y++) {
            uint32_t *dst = nh->win->buf + (CANVAS_Y + y) * nh->win->w + CANVAS_X;
            memcpy(dst, canvas[y], CANVAS_W * sizeof(uint32_t));
        }
        harp_flush(nh->win);
    }
    nk_harp_free(nh);
    return 0;
}