#include "ff.h"
#include "diskio.h"
#include "../ata.h"

ata_error_t ata_cache_flush(uint8_t drive);

static LBA_t get_sector_count(BYTE pdrv) {
    uint16_t id[256];
    if (ata_identify_drive(pdrv, id) != ATA_SUCCESS)
        return 1048576;
    uint32_t lba28 = ((uint32_t)id[61] << 16) | id[60];
    if (lba28 > 1)
        return (LBA_t)(lba28 - 1);
    return 1048576;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (ata_drive_exists(pdrv) != ATA_SUCCESS) return STA_NODISK;
    return 0;
}

DSTATUS disk_status(BYTE pdrv) {
    if (ata_drive_exists(pdrv) != ATA_SUCCESS) return STA_NODISK;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    while (count > 0) {
        UINT n = count > 254 ? 254 : count;
        if (ata_read_sectors(pdrv, sector, (uint8_t)n, buff) != ATA_SUCCESS)
            return RES_ERROR;
        buff   += n * 512;
        sector += n;
        count  -= n;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    while (count > 0) {
        UINT n = count > 254 ? 254 : count;
        if (ata_write_sectors(pdrv, sector, (uint8_t)n, buff) != ATA_SUCCESS)
            return RES_ERROR;
        buff   += n * 512;
        sector += n;
        count  -= n;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = get_sector_count(pdrv);
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}
