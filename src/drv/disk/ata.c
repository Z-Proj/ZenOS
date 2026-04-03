#include "ata.h"
#include "../../libk/ports.h"
#include "../../libk/string.h"
#include "../../libk/debug/log.h"
#include "../../libk/core/mem.h"
#include "../../libk/spinlock.h"
#include "../net/pci.h"
#include "../local_apic.h"
#include "../../cpu/isr.h"

#define BMIDE_CMD_START     0x01
#define BMIDE_CMD_WRITE     0x08
#define BMIDE_STATUS_ACTIVE 0x01
#define BMIDE_STATUS_ERR    0x02
#define BMIDE_STATUS_IRQ    0x04

#define BMIDE_REG_CMD       0x00
#define BMIDE_REG_STATUS    0x02
#define BMIDE_REG_PRDT      0x04

#define ATA_CMD_READ_DMA    0xC8
#define ATA_CMD_WRITE_DMA   0xCA

#define PRDT_EOT            0x80000000

typedef struct {
    uint32_t phys_addr;
    uint16_t byte_count;
    uint16_t flags;
} __attribute__((packed)) prdt_entry_t;

ata_drive_t drives[4];
static int8_t current_selected_drive = -1;

static uint16_t bmide_base = 0;
static int dma_available = 0;

static volatile uint8_t irq_fired[2] = {0, 0};

static prdt_entry_t *prdt = NULL;
static uint8_t *bounce_buf = NULL;
static uint64_t bounce_buf_phys = 0;
static uint64_t prdt_phys = 0;

static spinlock_t ata_lock[2] = {{0}, {0}};



#define DISK_CACHE_SLOTS 128

typedef struct {
    uint32_t lba;
    uint8_t  drive;
    uint8_t  valid;
    uint8_t  data[512];
} disk_cache_entry_t;

static disk_cache_entry_t disk_cache[DISK_CACHE_SLOTS];
static spinlock_t cache_lock = {0};

static int cache_lookup(uint8_t drive, uint32_t lba, uint8_t *buf)
{
    uint32_t slot = (lba * 2654435761u + drive) % DISK_CACHE_SLOTS;
    disk_cache_entry_t *e = &disk_cache[slot];
    if (e->valid && e->drive == drive && e->lba == lba)
    {
        memcpy(buf, e->data, 512);
        return 1;
    }
    return 0;
}

static void cache_insert(uint8_t drive, uint32_t lba, const uint8_t *buf)
{
    uint32_t slot = (lba * 2654435761u + drive) % DISK_CACHE_SLOTS;
    disk_cache_entry_t *e = &disk_cache[slot];
    e->drive = drive;
    e->lba   = lba;
    e->valid = 1;
    memcpy(e->data, buf, 512);
}

static void cache_invalidate(uint8_t drive, uint32_t lba)
{
    uint32_t slot = (lba * 2654435761u + drive) % DISK_CACHE_SLOTS;
    disk_cache_entry_t *e = &disk_cache[slot];
    if (e->valid && e->drive == drive && e->lba == lba)
        e->valid = 0;
}

static void ata_delay(uint16_t base_io)
{
    for (int i = 0; i < 4; i++)
        inportb(base_io + ATA_REG_STATUS);
}

static void ata_soft_reset(uint16_t ctrl_io)
{
    outportb(ctrl_io, 0x04);
    ata_delay(ctrl_io);
    outportb(ctrl_io, 0x00);
}

static ata_error_t ata_wait_ready(uint16_t base_io)
{
    uint16_t ctrl = (base_io == ATA_PRIMARY_IO) ? ATA_PRIMARY_DEVCTL : ATA_SECONDARY_DEVCTL;
    uint32_t timeout = 0;
    inportb(ctrl);
    inportb(ctrl);
    inportb(ctrl);
    inportb(ctrl);
    while (timeout < ATA_TIMEOUT_MS)
    {
        uint8_t status = inportb(ctrl);
        if (!(status & ATA_SR_BSY))
        {
            if (status & ATA_SR_ERR)
                return ATA_ERR_GENERAL;
            if (status & ATA_SR_DF)
                return ATA_ERR_DRIVE_FAULT;
            return ATA_SUCCESS;
        }
        timeout++;
    }
    return ATA_ERR_TIMEOUT;
}

