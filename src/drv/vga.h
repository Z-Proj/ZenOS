#ifndef VGA_H
#define VGA_H
#include <stdint.h>
#include <stdbool.h>

extern int current_font;
extern uint64_t framebuffer_width, framebuffer_height;
extern uint8_t framebuffer_bpp;
extern uint8_t *framebuffer_addr;
extern bool flanterm;

void vga_init(void);
void clr(void);
void ft_run(bool set);
void printc(char c);
void prints(const char *str);
void setcolor(uint32_t fg, uint32_t bg);
void put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t get_pixel_at(uint32_t x, uint32_t y);
void plotchar(char c, uint32_t x, uint32_t y, uint32_t fg);
void draw_text_at(const char *str, uint32_t x, uint32_t y, uint32_t color);

#endif