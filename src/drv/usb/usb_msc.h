#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdint.h>

typedef int (*usb_msc_read_fn)(void *ctx, uint32_t lba, uint32_t count, void *buf);
typedef int (*usb_msc_write_fn)(void *ctx, uint32_t lba, uint32_t count, const void *buf);

int usb_msc_register(uint64_t sector_count, uint32_t sector_size, void *ctx, usb_msc_read_fn read, usb_msc_write_fn write);
int usb_msc_count(void);
int usb_msc_read(int disk, uint32_t lba, uint32_t count, void *buf);
int usb_msc_write(int disk, uint32_t lba, uint32_t count, const void *buf);
int usb_msc_geometry(int disk, uint64_t *sector_count, uint32_t *sector_size);

#endif
