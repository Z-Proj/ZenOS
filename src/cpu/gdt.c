#include "gdt.h"
#include "../libk/debug/log.h"
#include "../libk/string.h"
#include "smp.h"

struct gdt_entry_struct
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));
typedef struct gdt_entry_struct gdt_entry_t;

struct tss_entry_struct
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));
typedef struct tss_entry_struct tss_entry_t;

struct gdt_ptr_struct
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));
typedef struct gdt_ptr_struct gdt_ptr_t;

extern void load_gdt(gdt_ptr_t *gdt_ptr);

static void gdt_set_gate(gdt_entry_t *entries, int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran);
static void gdt_set_tss(gdt_entry_t *entries, int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran);

static gdt_entry_t gdt_entries[MAX_CPUS][7];
static gdt_ptr_t gdt_ptrs[MAX_CPUS];
static tss_t tss_entries[MAX_CPUS];

void init_gdt_for_cpu(uint32_t cpu_index)
{
    if (cpu_index >= MAX_CPUS)
        cpu_index = 0;

    gdt_ptr_t *gdt_ptr = &gdt_ptrs[cpu_index];
    gdt_entry_t *cpu_gdt = gdt_entries[cpu_index];
    tss_t *cpu_tss = &tss_entries[cpu_index];

    gdt_ptr->limit = (sizeof(gdt_entry_t) * 7) - 1;
    gdt_ptr->base = (uint64_t)cpu_gdt;

    memset(cpu_gdt, 0, sizeof(gdt_entries[cpu_index]));

    gdt_set_gate(cpu_gdt, 0, 0, 0, 0, 0);
    gdt_set_gate(cpu_gdt, 1, 0, 0xFFFFF, 0x9A, 0xAF);
    gdt_set_gate(cpu_gdt, 2, 0, 0xFFFFF, 0x92, 0xCF);
    gdt_set_gate(cpu_gdt, 3, 0, 0xFFFFF, 0xF2, 0xCF);
    gdt_set_gate(cpu_gdt, 4, 0, 0xFFFFF, 0xFA, 0xAF);

    memset(cpu_tss, 0, sizeof(*cpu_tss));
    cpu_tss->iopb_offset = sizeof(*cpu_tss);
    gdt_set_tss(cpu_gdt, 5, (uint64_t)cpu_tss, sizeof(*cpu_tss), 0x89, 0x00);

    load_gdt(gdt_ptr);
    __asm__ volatile("ltr %0" : : "r"((uint16_t)(5 * 8)));
    log("GDT Installed.", 4, 0);
}

tss_t *gdt_get_tss(uint32_t cpu_index)
{
    if (cpu_index >= MAX_CPUS)
        return 0;

    return &tss_entries[cpu_index];
}

static void gdt_set_gate(gdt_entry_t *entries, int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    entries[num].base_low = (base & 0xFFFF);
    entries[num].base_middle = (base >> 16) & 0xFF;
    entries[num].base_high = (base >> 24) & 0xFF;

    entries[num].limit_low = (limit & 0xFFFF);
    entries[num].granularity = (limit >> 16) & 0x0F;
    entries[num].granularity |= gran & 0xF0;
    entries[num].access = access;
}

static void gdt_set_tss(gdt_entry_t *entries, int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    entries[num].limit_low = limit & 0xFFFF;
    entries[num].base_low = base & 0xFFFF;
    entries[num].base_middle = (base >> 16) & 0xFF;
    entries[num].access = access;
    entries[num].granularity = (limit >> 16) & 0x0F;
    entries[num].granularity |= gran & 0xF0;
    entries[num].base_high = (base >> 24) & 0xFF;

    entries[num + 1].limit_low = (base >> 32) & 0xFFFF;
    entries[num + 1].base_low = (base >> 48) & 0xFFFF;
    entries[num + 1].base_middle = 0;
    entries[num + 1].access = 0;
    entries[num + 1].granularity = 0;
    entries[num + 1].base_high = 0;
}
