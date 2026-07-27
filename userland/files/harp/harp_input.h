/**
 * 
 * @file : harp_input.h
 * @brief : Keyboard and mouse input handling for the Harp compositor.
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
 * The above licensing applies to all other parts of the software (Harp).
 * 
 * @author : Rishies2010
 * @copyright (c) 2026
 * 
 */

#pragma once
#include <stdint.h>

void input_init(int kbd_fd, int mouse_fd, uint32_t start_x, uint32_t start_y);

uint32_t input_ptr_x(void);
uint32_t input_ptr_y(void);
uint8_t  input_ptr_btn(void);
uint32_t input_modifiers(void);
void     input_set_ptr_pos(uint32_t x, uint32_t y);

int input_pump_keyboard(void);
int input_pump_mouse(void);
int input_consume_tab(void);

int translate_key(uint16_t code, uint32_t modifiers, int32_t value);