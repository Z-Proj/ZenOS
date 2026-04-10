/**
 * 
 * @file : /src/drv/hpet.c
 * @brief : HPET timer - sleeps, monotonic time, and periodic ticks.
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
#include "../cpu/isr.h"
#include "../drv/local_apic.h"
#include "../libk/debug/log.h"

#define HPET_IRQ_VECTOR 0x22

static uintptr_t hpet_base = 0;

static inline uint64_t hpet_read(uint32_t offset)
{
    return *(volatile uint64_t *)(hpet_base + offset);
}

static inline void hpet_write(uint32_t offset, uint64_t val)
{
    *(volatile uint64_t *)(hpet_base + offset) = val;
}

#define HPET_CAP        0x000
#define HPET_CFG        0x010
#define HPET_ISR        0x020
#define HPET_COUNTER    0x0F0
#define HPET_T0_CFG     0x100
#define HPET_T0_CMP     0x108

static uint64_t hpet_period_fs  = 0;
static uint64_t hpet_counter_hz = 0;
static uint64_t ticks_per_irq   = 0;
static uint32_t hpet_freq_hz    = 0;
volatile uint64_t hpet_ticks    = 0;

static void hpet_handler(registers_t *r)
{
    (void)r;
    hpet_write(HPET_ISR, 1);
    hpet_ticks++;
}

void SetHpetAddress(uint64_t addr)
{
    hpet_base = (uintptr_t)addr;
}

void hpet_init(uint32_t frequency_hz)
{
    if (!hpet_base || !frequency_hz)
        return;

    hpet_write(HPET_CFG, 0);
    hpet_write(HPET_COUNTER, 0);

    hpet_period_fs = hpet_read(HPET_CAP) >> 32;
    if (!hpet_period_fs)
        return;

    hpet_counter_hz = 1000000000000000ULL / hpet_period_fs;
    ticks_per_irq = hpet_counter_hz / frequency_hz;
    if (!ticks_per_irq)
        ticks_per_irq = 1;

    uint64_t tcfg = 0;
    tcfg |= (1ULL << 2);
    tcfg |= (1ULL << 3);
    tcfg |= (1ULL << 6);
    hpet_write(HPET_T0_CFG, tcfg);
    hpet_write(HPET_T0_CMP, ticks_per_irq);
    tcfg &= ~(1ULL << 6);
    hpet_write(HPET_T0_CFG, tcfg);

    register_interrupt_handler(HPET_IRQ_VECTOR, hpet_handler, "HPET Timer");

    hpet_write(HPET_CFG, 1 | 2);
    hpet_freq_hz = frequency_hz;

    log("HPET Initialized.", 4, 0);
}

uint32_t hpet_get_freq_hz(void)
{
    return hpet_freq_hz;
}

uint64_t hpet_monotonic_ns(void)
{
    if (!hpet_base || !hpet_period_fs)
    {
        if (!hpet_freq_hz)
            return 0;
        return (hpet_ticks * 1000000000ULL) / hpet_freq_hz;
    }
    if (!hpet_counter_hz)
        return 0;
    uint64_t counter = hpet_read(HPET_COUNTER);
    uint64_t sec = counter / hpet_counter_hz;
    uint64_t rem = counter % hpet_counter_hz;
    return sec * 1000000000ULL + (rem * 1000000000ULL) / hpet_counter_hz;
}

void sleep_us(uint64_t us)
{
    if (!hpet_base || !hpet_period_fs) {
        for (volatile uint64_t i = 0; i < us * 10; i++);
        return;
    }

    uint64_t ticks_needed = 0;
    if (hpet_counter_hz)
    {
        uint64_t whole = us / 1000000ULL;
        uint64_t frac = us % 1000000ULL;
        ticks_needed = whole * hpet_counter_hz;
        ticks_needed += (frac * hpet_counter_hz + 999999ULL) / 1000000ULL;
    }
    if (!ticks_needed) ticks_needed = 1;

    uint64_t start = hpet_read(HPET_COUNTER);
    for (;;)
    {
        uint64_t now = hpet_read(HPET_COUNTER);
        if ((now - start) >= ticks_needed)
            break;
        asm volatile("sti; hlt; cli" ::: "memory");
    }
}

void sleep_ms(uint32_t ms)
{
    sleep_us((uint64_t)ms * 1000ULL);
}

void sleep(uint32_t ms)
{
    sleep_ms(ms);
}
