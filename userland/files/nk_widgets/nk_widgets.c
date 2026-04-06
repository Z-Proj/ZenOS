#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "nk_harp.h"

extern char _binary_FreeSansB_sfn_start;

#define WIN_W 620
#define WIN_H 700

static float   slider_r     = 0.5f;
static float   slider_g     = 0.7f;
static float   slider_b     = 0.3f;
static nk_size progress_val = 40;
static int     check_a      = 0;
static int     check_b      = 1;
static int     radio_sel    = 0;
static int     combo_sel    = 0;
static char    edit_buf[128];
static nk_size edit_len     = 0;
static int     click_count  = 0;

static const char *combo_items[]  = { "Harp WM", "Nuklear", "Pixman", "SSFN" };
static const int   combo_count    = 4;
static const char *radio_labels[] = { "Alpha", "Beta", "Gamma" };

static void do_ui(nk_harp_t *nh)
{
    if (!nk_begin(&nh->ctx, "Nuklear Showcase :",
        nk_rect(0, 0, WIN_W, WIN_H),
        NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR))
    {
        nk_end(&nh->ctx);
        return;
    }

    nk_layout_row_dynamic(&nh->ctx, 8, 1);
    nk_spacing(&nh->ctx, 1);

    nk_layout_row_dynamic(&nh->ctx, 22, 1);
    nk_label(&nh->ctx, "Buttons & Counter", NK_TEXT_LEFT);

    nk_layout_row_static(&nh->ctx, 28, 100, 4);
    if (nk_button_label(&nh->ctx, "Click Me"))  click_count++;
    if (nk_button_label(&nh->ctx, "Reset"))     click_count = 0;
    if (nk_button_label(&nh->ctx, "+10"))       click_count += 10;
    if (nk_button_label(&nh->ctx, "-10"))       click_count -= 10;

    nk_layout_row_dynamic(&nh->ctx, 20, 1);
    char cnt_buf[48];
    snprintf(cnt_buf, sizeof(cnt_buf), "Count: %d", click_count);
    nk_label(&nh->ctx, cnt_buf, NK_TEXT_LEFT);

    nk_layout_row_dynamic(&nh->ctx, 8, 1);
    nk_spacing(&nh->ctx, 1);

    nk_layout_row_dynamic(&nh->ctx, 22, 1);
    nk_label(&nh->ctx, "RGB Sliders", NK_TEXT_LEFT);

    nk_layout_row_static(&nh->ctx, 22, 240, 2);
    nk_label(&nh->ctx, "Red  ", NK_TEXT_RIGHT);
    nk_slider_float(&nh->ctx, 0.0f, &slider_r, 1.0f, 0.01f);

    nk_layout_row_static(&nh->ctx, 22, 240, 2);
    nk_label(&nh->ctx, "Green", NK_TEXT_RIGHT);
    nk_slider_float(&nh->ctx, 0.0f, &slider_g, 1.0f, 0.01f);

    nk_layout_row_static(&nh->ctx, 22, 240, 2);
    nk_label(&nh->ctx, "Blue ", NK_TEXT_RIGHT);
    nk_slider_float(&nh->ctx, 0.0f, &slider_b, 1.0f, 0.01f);

    nk_layout_row_dynamic(&nh->ctx, 8, 1);
    nk_spacing(&nh->ctx, 1);

    nk_layout_row_dynamic(&nh->ctx, 22, 1);
    nk_label(&nh->ctx, "Progress Bar", NK_TEXT_LEFT);

    nk_layout_row_dynamic(&nh->ctx, 20, 1);
    nk_progress(&nh->ctx, &progress_val, 100, NK_MODIFIABLE);

    nk_layout_row_dynamic(&nh->ctx, 8, 1);
    nk_spacing(&nh->ctx, 1);

    nk_layout_row_dynamic(&nh->ctx, 22, 1);
    nk_label(&nh->ctx, "Checkboxes & Radio Buttons", NK_TEXT_LEFT);

    nk_layout_row_static(&nh->ctx, 22, 140, 3);
    nk_checkbox_label(&nh->ctx, "Enable fog",  &check_a);
    nk_checkbox_label(&nh->ctx, "Show grid",   &check_b);
    nk_spacing(&nh->ctx, 1);

    nk_layout_row_static(&nh->ctx, 22, 100, 3);
    for (int i = 0; i < 3; i++)
        if (nk_option_label(&nh->ctx, radio_labels[i], radio_sel == i))
            radio_sel = i;

    nk_layout_row_dynamic(&nh->ctx, 8, 1);
    nk_spacing(&nh->ctx, 1);

    nk_layout_row_dynamic(&nh->ctx, 22, 1);
    nk_label(&nh->ctx, "Combo Box", NK_TEXT_LEFT);

    nk_layout_row_static(&nh->ctx, 26, 180, 1);
    combo_sel = nk_combo(&nh->ctx, combo_items, combo_count,
                         combo_sel, 22, nk_vec2(180, 120));

    nk_layout_row_dynamic(&nh->ctx, 8, 1);
    nk_spacing(&nh->ctx, 1);

    nk_layout_row_dynamic(&nh->ctx, 22, 1);
    nk_label(&nh->ctx, "Text Input", NK_TEXT_LEFT);

    nk_layout_row_dynamic(&nh->ctx, 26, 1);
    nk_edit_string(&nh->ctx, NK_EDIT_SIMPLE, edit_buf,
                   (int *)&edit_len, sizeof(edit_buf) - 1,
                   nk_filter_default);

    nk_layout_row_dynamic(&nh->ctx, 8, 1);
    nk_spacing(&nh->ctx, 1);

    nk_layout_row_dynamic(&nh->ctx, 20, 1);
    char state_buf[128];
    snprintf(state_buf, sizeof(state_buf),
        "fog:%s  grid:%s  radio:%s  combo:%s",
        check_a ? "on" : "off",
        check_b ? "on" : "off",
        radio_labels[radio_sel],
        combo_items[combo_sel]);
    nk_label(&nh->ctx, state_buf, NK_TEXT_LEFT);

    nk_end(&nh->ctx);
}

int main(void)
{
    memset(edit_buf, 0, sizeof(edit_buf));

    nk_harp_t *nh = nk_harp_init("Nuklear Demo", 80, 60, WIN_W, WIN_H,
                                  &_binary_FreeSansB_sfn_start);
    if (!nh) return 1;

    float   prev_r     = slider_r, prev_g = slider_g, prev_b = slider_b;
    nk_size prev_prog  = progress_val;
    int     prev_ca    = check_a,  prev_cb = check_b;
    int     prev_radio = radio_sel, prev_combo = combo_sel;
    int     prev_count = click_count;
    nk_size prev_elen  = edit_len;

    while (!nh->close_req) {
        nk_harp_feed_events(nh);
        do_ui(nh);
        prev_r     = slider_r;  prev_g = slider_g; prev_b = slider_b;
        prev_prog  = progress_val;
        prev_ca    = check_a;   prev_cb = check_b;
        prev_radio = radio_sel; prev_combo = combo_sel;
        prev_count = click_count;
        prev_elen  = edit_len;
        nk_harp_render(nh);
        harp_flush(nh->win);
    }
    nk_harp_free(nh);
    return 0;
}
