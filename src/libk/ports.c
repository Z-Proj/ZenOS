#include "ports.h"

unsigned char inportb(unsigned short port) {
    unsigned char rv;
    __asm__ __volatile__("inb %1, %0" : "=a" (rv) : "dN" (port));
    return rv;
}
void outportb(unsigned short port, unsigned char data) {
    __asm__ __volatile__("outb %1, %0" : : "dN" (port), "a" (data));
}
void outportw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}
uint16_t inportw(uint16_t port) {
    uint16_t ret;
    __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
void outportl(uint16_t port, uint32_t val) {
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}
uint32_t inportl(uint16_t port) {
    uint32_t ret;
    __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
void outportsw(uint16_t port, uint16_t *data, uint32_t count) {
    __asm__ __volatile__(
        "rep outsw"
        :
        : "d"(port), "S"(data), "c"(count)
        : "memory"
    );
}

void inportsw(uint16_t port, uint16_t *data, uint32_t count) {
    __asm__ __volatile__(
        "rep insw"
        :
        : "d"(port), "D"(data), "c"(count)
        : "memory"
    );
}