#include "gdt.h"
#include "../libk/debug/log.h"
#include "../libk/string.h"

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

static void gdt_set_gate(int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran);
static void gdt_set_tss(int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran);

gdt_entry_t gdt_entries[7];
gdt_ptr_t gdt_ptr;
tss_t tss;

void init_gdt()
{
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 7) - 1;
    gdt_ptr.base = (uint64_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);                // 0x00 - Null
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xAF);    // 0x08 - Kernel Code
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);    // 0x10 - Kernel Data
    gdt_set_gate(3, 0, 0xFFFFF, 0xF2, 0xCF);    // 0x18 - User Data (SWAPPED!)
    gdt_set_gate(4, 0, 0xFFFFF, 0xFA, 0xAF);    // 0x20 - User Code (SWAPPED!)

    memset(&tss, 0, sizeof(tss_t));
    tss.iopb_offset = sizeof(tss_t);
    gdt_set_tss(5, (uint64_t)&tss, sizeof(tss_t), 0x89, 0x00);  // 0x28/0x30 - TSS

    load_gdt(&gdt_ptr);
    __asm__ volatile("ltr %0" : : "r"((uint16_t)(5 * 8)));
    log("GDT Installed.", 4, 0);
}

static void gdt_set_gate(int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt_entries[num].base_low = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access = access;
}

static void gdt_set_tss(int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt_entries[num].limit_low = limit & 0xFFFF;
    gdt_entries[num].base_low = base & 0xFFFF;
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].access = access;
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num + 1].limit_low = (base >> 32) & 0xFFFF;
    gdt_entries[num + 1].base_low = (base >> 48) & 0xFFFF;
    gdt_entries[num + 1].base_middle = 0;
    gdt_entries[num + 1].access = 0;
    gdt_entries[num + 1].granularity = 0;
    gdt_entries[num + 1].base_high = 0;
}