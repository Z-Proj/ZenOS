/**
 * 
 * @file : /src/drv/speaker.h
 * @brief : PC speaker driver.
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

#ifndef SPEAKER_H
#define SPEAKER_H
#include "stdint.h"

void speaker_note(uint8_t octave, uint8_t note);
void speaker_play(uint32_t hz);
void speaker_pause(void);

#endif