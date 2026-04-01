#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../userlib.h"

int main(int argc, char *argv[]) {
    if (argc > 1) {
        while (1) {
            for (int i = 1; i < argc; i++) {
                fputs(argv[i], stdout);
                if (i < argc - 1)
                    fputs(" ", stdout);
            }
            fputs("\n", stdout);
            zen_halt();
            zen_halt();
            zen_halt();
        }
    } else {
        while (1) {
            fputs("yes\n", stdout);
            zen_halt();
            zen_halt();
            zen_halt();
        }
    }
    return 0;
}
