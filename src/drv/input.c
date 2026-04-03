#include "input.h"
#include "disk/devfs.h"
#include "hpet.h"
#include "mouse.h"
#include "../kernel/sched.h"
#include "../libk/core/syscall.h"
#include "../libk/spinlock.h"
#include "../libk/string.h"

#define INPUT_EVENT_QUEUE_CAP 256
#define INPUT_BYTE_QUEUE_CAP 1024

typedef struct
{
    input_event_t data[INPUT_EVENT_QUEUE_CAP];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    spinlock_t lock;
} input_event_queue_t;

typedef struct
{
    uint8_t data[INPUT_BYTE_QUEUE_CAP];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    spinlock_t lock;
} input_byte_queue_t;

static input_event_queue_t keyboard_queue;
static input_event_queue_t mouse_queue;
static input_byte_queue_t mice_queue;
static int input_devfs_registered = 0;

static const uint16_t keycode_map[128] = {
    [0x76] = INPUT_KEY_ESC,
    [0x16] = INPUT_KEY_1,
    [0x1E] = INPUT_KEY_2,
    [0x26] = INPUT_KEY_3,
    [0x25] = INPUT_KEY_4,
    [0x2E] = INPUT_KEY_5,
    [0x36] = INPUT_KEY_6,
    [0x3D] = INPUT_KEY_7,
    [0x3E] = INPUT_KEY_8,
    [0x46] = INPUT_KEY_9,
    [0x45] = INPUT_KEY_0,
    [0x4E] = INPUT_KEY_MINUS,
    [0x55] = INPUT_KEY_EQUAL,
    [0x66] = INPUT_KEY_BACKSPACE,
    [0x0D] = INPUT_KEY_TAB,
    [0x15] = INPUT_KEY_Q,
    [0x1D] = INPUT_KEY_W,
    [0x24] = INPUT_KEY_E,
    [0x2D] = INPUT_KEY_R,
    [0x2C] = INPUT_KEY_T,
    [0x35] = INPUT_KEY_Y,
    [0x3C] = INPUT_KEY_U,
    [0x43] = INPUT_KEY_I,
    [0x44] = INPUT_KEY_O,
    [0x4D] = INPUT_KEY_P,
    [0x54] = INPUT_KEY_LEFTBRACE,
    [0x5B] = INPUT_KEY_RIGHTBRACE,
    [0x5A] = INPUT_KEY_ENTER,
    [0x14] = INPUT_KEY_LEFTCTRL,
    [0x1C] = INPUT_KEY_A,
    [0x1B] = INPUT_KEY_S,
    [0x23] = INPUT_KEY_D,
    [0x2B] = INPUT_KEY_F,
    [0x34] = INPUT_KEY_G,
    [0x33] = INPUT_KEY_H,
    [0x3B] = INPUT_KEY_J,
    [0x42] = INPUT_KEY_K,
    [0x4B] = INPUT_KEY_L,
    [0x4C] = INPUT_KEY_SEMICOLON,
    [0x52] = INPUT_KEY_APOSTROPHE,
    [0x0E] = INPUT_KEY_GRAVE,
    [0x12] = INPUT_KEY_LEFTSHIFT,
    [0x5D] = INPUT_KEY_BACKSLASH,
    [0x1A] = INPUT_KEY_Z,
    [0x22] = INPUT_KEY_X,
    [0x21] = INPUT_KEY_C,
    [0x2A] = INPUT_KEY_V,
    [0x32] = INPUT_KEY_B,
    [0x31] = INPUT_KEY_N,
    [0x3A] = INPUT_KEY_M,
    [0x41] = INPUT_KEY_COMMA,
    [0x49] = INPUT_KEY_DOT,
    [0x4A] = INPUT_KEY_SLASH,
    [0x59] = INPUT_KEY_RIGHTSHIFT,
    [0x11] = INPUT_KEY_LEFTALT,
    [0x29] = INPUT_KEY_SPACE,
    [0x58] = INPUT_KEY_CAPSLOCK,
    [0x05] = INPUT_KEY_F1,
    [0x06] = INPUT_KEY_F2,
};

static const uint16_t ext_keycode_map[128] = {
    [0x11] = INPUT_KEY_RIGHTALT,
    [0x14] = INPUT_KEY_RIGHTCTRL,
    [0x6B] = INPUT_KEY_LEFT,
    [0x72] = INPUT_KEY_DOWN,
    [0x74] = INPUT_KEY_RIGHT,
    [0x75] = INPUT_KEY_UP,
};

