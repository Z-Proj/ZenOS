#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { fputs("Usage: rmdir <dir>\n", stdout); return 1; }
    int ret = 0;
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) != 0) {
            fputs("rmdir: failed: ", stdout);
            fputs(argv[i], stdout);
            fputs("\n", stdout);
            ret = 1;
        }
    }
    return ret;
}
