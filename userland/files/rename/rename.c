#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fputs("Usage: rename <old> <new>\n", stdout);
        return 1;
    }

    if (rename(argv[1], argv[2]) != 0) {
        fputs("rename: failed\n", stdout);
        return 1;
    }

    return 0;
}
