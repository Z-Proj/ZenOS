#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>
#include "../../cpu/isr.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_REVISION_ID    0x08
#define PCI_PROG_IF        0x09
#define PCI_SUBCLASS       0x0A
#define PCI_CLASS_CODE     0x0B
#define PCI_CACHE_LINE     0x0C
#define PCI_LATENCY_TIMER  0x0D
#define PCI_HEADER_TYPE    0x0E
#define PCI_BIST           0x0F
#define PCI_BAR0           0x10
#define PCI_BAR1           0x14
#define PCI_BAR2           0x18
#define PCI_BAR3           0x1C
#define PCI_BAR4           0x20
#define PCI_BAR5           0x24
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_INTERRUPT_PIN  0x3D

#define PCI_CLASS_STORAGE     0x01
#define PCI_CLASS_NETWORK     0x02
#define PCI_CLASS_DISPLAY     0x03
#define PCI_CLASS_MULTIMEDIA  0x04
#define PCI_CLASS_MEMORY      0x05
#define PCI_CLASS_BRIDGE      0x06
#define PCI_CLASS_COMM        0x07
#define PCI_CLASS_PERIPHERAL  0x08
#define PCI_CLASS_INPUT       0x09
#define PCI_CLASS_DOCKING     0x0A
#define PCI_CLASS_PROCESSOR   0x0B
#define PCI_CLASS_SERIAL      0x0C

#define PCI_SUBCLASS_IDE      0x01
#define PCI_SUBCLASS_SATA     0x06
#define PCI_SUBCLASS_ETHERNET 0x00
#define PCI_SUBCLASS_VGA      0x00
#define PCI_SUBCLASS_USB      0x03
#define PCI_SUBCLASS_PCI      0x04

#define PCI_CAP_MSI           0x05
#define PCI_CAP_MSIX          0x11

#define PCI_CMD_IO_SPACE      0x01
#define PCI_CMD_MEM_SPACE     0x02
#define PCI_CMD_BUS_MASTER    0x04
#define PCI_CMD_INTERRUPT_DISABLE 0x400

typedef struct pci_device {
    uint8_t bus, slot, func;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if;
    uint32_t bars[6];
    uint8_t interrupt_line, interrupt_pin;
    uint16_t status, command;
    bool msi_capable, msix_capable;
    uint8_t msi_offset, msix_offset;
    struct pci_device *next;
} pci_device_t;

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

uint32_t pci_get_bar_size(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_num);
bool pci_is_64bit_bar(uint32_t bar);
uint8_t pci_find_capability(uint8_t bus, uint8_t slot, uint8_t func, uint8_t cap_id);

void pci_enable_msi(pci_device_t *dev, uint8_t vector, isr_handler_t handler, const char* handler_name);
void pci_enable_msix(pci_device_t *dev, uint8_t vector, isr_handler_t handler, const char* handler_name);
void pci_enable_bus_mastering(pci_device_t *dev);
void pci_enable_memory_space(pci_device_t *dev);
void pci_enable_io_space(pci_device_t *dev);

pci_device_t* pci_create_device(uint8_t bus, uint8_t slot, uint8_t func);
void pci_add_device(pci_device_t *dev);
bool pci_is_multifunction(uint8_t bus, uint8_t slot);
void pci_scan_bus(uint8_t bus);
void pci_init(void);

pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t* pci_find_device_by_class(uint8_t class_code, uint8_t subclass);
void pci_dump_devices(void);
void pci_setup_device(pci_device_t *dev);
void pci_initialize_system(void);

#endif