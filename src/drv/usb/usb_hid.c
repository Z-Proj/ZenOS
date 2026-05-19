#include "usb_hid.h"
#include "../input.h"
#include "../keyboard.h"
#include "../mouse.h"
#include "../../libk/string.h"

static uint8_t prev_keys[6];
static uint8_t prev_mods;
static uint8_t caps_lock;

#define USB_KEY_ARROW_UP 1
#define USB_KEY_ARROW_DOWN 2
#define USB_KEY_ARROW_LEFT 3
#define USB_KEY_ARROW_RIGHT 4

static const uint16_t hid_keymap[128] = {
    [0x04] = INPUT_KEY_A, [0x05] = INPUT_KEY_B, [0x06] = INPUT_KEY_C, [0x07] = INPUT_KEY_D,
    [0x08] = INPUT_KEY_E, [0x09] = INPUT_KEY_F, [0x0a] = INPUT_KEY_G, [0x0b] = INPUT_KEY_H,
    [0x0c] = INPUT_KEY_I, [0x0d] = INPUT_KEY_J, [0x0e] = INPUT_KEY_K, [0x0f] = INPUT_KEY_L,
    [0x10] = INPUT_KEY_M, [0x11] = INPUT_KEY_N, [0x12] = INPUT_KEY_O, [0x13] = INPUT_KEY_P,
    [0x14] = INPUT_KEY_Q, [0x15] = INPUT_KEY_R, [0x16] = INPUT_KEY_S, [0x17] = INPUT_KEY_T,
    [0x18] = INPUT_KEY_U, [0x19] = INPUT_KEY_V, [0x1a] = INPUT_KEY_W, [0x1b] = INPUT_KEY_X,
    [0x1c] = INPUT_KEY_Y, [0x1d] = INPUT_KEY_Z, [0x1e] = INPUT_KEY_1, [0x1f] = INPUT_KEY_2,
    [0x20] = INPUT_KEY_3, [0x21] = INPUT_KEY_4, [0x22] = INPUT_KEY_5, [0x23] = INPUT_KEY_6,
    [0x24] = INPUT_KEY_7, [0x25] = INPUT_KEY_8, [0x26] = INPUT_KEY_9, [0x27] = INPUT_KEY_0,
    [0x28] = INPUT_KEY_ENTER, [0x29] = INPUT_KEY_ESC, [0x2a] = INPUT_KEY_BACKSPACE,
    [0x2b] = INPUT_KEY_TAB, [0x2c] = INPUT_KEY_SPACE, [0x2d] = INPUT_KEY_MINUS,
    [0x2e] = INPUT_KEY_EQUAL, [0x2f] = INPUT_KEY_LEFTBRACE, [0x30] = INPUT_KEY_RIGHTBRACE,
    [0x31] = INPUT_KEY_BACKSLASH, [0x33] = INPUT_KEY_SEMICOLON, [0x34] = INPUT_KEY_APOSTROPHE,
    [0x35] = INPUT_KEY_GRAVE, [0x36] = INPUT_KEY_COMMA, [0x37] = INPUT_KEY_DOT,
    [0x38] = INPUT_KEY_SLASH, [0x39] = INPUT_KEY_CAPSLOCK, [0x3a] = INPUT_KEY_F1,
    [0x3b] = INPUT_KEY_F2, [0x4f] = INPUT_KEY_RIGHT, [0x50] = INPUT_KEY_LEFT,
    [0x51] = INPUT_KEY_DOWN, [0x52] = INPUT_KEY_UP,
};

static const uint16_t hid_modmap[8] = {
    INPUT_KEY_LEFTCTRL, INPUT_KEY_LEFTSHIFT, INPUT_KEY_LEFTALT, 0,
    INPUT_KEY_RIGHTCTRL, INPUT_KEY_RIGHTSHIFT, INPUT_KEY_RIGHTALT, 0
};

