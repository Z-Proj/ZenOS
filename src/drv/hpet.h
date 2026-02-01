#ifndef HPET_H
#define HPET_H

#include <stdint.h>
#include "../cpu/isr.h"

extern volatile uint64_t hpet_ticks;

void hpet_init(uint32_t frequency_hz);
void SetHpetAddress(uint64_t addr);

#endif
