#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>

#define SSFN_IMPLEMENTATION
#define SSFN_MAXLINES 4096
#define SSFN_memcmp  memcmp
#define SSFN_memset  memset
#define SSFN_memcpy  memcpy
#define SSFN_realloc realloc
#define SSFN_free    free
#include "ssfn.h"
#include "../../include/harp_api.h"

extern char _binary_mono_sfn_start;

#define WIN_W     800
#define WIN_H     500
#define CELL_W    8
#define CELL_H    18
#define TERM_COLS (WIN_W / CELL_W)
#define TERM_ROWS (WIN_H / CELL_H)

#define C_BG     0xFF0D1117
#define C_FG     0xFFCDD6F4
#define C_CURSOR 0xFFCDD6F4

static const uint32_t ansi_pal[16] = {
    0xFF1E2030, 0xFFF38BA8, 0xFFA6E3A1, 0xFFF9E2AF,
    0xFF89B4FA, 0xFFCBA6F7, 0xFF89DCEB, 0xFFBAC2DE,
    0xFF45475A, 0xFFF38BA8, 0xFFA6E3A1, 0xFFF9E2AF,
    0xFF89B4FA, 0xFFCBA6F7, 0xFF94E2D5, 0xFFCDD6F4,
};

static ssfn_t     sctx;
static ssfn_buf_t sbuf;
static harp_window_t *win;

typedef struct { char ch; uint32_t fg; uint32_t bg; } cell_t;
static cell_t screen[TERM_ROWS][TERM_COLS];

static int32_t  cur_row, cur_col;
static uint32_t cur_fg, cur_bg;
static int      cursor_vis;

static int master_fd = -1;
static int shell_pid = -1;

#define ESC_NONE 0
#define ESC_ESC  1
#define ESC_CSI  2
static int  esc_state;
static char esc_buf[64];
static int  esc_len;

static inline void buf_fill_rect(int x, int y, int w, int h, uint32_t c)
{
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + w > WIN_W ? WIN_W : x + w;
    int y2 = y + h > WIN_H ? WIN_H : y + h;
    uint32_t px = c | 0xFF000000;
    for (int row = y1; row < y2; row++) {
        uint32_t *line = win->buf + row * WIN_W;
        for (int col = x1; col < x2; col++)
            line[col] = px;
    }
}

static void render_cell(int row, int col)
{
    cell_t *c = &screen[row][col];
    int x = col * CELL_W;
    int y = row * CELL_H;
    buf_fill_rect(x, y, CELL_W, CELL_H, c->bg);
    if (c->ch >= 32 && c->ch < 127) {
        sbuf.x  = x;
        sbuf.y  = y + 16;
        sbuf.fg = c->fg | 0xFF000000;
        sbuf.bg = c->bg | 0xFF000000;
        char s[2] = {c->ch, 0};
        ssfn_render(&sctx, &sbuf, s);
    }
}

static void cursor_show(int vis)
{
    if (cursor_vis == vis) return;
    cursor_vis = vis;
    int x = cur_col * CELL_W;
    int y = cur_row * CELL_H + CELL_H - 3;
    uint32_t c = vis ? C_CURSOR : screen[cur_row][cur_col].bg;
    buf_fill_rect(x, y, CELL_W, 3, c);
}

static void scroll_up(void)
{
    memmove(win->buf, win->buf + CELL_H * WIN_W,
            (WIN_H - CELL_H) * WIN_W * sizeof(uint32_t));
    buf_fill_rect(0, WIN_H - CELL_H, WIN_W, CELL_H, C_BG);
    memmove(&screen[0], &screen[1], (TERM_ROWS - 1) * sizeof(screen[0]));
    for (int c = 0; c < TERM_COLS; c++)
        screen[TERM_ROWS - 1][c] = (cell_t){' ', cur_fg, C_BG};
}

static void clear_screen(void)
{
    buf_fill_rect(0, 0, WIN_W, WIN_H, C_BG);
    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            screen[r][c] = (cell_t){' ', C_FG, C_BG};
    cur_row = 0; cur_col = 0;
}

static void clear_eol(void)
{
    for (int c = cur_col; c < TERM_COLS; c++) {
        screen[cur_row][c] = (cell_t){' ', cur_fg, C_BG};
        render_cell(cur_row, c);
    }
}

