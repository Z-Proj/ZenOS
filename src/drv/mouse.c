#include "mouse.h"
#include "../cpu/isr.h"
#include "../libk/ports.h"
#include "vga.h"
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

static bool mouse_wait(uint8_t type)
{
    uint32_t timeout = 100000;
    while (timeout--)
    {
        uint8_t status = inportb(0x64);
        if (type == 0)
        {
            if (status & 0x01)
                return true;
        }
        else
        {
            if (!(status & 0x02))
                return true;
        }
    }
    return false;
}

static void mouse_drain_output(void)
{
    for (int i = 0; i < 256; i++)
    {
        if (!(inportb(0x64) & 0x01))
            break;
        inportb(0x60);
    }
}

static bool mouse_write(uint8_t data)
{
    if (!mouse_wait(1))
        return false;
    outportb(0x64, 0xD4);
    if (!mouse_wait(1))
        return false;
    outportb(0x60, data);
    return true;
}

static bool mouse_read(uint8_t *data)
{
    if (!mouse_wait(0))
        return false;
    *data = inportb(0x60);
    return true;
}

void mouse_process_byte(uint8_t data)
{
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
        if (mouse.packet[0] & 0xC0)
        {
            mouse.cycle = 0;
            break;
        }
        mouse.buttons = mouse.packet[0] & 0x07;
        int32_t dx = (int8_t)mouse.packet[1];
        int32_t dy = -(int8_t)mouse.packet[2];
        int32_t next_x = (int32_t)mouse.x + dx;
        int32_t next_y = (int32_t)mouse.y + dy;

        if (next_x < 0)
            next_x = 0;
        if (next_y < 0)
            next_y = 0;
        if (next_x >= (int32_t)framebuffer_width)
            next_x = (int32_t)framebuffer_width - 1;
        if (next_y >= (int32_t)framebuffer_height)
            next_y = (int32_t)framebuffer_height - 1;

        mouse.x = (uint32_t)next_x;
        mouse.y = (uint32_t)next_y;
        mouse.cycle = 0;
        mouse.ready = true;
        break;
    }
}

void mouse_handler(registers_t *regs)
{
    (void)regs;
    uint8_t status = inportb(0x64);
    if (!(status & 0x01))
        return;
    if (!(status & 0x20))
        return;
    mouse_process_byte(inportb(0x60));
}

void mouse_init(void)
{
    mouse_drain_output();

    if (!mouse_wait(1))
    {
        log("Mouse init failed: controller busy", 2, 1);
        return;
    }
    outportb(0x64, 0xA8);

    if (!mouse_wait(1))
    {
        log("Mouse init failed: controller busy", 2, 1);
        return;
    }
    outportb(0x64, 0x20);
    uint8_t status = 0;
    if (!mouse_read(&status))
    {
        log("Mouse init failed: cannot read controller config", 2, 1);
        return;
    }
    status |= 0x02;
    status &= ~0x20;

    if (!mouse_wait(1))
    {
        log("Mouse init failed: controller busy", 2, 1);
        return;
    }
    outportb(0x64, 0x60);
    if (!mouse_wait(1))
    {
        log("Mouse init failed: controller busy", 2, 1);
        return;
    }
    outportb(0x60, status);

    register_interrupt_handler(IRQ12, mouse_handler, "Mouse Handler");

    uint8_t response = 0;
    if (!mouse_write(0xF6) || !mouse_read(&response) || response != 0xFA)
    {
        log("Mouse init warning: defaults command ack mismatch (0x%x)", 2, 1, response);
    }
    response = 0;
    if (!mouse_write(0xF4) || !mouse_read(&response) || response != 0xFA)
    {
        log("Mouse init warning: enable command ack mismatch (0x%x)", 2, 1, response);
    }

    mouse_drain_output();

    mouse.x = framebuffer_width / 2;
    mouse.y = framebuffer_height / 2;
    mouse.cycle = 0;
    mouse.ready = false;
    mouse.buttons = 0;
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
