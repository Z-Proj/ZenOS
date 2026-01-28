#include <stdint.h>
#include "serial.h"
#include "../ports.h"

#define COM1 0x3F8

void serial_init() {
    outportb(COM1 + 1, 0x00);
    outportb(COM1 + 3, 0x80);
    outportb(COM1 + 0, 0x03);
    outportb(COM1 + 1, 0x00);
    outportb(COM1 + 3, 0x03);
    outportb(COM1 + 2, 0xC7);
    outportb(COM1 + 4, 0x0B);
    serial_write_string("\x1b[38;2;50;255;50m[src/libk/debug/serial.c:15]- Initialized.\n");
}

void serial_write_char(char c) {
    if(c == '\t') serial_write_string("    ");
    if(c == '\n')serial_write_char('\r');
    while (!(inportb(COM1 + 5) & 0x20));
    outportb(COM1, c);
}

void serial_write_string(const char* str) {
    while (*str) {
        serial_write_char(*str++);
    }
}

void serial_write_hex(unsigned int n) {
    char hex_chars[] = "0123456789ABCDEF";

    serial_write_char('0');
    serial_write_char('x');

    for (int i = 28; i >= 0; i -= 4) {
        unsigned int nibble = (n >> i) & 0xF;
        serial_write_char(hex_chars[nibble]);
    }
}

void serial_write_uint(unsigned int n) {
    char buffer[10];
    int i = 0;
    if (n == 0) {
        serial_write_char('0');
        return;}
    while (n > 0) {
        buffer[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i--) {
        serial_write_char(buffer[i]);
    }
}