static void clear_eos(void)
{
    clear_eol();
    for (int r = cur_row + 1; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++) {
            screen[r][c] = (cell_t){' ', C_FG, C_BG};
            render_cell(r, c);
        }
}

static void clear_sol(void)
{
    for (int c = 0; c <= cur_col; c++) {
        screen[cur_row][c] = (cell_t){' ', cur_fg, C_BG};
        render_cell(cur_row, c);
    }
}

static void apply_sgr(int *p, int n)
{
    if (n == 0) { cur_fg = C_FG; cur_bg = C_BG; return; }
    for (int i = 0; i < n; i++) {
        int v = p[i];
        if (v == 0)                    { cur_fg = C_FG; cur_bg = C_BG; }
        else if (v >= 30 && v <= 37)   cur_fg = ansi_pal[v - 30];
        else if (v == 38 && i + 2 < n && p[i+1] == 5) { cur_fg = ansi_pal[p[i+2] & 15]; i += 2; }
        else if (v == 39)              cur_fg = C_FG;
        else if (v >= 40 && v <= 47)   cur_bg = ansi_pal[v - 40];
        else if (v == 48 && i + 2 < n && p[i+1] == 5) { cur_bg = ansi_pal[p[i+2] & 15]; i += 2; }
        else if (v == 49)              cur_bg = C_BG;
        else if (v >= 90 && v <= 97)   cur_fg = ansi_pal[v - 90 + 8];
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
    case 'G': cur_col = (p0 ? p0 : 1) - 1;
              if (cur_col < 0) cur_col = 0;
              if (cur_col >= TERM_COLS) cur_col = TERM_COLS - 1; break;
    case 'H': case 'f':
        cur_row = (p0 ? p0 : 1) - 1;
        cur_col = (p1 ? p1 : 1) - 1;
        if (cur_row < 0) cur_row = 0; if (cur_row >= TERM_ROWS) cur_row = TERM_ROWS - 1;
        if (cur_col < 0) cur_col = 0; if (cur_col >= TERM_COLS) cur_col = TERM_COLS - 1;
        break;
    case 'J':
        if (p0 == 0) clear_eos();
        else if (p0 == 1) clear_sol();
        else if (p0 == 2 || p0 == 3) clear_screen();
        break;
    case 'K':
        if (p0 == 0) clear_eol();
        else if (p0 == 1) clear_sol();
        else if (p0 == 2) {
            for (int c = 0; c < TERM_COLS; c++) {
                screen[cur_row][c] = (cell_t){' ', cur_fg, C_BG};
                render_cell(cur_row, c);
            }
        }
        break;
    case 'P': {
        int n = p0 ? p0 : 1;
        for (int c = cur_col; c < TERM_COLS - n; c++) {
            screen[cur_row][c] = screen[cur_row][c + n];
            render_cell(cur_row, c);
        }
        for (int c = TERM_COLS - n; c < TERM_COLS; c++) {
            screen[cur_row][c] = (cell_t){' ', cur_fg, C_BG};
            render_cell(cur_row, c);
        }
        break;
    }
    case 'm': apply_sgr(params, np); break;
    case 'l': case 'h': case 'r': case 's': case 'u': case 'n': break;
    default: break;
    }
}

static void put_raw(char c)
{
    cursor_show(0);
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
            screen[cur_row][cur_col] = (cell_t){' ', cur_fg, C_BG};
            render_cell(cur_row, cur_col);
            break;
        }
        if (c == '\t') {
            int next = (cur_col + 8) & ~7;
            if (next >= TERM_COLS) next = TERM_COLS - 1;
            for (; cur_col < next; cur_col++) {
                screen[cur_row][cur_col] = (cell_t){' ', cur_fg, cur_bg};
                render_cell(cur_row, cur_col);
            }
            break;
        }
        if (c == '\a') break;
        if ((unsigned char)c >= 32 && (unsigned char)c < 128) {
            if (cur_col >= TERM_COLS) {
                cur_col = 0; cur_row++;
                if (cur_row >= TERM_ROWS) { cur_row = TERM_ROWS - 1; scroll_up(); }
            }
            screen[cur_row][cur_col] = (cell_t){c, cur_fg, cur_bg};
            render_cell(cur_row, cur_col);
            cur_col++;
        }
        break;
    case ESC_ESC:
        if (c == '[') { esc_state = ESC_CSI; esc_len = 0; goto done; }
        if (c == 'c') clear_screen();
        if (c == 'M') { cur_row--; if (cur_row < 0) cur_row = 0; }
        esc_state = ESC_NONE;
        break;
    case ESC_CSI:
        if ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == '>' || c == '!') {
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
    cursor_show(1);
}

