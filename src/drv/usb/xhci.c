#include "xhci.h"
#include "usb_hid.h"
#include "../../libk/core/mem.h"
#include "../../libk/string.h"
#include "../../libk/debug/log.h"
#include "../../kernel/sched.h"
#include "../local_apic.h"

#define XHCI_MAX_CONTROLLERS 8
#define XHCI_MAX_DEVICES 32
#define XHCI_MAX_PORTS 32
#define XHCI_TRB_COUNT 256
#define XHCI_CFG_BUF_SIZE 512
#define XHCI_USBCMD 0x00
#define XHCI_USBSTS 0x04
#define XHCI_PAGESIZE 0x08
#define XHCI_CRCR 0x18
#define XHCI_DCBAAP 0x30
#define XHCI_CONFIG 0x38
#define XHCI_PORTREGS 0x400
#define XHCI_USBCMD_RS 0x00000001
#define XHCI_USBCMD_HCRST 0x00000002
#define XHCI_USBCMD_INTE 0x00000004
#define XHCI_USBSTS_HCH 0x00000001
#define XHCI_USBSTS_EINT 0x00000008
#define XHCI_USBSTS_CNR 0x00000800
#define XHCI_PORT_CCS 0x00000001
#define XHCI_PORT_PED 0x00000002
#define XHCI_PORT_PR 0x00000010
#define XHCI_PORT_SPEED_SHIFT 10
#define XHCI_PORT_RESET_CHANGE 0x00200000
#define XHCI_PORT_CHANGE_MASK 0x00fe0000
#define XHCI_RUNTIME_IR0 0x20
#define XHCI_IMAN 0x00
#define XHCI_ERSTSZ 0x08
#define XHCI_ERSTBA 0x10
#define XHCI_ERDP 0x18
#define XHCI_TRB_NORMAL 1
#define XHCI_TRB_SETUP 2
#define XHCI_TRB_DATA 3
#define XHCI_TRB_STATUS 4
#define XHCI_TRB_LINK 6
#define XHCI_TRB_ENABLE_SLOT 9
#define XHCI_TRB_ADDRESS_DEVICE 11
#define XHCI_TRB_CONFIGURE_ENDPOINT 12
#define XHCI_TRB_EVALUATE_CONTEXT 13
#define XHCI_TRB_TRANSFER_EVENT 32
#define XHCI_TRB_COMMAND_EVENT 33
#define XHCI_TRB_PORT_EVENT 34
#define XHCI_CC_SUCCESS 1
#define XHCI_CC_SHORT_PACKET 13
#define XHCI_EP_CONTROL 4
#define XHCI_EP_INTERRUPT_IN 7
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_DT_DEVICE 1
#define USB_DT_CONFIG 2
#define USB_DT_INTERFACE 4
#define USB_DT_ENDPOINT 5
#define USB_CLASS_HID 3
#define USB_HID_BOOT 1
#define USB_HID_KEYBOARD 1
#define USB_HID_MOUSE 2
#define USB_REQ_SET_IDLE 0x0a
#define USB_REQ_SET_PROTOCOL 0x0b

typedef struct {
    uint32_t p0;
    uint32_t p1;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) xhci_trb_t;

typedef struct {
    uint64_t base;
    uint32_t size;
    uint32_t rsvd;
} __attribute__((packed)) xhci_erst_entry_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint16_t usb;
    uint8_t cls;
    uint8_t subcls;
    uint8_t proto;
    uint8_t mps0;
    uint16_t vid;
    uint16_t pid;
    uint16_t bcd;
    uint8_t mfg;
    uint8_t product;
    uint8_t serial;
    uint8_t configs;
} __attribute__((packed)) usb_device_desc_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint16_t total_len;
    uint8_t interfaces;
    uint8_t config_value;
    uint8_t config_index;
    uint8_t attributes;
    uint8_t max_power;
} __attribute__((packed)) usb_config_desc_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint8_t number;
    uint8_t alt;
    uint8_t endpoints;
    uint8_t cls;
    uint8_t subcls;
    uint8_t proto;
    uint8_t string_index;
} __attribute__((packed)) usb_iface_desc_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint8_t addr;
    uint8_t attr;
    uint16_t max_packet;
    uint8_t interval;
} __attribute__((packed)) usb_ep_desc_t;

typedef struct {
    int active;
    int is_mouse;
    uint8_t slot_id;
    uint8_t dci;
    uint8_t interval;
    uint16_t max_packet;
    uint64_t ring_phys;
    uint64_t buf_phys;
    xhci_trb_t *ring;
    uint8_t *buf;
    uint32_t enqueue;
    uint8_t cycle;
    uint8_t errors;
    xhci_trb_t *pending;
} xhci_intr_ep_t;

