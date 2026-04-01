#include "keyboard.h"
#include "vga.h"
#include "mouse.h"
#include "input.h"
#include "../cpu/isr.h"
#include "../libk/ports.h"
#include "../libk/spinlock.h"
#include "../libk/debug/log.h"
#include "../libk/string.h"
#include "../libk/core/mem.h"
#include "../kernel/sched.h"
#include <stdbool.h>

#define PS2_DATA_PORT        0x60
#define PS2_STATUS_PORT      0x64
#define PS2_COMMAND_PORT     0x64

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02
#define PS2_CMD_READ_CONFIG    0x20
#define PS2_CMD_WRITE_CONFIG   0x60
#define PS2_CMD_DISABLE_PORT2  0xA7
#define PS2_CMD_ENABLE_PORT2   0xA8
#define PS2_CMD_DISABLE_PORT1  0xAD
#define PS2_CMD_ENABLE_PORT1   0xAE
#define PS2_CMD_TEST_PORT1     0xAB

#define PS2_CONFIG_PORT1_INT   0x01
#define PS2_CONFIG_PORT2_INT   0x02
#define PS2_CONFIG_PORT1_TRANS 0x40

#define KBD_CMD_RESET        0xFF
#define KBD_CMD_ENABLE_SCAN  0xF4
#define KBD_RESPONSE_ACK     0xFA
#define KBD_RESPONSE_RESEND  0xFE

#define SCANCODE_RELEASE_PREFIX  0xF0
#define SCANCODE_EXTENDED_PREFIX 0xE0

#define SWITCHER_HIDE_TICKS 300

typedef struct {
    char normal;
    char shifted;
    uint8_t flags;
} key_mapping_t;

#define KEY_FLAG_ALPHA    0x01
#define KEY_FLAG_MODIFIER 0x02
#define KEY_FLAG_SPECIAL  0x04

static key_mapping_t scancode_map[128] = {
    [0x0D] = {9,   9,   KEY_FLAG_SPECIAL},
    [0x0E] = {'`', '~', 0},
    [0x11] = {0,   0,   KEY_FLAG_MODIFIER},
    [0x12] = {0,   0,   KEY_FLAG_MODIFIER},
    [0x14] = {0,   0,   KEY_FLAG_MODIFIER},
    [0x15] = {'q', 'Q', KEY_FLAG_ALPHA},
    [0x16] = {'1', '!', 0},
    [0x1A] = {'z', 'Z', KEY_FLAG_ALPHA},
    [0x1B] = {'s', 'S', KEY_FLAG_ALPHA},
    [0x1C] = {'a', 'A', KEY_FLAG_ALPHA},
    [0x1D] = {'w', 'W', KEY_FLAG_ALPHA},
    [0x1E] = {'2', '@', 0},
    [0x21] = {'c', 'C', KEY_FLAG_ALPHA},
    [0x22] = {'x', 'X', KEY_FLAG_ALPHA},
    [0x23] = {'d', 'D', KEY_FLAG_ALPHA},
    [0x24] = {'e', 'E', KEY_FLAG_ALPHA},
    [0x25] = {'4', '$', 0},
    [0x26] = {'3', '#', 0},
    [0x29] = {' ', ' ', 0},
    [0x2A] = {'v', 'V', KEY_FLAG_ALPHA},
    [0x2B] = {'f', 'F', KEY_FLAG_ALPHA},
    [0x2C] = {'t', 'T', KEY_FLAG_ALPHA},
    [0x2D] = {'r', 'R', KEY_FLAG_ALPHA},
    [0x2E] = {'5', '%', 0},
    [0x31] = {'n', 'N', KEY_FLAG_ALPHA},
    [0x32] = {'b', 'B', KEY_FLAG_ALPHA},
    [0x33] = {'h', 'H', KEY_FLAG_ALPHA},
    [0x34] = {'g', 'G', KEY_FLAG_ALPHA},
    [0x35] = {'y', 'Y', KEY_FLAG_ALPHA},
    [0x36] = {'6', '^', 0},
    [0x3A] = {'m', 'M', KEY_FLAG_ALPHA},
    [0x3B] = {'j', 'J', KEY_FLAG_ALPHA},
    [0x3C] = {'u', 'U', KEY_FLAG_ALPHA},
    [0x3D] = {'7', '&', 0},
    [0x3E] = {'8', '*', 0},
    [0x41] = {',', '<', 0},
    [0x42] = {'k', 'K', KEY_FLAG_ALPHA},
    [0x43] = {'i', 'I', KEY_FLAG_ALPHA},
    [0x44] = {'o', 'O', KEY_FLAG_ALPHA},
    [0x45] = {'0', ')', 0},
    [0x46] = {'9', '(', 0},
    [0x49] = {'.', '>', 0},
    [0x4A] = {'/', '?', 0},
    [0x4B] = {'l', 'L', KEY_FLAG_ALPHA},
    [0x4C] = {';', ':', 0},
    [0x4D] = {'p', 'P', KEY_FLAG_ALPHA},
    [0x4E] = {'-', '_', 0},
    [0x52] = {'\'', '"', 0},
    [0x54] = {'[', '{', 0},
    [0x55] = {'=', '+', 0},
    [0x58] = {0,   0,   KEY_FLAG_MODIFIER},
    [0x59] = {0,   0,   KEY_FLAG_MODIFIER},
    [0x5A] = {10,  10,  KEY_FLAG_SPECIAL},
    [0x5B] = {']', '}', 0},
    [0x5D] = {'\\','|', 0},
    [0x66] = {8,   8,   KEY_FLAG_SPECIAL},
    [0x76] = {27,  27,  KEY_FLAG_SPECIAL},
};

