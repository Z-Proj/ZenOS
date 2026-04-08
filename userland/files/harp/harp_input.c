#include "harp_input.h"
#include "harp_wm.h"
#include "harp_proto.h"
#include <unistd.h>
#include <linux/input.h>
#include "../../userlib.h"

static int      s_kbd_fd   = -1;
static int      s_mouse_fd = -1;
static uint32_t s_ptr_x    = 0;
static uint32_t s_ptr_y    = 0;
static uint8_t  s_ptr_btn  = 0;
static uint32_t s_mods     = 0;
static int      s_tab_flag = 0;

void input_init(int kbd_fd, int mouse_fd, uint32_t start_x, uint32_t start_y)
{
    s_kbd_fd   = kbd_fd;
    s_mouse_fd = mouse_fd;
    s_ptr_x    = start_x;
    s_ptr_y    = start_y;
}

uint32_t input_ptr_x(void)    { return s_ptr_x; }
uint32_t input_ptr_y(void)    { return s_ptr_y; }
uint8_t  input_ptr_btn(void)  { return s_ptr_btn; }
uint32_t input_modifiers(void){ return s_mods; }
int      input_consume_tab(void) { int v = s_tab_flag; s_tab_flag = 0; return v; }

int translate_key(uint16_t code, uint32_t mods, int32_t value)
{
    if (value == 0) return 0;
    int shift = (mods & HARP_MOD_SHIFT) != 0;
    int caps  = (mods & HARP_MOD_CAPS)  != 0;
    int ctrl  = (mods & HARP_MOD_CTRL)  != 0;
    switch (code) {
    case KEY_UP:        return KEY_ARROW_UP;
    case KEY_DOWN:      return KEY_ARROW_DOWN;
    case KEY_LEFT:      return KEY_ARROW_LEFT;
    case KEY_RIGHT:     return KEY_ARROW_RIGHT;
    case KEY_ENTER:     return '\n';
    case KEY_BACKSPACE: return '\b';
    case KEY_ESC:       return 27;
    case KEY_TAB:       return '\t';
    case KEY_SPACE:     return ctrl ? 0 : ' ';
    case KEY_A: return ctrl ? 1  : ((shift != caps) ? 'A' : 'a');
    case KEY_B: return ctrl ? 2  : ((shift != caps) ? 'B' : 'b');
    case KEY_C: return ctrl ? 3  : ((shift != caps) ? 'C' : 'c');
    case KEY_D: return ctrl ? 4  : ((shift != caps) ? 'D' : 'd');
    case KEY_E: return ctrl ? 5  : ((shift != caps) ? 'E' : 'e');
    case KEY_F: return ctrl ? 6  : ((shift != caps) ? 'F' : 'f');
    case KEY_G: return ctrl ? 7  : ((shift != caps) ? 'G' : 'g');
    case KEY_H: return ctrl ? 8  : ((shift != caps) ? 'H' : 'h');
    case KEY_I: return ctrl ? 9  : ((shift != caps) ? 'I' : 'i');
    case KEY_J: return ctrl ? 10 : ((shift != caps) ? 'J' : 'j');
    case KEY_K: return ctrl ? 11 : ((shift != caps) ? 'K' : 'k');
    case KEY_L: return ctrl ? 12 : ((shift != caps) ? 'L' : 'l');
    case KEY_M: return ctrl ? 13 : ((shift != caps) ? 'M' : 'm');
    case KEY_N: return ctrl ? 14 : ((shift != caps) ? 'N' : 'n');
    case KEY_O: return ctrl ? 15 : ((shift != caps) ? 'O' : 'o');
    case KEY_P: return ctrl ? 16 : ((shift != caps) ? 'P' : 'p');
    case KEY_Q: return ctrl ? 17 : ((shift != caps) ? 'Q' : 'q');
    case KEY_R: return ctrl ? 18 : ((shift != caps) ? 'R' : 'r');
    case KEY_S: return ctrl ? 19 : ((shift != caps) ? 'S' : 's');
    case KEY_T: return ctrl ? 20 : ((shift != caps) ? 'T' : 't');
    case KEY_U: return ctrl ? 21 : ((shift != caps) ? 'U' : 'u');
    case KEY_V: return ctrl ? 22 : ((shift != caps) ? 'V' : 'v');
    case KEY_W: return ctrl ? 23 : ((shift != caps) ? 'W' : 'w');
    case KEY_X: return ctrl ? 24 : ((shift != caps) ? 'X' : 'x');
    case KEY_Y: return ctrl ? 25 : ((shift != caps) ? 'Y' : 'y');
    case KEY_Z: return ctrl ? 26 : ((shift != caps) ? 'Z' : 'z');
    case KEY_1: return shift ? '!' : '1';
    case KEY_2: return shift ? '@' : '2';
    case KEY_3: return shift ? '#' : '3';
    case KEY_4: return shift ? '$' : '4';
    case KEY_5: return shift ? '%' : '5';
    case KEY_6: return ctrl ? 30 : (shift ? '^' : '6');
    case KEY_7: return shift ? '&' : '7';
    case KEY_8: return shift ? '*' : '8';
    case KEY_9: return shift ? '(' : '9';
    case KEY_0: return shift ? ')' : '0';
    case KEY_MINUS:      return ctrl ? 31 : (shift ? '_'  : '-');
    case KEY_EQUAL:      return shift ? '+'  : '=';
    case KEY_LEFTBRACE:  return ctrl ? 27 : (shift ? '{'  : '[');
    case KEY_RIGHTBRACE: return ctrl ? 29 : (shift ? '}'  : ']');
    case KEY_BACKSLASH:  return ctrl ? 28 : (shift ? '|'  : '\\');
    case KEY_SEMICOLON:  return shift ? ':'  : ';';
    case KEY_APOSTROPHE: return shift ? '"'  : '\'';
    case KEY_GRAVE:      return shift ? '~'  : '`';
    case KEY_COMMA:      return shift ? '<'  : ',';
    case KEY_DOT:        return shift ? '>'  : '.';
    case KEY_SLASH:      return shift ? '?'  : '/';
    default: return 0;
    }
}

