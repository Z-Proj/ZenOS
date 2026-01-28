#include "acpi.h"
#include "../../drv/ioapic.h"
#include "../../drv/local_apic.h"
#include "../../libk/string.h"
#include "../../libk/ports.h"
#include "../../libk/debug/log.h"
#include "stdint.h"
#include "stdbool.h"
#include "../../drv/vga.h"


typedef struct {
    uint32_t signature;
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t oem[6];
    uint8_t oemTableId[8];
    uint32_t oemRevision;
    uint32_t creatorId;
    uint32_t creatorRevision;
} __attribute__((__packed__)) AcpiHeader;

typedef struct {
    AcpiHeader header;
    uint32_t firmwareControl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferredPMProfile;
    uint16_t sciInterrupt;
    uint32_t smiCommandPort;
    uint8_t acpiEnable;
    uint8_t acpiDisable;
    uint8_t s4BiosReq;
    uint8_t pStateCnt;
    uint32_t pm1aEventBlk;
    uint32_t pm1bEventBlk;
    uint32_t pm1aControlBlk;
    uint32_t pm1bControlBlk;
    uint32_t pm2ControlBlk;
    uint32_t pmTimerBlk;
    uint32_t gpe0Blk;
    uint32_t gpe1Blk;
    uint8_t pm1EventLength;
    uint8_t pm1ControlLength;
    uint8_t pm2ControlLength;
    uint8_t pmTimerLength;
    uint8_t gpe0BlkLength;
    uint8_t gpe1BlkLength;
    uint8_t gpe1Base;
    uint8_t cStateControl;
    uint16_t worstC2Latency;
    uint16_t worstC3Latency;
    uint16_t flushSize;
    uint16_t flushStride;
    uint8_t dutyOffset;
    uint8_t dutyWidth;
    uint8_t dayAlrm;
    uint8_t monAlrm;
    uint8_t century;
    uint16_t bootArchFlags;
    uint8_t reserved2;
    uint32_t flags;
    uint8_t resetReg[12];
    uint8_t resetValue;
    uint16_t armBootArchFlags;
    uint8_t fadtMinorVersion;
    uint64_t xFirmwareControl;
    uint64_t xDsdt;
    uint8_t xPm1aEventBlk[12];
    uint8_t xPm1bEventBlk[12];
    uint8_t xPm1aControlBlk[12];
    uint8_t xPm1bControlBlk[12];
    uint8_t xPm2ControlBlk[12];
    uint8_t xPmTimerBlk[12];
    uint8_t xGpe0Blk[12];
    uint8_t xGpe1Blk[12];
    uint8_t sleepControlReg[12];
    uint8_t sleepStatusReg[12];
    uint64_t hypervisorVendorId;
} __attribute__((__packed__)) AcpiFadt;

typedef struct {
    AcpiHeader header;
    uint32_t localApicAddr;
    uint32_t flags;
} __attribute__((__packed__)) AcpiMadt;

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((__packed__)) ApicHeader;

#define APIC_TYPE_LOCAL_APIC 0
#define APIC_TYPE_IO_APIC 1
#define APIC_TYPE_INTERRUPT_OVERRIDE 2
#define APIC_TYPE_NMI_SOURCE 3
#define APIC_TYPE_LOCAL_APIC_NMI 4
#define APIC_TYPE_LOCAL_APIC_OVERRIDE 5
#define APIC_TYPE_IO_SAPIC 6
#define APIC_TYPE_LOCAL_SAPIC 7
#define APIC_TYPE_PLATFORM_INTERRUPT 8
#define APIC_TYPE_LOCAL_X2APIC 9
#define APIC_TYPE_LOCAL_X2APIC_NMI 10

typedef struct {
    ApicHeader header;
    uint8_t acpiProcessorId;
    uint8_t apicId;
    uint32_t flags;
} __attribute__((__packed__)) ApicLocalApic;

typedef struct {
    ApicHeader header;
    uint8_t ioApicId;
    uint8_t reserved;
    uint32_t ioApicAddress;
    uint32_t globalSystemInterruptBase;
} __attribute__((__packed__)) ApicIoApic;

typedef struct {
    ApicHeader header;
    uint8_t bus;
    uint8_t source;
    uint32_t interrupt;
    uint16_t flags;
} __attribute__((__packed__)) ApicInterruptOverride;

typedef struct {
    ApicHeader header;
    uint8_t acpiProcessorId;
    uint16_t flags;
    uint8_t lint;
} __attribute__((__packed__)) ApicLocalApicNmi;

typedef struct {
    ApicHeader header;
    uint16_t reserved;
    uint64_t localApicAddress;
} __attribute__((__packed__)) ApicLocalApicOverride;

typedef struct {
    ApicHeader header;
    uint32_t x2ApicId;
    uint32_t flags;
    uint32_t acpiId;
} __attribute__((__packed__)) ApicLocalX2Apic;

static AcpiFadt *s_fadt;
static AcpiMadt *s_madt;
static uint16_t s_slp_typa = 0;
static uint16_t s_slp_typb = 0;
static bool s_s5_found = false;
static bool s_acpi_enabled = false;