typedef struct {
    int used;
    uint8_t slot_id;
    uint8_t port_id;
    uint8_t speed;
    uint16_t ep0_mps;
    uint8_t hid_iface;
    uint8_t hid_proto;
    uint64_t input_ctx_phys;
    uint64_t device_ctx_phys;
    uint64_t ctrl_ring_phys;
    uint8_t *input_ctx;
    uint8_t *device_ctx;
    xhci_trb_t *ctrl_ring;
    uint32_t ctrl_enqueue;
    uint8_t ctrl_cycle;
    xhci_intr_ep_t intr;
} xhci_device_t;

typedef struct {
    pci_device_t *pci;
    volatile uint8_t *mmio;
    volatile uint8_t *op;
    volatile uint8_t *rt;
    volatile uint32_t *db;
    uint8_t cap_len;
    uint8_t max_slots;
    uint8_t max_ports;
    uint8_t ctx_size;
    uint32_t db_off;
    uint32_t rt_off;
    uint64_t dcbaa_phys;
    uint64_t cmd_ring_phys;
    uint64_t event_ring_phys;
    uint64_t erst_phys;
    uint64_t *dcbaa;
    xhci_trb_t *cmd_ring;
    xhci_trb_t *event_ring;
    xhci_erst_entry_t *erst;
    uint32_t cmd_enqueue;
    uint8_t cmd_cycle;
    uint32_t event_dequeue;
    uint8_t event_cycle;
    uint8_t port_slot[XHCI_MAX_PORTS];
    int running;
} xhci_controller_t;

typedef struct {
    int type;
    int code;
    uint8_t slot;
    uint8_t ep;
    xhci_trb_t *trb;
    xhci_trb_t raw;
} xhci_event_t;

static xhci_controller_t controllers[XHCI_MAX_CONTROLLERS];
static int controller_count;
static xhci_device_t devices[XHCI_MAX_DEVICES];
static int poll_task_started;

static uint32_t rd32(volatile uint8_t *base, uint32_t reg)
{
    return *(volatile uint32_t *)(base + reg);
}

static void wr32(volatile uint8_t *base, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(base + reg) = value;
}

static void wr64(volatile uint8_t *base, uint32_t reg, uint64_t value)
{
    wr32(base, reg, (uint32_t)value);
    wr32(base, reg + 4, (uint32_t)(value >> 32));
}

static uint32_t trb_type(const xhci_trb_t *trb)
{
    return (trb->control >> 10) & 0x3f;
}

static uint64_t trb_param(const xhci_trb_t *trb)
{
    return (uint64_t)trb->p0 | ((uint64_t)trb->p1 << 32);
}

static void wait_pause(uint32_t count)
{
    while (count--)
        __asm__ volatile("pause" ::: "memory");
}

static int wait_clear(volatile uint8_t *base, uint32_t reg, uint32_t mask, uint32_t limit)
{
    while (limit--) {
        if (!(rd32(base, reg) & mask))
            return 0;
        __asm__ volatile("pause" ::: "memory");
    }
    return -1;
}

static int wait_set(volatile uint8_t *base, uint32_t reg, uint32_t mask, uint32_t limit)
{
    while (limit--) {
        if (rd32(base, reg) & mask)
            return 0;
        __asm__ volatile("pause" ::: "memory");
    }
    return -1;
}

static void *ctx_at(xhci_controller_t *hc, void *base, uint32_t index)
{
    return (uint8_t *)base + index * hc->ctx_size;
}

static uint32_t *input_control(xhci_device_t *dev)
{
    return (uint32_t *)dev->input_ctx;
}

static uint32_t *input_slot(xhci_controller_t *hc, xhci_device_t *dev)
{
    return (uint32_t *)ctx_at(hc, dev->input_ctx, 1);
}

static uint32_t *input_ep(xhci_controller_t *hc, xhci_device_t *dev, uint8_t dci)
{
    return (uint32_t *)ctx_at(hc, dev->input_ctx, dci + 1);
}

static void ring_link(xhci_trb_t *ring, uint64_t phys)
{
    ring[XHCI_TRB_COUNT - 1].p0 = (uint32_t)phys;
    ring[XHCI_TRB_COUNT - 1].p1 = (uint32_t)(phys >> 32);
    ring[XHCI_TRB_COUNT - 1].control = (XHCI_TRB_LINK << 10) | 2;
}

static xhci_trb_t *ring_push(xhci_trb_t *ring, uint64_t phys, uint32_t *enqueue, uint8_t *cycle, xhci_trb_t trb)
{
    if (*enqueue >= XHCI_TRB_COUNT - 1) {
        ring[XHCI_TRB_COUNT - 1].control = (XHCI_TRB_LINK << 10) | (*cycle ? 1 : 0) | 2;
        *cycle ^= 1;
        *enqueue = 0;
    }
    trb.control = (trb.control & ~1u) | (*cycle ? 1u : 0u);
    ring[*enqueue] = trb;
    xhci_trb_t *out = &ring[*enqueue];
    *enqueue += 1;
    (void)phys;
    return out;
}

