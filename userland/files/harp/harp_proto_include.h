#pragma once
#include <stdint.h>

#define WM_SOCK "wm:events"

#define WM_MSG_REGISTER   1
#define WM_MSG_DIRTY      2
#define WM_MSG_UNREGISTER 3
#define WM_MSG_RETITLE    4
#define WM_MSG_RESIZE_REQ 5

#define HARP_EVENT_FOCUS        1
#define HARP_EVENT_BLUR         2
#define HARP_EVENT_MOUSE_MOVE   3
#define HARP_EVENT_MOUSE_BUTTON 4
#define HARP_EVENT_KEY          5
#define HARP_EVENT_RESIZE       6
#define HARP_EVENT_CLOSE_REQ    7
#define HARP_EVENT_EXPOSE       8

#define HARP_MOD_SHIFT 0x01
#define HARP_MOD_CTRL  0x02
#define HARP_MOD_ALT   0x04
#define HARP_MOD_CAPS  0x08

#pragma pack(push, 1)
typedef struct {
    uint8_t  type;
    uint32_t pid;
    int32_t  x;
    int32_t  y;
    int32_t  w;
    int32_t  h;
    char     title[64];
} wm_msg_t;

typedef struct {
    uint16_t type;
    uint16_t code;
    int32_t  value;
    int32_t  x;
    int32_t  y;
    uint32_t modifiers;
    int32_t  key;
    int32_t  w;
    int32_t  h;
} harp_event_t;
#pragma pack(pop)
