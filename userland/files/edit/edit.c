

#define SSFN_IMPLEMENTATION
#define SSFN_MAXLINES 4096
#define SSFN_memcmp memcmp
#define SSFN_memset memset
#define SSFN_realloc realloc
#define SSFN_free free
#include "ssfn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "../../userlib.h"

extern char _binary_mono_sfn_start;
extern char _binary_mono_sfn_end;

#define KEY_UP KEY_ARROW_UP
#define KEY_DOWN KEY_ARROW_DOWN
#define KEY_LEFT KEY_ARROW_LEFT
#define KEY_RIGHT KEY_ARROW_RIGHT
#define KEY_BACKSPACE '\b'
#define KEY_ENTER '\n'
#define KEY_TAB '\t'
#define KEY_ESC 0x1B

#define FONT_SIZE 14
#define LINE_H (FONT_SIZE + 5)
#define LINENUM_W 48
#define TITLE_H 26
#define STATUS_H 22
#define SCROLL_W 10
#define BASELINE(top) ((top) + FONT_SIZE)

#define C_BG 0xFF12121E
#define C_GUTTER 0xFF0D0D18
#define C_GUTTER_SEP 0xFF2A2A55
#define C_CURLINE 0xFF1A1A35
#define C_TITLE 0xFF0F3460
#define C_TITLE_ACC 0xFF00D4FF
#define C_STATUS 0xFF0A0A1A
#define C_TEXT 0xFFDDDDDD
#define C_LINENUM 0xFF555577
#define C_LINENUM_CUR 0xFFAABBFF
#define C_CURSOR 0xFF00D4FF
#define C_MODIFIED 0xFFFF6B6B
#define C_SAVED 0xFF6BFF9E
#define C_SCROLLBG 0xFF1A1A30
#define C_SCROLLTHUMB 0xFF3A6090
#define C_MENU_BG 0xFF1E1E3A
#define C_MENU_BORDER 0xFF00D4FF
#define C_MENU_KEY 0xFF00D4FF
#define C_MENU_TEXT 0xFFDDDDDD

#define MAX_LINES 4096
#define MAX_LINE_LEN 1024
#define MAX_FILENAME 256

#define CURSOR_W 2

static ssfn_t ssfn;
static ssfn_buf_t canvas;
static fb_info_t fb;

static int SCR_W, SCR_H;
static int ED_X, ED_Y, ED_W, ED_H;

static char lines[MAX_LINES][MAX_LINE_LEN];
static int line_len[MAX_LINES];
static int num_lines = 1;
static int cur_row = 0;
static int cur_col = 0;
static int scroll_row = 0;
static int modified = 0;
static char filename[MAX_FILENAME];
static int has_file = 0;
static int menu_open = 0;

static inline void fb_fill_rect(int x, int y, int w, int h, uint32_t col)
{
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > SCR_W)
        w = SCR_W - x;
    if (y + h > SCR_H)
        h = SCR_H - y;
    if (w <= 0 || h <= 0)
        return;
    uint32_t bpp_b = fb.bpp >> 3;
    uint8_t *base = (uint8_t *)(uintptr_t)fb.addr + (uint32_t)(fb.width / 2) * bpp_b;
    uint8_t r = (col >> 16) & 0xFF;
    uint8_t g = (col >> 8) & 0xFF;
    uint8_t b = col & 0xFF;
    for (int row = y; row < y + h; row++)
    {
        uint8_t *p = base + row * fb.pitch + x * bpp_b;
        for (int c = 0; c < w; c++, p += bpp_b)
        {
            if (fb.bpp == 32)
            {
                p[0] = b;
                p[1] = g;
                p[2] = r;
                p[3] = 0xFF;
            }
            else if (fb.bpp == 24)
            {
                p[0] = b;
                p[1] = g;
                p[2] = r;
            }
        }
    }
}

