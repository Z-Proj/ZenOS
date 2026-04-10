/**
 * 
 * @file : /src/cpu/acpi/acpi.h
 * @brief : ACPI subsystem for hardware discovery and IRQ remapping.
 *          Parses APIC, HPET tables and handles power control.
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

#ifndef ACPI_H
#define ACPI_H

#include "stdint.h"
#include "stdbool.h"

void AcpiInit();
int AcpiRemapIrq(int irq);
bool AcpiGetIrqOverride(int irq, int *gsi, uint16_t *flags);
void AcpiShutdown();
void AcpiReboot();
bool AcpiIsEnabled();

#endif
