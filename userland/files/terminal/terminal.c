#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../../userlib.h"
#include <termios.h>

#define SSFN_IMPLEMENTATION
#define SSFN_MAXLINES 4096
#define SSFN_memcmp  memcmp
#define SSFN_memset  memset
#define SSFN_memcpy  memcpy
#define SSFN_realloc realloc
#define SSFN_free    free
#include "ssfn.h"

extern char _binary_mono_sfn_start;
extern char _binary_mono_sfn_end;

#define C_BG     0xFF0D1117
#define C_FG     0xFFCDD6F4
#define C_CURSOR 0xFFCDD6F4

static const uint32_t ansi_pal[16] = {
    0xFF1E2030, 0xFFF38BA8, 0xFFA6E3A1, 0xFFF9E2AF,
    0xFF89B4FA, 0xFFCBA6F7, 0xFF89DCEB, 0xFFBAC2DE,
    0xFF45475A, 0xFFF38BA8, 0xFFA6E3A1, 0xFFF9E2AF,
    0xFF89B4FA, 0xFFCBA6F7, 0xFF94E2D5, 0xFFCDD6F4,
};

static ssfn_t    sctx;
static ssfn_buf_t sbuf;

static uint8_t  *fbp;
static uint32_t  fb_w, fb_h, fb_pitch;

static int TERM_ROWS;
static int TERM_COLS;
static int CELL_W;
static int CELL_H;

typedef struct { char ch; uint32_t fg; uint32_t bg; } cell_t;
static cell_t *screen;

static int32_t  cur_row, cur_col;
static uint32_t cur_fg, cur_bg;

static int master_fd = -1;
static int shell_pid = -1;

#define ESC_NONE 0
#define ESC_ESC  1
#define ESC_CSI  2
static int esc_state;
static char esc_buf[64];
static int  esc_len;

static inline void fb_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >>  8) & 0xFF;
    uint8_t b =  c        & 0xFF;
    for (int32_t row = y; row < y + h; row++) {
        if (row < 0 || (uint32_t)row >= fb_h) continue;
        uint8_t *p = fbp + row * fb_pitch + x * 4;
        for (int32_t col = 0; col < w; col++) {
            if (x + col < 0 || (uint32_t)(x + col) >= fb_w) { p += 4; continue; }
            p[0] = b; p[1] = g; p[2] = r; p[3] = 0xFF;
            p += 4;
        }
    }
}

static void render_cell(int32_t row, int32_t col)
{
    cell_t *c = &screen[row * TERM_COLS + col];
    int32_t x = col * CELL_W;
    int32_t y = row * CELL_H;
    fb_fill_rect(x, y, CELL_W, CELL_H, c->bg);
    if (c->ch >= 32 && c->ch < 127) {
        sbuf.x  = x;
        sbuf.y  = y + 16;  
        sbuf.fg = c->fg | 0xFF000000;
        sbuf.bg = c->bg | 0xFF000000;
        char s[2] = {c->ch, 0};
        ssfn_render(&sctx, &sbuf, s);
    }
}

static void draw_cursor(int vis)
{
    int32_t x = cur_col * CELL_W;
    int32_t y = cur_row * CELL_H + CELL_H - 2;
    uint32_t c = vis ? C_CURSOR : screen[cur_row * TERM_COLS + cur_col].bg;
    fb_fill_rect(x, y, CELL_W, 2, c);
}

static void scroll_up(void)
{
    memmove(fbp, fbp + CELL_H * fb_pitch, (fb_h - CELL_H) * fb_pitch);
    fb_fill_rect(0, (int32_t)(fb_h - CELL_H), (int32_t)fb_w, CELL_H, C_BG);
    memmove(&screen[0], &screen[TERM_COLS], (TERM_ROWS - 1) * TERM_COLS * sizeof(cell_t));
    for (int c = 0; c < TERM_COLS; c++)
        screen[(TERM_ROWS - 1) * TERM_COLS + c] = (cell_t){' ', cur_fg, C_BG};
}

