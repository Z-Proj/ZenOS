#ifndef GFX_H
#define GFX_H

#include "userlib.h"

#define GFX_SOCKET_NAME "gfx"
#define GFX_TEXT_MAX    120

// ── commands ──
#define CMD_TEXT      0x01  // draw unicode text
#define CMD_CLEAR     0x02  // clear right half (or a rect)
#define CMD_RECT      0x03  // filled rectangle
#define CMD_RECT_OUT  0x04  // outline rectangle
#define CMD_LINE      0x05  // line from (x,y) to (x2,y2)
#define CMD_CIRCLE    0x06  // filled circle
#define CMD_CIRCLE_OUT 0x07 // outline circle
#define CMD_TRIANGLE  0x08  // filled triangle
#define CMD_PIXEL     0x09  // single pixel

// ── message ──
typedef struct {
    uint8_t  cmd;
    int32_t  x,  y;   // primary coords
    int32_t  x2, y2;  // secondary coords (line end / triangle pt2)
    int32_t  x3, y3;  // triangle pt3
    int32_t  r;        // radius (circle) or width (rect) or unused
    int32_t  h;        // height (rect)
    uint32_t fg;       // foreground / fill color  ARGB
    uint32_t bg;       // background color (text only)
    uint8_t  size;     // font size (text only)
    char     text[GFX_TEXT_MAX];
} gfx_msg_t;

// ── client helpers ── (include this header in any app)

static inline int gfx_open(socket_file_t **sock) {
    return socket_open(GFX_SOCKET_NAME, sock);
}

static inline void gfx_close(socket_file_t *sock) {
    socket_close(sock);
}

static inline void gfx_send(socket_file_t *sock, gfx_msg_t *msg) {
    socket_write(sock, msg, sizeof(gfx_msg_t));
}

// convenience wrappers
static inline void gfx_text(socket_file_t *s, int x, int y, uint32_t fg, uint8_t sz, const char *txt) {
    gfx_msg_t m = {0};
    m.cmd = CMD_TEXT; m.x = x; m.y = y; m.fg = fg; m.size = sz;
    // manual strcpy (no libc)
    int i = 0;
    while (txt[i] && i < GFX_TEXT_MAX - 1) { m.text[i] = txt[i]; i++; }
    m.text[i] = 0;
    gfx_send(s, &m);
}

static inline void gfx_clear(socket_file_t *s, uint32_t color) {
    gfx_msg_t m = {0};
    m.cmd = CMD_CLEAR; m.fg = color;
    gfx_send(s, &m);
}

static inline void gfx_rect(socket_file_t *s, int x, int y, int w, int h, uint32_t color) {
    gfx_msg_t m = {0};
    m.cmd = CMD_RECT; m.x = x; m.y = y; m.r = w; m.h = h; m.fg = color;
    gfx_send(s, &m);
}

static inline void gfx_rect_outline(socket_file_t *s, int x, int y, int w, int h, uint32_t color) {
    gfx_msg_t m = {0};
    m.cmd = CMD_RECT_OUT; m.x = x; m.y = y; m.r = w; m.h = h; m.fg = color;
    gfx_send(s, &m);
}

static inline void gfx_line(socket_file_t *s, int x, int y, int x2, int y2, uint32_t color) {
    gfx_msg_t m = {0};
    m.cmd = CMD_LINE; m.x = x; m.y = y; m.x2 = x2; m.y2 = y2; m.fg = color;
    gfx_send(s, &m);
}

static inline void gfx_circle(socket_file_t *s, int x, int y, int radius, uint32_t color) {
    gfx_msg_t m = {0};
    m.cmd = CMD_CIRCLE; m.x = x; m.y = y; m.r = radius; m.fg = color;
    gfx_send(s, &m);
}

static inline void gfx_circle_outline(socket_file_t *s, int x, int y, int radius, uint32_t color) {
    gfx_msg_t m = {0};
    m.cmd = CMD_CIRCLE_OUT; m.x = x; m.y = y; m.r = radius; m.fg = color;
    gfx_send(s, &m);
}

static inline void gfx_triangle(socket_file_t *s,
    int x, int y, int x2, int y2, int x3, int y3, uint32_t color) {
    gfx_msg_t m = {0};
    m.cmd = CMD_TRIANGLE;
    m.x=x; m.y=y; m.x2=x2; m.y2=y2; m.x3=x3; m.y3=y3;
    m.fg = color;
    gfx_send(s, &m);
}

static inline void gfx_pixel(socket_file_t *s, int x, int y, uint32_t color) {
    gfx_msg_t m = {0};
    m.cmd = CMD_PIXEL; m.x = x; m.y = y; m.fg = color;
    gfx_send(s, &m);
}

#endif