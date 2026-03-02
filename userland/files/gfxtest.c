#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "userlib.h"
#include "gfx.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    socket_file_t *gfx;
    int tries = 0;
    socket_open(GFX_SOCKET_NAME, &gfx);
    gfx_clear(gfx, 0xFF12121E);
    gfx_text(gfx, 8, 20,  0xFFFFD700, 16, "ZenOS Shape Demo");
    gfx_text(gfx, 8, 32, 0xFF555577, 10, "via gfxserver");
    gfx_text(gfx, 8, 44, 0xFF00FFCC, 11, "Lines");
    gfx_line(gfx,  8, 58, 150, 58,  0xFFFF4444);
    gfx_line(gfx,  8, 62, 150, 75,  0xFF44FF44);
    gfx_line(gfx,  8, 80, 150, 60,  0xFF4488FF);
    gfx_text(gfx, 8, 92, 0xFF00FFCC, 11, "Rectangles");
    gfx_rect(gfx,        8, 106, 55, 30, 0xFF883333);
    gfx_rect_outline(gfx,70, 106, 55, 30, 0xFF33AA33);
    gfx_rect(gfx,       132, 106, 30, 30, 0xFF334488);
    gfx_rect_outline(gfx,168, 106, 30, 30, 0xFFAA8800);
    gfx_text(gfx, 8, 146, 0xFF00FFCC, 11, "Circles");
    gfx_circle(gfx,      28, 170, 18, 0xFFCC4466);
    gfx_circle_outline(gfx,68, 170, 18, 0xFF44CCAA);
    gfx_circle(gfx,     108, 170, 14, 0xFF8844CC);
    gfx_circle_outline(gfx,145,170, 22, 0xFFFFAA00);
    gfx_text(gfx, 8, 200, 0xFF00FFCC, 11, "Triangles");
    gfx_triangle(gfx,  30, 230,  8, 260, 52, 260, 0xFFFF6644);
    gfx_triangle(gfx,  90, 260, 68, 230,112, 230, 0xFF44AAFF);
    gfx_triangle(gfx, 130, 230,130, 260,175, 260, 0xFF88FF44);
    gfx_text(gfx, 8, 272, 0xFF00FFCC, 11, "Combined");
    gfx_circle(gfx, 40, 305, 15, 0xFFFFDD00);
    gfx_line(gfx, 40, 283, 40, 276, 0xFFFFDD00);
    gfx_line(gfx, 40, 327, 40, 334, 0xFFFFDD00);
    gfx_line(gfx, 62, 305, 69, 305, 0xFFFFDD00);
    gfx_line(gfx, 18, 305, 11, 305, 0xFFFFDD00);
    gfx_line(gfx, 56, 289, 61, 284, 0xFFFFDD00);
    gfx_line(gfx, 24, 321, 19, 326, 0xFFFFDD00);
    gfx_line(gfx, 56, 321, 61, 326, 0xFFFFDD00);
    gfx_line(gfx, 24, 289, 19, 284, 0xFFFFDD00);
    gfx_rect(gfx,     100, 310, 60, 38, 0xFF885533);
    gfx_rect(gfx,     117, 325, 26, 23, 0xFF4466AA);
    gfx_triangle(gfx, 130, 285, 97, 313, 163, 313, 0xFFAA4433);
    gfx_rect_outline(gfx, 104, 315, 10, 10, 0xFFFFFFAA);
    gfx_rect_outline(gfx, 147, 315, 10, 10, 0xFFFFFFAA);
    gfx_text(gfx, 8, 358, 0xFF00FFCC, 11, "Unicode");
    gfx_text(gfx, 8, 372, 0xFFFFFFFF, 12,
        "\xE2\x88\x9E \xE2\x88\x91 \xE2\x88\xAB \xE2\x88\x9A \xE2\x89\xA0 \xCF\x80");
    gfx_text(gfx, 8, 388, 0xFFAABBFF, 11,
        "\xCE\xB1\xCE\xB2\xCE\xB3\xCE\xB4\xCE\xB5\xCE\xB6\xCE\xB7\xCE\xB8\xCE\xB9\xCE\xBA");
    gfx_line(gfx, 4, 402, 308, 402, 0xFF333355);
    gfx_text(gfx, 8, 412, 0xFF444466, 9,
        "ZenOS \xE2\x80\x94 https://github.com/Z-Proj/ZenOS");
    gfx_close(gfx);
    exit(0);
    return 0;
}