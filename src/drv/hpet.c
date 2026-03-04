#include <stdint.h>
#include "../cpu/isr.h"
#include "../drv/local_apic.h"
#include "../drv/keyboard.h"
#include "../kernel/sched.h"
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
static uint64_t ticks_per_irq   = 0;
static uint32_t hpet_freq_hz    = 0;
volatile uint64_t hpet_ticks    = 0;

static void hpet_handler(registers_t *r)
{
    (void)r;
    hpet_write(HPET_ISR, 1);
    hpet_ticks++;
    LocalApicSendEOI();
    kbd_switcher_tick();
    sched_tick();
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

    uint64_t ticks_per_sec = 1000000000000000ULL / hpet_period_fs;
    ticks_per_irq = ticks_per_sec / frequency_hz;
    if (!ticks_per_irq)
        ticks_per_irq = 1;

    hpet_write(HPET_T0_CFG, 0);
    hpet_write(HPET_T0_CMP, ticks_per_irq);
    hpet_write(HPET_T0_CFG, (1ULL << 2) | (1ULL << 3) | (1ULL << 4));

    register_interrupt_handler(HPET_IRQ_VECTOR, hpet_handler, "HPET Timer");

    hpet_write(HPET_CFG, 1);
    hpet_freq_hz = frequency_hz;

    log("HPET Initialized.", 4, 0);
}

void sleep_us(uint64_t us)
{
    if (!hpet_base || !hpet_period_fs) {
        for (volatile uint64_t i = 0; i < us * 10; i++);
        return;
    }

    uint64_t ticks_needed = (us * 1000000000ULL) / hpet_period_fs;
    if (!ticks_needed) ticks_needed = 1;

    uint64_t start = hpet_read(HPET_COUNTER);
    while ((hpet_read(HPET_COUNTER) - start) < ticks_needed)
        sched_yield();
}

void sleep_ms(uint32_t ms)
{
    if (!hpet_base || !hpet_freq_hz) {
        sleep_us((uint64_t)ms * 1000ULL);
        return;
    }

    uint64_t ticks_needed = ((uint64_t)ms * hpet_freq_hz + 999) / 1000;
    if (!ticks_needed) ticks_needed = 1;
    uint64_t deadline = hpet_ticks + ticks_needed;

    while (hpet_ticks < deadline)
        sched_yield();
}

void sleep(uint32_t ms)
{
    sleep_ms(ms);
}