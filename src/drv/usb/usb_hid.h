#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>

void usb_hid_keyboard_report(const uint8_t *report, uint32_t len);
void usb_hid_mouse_report(const uint8_t *report, uint32_t len);

#endif
