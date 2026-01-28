#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>

#define ATA_PRIMARY_IO          0x1F0
#define ATA_SECONDARY_IO        0x170
#define ATA_PRIMARY_DEVCTL      0x3F6
#define ATA_SECONDARY_DEVCTL    0x376

#define ATA_SR_BSY              0x80
#define ATA_SR_DRDY             0x40
#define ATA_SR_DF               0x20
#define ATA_SR_DSC              0x10
#define ATA_SR_DRQ              0x08
#define ATA_SR_CORR             0x04
#define ATA_SR_IDX              0x02
#define ATA_SR_ERR              0x01

#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC

#define ATA_REG_DATA            0x00
#define ATA_REG_ERROR           0x01
#define ATA_REG_FEATURES        0x01
#define ATA_REG_SECCOUNT0       0x02
#define ATA_REG_LBA0            0x03
#define ATA_REG_LBA1            0x04
#define ATA_REG_LBA2            0x05
#define ATA_REG_HDDEVSEL        0x06
#define ATA_REG_COMMAND         0x07
#define ATA_REG_STATUS          0x07

#define ATA_SECTOR_SIZE         512
#define ATA_MAX_SECTORS         254 //not 256 as PIO is 0 - 255
#define ATA_TIMEOUT_MS          101010

typedef enum {
    ATA_SUCCESS = 0,
    ATA_ERR_TIMEOUT,
    ATA_ERR_DRIVE_FAULT,
    ATA_ERR_GENERAL,
    ATA_ERR_NO_DRQ,
    ATA_ERR_INVALID_PARAM
} ata_error_t;

typedef enum {
    ATA_DEVICE_UNKNOWN = -1,
    ATA_DEVICE_PATAPI = 0,
    ATA_DEVICE_SATAPI = 1,
    ATA_DEVICE_PATA = 2,
    ATA_DEVICE_SATA = 3
} ata_device_type_t;

typedef struct {
    uint16_t base_io;
    uint16_t ctrl_io;
    uint8_t drive_select;
    ata_device_type_t type;
    uint8_t exists;
} ata_drive_t;

ata_error_t ata_init(void);
ata_error_t ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer);
ata_error_t ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void* buffer);
ata_device_type_t ata_detect_drive(uint8_t drive);
ata_error_t ata_identify_drive(uint8_t drive, uint16_t* buffer);
ata_error_t ata_drive_exists(int pdrv);

#endif