static ata_error_t ata_wait_drq(uint16_t base_io)
{
    uint32_t timeout = 0;
    asm volatile("sti");
    while (timeout < ATA_TIMEOUT_MS)
    {
        uint8_t status = inportb(base_io + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY))
        {
            if (status & ATA_SR_ERR){
                asm volatile("cli");
                return ATA_ERR_GENERAL;}
            if (status & ATA_SR_DF){
                asm volatile("cli");
                return ATA_ERR_DRIVE_FAULT;}
            if (status & ATA_SR_DRQ){
                asm volatile("cli");
                return ATA_SUCCESS;}
            asm volatile("cli");
            return ATA_ERR_NO_DRQ;
        }
        timeout++;
    }
    asm volatile("cli");
    return ATA_ERR_TIMEOUT;
}

static void ata_select_drive(uint16_t base_io, uint8_t drive_select)
{
    int8_t id = (base_io == ATA_PRIMARY_IO ? 0 : 2) | drive_select;
    if (current_selected_drive == id)
        return;
    current_selected_drive = id;
    outportb(base_io + ATA_REG_HDDEVSEL, 0xA0 | (drive_select << 4));
    ata_delay(base_io);
}

static void ata_setup_lba28(uint16_t base_io, uint32_t lba, uint8_t count, uint8_t drive_select)
{
    outportb(base_io + ATA_REG_HDDEVSEL, 0xE0 | (drive_select << 4) | ((lba >> 24) & 0x0F));
    outportb(base_io + ATA_REG_SECCOUNT0, count);
    outportb(base_io + ATA_REG_LBA0, lba & 0xFF);
    outportb(base_io + ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outportb(base_io + ATA_REG_LBA2, (lba >> 16) & 0xFF);
}

static void irq14_handler(registers_t *r)
{
    (void)r;
    __asm__ volatile("mfence" ::: "memory");
    irq_fired[0] = 1;
    __asm__ volatile("mfence" ::: "memory");
    outportb(bmide_base + BMIDE_REG_STATUS, inportb(bmide_base + BMIDE_REG_STATUS));
}

static void irq15_handler(registers_t *r)
{
    (void)r;
    __asm__ volatile("mfence" ::: "memory");
    irq_fired[1] = 1;
    __asm__ volatile("mfence" ::: "memory");
    outportb(bmide_base + BMIDE_REG_STATUS + 8, inportb(bmide_base + BMIDE_REG_STATUS + 8));
}

static void ata_get_drive_name(uint8_t drive, char *name_buffer)
{
    uint16_t identify_buffer[256];
    if (ata_identify_drive(drive, identify_buffer) != ATA_SUCCESS)
    {
        name_buffer[0] = '\0';
        return;
    }
    char *name = (char *)&identify_buffer[27];
    int name_len = 0;
    for (int i = 0; i < 40; i += 2)
    {
        if (name[i + 1] != 0 && name[i + 1] != ' ')
            name_buffer[name_len++] = name[i + 1];
        if (name[i] != 0 && name[i] != ' ')
            name_buffer[name_len++] = name[i];
    }
    while (name_len > 0 && name_buffer[name_len - 1] == ' ')
        name_len--;
    name_buffer[name_len] = '\0';
}

ata_device_type_t ata_detect_drive(uint8_t drive)
{
    if (drive >= 4)
        return ATA_DEVICE_UNKNOWN;

    uint8_t chan = (drive < 2) ? 0 : 1;
    spinlock_acquire_raw(&ata_lock[chan]);
    uint16_t base_io = (drive < 2) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
    uint8_t drive_select = drive & 1;

    ata_select_drive(base_io, drive_select);

    if (ata_wait_ready(base_io) != ATA_SUCCESS) {
        spinlock_release_raw(&ata_lock[chan]);
        return ATA_DEVICE_UNKNOWN;
    }

    uint8_t cl = inportb(base_io + ATA_REG_LBA1);
    uint8_t ch = inportb(base_io + ATA_REG_LBA2);
    spinlock_release_raw(&ata_lock[chan]);

    if (cl == 0x14 && ch == 0xEB) return ATA_DEVICE_PATAPI;
    if (cl == 0x69 && ch == 0x96) return ATA_DEVICE_SATAPI;
    if (cl == 0x00 && ch == 0x00) return ATA_DEVICE_PATA;
    if (cl == 0x3C && ch == 0xC3) return ATA_DEVICE_SATA;

    return ATA_DEVICE_UNKNOWN;
}

ata_error_t ata_init(void)
{
    spinlock_init(&ata_lock[0]);
    spinlock_init(&ata_lock[1]);
    pci_device_t *ide = pci_find_device_by_class(PCI_CLASS_STORAGE, PCI_SUBCLASS_IDE);
    if (ide)
    {
        pci_enable_bus_mastering(ide);
        uint32_t bar4 = pci_read(ide->bus, ide->slot, ide->func, PCI_BAR4);
        if (bar4 & 1)
        {
            bmide_base = (uint16_t)(bar4 & 0xFFFC);

            uint64_t prdt_page = alloc_page();
            uint64_t buf_page  = alloc_page();

            if (prdt_page && buf_page)
            {
                prdt_phys       = prdt_page;
                bounce_buf_phys = buf_page;
                prdt            = (prdt_entry_t *)(prdt_page + KERNEL_VIRT_OFFSET);
                bounce_buf      = (uint8_t *)(buf_page + KERNEL_VIRT_OFFSET);
                dma_available   = 1;

                register_interrupt_handler(46, irq14_handler, "ATA Primary DMA");
                register_interrupt_handler(47, irq15_handler, "ATA Secondary DMA");

                log("ATA DMA enabled. BMIDE base: 0x%x", 4, 0, bmide_base);
            }
        }
    }

    if (!dma_available)
        log("ATA DMA unavailable, falling back to PIO.", 2, 0);

    int found_drives = 0;
    for (int i = 0; i < 4; i++)
    {
        drives[i].base_io      = (i < 2) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
        drives[i].ctrl_io      = (i < 2) ? ATA_PRIMARY_DEVCTL : ATA_SECONDARY_DEVCTL;
        drives[i].drive_select = i & 1;
        drives[i].exists       = 0;

        ata_soft_reset(drives[i].ctrl_io);
        drives[i].type = ata_detect_drive(i);

        if (drives[i].type == ATA_DEVICE_PATA || drives[i].type == ATA_DEVICE_SATA)
        {
            drives[i].exists = 1;
            char drive_name[41];
            ata_get_drive_name(i, drive_name);
            const char *type_str = (drives[i].type == ATA_DEVICE_PATA) ? "PATA" : "SATA";
            if (drive_name[0] != '\0')
            {
                log("Drive %d (%s): %s", 1, 0, i, type_str, drive_name);
                found_drives++;
            }
            else
                drives[i].exists = 0;
        }
    }
    log("Initialization complete. Found %i drive%s.", 4, 0, found_drives, found_drives == 1 ? "" : "s");
    return ATA_SUCCESS;
}

ata_error_t ata_identify_drive(uint8_t drive, uint16_t *buffer)
{
    if (drive >= 4 || !drives[drive].exists || !buffer)
        return ATA_ERR_INVALID_PARAM;
    uint8_t chan = (drive < 2) ? 0 : 1;
    spinlock_acquire_raw(&ata_lock[chan]);
    uint16_t base_io = drives[drive].base_io;
    ata_select_drive(base_io, drives[drive].drive_select);
    ata_error_t err = ata_wait_ready(base_io);
    if (err != ATA_SUCCESS) {
        spinlock_release_raw(&ata_lock[chan]);
        return err;
    }
    outportb(base_io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    err = ata_wait_drq(base_io);
    if (err != ATA_SUCCESS) {
        spinlock_release_raw(&ata_lock[chan]);
        return err;
    }
    for (int i = 0; i < 256; i++)
        buffer[i] = inportw(base_io + ATA_REG_DATA);
    spinlock_release_raw(&ata_lock[chan]);
    return ATA_SUCCESS;
}

static ata_error_t ata_dma_transfer(uint8_t drive, uint32_t lba, uint8_t count, void *buffer, int write)
{
    uint16_t base_io     = drives[drive].base_io;
    uint8_t  chan        = (drive < 2) ? 0 : 1;
    uint16_t bm_base     = bmide_base + (chan ? 8 : 0);
    uint32_t total_bytes = (uint32_t)count * 512;

    uint64_t buf_virt = (uint64_t)buffer;
    uint64_t buf_phys;
    int using_bounce = 0;

    if (buf_virt >= KERNEL_VIRT_OFFSET)
    {
        buf_phys = buf_virt - KERNEL_VIRT_OFFSET;
    }
    else
    {
        buf_phys = virt_to_phys(get_kernel_pml4(), buf_virt);
    }

    if (!buf_phys || buf_phys + total_bytes > 0xFFFFFFFF)
    {
        using_bounce = 1;
        buf_phys = bounce_buf_phys;
        if (write)
            memcpy(bounce_buf, buffer, total_bytes);
    }

    uint32_t remaining = total_bytes;
    uint32_t poffset = 0;
    int prdt_idx = 0;
    while (remaining > 0)
    {
        uint32_t chunk = remaining > 0x8000 ? 0x8000 : remaining;
        prdt[prdt_idx].phys_addr  = (uint32_t)(buf_phys + poffset);
        prdt[prdt_idx].byte_count = (uint16_t)chunk;
        prdt[prdt_idx].flags      = 0;
        poffset   += chunk;
        remaining -= chunk;
        prdt_idx++;
    }
    prdt[prdt_idx - 1].flags = (uint16_t)(PRDT_EOT >> 16);


    

    __asm__ volatile("mfence" ::: "memory");
    

    outportl(bm_base + BMIDE_REG_PRDT, (uint32_t)prdt_phys);


    outportb(bm_base + BMIDE_REG_STATUS, BMIDE_STATUS_ERR | BMIDE_STATUS_IRQ);
    


    uint8_t dmactl = inportb(bm_base + BMIDE_REG_CMD);
    dmactl &= ~(BMIDE_CMD_WRITE | BMIDE_CMD_START);
    if (!write)
        dmactl |= BMIDE_CMD_WRITE;
    outportb(bm_base + BMIDE_REG_CMD, dmactl);

    __asm__ volatile("mfence" ::: "memory");
    irq_fired[chan] = 0;
    __asm__ volatile("mfence" ::: "memory");


    ata_setup_lba28(base_io, lba, count, drives[drive].drive_select);
    outportb(base_io + ATA_REG_COMMAND, write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA);



    dmactl = inportb(bm_base + BMIDE_REG_CMD);
    outportb(bm_base + BMIDE_REG_CMD, dmactl | BMIDE_CMD_START);

    uint32_t timeout = 0;
    __asm__ volatile("sti" ::: "memory");
    while (!irq_fired[chan])
    {
        __asm__ volatile("mfence" ::: "memory");
        if (++timeout >= ATA_TIMEOUT_MS * 100)
        {
            log("DMA[%d]: TIMEOUT lba=%u", 3, 0, chan, lba);
            outportb(bm_base + BMIDE_REG_CMD, 0);
            __asm__ volatile("cli" ::: "memory");
            return ATA_ERR_TIMEOUT;
        }
        asm volatile("pause" ::: "memory");
    }
    __asm__ volatile("cli" ::: "memory");

    outportb(bm_base + BMIDE_REG_CMD, 0);

    uint8_t final_status = inportb(bm_base + BMIDE_REG_STATUS);

    if (final_status & BMIDE_STATUS_ERR)
    {
        log("DMA[%d]: ERROR status=0x%02x lba=%u", 3, 0, chan, final_status, lba);
        return ATA_ERR_GENERAL;
    }

    if (!write && using_bounce)
        memcpy(buffer, bounce_buf, total_bytes);


    return ATA_SUCCESS;
}

ata_error_t ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buffer)
{
    if (drive >= 4 || !drives[drive].exists || !buffer || count == 0 || count > ATA_MAX_SECTORS)
        return ATA_ERR_INVALID_PARAM;

    uint8_t *buf8 = (uint8_t *)buffer;
    uint8_t hits = 0;

    spinlock_acquire_raw(&cache_lock);
    for (uint8_t i = 0; i < count; i++)
    {
        if (cache_lookup(drive, lba + i, buf8 + (uint32_t)i * 512))
            hits++;
    }
    spinlock_release_raw(&cache_lock);

    if (hits == count)
        return ATA_SUCCESS;

    uint8_t chan = (drive < 2) ? 0 : 1;
    spinlock_acquire_raw(&ata_lock[chan]);
    uint16_t base_io = drives[drive].base_io;

    ata_select_drive(base_io, drives[drive].drive_select);
    ata_error_t err = ata_wait_ready(base_io);
    if (err != ATA_SUCCESS) {
        spinlock_release_raw(&ata_lock[chan]);
        return err;
    }

    if (dma_available) {
        err = ata_dma_transfer(drive, lba, count, buffer, 0);
        spinlock_release_raw(&ata_lock[chan]);
        if (err == ATA_SUCCESS)
        {
            spinlock_acquire_raw(&cache_lock);
            for (uint8_t i = 0; i < count; i++)
                cache_insert(drive, lba + i, buf8 + (uint32_t)i * 512);
            spinlock_release_raw(&cache_lock);
        }
        return err;
    }

    uint16_t *buf = (uint16_t *)buffer;
    ata_setup_lba28(base_io, lba, count, drives[drive].drive_select);
    outportb(base_io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    for (int sector = 0; sector < count; sector++)
    {
        err = ata_wait_drq(base_io);
        if (err != ATA_SUCCESS) {
            spinlock_release_raw(&ata_lock[chan]);
            return err;
        }
        for (int word = 0; word < 256; word++)
            buf[sector * 256 + word] = inportw(base_io + ATA_REG_DATA);
    }
    spinlock_release_raw(&ata_lock[chan]);
    spinlock_acquire_raw(&cache_lock);
    for (uint8_t i = 0; i < count; i++)
        cache_insert(drive, lba + i, buf8 + (uint32_t)i * 512);
    spinlock_release_raw(&cache_lock);
    return ATA_SUCCESS;
}

ata_error_t ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buffer)
{
    if (drive >= 4 || !drives[drive].exists || !buffer || count == 0 || count > ATA_MAX_SECTORS)
        return ATA_ERR_INVALID_PARAM;

    uint8_t chan = (drive < 2) ? 0 : 1;
    spinlock_acquire_raw(&ata_lock[chan]);
    uint16_t base_io = drives[drive].base_io;

    ata_select_drive(base_io, drives[drive].drive_select);
    ata_error_t err = ata_wait_ready(base_io);
    if (err != ATA_SUCCESS) {
        spinlock_release_raw(&ata_lock[chan]);
        return err;
    }

    if (dma_available)
    {
        err = ata_dma_transfer(drive, lba, count, (void *)buffer, 1);
        spinlock_release_raw(&ata_lock[chan]);
        if (err == ATA_SUCCESS)
        {
            spinlock_acquire_raw(&cache_lock);
            for (uint8_t i = 0; i < count; i++)
                cache_invalidate(drive, lba + i);
            spinlock_release_raw(&cache_lock);
        }
        return err;
    }

    const uint16_t *buf = (const uint16_t *)buffer;
    ata_setup_lba28(base_io, lba, count, drives[drive].drive_select);
    outportb(base_io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    for (int sector = 0; sector < count; sector++)
    {
        err = ata_wait_drq(base_io);
        if (err != ATA_SUCCESS) {
            spinlock_release_raw(&ata_lock[chan]);
            return err;
        }
        for (int word = 0; word < 256; word++)
            outportw(base_io + ATA_REG_DATA, buf[sector * 256 + word]);
    }

    err = ata_wait_ready(base_io);
    if (err != ATA_SUCCESS) {
        spinlock_release_raw(&ata_lock[chan]);
        return err;
    }
    outportb(base_io + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    err = ata_wait_ready(base_io);
    spinlock_release_raw(&ata_lock[chan]);
    return err;
}

ata_error_t ata_drive_exists(int pdrv)
{
    if (pdrv >= 4 || pdrv < 0)
        return ATA_ERR_INVALID_PARAM;
    if (drives[pdrv].exists)
        return ATA_SUCCESS;
    return ATA_ERR_DRIVE_FAULT;
}
