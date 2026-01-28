#ifndef ACPI_H
#define ACPI_H

#include "stdint.h"
#include "stdbool.h"

void AcpiInit();
int AcpiRemapIrq(int irq);
void AcpiShutdown();
void AcpiReboot();
bool AcpiIsEnabled();

#endif