static uint64_t ring_trb_phys(uint64_t ring_phys, xhci_trb_t *ring, xhci_trb_t *trb)
{
    return ring_phys + (uint64_t)(trb - ring) * sizeof(xhci_trb_t);
}

static int event_pop(xhci_controller_t *hc, xhci_event_t *out)
{
    xhci_trb_t *ev = &hc->event_ring[hc->event_dequeue];
    if ((ev->control & 1) != hc->event_cycle)
        return 0;

    memset(out, 0, sizeof(*out));
    out->raw = *ev;
    out->type = trb_type(ev);
    out->code = (ev->status >> 24) & 0xff;
    out->slot = (ev->control >> 24) & 0xff;
    out->ep = (ev->control >> 16) & 0x1f;
    out->trb = (xhci_trb_t *)(trb_param(ev) + KERNEL_VIRT_OFFSET);

    hc->event_dequeue++;
    if (hc->event_dequeue >= XHCI_TRB_COUNT) {
        hc->event_dequeue = 0;
        hc->event_cycle ^= 1;
    }

    uint64_t erdp = hc->event_ring_phys + (uint64_t)hc->event_dequeue * sizeof(xhci_trb_t);
    wr64((volatile uint8_t *)hc->rt + XHCI_RUNTIME_IR0, XHCI_ERDP, erdp | 8);
    return 1;
}

static xhci_device_t *device_by_slot(uint8_t slot)
{
    for (int i = 0; i < XHCI_MAX_DEVICES; i++)
        if (devices[i].used && devices[i].slot_id == slot)
            return &devices[i];
    return NULL;
}

static void submit_intr(xhci_controller_t *hc, xhci_device_t *dev)
{
    xhci_intr_ep_t *ep = &dev->intr;
    if (!ep->active || ep->pending)
        return;
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.p0 = (uint32_t)ep->buf_phys;
    trb.p1 = (uint32_t)(ep->buf_phys >> 32);
    trb.status = ep->max_packet;
    trb.control = (XHCI_TRB_NORMAL << 10) | (1 << 5);
    ep->pending = ring_push(ep->ring, ep->ring_phys, &ep->enqueue, &ep->cycle, trb);
    hc->db[dev->slot_id] = ep->dci;
}

static void handle_transfer_event(xhci_controller_t *hc, xhci_event_t *ev)
{
    xhci_device_t *dev = device_by_slot(ev->slot);
    if (!dev || !dev->intr.active || ev->ep != dev->intr.dci)
        return;
    if (ev->trb != dev->intr.pending)
        return;
    uint32_t remain = ev->raw.status & 0x1ffff;
    uint32_t got = dev->intr.max_packet > remain ? dev->intr.max_packet - remain : dev->intr.max_packet;
    if (ev->code == XHCI_CC_SUCCESS || ev->code == XHCI_CC_SHORT_PACKET) {
        dev->intr.errors = 0;
        if (dev->intr.is_mouse)
            usb_hid_mouse_report(dev->intr.buf, got);
        else
            usb_hid_keyboard_report(dev->intr.buf, got);
    } else if (++dev->intr.errors >= 3) {
        log("xHCI: disabling HID slot=%d ep=%d cc=%d", 2, 0, dev->slot_id, dev->intr.dci, ev->code);
        dev->intr.active = 0;
    }
    dev->intr.pending = NULL;
    submit_intr(hc, dev);
}

static void disconnect_port(xhci_controller_t *hc, uint8_t port)
{
    if (!port || port > XHCI_MAX_PORTS)
        return;
    uint8_t slot = hc->port_slot[port - 1];
    if (!slot)
        return;
    xhci_device_t *dev = device_by_slot(slot);
    if (dev) {
        dev->intr.active = 0;
        dev->used = 0;
    }
    hc->port_slot[port - 1] = 0;
    log("xHCI: port %d disconnected", 2, 0, port);
}

static int enumerate_port(xhci_controller_t *hc, uint8_t port);

static void handle_port_change(xhci_controller_t *hc, uint8_t port)
{
    if (!port || port > hc->max_ports || port > XHCI_MAX_PORTS)
        return;
    uint32_t reg = XHCI_PORTREGS + (uint32_t)(port - 1) * 0x10;
    uint32_t portsc = rd32(hc->op, reg);
    wr32(hc->op, reg, portsc | XHCI_PORT_CHANGE_MASK);
    if (!(portsc & XHCI_PORT_CCS)) {
        disconnect_port(hc, port);
        return;
    }
    if (!hc->port_slot[port - 1])
        enumerate_port(hc, port);
}

static void process_events(xhci_controller_t *hc)
{
    xhci_event_t ev;
    while (event_pop(hc, &ev)) {
        if (ev.type == XHCI_TRB_TRANSFER_EVENT)
            handle_transfer_event(hc, &ev);
        else if (ev.type == XHCI_TRB_PORT_EVENT)
            handle_port_change(hc, (ev.raw.p0 >> 24) & 0xff);
    }
}

