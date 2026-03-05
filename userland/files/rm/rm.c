#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { fputs("Usage: rm <file> [file2 ...]\n", stdout); return 1; }
    int ret = 0;
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) != 0) {
            fputs("rm: failed: ", stdout);
            fputs(argv[i], stdout);
            fputs("\n", stdout);
            ret = 1;
        }
    }
    return ret;
}