#define EXT_SCANCODE_UP    0x75
#define EXT_SCANCODE_DOWN  0x72
#define EXT_SCANCODE_LEFT  0x6B
#define EXT_SCANCODE_RIGHT 0x74

#define KEY_ARROW_UP    0x01
#define KEY_ARROW_DOWN  0x02
#define KEY_ARROW_LEFT  0x03
#define KEY_ARROW_RIGHT 0x04

typedef struct {
    bool left_shift;
    bool right_shift;
    bool left_ctrl;
    bool right_ctrl;
    bool left_alt;
    bool right_alt;
    bool caps_lock;
} modifier_state_t;

typedef struct {
    char buffer[256];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} input_buffer_t;

static modifier_state_t modifiers = {0};
static input_buffer_t key_buffer = {0};
static bool key_states[256] = {0};
static bool waiting_for_release_code = false;
static bool waiting_for_extended_code = false;
static spinlock_t kbdlock;

#define SWITCHER_MAX_TASKS 32
#define SWITCHER_PAD_X     16
#define SWITCHER_PAD_Y     8
#define SWITCHER_ITEM_H    18
#define SWITCHER_ITEM_W    180

#define COL_BG        0x1a1a2e
#define COL_BORDER    0x4a9eff
#define COL_SEL_BG    0x4a9eff
#define COL_SEL_TEXT  0x000000
#define COL_TEXT      0xe0e0e0

static uint32_t *switcher_saved_pixels = NULL;
static uint32_t switcher_box_x = 0;
static uint32_t switcher_box_y = 0;
static uint32_t switcher_box_w = 0;
static uint32_t switcher_box_h = 0;
static bool switcher_visible = false;
static int switcher_hide_ticks = 0;

static uint64_t kbd_focused_pid = 0;
static bool focus_initialized = false;

static task_t *switcher_tasks[SWITCHER_MAX_TASKS];
static int switcher_task_count = 0;
static int switcher_selected = 0;

static void buffer_clear(void);

static void switcher_collect_tasks(void)
{
    switcher_task_count = 0;
    task_t *head = sched_get_task_list();
    if (!head) return;
    task_t *t = head;
    do {
        if (strcmp(t->name, "Idle") != 0 && t->state != TASK_DEAD) {
            if (switcher_task_count < SWITCHER_MAX_TASKS)
                switcher_tasks[switcher_task_count++] = t;
        }
        t = t->next;
    } while (t != head);
}

