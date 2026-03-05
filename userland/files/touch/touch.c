#include <stdio.h>
#include "../../userlib.h"

int main(int argc, char *argv[]) {
    if (argc < 2) { fputs("Usage: touch <file>\n", stdout); return 1; }
    for (int i = 1; i < argc; i++) {
        if (zen_create(argv[i]) != 0) {
            fputs("touch: failed: ", stdout);
            fputs(argv[i], stdout);
            fputs("\n", stdout);
        }
    }
    return 0;
}
