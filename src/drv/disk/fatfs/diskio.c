#include "ff.h"
#include "diskio.h"
#include "../ata.h"

DSTATUS disk_initialize(BYTE pdrv) {
    if (ata_drive_exists(pdrv) != ATA_SUCCESS)
        return STA_NODISK;
    return 0;
}

DSTATUS disk_status(BYTE pdrv) {
    if (ata_drive_exists(pdrv) != ATA_SUCCESS)
        return STA_NODISK;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    /* ATA driver max 254 sectors per call, loop if needed */
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
    (void)pdrv;
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = 1048576;
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
