#include "e1000.h"
#include "pci.h"
#include "../local_apic.h"

static e1000_device dev;
static pci_device_t *pci_dev = NULL;
static void *rx_buf_virt[NUM_RX_DESC];
static void *tx_buf_virt[NUM_TX_DESC];

static uint32_t e1000_read(uint32_t reg)
{
    return *((volatile uint32_t *)(dev.mem_base + reg));
}

static void e1000_write(uint32_t reg, uint32_t value)
{
    *((volatile uint32_t *)(dev.mem_base + reg)) = value;
}

static uint32_t e1000_eeprom_read(uint8_t addr)
{
    uint32_t data = 0;
    uint32_t tmp = 0;

    if (dev.has_eeprom)
    {
        e1000_write(E1000_REG_EEPROM, 1 | (addr << 8));
        while (!((tmp = e1000_read(E1000_REG_EEPROM)) & (1 << 4)))
            ;
    }
    else
    {
        e1000_write(E1000_REG_EEPROM, 1 | (addr << 2));
        while (!((tmp = e1000_read(E1000_REG_EEPROM)) & (1 << 1)))
            ;
    }

    data = (tmp >> 16) & 0xFFFF;
    return data;
}

static uint8_t e1000_detect_eeprom(void)
{
    e1000_write(E1000_REG_EEPROM, 1);
    for (int i = 0; i < 10000; i++)
    {
        uint32_t val = e1000_read(E1000_REG_EEPROM);
        if (val & (1 << 4))
            return 1;
    }
    return 0;
}

static void e1000_reset(void)
{
    uint32_t ctrl = e1000_read(E1000_REG_CTRL);
    e1000_write(E1000_REG_CTRL, ctrl | 0x04000000);
    while (e1000_read(E1000_REG_CTRL) & 0x04000000)
        ;
    for (volatile int i = 0; i < 100; i++)
        ;
}

static void e1000_read_mac(void)
{
    if (dev.has_eeprom)
    {
        uint32_t temp = e1000_eeprom_read(0);
        dev.mac[0] = temp & 0xFF;
        dev.mac[1] = temp >> 8;
        temp = e1000_eeprom_read(1);
        dev.mac[2] = temp & 0xFF;
        dev.mac[3] = temp >> 8;
        temp = e1000_eeprom_read(2);
        dev.mac[4] = temp & 0xFF;
        dev.mac[5] = temp >> 8;
    }
    else
    {
        uint32_t mac_low = e1000_read(E1000_REG_RXADDR);
        uint32_t mac_high = e1000_read(E1000_REG_RXADDR2);
        dev.mac[0] = mac_low & 0xFF;
        dev.mac[1] = (mac_low >> 8) & 0xFF;
        dev.mac[2] = (mac_low >> 16) & 0xFF;
        dev.mac[3] = (mac_low >> 24) & 0xFF;
        dev.mac[4] = mac_high & 0xFF;
        dev.mac[5] = (mac_high >> 8) & 0xFF;
    }


}

static void e1000_init_rx(void)
{

    uint64_t ring_phys = alloc_page();
    dev.rx_ring = (e1000_rx_desc *)(ring_phys + KERNEL_VIRT_OFFSET);
    memset(dev.rx_ring, 0, NUM_RX_DESC * sizeof(e1000_rx_desc));

    uint64_t buffers_phys = alloc_pages(NUM_RX_DESC);
    for (int i = 0; i < NUM_RX_DESC; i++)
    {
        uint64_t buf_phys = buffers_phys + (i * PAGE_SIZE);
        void *va = (void *)(buf_phys + KERNEL_VIRT_OFFSET);
        rx_buf_virt[i] = va;
        dev.rx_ring[i].addr = buf_phys;
        dev.rx_ring[i].status = 0;
        dev.rx_ring[i].length = 0;
        dev.rx_ring[i].errors = 0;
    }

    e1000_write(E1000_REG_RDBAL, (uint32_t)(ring_phys & 0xFFFFFFFF));
    e1000_write(E1000_REG_RDBAH, (uint32_t)(ring_phys >> 32));
    e1000_write(E1000_REG_RDLEN, NUM_RX_DESC * sizeof(e1000_rx_desc));

    e1000_write(E1000_REG_RDH, 0);
    e1000_write(E1000_REG_RDT, NUM_RX_DESC - 1);
    dev.rx_cur = 0;

    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE |
                    E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_SECRC |
                    E1000_RCTL_RDMTS_HALF | (2 << 16) | (0 << 20);
    e1000_write(E1000_REG_RCTL, rctl);
}

