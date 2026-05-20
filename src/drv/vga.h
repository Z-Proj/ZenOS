/**
 * 
 * @file : /src/drv/vga.h
 * @brief : Framebuffer and terminal output via flanterm with font switching.
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

#ifndef VGA_H
#define VGA_H
#include <stdint.h>
#include <stdbool.h>

extern int current_font;
extern uint64_t framebuffer_width, framebuffer_height, framebuffer_pitch;
extern uint8_t framebuffer_bpp;
extern uint8_t *framebuffer_addr;
extern bool flanterm;

void vga_init(void);
void vga_boot_splash_show(const char *status);
int vga_boot_splash_load_tga(const char *path);
void vga_boot_splash_status(const char *status);
void vga_crash_screen(const char *name, const char *title, const char *info);
void clr(void);
void ft_run(bool set);
void printc(char c);
void font(uint32_t num);
void prints(const char *str);
void setcolor(uint32_t fg, uint32_t bg);
void put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t get_pixel_at(uint32_t x, uint32_t y);
void plotchar(char c, uint32_t x, uint32_t y, uint32_t fg);
void draw_text_at(const char *str, uint32_t x, uint32_t y, uint32_t color);

#endif
