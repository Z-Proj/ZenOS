#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../userlib.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    fputs("\033[32mHello world!\033[0m\n", stdout);
    exit(0);
    return 0;
}
