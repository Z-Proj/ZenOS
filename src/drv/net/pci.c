#include "pci.h"
#include "../../libk/core/mem.h"
#include "../../libk/debug/log.h"
#include "../../libk/ports.h"
#include "../local_apic.h"

static pci_device_t *device_list = NULL;
static uint32_t device_count = 0;

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outportl(PCI_CONFIG_ADDRESS, address);
    return inportl(PCI_CONFIG_DATA);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outportl(PCI_CONFIG_ADDRESS, address);
    outportl(PCI_CONFIG_DATA, value);
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t data = pci_read(bus, slot, func, offset & 0xFC);
    return (data >> ((offset & 3) * 8)) & 0xFF;
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t data = pci_read(bus, slot, func, offset & 0xFC);
    return (data >> ((offset & 2) * 8)) & 0xFFFF;
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t data = pci_read(bus, slot, func, offset & 0xFC);
    uint8_t shift = (offset & 2) * 8;
    data = (data & ~(0xFFFF << shift)) | (value << shift);
    pci_write(bus, slot, func, offset & 0xFC, data);
}

uint32_t pci_get_bar_size(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_num) {
    uint8_t offset = 0x10 + 4 * bar_num;
    uint32_t original = pci_read(bus, slot, func, offset);
    pci_write(bus, slot, func, offset, 0xFFFFFFFF);
    uint32_t size_mask = pci_read(bus, slot, func, offset);
    pci_write(bus, slot, func, offset, original);
    
    if (size_mask == 0) return 0;
    
    if (original & 1) {
        return (~(size_mask & 0xFFFFFFFC)) + 1;
    } else {
        return (~size_mask) + 1;
    }
}

bool pci_is_64bit_bar(uint32_t bar) {
    return !(bar & 1) && ((bar >> 1) & 3) == 2;
}

uint8_t pci_find_capability(uint8_t bus, uint8_t slot, uint8_t func, uint8_t cap_id) {
    if (!(pci_read16(bus, slot, func, 0x06) & 0x10)) return 0;
    
    uint8_t cap_ptr = pci_read8(bus, slot, func, 0x34) & 0xFC;
    
    while (cap_ptr) {
        uint8_t id = pci_read8(bus, slot, func, cap_ptr);
        if (id == cap_id) return cap_ptr;
        cap_ptr = pci_read8(bus, slot, func, cap_ptr + 1) & 0xFC;
    }
    return 0;
}

void pci_enable_msi(pci_device_t *dev, uint8_t vector, isr_handler_t handler, const char* handler_name) {
    if (!dev->msi_capable) return;
    
    register_interrupt_handler(vector, handler, handler_name);
    
    uint16_t control = pci_read16(dev->bus, dev->slot, dev->func, dev->msi_offset + 2);
    // uint8_t multi_msg = (control >> 1) & 7;
    
    uint32_t msi_addr = 0xFEE00000 | (LocalApicGetId() << 12);
    uint32_t msi_data = vector;
    
    pci_write(dev->bus, dev->slot, dev->func, dev->msi_offset + 4, msi_addr);
    pci_write16(dev->bus, dev->slot, dev->func, dev->msi_offset + 8, msi_data);
    
    if (control & 0x80) {
        pci_write(dev->bus, dev->slot, dev->func, dev->msi_offset + 8, 0);
        pci_write16(dev->bus, dev->slot, dev->func, dev->msi_offset + 12, msi_data);
    }
    
    control = (control & ~(7 << 4)) | (1 << 4) | 1;
    pci_write16(dev->bus, dev->slot, dev->func, dev->msi_offset + 2, control);
}

void pci_enable_msix(pci_device_t *dev, uint8_t vector, isr_handler_t handler, const char* handler_name) {
    if (!dev->msix_capable) return;
    
    register_interrupt_handler(vector, handler, handler_name);
    
    uint16_t control = pci_read16(dev->bus, dev->slot, dev->func, dev->msix_offset + 2);
    // uint16_t table_size = (control & 0x7FF) + 1;
    
    uint32_t table_offset = pci_read(dev->bus, dev->slot, dev->func, dev->msix_offset + 4);
    uint8_t table_bar = table_offset & 7;
    table_offset &= ~7;
    
    uint64_t table_addr = (dev->bars[table_bar] & ~0xF) + table_offset;
    uint32_t* msix_table = (uint32_t*)table_addr;
    
    msix_table[0] = 0xFEE00000 | (LocalApicGetId() << 12);
    msix_table[1] = 0;
    msix_table[2] = vector;
    msix_table[3] = 0;
    
    control |= 0x8000;
    pci_write16(dev->bus, dev->slot, dev->func, dev->msix_offset + 2, control);
}

void pci_enable_bus_mastering(pci_device_t *dev) {
    uint16_t command = pci_read16(dev->bus, dev->slot, dev->func, 0x04);
    command |= 0x04;
    pci_write16(dev->bus, dev->slot, dev->func, 0x04, command);
    dev->command = command;
}

void pci_enable_memory_space(pci_device_t *dev) {
    uint16_t command = pci_read16(dev->bus, dev->slot, dev->func, 0x04);
    command |= 0x02;
    pci_write16(dev->bus, dev->slot, dev->func, 0x04, command);
    dev->command = command;
}

