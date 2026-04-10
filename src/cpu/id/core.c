/**
 * 
 * @file : /src/cpu/id/core.c
 * @brief : Entry point for secondary CPUs during SMP bootstrap.
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

#include "core.h"
#include "../../libk/debug/log.h"
#include "../../drv/local_apic.h"
#include "../../kernel/sched.h"

void ap_main(void)
{
    sched_ap_entry();
}
