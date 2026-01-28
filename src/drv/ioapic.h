#include "stdint.h"

extern uint8_t *g_ioApicAddr;

void IoApicInit();
void IoApicSetEntry(uint8_t *base, uint8_t index, uint64_t data);
void IoApicSetIrq(uint8_t *base, uint8_t irq, uint8_t vector, uint8_t dest_apic_id);