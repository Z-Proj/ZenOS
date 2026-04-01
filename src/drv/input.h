#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define INPUT_EV_SYN 0x00
#define INPUT_EV_KEY 0x01
#define INPUT_EV_REL 0x02

#define INPUT_SYN_REPORT 0

#define INPUT_REL_X 0x00
#define INPUT_REL_Y 0x01

#define INPUT_BTN_LEFT   0x110
#define INPUT_BTN_RIGHT  0x111
#define INPUT_BTN_MIDDLE 0x112

#define INPUT_KEY_ESC        1
#define INPUT_KEY_1          2
#define INPUT_KEY_2          3
#define INPUT_KEY_3          4
#define INPUT_KEY_4          5
#define INPUT_KEY_5          6
#define INPUT_KEY_6          7
#define INPUT_KEY_7          8
#define INPUT_KEY_8          9
#define INPUT_KEY_9          10
#define INPUT_KEY_0          11
#define INPUT_KEY_MINUS      12
#define INPUT_KEY_EQUAL      13
#define INPUT_KEY_BACKSPACE  14
#define INPUT_KEY_TAB        15
#define INPUT_KEY_Q          16
#define INPUT_KEY_W          17
#define INPUT_KEY_E          18
#define INPUT_KEY_R          19
#define INPUT_KEY_T          20
#define INPUT_KEY_Y          21
#define INPUT_KEY_U          22
#define INPUT_KEY_I          23
#define INPUT_KEY_O          24
#define INPUT_KEY_P          25
#define INPUT_KEY_LEFTBRACE  26
#define INPUT_KEY_RIGHTBRACE 27
#define INPUT_KEY_ENTER      28
#define INPUT_KEY_LEFTCTRL   29
#define INPUT_KEY_A          30
#define INPUT_KEY_S          31
#define INPUT_KEY_D          32
#define INPUT_KEY_F          33
#define INPUT_KEY_G          34
#define INPUT_KEY_H          35
#define INPUT_KEY_J          36
#define INPUT_KEY_K          37
#define INPUT_KEY_L          38
#define INPUT_KEY_SEMICOLON  39
#define INPUT_KEY_APOSTROPHE 40
#define INPUT_KEY_GRAVE      41
#define INPUT_KEY_LEFTSHIFT  42
#define INPUT_KEY_BACKSLASH  43
#define INPUT_KEY_Z          44
#define INPUT_KEY_X          45
#define INPUT_KEY_C          46
#define INPUT_KEY_V          47
#define INPUT_KEY_B          48
#define INPUT_KEY_N          49
#define INPUT_KEY_M          50
#define INPUT_KEY_COMMA      51
#define INPUT_KEY_DOT        52
#define INPUT_KEY_SLASH      53
#define INPUT_KEY_RIGHTSHIFT 54
#define INPUT_KEY_LEFTALT    56
#define INPUT_KEY_SPACE      57
#define INPUT_KEY_CAPSLOCK   58
#define INPUT_KEY_F1         59
#define INPUT_KEY_F2         60
#define INPUT_KEY_RIGHTCTRL  97
#define INPUT_KEY_RIGHTALT   100
#define INPUT_KEY_UP         103
#define INPUT_KEY_LEFT       105
#define INPUT_KEY_RIGHT      106
#define INPUT_KEY_DOWN       108

typedef struct
{
    int64_t tv_sec;
    int64_t tv_usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
} input_event_t;

void input_init(void);
void input_register_devfs(void);
uint16_t input_keycode_from_scancode(uint8_t scancode, int extended);
void input_enqueue_key(uint16_t keycode, int32_t value);
void input_enqueue_mouse(int32_t dx, int32_t dy, uint8_t buttons, uint8_t prev_buttons);

#endif
