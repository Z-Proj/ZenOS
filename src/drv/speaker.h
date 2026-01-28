#ifndef SPEAKER_H
#define SPEAKER_H
#include "stdint.h"

void speaker_note(uint8_t octave, uint8_t note);
void speaker_play(uint32_t hz);
void speaker_pause(void);

#endif