#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../userlib.h"
#include "nk_harp.h"

#define WIN_W 440
#define WIN_H 300

int main(void)
{
    utsname_t u;
    memset(&u, 0, sizeof(u));
    uname(&u);

    fb_info_t fb;
    memset(&fb, 0, sizeof(fb));
    zen_fbinfo(&fb);

    char line_res[64];
    snprintf(line_res, sizeof(line_res), "%llu x %llu, %u bpp",
             (unsigned long long)fb.width, (unsigned long long)fb.height, fb.bpp);

    nk_harp_t *nh = nk_harp_init("System Info", 140, 90, WIN_W, WIN_H,
                                  "/mnt/drv0/lib/fonts/default.ttf");
    if (!nh) return 1;

    while (!nh->close_req) {
        nk_harp_feed_events(nh);

        if (nk_begin(&nh->ctx, "System Info",
            nk_rect(0, 0, WIN_W, WIN_H),
            NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {

            nk_layout_row_static(&nh->ctx, 22, 110, 2);
            nk_label(&nh->ctx, "System", NK_TEXT_LEFT);
            nk_label(&nh->ctx, u.sysname, NK_TEXT_LEFT);

            nk_layout_row_static(&nh->ctx, 22, 110, 2);
            nk_label(&nh->ctx, "Host name", NK_TEXT_LEFT);
            nk_label(&nh->ctx, u.nodename, NK_TEXT_LEFT);

            nk_layout_row_static(&nh->ctx, 22, 110, 2);
            nk_label(&nh->ctx, "Release", NK_TEXT_LEFT);
            nk_label(&nh->ctx, u.release, NK_TEXT_LEFT);

            nk_layout_row_static(&nh->ctx, 22, 110, 2);
            nk_label(&nh->ctx, "Version", NK_TEXT_LEFT);
            nk_label(&nh->ctx, u.version, NK_TEXT_LEFT);

            nk_layout_row_static(&nh->ctx, 22, 110, 2);
            nk_label(&nh->ctx, "Machine", NK_TEXT_LEFT);
            nk_label(&nh->ctx, u.machine, NK_TEXT_LEFT);

            nk_layout_row_static(&nh->ctx, 22, 110, 2);
            nk_label(&nh->ctx, "Display", NK_TEXT_LEFT);
            nk_label(&nh->ctx, line_res, NK_TEXT_LEFT);
        }
        nk_end(&nh->ctx);

        nk_harp_render(nh);
        harp_flush(nh->win);
    }
    nk_harp_free(nh);
    return 0;
}
