/**
 * 
 * @file : /src/drv/mouse.c
 * @brief : PS/2 mouse driver.
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

#include "mouse.h"
#include "input.h"
#include "../cpu/isr.h"
#include "../libk/ports.h"
#include "../libk/spinlock.h"
#include "vga.h"
#include "../libk/debug/log.h"

typedef struct
{
    uint32_t x, y;
    uint8_t buttons;
    uint8_t packet[3];
    uint8_t cycle;
    bool irq_ready;
    bool ready;
} mouse_t;

static mouse_t mouse = {0};
static spinlock_t mouse_lock;

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL 0x02
#define PS2_STATUS_AUX_DATA 0x20

#define PS2_MOUSE_ALWAYS_ONE 0x08
#define PS2_MOUSE_X_SIGN 0x10
#define PS2_MOUSE_Y_SIGN 0x20
#define PS2_MOUSE_X_OVERFLOW 0x40
#define PS2_MOUSE_Y_OVERFLOW 0x80

#define MOUSE_RESPONSE_ACK 0xFA
#define MOUSE_RESPONSE_RESEND 0xFE

static bool mouse_wait(uint8_t type)
{
    uint32_t timeout = 100000;
    while (timeout--)
    {
        uint8_t status = inportb(0x64);
        if (type == 0)
        {
            if (status & PS2_STATUS_OUTPUT_FULL)
                return true;
        }
        else
        {
            if (!(status & PS2_STATUS_INPUT_FULL))
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

static bool mouse_read_response(uint8_t *data)
{
    uint32_t timeout = 100000;
    while (timeout--)
    {
        uint8_t status = inportb(0x64);
        if (!(status & PS2_STATUS_OUTPUT_FULL))
            continue;

        uint8_t value = inportb(0x60);
        if (status & PS2_STATUS_AUX_DATA)
        {
            *data = value;
            return true;
        }
    }
    return false;
}

static bool mouse_send_command(uint8_t cmd)
{
    for (int attempt = 0; attempt < 3; attempt++)
    {
        uint8_t response = 0;
        if (!mouse_write(cmd))
            return false;
        if (!mouse_read_response(&response))
            return false;
        if (response == MOUSE_RESPONSE_ACK)
            return true;
        if (response != MOUSE_RESPONSE_RESEND)
            return false;
    }
    return false;
}

static int32_t mouse_decode_delta(uint8_t header, uint8_t value, uint8_t sign)
{
    int32_t delta = value;
    if (header & sign)
        delta -= 256;
    return delta;
}

void mouse_apply_delta(int32_t dx, int32_t dy, uint8_t buttons)
{
    uint64_t rflags = spinlock_acquire_irqsave(&mouse_lock);
    uint8_t prev_buttons = mouse.buttons;
    int32_t next_x = (int32_t)mouse.x + dx;
    int32_t next_y = (int32_t)mouse.y + dy;
    int32_t max_x = framebuffer_width ? (int32_t)framebuffer_width - 1 : 0;
    int32_t max_y = framebuffer_height ? (int32_t)framebuffer_height - 1 : 0;
    if (next_x < 0) next_x = 0;
    if (next_y < 0) next_y = 0;
    if (next_x > max_x) next_x = max_x;
    if (next_y > max_y) next_y = max_y;
    mouse.buttons = buttons;
    mouse.x = (uint32_t)next_x;
    mouse.y = (uint32_t)next_y;
    mouse.ready = true;
    spinlock_release_irqrestore(&mouse_lock, rflags);
    input_enqueue_mouse(dx, dy, buttons, prev_buttons);
}

void mouse_process_byte(uint8_t data)
{
    uint64_t rflags = spinlock_acquire_irqsave(&mouse_lock);
    switch (mouse.cycle)
    {
    case 0:
        if ((data & PS2_MOUSE_ALWAYS_ONE) && !(data & (PS2_MOUSE_X_OVERFLOW | PS2_MOUSE_Y_OVERFLOW)))
        {
            mouse.packet[0] = data;
            mouse.cycle++;
        }
        spinlock_release_irqrestore(&mouse_lock, rflags);
        break;
    case 1:
        mouse.packet[1] = data;
        mouse.cycle++;
        spinlock_release_irqrestore(&mouse_lock, rflags);
        break;
    case 2:
        mouse.packet[2] = data;
        uint8_t new_buttons = mouse.packet[0] & 0x07;
        int32_t dx = mouse_decode_delta(mouse.packet[0], mouse.packet[1], PS2_MOUSE_X_SIGN);
        int32_t dy = -mouse_decode_delta(mouse.packet[0], mouse.packet[2], PS2_MOUSE_Y_SIGN);
        mouse.cycle = 0;
        spinlock_release_irqrestore(&mouse_lock, rflags);
        mouse_apply_delta(dx, dy, new_buttons);
        break;
    }
}

void mouse_poll(void)
{
    uint64_t rflags = spinlock_acquire_irqsave(&mouse_lock);
    bool irq_ready = mouse.irq_ready;
    spinlock_release_irqrestore(&mouse_lock, rflags);
    if (irq_ready)
        return;

    for (int i = 0; i < 32; i++)
    {
        uint8_t status = inportb(0x64);
        if (!(status & PS2_STATUS_OUTPUT_FULL))
            break;
        if (!(status & PS2_STATUS_AUX_DATA))
            break;
        mouse_process_byte(inportb(0x60));
    }
}

void mouse_handler(registers_t *regs)
{
    (void)regs;
    uint8_t status = inportb(0x64);
    if ((status & (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA)) == (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA))
        mouse_process_byte(inportb(0x60));
}

void mouse_init(void)
{
    spinlock_init(&mouse_lock);
    mouse.x = framebuffer_width / 2;
    mouse.y = framebuffer_height / 2;
    mouse.cycle = 0;
    mouse.irq_ready = false;
    mouse.ready = false;
    mouse.buttons = 0;
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

    if (!mouse_send_command(0xF6))
    {
        log("Mouse init failed: defaults command rejected", 2, 1);
        return;
    }
    if (!mouse_send_command(0xF4))
    {
        log("Mouse init failed: stream enable rejected", 2, 1);
        return;
    }

    register_interrupt_handler(IRQ12, mouse_handler, "Mouse Handler");
    uint64_t rflags = spinlock_acquire_irqsave(&mouse_lock);
    mouse.irq_ready = true;
    spinlock_release_irqrestore(&mouse_lock, rflags);

    mouse_drain_output();
    log("Mouse Initialized.", 4, 0);
}

uint32_t mouse_x(void)
{
    uint64_t rflags = spinlock_acquire_irqsave(&mouse_lock);
    uint32_t x = mouse.x;
    spinlock_release_irqrestore(&mouse_lock, rflags);
    return x;
}

uint32_t mouse_y(void)
{
    uint64_t rflags = spinlock_acquire_irqsave(&mouse_lock);
    uint32_t y = mouse.y;
    spinlock_release_irqrestore(&mouse_lock, rflags);
    return y;
}

uint8_t mouse_button(void)
{
    uint64_t rflags = spinlock_acquire_irqsave(&mouse_lock);
    uint8_t buttons = mouse.buttons;
    spinlock_release_irqrestore(&mouse_lock, rflags);
    if (buttons & 0x01) return 1;
    if (buttons & 0x04) return 2;
    if (buttons & 0x02) return 3;
    return 0;
}

bool mouse_moved(void)
{
    uint64_t rflags = spinlock_acquire_irqsave(&mouse_lock);
    if (mouse.ready)
    {
        mouse.ready = false;
        spinlock_release_irqrestore(&mouse_lock, rflags);
        return true;
    }
    spinlock_release_irqrestore(&mouse_lock, rflags);
    return false;
}

void mouse_set_pos(uint32_t x, uint32_t y)
{
    uint64_t rflags = spinlock_acquire_irqsave(&mouse_lock);
    if (x < framebuffer_width)
        mouse.x = x;
    if (y < framebuffer_height)
        mouse.y = y;
    spinlock_release_irqrestore(&mouse_lock, rflags);
}
