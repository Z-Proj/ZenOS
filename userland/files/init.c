#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../userlib.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (execv("shell", NULL) < 0) {
        fputs("init: fatal: shell not found\n", stdout);
        exit(1);
    }
    
    exit(0);
    return 0;
}
