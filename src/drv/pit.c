/**
 * 
 * @file : /src/drv/pit.c
 * @brief : PIT timer - IRQ0 fallback tick source when HPET is unavailable.
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

#include <stdint.h>
#include "pit.h"
#include "hpet.h"
#include "../cpu/isr.h"
#include "../libk/ports.h"
#include "../libk/debug/log.h"

#define PIT_CH0_DATA 0x40
#define PIT_CMD      0x43
#define PIT_BASE_HZ  1193182U

static void pit_handler(registers_t *r)
{
    (void)r;
    hpet_ticks++;
}

void pit_init(uint32_t frequency_hz)
{
    if (!frequency_hz)
        return;

    uint32_t divisor = PIT_BASE_HZ / frequency_hz;
    if (!divisor)
        divisor = 1;
    if (divisor > 65535)
        divisor = 65535;

    outportb(PIT_CMD, 0x36);
    outportb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    outportb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    register_interrupt_handler(TIMER_IRQ_VECTOR, pit_handler, "PIT Timer");

    hpet_set_freq_hz(PIT_BASE_HZ / divisor);

    log("PIT Initialized (HPET fallback).", 4, 0);
}