static void draw_rect_filled(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    for (uint32_t row = y; row < y + h; row++)
        for (uint32_t col = x; col < x + w; col++)
            put_pixel(col, row, color);
}

static void draw_rect_border(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t thickness)
{
    for (uint32_t t = 0; t < thickness; t++) {
        for (uint32_t col = x + t; col < x + w - t; col++) {
            put_pixel(col, y + t, color);
            put_pixel(col, y + h - 1 - t, color);
        }
        for (uint32_t row = y + t; row < y + h - t; row++) {
            put_pixel(x + t, row, color);
            put_pixel(x + w - 1 - t, row, color);
        }
    }
}

static void switcher_save_pixels(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (switcher_saved_pixels) {
        kfree(switcher_saved_pixels);
        switcher_saved_pixels = NULL;
    }
    switcher_saved_pixels = (uint32_t *)kmalloc(w * h * sizeof(uint32_t));
    if (!switcher_saved_pixels) return;
    for (uint32_t row = 0; row < h; row++)
        for (uint32_t col = 0; col < w; col++)
            switcher_saved_pixels[row * w + col] = get_pixel_at(x + col, y + row);
}

static void switcher_restore_pixels(void)
{
    if (!switcher_saved_pixels) return;
    uint32_t w = switcher_box_w;
    uint32_t h = switcher_box_h;
    uint32_t x = switcher_box_x;
    uint32_t y = switcher_box_y;
    for (uint32_t row = 0; row < h; row++)
        for (uint32_t col = 0; col < w; col++)
            put_pixel(x + col, y + row, switcher_saved_pixels[row * w + col]);
    kfree(switcher_saved_pixels);
    switcher_saved_pixels = NULL;
}

static void switcher_draw(void)
{
    if (!framebuffer_addr) return;

    switcher_collect_tasks();
    if (switcher_task_count == 0) return;

    uint32_t inner_w = SWITCHER_ITEM_W + SWITCHER_PAD_X;
    uint32_t inner_h = (uint32_t)switcher_task_count * SWITCHER_ITEM_H + SWITCHER_PAD_Y;

    uint32_t bx = 8;
    uint32_t by = (uint32_t)(framebuffer_height - inner_h - 8);

    switcher_box_x = bx;
    switcher_box_y = by;
    switcher_box_w = inner_w;
    switcher_box_h = inner_h;

    switcher_save_pixels(bx, by, inner_w, inner_h);

    draw_rect_filled(bx, by, inner_w, inner_h, COL_BG);
    draw_rect_border(bx, by, inner_w, inner_h, COL_BORDER, 2);

    for (int i = 0; i < switcher_task_count; i++) {
        uint32_t item_x = bx + SWITCHER_PAD_X / 2;
        uint32_t item_y = by + SWITCHER_PAD_Y / 2 + (uint32_t)i * SWITCHER_ITEM_H;

        if (i == switcher_selected) {
            draw_rect_filled(item_x - 4, item_y + 1, SWITCHER_ITEM_W, SWITCHER_ITEM_H - 2, COL_SEL_BG);
            draw_text_at(switcher_tasks[i]->name, item_x, item_y + 2, COL_SEL_TEXT);
        } else {
            draw_text_at(switcher_tasks[i]->name, item_x, item_y + 2, COL_TEXT);
        }
    }
}

static void switcher_show(int direction)
{
    switcher_collect_tasks();
    if (switcher_task_count == 0) return;

    if (!switcher_visible) {
        switcher_selected = 0;
        for (int i = 0; i < switcher_task_count; i++) {
            if (switcher_tasks[i]->pid == kbd_focused_pid) {
                switcher_selected = i;
                break;
            }
        }
    } else {
        if (switcher_visible)
            switcher_restore_pixels();
    }

    switcher_selected = (switcher_selected + direction + switcher_task_count) % switcher_task_count;
    switcher_visible = true;
    switcher_hide_ticks = SWITCHER_HIDE_TICKS;

    kbd_focused_pid = switcher_tasks[switcher_selected]->pid;
    buffer_clear();

    switcher_draw();
}

static void switcher_hide(void)
{
    if (!switcher_visible) return;
    switcher_restore_pixels();
    switcher_visible = false;
    switcher_hide_ticks = 0;
}