static int wait_command(xhci_controller_t *hc, xhci_trb_t *cmd, uint8_t *slot_out)
{
    uint64_t phys = ring_trb_phys(hc->cmd_ring_phys, hc->cmd_ring, cmd);
    for (uint32_t i = 0; i < 10000000; i++) {
        xhci_event_t ev;
        while (event_pop(hc, &ev)) {
            if (ev.type == XHCI_TRB_COMMAND_EVENT && trb_param(&ev.raw) == phys) {
                if (slot_out)
                    *slot_out = ev.slot;
                return ev.code;
            }
            if (ev.type == XHCI_TRB_TRANSFER_EVENT)
                handle_transfer_event(hc, &ev);
        }
        __asm__ volatile("pause" ::: "memory");
    }
    return -1;
}

static int command_trb(xhci_controller_t *hc, xhci_trb_t trb, uint8_t *slot_out)
{
    xhci_trb_t *cmd = ring_push(hc->cmd_ring, hc->cmd_ring_phys, &hc->cmd_enqueue, &hc->cmd_cycle, trb);
    hc->db[0] = 0;
    int code = wait_command(hc, cmd, slot_out);
    return code == XHCI_CC_SUCCESS ? 0 : -1;
}

static int wait_transfer(xhci_controller_t *hc, xhci_device_t *dev, xhci_trb_t *last)
{
    for (uint32_t i = 0; i < 10000000; i++) {
        xhci_event_t ev;
        while (event_pop(hc, &ev)) {
            if (ev.type == XHCI_TRB_TRANSFER_EVENT && ev.slot == dev->slot_id && ev.trb == last) {
                if (ev.code == XHCI_CC_SUCCESS || ev.code == XHCI_CC_SHORT_PACKET)
                    return 0;
                return -1;
            }
            if (ev.type == XHCI_TRB_TRANSFER_EVENT)
                handle_transfer_event(hc, &ev);
        }
        __asm__ volatile("pause" ::: "memory");
    }
    return -1;
}

static int control_transfer(xhci_controller_t *hc, xhci_device_t *dev, uint8_t req_type, uint8_t req, uint16_t value, uint16_t index, void *data, uint16_t len)
{
    int in = (req_type & 0x80) != 0;
    xhci_trb_t setup;
    memset(&setup, 0, sizeof(setup));
    setup.p0 = (uint32_t)req_type | ((uint32_t)req << 8) | ((uint32_t)value << 16);
    setup.p1 = (uint32_t)index | ((uint32_t)len << 16);
    setup.status = 8;
    setup.control = (XHCI_TRB_SETUP << 10) | (1 << 6) | ((len ? (in ? 3 : 2) : 0) << 16);
    ring_push(dev->ctrl_ring, dev->ctrl_ring_phys, &dev->ctrl_enqueue, &dev->ctrl_cycle, setup);

    if (len) {
        xhci_trb_t data_trb;
        memset(&data_trb, 0, sizeof(data_trb));
        uint64_t phys = (uint64_t)data - KERNEL_VIRT_OFFSET;
        data_trb.p0 = (uint32_t)phys;
        data_trb.p1 = (uint32_t)(phys >> 32);
        data_trb.status = len;
        data_trb.control = (XHCI_TRB_DATA << 10) | (in ? (1 << 16) : 0);
        ring_push(dev->ctrl_ring, dev->ctrl_ring_phys, &dev->ctrl_enqueue, &dev->ctrl_cycle, data_trb);
    }

    xhci_trb_t status;
    memset(&status, 0, sizeof(status));
    status.control = (XHCI_TRB_STATUS << 10) | (1 << 5) | ((!len || !in) ? (1 << 16) : 0);
    xhci_trb_t *last = ring_push(dev->ctrl_ring, dev->ctrl_ring_phys, &dev->ctrl_enqueue, &dev->ctrl_cycle, status);
    hc->db[dev->slot_id] = 1;
    return wait_transfer(hc, dev, last);
}

static int reset_port(xhci_controller_t *hc, uint8_t port)
{
    uint32_t reg = XHCI_PORTREGS + (uint32_t)(port - 1) * 0x10;
    uint32_t v = rd32(hc->op, reg);
    if (!(v & XHCI_PORT_CCS))
        return -1;
    wr32(hc->op, reg, (v & ~XHCI_PORT_CHANGE_MASK) | XHCI_PORT_PR);
    for (uint32_t i = 0; i < 1000000; i++) {
        v = rd32(hc->op, reg);
        if (!(v & XHCI_PORT_PR) && (v & XHCI_PORT_RESET_CHANGE))
            break;
        __asm__ volatile("pause" ::: "memory");
    }
    wr32(hc->op, reg, rd32(hc->op, reg) | XHCI_PORT_CHANGE_MASK);
    v = rd32(hc->op, reg);
    return (v & XHCI_PORT_PED) ? 0 : -1;
}

