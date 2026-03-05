#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../userlib.h"

int main(int argc, char *argv[]) {
    char cwd[256];
    char *buf = malloc(8192);
    if (!buf) return 1;

    if (argc >= 2) {
        if (!getcwd(cwd, sizeof(cwd))) { free(buf); return 1; }
        if (chdir(argv[1]) != 0) {
            fputs("ls: cannot access: ", stdout);
            fputs(argv[1], stdout);
            fputs("\n", stdout);
            free(buf);
            return 1;
        }
    }

    if (zen_ls(buf, 8192) == 0)
        fputs(buf, stdout);
    else
        fputs("ls: failed\n", stdout);

    if (argc >= 2)
        chdir(cwd);

    free(buf);
    return 0;
}
