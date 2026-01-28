

#include "vga.h"
#include "ioapic.h"
#include "../libk/debug/log.h"

uint8_t *g_ioApicAddr;

#define IOREGSEL 0x00
#define IOWIN 0x10

#define IOAPICID 0x00
#define IOAPICVER 0x01
#define IOAPICARB 0x02
#define IOREDTBL 0x10

static void IoApicOut(uint8_t *base, uint8_t reg, uint32_t val)
{
    *(volatile uint32_t *)(base + IOREGSEL) = reg;
    *(volatile uint32_t *)(base + IOWIN) = val;
}

static uint32_t IoApicIn(uint8_t *base, uint8_t reg)
{
    *(volatile uint32_t *)(base + IOREGSEL) = reg;
    return *(volatile uint32_t *)(base + IOWIN);
}

void IoApicSetEntry(uint8_t *base, uint8_t index, uint64_t data)
{
    IoApicOut(base, IOREDTBL + index * 2, (uint32_t)data);
    IoApicOut(base, IOREDTBL + index * 2 + 1, (uint32_t)(data >> 32));
}

void IoApicSetIrq(uint8_t *base, uint8_t irq, uint8_t vector, uint8_t dest_apic_id)
{
    uint64_t entry = vector |
                    (0 << 8) |
                    (0 << 11) |
                    (0 << 13) |
                    (0 << 15) |
                    (0 << 16) |
                    ((uint64_t)dest_apic_id << 56);

    IoApicSetEntry(base, irq, entry);
}

void IoApicInit()
{

    uint32_t x = IoApicIn(g_ioApicAddr, IOAPICVER);
    int count = ((x >> 16) & 0xff) + 1;

    log("I/O APIC pins = %d", 1, 0, count);

    for (int i = 0; i < count; ++i)
    {
        IoApicSetEntry(g_ioApicAddr, i, 1 << 16);
    }
    log("I/O APIC Initialized.", 4, 0);
}