static bool ValidateChecksum(void *table, uint32_t length) {
    uint8_t sum = 0;
    uint8_t *ptr = (uint8_t *)table;
    for (uint32_t i = 0; i < length; i++) {
        sum += ptr[i];
    }
    return sum == 0;
}

static bool AcpiFindS5(uint8_t *dsdt_addr, uint32_t dsdt_length) {
    uint32_t *signature = (uint32_t *)dsdt_addr;

    if (*signature != 0x54445344)
        return false;

    if (!ValidateChecksum(dsdt_addr, dsdt_length)) {
        log("DSDT checksum invalid", 2, 1);
        return false;
    }

    uint8_t *end = dsdt_addr + dsdt_length;
    uint8_t *ptr = dsdt_addr + sizeof(AcpiHeader);

    while (ptr < end - 4) {
        if (ptr[0] == '_' && ptr[1] == 'S' && ptr[2] == '5' && ptr[3] == '_') {
            ptr += 4;
            if (*ptr == 0x12) {
                ptr++;
                uint8_t pkg_length = *ptr++;
                if (pkg_length >= 4) {
                    ptr++;
                    if (*ptr == 0x0A) {
                        ptr++;
                        s_slp_typa = *ptr++;
                    }
                    if (*ptr == 0x0A) {
                        ptr++;
                        s_slp_typb = *ptr++;
                    }
                    s_s5_found = true;
                    return true;
                }
            }
        }
        ptr++;
    }
    return false;
}

static void AcpiEnableMode(void) {
    if (!s_fadt)
        return;

    if (s_fadt->smiCommandPort) {
        outportb(s_fadt->smiCommandPort, s_fadt->acpiEnable);

        uint16_t timeout = 1000;
        while (timeout-- > 0) {
            uint16_t status = inportw(s_fadt->pm1aControlBlk);
            if (status & 0x0001) {
                s_acpi_enabled = true;
                return;
            }
            for (volatile int i = 0; i < 10000; i++);
        }
        log("ACPI enable timeout", 2, 1);
    } else {
        s_acpi_enabled = true;
    }
}

static void AcpiParseFacp(AcpiFadt *facp) {
    if (!ValidateChecksum(facp, facp->header.length)) {
        log("FADT checksum invalid", 2, 1);
        return;
    }

    s_fadt = facp;

    if (facp->dsdt) {
        AcpiHeader *dsdt_header = (AcpiHeader *)(uintptr_t)facp->dsdt;
        AcpiFindS5((uint8_t *)dsdt_header, dsdt_header->length);
    } else if (facp->xDsdt) {
        AcpiHeader *dsdt_header = (AcpiHeader *)(uintptr_t)facp->xDsdt;
        AcpiFindS5((uint8_t *)dsdt_header, dsdt_header->length);
    }

    AcpiEnableMode();
}

void AcpiShutdown(void) {
    if (!s_fadt || !s_s5_found) {
        outportb(0x64, 0xFE);
        asm volatile("cli; hlt" : : : "memory");
        return;
    }

    uint16_t pm1a_cnt = s_fadt->pm1aControlBlk;
    uint16_t pm1b_cnt = s_fadt->pm1bControlBlk;

    if (pm1a_cnt == 0) {
        log("No PM1a control block", 2, 1);
        outportb(0x64, 0xFE);
        asm volatile("cli; hlt" : : : "memory");
        return;
    }

    uint16_t pm1a_val = inportw(pm1a_cnt);
    pm1a_val &= ~(0x7 << 10);
    pm1a_val |= (s_slp_typa << 10) | (1 << 13);

    asm volatile("cli" : : : "memory");
    outportw(pm1a_cnt, pm1a_val);

    if (pm1b_cnt != 0) {
        uint16_t pm1b_val = inportw(pm1b_cnt);
        pm1b_val &= ~(0x7 << 10);
        pm1b_val |= (s_slp_typb << 10) | (1 << 13);
        outportw(pm1b_cnt, pm1b_val);
    }

    for (volatile int i = 0; i < 1000000; i++);

    log("Shutdown failed, trying KB reset", 2, 1);
    outportb(0x64, 0xFE);
    asm volatile("hlt" : : : "memory");
}

void AcpiReboot(void) {
    if (s_fadt && s_fadt->resetReg[0] == 1) {
        uint32_t reset_port = s_fadt->resetReg[8] | (s_fadt->resetReg[9] << 8) |
                              (s_fadt->resetReg[10] << 16) | (s_fadt->resetReg[11] << 24);
        uint8_t reset_val = s_fadt->resetValue;

        if (reset_port != 0) {
            outportb(reset_port, reset_val);
            for (volatile int i = 0; i < 1000000; i++);
        }
    }

    uint8_t temp;
    asm volatile("cli" : : : "memory");
    do {
        temp = inportb(0x64);
        if (temp & 0x01)
            inportb(0x60);
    } while (temp & 0x02);

    outportb(0x64, 0xFE);
    asm volatile("hlt" : : : "memory");
}

