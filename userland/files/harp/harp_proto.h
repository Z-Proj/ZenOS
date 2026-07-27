/**
 * 
 * @file : harp_proto.h
 * @brief : Harp IPC protocol - window manager messages and event definitions.
 * 
 * MIT License
 * 
 * Copyright (c) 2026 Rishies2010
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * @author : Rishies2010
 * @copyright (c) 2026
 * 
 */

#pragma once
#include <stdint.h>

#define WM_SOCK "wm:events"

#define WM_MSG_REGISTER   1
#define WM_MSG_DIRTY      2
#define WM_MSG_UNREGISTER 3
#define WM_MSG_RETITLE    4
#define WM_MSG_RESIZE_REQ 5
#define WM_MSG_CAPTURE    6

#define HARP_EVENT_FOCUS         1
#define HARP_EVENT_BLUR          2
#define HARP_EVENT_MOUSE_MOVE    3
#define HARP_EVENT_MOUSE_BUTTON  4
#define HARP_EVENT_KEY           5
#define HARP_EVENT_RESIZE        6
#define HARP_EVENT_CLOSE_REQ     7
#define HARP_EVENT_EXPOSE        8
#define HARP_EVENT_MOUSE_RAW_MOVE 9

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