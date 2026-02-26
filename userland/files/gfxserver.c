// gfxserver.c — ZenOS Graphics Server

#include "userlib.h"
#include "../libs/gfx.h"

extern char _binary_FreeSansB_sfn_start;

#define SSFN_IMPLEMENTATION
#define SSFN_MAXLINES 4096
#define SSFN_memcmp memcmp
#define SSFN_memset memset
#define SSFN_memcpy memcpy
#define SSFN_realloc realloc
#define SSFN_free free
#include "ssfn.h"

// ── bump allocator ──
static uint8_t heap[256 * 1024];
static uint32_t heap_pos = 0;
void *malloc(size_t sz) {
    sz = (sz + 7) & ~7ULL;
    if (heap_pos + sz > sizeof(heap)) return NULL;
    void *p = &heap[heap_pos]; heap_pos += sz; return p;
}
void free(void *p) { (void)p; }
void *realloc(void *p, size_t sz) {
    void *n = malloc(sz);
    if (p && n) memcpy(n, p, sz);
    return n;
}

// ── globals ──
static ssfn_t     ssfn_ctx;
static ssfn_buf_t ssfn_buf;

static uint32_t *fbp;
static uint32_t  fb_w, fb_h, pitch_px;
static uint32_t  rx;   // right half start x
static uint32_t  rw;   // right half width

// ── math helpers (no libm) ──
static int32_t iabs(int32_t x) { return x < 0 ? -x : x; }

static int32_t isqrt(int32_t n) {
    if (n < 0) return 0;
    int32_t x = n, y = 1;
    while (x > y) { x = (x + y) / 2; y = n / x; }
    return x;
}

// ── pixel (clipped to right half) ──
static inline void px(int x, int y, uint32_t col) {
    if (x < 0 || x >= (int)rw) return;
    if (y < 0 || y >= (int)fb_h) return;
    fbp[y * pitch_px + rx + x] = col;
}

// ── clear right half ──
static void do_clear(uint32_t col) {
    for (uint32_t y = 0; y < fb_h; y++)
        for (uint32_t x = 0; x < rw; x++)
            fbp[y * pitch_px + rx + x] = col;
}

// ── filled rect ──
static void do_rect(int x, int y, int w, int h, uint32_t col) {
    for (int row = 0; row < h; row++)
        for (int col2 = 0; col2 < w; col2++)
            px(x + col2, y + row, col);
}

// ── outline rect ──
static void do_rect_out(int x, int y, int w, int h, uint32_t col) {
    for (int i = 0; i < w; i++) { px(x+i, y,     col); px(x+i, y+h-1, col); }
    for (int i = 0; i < h; i++) { px(x,   y+i,   col); px(x+w-1, y+i, col); }
}