static void AcpiParseApic(AcpiMadt *madt) {
    if (!ValidateChecksum(madt, madt->header.length)) {
        log("MADT checksum invalid", 2, 1);
        return;
    }

    s_madt = madt;
    g_localApicAddr = (uint8_t *)(uintptr_t)madt->localApicAddr;

    uint8_t *p = (uint8_t *)(madt + 1);
    uint8_t *end = (uint8_t *)madt + madt->header.length;
    int irq_overrides = 0;

    while (p < end) {
        ApicHeader *header = (ApicHeader *)p;

        if (header->type == APIC_TYPE_IO_APIC) {
            ApicIoApic *s = (ApicIoApic *)p;
            g_ioApicAddr = (uint8_t *)(uintptr_t)s->ioApicAddress;
        }
        else if (header->type == APIC_TYPE_INTERRUPT_OVERRIDE) {
            irq_overrides++;
        }
        else if (header->type == APIC_TYPE_LOCAL_APIC_OVERRIDE) {
            ApicLocalApicOverride *s = (ApicLocalApicOverride *)p;
            g_localApicAddr = (uint8_t *)(uintptr_t)s->localApicAddress;
        }

        p += header->length;
    }

    log("ACPI: %d IRQ overrides", 1, 0, irq_overrides);
}

static void AcpiParseDT(AcpiHeader *header) {
    if (!ValidateChecksum(header, header->length)) {
        log("Table checksum invalid", 2, 1);
        return;
    }

    if (header->signature == 0x50434146) {
        AcpiParseFacp((AcpiFadt *)header);
    }
    else if (header->signature == 0x43495041) {
        AcpiParseApic((AcpiMadt *)header);
    }
}

static void AcpiParseRsdt(AcpiHeader *rsdt) {
    if (!ValidateChecksum(rsdt, rsdt->length)) {
        log("RSDT checksum invalid", 2, 1);
        return;
    }

    uint32_t *p = (uint32_t *)(rsdt + 1);
    uint32_t *end = (uint32_t *)((uint8_t *)rsdt + rsdt->length);

    while (p < end) {
        AcpiParseDT((AcpiHeader *)(uintptr_t)*p);
        p++;
    }
}

static void AcpiParseXsdt(AcpiHeader *xsdt) {
    if (!ValidateChecksum(xsdt, xsdt->length)) {
        log("XSDT checksum invalid", 2, 1);
        return;
    }

    uint64_t *p = (uint64_t *)(xsdt + 1);
    uint64_t *end = (uint64_t *)((uint8_t *)xsdt + xsdt->length);

    while (p < end) {
        AcpiParseDT((AcpiHeader *)(uintptr_t)*p);
        p++;
    }
}

static bool AcpiParseRsdp(uint8_t *p) {
    if (!ValidateChecksum(p, 20)) {
        log("RSDP checksum invalid", 3, 1);
        return false;
    }

    char oem[7];
    memcpy(oem, p + 9, 6);
    oem[6] = 0;

    uint8_t revision = p[15];
    log("ACPI Rev %d (%s)", 1, 0, revision, oem);

    if (revision == 0) {
        uint32_t rsdtAddr = *(uint32_t *)(p + 16);
        AcpiParseRsdt((AcpiHeader *)(uintptr_t)rsdtAddr);
    }
    else if (revision == 2) {
        if (!ValidateChecksum(p, 36)) {
            log("Extended RSDP checksum invalid", 3, 1);
            return false;
        }

        uint64_t xsdtAddr = *(uint64_t *)(p + 24);
        if (xsdtAddr) {
            AcpiParseXsdt((AcpiHeader *)(uintptr_t)xsdtAddr);
        } else {
            uint32_t rsdtAddr = *(uint32_t *)(p + 16);
            AcpiParseRsdt((AcpiHeader *)(uintptr_t)rsdtAddr);
        }
    }
    else {
        log("Unsupported ACPI revision %d", 3, 1, revision);
        return false;
    }

    return true;
}

void AcpiInit(void) {
    uint8_t *p = (uint8_t *)0x000e0000;
    uint8_t *end = (uint8_t *)0x000fffff;

    while (p < end) {
        uint64_t signature = *(uint64_t *)p;
        if (signature == 0x2052545020445352) {
            if (AcpiParseRsdp(p)) {
                return;
            }
        }
        p += 16;
    }

    log("RSDP not found", 3, 1);
}

int AcpiRemapIrq(int irq) {
    if (!s_madt)
        return irq;

    uint8_t *p = (uint8_t *)(s_madt + 1);
    uint8_t *end = (uint8_t *)s_madt + s_madt->header.length;

    while (p < end) {
        ApicHeader *header = (ApicHeader *)p;
        if (header->type == APIC_TYPE_INTERRUPT_OVERRIDE) {
            ApicInterruptOverride *s = (ApicInterruptOverride *)p;
            if (s->source == irq) {
                return s->interrupt;
            }
        }
        p += header->length;
    }

    return irq;
}

bool AcpiIsEnabled(void) {
    return s_acpi_enabled;
}