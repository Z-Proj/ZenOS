#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include "../userlib.h"
#include "../../libs/gfx.h"

#define C_BG     0xFF0A0E14
#define C_PANEL  0xFF111A24
#define C_BORDER 0xFF1E3A5F
#define C_TITLE  0xFF00CCFF
#define C_LABEL  0xFF88AACC
#define C_VAL    0xFFFFFFFF
#define C_CYAN   0xFF00DDFF
#define C_ACCENT 0xFF7744FF
#define C_DIM    0xFF334455

#define ROW_CLOCK   252
#define ROW_H       16
#define VAL_X       90

static char g_nbuf[32];

static char *u64str(uint64_t n) {
    if (n == 0) { g_nbuf[0]='0'; g_nbuf[1]=0; return g_nbuf; }
    int i = 0; uint64_t t = n;
    while (t > 0) { g_nbuf[i++] = '0' + t % 10; t /= 10; }
    for (int a = 0, b = i-1; a < b; a++, b--) {
        char tmp = g_nbuf[a]; g_nbuf[a] = g_nbuf[b]; g_nbuf[b] = tmp;
    }
    g_nbuf[i] = 0;
    return g_nbuf;
}

static void append(char *buf, int *pos, const char *s) {
    while (*s) buf[(*pos)++] = *s++;
}

static void pad2(char *buf, int *pos, int n) {
    buf[(*pos)++] = '0' + n / 10;
    buf[(*pos)++] = '0' + n % 10;
}

static void panel(socket_file_t *g, int x, int y, int w, int h) {
    gfx_rect(g, x, y, w, h, C_PANEL);
    gfx_rect_outline(g, x, y, w, h, C_BORDER);
}

static void row(socket_file_t *g, int y, const char *label, const char *val) {
    gfx_text(g, 16,    y + 10, C_LABEL, 1, label);
    gfx_text(g, VAL_X, y + 10, C_VAL,   1, val);
}

static void draw_static(socket_file_t *g, utsname_t *un, fb_info_t *fb) {
    gfx_clear(g, C_BG);

    gfx_text(g, 16,  22, C_TITLE,  2, "ZenOS");
    gfx_text(g, 145, 20, C_ACCENT, 1, "sysinfo");
    gfx_line(g, 8, 44, 504, 44, C_BORDER);

    panel(g, 8, 52, 496, 84);
    gfx_text(g, 16, 62+10, C_CYAN, 1, "System");
    row(g, 78,  "OS:",    un->sysname);
    row(g, 94,  "Arch:",  un->machine);
    row(g, 110, "Ver:",   un->release);

    panel(g, 8, 144, 496, 66);
    gfx_text(g, 16, 154+10, C_CYAN, 1, "Display");

    static char res[32]; int ri = 0;
    append(res, &ri, u64str(fb->width));  res[ri++] = 'x';
    append(res, &ri, u64str(fb->height)); res[ri++] = ' ';
    res[ri++] = '@'; res[ri++] = ' ';
    append(res, &ri, u64str(fb->bpp));
    append(res, &ri, "bpp"); res[ri] = 0;

    row(g, 170, "Res:",   res);
    row(g, 186, "Pitch:", u64str(fb->pitch));

    panel(g, 8, 218, 496, 68);
    gfx_text(g, 16, 228+10, C_CYAN, 1, "Time");
    gfx_text(g, 16, ROW_CLOCK+10,  C_LABEL, 1, "Clock:");

    gfx_line(g, 8, 300, 504, 300, C_BORDER);
    gfx_text(g, 8, 310+10, C_DIM, 1, "any key to exit");
}

static void draw_time(socket_file_t *g) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t total = tv.tv_sec;

    static char tbuf[12];
    int ti = 0;

    int hh = (int)(total / 3600 % 24);
    int mm = (int)(total / 60 % 60);
    int ss = (int)(total % 60);
    
    pad2(tbuf, &ti, hh); tbuf[ti++] = ':';
    pad2(tbuf, &ti, mm); tbuf[ti++] = ':';
    pad2(tbuf, &ti, ss); tbuf[ti] = 0;

    // Clear the previous time value
    gfx_rect(g, VAL_X, ROW_CLOCK, 496-VAL_X, ROW_H, C_PANEL);
    gfx_text(g, VAL_X, ROW_CLOCK+10, C_VAL, 1, tbuf);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    socket_file_t *g;
    if (gfx_open(&g) != 0) {
        fputs("\033[31msysinfo: no gfxserver\033[0m\n", stdout);
        exit(1);
    }

    utsname_t un; uname(&un);
    fb_info_t fb; zen_fbinfo(&fb);

    draw_static(g, &un, &fb);
    draw_time(g);

    int last_sec = -1;
    while (1) {
        struct timeval tv; gettimeofday(&tv, NULL);
        int sec = (int)(tv.tv_sec % 60);
        if (sec != last_sec) {
            draw_time(g);
            last_sec = sec;
        }
        for(int i = 0; i < 20; i++)zen_halt();
        if (zen_getkey() != 0) break;
    }

    gfx_clear(g, C_BG);
    gfx_close(g);
    exit(0);
    return 0;
}
