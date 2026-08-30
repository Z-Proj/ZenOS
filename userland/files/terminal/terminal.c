/**
 * 
 * @file : terminal.c
 * @brief : Terminal emulator with VT100/ANSI support.
 * 
 * MIT License
 * 
 * Copyright (c) 2026 Rishies2010
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * @author : Rishies2010
 * @copyright (c) 2026
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>

#include "../../include/harp_api.h"
#include "libfont.h"

#define WIN_W     600
#define WIN_H     400
#define FONT_SIZE 14

#define C_BG     0xFF0D1117
#define C_FG     0xFFCDD6F4
#define C_CURSOR 0xFFCDD6F4

#ifndef KEY_HOME
#define KEY_HOME 102
#endif
#ifndef KEY_PAGEUP
#define KEY_PAGEUP 104
#endif
#ifndef KEY_END
#define KEY_END 107
#endif
#ifndef KEY_PAGEDOWN
#define KEY_PAGEDOWN 109
#endif
#ifndef KEY_DELETE
#define KEY_DELETE 111
#endif

static const uint32_t ansi_pal[16] = {
    0xFF1E2030, 0xFFF38BA8, 0xFFA6E3A1, 0xFFF9E2AF,
    0xFF89B4FA, 0xFFCBA6F7, 0xFF89DCEB, 0xFFBAC2DE,
    0xFF45475A, 0xFFF38BA8, 0xFFA6E3A1, 0xFFF9E2AF,
    0xFF89B4FA, 0xFFCBA6F7, 0xFF94E2D5, 0xFFCDD6F4,
};

typedef struct { char ch; uint32_t fg; uint32_t bg; } cell_t;

static harp_window_t *win;
static font_face_t   *font;
static cell_t        *screen;

static int term_cols;
static int term_rows;
static int cell_w;
static int cell_h;
static int cell_baseline;
static int glyph_pad_x;

static int32_t  cur_row, cur_col;
static uint32_t cur_fg, cur_bg;
static int      cursor_vis;
static int      close_requested = 0;

static int master_fd  = -1;
static int shell_pid  = -1;
static int ctrl_down  = 0;
static int shift_down = 0;
static int alt_down   = 0;
static char font_path[64];

static int dirty_valid;
static int dirty_x0, dirty_y0, dirty_x1, dirty_y1;

#define ESC_NONE 0
#define ESC_ESC  1
#define ESC_CSI  2
static int  esc_state;
static char esc_buf[64];
static int  esc_len;

static void get_drive_root(char out[32])
{
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(out, "/mnt/drv0");
        return;
    }
    if (strncmp(cwd, "/mnt/drv", 8) != 0) {
        strcpy(out, "/mnt/drv0");
        return;
    }
    char *slash = cwd + 8;
    while (*slash && *slash != '/')
        slash++;
    size_t len = (size_t)(slash - cwd);
    if (len == 0 || len >= 32) {
        strcpy(out, "/mnt/drv0");
        return;
    }
    memcpy(out, cwd, len);
    out[len] = 0;
}

static inline cell_t *cell_at(int row, int col)
{
    return &screen[row * term_cols + col];
}

static inline void mark_dirty_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > WIN_W ? WIN_W : x + w;
    int y1 = y + h > WIN_H ? WIN_H : y + h;
    if (x0 >= x1 || y0 >= y1)
        return;
    if (!dirty_valid) {
        dirty_valid = 1;
        dirty_x0 = x0;
        dirty_y0 = y0;
        dirty_x1 = x1;
        dirty_y1 = y1;
        return;
    }
    if (x0 < dirty_x0) dirty_x0 = x0;
    if (y0 < dirty_y0) dirty_y0 = y0;
    if (x1 > dirty_x1) dirty_x1 = x1;
    if (y1 > dirty_y1) dirty_y1 = y1;
}

static inline void mark_dirty_cell(int row, int col)
{
    mark_dirty_rect(col * cell_w, row * cell_h, cell_w, cell_h);
}

static void flush_dirty(void)
{
    if (!dirty_valid)
        return;
    harp_flush_rect(win, dirty_x0, dirty_y0, dirty_x1 - dirty_x0, dirty_y1 - dirty_y0);
    dirty_valid = 0;
}

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
    cell_t *c = cell_at(row, col);
    int x = col * cell_w;
    int y = row * cell_h;
    buf_fill_rect(x, y, cell_w, cell_h, c->bg);
    if ((unsigned char)c->ch >= 32 && (unsigned char)c->ch < 127) {
        font_draw_codepoint(font, win->buf, WIN_W, WIN_H,
                            x + glyph_pad_x, y + cell_baseline, FONT_SIZE,
                            c->fg, c->bg, (uint32_t)(unsigned char)c->ch);
    }
    mark_dirty_cell(row, col);
}

static void cursor_show(int vis)
{
    if (cursor_vis == vis)
        return;
    cursor_vis = vis;
    int x = cur_col * cell_w;
    int y = cur_row * cell_h + cell_h - 3;
    uint32_t c = vis ? C_CURSOR : cell_at(cur_row, cur_col)->bg;
    buf_fill_rect(x, y, cell_w, 3, c);
    mark_dirty_rect(x, y, cell_w, 3);
}

static void scroll_up(void)
{
    size_t scroll_px = (size_t)cell_h * (size_t)WIN_W;
    memmove(win->buf, win->buf + scroll_px,
            (WIN_H - cell_h) * WIN_W * sizeof(uint32_t));
    buf_fill_rect(0, WIN_H - cell_h, WIN_W, cell_h, C_BG);
    memmove(screen, screen + term_cols,
            (size_t)(term_rows - 1) * (size_t)term_cols * sizeof(*screen));
    for (int c = 0; c < term_cols; c++)
        *cell_at(term_rows - 1, c) = (cell_t){' ', cur_fg, C_BG};
    mark_dirty_rect(0, 0, WIN_W, WIN_H);
}

static void clear_screen(void)
{
    buf_fill_rect(0, 0, WIN_W, WIN_H, C_BG);
    for (int r = 0; r < term_rows; r++)
        for (int c = 0; c < term_cols; c++)
            *cell_at(r, c) = (cell_t){' ', C_FG, C_BG};
    cur_row = 0;
    cur_col = 0;
    mark_dirty_rect(0, 0, WIN_W, WIN_H);
}

static void clear_eol(void)
{
    for (int c = cur_col; c < term_cols; c++) {
        cell_t next = {' ', cur_fg, C_BG};
        if (memcmp(cell_at(cur_row, c), &next, sizeof(next)) != 0) {
            *cell_at(cur_row, c) = next;
            render_cell(cur_row, c);
        }
    }
}

static void clear_eos(void)
{
    clear_eol();
    for (int r = cur_row + 1; r < term_rows; r++)
        for (int c = 0; c < term_cols; c++) {
            cell_t next = {' ', C_FG, C_BG};
            if (memcmp(cell_at(r, c), &next, sizeof(next)) != 0) {
                *cell_at(r, c) = next;
                render_cell(r, c);
            }
        }
}

static void clear_sol(void)
{
    for (int c = 0; c <= cur_col; c++) {
        cell_t next = {' ', cur_fg, C_BG};
        if (memcmp(cell_at(cur_row, c), &next, sizeof(next)) != 0) {
            *cell_at(cur_row, c) = next;
            render_cell(cur_row, c);
        }
    }
}

static void apply_sgr(int *p, int n)
{
    if (n == 0) { cur_fg = C_FG; cur_bg = C_BG; return; }
    for (int i = 0; i < n; i++) {
        int v = p[i];
        if (v == 0)                    { cur_fg = C_FG; cur_bg = C_BG; }
        else if (v >= 30 && v <= 37)   cur_fg = ansi_pal[v - 30];
        else if (v == 38 && i + 2 < n && p[i + 1] == 5) { cur_fg = ansi_pal[p[i + 2] & 15]; i += 2; }
        else if (v == 39)              cur_fg = C_FG;
        else if (v >= 40 && v <= 47)   cur_bg = ansi_pal[v - 40];
        else if (v == 48 && i + 2 < n && p[i + 1] == 5) { cur_bg = ansi_pal[p[i + 2] & 15]; i += 2; }
        else if (v == 49)              cur_bg = C_BG;
        else if (v >= 90 && v <= 97)   cur_fg = ansi_pal[v - 90 + 8];
        else if (v >= 100 && v <= 107) cur_bg = ansi_pal[v - 100 + 8];
    }
}

static void dispatch_csi(char cmd)
{
    int params[16] = {0};
    int np = 0;
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
    case 'B': cur_row += p0 ? p0 : 1; if (cur_row >= term_rows) cur_row = term_rows - 1; break;
    case 'C': cur_col += p0 ? p0 : 1; if (cur_col >= term_cols) cur_col = term_cols - 1; break;
    case 'D': cur_col -= p0 ? p0 : 1; if (cur_col < 0) cur_col = 0; break;
    case 'G':
        cur_col = (p0 ? p0 : 1) - 1;
        if (cur_col < 0) cur_col = 0;
        if (cur_col >= term_cols) cur_col = term_cols - 1;
        break;
    case 'H':
    case 'f':
        cur_row = (p0 ? p0 : 1) - 1;
        cur_col = (p1 ? p1 : 1) - 1;
        if (cur_row < 0) cur_row = 0;
        if (cur_row >= term_rows) cur_row = term_rows - 1;
        if (cur_col < 0) cur_col = 0;
        if (cur_col >= term_cols) cur_col = term_cols - 1;
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
            for (int c = 0; c < term_cols; c++) {
                *cell_at(cur_row, c) = (cell_t){' ', cur_fg, C_BG};
                render_cell(cur_row, c);
            }
        }
        break;
    case 'P': {
        int n = p0 ? p0 : 1;
        if (n > term_cols - cur_col)
            n = term_cols - cur_col;
        for (int c = cur_col; c < term_cols - n; c++) {
            *cell_at(cur_row, c) = *cell_at(cur_row, c + n);
            render_cell(cur_row, c);
        }
        for (int c = term_cols - n; c < term_cols; c++) {
            *cell_at(cur_row, c) = (cell_t){' ', cur_fg, C_BG};
            render_cell(cur_row, c);
        }
        break;
    }
    case 'm':
        apply_sgr(params, np);
        break;
    case 'l':
    case 'h':
    case 'r':
    case 's':
    case 'u':
    case 'n':
        break;
    default:
        break;
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
            if (cur_row >= term_rows) { cur_row = term_rows - 1; scroll_up(); }
            break;
        }
        if (c == '\b') {
            if (cur_col > 0) cur_col--;
            *cell_at(cur_row, cur_col) = (cell_t){' ', cur_fg, C_BG};
            render_cell(cur_row, cur_col);
            break;
        }
        if (c == '\t') {
            int next = (cur_col + 8) & ~7;
            if (next >= term_cols) next = term_cols - 1;
            for (; cur_col < next; cur_col++) {
                cell_t next_cell = {' ', cur_fg, cur_bg};
                if (memcmp(cell_at(cur_row, cur_col), &next_cell, sizeof(next_cell)) != 0) {
                    *cell_at(cur_row, cur_col) = next_cell;
                    render_cell(cur_row, cur_col);
                }
            }
            break;
        }
        if (c == '\a') break;
        if ((unsigned char)c >= 32 && (unsigned char)c < 128) {
            if (cur_col >= term_cols) {
                cur_col = 0;
                cur_row++;
                if (cur_row >= term_rows) { cur_row = term_rows - 1; scroll_up(); }
            }
            cell_t next_cell = {c, cur_fg, cur_bg};
            if (memcmp(cell_at(cur_row, cur_col), &next_cell, sizeof(next_cell)) != 0) {
                *cell_at(cur_row, cur_col) = next_cell;
                render_cell(cur_row, cur_col);
            }
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
            if (esc_len < (int)sizeof(esc_buf) - 1) esc_buf[esc_len++] = c;
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
        for (int i = 0; i < n; i++)
            put_raw(buf[i]);
    }
    cursor_show(1);
    flush_dirty();
}

static void send_bytes(const char *s, size_t n)
{
    if (master_fd >= 0 && s && n)
        write(master_fd, s, n);
}

static void send_char(char c)
{
    send_bytes(&c, 1);
}

static void send_esc_prefixed_char(char c)
{
    send_char(27);
    send_char(c);
}

static void send_csi_key(char final, uint32_t mods)
{
    int modifier = 1;
    int shift = (mods & HARP_MOD_SHIFT) != 0;
    int alt = (mods & HARP_MOD_ALT) != 0;
    int ctrl = (mods & HARP_MOD_CTRL) != 0;

    if (shift && alt && ctrl) modifier = 8;
    else if (alt && ctrl) modifier = 7;
    else if (shift && ctrl) modifier = 6;
    else if (ctrl) modifier = 5;
    else if (shift && alt) modifier = 4;
    else if (alt) modifier = 3;
    else if (shift) modifier = 2;

    if (modifier == 1) {
        char seq[3] = {'\033', '[', final};
        send_bytes(seq, sizeof(seq));
        return;
    }

    char seq[8];
    int len = snprintf(seq, sizeof(seq), "\033[1;%d%c", modifier, final);
    if (len > 0)
        send_bytes(seq, (size_t)len);
}

static void send_tilde_key(int number, uint32_t mods)
{
    int modifier = 1;
    int shift = (mods & HARP_MOD_SHIFT) != 0;
    int alt = (mods & HARP_MOD_ALT) != 0;
    int ctrl = (mods & HARP_MOD_CTRL) != 0;

    if (shift && alt && ctrl) modifier = 8;
    else if (alt && ctrl) modifier = 7;
    else if (shift && ctrl) modifier = 6;
    else if (ctrl) modifier = 5;
    else if (shift && alt) modifier = 4;
    else if (alt) modifier = 3;
    else if (shift) modifier = 2;

    char seq[12];
    int len;
    if (modifier == 1)
        len = snprintf(seq, sizeof(seq), "\033[%d~", number);
    else
        len = snprintf(seq, sizeof(seq), "\033[%d;%d~", number, modifier);
    if (len > 0)
        send_bytes(seq, (size_t)len);
}

static int ctrl_code_from_keycode(uint16_t code, int *out)
{
    if (!out)
        return 0;
    switch (code) {
    case KEY_A: *out = 1; return 1;
    case KEY_B: *out = 2; return 1;
    case KEY_C: *out = 3; return 1;
    case KEY_D: *out = 4; return 1;
    case KEY_E: *out = 5; return 1;
    case KEY_F: *out = 6; return 1;
    case KEY_G: *out = 7; return 1;
    case KEY_H: *out = 8; return 1;
    case KEY_I: *out = 9; return 1;
    case KEY_J: *out = 10; return 1;
    case KEY_K: *out = 11; return 1;
    case KEY_L: *out = 12; return 1;
    case KEY_M: *out = 13; return 1;
    case KEY_N: *out = 14; return 1;
    case KEY_O: *out = 15; return 1;
    case KEY_P: *out = 16; return 1;
    case KEY_Q: *out = 17; return 1;
    case KEY_R: *out = 18; return 1;
    case KEY_S: *out = 19; return 1;
    case KEY_T: *out = 20; return 1;
    case KEY_U: *out = 21; return 1;
    case KEY_V: *out = 22; return 1;
    case KEY_W: *out = 23; return 1;
    case KEY_X: *out = 24; return 1;
    case KEY_Y: *out = 25; return 1;
    case KEY_Z: *out = 26; return 1;
    case KEY_LEFTBRACE: *out = 27; return 1;
    case KEY_BACKSLASH: *out = 28; return 1;
    case KEY_RIGHTBRACE: *out = 29; return 1;
    case KEY_6: *out = 30; return 1;
    case KEY_MINUS: *out = 31; return 1;
    case KEY_SPACE: *out = 0; return 1;
    default:
        return 0;
    }
}

static void send_key_event(const harp_event_t *ev)
{
    if (!ev)
        return;

    uint16_t code = ev->code;
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
        shift_down = ev->value != 0;
        return;
    }
    if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
        ctrl_down = ev->value != 0;
        return;
    }
    if (code == KEY_LEFTALT || code == KEY_RIGHTALT) {
        alt_down = ev->value != 0;
        return;
    }

    if (ev->value == 0)
        return;

    uint32_t mods = ev->modifiers;
    if (shift_down) mods |= HARP_MOD_SHIFT;
    if (ctrl_down)  mods |= HARP_MOD_CTRL;
    if (alt_down)   mods |= HARP_MOD_ALT;

    if (mods & HARP_MOD_CTRL) {
        int ctrl_code = -1;
        if (ev->key >= 0 && ev->key < 32)
            ctrl_code = ev->key;
        else if (ctrl_code_from_keycode(code, &ctrl_code)) {
        }
        if (ctrl_code >= 0) {
            if (mods & HARP_MOD_ALT)
                send_char(27);
            send_char((char)ctrl_code);
            return;
        }
    }

    switch (code) {
    case KEY_UP:        send_csi_key('A', mods); return;
    case KEY_DOWN:      send_csi_key('B', mods); return;
    case KEY_RIGHT:     send_csi_key('C', mods); return;
    case KEY_LEFT:      send_csi_key('D', mods); return;
    case KEY_HOME:      send_csi_key('H', mods); return;
    case KEY_END:       send_csi_key('F', mods); return;
    case KEY_DELETE:    send_tilde_key(3, mods); return;
    case KEY_PAGEUP:    send_tilde_key(5, mods); return;
    case KEY_PAGEDOWN:  send_tilde_key(6, mods); return;
    case KEY_ENTER:
        if (mods & HARP_MOD_ALT)
            send_char(27);
        send_char('\r');
        return;
    case KEY_BACKSPACE:
        if (mods & HARP_MOD_ALT)
            send_char(27);
        send_char(127);
        return;
    case KEY_ESC:       send_char(27); return;
    case KEY_TAB:
        if (mods & HARP_MOD_SHIFT) {
            send_bytes("\033[Z", 3);
            return;
        }
        if (mods & HARP_MOD_ALT)
            send_char(27);
        send_char('\t');
        return;
    default:
        break;
    }

    if (ev->key != 0) {
        char c = (char)ev->key;
        if (mods & HARP_MOD_ALT)
            send_esc_prefixed_char(c);
        else
            send_char(c);
    }
}

static int pump_window_events(void)
{
    int handled = 0;
    harp_event_t ev;
    while (harp_poll_event(win, &ev)) {
        handled = 1;
        if (ev.type == HARP_EVENT_KEY)
            send_key_event(&ev);
        else if (ev.type == HARP_EVENT_CLOSE_REQ)
            close_requested = 1;
    }
    return handled;
}

static void cleanup_terminal(void)
{
    free(screen);
    screen = NULL;
    if (font) {
        font_free(font);
        font = NULL;
    }
    if (win) {
        harp_close(win);
        win = NULL;
    }
}

int main(void)
{
    win = harp_open("Terminal", 60, 60, WIN_W, WIN_H);
    if (!win) {
        zen_log("terminal: harp not available", 2, 1);
        return 1;
    }

    char drive_root[32];
    get_drive_root(drive_root);
    snprintf(font_path, sizeof(font_path), "%s/lib/fonts/monospace.ttf", drive_root);
    font = font_load(font_path);
    if (!font && strcmp(font_path, "/mnt/drv0/lib/fonts/monospace.ttf") != 0)
        font = font_load("/mnt/drv0/lib/fonts/monospace.ttf");
    if (!font)
        font = font_load("/mnt/drv0/lib/fonts/default.ttf");
    if (!font) {
        zen_log("terminal: font load failed", 2, 1);
        cleanup_terminal();
        return 1;
    }
    font_prime_ascii(font, FONT_SIZE);

    font_metrics_t metrics;
    if (!font_get_metrics(font, FONT_SIZE, &metrics)) {
        zen_log("terminal: font metrics failed", 2, 1);
        cleanup_terminal();
        return 1;
    }

    cell_w = metrics.max_advance - 1;
    if (cell_w < 8) cell_w = 8;
    cell_h = metrics.line_height + 2;
    if (cell_h < FONT_SIZE + 1) cell_h = FONT_SIZE + 1;
    glyph_pad_x = 1;
    cell_baseline = 1 + metrics.ascent;
    term_cols = WIN_W / cell_w;
    term_rows = WIN_H / cell_h;
    if (term_cols <= 0 || term_rows <= 0) {
        zen_log("terminal: invalid grid metrics", 2, 1);
        cleanup_terminal();
        return 1;
    }

    screen = (cell_t *)calloc((size_t)term_rows * (size_t)term_cols, sizeof(*screen));
    if (!screen) {
        zen_log("terminal: screen alloc failed", 2, 1);
        cleanup_terminal();
        return 1;
    }

    cur_row = 0;
    cur_col = 0;
    cur_fg  = C_FG;
    cur_bg  = C_BG;
    cursor_vis = 0;
    esc_state = ESC_NONE;
    esc_len = 0;
    dirty_valid = 0;

    clear_screen();
    cursor_show(1);
    flush_dirty();

    int slave_fd = -1;
    master_fd = zen_pty_open(&slave_fd);
    if (master_fd < 0) {
        const char *msg = "pty_open failed\n";
        for (const char *p = msg; *p; p++) put_raw(*p);
        flush_dirty();
        for (;;)
            sched_yield();
    }

    zen_winsize_t ws = {
        (uint16_t)term_rows, (uint16_t)term_cols,
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
        tios.c_cc[VINTR]  = 3;   tios.c_cc[VQUIT]  = 28;
        tios.c_cc[VERASE] = 127; tios.c_cc[VKILL]  = 21;
        tios.c_cc[VEOF]   = 4;   tios.c_cc[VTIME]  = 0;
        tios.c_cc[VMIN]   = 1;   tios.c_cc[VSTART] = 17;
        tios.c_cc[VSTOP]  = 19;  tios.c_cc[VSUSP]  = 26;
        tcsetattr(slave_fd, TCSANOW, &tios);
    }

    shell_pid = fork();
    if (shell_pid == 0) {
        dup2(slave_fd, 0);
        dup2(slave_fd, 1);
        dup2(slave_fd, 2);
        if (slave_fd > 2) close(slave_fd);
        if (master_fd > 2) close(master_fd);
        execv("/mnt/drv0/bin/ash", (char *[]){"/mnt/drv0/bin/ash", NULL});
        execv("/mnt/drv0/bin/shell",    (char *[]){"/mnt/drv0/bin/shell",    NULL});
        execv("/mnt/drv0/bin/sh",    (char *[]){"/mnt/drv0/bin/sh",    NULL});
        write(1, "shell not found\n", 16);
        _exit(1);
    }
    if (slave_fd >= 0)
        close(slave_fd);

    while (1) {
        if (close_requested) {
            if (shell_pid > 0)
                zen_kill(shell_pid, 15);
            cleanup_terminal();
            return 0;
        }

        int status = 0;
        if (waitpid(shell_pid, &status, WNOHANG) == shell_pid) {
            cursor_show(0);
            uint32_t old_fg = cur_fg;
            cur_fg = 0xFFF9E2AF;
            const char *msg = "\r\n[shell exited]\r\n";
            for (const char *p = msg; *p; p++) put_raw(*p);
            cur_fg = old_fg;
            cursor_show(1);
            flush_dirty();
            cleanup_terminal();
            return 0;
        }

        int avail = 0;
        zen_ioctl(master_fd, ZEN_FIONREAD, &avail);
        int did_work = 0;
        if (avail > 0) { drain_master(); did_work = 1; }
        if (pump_window_events()) did_work = 1;
        if (!did_work)
            sched_yield();
    }
}