static const char hid_ascii[128][2] = {
    [0x04] = {'a', 'A'}, [0x05] = {'b', 'B'}, [0x06] = {'c', 'C'}, [0x07] = {'d', 'D'},
    [0x08] = {'e', 'E'}, [0x09] = {'f', 'F'}, [0x0a] = {'g', 'G'}, [0x0b] = {'h', 'H'},
    [0x0c] = {'i', 'I'}, [0x0d] = {'j', 'J'}, [0x0e] = {'k', 'K'}, [0x0f] = {'l', 'L'},
    [0x10] = {'m', 'M'}, [0x11] = {'n', 'N'}, [0x12] = {'o', 'O'}, [0x13] = {'p', 'P'},
    [0x14] = {'q', 'Q'}, [0x15] = {'r', 'R'}, [0x16] = {'s', 'S'}, [0x17] = {'t', 'T'},
    [0x18] = {'u', 'U'}, [0x19] = {'v', 'V'}, [0x1a] = {'w', 'W'}, [0x1b] = {'x', 'X'},
    [0x1c] = {'y', 'Y'}, [0x1d] = {'z', 'Z'}, [0x1e] = {'1', '!'}, [0x1f] = {'2', '@'},
    [0x20] = {'3', '#'}, [0x21] = {'4', '$'}, [0x22] = {'5', '%'}, [0x23] = {'6', '^'},
    [0x24] = {'7', '&'}, [0x25] = {'8', '*'}, [0x26] = {'9', '('}, [0x27] = {'0', ')'},
    [0x28] = {'\n', '\n'}, [0x29] = {27, 27}, [0x2a] = {8, 8}, [0x2b] = {'\t', '\t'},
    [0x2c] = {' ', ' '}, [0x2d] = {'-', '_'}, [0x2e] = {'=', '+'}, [0x2f] = {'[', '{'},
    [0x30] = {']', '}'}, [0x31] = {'\\', '|'}, [0x33] = {';', ':'}, [0x34] = {'\'', '"'},
    [0x35] = {'`', '~'}, [0x36] = {',', '<'}, [0x37] = {'.', '>'}, [0x38] = {'/', '?'},
    [0x4f] = {USB_KEY_ARROW_RIGHT, USB_KEY_ARROW_RIGHT},
    [0x50] = {USB_KEY_ARROW_LEFT, USB_KEY_ARROW_LEFT},
    [0x51] = {USB_KEY_ARROW_DOWN, USB_KEY_ARROW_DOWN},
    [0x52] = {USB_KEY_ARROW_UP, USB_KEY_ARROW_UP},
};

static int key_in_report(uint8_t key, const uint8_t *keys)
{
    for (int i = 0; i < 6; i++)
        if (keys[i] == key)
            return 1;
    return 0;
}

static void emit_ascii(uint8_t key, uint8_t mods)
{
    if (key == 0x39) {
        caps_lock ^= 1;
        return;
    }
    if (key >= 128)
        return;
    char normal = hid_ascii[key][0];
    char shifted = hid_ascii[key][1];
    if (!normal)
        return;
    int shift = (mods & 0x22) != 0;
    int alpha = normal >= 'a' && normal <= 'z';
    keyboard_enqueue_char(alpha ? (shift ^ caps_lock ? shifted : normal) : (shift ? shifted : normal));
}

void usb_hid_keyboard_report(const uint8_t *report, uint32_t len)
{
    if (!report || len < 8)
        return;

    uint8_t mods = report[0];
    const uint8_t *keys = report + 2;

    for (int i = 0; i < 8; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        if ((mods & bit) != (prev_mods & bit))
            input_enqueue_key(hid_modmap[i], (mods & bit) ? 1 : 0);
    }

    for (int i = 0; i < 6; i++) {
        uint8_t key = prev_keys[i];
        if (key && !key_in_report(key, keys) && key < 128)
            input_enqueue_key(hid_keymap[key], 0);
    }

    for (int i = 0; i < 6; i++) {
        uint8_t key = keys[i];
        if (key && !key_in_report(key, prev_keys) && key < 128) {
            input_enqueue_key(hid_keymap[key], 1);
            emit_ascii(key, mods);
        }
    }

    prev_mods = mods;
    memcpy(prev_keys, keys, sizeof(prev_keys));
}

void usb_hid_mouse_report(const uint8_t *report, uint32_t len)
{
    if (!report || len < 3)
        return;

    uint8_t buttons = report[0] & 0x07;
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];
    mouse_apply_delta(dx, dy, buttons);
}