void e1000_init_tx(void)
{

    uint64_t ring_phys = alloc_page();
    dev.tx_ring = (e1000_tx_desc *)(ring_phys + KERNEL_VIRT_OFFSET);
    memset(dev.tx_ring, 0, NUM_TX_DESC * sizeof(e1000_tx_desc));

    uint64_t buffers_phys = alloc_pages(NUM_TX_DESC);
    for (int i = 0; i < NUM_TX_DESC; i++)
    {
        uint64_t buf_phys = buffers_phys + (i * PAGE_SIZE);
        void *va = (void *)(buf_phys + KERNEL_VIRT_OFFSET);
        tx_buf_virt[i] = va;
        dev.tx_ring[i].addr = buf_phys;
        dev.tx_ring[i].status = 1;
        dev.tx_ring[i].cmd = 0;
        dev.tx_ring[i].length = 0;
    }

    e1000_write(E1000_REG_TDBAL, (uint32_t)(ring_phys & 0xFFFFFFFF));
    e1000_write(E1000_REG_TDBAH, (uint32_t)(ring_phys >> 32));
    e1000_write(E1000_REG_TDLEN, NUM_TX_DESC * sizeof(e1000_tx_desc));
    e1000_write(E1000_REG_TDH, 0);
    e1000_write(E1000_REG_TDT, 0);

    dev.tx_cur = 0;

    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP |
                    (0x10 << E1000_TCTL_CT) | (0x40 << E1000_TCTL_COLD);
    e1000_write(E1000_REG_TCTL, tctl);
    e1000_write(E1000_REG_TIPG, 0x0060200A);
}

static void e1000_interrupt_handler_legacy(registers_t *regs)
{
    (void)regs;
    e1000_handle_interrupt();
}

static void e1000_interrupt_handler_msi(registers_t *regs)
{
    (void)regs;
    e1000_handle_interrupt();
    LocalApicSendEOI();
}

void e1000_init(void)
{
    memset(&dev, 0, sizeof(e1000_device));

    pci_dev = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (!pci_dev)
        pci_dev = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID_2);
    if (!pci_dev)
        pci_dev = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID_3);
    if (!pci_dev)
    {
        pci_dev = pci_find_device_by_class(PCI_CLASS_NETWORK, PCI_SUBCLASS_ETHERNET);
        if (pci_dev && pci_dev->vendor_id != E1000_VENDOR_ID)
        {
            log("Non-Intel NIC found: %04x:%04x", 2, 1,
                pci_dev->vendor_id, pci_dev->device_id);
            pci_dev = NULL;
        }
    }

    if (!pci_dev)
    {
        log("No E1000 NIC found", 3, 1);
        return;
    }

    dev.bus = pci_dev->bus;
    dev.slot = pci_dev->slot;
    dev.func = pci_dev->func;
    dev.device_id = pci_dev->device_id;
    dev.bar0 = pci_dev->bars[0];
    dev.mem_base = dev.bar0 & ~0xF;
    dev.irq = pci_dev->interrupt_line;

    pci_enable_bus_mastering(pci_dev);
    pci_enable_memory_space(pci_dev);

    e1000_reset();
    e1000_write(E1000_REG_IMASK, 0);

    dev.has_eeprom = e1000_detect_eeprom();
    e1000_read_mac();
    e1000_init_rx();
    e1000_init_tx();

    e1000_write(E1000_REG_IMASK, 0x1F6DC);
    e1000_read(0xC0);

    if (pci_dev->msi_capable)
    {
        pci_enable_msi(pci_dev, 50, e1000_interrupt_handler_msi, "E1000 MSI");
    }
    else
    {
        register_interrupt_handler(32 + dev.irq, e1000_interrupt_handler_legacy, "E1000 Legacy");
    }

    log("E1000: %02x:%02x:%02x:%02x:%02x:%02x @ %02x:%02x.%x IRQ %d", 1, 0,
        dev.mac[0], dev.mac[1], dev.mac[2], dev.mac[3], dev.mac[4], dev.mac[5],
        dev.bus, dev.slot, dev.func, dev.irq);
}

