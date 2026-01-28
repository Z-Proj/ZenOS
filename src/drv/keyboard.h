#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SCANCODE_ESC        0x01
#define SCANCODE_BACKSPACE  0x0E
#define SCANCODE_TAB        0x0F
#define SCANCODE_ENTER      0x1C
#define SCANCODE_CTRL_L     0x1D
#define SCANCODE_SHIFT_L    0x2A
#define SCANCODE_SHIFT_R    0x36
#define SCANCODE_ALT_L      0x38
#define SCANCODE_SPACE      0x39
#define SCANCODE_CAPS       0x3A

#define SCANCODE_F1         0x3B
#define SCANCODE_F2         0x3C
#define SCANCODE_F3         0x3D
#define SCANCODE_F4         0x3E
#define SCANCODE_F5         0x3F
#define SCANCODE_F6         0x40
#define SCANCODE_F7         0x41
#define SCANCODE_F8         0x42
#define SCANCODE_F9         0x43
#define SCANCODE_F10        0x44

void init_keyboard(void);
char get_key(void);
char wait_for_key(void);
void read_line(char *buffer, size_t max_size, bool print);
bool is_key_pressed(uint8_t scancode);
bool is_shift_pressed(void);
bool is_ctrl_pressed(void);
bool is_alt_pressed(void);
bool is_caps_lock_on(void);

#endif