// ── line (Bresenham) ──
static void do_line(int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = iabs(x1-x0), sx = x0 < x1 ? 1 : -1;
    int dy = -iabs(y1-y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        px(x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ── filled circle (midpoint) ──
static void do_circle(int cx, int cy, int r, uint32_t col) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x*x + y*y <= r*r)
                px(cx+x, cy+y, col);
}

// ── outline circle (midpoint) ──
static void do_circle_out(int cx, int cy, int r, uint32_t col) {
    int x = 0, y = r, d = 1 - r;
    while (x <= y) {
        px(cx+x, cy+y, col); px(cx-x, cy+y, col);
        px(cx+x, cy-y, col); px(cx-x, cy-y, col);
        px(cx+y, cy+x, col); px(cx-y, cy+x, col);
        px(cx+y, cy-x, col); px(cx-y, cy-x, col);
        if (d < 0) d += 2*x+3;
        else { d += 2*(x-y)+5; y--; }
        x++;
    }
}

// ── filled triangle (scanline) ──
static void triangle_flat(int x0,int y0,int x1,int y1,int x2,int y2,uint32_t col);

static void do_triangle(int x0,int y0,int x1,int y1,int x2,int y2,uint32_t col) {
    // sort by y
    int tx,ty;
    if (y0 > y1) { tx=x0;ty=y0; x0=x1;y0=y1; x1=tx;y1=ty; }
    if (y0 > y2) { tx=x0;ty=y0; x0=x2;y0=y2; x2=tx;y2=ty; }
    if (y1 > y2) { tx=x1;ty=y1; x1=x2;y1=y2; x2=tx;y2=ty; }

    if (y1 == y2) {
        triangle_flat(x0,y0,x1,y1,x2,y2,col);
    } else if (y0 == y1) {
        triangle_flat(x2,y2,x0,y0,x1,y1,col);
    } else {
        // split into two flat triangles
        int mx = x0 + (x2-x0)*(y1-y0)/(y2-y0);
        int my = y1;
        triangle_flat(x0,y0,x1,y1,mx,my,col);
        triangle_flat(x2,y2,x1,y1,mx,my,col);
    }
}

static void triangle_flat(int x0,int y0,int x1,int y1,int x2,int y2,uint32_t col) {
    // x0,y0 is the single vertex; x1y1 x2y2 are the flat edge
    int dir = (y0 < y1) ? 1 : -1;
    float slope1 = (float)(x1-x0)/(float)(y1-y0+1);
    float slope2 = (float)(x2-x0)/(float)(y2-y0+1);
    for (int y = y0; y != y1+dir; y += dir) {
        int xa = (int)(x0 + slope1*(y-y0));
        int xb = (int)(x0 + slope2*(y-y0));
        if (xa > xb) { int t=xa; xa=xb; xb=t; }
        for (int x = xa; x <= xb; x++) px(x, y, col);
    }
}

// ── text ──
static void do_text(int x, int y, uint32_t fg, uint32_t bg, uint8_t size, const char *str) {
    ssfn_select(&ssfn_ctx, SSFN_FAMILY_SANS, NULL, SSFN_STYLE_REGULAR, size ? size : 14);
    ssfn_buf.x  = x;
    ssfn_buf.y  = y;
    ssfn_buf.fg = fg;
    ssfn_buf.bg = bg;
    const char *p = str;
    while (*p) {
        int r = ssfn_render(&ssfn_ctx, &ssfn_buf, p);
        if (r <= 0) break;
        p += r;
    }
}

// ── dispatch ──
static void handle(gfx_msg_t *m) {
    switch (m->cmd) {
        case CMD_CLEAR:
            do_clear(m->fg);
            break;
        case CMD_PIXEL:
            px(m->x, m->y, m->fg);
            break;
        case CMD_LINE:
            do_line(m->x, m->y, m->x2, m->y2, m->fg);
            break;
        case CMD_RECT:
            do_rect(m->x, m->y, m->r, m->h, m->fg);
            break;
        case CMD_RECT_OUT:
            do_rect_out(m->x, m->y, m->r, m->h, m->fg);
            break;
        case CMD_CIRCLE:
            do_circle(m->x, m->y, m->r, m->fg);
            break;
        case CMD_CIRCLE_OUT:
            do_circle_out(m->x, m->y, m->r, m->fg);
            break;
        case CMD_TRIANGLE:
            do_triangle(m->x,m->y, m->x2,m->y2, m->x3,m->y3, m->fg);
            break;
        case CMD_TEXT:
            do_text(m->x, m->y, m->fg, m->bg, m->size, m->text);
            break;
        default:
            break;
    }
}

int main() {
    // ── framebuffer ──
    fb_info_t fb;
    if (fbinfo(&fb) != 0) { prints("gfxserver: fbinfo failed\n"); exit(1); }

    fbp      = (uint32_t *)(uintptr_t)fb.addr;
    fb_w     = fb.width;
    fb_h     = fb.height;
    pitch_px = fb.pitch / 4;
    rx       = fb_w / 2;
    rw       = fb_w - rx;

    // ── ssfn ──
    memset(&ssfn_ctx, 0, sizeof(ssfn_ctx));
    if (ssfn_load(&ssfn_ctx, (const void *)&_binary_FreeSansB_sfn_start) != SSFN_OK) {
        prints("gfxserver: ssfn_load failed\n"); exit(1);
    }
    ssfn_buf.ptr = (uint8_t *)&fbp[rx];
    ssfn_buf.p   = fb.pitch;
    ssfn_buf.w   = (int)rw;
    ssfn_buf.h   = (int)fb_h;
    ssfn_buf.bg  = 0;

    // ── clear to dark bg + left border ──
    do_clear(0xFF12121E);
    for (uint32_t y = 0; y < fb_h; y++)
        fbp[y * pitch_px + rx] = 0xFF3333AA;

    // ── create socket ──
    if (socket_create(GFX_SOCKET_NAME) != 0) {
        prints("gfxserver: socket_create failed\n"); exit(1);
    }

    socket_file_t *sock = NULL;
    if (socket_open(GFX_SOCKET_NAME, &sock) != 0) {
        prints("gfxserver: socket_open failed\n"); exit(1);
    }

    // ── main loop ──
    gfx_msg_t msg;
    uint32_t bytes_read = 0;

    while (1) {
        if (socket_available(sock) >= sizeof(gfx_msg_t)) {
            int r = socket_read(sock, &msg, sizeof(gfx_msg_t), &bytes_read);
            if (r == 0 && bytes_read == sizeof(gfx_msg_t)) {
                handle(&msg);
            }
        } else {
            yield();
        }
    }

    return 0;
}