static void drain_master(void)
{
    cursor_show(0);
    char buf[1024];
    for (;;) {
        int avail = 0;
        zen_ioctl(master_fd, ZEN_FIONREAD, &avail);
        if (avail <= 0) break;
        int want = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
        int n = read(master_fd, buf, want);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) put_raw(buf[i]);
    }
    cursor_show(1);
    harp_flush(win);
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

static int pump_window_events(void)
{
    int handled = 0;
    harp_event_t event;
    while (harp_poll_event(win, &event))
    {
        handled = 1;
        if (event.type == HARP_EVENT_KEY && event.value != 0 && event.key != 0)
            send_key(event.key);
    }
    return handled;
}

int main(void)
{
    win = harp_open("Terminal", 60, 60, WIN_W, WIN_H);
    if (!win) {
        zen_log("terminal: harp not available", 2, 1);
        return 1;
    }

    memset(&sctx, 0, sizeof(sctx));
    if (ssfn_load(&sctx, &_binary_mono_sfn_start) != SSFN_OK) {
        zen_log("terminal: font load failed", 2, 1);
        harp_close(win);
        return 1;
    }
    ssfn_select(&sctx, SSFN_FAMILY_MONOSPACE, NULL, SSFN_STYLE_REGULAR, 16);

    sbuf.ptr = (uint8_t *)win->buf;
    sbuf.w   = WIN_W;
    sbuf.h   = WIN_H;
    sbuf.p   = WIN_W * 4;

    cur_row = 0; cur_col = 0;
    cur_fg  = C_FG; cur_bg = C_BG;
    cursor_vis = 0;
    esc_state = ESC_NONE; esc_len = 0;

    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            screen[r][c] = (cell_t){' ', C_FG, C_BG};
    buf_fill_rect(0, 0, WIN_W, WIN_H, C_BG);
    cursor_show(1);
    harp_flush(win);

    int slave_fd = -1;
    master_fd = zen_pty_open(&slave_fd);
    if (master_fd < 0) {
        const char *msg = "pty_open failed\n";
        for (const char *p = msg; *p; p++) put_raw(*p);
        harp_flush(win);
        for (;;) zen_halt();
    }

    zen_winsize_t ws = {
        (uint16_t)TERM_ROWS, (uint16_t)TERM_COLS,
        (uint16_t)WIN_W,     (uint16_t)WIN_H
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
        execv("/mnt/drv0/bin/shell", (char *[]){"/mnt/drv0/bin/shell", NULL});
        execv("/mnt/drv0/bin/sh",    (char *[]){"/mnt/drv0/bin/sh",    NULL});
        write(1, "shell not found\n", 16);
        _exit(1);
    }

    if (slave_fd >= 0) close(slave_fd);
    for (int i = 0; i < 6000000; i++) asm volatile("nop");
    while (1) {
        int status = 0;
        if (waitpid(shell_pid, &status, WNOHANG) == shell_pid) {
            cursor_show(0);
            uint32_t old_fg = cur_fg;
            cur_fg = 0xFFF9E2AF;
            const char *msg = "\r\n[shell exited]\r\n";
            for (const char *p = msg; *p; p++) put_raw(*p);
            cur_fg = old_fg;
            cursor_show(1);
            harp_flush(win);
            harp_close(win);
            return 0;
        }

        int avail = 0;
        zen_ioctl(master_fd, ZEN_FIONREAD, &avail);
        int did_work = 0;
        if (avail > 0) {
            drain_master();
            did_work = 1;
        }
        if (pump_window_events())
            did_work = 1;
        if (!did_work)
            zen_halt();
    }

    return 0;
}