static void clear_screen(void)
{
    fb_fill_rect(0, 0, (int32_t)fb_w, (int32_t)fb_h, C_BG);
    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            screen[r * TERM_COLS + c] = (cell_t){' ', C_FG, C_BG};
    cur_row = 0; cur_col = 0;
}

static void clear_eol(void)
{
    for (int c = cur_col; c < TERM_COLS; c++) {
        screen[cur_row * TERM_COLS + c] = (cell_t){' ', cur_fg, C_BG};
        render_cell(cur_row, c);
    }
}

static void clear_eos(void)
{
    clear_eol();
    for (int r = cur_row + 1; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++) {
            screen[r * TERM_COLS + c] = (cell_t){' ', C_FG, C_BG};
            render_cell(r, c);
        }
}

static void apply_sgr(int *p, int n)
{
    if (n == 0) { cur_fg = C_FG; cur_bg = C_BG; return; }
    for (int i = 0; i < n; i++) {
        int v = p[i];
        if (v == 0)              { cur_fg = C_FG; cur_bg = C_BG; }
        else if (v == 1)         { }
        else if (v >= 30 && v <= 37) cur_fg = ansi_pal[v - 30];
        else if (v == 39)        cur_fg = C_FG;
        else if (v >= 40 && v <= 47) cur_bg = ansi_pal[v - 40];
        else if (v == 49)        cur_bg = C_BG;
        else if (v >= 90 && v <= 97)  cur_fg = ansi_pal[v - 90 + 8];
        else if (v >= 100 && v <= 107) cur_bg = ansi_pal[v - 100 + 8];
    }
}

static void dispatch_csi(char cmd)
{
    int params[16] = {0}; int np = 0;
    int acc = 0, any = 0;
    for (int i = 0; i < esc_len && np < 16; i++) {
        char c = esc_buf[i];
        if (c >= '0' && c <= '9') { acc = acc * 10 + (c - '0'); any = 1; }
        else if (c == ';') { params[np++] = acc; acc = 0; any = 0; }
    }
    if (any || np > 0) params[np++] = acc;
    int p0 = np > 0 ? params[0] : 0;
    int p1 = np > 1 ? params[1] : 0;

    switch (cmd) {
    case 'A': cur_row -= p0 ? p0 : 1; if (cur_row < 0) cur_row = 0; break;
    case 'B': cur_row += p0 ? p0 : 1; if (cur_row >= TERM_ROWS) cur_row = TERM_ROWS - 1; break;
    case 'C': cur_col += p0 ? p0 : 1; if (cur_col >= TERM_COLS) cur_col = TERM_COLS - 1; break;
    case 'D': cur_col -= p0 ? p0 : 1; if (cur_col < 0) cur_col = 0; break;
    case 'H': case 'f':
        cur_row = (p0 ? p0 : 1) - 1;
        cur_col = (p1 ? p1 : 1) - 1;
        if (cur_row < 0) cur_row = 0;
        if (cur_row >= TERM_ROWS) cur_row = TERM_ROWS - 1;
        if (cur_col < 0) cur_col = 0;
        if (cur_col >= TERM_COLS) cur_col = TERM_COLS - 1;
        break;
    case 'J':
        if (p0 == 2 || p0 == 3) clear_screen();
        else if (p0 == 0) clear_eos();
        break;
    case 'K':
        if (p0 == 0) clear_eol();
        break;
    case 'm': apply_sgr(params, np); break;
    case 'l': case 'h': break;
    default: break;
    }
}