void pci_enable_io_space(pci_device_t *dev) {
    uint16_t command = pci_read16(dev->bus, dev->slot, dev->func, 0x04);
    command |= 0x01;
    pci_write16(dev->bus, dev->slot, dev->func, 0x04, command);
    dev->command = command;
}

pci_device_t* pci_create_device(uint8_t bus, uint8_t slot, uint8_t func) {
    pci_device_t *dev = kmalloc(sizeof(pci_device_t));
    if (!dev) return NULL;
    
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = pci_read16(bus, slot, func, 0x00);
    dev->device_id = pci_read16(bus, slot, func, 0x02);
    dev->class_code = pci_read8(bus, slot, func, 0x0B);
    dev->subclass = pci_read8(bus, slot, func, 0x0A);
    dev->prog_if = pci_read8(bus, slot, func, 0x09);
    dev->interrupt_line = pci_read8(bus, slot, func, 0x3C);
    dev->interrupt_pin = pci_read8(bus, slot, func, 0x3D);
    dev->status = pci_read16(bus, slot, func, 0x06);
    dev->command = pci_read16(bus, slot, func, 0x04);
    
    for (int i = 0; i < 6; i++) {
        dev->bars[i] = pci_read(bus, slot, func, 0x10 + 4 * i);
    }
    
    dev->msi_offset = pci_find_capability(bus, slot, func, 0x05);
    dev->msi_capable = dev->msi_offset != 0;
    dev->msix_offset = pci_find_capability(bus, slot, func, 0x11);
    dev->msix_capable = dev->msix_offset != 0;
    
    dev->next = NULL;
    return dev;
}

void pci_add_device(pci_device_t *dev) {
    if (!device_list) {
        device_list = dev;
    } else {
        pci_device_t *current = device_list;
        while (current->next) current = current->next;
        current->next = dev;
    }
    device_count++;
}

bool pci_is_multifunction(uint8_t bus, uint8_t slot) {
    return pci_read8(bus, slot, 0, 0x0E) & 0x80;
}

void pci_scan_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        if (pci_read16(bus, slot, 0, 0x00) == 0xFFFF) continue;
        
        pci_device_t *dev = pci_create_device(bus, slot, 0);
        if (dev) {
            pci_add_device(dev);
            
            if (dev->class_code == 0x06 && dev->subclass == 0x04) {
                uint8_t secondary_bus = pci_read8(bus, slot, 0, 0x19);
                if (secondary_bus != 0) pci_scan_bus(secondary_bus);
            }
        }
        
        if (pci_is_multifunction(bus, slot)) {
            for (uint8_t func = 1; func < 8; func++) {
                if (pci_read16(bus, slot, func, 0x00) == 0xFFFF) continue;
                
                pci_device_t *func_dev = pci_create_device(bus, slot, func);
                if (func_dev) pci_add_device(func_dev);
            }
        }
    }
}

void pci_init(void) {
    if ((pci_read8(0, 0, 0, 0x0E) & 0x80) == 0) {
        pci_scan_bus(0);
    } else {
        for (uint8_t func = 0; func < 8; func++) {
            if (pci_read16(0, 0, func, 0x00) == 0xFFFF) break;
            uint8_t bus = func;
            pci_scan_bus(bus);
        }
    }
}

pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    pci_device_t *current = device_list;
    while (current) {
        if (current->vendor_id == vendor_id && current->device_id == device_id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

pci_device_t* pci_find_device_by_class(uint8_t class_code, uint8_t subclass) {
    pci_device_t *current = device_list;
    while (current) {
        if (current->class_code == class_code && current->subclass == subclass) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void pci_dump_devices(void) {
    pci_device_t *current = device_list;
    while (current) {
        log("%02x:%02x.%x %04x:%04x class=%02x:%02x", 1, 0,
            current->bus, current->slot, current->func,
            current->vendor_id, current->device_id,
            current->class_code, current->subclass);
        current = current->next;
    }
    log("Total devices: %d", 1, 0, device_count);
}

void pci_setup_device(pci_device_t *dev) {
    switch(dev->class_code) {
        case 0x01:
            if (dev->subclass == 0x01 || dev->subclass == 0x06) {
                pci_enable_bus_mastering(dev);
                pci_enable_memory_space(dev);
            }
            break;
            
        case 0x02:
            if (dev->subclass == 0x00) {
                pci_enable_bus_mastering(dev);
                pci_enable_memory_space(dev);
            }
            break;
            
        case 0x03:
            if (dev->subclass == 0x00) {
                pci_enable_memory_space(dev);
            }
            break;
            
        case 0x0C:
            if (dev->subclass == 0x03) {
                pci_enable_memory_space(dev);
                pci_enable_bus_mastering(dev);
            }
            break;
    }
}

void pci_initialize_system(void) {
    pci_init();
    
    pci_device_t *current = device_list;
    while (current) {
        pci_setup_device(current);
        current = current->next;
    }
    
    log("PCI: %d devices", 1, 0, device_count);
}