static void fb_text(int x, int by, uint32_t fg, int size, const char *str, int clip_x)
{
    if (!str || !*str)
        return;
    ssfn_select(&ssfn, SSFN_FAMILY_ANY, NULL, SSFN_STYLE_REGULAR, size);
    canvas.x = x;
    canvas.y = by;
    canvas.fg = fg;
    canvas.bg = 0;
    const char *s = str;
    while (*s)
    {
        if (clip_x > 0 && canvas.x >= clip_x)
            break;
        int r = ssfn_render(&ssfn, &canvas, s);
        if (r <= 0)
            break;
        s += r;
    }
}

static int measure_n(const char *s, int n)
{
    if (!s || n <= 0)
        return 0;
    char tmp[MAX_LINE_LEN + 1];
    if (n > MAX_LINE_LEN)
        n = MAX_LINE_LEN;
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    ssfn_select(&ssfn, SSFN_FAMILY_ANY, NULL, SSFN_STYLE_REGULAR, FONT_SIZE);
    ssfn_buf_t d;
    memset(&d, 0, sizeof(d));
    d.ptr = NULL;
    d.w = 0x7FFF;
    d.h = 0x7FFF;
    d.p = 0x7FFF * 4;
    d.x = 0;
    d.y = FONT_SIZE;
    d.fg = 0xFFFFFFFF;
    const char *p = tmp;
    while (*p)
    {
        int r = ssfn_render(&ssfn, &d, p);
        if (r <= 0)
            break;
        p += r;
    }
    return d.x;
}

static void recalc_geo(void)
{
    SCR_W = (int)(fb.width / 2);
    SCR_H = (int)fb.height;
    ED_X = LINENUM_W;
    ED_Y = TITLE_H;
    ED_W = SCR_W - LINENUM_W - SCROLL_W;
    ED_H = SCR_H - TITLE_H - STATUS_H;
    canvas.ptr = (uint8_t *)(uintptr_t)fb.addr + (uint32_t)(fb.width / 2) * (fb.bpp >> 3);
    canvas.w = SCR_W;
    canvas.h = SCR_H;
    canvas.p = fb.pitch;
}

static int vis_rows(void) { return ED_H / LINE_H; }

static void editor_load(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return;
    static char buf[MAX_LINES * MAX_LINE_LEN];
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = '\0';
    num_lines = 0;
    int li = 0;
    for (int i = 0; i <= n && num_lines < MAX_LINES; i++)
    {
        char c = buf[i];
        if (c == '\n' || c == '\0')
        {
            lines[num_lines][li] = '\0';
            line_len[num_lines] = li;
            num_lines++;
            li = 0;
        }
        else if (li < MAX_LINE_LEN - 1)
            lines[num_lines][li++] = c;
    }
    if (!num_lines)
    {
        num_lines = 1;
        line_len[0] = 0;
        lines[0][0] = 0;
    }
    modified = 0;
}

static int editor_save(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        zen_create(path);
        fd = open(path, O_WRONLY, 0644);
    }
    if (fd < 0)
        return -1;
    for (int i = 0; i < num_lines; i++)
    {
        write(fd, lines[i], line_len[i]);
        write(fd, "\n", 1);
    }
    close(fd);
    modified = 0;
    return 0;
}

static void clamp_cursor(void)
{
    if (cur_row < 0)
        cur_row = 0;
    if (cur_row >= num_lines)
        cur_row = num_lines - 1;
    if (cur_col < 0)
        cur_col = 0;
    if (cur_col > line_len[cur_row])
        cur_col = line_len[cur_row];
    int vr = vis_rows();
    if (cur_row < scroll_row)
        scroll_row = cur_row;
    if (cur_row >= scroll_row + vr)
        scroll_row = cur_row - vr + 1;
}

