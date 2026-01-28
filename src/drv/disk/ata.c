#include "ata.h"
#include "../../libk/ports.h"
#include "../../libk/string.h"
#include "../../libk/debug/log.h"

ata_drive_t drives[4];
static uint32_t timeout_counter;

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
    timeout_counter = 0;

    while (timeout_counter < ATA_TIMEOUT_MS)
    {
        uint8_t status = inportb(base_io + ATA_REG_STATUS);

        if (!(status & ATA_SR_BSY))
        {
            if (status & ATA_SR_ERR)
                return ATA_ERR_GENERAL;
            if (status & ATA_SR_DF)
                return ATA_ERR_DRIVE_FAULT;
            return ATA_SUCCESS;
        }

        timeout_counter++;
        ata_delay(base_io);
    }

    return ATA_ERR_TIMEOUT;
}

static ata_error_t ata_wait_drq(uint16_t base_io)
{
    timeout_counter = 0;
    asm volatile("sti");

    while (timeout_counter < ATA_TIMEOUT_MS)
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

        timeout_counter++;
        ata_delay(base_io);
    }
    asm volatile("cli");

    return ATA_ERR_TIMEOUT;
}

static void ata_select_drive(uint16_t base_io, uint8_t drive_select)
{
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
        {
            name_buffer[name_len++] = name[i + 1];
        }
        if (name[i] != 0 && name[i] != ' ')
        {
            name_buffer[name_len++] = name[i];
        }
    }

    while (name_len > 0 && name_buffer[name_len - 1] == ' ')
    {
        name_len--;
    }

    name_buffer[name_len] = '\0';
}

ata_device_type_t ata_detect_drive(uint8_t drive)
{
    if (drive >= 4)
        return ATA_DEVICE_UNKNOWN;

    uint16_t base_io = (drive < 2) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
    uint8_t drive_select = drive & 1;

    ata_select_drive(base_io, drive_select);

    if (ata_wait_ready(base_io) != ATA_SUCCESS)
    {
        return ATA_DEVICE_UNKNOWN;
    }

    uint8_t cl = inportb(base_io + ATA_REG_LBA1);
    uint8_t ch = inportb(base_io + ATA_REG_LBA2);

    if (cl == 0x14 && ch == 0xEB)
        return ATA_DEVICE_PATAPI;
    if (cl == 0x69 && ch == 0x96)
        return ATA_DEVICE_SATAPI;
    if (cl == 0x00 && ch == 0x00)
        return ATA_DEVICE_PATA;
    if (cl == 0x3C && ch == 0xC3)
        return ATA_DEVICE_SATA;

    return ATA_DEVICE_UNKNOWN;
}

ata_error_t ata_init(void)
{
    int found_drives = 0;
    for (int i = 0; i < 4; i++)
    {
        drives[i].base_io = (i < 2) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
        drives[i].ctrl_io = (i < 2) ? ATA_PRIMARY_DEVCTL : ATA_SECONDARY_DEVCTL;
        drives[i].drive_select = i & 1;
        drives[i].exists = 0;

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
            if (strstr(drive_name, "QEMU") != NULL)
                log("ZenOS running in a VM (QEMU)", 4, 0);
            if (strstr(drive_name, "VBOX") != NULL)
                log("ZenOS running in a VM (VirtualBox)", 4, 0);
        }
    }
    log("Initialization complete. Found %i drive%s.", 4, 0, found_drives, found_drives == 1 ? "" : "s");
    return ATA_SUCCESS;
}

ata_error_t ata_identify_drive(uint8_t drive, uint16_t *buffer)
{
    if (drive >= 4 || !drives[drive].exists || !buffer)
    {
        return ATA_ERR_INVALID_PARAM;
    }
    uint16_t base_io = drives[drive].base_io;
    ata_select_drive(base_io, drives[drive].drive_select);
    ata_error_t err = ata_wait_ready(base_io);
    if (err != ATA_SUCCESS)
        return err;
    outportb(base_io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    err = ata_wait_drq(base_io);
    if (err != ATA_SUCCESS)
        return err;
    for (int i = 0; i < 256; i++)
    {
        buffer[i] = inportw(base_io + ATA_REG_DATA);
    }
    return ATA_SUCCESS;
}

ata_error_t ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buffer)
{
    if (drive >= 4 || !drives[drive].exists || !buffer || count == 0 || count > ATA_MAX_SECTORS)
    {
        return ATA_ERR_INVALID_PARAM;
    }

    uint16_t base_io = drives[drive].base_io;
    uint16_t *buf = (uint16_t *)buffer;

    ata_select_drive(base_io, drives[drive].drive_select);

    ata_error_t err = ata_wait_ready(base_io);
    if (err != ATA_SUCCESS)
        return err;

    ata_setup_lba28(base_io, lba, count, drives[drive].drive_select);

    outportb(base_io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    for (int sector = 0; sector < count; sector++)
    {
        err = ata_wait_drq(base_io);
        if (err != ATA_SUCCESS)
            return err;

        for (int word = 0; word < 256; word++)
        {
            buf[sector * 256 + word] = inportw(base_io + ATA_REG_DATA);
        }
    }

    return ATA_SUCCESS;
}

ata_error_t ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buffer)
{
    if (drive >= 4 || !drives[drive].exists || !buffer || count == 0 || count > ATA_MAX_SECTORS)
    {
        return ATA_ERR_INVALID_PARAM;
    }

    uint16_t base_io = drives[drive].base_io;
    const uint16_t *buf = (const uint16_t *)buffer;

    ata_select_drive(base_io, drives[drive].drive_select);

    ata_error_t err = ata_wait_ready(base_io);
    if (err != ATA_SUCCESS)
        return err;

    ata_setup_lba28(base_io, lba, count, drives[drive].drive_select);

    outportb(base_io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    for (int sector = 0; sector < count; sector++)
    {
        err = ata_wait_drq(base_io);
        if (err != ATA_SUCCESS)
            return err;

        for (int word = 0; word < 256; word++)
        {
            outportw(base_io + ATA_REG_DATA, buf[sector * 256 + word]);
        }
    }

    err = ata_wait_ready(base_io);
    if (err != ATA_SUCCESS)
        return err;

    outportb(base_io + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);

    return ata_wait_ready(base_io);
}

ata_error_t ata_drive_exists(int pdrv)
{
    if (pdrv > 4 || pdrv < 0)
        return ATA_ERR_INVALID_PARAM;
    if (drives[pdrv].exists)
        return ATA_SUCCESS;
    return ATA_ERR_DRIVE_FAULT;
}