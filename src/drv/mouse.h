/**
 * 
 * @file : /src/drv/mouse.h
 * @brief : PS/2 mouse driver.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

void mouse_init(void);
void mouse_process_byte(uint8_t data);
void mouse_poll(void);
uint32_t mouse_x(void);
uint32_t mouse_y(void);
uint8_t mouse_button(void);
bool mouse_moved(void);
void mouse_set_pos(uint32_t x, uint32_t y);

#endif
