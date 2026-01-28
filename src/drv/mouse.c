#include "mouse.h"
#include "../cpu/isr.h"
#include "../libk/ports.h"
#include "vga.h"
#include "local_apic.h"
#include "ioapic.h"
#include "../libk/debug/log.h"

typedef struct
{
    uint32_t x, y;
    uint8_t buttons;
    uint8_t packet[3];
    uint8_t cycle;
    bool ready;
} mouse_t;

static mouse_t mouse = {0};

static void mouse_wait(uint8_t type)
{
    uint32_t timeout = 10000;
    while (timeout--)
    {
        if (type == 0)
        {
            if (inportb(0x64) & 0x01)
                return;
        }
        else
        {
            if (!(inportb(0x64) & 0x02))
                return;
        }
    }
}

static void mouse_write(uint8_t data)
{
    mouse_wait(1);
    outportb(0x64, 0xD4);
    mouse_wait(1);
    outportb(0x60, data);
}

static uint8_t mouse_read(void)
{
    mouse_wait(0);
    return inportb(0x60);
}

void mouse_handler(registers_t *regs)
{
    (void)regs;
    uint8_t data = inportb(0x60);

    switch (mouse.cycle)
    {
    case 0:
        if (data & 0x08)
        {
            mouse.packet[0] = data;
            mouse.cycle++;
        }
        break;
    case 1:
        mouse.packet[1] = data;
        mouse.cycle++;
        break;
    case 2:
        mouse.packet[2] = data;
        mouse.buttons = mouse.packet[0] & 0x07;
        int32_t dx = (mouse.packet[0] & 0x10) ? mouse.packet[1] - 256 : mouse.packet[1];
        int32_t dy = (mouse.packet[0] & 0x20) ? -(mouse.packet[2] - 256) : -mouse.packet[2];
        mouse.x += dx;
        mouse.y += dy;
        if (mouse.x >= framebuffer_width)
            mouse.x = framebuffer_width - 1;
        if (mouse.y >= framebuffer_height)
            mouse.y = framebuffer_height - 1;
        mouse.cycle = 0;
        mouse.ready = true;
        break;
    }
}

void mouse_init(void)
{

    mouse_wait(1);
    outportb(0x64, 0xA8);

    mouse_wait(1);
    outportb(0x64, 0x20);
    mouse_wait(0);
    uint8_t status = inportb(0x60);
    status |= 0x02;

    mouse_wait(1);
    outportb(0x64, 0x60);
    mouse_wait(1);
    outportb(0x60, status);

    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF4);
    mouse_read();

    IoApicSetEntry(g_ioApicAddr, 12, 0x2C);
    register_interrupt_handler(0x2C, mouse_handler, "Mouse Handler");

    uint32_t drain_count = 20;
    while ((inportb(0x64) & 0x01) && drain_count--)
    {
        inportb(0x60);
    }

    mouse.x = framebuffer_width / 2;
    mouse.y = framebuffer_height / 2;
    log("Mouse Initialized.", 4, 0);
}

uint32_t mouse_x(void)
{
    return mouse.x;
}

uint32_t mouse_y(void)
{
    return mouse.y;
}

uint8_t mouse_button(void)
{
    if (mouse.buttons & 0x01)
        return 1;
    if (mouse.buttons & 0x04)
        return 2;
    if (mouse.buttons & 0x02)
        return 3;
    return 0;
}

bool mouse_moved(void)
{
    if (mouse.ready)
    {
        mouse.ready = false;
        return true;
    }
    return false;
}

void mouse_set_pos(uint32_t x, uint32_t y)
{
    if (x < framebuffer_width)
        mouse.x = x;
    if (y < framebuffer_height)
        mouse.y = y;
}