static void input_fill_time(input_event_t *event)
{
    uint64_t ns = hpet_monotonic_ns();
    event->tv_sec = (int64_t)(ns / 1000000000ULL);
    event->tv_usec = (int64_t)((ns % 1000000000ULL) / 1000ULL);
}

static void input_event_queue_init(input_event_queue_t *queue)
{
    memset(queue, 0, sizeof(*queue));
    spinlock_init(&queue->lock);
}

static void input_byte_queue_init(input_byte_queue_t *queue)
{
    memset(queue, 0, sizeof(*queue));
    spinlock_init(&queue->lock);
}

static void input_event_queue_push(input_event_queue_t *queue, uint16_t type, uint16_t code, int32_t value)
{
    uint64_t rflags = spinlock_acquire_irqsave(&queue->lock);
    if (queue->count < INPUT_EVENT_QUEUE_CAP)
    {
        input_event_t *event = &queue->data[queue->head];
        input_fill_time(event);
        event->type = type;
        event->code = code;
        event->value = value;
        queue->head = (queue->head + 1) % INPUT_EVENT_QUEUE_CAP;
        queue->count++;
    }
    spinlock_release_irqrestore(&queue->lock, rflags);
}

static int input_event_queue_pop(input_event_queue_t *queue, input_event_t *event)
{
    int found = 0;
    uint64_t rflags = spinlock_acquire_irqsave(&queue->lock);
    if (queue->count)
    {
        *event = queue->data[queue->tail];
        queue->tail = (queue->tail + 1) % INPUT_EVENT_QUEUE_CAP;
        queue->count--;
        found = 1;
    }
    spinlock_release_irqrestore(&queue->lock, rflags);
    return found;
}

static uint32_t input_event_queue_bytes(input_event_queue_t *queue)
{
    uint32_t bytes;
    uint64_t rflags = spinlock_acquire_irqsave(&queue->lock);
    bytes = queue->count * (uint32_t)sizeof(input_event_t);
    spinlock_release_irqrestore(&queue->lock, rflags);
    return bytes;
}

static void input_byte_queue_push(input_byte_queue_t *queue, uint8_t value)
{
    uint64_t rflags = spinlock_acquire_irqsave(&queue->lock);
    if (queue->count < INPUT_BYTE_QUEUE_CAP)
    {
        queue->data[queue->head] = value;
        queue->head = (queue->head + 1) % INPUT_BYTE_QUEUE_CAP;
        queue->count++;
    }
    spinlock_release_irqrestore(&queue->lock, rflags);
}

static int input_byte_queue_pop(input_byte_queue_t *queue, uint8_t *value)
{
    int found = 0;
    uint64_t rflags = spinlock_acquire_irqsave(&queue->lock);
    if (queue->count)
    {
        *value = queue->data[queue->tail];
        queue->tail = (queue->tail + 1) % INPUT_BYTE_QUEUE_CAP;
        queue->count--;
        found = 1;
    }
    spinlock_release_irqrestore(&queue->lock, rflags);
    return found;
}

static uint32_t input_byte_queue_count(input_byte_queue_t *queue)
{
    uint32_t count;
    uint64_t rflags = spinlock_acquire_irqsave(&queue->lock);
    count = queue->count;
    spinlock_release_irqrestore(&queue->lock, rflags);
    return count;
}

static int input_event_read(input_event_queue_t *queue, void *buf, uint32_t size, uint32_t *got)
{
    if (got)
        *got = 0;
    if (!buf || size < sizeof(input_event_t))
        return 0;

    uint32_t copied = 0;
    while (copied + sizeof(input_event_t) <= size)
    {
        input_event_t event;
        if (!input_event_queue_pop(queue, &event))
        {
            if (copied)
                break;
            sched_yield();
            continue;
        }
        memcpy((uint8_t *)buf + copied, &event, sizeof(event));
        copied += sizeof(event);
    }

    if (got)
        *got = copied;
    return 0;
}

static int input_byte_read(input_byte_queue_t *queue, void *buf, uint32_t size, uint32_t *got)
{
    if (got)
        *got = 0;
    if (!buf || size == 0)
        return 0;

    uint32_t copied = 0;
    while (copied < size)
    {
        uint8_t value = 0;
        if (!input_byte_queue_pop(queue, &value))
        {
            if (copied)
                break;
            sched_yield();
            continue;
        }
        ((uint8_t *)buf)[copied++] = value;
    }

    if (got)
        *got = copied;
    return 0;
}

