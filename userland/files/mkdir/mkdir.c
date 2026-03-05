#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { fputs("Usage: mkdir <dir>\n", stdout); return 1; }
    int ret = 0;
    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0755) != 0) {
            fputs("mkdir: failed: ", stdout);
            fputs(argv[i], stdout);
            fputs("\n", stdout);
            ret = 1;
        }
    }
    return ret;
}