static uint8_t port_speed(xhci_controller_t *hc, uint8_t port)
{
    uint32_t reg = XHCI_PORTREGS + (uint32_t)(port - 1) * 0x10;
    return (rd32(hc->op, reg) >> XHCI_PORT_SPEED_SHIFT) & 0xf;
}

static uint16_t default_mps(uint8_t speed)
{
    if (speed == 4)
        return 512;
    if (speed == 3)
        return 64;
    return 8;
}

static xhci_device_t *alloc_device(void)
{
    for (int i = 0; i < XHCI_MAX_DEVICES; i++) {
        if (devices[i].used)
            continue;
        memset(&devices[i], 0, sizeof(devices[i]));
        devices[i].used = 1;
        return &devices[i];
    }
    return NULL;
}

static int alloc_device_memory(xhci_controller_t *hc, xhci_device_t *dev)
{
    dev->input_ctx_phys = alloc_page();
    dev->device_ctx_phys = alloc_page();
    dev->ctrl_ring_phys = alloc_page();
    if (!dev->input_ctx_phys || !dev->device_ctx_phys || !dev->ctrl_ring_phys)
        return -1;
    dev->input_ctx = (uint8_t *)(dev->input_ctx_phys + KERNEL_VIRT_OFFSET);
    dev->device_ctx = (uint8_t *)(dev->device_ctx_phys + KERNEL_VIRT_OFFSET);
    dev->ctrl_ring = (xhci_trb_t *)(dev->ctrl_ring_phys + KERNEL_VIRT_OFFSET);
    memset(dev->input_ctx, 0, PAGE_SIZE);
    memset(dev->device_ctx, 0, PAGE_SIZE);
    memset(dev->ctrl_ring, 0, PAGE_SIZE);
    ring_link(dev->ctrl_ring, dev->ctrl_ring_phys);
    dev->ctrl_cycle = 1;
    (void)hc;
    return 0;
}

static void setup_ep0_context(xhci_controller_t *hc, xhci_device_t *dev)
{
    uint32_t *ic = input_control(dev);
    uint32_t *slot = input_slot(hc, dev);
    uint32_t *ep0 = input_ep(hc, dev, 1);
    ic[0] = 0;
    ic[1] = (1 << 0) | (1 << 1);
    slot[0] = ((uint32_t)dev->speed << 20) | (1u << 27);
    slot[1] = (uint32_t)dev->port_id << 16;
    ep0[0] = 0;
    ep0[1] = (3 << 1) | (XHCI_EP_CONTROL << 3) | ((uint32_t)dev->ep0_mps << 16);
    ep0[2] = (uint32_t)(dev->ctrl_ring_phys | 1);
    ep0[3] = (uint32_t)(dev->ctrl_ring_phys >> 32);
    ep0[4] = 8;
}

static int evaluate_ep0(xhci_controller_t *hc, xhci_device_t *dev, uint16_t mps)
{
    memset(dev->input_ctx, 0, PAGE_SIZE);
    uint32_t *ic = input_control(dev);
    uint32_t *ep0 = input_ep(hc, dev, 1);
    ic[1] = 1 << 1;
    ep0[1] = (3 << 1) | (XHCI_EP_CONTROL << 3) | ((uint32_t)mps << 16);
    xhci_trb_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.p0 = (uint32_t)dev->input_ctx_phys;
    cmd.p1 = (uint32_t)(dev->input_ctx_phys >> 32);
    cmd.control = (XHCI_TRB_EVALUATE_CONTEXT << 10) | ((uint32_t)dev->slot_id << 24);
    if (command_trb(hc, cmd, NULL) < 0)
        return -1;
    dev->ep0_mps = mps;
    return 0;
}