static int input_keyboard_read(void *buf, uint32_t size, uint32_t *got)
{
    return input_event_read(&keyboard_queue, buf, size, got);
}

static int input_mouse_read(void *buf, uint32_t size, uint32_t *got)
{
    mouse_poll();
    return input_event_read(&mouse_queue, buf, size, got);
}

static int input_mice_read(void *buf, uint32_t size, uint32_t *got)
{
    mouse_poll();
    return input_byte_read(&mice_queue, buf, size, got);
}

static int input_keyboard_ioctl(unsigned long req, void *argp)
{
    if (req == ZEN_FIONREAD && argp)
    {
        *(int *)argp = (int)input_event_queue_bytes(&keyboard_queue);
        return 0;
    }
    return -1;
}

static int input_mouse_ioctl(unsigned long req, void *argp)
{
    mouse_poll();
    if (req == ZEN_FIONREAD && argp)
    {
        *(int *)argp = (int)input_event_queue_bytes(&mouse_queue);
        return 0;
    }
    return -1;
}

static int input_mice_ioctl(unsigned long req, void *argp)
{
    mouse_poll();
    if (req == ZEN_FIONREAD && argp)
    {
        *(int *)argp = (int)input_byte_queue_count(&mice_queue);
        return 0;
    }
    return -1;
}

void input_init(void)
{
    input_event_queue_init(&keyboard_queue);
    input_event_queue_init(&mouse_queue);
    input_byte_queue_init(&mice_queue);
    input_devfs_registered = 0;
}

void input_register_devfs(void)
{
    if (input_devfs_registered)
        return;

    devfs_register("input/event0", input_keyboard_read, NULL, input_keyboard_ioctl);
    devfs_register("input/event1", input_mouse_read, NULL, input_mouse_ioctl);
    devfs_register("input/mice", input_mice_read, NULL, input_mice_ioctl);
    input_devfs_registered = 1;
}

uint16_t input_keycode_from_scancode(uint8_t scancode, int extended)
{
    if (scancode >= 128)
        return 0;
    return extended ? ext_keycode_map[scancode] : keycode_map[scancode];
}

void input_enqueue_key(uint16_t keycode, int32_t value)
{
    if (!keycode)
        return;
    input_event_queue_push(&keyboard_queue, INPUT_EV_KEY, keycode, value);
    input_event_queue_push(&keyboard_queue, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
}

void input_enqueue_mouse(int32_t dx, int32_t dy, uint8_t buttons, uint8_t prev_buttons)
{
    int pushed = 0;
    if (dx)
    {
        input_event_queue_push(&mouse_queue, INPUT_EV_REL, INPUT_REL_X, dx);
        pushed = 1;
    }
    if (dy)
    {
        input_event_queue_push(&mouse_queue, INPUT_EV_REL, INPUT_REL_Y, dy);
        pushed = 1;
    }

    uint8_t changed = buttons ^ prev_buttons;
    if (changed & 0x01)
    {
        input_event_queue_push(&mouse_queue, INPUT_EV_KEY, INPUT_BTN_LEFT, (buttons & 0x01) ? 1 : 0);
        pushed = 1;
    }
    if (changed & 0x02)
    {
        input_event_queue_push(&mouse_queue, INPUT_EV_KEY, INPUT_BTN_RIGHT, (buttons & 0x02) ? 1 : 0);
        pushed = 1;
    }
    if (changed & 0x04)
    {
        input_event_queue_push(&mouse_queue, INPUT_EV_KEY, INPUT_BTN_MIDDLE, (buttons & 0x04) ? 1 : 0);
        pushed = 1;
    }

    if (pushed)
        input_event_queue_push(&mouse_queue, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);

    if (dx || dy || changed)
    {
        int32_t raw_dy = -dy;
        uint8_t packet0 = 0x08;
        if (buttons & 0x01) packet0 |= 0x01;
        if (buttons & 0x02) packet0 |= 0x02;
        if (buttons & 0x04) packet0 |= 0x04;
        if (dx < 0) packet0 |= 0x10;
        if (raw_dy < 0) packet0 |= 0x20;
        input_byte_queue_push(&mice_queue, packet0);
        input_byte_queue_push(&mice_queue, (uint8_t)dx);
        input_byte_queue_push(&mice_queue, (uint8_t)raw_dy);
    }
}