static void put_raw(char c)
{
    draw_cursor(0);
    switch (esc_state) {
    case ESC_NONE:
        if (c == '\033') { esc_state = ESC_ESC; goto done; }
        if (c == '\r')   { cur_col = 0; break; }
        if (c == '\n') {
            cur_row++;
            if (cur_row >= TERM_ROWS) { cur_row = TERM_ROWS - 1; scroll_up(); }
            break;
        }
        if (c == '\b') {
            if (cur_col > 0) cur_col--;
            screen[cur_row * TERM_COLS + cur_col] = (cell_t){' ', cur_fg, C_BG};
            render_cell(cur_row, cur_col);
            break;
        }
        if (c == '\t') {
            int next = (cur_col + 8) & ~7;
            if (next >= TERM_COLS) next = TERM_COLS - 1;
            for (; cur_col < next; cur_col++) {
                screen[cur_row * TERM_COLS + cur_col] = (cell_t){' ', cur_fg, cur_bg};
                render_cell(cur_row, cur_col);
            }
            break;
        }
        if (c >= 32 && (unsigned char)c < 128) {
            if (cur_col >= TERM_COLS) {
                cur_col = 0; cur_row++;
                if (cur_row >= TERM_ROWS) { cur_row = TERM_ROWS - 1; scroll_up(); }
            }
            screen[cur_row * TERM_COLS + cur_col] = (cell_t){c, cur_fg, cur_bg};
            render_cell(cur_row, cur_col);
            cur_col++;
        }
        break;
    case ESC_ESC:
        if (c == '[') { esc_state = ESC_CSI; esc_len = 0; goto done; }
        if (c == 'c') { clear_screen(); }
        esc_state = ESC_NONE;
        break;
    case ESC_CSI:
        if ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == '>') {
            if (esc_len < (int)sizeof(esc_buf) - 1)
                esc_buf[esc_len++] = c;
        } else {
            esc_buf[esc_len] = 0;
            dispatch_csi(c);
            esc_state = ESC_NONE;
        }
        break;
    }
done:
    draw_cursor(1);
}

static void drain_master(void)
{
    char buf[512];
    draw_cursor(0);
    for (;;) {
        int avail = 0;
        zen_ioctl(master_fd, ZEN_FIONREAD, &avail);
        if (avail <= 0) break;
        int want = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
        int n = read(master_fd, buf, want);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) put_raw(buf[i]);
    }
    draw_cursor(1);
}

static void send_key(int k)
{
    if (k == 0) return;
    if (k == KEY_ARROW_UP)    { write(master_fd, "\033[A", 3); return; }
    if (k == KEY_ARROW_DOWN)  { write(master_fd, "\033[B", 3); return; }
    if (k == KEY_ARROW_RIGHT) { write(master_fd, "\033[C", 3); return; }
    if (k == KEY_ARROW_LEFT)  { write(master_fd, "\033[D", 3); return; }
    if (k == '\r') k = '\n';
    char c = (char)k;
    write(master_fd, &c, 1);
}