static void ps2_wait_input(void)
{
    uint32_t timeout = 100000;
    while ((inportb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) && --timeout);
}

static void ps2_wait_output(void)
{
    uint32_t timeout = 100000;
    while (!(inportb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) && --timeout);
}

static uint8_t ps2_read_data(void)
{
    ps2_wait_output();
    return inportb(PS2_DATA_PORT);
}

static void ps2_write_data(uint8_t data)
{
    ps2_wait_input();
    outportb(PS2_DATA_PORT, data);
}

static void ps2_write_command(uint8_t cmd)
{
    ps2_wait_input();
    outportb(PS2_COMMAND_PORT, cmd);
}

static uint8_t ps2_read_config(void)
{
    ps2_write_command(PS2_CMD_READ_CONFIG);
    return ps2_read_data();
}

static void ps2_write_config(uint8_t config)
{
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);
}

static bool kbd_send_command(uint8_t cmd)
{
    for (int retry = 0; retry < 3; retry++) {
        ps2_write_data(cmd);
        uint8_t response = ps2_read_data();
        if (response == KBD_RESPONSE_ACK) return true;
        if (response != KBD_RESPONSE_RESEND) break;
    }
    return false;
}

static void buffer_put_char(char c)
{
    uint64_t rflags = spinlock_acquire_irqsave(&kbdlock);
    if (key_buffer.count < 255) {
        key_buffer.buffer[key_buffer.head] = c;
        key_buffer.head = (key_buffer.head + 1) % 256;
        key_buffer.count++;
    }
    spinlock_release_irqrestore(&kbdlock, rflags);
}

static char buffer_get_char(void)
{
    uint64_t rflags = spinlock_acquire_irqsave(&kbdlock);
    if (key_buffer.count == 0) {
        spinlock_release_irqrestore(&kbdlock, rflags);
        return 0;
    }
    char c = key_buffer.buffer[key_buffer.tail];
    key_buffer.tail = (key_buffer.tail + 1) % 256;
    key_buffer.count--;
    spinlock_release_irqrestore(&kbdlock, rflags);
    return c;
}

static bool buffer_has_data(void)
{
    uint64_t rflags = spinlock_acquire_irqsave(&kbdlock);
    bool has_data = key_buffer.count > 0;
    spinlock_release_irqrestore(&kbdlock, rflags);
    return has_data;
}

static void buffer_clear(void)
{
    uint64_t rflags = spinlock_acquire_irqsave(&kbdlock);
    key_buffer.head = 0;
    key_buffer.tail = 0;
    key_buffer.count = 0;
    spinlock_release_irqrestore(&kbdlock, rflags);
}

static void update_modifier_state(uint8_t scancode, bool pressed, bool extended)
{
    if (extended) {
        switch (scancode) {
            case 0x14: modifiers.right_ctrl = pressed; break;
            case 0x11: modifiers.right_alt = pressed; break;
        }
        return;
    }
    switch (scancode) {
        case 0x12: modifiers.left_shift  = pressed; break;
        case 0x59: modifiers.right_shift = pressed; break;
        case 0x14: modifiers.left_ctrl   = pressed; break;
        case 0x11: modifiers.left_alt    = pressed; break;
        case 0x58: if (pressed) modifiers.caps_lock = !modifiers.caps_lock; break;
    }
}

static char process_key(uint8_t scancode)
{
    if (scancode >= 128) return 0;
    key_mapping_t mapping = scancode_map[scancode];
    if (mapping.flags & KEY_FLAG_MODIFIER) return 0;

    bool shift_active = modifiers.left_shift || modifiers.right_shift;
    bool caps_active  = modifiers.caps_lock;
    char result;

    if (mapping.flags & KEY_FLAG_ALPHA)
        result = (shift_active != caps_active) ? mapping.shifted : mapping.normal;
    else
        result = shift_active ? mapping.shifted : mapping.normal;

    return result;
}

