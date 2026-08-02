#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../userlib.h"
#include "nk_harp.h"

#define WIN_W 600
#define WIN_H 520
#define BUF_CAP 16384
#define PATH_CAP 128

static char text[BUF_CAP];
static int  text_len = 0;

static char path[PATH_CAP] = "/mnt/drv0/notepad.txt";
static int  path_len = 22;

static char status[64] = "";

static void do_save(void)
{
    if (path_len <= 0) { snprintf(status, sizeof(status), "No path set."); return; }
    path[path_len] = '\0';
    FILE *f = fopen(path, "w");
    if (!f) { snprintf(status, sizeof(status), "Save failed: %s", path); return; }
    fwrite(text, 1, (size_t)text_len, f);
    fclose(f);
    snprintf(status, sizeof(status), "Saved.");
}

static void do_load(void)
{
    if (path_len <= 0) { snprintf(status, sizeof(status), "No path set."); return; }
    path[path_len] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(status, sizeof(status), "File not found: %s", path); return; }
    text_len = (int)fread(text, 1, BUF_CAP - 1, f);
    if (text_len < 0) text_len = 0;
    text[text_len] = '\0';
    fclose(f);
    snprintf(status, sizeof(status), "Loaded.");
}

int main(void)
{
    memset(text, 0, sizeof(text));

    nk_harp_t *nh = nk_harp_init("Notepad", 120, 80, WIN_W, WIN_H,
                                  "/mnt/drv0/lib/fonts/default.ttf");
    if (!nh) return 1;

    while (!nh->close_req) {
        nk_harp_feed_events(nh);

        if (nk_begin(&nh->ctx, "Notepad",
            nk_rect(0, 0, WIN_W, WIN_H),
            NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {

            nk_layout_row_begin(&nh->ctx, NK_STATIC, 26, 4);
            nk_layout_row_push(&nh->ctx, 60);
            if (nk_button_label(&nh->ctx, "New")) {
                text_len = 0; text[0] = '\0';
                snprintf(status, sizeof(status), "New file opened.");
            }
            nk_layout_row_push(&nh->ctx, WIN_W - 60 - 70 - 70 - 40);
            nk_edit_string(&nh->ctx, NK_EDIT_FIELD, path, &path_len,
                           PATH_CAP - 1, nk_filter_default);
            nk_layout_row_push(&nh->ctx, 70);
            if (nk_button_label(&nh->ctx, "Save")) do_save();
            nk_layout_row_push(&nh->ctx, 70);
            if (nk_button_label(&nh->ctx, "Load")) do_load();
            nk_layout_row_end(&nh->ctx);

            nk_layout_row_dynamic(&nh->ctx, 16, 1);
            nk_label(&nh->ctx, status, NK_TEXT_LEFT);

            nk_layout_row_dynamic(&nh->ctx, 4, 1);
            nk_spacing(&nh->ctx, 1);

            nk_layout_row_dynamic(&nh->ctx, WIN_H - 130, 1);
            nk_edit_string(&nh->ctx, NK_EDIT_BOX, text, &text_len,
                           BUF_CAP - 1, nk_filter_default);
        }
        nk_end(&nh->ctx);

        nk_harp_render(nh);
        harp_flush(nh->win);
    }
    nk_harp_free(nh);
    return 0;
}
