#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>
#include "../net/pci.h"

int xhci_init_controller(pci_device_t *pci);
void xhci_poll(void);
int xhci_controller_count(void);

#endif