static void insert_char(char c)
{
    if (line_len[cur_row] >= MAX_LINE_LEN - 1)
        return;
    char *ln = lines[cur_row];
    memmove(ln + cur_col + 1, ln + cur_col, line_len[cur_row] - cur_col + 1);
    ln[cur_col] = c;
    line_len[cur_row]++;
    cur_col++;
    modified = 1;
}
static void insert_newline(void)
{
    if (num_lines >= MAX_LINES)
        return;
    int rest = line_len[cur_row] - cur_col;
    for (int i = num_lines; i > cur_row + 1; i--)
    {
        memcpy(lines[i], lines[i - 1], line_len[i - 1] + 1);
        line_len[i] = line_len[i - 1];
    }
    num_lines++;
    memcpy(lines[cur_row + 1], lines[cur_row] + cur_col, rest);
    lines[cur_row + 1][rest] = '\0';
    line_len[cur_row + 1] = rest;
    lines[cur_row][cur_col] = '\0';
    line_len[cur_row] = cur_col;
    cur_row++;
    cur_col = 0;
    modified = 1;
}
static void backspace_char(void)
{
    if (cur_col > 0)
    {
        memmove(lines[cur_row] + cur_col - 1, lines[cur_row] + cur_col,
                line_len[cur_row] - cur_col + 1);
        line_len[cur_row]--;
        cur_col--;
        modified = 1;
    }
    else if (cur_row > 0)
    {
        int pl = line_len[cur_row - 1], cl = line_len[cur_row];
        if (pl + cl < MAX_LINE_LEN - 1)
        {
            memcpy(lines[cur_row - 1] + pl, lines[cur_row], cl + 1);
            line_len[cur_row - 1] += cl;
            for (int i = cur_row; i < num_lines - 1; i++)
            {
                memcpy(lines[i], lines[i + 1], line_len[i + 1] + 1);
                line_len[i] = line_len[i + 1];
            }
            num_lines--;
            cur_row--;
            cur_col = pl;
            modified = 1;
        }
    }
}

static int draw_row(int row, int vi_start, int draw_cur)
{
    int clip_r = ED_X + ED_W;
    int vr = vis_rows();
    int is_cur = draw_cur && (row == cur_row);

    int vi = vi_start;
    int py = ED_Y + vi * LINE_H;

    fb_fill_rect(0, py, LINENUM_W, LINE_H, C_GUTTER);
    fb_fill_rect(LINENUM_W - 1, py, 1, LINE_H, C_GUTTER_SEP);
    fb_fill_rect(ED_X, py, ED_W, LINE_H, is_cur ? C_CURLINE : C_BG);

    char lnbuf[8];
    snprintf(lnbuf, sizeof(lnbuf), "%4d", row + 1);
    fb_text(2, BASELINE(py), is_cur ? C_LINENUM_CUR : C_LINENUM, FONT_SIZE, lnbuf, 0);

    char *ln = lines[row];
    int len = line_len[row];

    ssfn_select(&ssfn, SSFN_FAMILY_ANY, NULL, SSFN_STYLE_REGULAR, FONT_SIZE);
    canvas.x = ED_X;
    canvas.y = BASELINE(py);
    canvas.fg = C_TEXT;
    canvas.bg = 0;

    int cursor_x = -1;
    int cursor_vi = vi;

    int vis_consumed = 1;

    for (int col = 0; col <= len; col++)
    {

        if (is_cur && col == cur_col)
        {
            cursor_x = canvas.x;
            cursor_vi = vi;
        }

        if (col == len)
            break;

        char c = ln[col];
        if ((unsigned char)c < 0x20)
            continue;

        if (canvas.x >= clip_r)
        {
            vi++;
            if (vi >= vi_start + vr)
                break;
            int npy = ED_Y + vi * LINE_H;
            fb_fill_rect(0, npy, LINENUM_W, LINE_H, C_GUTTER);
            fb_fill_rect(LINENUM_W - 1, npy, 1, LINE_H, C_GUTTER_SEP);
            fb_fill_rect(ED_X, npy, ED_W, LINE_H, is_cur ? C_CURLINE : C_BG);
            canvas.x = ED_X;
            canvas.y = BASELINE(npy);
            vis_consumed++;
        }

        char tmp[2] = {c, 0};
        ssfn_render(&ssfn, &canvas, tmp);
    }

    if (is_cur)
    {
        int cx = (cursor_x >= 0) ? cursor_x : canvas.x;
        int cpy = ED_Y + cursor_vi * LINE_H;
        if (cx + CURSOR_W > clip_r)
            cx = clip_r - CURSOR_W;
        if (cx < ED_X)
            cx = ED_X;
        fb_fill_rect(cx, cpy, CURSOR_W, LINE_H, C_CURSOR);
    }

    return vis_consumed;
}

