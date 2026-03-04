#ifndef HPET_H
#define HPET_H

#include <stdint.h>
#include "../cpu/isr.h"

extern volatile uint64_t hpet_ticks;

void hpet_init(uint32_t frequency_hz);
void SetHpetAddress(uint64_t addr);
uint64_t hpet_monotonic_ns(void);
uint32_t hpet_get_freq_hz(void);
void sleep_us(uint64_t us);
void sleep_ms(uint32_t ms);
void sleep(uint32_t ms);

#endif
