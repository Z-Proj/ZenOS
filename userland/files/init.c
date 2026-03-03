#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../userlib.h"

#define MAX_LINE 256

int main(int argc, char *argv[]) {
    if(argc < 1 || argc > 1){
        fputs("\n\x1b[38;2;255;50;50mInit: Can only be executed by the kernel. (Err: INVALID_ARGC)\x1b[0m\n", stderr);
        exit(1);
    } else if(strcmp(argv[0], "kernel") != 0) {
        fputs("\n\x1b[38;2;255;50;50mInit: Can only be executed by the kernel. (Err: INVALID_SIGN)\x1b[0m\n", stderr);
        exit(2);     
    }

    FILE *fp = fopen("/sys/init.run", "r");
    if (!fp) {
        fputs("\x1b[38;2;255;50;50mInit: fatal: /sys/init.run not found.\x1b[0m\n", stderr);
        exit(3);
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '\0' || line[0] == '#') continue;

        char *args[] = {line, NULL};
        if (execv(line, args) < 0) {
            fputs("\x1b[38;2;255;50;50mInit: failed to execute ", stderr);
            fputs(line, stderr);
            fputs("\x1b[0m\n", stderr);
            exit(4);
        }
    }

    fclose(fp);
    exit(0);
    return 0;
}