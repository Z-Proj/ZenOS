/**
 * 
 * @file : /src/cpu/gdt.h
 * @brief : GDT/TSS setup per CPU - kernel/user segments and task state segments.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

struct tss_struct
{
    uint32_t reserved1;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iopb_offset;
} __attribute__((packed));
typedef struct tss_struct tss_t;

tss_t *gdt_get_tss(uint32_t cpu_index);
void init_gdt_for_cpu(uint32_t cpu_index);

#endif
