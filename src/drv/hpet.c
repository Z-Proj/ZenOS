#include <stdint.h>
#include "../cpu/isr.h"
#include "../drv/local_apic.h"
#include "../kernel/sched.h"

#define HPET_BASE 0xFED00000
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

static volatile hpet_regs_t *hpet =
    (volatile hpet_regs_t *)HPET_BASE;

static volatile hpet_timer_t *t0 =
    (volatile hpet_timer_t *)(HPET_BASE + 0x100);

static uint64_t ticks_per_irq;

static void hpet_handler(registers_t *r)
{
    (void)r;
    hpet->int_status = 1;
    LocalApicSendEOI();
    sched_tick();
}

void hpet_init(uint32_t frequency_hz)
{
    if (!frequency_hz)
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

    register_interrupt_handler(
        HPET_IRQ_VECTOR,
        hpet_handler,
        "HPET Timer"
    );

    hpet->config = 1;
}
