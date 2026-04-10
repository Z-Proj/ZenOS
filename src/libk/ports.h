/**
 * 
 * @file : /src/libk/ports.h
 * @brief : x86 I/O port wrappers - inb/outb, inw/outw, inl/outl, etc.
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

#ifndef PORTS_H
#define PORTS_H
#include <stdint.h>

unsigned char inportb(unsigned short port);
void outportb(unsigned short port, unsigned char data);
void outportw(uint16_t port, uint16_t val);
uint16_t inportw(uint16_t port);
void outportl(uint16_t port, uint32_t val);
uint32_t inportl(uint16_t port);
void outportsw(uint16_t port, uint16_t *data, uint32_t count);
void inportsw(uint16_t port, uint16_t *data, uint32_t count);

#endif