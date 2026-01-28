#ifndef SMP_H
#define SMP_H
#include "stdint.h"
#include "acpi/acpi.h"

extern volatile uint32_t g_activeCpuCount;

void init_smp();

#endif