int e1000_send_packet(void *data, size_t len)
{
    if (len > TX_BUFFER_SIZE)
        return -1;
    if (!pci_dev)
        return -1;

    if (!e1000_link_up())
    {
        log("Link down, cannot send.", 2, 1);
        return -1;
    }

    uint16_t tail = dev.tx_cur;

    int wait_tries = 100000;
    while (!(dev.tx_ring[tail].status & 0x1) && --wait_tries > 0)
    {
        for (volatile int i = 0; i < 100; i++)
            ;
    }

    if (wait_tries == 0)
    {
        log("TX ring full. Descriptor %d, Status: %02x", 2, 1, tail, dev.tx_ring[tail].status);
        return -1;
    }

    memcpy(tx_buf_virt[tail], data, len);

    dev.tx_ring[tail].length = len;
    dev.tx_ring[tail].cmd = E1000_CMD_EOP | E1000_CMD_IFCS | E1000_CMD_RS;
    dev.tx_ring[tail].status = 0;

    dev.tx_cur = (tail + 1) % NUM_TX_DESC;
    e1000_write(E1000_REG_TDT, dev.tx_cur);

    int tries = 1000000;
    while (!(dev.tx_ring[tail].status & 0x1) && --tries > 0)
    {
        for (volatile int i = 0; i < 10; i++)
            ;
    }

    if (tries == 0)
    {
        uint32_t tdh = e1000_read(E1000_REG_TDH);
        uint32_t tdt = e1000_read(E1000_REG_TDT);
        uint32_t tctl = e1000_read(E1000_REG_TCTL);
        log("TX Timeout. Desc %d, Status: %02x, TDH: %d, TDT: %d, TCTL: %08x", 2, 1,
            tail, dev.tx_ring[tail].status, tdh, tdt, tctl);
        return -1;
    }

    return len;
}

int e1000_receive_packet(void *buf, size_t buf_size)
{

    uint16_t idx = dev.rx_cur;
    e1000_rx_desc *desc = &dev.rx_ring[idx];

    if (!(desc->status & 0x01))
        return -1;

    size_t len = desc->length;
    if (len > buf_size)
        len = buf_size;

    memcpy(buf, rx_buf_virt[idx], len);

    uint64_t new_buf = alloc_page();
    void *va = (void *)(new_buf + KERNEL_VIRT_OFFSET);
    rx_buf_virt[idx] = va;
    desc->addr = new_buf;
    desc->status = 0;
    desc->length = 0;
    desc->errors = 0;

    dev.rx_cur = (idx + 1) % NUM_RX_DESC;

    e1000_write(E1000_REG_RDT, idx);

    return (int)len;
}

void e1000_get_mac_address(uint8_t *mac)
{
    for (int i = 0; i < 6; i++)
        mac[i] = dev.mac[i];
}

uint32_t e1000_link_up(void)
{
    if (!pci_dev)
        return 0;
    uint32_t status = e1000_read(E1000_REG_STATUS);
    return (status & 2) ? 1 : 0;
}

void e1000_enable_interrupts(void)
{
    if (!pci_dev)
        return;
    e1000_write(E1000_REG_IMASK, 0x1F6DC);
}

void e1000_disable_interrupts(void)
{
    if (!pci_dev)
        return;
    e1000_write(E1000_REG_IMASK, 0);
}

uint32_t e1000_get_interrupt_status(void)
{
    if (!pci_dev)
        return 0;
    return e1000_read(0xC0);
}

void e1000_handle_interrupt(void)
{
    uint32_t status = e1000_get_interrupt_status();
    (void)status;
}