static int row_vis_height(int row)
{
    int len = line_len[row];
    if (len == 0)
        return 1;
    int text_w = measure_n(lines[row], len);
    int rows = (text_w + ED_W - 1) / ED_W;
    return rows < 1 ? 1 : rows;
}

static void redraw_row(int row)
{
    int vi = row - scroll_row;
    if (vi < 0 || vi >= vis_rows())
        return;
    draw_row(row, vi, 1);
}

static void redraw_from(int from_row)
{
    int vr = vis_rows();
    int vi = from_row - scroll_row;
    if (vi < 0)
        vi = 0;

    for (int row = from_row; row < num_lines && vi < vr; row++)
    {
        int consumed = draw_row(row, vi, 1);
        vi += consumed;
    }

    while (vi < vr)
    {
        int py = ED_Y + vi * LINE_H;
        fb_fill_rect(0, py, LINENUM_W, LINE_H, C_GUTTER);
        fb_fill_rect(LINENUM_W - 1, py, 1, LINE_H, C_GUTTER_SEP);
        fb_fill_rect(ED_X, py, ED_W, LINE_H, C_BG);
        vi++;
    }
}

static void draw_title(void)
{
    fb_fill_rect(0, 0, SCR_W, TITLE_H, C_TITLE);
    fb_fill_rect(0, TITLE_H - 2, SCR_W, 2, C_TITLE_ACC);
    char t[300];
    if (has_file)
        snprintf(t, sizeof(t), " ZenEdit  %s%s", filename, modified ? "  [+]" : "");
    else
        snprintf(t, sizeof(t), " ZenEdit  [New File]%s", modified ? "  [+]" : "");
    fb_text(6, BASELINE(4), C_TITLE_ACC, FONT_SIZE, t, 0);
    fb_text(SCR_W - 80, BASELINE(4), C_LINENUM, FONT_SIZE, "ESC:Menu", 0);
}

static void draw_status(void)
{
    int sy = SCR_H - STATUS_H;
    fb_fill_rect(0, sy, SCR_W, STATUS_H, C_STATUS);
    fb_fill_rect(0, sy, SCR_W, 1, C_TITLE_ACC);
    char s[220];
    snprintf(s, sizeof(s), "  Ln %d/%d   Col %d   %s",
             cur_row + 1, num_lines, cur_col + 1, modified ? "UNSAVED" : "saved");
    fb_text(4, BASELINE(sy + 4), modified ? C_MODIFIED : C_SAVED, FONT_SIZE - 2, s, 0);
}

static void draw_scrollbar(void)
{
    int sx = SCR_W - SCROLL_W, sy = ED_Y, sh = ED_H;
    fb_fill_rect(sx, sy, SCROLL_W, sh, C_SCROLLBG);
    if (num_lines > 0)
    {
        int th = sh * vis_rows() / num_lines;
        if (th < 8)
            th = 8;
        if (th > sh)
            th = sh;
        int denom = num_lines - vis_rows();
        int ty = sy + (denom > 0 ? (sh - th) * scroll_row / denom : 0);
        fb_fill_rect(sx + 1, ty, SCROLL_W - 2, th, C_SCROLLTHUMB);
    }
}