static int address_device(xhci_controller_t *hc, xhci_device_t *dev)
{
    uint8_t slot = 0;
    xhci_trb_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.control = XHCI_TRB_ENABLE_SLOT << 10;
    if (command_trb(hc, cmd, &slot) < 0 || !slot)
        return -1;

    dev->slot_id = slot;
    hc->dcbaa[slot] = dev->device_ctx_phys;
    memset(dev->input_ctx, 0, PAGE_SIZE);
    setup_ep0_context(hc, dev);

    memset(&cmd, 0, sizeof(cmd));
    cmd.p0 = (uint32_t)dev->input_ctx_phys;
    cmd.p1 = (uint32_t)(dev->input_ctx_phys >> 32);
    cmd.control = (XHCI_TRB_ADDRESS_DEVICE << 10) | ((uint32_t)slot << 24);
    return command_trb(hc, cmd, NULL);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int parse_hid_config(xhci_device_t *dev, const uint8_t *cfg, uint16_t len, uint8_t *config_value, usb_ep_desc_t *ep)
{
    const usb_config_desc_t *cd = (const usb_config_desc_t *)cfg;
    *config_value = cd->config_value;
    uint8_t iface = 0xff;
    uint8_t proto = 0;
    for (uint16_t off = 0; off + 2 <= len;) {
        uint8_t dl = cfg[off];
        uint8_t dt = cfg[off + 1];
        if (dl < 2 || off + dl > len)
            break;
        if (dt == USB_DT_INTERFACE && dl >= sizeof(usb_iface_desc_t)) {
            const usb_iface_desc_t *id = (const usb_iface_desc_t *)(cfg + off);
            if (id->cls == USB_CLASS_HID && id->subcls == USB_HID_BOOT &&
                (id->proto == USB_HID_KEYBOARD || id->proto == USB_HID_MOUSE)) {
                iface = id->number;
                proto = id->proto;
            } else {
                iface = 0xff;
            }
        } else if (dt == USB_DT_ENDPOINT && iface != 0xff && dl >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t *ed = (const usb_ep_desc_t *)(cfg + off);
            if ((ed->addr & 0x80) && ((ed->attr & 3) == 3)) {
                memcpy(ep, ed, sizeof(*ep));
                dev->hid_iface = iface;
                dev->hid_proto = proto;
                return 0;
            }
        }
        off += dl;
    }
    return -1;
}

static int configure_intr_ep(xhci_controller_t *hc, xhci_device_t *dev, const usb_ep_desc_t *ed)
{
    uint8_t ep_num = ed->addr & 0x0f;
    uint8_t dci = ep_num * 2 + 1;
    uint16_t mps = ed->max_packet;
    if (!mps)
        mps = rd16((const uint8_t *)&ed->max_packet);
    dev->intr.ring_phys = alloc_page();
    dev->intr.buf_phys = alloc_page();
    if (!dev->intr.ring_phys || !dev->intr.buf_phys)
        return -1;
    dev->intr.ring = (xhci_trb_t *)(dev->intr.ring_phys + KERNEL_VIRT_OFFSET);
    dev->intr.buf = (uint8_t *)(dev->intr.buf_phys + KERNEL_VIRT_OFFSET);
    memset(dev->intr.ring, 0, PAGE_SIZE);
    memset(dev->intr.buf, 0, PAGE_SIZE);
    ring_link(dev->intr.ring, dev->intr.ring_phys);
    dev->intr.cycle = 1;
    dev->intr.dci = dci;
    dev->intr.interval = ed->interval ? ed->interval : 10;
    dev->intr.max_packet = mps;
    dev->intr.is_mouse = dev->hid_proto == USB_HID_MOUSE;

    memset(dev->input_ctx, 0, PAGE_SIZE);
    uint32_t *ic = input_control(dev);
    uint32_t *slot = input_slot(hc, dev);
    uint32_t *ep = input_ep(hc, dev, dci);
    ic[1] = (1u << 0) | (1u << dci);
    slot[0] = ((uint32_t)dev->speed << 20) | ((uint32_t)dci << 27);
    slot[1] = (uint32_t)dev->port_id << 16;
    ep[0] = (uint32_t)dev->intr.interval << 16;
    ep[1] = (3 << 1) | (XHCI_EP_INTERRUPT_IN << 3) | ((uint32_t)mps << 16);
    ep[2] = (uint32_t)(dev->intr.ring_phys | 1);
    ep[3] = (uint32_t)(dev->intr.ring_phys >> 32);
    ep[4] = mps;

    xhci_trb_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.p0 = (uint32_t)dev->input_ctx_phys;
    cmd.p1 = (uint32_t)(dev->input_ctx_phys >> 32);
    cmd.control = (XHCI_TRB_CONFIGURE_ENDPOINT << 10) | ((uint32_t)dev->slot_id << 24);
    if (command_trb(hc, cmd, NULL) < 0)
        return -1;

    dev->intr.active = 1;
    submit_intr(hc, dev);
    return 0;
}

static int enumerate_port(xhci_controller_t *hc, uint8_t port)
{
    if (reset_port(hc, port) < 0)
        return -1;

    xhci_device_t *dev = alloc_device();
    if (!dev)
        return -1;
    dev->port_id = port;
    dev->speed = port_speed(hc, port);
    dev->ep0_mps = default_mps(dev->speed);

    if (alloc_device_memory(hc, dev) < 0 || address_device(hc, dev) < 0) {
        dev->used = 0;
        return -1;
    }
    if (port <= XHCI_MAX_PORTS)
        hc->port_slot[port - 1] = dev->slot_id;

    usb_device_desc_t dd;
    memset(&dd, 0, sizeof(dd));
    if (control_transfer(hc, dev, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE << 8, 0, &dd, 8) < 0) {
        if (port <= XHCI_MAX_PORTS)
            hc->port_slot[port - 1] = 0;
        dev->used = 0;
        return -1;
    }
    uint16_t real_mps = dd.mps0;
    if (dev->speed == 4 && real_mps <= 16)
        real_mps = 1u << real_mps;
    if (real_mps && real_mps != dev->ep0_mps)
        evaluate_ep0(hc, dev, real_mps);

    memset(&dd, 0, sizeof(dd));
    if (control_transfer(hc, dev, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE << 8, 0, &dd, sizeof(dd)) < 0) {
        if (port <= XHCI_MAX_PORTS)
            hc->port_slot[port - 1] = 0;
        dev->used = 0;
        return -1;
    }

    uint8_t cfg_buf[XHCI_CFG_BUF_SIZE];
    memset(cfg_buf, 0, sizeof(cfg_buf));
    if (control_transfer(hc, dev, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DT_CONFIG << 8, 0, cfg_buf, 9) < 0) {
        if (port <= XHCI_MAX_PORTS)
            hc->port_slot[port - 1] = 0;
        dev->used = 0;
        return -1;
    }

    uint16_t total = rd16(cfg_buf + 2);
    if (total > sizeof(cfg_buf))
        total = sizeof(cfg_buf);
    if (control_transfer(hc, dev, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DT_CONFIG << 8, 0, cfg_buf, total) < 0) {
        if (port <= XHCI_MAX_PORTS)
            hc->port_slot[port - 1] = 0;
        dev->used = 0;
        return -1;
    }

    uint8_t config_value = 0;
    usb_ep_desc_t ep;
    memset(&ep, 0, sizeof(ep));
    if (parse_hid_config(dev, cfg_buf, total, &config_value, &ep) < 0) {
        log("xHCI: slot %d non-HID device %04x:%04x", 2, 0, dev->slot_id, dd.vid, dd.pid);
        return 0;
    }

    control_transfer(hc, dev, 0x00, USB_REQ_SET_CONFIGURATION, config_value, 0, NULL, 0);
    control_transfer(hc, dev, 0x21, USB_REQ_SET_PROTOCOL, 0, dev->hid_iface, NULL, 0);
    control_transfer(hc, dev, 0x21, USB_REQ_SET_IDLE, 0, dev->hid_iface, NULL, 0);

    if (configure_intr_ep(hc, dev, &ep) < 0) {
        if (port <= XHCI_MAX_PORTS)
            hc->port_slot[port - 1] = 0;
        dev->used = 0;
        return -1;
    }

    log("xHCI: HID %s slot=%d port=%d ep=%d mps=%d", 1, 0,
        dev->hid_proto == USB_HID_MOUSE ? "mouse" : "keyboard",
        dev->slot_id, dev->port_id, dev->intr.dci, dev->intr.max_packet);
    return 0;
}

static void enumerate_ports(xhci_controller_t *hc)
{
    for (uint8_t p = 1; p <= hc->max_ports && p <= XHCI_MAX_PORTS; p++) {
        uint32_t portsc = rd32(hc->op, XHCI_PORTREGS + (uint32_t)(p - 1) * 0x10);
        if (!(portsc & XHCI_PORT_CCS))
            continue;
        log("xHCI: port %d connected status=%x", 1, 0, p, portsc);
        enumerate_port(hc, p);
    }
}

static void xhci_task(void)
{
    while (1) {
        xhci_poll();
        sched_yield();
    }
}

static void xhci_irq(registers_t *regs)
{
    (void)regs;
    for (int i = 0; i < controller_count; i++) {
        xhci_controller_t *hc = &controllers[i];
        if (!hc->running)
            continue;
        uint32_t st = rd32(hc->op, XHCI_USBSTS);
        if (st & XHCI_USBSTS_EINT)
            wr32(hc->op, XHCI_USBSTS, st);
        volatile uint8_t *ir = hc->rt + XHCI_RUNTIME_IR0;
        uint32_t iman = rd32(ir, XHCI_IMAN);
        if (iman & 1)
            wr32(ir, XHCI_IMAN, iman);
        process_events(hc);
    }
    LocalApicSendEOI();
}

static uint64_t xhci_bar_phys(pci_device_t *pci)
{
    uint64_t bar = pci->bars[0];
    if (pci_is_64bit_bar(pci->bars[0]))
        bar |= (uint64_t)pci->bars[1] << 32;
    return bar & ~0xFULL;
}

static int xhci_alloc_rings(xhci_controller_t *hc)
{
    hc->dcbaa_phys = alloc_page();
    hc->cmd_ring_phys = alloc_page();
    hc->event_ring_phys = alloc_page();
    hc->erst_phys = alloc_page();
    if (!hc->dcbaa_phys || !hc->cmd_ring_phys || !hc->event_ring_phys || !hc->erst_phys)
        return -1;

    hc->dcbaa = (uint64_t *)(hc->dcbaa_phys + KERNEL_VIRT_OFFSET);
    hc->cmd_ring = (xhci_trb_t *)(hc->cmd_ring_phys + KERNEL_VIRT_OFFSET);
    hc->event_ring = (xhci_trb_t *)(hc->event_ring_phys + KERNEL_VIRT_OFFSET);
    hc->erst = (xhci_erst_entry_t *)(hc->erst_phys + KERNEL_VIRT_OFFSET);

    memset(hc->dcbaa, 0, PAGE_SIZE);
    memset(hc->cmd_ring, 0, PAGE_SIZE);
    memset(hc->event_ring, 0, PAGE_SIZE);
    memset(hc->erst, 0, PAGE_SIZE);

    ring_link(hc->cmd_ring, hc->cmd_ring_phys);
    hc->cmd_cycle = 1;
    hc->event_cycle = 1;

    hc->erst[0].base = hc->event_ring_phys;
    hc->erst[0].size = XHCI_TRB_COUNT;
    return 0;
}

int xhci_init_controller(pci_device_t *pci)
{
    if (!pci || controller_count >= XHCI_MAX_CONTROLLERS)
        return -1;

    xhci_controller_t *hc = &controllers[controller_count];
    memset(hc, 0, sizeof(*hc));
    hc->pci = pci;

    uint64_t phys = xhci_bar_phys(pci);
    if (!phys)
        return -1;

    hc->mmio = (volatile uint8_t *)(phys + KERNEL_VIRT_OFFSET);
    hc->cap_len = rd32(hc->mmio, 0) & 0xff;
    hc->op = hc->mmio + hc->cap_len;

    uint32_t hcs1 = rd32(hc->mmio, 0x04);
    uint32_t hcc1 = rd32(hc->mmio, 0x10);
    hc->max_slots = hcs1 & 0xff;
    hc->max_ports = (hcs1 >> 24) & 0xff;
    hc->ctx_size = (hcc1 & (1 << 2)) ? 64 : 32;
    hc->db_off = rd32(hc->mmio, 0x14) & ~3u;
    hc->rt_off = rd32(hc->mmio, 0x18) & ~0x1fu;
    hc->db = (volatile uint32_t *)(hc->mmio + hc->db_off);
    hc->rt = hc->mmio + hc->rt_off;

    pci_enable_memory_space(pci);
    pci_enable_bus_mastering(pci);

    wr32(hc->op, XHCI_USBCMD, 0);
    if (wait_set(hc->op, XHCI_USBSTS, XHCI_USBSTS_HCH, 1000000) < 0)
        return -1;

    wr32(hc->op, XHCI_USBCMD, XHCI_USBCMD_HCRST);
    if (wait_clear(hc->op, XHCI_USBCMD, XHCI_USBCMD_HCRST, 1000000) < 0)
        return -1;
    if (wait_clear(hc->op, XHCI_USBSTS, XHCI_USBSTS_CNR, 1000000) < 0)
        return -1;

    if (xhci_alloc_rings(hc) < 0)
        return -1;

    wr32(hc->op, XHCI_CONFIG, hc->max_slots);
    wr64(hc->op, XHCI_DCBAAP, hc->dcbaa_phys);
    wr64(hc->op, XHCI_CRCR, hc->cmd_ring_phys | hc->cmd_cycle);

    volatile uint8_t *ir = hc->rt + XHCI_RUNTIME_IR0;
    wr32(ir, XHCI_ERSTSZ, 1);
    wr64(ir, XHCI_ERSTBA, hc->erst_phys);
    wr64(ir, XHCI_ERDP, hc->event_ring_phys);
    wr32(ir, XHCI_IMAN, 2);

    if (pci->msi_capable)
        pci_enable_msi(pci, 51, xhci_irq, "xHCI MSI");
    else if (pci->interrupt_line != 0xff)
        register_interrupt_handler(32 + pci->interrupt_line, xhci_irq, "xHCI IRQ");

    wr32(hc->op, XHCI_USBCMD, XHCI_USBCMD_RS | XHCI_USBCMD_INTE);
    if (wait_clear(hc->op, XHCI_USBSTS, XHCI_USBSTS_HCH, 1000000) < 0)
        return -1;

    hc->running = 1;
    controller_count++;

    log("xHCI: %02x:%02x.%x ports=%d slots=%d ctx=%d", 1, 0,
        pci->bus, pci->slot, pci->func, hc->max_ports, hc->max_slots, hc->ctx_size);

    wait_pause(100000);
    enumerate_ports(hc);

    if (!poll_task_started) {
        task_create(xhci_task, "xHCI");
        poll_task_started = 1;
    }

    return 0;
}

void xhci_poll(void)
{
    for (int i = 0; i < controller_count; i++) {
        xhci_controller_t *hc = &controllers[i];
        if (!hc->running)
            continue;
        uint32_t st = rd32(hc->op, XHCI_USBSTS);
        if (st & XHCI_USBSTS_EINT)
            wr32(hc->op, XHCI_USBSTS, st);
        process_events(hc);
    }
}

int xhci_controller_count(void)
{
    return controller_count;
}
