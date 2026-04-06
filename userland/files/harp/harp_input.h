#pragma once
#include <stdint.h>

void input_init(int kbd_fd, int mouse_fd, uint32_t start_x, uint32_t start_y);

uint32_t input_ptr_x(void);
uint32_t input_ptr_y(void);
uint8_t  input_ptr_btn(void);
uint32_t input_modifiers(void);

int input_pump_keyboard(void);
int input_pump_mouse(void);
int input_consume_tab(void);

int translate_key(uint16_t code, uint32_t modifiers, int32_t value);
