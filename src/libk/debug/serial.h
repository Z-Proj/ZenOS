/**
 * 
 * @file : /src/libk/debug/serial.h
 * @brief : COM1 serial port driver for logging.
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

#ifndef SERIAL_H
#define SERIAL_H

void serial_init();
void serial_write_string(const char* str);
void serial_write_char(char c);
void serial_write_uint(unsigned int n);
void serial_write_hex(unsigned int n);

#endif