static void switcher_move(int direction)
{
    if (!switcher_visible || switcher_task_count == 0) return;
    switcher_selected = (switcher_selected + direction + switcher_task_count) % switcher_task_count;
    kbd_focused_pid = switcher_tasks[switcher_selected]->pid;
    buffer_clear();
    switcher_hide_ticks = SWITCHER_HIDE_TICKS;

    uint32_t bx = switcher_box_x;
    uint32_t by = switcher_box_y;

    draw_rect_filled(bx + 2, by + 2, switcher_box_w - 4, switcher_box_h - 4, COL_BG);

    for (int i = 0; i < switcher_task_count; i++) {
        uint32_t item_x = bx + SWITCHER_PAD_X / 2;
        uint32_t item_y = by + SWITCHER_PAD_Y / 2 + (uint32_t)i * SWITCHER_ITEM_H;
        if (i == switcher_selected) {
            draw_rect_filled(item_x - 4, item_y + 1, SWITCHER_ITEM_W, SWITCHER_ITEM_H - 2, COL_SEL_BG);
            draw_text_at(switcher_tasks[i]->name, item_x, item_y + 2, COL_SEL_TEXT);
        } else {
            draw_text_at(switcher_tasks[i]->name, item_x, item_y + 2, COL_TEXT);
        }
    }
}

int font_id = 3;

static void kbd_handle_scancode(uint8_t scancode)
{
    if (scancode == SCANCODE_EXTENDED_PREFIX) {
        waiting_for_extended_code = true;
        return;
    }

    if (scancode == SCANCODE_RELEASE_PREFIX) {
        waiting_for_release_code = true;
        return;
    }

    bool extended = waiting_for_extended_code;
    bool released = waiting_for_release_code;
    waiting_for_extended_code = false;
    waiting_for_release_code  = false;

    if (released) {
        if (!extended)
            key_states[scancode] = false;
        update_modifier_state(scancode, false, extended);
        input_enqueue_key(input_keycode_from_scancode(scancode, extended), 0);
        return;
    }

    if (extended) {
        update_modifier_state(scancode, true, true);
        input_enqueue_key(input_keycode_from_scancode(scancode, 1), 1);
        if (switcher_visible) {
            if (scancode == EXT_SCANCODE_UP || scancode == EXT_SCANCODE_LEFT)
                switcher_move(-1);
            else if (scancode == EXT_SCANCODE_DOWN || scancode == EXT_SCANCODE_RIGHT)
                switcher_move(1);
        } else {
            if (scancode == EXT_SCANCODE_UP)    buffer_put_char(KEY_ARROW_UP);
            if (scancode == EXT_SCANCODE_DOWN)  buffer_put_char(KEY_ARROW_DOWN);
            if (scancode == EXT_SCANCODE_LEFT)  buffer_put_char(KEY_ARROW_LEFT);
            if (scancode == EXT_SCANCODE_RIGHT) buffer_put_char(KEY_ARROW_RIGHT);
        }
        return;
    }

    if (!key_states[scancode]) {
        key_states[scancode] = true;
        update_modifier_state(scancode, true, false);
        input_enqueue_key(input_keycode_from_scancode(scancode, 0), 1);

        bool shift = modifiers.left_shift || modifiers.right_shift;

        if (scancode == KBD_SCANCODE2_F1) {
            switcher_show(shift ? -1 : 1);
            return;
        }
        
        if (scancode == KBD_SCANCODE2_F2) {
            font(font_id++);
            if(font_id > 3) font_id = 0;
            return;
        }

        if (switcher_visible) {
            if (scancode == 0x5A) {
                kbd_focused_pid = switcher_tasks[switcher_selected]->pid;
                switcher_hide();
            } else if (scancode == 0x76) {
                switcher_hide();
            }
            return;
        }

        char c = process_key(scancode);
        if (c != 0)
            buffer_put_char(c);
    }
}

static void kbd_interrupt_handler(registers_t *regs)
{
    (void)regs;
    uint8_t status = inportb(PS2_STATUS_PORT);
    if (!(status & PS2_STATUS_OUTPUT_FULL)) return;
    if (status & 0x20) return;
    uint8_t scancode = inportb(PS2_DATA_PORT);
    kbd_handle_scancode(scancode);
}

