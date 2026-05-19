#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stddef.h>

void usb_init(void);
void usb_poll(void);
int usb_controller_count(void);
int usb_storage_count(void);

#endif
