#include "smp.h"
#include "../libk/debug/log.h"
#include "../drv/local_apic.h"
#include "../libk/string.h"
#include "sse_fpu.h"
#include "../libk/limine.h"
#include "../libk/core/syscall.h"
#include "gdt.h"
#include "idt.h"
#include "id/core.h"
#include "../drv/vga.h"

#define STACK_SIZE 4096

static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};

volatile uint32_t g_activeCpuCount = 1;
static cpu_info_t g_cpu_info[MAX_CPUS];
static uint32_t g_cpu_count = 1;
static uint32_t g_bsp_cpu_index = 0;

void ap_entry(struct limine_smp_info *info) {
    uint32_t cpu_index = (uint32_t)info->extra_argument;
    enable_sse_and_fpu();
    init_gdt_for_cpu(cpu_index);
    init_idt();
    LocalApicInit();
    LocalApicTimerInit(200);
    init_syscalls();
    __atomic_add_fetch(&g_activeCpuCount, 1, __ATOMIC_SEQ_CST);
    ap_main();
    while(1);
}

void smp_prepare(void)
{
    struct limine_smp_response *smp = smp_request.response;
    if (smp == NULL) {
        clr();
        log("Failed to fetch SMP (CPU) info.", 0, 1);
    }
    if(smp->cpu_count > MAX_CPUS) {
        clr();
        log("\n\nThis system cannot run ZenOS\n\n - This system does not meet the requirement of maximum 8 CPUs.\n\nConsider downgrading your CPU.\nDecrease CPUs available if on a VM.\n ", 0, 1);
    }

    g_cpu_count = (uint32_t)smp->cpu_count;
    g_bsp_cpu_index = 0;

    for (uint32_t i = 0; i < g_cpu_count; i++)
    {
        g_cpu_info[i].processor_id = smp->cpus[i]->processor_id;
        g_cpu_info[i].lapic_id = smp->cpus[i]->lapic_id;
        if (smp->cpus[i]->lapic_id == smp->bsp_lapic_id)
            g_bsp_cpu_index = i;
    }
}

void init_smp() {
    struct limine_smp_response *smp = smp_request.response;
    log("Bootstrap Processor ID: %d, Total CPUs: %d", 1, 0,
        smp->bsp_lapic_id, smp->cpu_count);
    for (size_t i = 0; i < smp->cpu_count; i++) {
        if (smp->cpus[i]->lapic_id != smp->bsp_lapic_id) {
            log("Starting CPU %lu (LAPIC ID %d)", 1, 0,
                i, smp->cpus[i]->lapic_id);
            smp->cpus[i]->extra_argument = i;
            smp->cpus[i]->goto_address = ap_entry;
        }
    }
    while (g_activeCpuCount < smp->cpu_count) {
        asm volatile("pause");
    }
    log("All %d CPUs online", 4, 0, smp->cpu_count);
}

uint32_t smp_cpu_count(void)
{
    return g_cpu_count;
}

uint32_t smp_bsp_cpu_index(void)
{
    return g_bsp_cpu_index;
}

uint32_t smp_current_cpu_index(void)
{
    int lapic_id = LocalApicGetId();

    for (uint32_t i = 0; i < g_cpu_count; i++)
    {
        if ((int)g_cpu_info[i].lapic_id == lapic_id)
            return i;
    }

    return g_bsp_cpu_index;
}

const cpu_info_t *smp_cpu_info(uint32_t cpu_index)
{
    if (cpu_index >= g_cpu_count)
        return NULL;

    return &g_cpu_info[cpu_index];
}
