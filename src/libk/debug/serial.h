#ifndef SERIAL_H
#define SERIAL_H

void serial_init();
void serial_write_string(const char* str);
void serial_write_char(char c);
void serial_write_uint(unsigned int n);
void serial_write_hex(unsigned int n);

#endif