static void draw_menu(void)
{
    int mw = 260, mh = 220;
    int mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
    fb_fill_rect(mx, my, mw, mh, C_MENU_BG);
    fb_fill_rect(mx, my, mw, 2, C_MENU_BORDER);
    fb_fill_rect(mx, my + mh - 2, mw, 2, C_MENU_BORDER);
    fb_fill_rect(mx, my, 2, mh, C_MENU_BORDER);
    fb_fill_rect(mx + mw - 2, my, 2, mh, C_MENU_BORDER);

    int tx = mx + 16;
    int fs = FONT_SIZE - 1;
    fb_text(tx, BASELINE(my + 10), C_TITLE_ACC, FONT_SIZE, "Menu", 0);

#define MROW(dy, key, label)                                  \
    fb_text(tx, BASELINE(my + (dy)), C_MENU_KEY, fs, key, 0); \
    fb_text(tx + 22, BASELINE(my + (dy)), C_MENU_TEXT, fs, label, 0);

    MROW(36, "S", "Save file")
    MROW(58, "Q", "Quit")
    MROW(86, "H", "Line start  (Home)")
    MROW(108, "E", "Line end    (End)")
    MROW(130, "T", "Top of file")
    MROW(152, "B", "Bottom of file")
    MROW(174, "U", "Page up")
    MROW(196, "D", "Page down")
#undef MROW

    fb_text(tx, BASELINE(my + mh - 18), C_LINENUM, FONT_SIZE - 2, "ESC  close", 0);
}

static void redraw_all(void)
{
    draw_title();
    redraw_from(scroll_row);
    draw_status();
    draw_scrollbar();
    if (menu_open)
        draw_menu();
}

