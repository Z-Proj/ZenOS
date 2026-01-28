#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include "pci.h"
#include "../../libk/core/mem.h"
#include "../../libk/debug/log.h"
#include "../../libk/string.h"
#include "../../cpu/isr.h"

#define E1000_VENDOR_ID     0x8086
#define E1000_DEVICE_ID     0x100E
#define E1000_DEVICE_ID_2   0x153A
#define E1000_DEVICE_ID_3   0x10EA

#define E1000_REG_CTRL      0x0000
#define E1000_REG_STATUS    0x0008
#define E1000_REG_EEPROM    0x0014
#define E1000_REG_CTRL_EXT  0x0018
#define E1000_REG_IMASK     0x00D0
#define E1000_REG_RCTRL     0x0100
#define E1000_REG_RXDCTL    0x3828
#define E1000_REG_RXADDR    0x5400
#define E1000_REG_RXADDR2   0x5404
#define E1000_REG_TCTRL     0x0400
#define E1000_REG_TXDCTL    0x3828
#define E1000_REG_RDBAL     0x2800
#define E1000_REG_RDBAH     0x2804
#define E1000_REG_RDLEN     0x2808
#define E1000_REG_RDH       0x2810
#define E1000_REG_RDT       0x2818
#define E1000_REG_TDBAL     0x3800
#define E1000_REG_TDBAH     0x3804
#define E1000_REG_TDLEN     0x3808
#define E1000_REG_TDH       0x3810
#define E1000_REG_TDT       0x3818
#define E1000_REG_RCTL      0x0100
#define E1000_REG_TCTL      0x0400
#define E1000_REG_TIPG      0x0410

#define E1000_RCTL_EN       (1 << 1)
#define E1000_RCTL_SBP      (1 << 2)
#define E1000_RCTL_UPE      (1 << 3)
#define E1000_RCTL_MPE      (1 << 4)
#define E1000_RCTL_LPE      (1 << 5)
#define E1000_RCTL_LBM_NO   (0 << 6)
#define E1000_RCTL_LBM_PHY  (3 << 6)
#define E1000_RCTL_RDMTS_HALF (0 << 8)
#define E1000_RCTL_RDMTS_QUARTER (1 << 8)
#define E1000_RCTL_RDMTS_EIGHTH (2 << 8)
#define E1000_RCTL_MO_36    (0 << 12)
#define E1000_RCTL_MO_35    (1 << 12)
#define E1000_RCTL_MO_34    (2 << 12)
#define E1000_RCTL_MO_32    (3 << 12)
#define E1000_RCTL_BAM      (1 << 15)
#define E1000_RCTL_VFE      (1 << 18)
#define E1000_RCTL_CFIEN    (1 << 19)
#define E1000_RCTL_CFI      (1 << 20)
#define E1000_RCTL_DPF      (1 << 22)
#define E1000_RCTL_PMCF     (1 << 23)
#define E1000_RCTL_SECRC    (1 << 26)

#define E1000_TCTL_EN       (1 << 1)
#define E1000_TCTL_PSP      (1 << 3)
#define E1000_TCTL_CT       4
#define E1000_TCTL_COLD     12
#define E1000_TCTL_SWXOFF   (1 << 22)
#define E1000_TCTL_RTLC     (1 << 24)

#define E1000_CMD_EOP       (1 << 0)
#define E1000_CMD_IFCS      (1 << 1)
#define E1000_CMD_IC        (1 << 2)
#define E1000_CMD_RS        (1 << 3)
#define E1000_CMD_RPS       (1 << 4)
#define E1000_CMD_VLE       (1 << 6)
#define E1000_CMD_IDE       (1 << 7)

#define NUM_RX_DESC         256
#define NUM_TX_DESC         256
#define RX_BUFFER_SIZE      2048
#define TX_BUFFER_SIZE      2048

typedef struct {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t status;
    volatile uint8_t errors;
    volatile uint16_t special;
} __attribute__((packed)) e1000_rx_desc;

typedef struct {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint8_t cso;
    volatile uint8_t cmd;
    volatile uint8_t status;
    volatile uint8_t css;
    volatile uint16_t special;
} __attribute__((packed)) e1000_tx_desc;

typedef struct {
    uint8_t bus, slot, func;
    uint32_t bar0;
    uint64_t mem_base;
    e1000_rx_desc *rx_ring;
    e1000_tx_desc *tx_ring;
    uint8_t *rx_buffers;
    uint8_t *tx_buffers;
    uint16_t rx_cur;
    uint16_t tx_cur;
    uint8_t mac[6];
    uint8_t has_eeprom;
    uint32_t device_id;
    uint8_t irq;
} e1000_device;

void e1000_init(void);
int e1000_send_packet(void* data, size_t len);
int e1000_receive_packet(void* buf, size_t buf_size);
void e1000_get_mac_address(uint8_t *mac);
uint32_t e1000_link_up(void);
void e1000_enable_interrupts(void);
void e1000_disable_interrupts(void);
uint32_t e1000_get_interrupt_status(void);
void e1000_handle_interrupt(void);

#endif