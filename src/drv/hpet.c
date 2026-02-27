#include <stdint.h>
#include "../cpu/isr.h"
#include "../drv/local_apic.h"
#include "../drv/keyboard.h"
#include "../kernel/sched.h"
#include "../libk/debug/log.h"

#define HPET_IRQ_VECTOR 0x22

typedef struct {
    uint64_t cap_id;
    uint64_t _r0;
    uint64_t config;
    uint64_t _r1;
    uint64_t int_status;
    uint64_t _r2;
    uint64_t counter;
} __attribute__((packed)) hpet_regs_t;

typedef struct {
    uint64_t config;
    uint64_t comparator;
    uint64_t fsb;
    uint64_t _r;
} __attribute__((packed)) hpet_timer_t;

static volatile hpet_regs_t *hpet = NULL;
static volatile hpet_timer_t *t0 = NULL;

static uint64_t ticks_per_irq;
volatile uint64_t hpet_ticks = 0;
static uint32_t hpet_freq_hz = 0;

static void hpet_handler(registers_t *r)
{
    (void)r;
    hpet->int_status = 1;
    hpet_ticks++;
    LocalApicSendEOI();
    kbd_switcher_tick();
    sched_tick();
}

void SetHpetAddress(uint64_t addr)
{
    hpet = (volatile hpet_regs_t *)(uintptr_t)addr;
    t0 = (volatile hpet_timer_t *)((uintptr_t)addr + 0x100);
}

void hpet_init(uint32_t frequency_hz)
{
    if (!hpet || !frequency_hz)
        return;

    hpet->config = 0;
    hpet->counter = 0;

    uint64_t period_fs = hpet->cap_id >> 32;
    if (!period_fs)
        return;

    uint64_t ticks_per_sec = 1000000000000000ULL / period_fs;
    ticks_per_irq = ticks_per_sec / frequency_hz;
    if (!ticks_per_irq)
        ticks_per_irq = 1;

    t0->config = 0;
    t0->comparator = ticks_per_irq;
    t0->config = (1ULL << 2) | (1ULL << 4);

    register_interrupt_handler(HPET_IRQ_VECTOR, hpet_handler, "HPET Timer");
    hpet->config = 1;
    hpet_freq_hz = frequency_hz;
    log("HPET Initialized.", 4, 0);
}
void sleep(uint32_t ms)
{
    if (!hpet_freq_hz || !hpet) {
        for (volatile uint32_t i = 0; i < ms * 10000; i++);
        return;
    }
    uint64_t ticks_to_wait = ((uint64_t)ms * hpet_freq_hz + 999) / 1000;
    uint64_t start = hpet_ticks;
    while ((hpet_ticks - start) < ticks_to_wait)
        sched_yield();
}