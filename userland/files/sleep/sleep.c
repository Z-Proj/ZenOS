#include <stdio.h>
#include <stdlib.h>
#include "../../userlib.h"

int main(int argc, char *argv[]) {
    if (argc < 2) { fputs("Usage: sleep <milliseconds>\n", stdout); return 1; }
    uint32_t ms = (uint32_t)atoi(argv[1]);
    zen_sleep_ms(ms);
    return 0;
}
