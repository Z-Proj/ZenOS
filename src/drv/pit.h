/**
 * 
 * @file : /src/drv/pit.h
 * @brief : PIT timer - IRQ0 fallback tick source when HPET is unavailable.
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

#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void pit_init(uint32_t frequency_hz);

#endif
