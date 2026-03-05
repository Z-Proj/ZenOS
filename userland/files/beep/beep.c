#include <stdio.h>
#include <stdlib.h>
#include "../../userlib.h"

int main(int argc, char *argv[]) {
    uint32_t freq = argc >= 2 ? (uint32_t)atoi(argv[1]) : 440;
    uint32_t dur  = argc >= 3 ? (uint32_t)atoi(argv[2]) : 200;
    zen_speaker(freq);
    zen_sleep_ms(dur);
    zen_speaker_off();
    return 0;
}