int main(void)
{
    fb_info_t fb;
    if (zen_fbinfo(&fb) < 0) {
        zen_log("terminal: fbinfo failed", 0, 1);
        exit(1);
    }

    fbp      = (uint8_t *)(uintptr_t)fb.addr;
    fb_w     = (uint32_t)fb.width;
    fb_h     = (uint32_t)fb.height;
    fb_pitch = (uint32_t)fb.pitch;

    if (ssfn_load(&sctx, &_binary_mono_sfn_start) != SSFN_OK) {
        zen_log("terminal: font load failed", 0, 1);
        exit(1);
    }
    ssfn_select(&sctx, SSFN_FAMILY_MONOSPACE, NULL, SSFN_STYLE_REGULAR, 16);

    sbuf.ptr   = fbp;
    sbuf.w     = (int)fb_w;
    sbuf.h     = (int)fb_h;
    sbuf.p     = (int)fb_pitch;
    sbuf.x     = 0;
    sbuf.y     = 0;
    sbuf.fg    = 0xFFFFFF;
    sbuf.bg    = 0;

    CELL_W   = 8;
    CELL_H   = 16;
    TERM_COLS = (int)(fb_w / CELL_W);
    TERM_ROWS = (int)(fb_h / CELL_H);
    if (TERM_COLS < 1)  TERM_COLS = 80;
    if (TERM_ROWS < 1)  TERM_ROWS = 24;
    if (TERM_COLS > 512) TERM_COLS = 512;
    if (TERM_ROWS > 256) TERM_ROWS = 256;

    screen = (cell_t *)malloc((size_t)(TERM_ROWS * TERM_COLS) * sizeof(cell_t));
    if (!screen) {
        zen_log("terminal: out of memory", 0, 1);
        exit(1);
    }

    cur_row = 0; cur_col = 0;
    cur_fg  = C_FG; cur_bg = C_BG;
    esc_state = ESC_NONE;
    esc_len   = 0;

    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            screen[r * TERM_COLS + c] = (cell_t){' ', C_FG, C_BG};
    fb_fill_rect(0, 0, (int32_t)fb_w, (int32_t)fb_h, C_BG);

    draw_cursor(1);

    int slave_fd = -1;
    master_fd = zen_pty_open(&slave_fd);
    if (master_fd < 0) {
        const char *msg = "pty_open failed";
        for (const char *p = msg; *p; p++) put_raw(*p);
        draw_cursor(1);
        for (;;) zen_halt();
    }

    zen_winsize_t ws = {
        (uint16_t)TERM_ROWS,
        (uint16_t)TERM_COLS,
        (uint16_t)fb_w,
        (uint16_t)fb_h
    };
    zen_ioctl(master_fd, ZEN_TIOCSWINSZ, &ws);
    zen_ioctl(slave_fd,  ZEN_TIOCSWINSZ, &ws);

    struct termios tios;
    if (tcgetattr(slave_fd, &tios) == 0) {
        tios.c_iflag = ICRNL | IXON;
        tios.c_oflag = OPOST | ONLCR;
        tios.c_cflag = CREAD | CS8;
        tios.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
        tios.c_cc[VINTR]  = 3;
        tios.c_cc[VQUIT]  = 28;
        tios.c_cc[VERASE] = 127;
        tios.c_cc[VKILL]  = 21;
        tios.c_cc[VEOF]   = 4;
        tios.c_cc[VTIME]  = 0;
        tios.c_cc[VMIN]   = 1;
        tios.c_cc[VSTART] = 17;
        tios.c_cc[VSTOP]  = 19;
        tios.c_cc[VSUSP]  = 26;
        tcsetattr(slave_fd, TCSANOW, &tios);
    }

    shell_pid = fork();
    if (shell_pid == 0) {
        dup2(slave_fd, 0);
        dup2(slave_fd, 1);
        dup2(slave_fd, 2);
        if (slave_fd > 2) close(slave_fd);
        if (master_fd > 2) close(master_fd);
        char *sh_argv[] = { "/mnt/drv0/bin/shell", NULL };
        execv("/mnt/drv0/bin/shell", sh_argv);
        char *fb_argv[] = { "/mnt/drv0/bin/sh", NULL };
        execv("/mnt/drv0/bin/sh", fb_argv);
        write(1, "shell not found\n", 16);
        _exit(1);
    }

    if (slave_fd >= 0) close(slave_fd);

    while (1) {
        int status = 0;
        pid_t dead = waitpid(shell_pid, &status, 1);
        if (dead == shell_pid) {
            draw_cursor(0);
            uint32_t old_fg = cur_fg;
            cur_fg = 0xFFF9E2AF;
            const char *msg = "\r\n[shell exited - press any key to reboot]";
            for (const char *p = msg; *p; p++) put_raw(*p);
            cur_fg = old_fg;
            draw_cursor(1);
            while (zen_getkey() == 0) zen_halt();
            zen_reboot();
            for (;;) zen_halt();
        }

       
        int avail = 0;
        zen_ioctl(master_fd, ZEN_FIONREAD, &avail);
        if (avail > 0) {
            drain_master();
        } else {
           
            zen_halt();
        }

        if (zen_is_focused()) {
            int k = zen_getkey();
            if (k != 0) send_key(k);
        }
    }

    return 0;
}
