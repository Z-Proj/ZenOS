/**
 * 
 * @file : /src/drv/ioapic.h
 * @brief : I/O APIC setup - maps IRQs to vectors with ACPI override support.
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

#include "stdint.h"

extern uint8_t *g_ioApicAddr;
extern uint32_t g_ioApicGsiBase;

void IoApicInit();
void IoApicSetEntry(uint8_t *base, uint8_t index, uint64_t data);
void IoApicSetIrq(uint8_t *base, uint8_t irq, uint8_t vector, uint8_t dest_apic_id);
void IoApicSetIrqMapped(int irq, int vector);
