#ifndef SMP_H
#define SMP_H
#include "stdint.h"
#include "acpi/acpi.h"

#define MAX_CPUS 8

typedef struct
{
    uint32_t processor_id;
    uint32_t lapic_id;
} cpu_info_t;

extern volatile uint32_t g_activeCpuCount;

void smp_prepare(void);
void init_smp();
uint32_t smp_cpu_count(void);
uint32_t smp_bsp_cpu_index(void);
uint32_t smp_current_cpu_index(void);
const cpu_info_t *smp_cpu_info(uint32_t cpu_index);

#endif