static void update_mods(uint16_t code, int32_t value)
{
    switch (code) {
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
        if (value) s_mods |= HARP_MOD_SHIFT; else s_mods &= ~HARP_MOD_SHIFT; break;
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL:
        if (value) s_mods |= HARP_MOD_CTRL;  else s_mods &= ~HARP_MOD_CTRL;  break;
    case KEY_LEFTALT: case KEY_RIGHTALT:
        if (value) s_mods |= HARP_MOD_ALT;   else s_mods &= ~HARP_MOD_ALT;   break;
    case KEY_CAPSLOCK:
        if (value == 1) s_mods ^= HARP_MOD_CAPS; break;
    }
}

int input_pump_keyboard(void)
{
    if (s_kbd_fd < 0) return 0;
    int avail = 0;
    if (zen_ioctl(s_kbd_fd, ZEN_FIONREAD, &avail) < 0) return 0;
    int got = 0;
    while (avail >= (int)sizeof(struct input_event)) {
        struct input_event ev;
        int n = read(s_kbd_fd, &ev, sizeof(ev));
        if (n != (int)sizeof(ev)) break;
        if (ev.type == EV_KEY) {
            update_mods((uint16_t)ev.code, ev.value);
            if (ev.code == KEY_TAB && ev.value == 1 && (s_mods & HARP_MOD_ALT))
                s_tab_flag = 1;
            else {
                int k = translate_key((uint16_t)ev.code, s_mods, ev.value);
                if (focused_win >= 0)
                    send_key_event(focused_win, (uint16_t)ev.code, ev.value, s_mods, k);
            }
            got = 1;
        }
        avail -= (int)sizeof(ev);
    }
    return got;
}

int input_pump_mouse(void)
{
    if (s_mouse_fd < 0) return 0;
    int avail = 0;
    if (zen_ioctl(s_mouse_fd, ZEN_FIONREAD, &avail) < 0) return 0;
    int got = 0;
    while (avail >= (int)sizeof(struct input_event)) {
        struct input_event ev;
        int n = read(s_mouse_fd, &ev, sizeof(ev));
        if (n != (int)sizeof(ev)) break;
        if (ev.type == EV_REL) {
            if (ev.code == REL_X) {
                int32_t nx = (int32_t)s_ptr_x + ev.value;
                if (nx < 0) nx = 0;
                if (nx >= (int32_t)SCR_W) nx = (int32_t)SCR_W - 1;
                s_ptr_x = (uint32_t)nx;
            } else if (ev.code == REL_Y) {
                int32_t ny = (int32_t)s_ptr_y + ev.value;
                if (ny < 0) ny = 0;
                if (ny >= (int32_t)SCR_H) ny = (int32_t)SCR_H - 1;
                s_ptr_y = (uint32_t)ny;
            }
            got = 1;
        } else if (ev.type == EV_KEY) {
            if      (ev.code == BTN_LEFT)   { if (ev.value) s_ptr_btn |= 1; else s_ptr_btn &= (uint8_t)~1u; }
            else if (ev.code == BTN_MIDDLE) { if (ev.value) s_ptr_btn |= 2; else s_ptr_btn &= (uint8_t)~2u; }
            else if (ev.code == BTN_RIGHT)  { if (ev.value) s_ptr_btn |= 4; else s_ptr_btn &= (uint8_t)~4u; }
            got = 1;
        }
        avail -= (int)sizeof(ev);
    }
    return got;
}
