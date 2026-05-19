#include "usb.h"
#include "xhci.h"
#include "usb_msc.h"
#include "../net/pci.h"
#include "../../libk/debug/log.h"

static int controllers;

void usb_init(void)
{
    controllers = 0;
    for (pci_device_t *dev = pci_first_device(); dev; dev = pci_next_device(dev)) {
        if (dev->class_code != PCI_CLASS_SERIAL || dev->subclass != PCI_SUBCLASS_USB)
            continue;
        if (dev->prog_if == 0x30) {
            if (xhci_init_controller(dev) == 0)
                controllers++;
        } else {
            log("USB: unsupported HCI %02x:%02x.%x prog_if=%x", 2, 0,
                dev->bus, dev->slot, dev->func, dev->prog_if);
        }
    }
    log("USB: %d controller(s), %d storage disk(s)", controllers ? 4 : 2, 0,
        controllers, usb_msc_count());
}

void usb_poll(void)
{
    xhci_poll();
}

int usb_controller_count(void)
{
    return controllers;
}

int usb_storage_count(void)
{
    return usb_msc_count();
}