int main(int argc, char **argv)
{
    if (zen_fbinfo(&fb) != 0)
    {
        write(1, "edit: no framebuffer\n", 21);
        return 1;
    }
    recalc_geo();

    memset(&ssfn, 0, sizeof(ssfn));
    if (ssfn_load(&ssfn, (const void *)&_binary_mono_sfn_start) != SSFN_OK)
    {
        write(1, "edit: font load failed\n", 23);
        return 1;
    }

    if (argc >= 2)
    {
        strncpy(filename, argv[1], MAX_FILENAME - 1);
        has_file = 1;
        editor_load(filename);
    }
    else
    {
        lines[0][0] = '\0';
        line_len[0] = 0;
        num_lines = 1;
    }

    redraw_all();

    int prev_row = cur_row;

    while (1)
    {
        if (!zen_is_focused())
        {
            zen_sleep_ms(20);
            continue;
        }
        int key = zen_getkey();
        if (key == 0)
        {
            zen_sleep_ms(8);
            continue;
        }

        if (menu_open)
        {
            int close_menu = 1;
            switch (key)
            {
            case KEY_ESC:
                break;
            case 's':
            case 'S':
                if (!has_file)
                {
                    strncpy(filename, "untitled.txt", MAX_FILENAME - 1);
                    has_file = 1;
                }
                editor_save(filename);
                break;
            case 'q':
            case 'Q':
                ssfn_free(&ssfn);
                return 0;
            case 'h':
            case 'H':
                cur_col = 0;
                break;
            case 'e':
            case 'E':
                cur_col = line_len[cur_row];
                break;
            case 't':
            case 'T':
                cur_row = 0;
                cur_col = 0;
                break;
            case 'b':
            case 'B':
                cur_row = num_lines - 1;
                cur_col = line_len[cur_row];
                break;
            case 'u':
            case 'U':
                cur_row -= vis_rows();
                break;
            case 'd':
            case 'D':
                cur_row += vis_rows();
                break;
            default:
                close_menu = 0;
                break;
            }
            menu_open = !close_menu;
            clamp_cursor();
            redraw_all();
            prev_row = cur_row;
            continue;
        }

        int scroll_changed = 0;
        int old_scroll = scroll_row;

        switch (key)
        {
        case KEY_ESC:
            menu_open = 1;
            redraw_all();
            prev_row = cur_row;
            continue;

        case KEY_UP:
            prev_row = cur_row;
            cur_row--;
            if (cur_row >= 0 && cur_col > line_len[cur_row])
                cur_col = line_len[cur_row];
            clamp_cursor();
            scroll_changed = (scroll_row != old_scroll);
            if (scroll_changed)
            {
                redraw_from(scroll_row);
            }
            else
            {

                redraw_row(prev_row);
                redraw_row(cur_row);
            }
            draw_status();
            draw_scrollbar();
            prev_row = cur_row;
            continue;

        case KEY_DOWN:
            prev_row = cur_row;
            cur_row++;
            if (cur_row < num_lines && cur_col > line_len[cur_row])
                cur_col = line_len[cur_row];
            clamp_cursor();
            scroll_changed = (scroll_row != old_scroll);
            if (scroll_changed)
            {
                redraw_from(scroll_row);
            }
            else
            {
                redraw_row(prev_row);
                redraw_row(cur_row);
            }
            draw_status();
            draw_scrollbar();
            prev_row = cur_row;
            continue;

        case KEY_LEFT:
            prev_row = cur_row;
            if (cur_col > 0)
                cur_col--;
            else if (cur_row > 0)
            {
                cur_row--;
                cur_col = line_len[cur_row];
            }
            clamp_cursor();
            scroll_changed = (scroll_row != old_scroll);
            if (scroll_changed)
            {
                redraw_from(scroll_row);
            }
            else if (cur_row != prev_row)
            {
                redraw_row(prev_row);
                redraw_row(cur_row);
            }
            else
            {
                redraw_row(cur_row);
            }
            draw_status();
            prev_row = cur_row;
            continue;

        case KEY_RIGHT:
            prev_row = cur_row;
            if (cur_col < line_len[cur_row])
                cur_col++;
            else if (cur_row < num_lines - 1)
            {
                cur_row++;
                cur_col = 0;
            }
            clamp_cursor();
            scroll_changed = (scroll_row != old_scroll);
            if (scroll_changed)
            {
                redraw_from(scroll_row);
            }
            else if (cur_row != prev_row)
            {
                redraw_row(prev_row);
                redraw_row(cur_row);
            }
            else
            {
                redraw_row(cur_row);
            }
            draw_status();
            prev_row = cur_row;
            continue;

        case KEY_BACKSPACE:
            prev_row = cur_row;
            backspace_char();
            clamp_cursor();
            scroll_changed = (scroll_row != old_scroll);
            if (scroll_changed || cur_row != prev_row)
            {

                redraw_from(cur_row);
            }
            else
            {
                redraw_row(cur_row);
            }
            draw_title();
            draw_status();
            draw_scrollbar();
            prev_row = cur_row;
            continue;

        case KEY_ENTER:
        case '\r':
            insert_newline();
            clamp_cursor();

            redraw_from(cur_row - 1 >= scroll_row ? cur_row - 1 : scroll_row);
            draw_title();
            draw_status();
            draw_scrollbar();
            prev_row = cur_row;
            continue;

        case KEY_TAB:
            for (int i = 0; i < 4; i++)
                insert_char(' ');
            clamp_cursor();
            redraw_row(cur_row);
            draw_title();
            draw_status();
            prev_row = cur_row;
            continue;

        default:
            if (key >= 0x20 && key < 0x7F)
            {
                insert_char((char)key);
                clamp_cursor();

                if (row_vis_height(cur_row) > 1)
                {
                    redraw_from(cur_row);
                }
                else
                {
                    redraw_row(cur_row);
                }
                draw_title();
                draw_status();
                prev_row = cur_row;
            }
            continue;
        }
    }
}