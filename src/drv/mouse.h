#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

void mouse_init(void);
uint32_t mouse_x(void);
uint32_t mouse_y(void);
uint8_t mouse_button(void);
bool mouse_moved(void);
void mouse_set_pos(uint32_t x, uint32_t y);

#endif