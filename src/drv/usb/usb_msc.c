#include "usb_msc.h"
#include "../../libk/string.h"

#define USB_MSC_MAX_DISKS 8

typedef struct {
    int used;
    uint64_t sector_count;
    uint32_t sector_size;
    void *ctx;
    usb_msc_read_fn read;
    usb_msc_write_fn write;
} usb_msc_disk_t;

static usb_msc_disk_t disks[USB_MSC_MAX_DISKS];

int usb_msc_register(uint64_t sector_count, uint32_t sector_size, void *ctx, usb_msc_read_fn read, usb_msc_write_fn write)
{
    if (!sector_count || !sector_size || !read)
        return -1;
    for (int i = 0; i < USB_MSC_MAX_DISKS; i++) {
        if (disks[i].used)
            continue;
        memset(&disks[i], 0, sizeof(disks[i]));
        disks[i].used = 1;
        disks[i].sector_count = sector_count;
        disks[i].sector_size = sector_size;
        disks[i].ctx = ctx;
        disks[i].read = read;
        disks[i].write = write;
        return i;
    }
    return -1;
}

int usb_msc_count(void)
{
    int count = 0;
    for (int i = 0; i < USB_MSC_MAX_DISKS; i++)
        if (disks[i].used)
            count++;
    return count;
}

int usb_msc_read(int disk, uint32_t lba, uint32_t count, void *buf)
{
    if (disk < 0 || disk >= USB_MSC_MAX_DISKS || !disks[disk].used || !disks[disk].read)
        return -1;
    return disks[disk].read(disks[disk].ctx, lba, count, buf);
}

int usb_msc_write(int disk, uint32_t lba, uint32_t count, const void *buf)
{
    if (disk < 0 || disk >= USB_MSC_MAX_DISKS || !disks[disk].used || !disks[disk].write)
        return -1;
    return disks[disk].write(disks[disk].ctx, lba, count, buf);
}

int usb_msc_geometry(int disk, uint64_t *sector_count, uint32_t *sector_size)
{
    if (disk < 0 || disk >= USB_MSC_MAX_DISKS || !disks[disk].used)
        return -1;
    if (sector_count)
        *sector_count = disks[disk].sector_count;
    if (sector_size)
        *sector_size = disks[disk].sector_size;
    return 0;
}
