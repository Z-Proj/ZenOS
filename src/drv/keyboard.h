#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SCANCODE_ESC        0x76
#define SCANCODE_BACKSPACE  0x66
#define SCANCODE_TAB        0x0D
#define SCANCODE_ENTER      0x5A
#define SCANCODE_CTRL_L     0x14
#define SCANCODE_SHIFT_L    0x12
#define SCANCODE_SHIFT_R    0x59
#define SCANCODE_ALT_L      0x11
#define SCANCODE_SPACE      0x29
#define SCANCODE_CAPS       0x58

#define KBD_SCANCODE2_F1    0x05
#define KBD_SCANCODE2_F2    0x06
#define KBD_SCANCODE2_F3    0x04
#define KBD_SCANCODE2_F4    0x0C
#define KBD_SCANCODE2_F5    0x03
#define KBD_SCANCODE2_F6    0x0B
#define KBD_SCANCODE2_F7    0x83
#define KBD_SCANCODE2_F8    0x0A
#define KBD_SCANCODE2_F9    0x01
#define KBD_SCANCODE2_F10   0x09

void init_keyboard(void);
void kbd_init_focus(void);
void kbd_switcher_tick(void);
uint64_t kbd_get_focused_pid(void);
void kbd_transfer_focus(uint64_t dead_pid);

char get_key(void);
char wait_for_key(void);
void read_line(char *buffer, size_t max_size, bool print);
bool is_key_pressed(uint8_t scancode);
bool is_shift_pressed(void);
bool is_ctrl_pressed(void);
bool is_alt_pressed(void);
bool is_caps_lock_on(void);

#endif