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