void kbd_switcher_tick(void)
{
    if (!switcher_visible) return;
    if (switcher_hide_ticks > 0) switcher_hide_ticks--;
    if (switcher_hide_ticks == 0) switcher_hide();
}

void init_keyboard(void)
{
    spinlock_init(&kbdlock);
    ps2_write_command(PS2_CMD_DISABLE_PORT1);
    ps2_write_command(PS2_CMD_DISABLE_PORT2);

    while (inportb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL)
        inportb(PS2_DATA_PORT);

    uint8_t config = ps2_read_config();
    config &= ~(PS2_CONFIG_PORT1_INT | PS2_CONFIG_PORT2_INT | PS2_CONFIG_PORT1_TRANS);
    ps2_write_config(config);

    ps2_write_command(PS2_CMD_TEST_PORT1);
    if (ps2_read_data() != 0x00) return;

    ps2_write_command(PS2_CMD_ENABLE_PORT1);
    config = ps2_read_config();
    config |= PS2_CONFIG_PORT1_INT;
    ps2_write_config(config);

    kbd_send_command(KBD_CMD_RESET);
    ps2_read_data();
    kbd_send_command(KBD_CMD_ENABLE_SCAN);

    register_interrupt_handler(IRQ1, kbd_interrupt_handler, "Keyboard Handler");
    log("Keyboard Initialized.", 1, 0);
}

void kbd_init_focus(void)
{
    task_t *head = sched_get_task_list();
    if (!head) return;
    task_t *t = head;
    do {
        if (strcmp(t->name, "Idle") != 0 && t->state != TASK_DEAD) {
            kbd_focused_pid = t->pid;
            focus_initialized = true;
            return;
        }
        t = t->next;
    } while (t != head);
}

uint64_t kbd_get_focused_pid(void)
{
    return kbd_focused_pid;
}

int kbd_set_focused_pid(uint64_t pid)
{
    task_t *head = sched_get_task_list();
    if (!head)
        return -1;

    task_t *t = head;
    do {
        if (t->pid == pid &&
            strcmp(t->name, "Idle") != 0 &&
            t->state != TASK_DEAD) {
            kbd_focused_pid = pid;
            focus_initialized = true;
            return 0;
        }
        t = t->next;
    } while (t != head);

    return -1;
}

void kbd_transfer_focus(uint64_t dead_pid)
{
    if (kbd_focused_pid != dead_pid) return;

    task_t *head = sched_get_task_list();
    if (!head) return;

    task_t *t = head;
    do {
        if (t->pid != dead_pid &&
            strcmp(t->name, "Idle") != 0 &&
            t->state != TASK_DEAD) {
            kbd_focused_pid = t->pid;
            return;
        }
        t = t->next;
    } while (t != head);
}

char get_key(void)
{
    if (!focus_initialized) return buffer_get_char();

    task_t *current = sched_current_task();
    if (!current) return 0;
    if (current->pid != kbd_focused_pid) return 0;

    return buffer_get_char();
}

char wait_for_key(void)
{
    while (!buffer_has_data());
    return get_key();
}

void read_line(char *buffer, size_t max_size, bool print)
{
    size_t pos = 0;
    char c;
    while (pos < max_size - 1) {
        c = wait_for_key();
        asm volatile("cli");
        if (c == 10) {
            buffer[pos] = '\0';
            return;
        } else if (c == 8) {
            if (pos > 0) pos--;
            if (print) printc('\b');
        } else if (c >= 32 && c <= 126) {
            if (print) printc(c);
            buffer[pos++] = c;
        }
        asm volatile("sti");
    }
    buffer[max_size - 1] = '\0';
}

bool is_key_pressed(uint8_t scancode)  { return key_states[scancode]; }
bool is_shift_pressed(void)            { return modifiers.left_shift || modifiers.right_shift; }
bool is_ctrl_pressed(void)             { return modifiers.left_ctrl  || modifiers.right_ctrl; }
bool is_alt_pressed(void)              { return modifiers.left_alt   || modifiers.right_alt; }
bool is_caps_lock_on(void)             { return modifiers.caps_lock; }
