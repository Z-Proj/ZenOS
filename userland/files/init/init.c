#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../../userlib.h"

#define MAX_LINE 256
#define MAX_ARGS 32

static int parse_args(char *line, char *argv[], int max_args)
{
    int argc = 0;
    int in_tok = 0;
    int i = 0;

    while (line[i] && argc < max_args - 1) {
        if (line[i] == ' ' || line[i] == '\t') {
            line[i] = '\0';
            in_tok = 0;
        } else if (!in_tok) {
            argv[argc++] = &line[i];
            in_tok = 1;
        }
        i++;
    }

    argv[argc] = NULL;
    return argc;
}

int main(int argc, char *argv[]) {
    if(argc < 1 || argc > 1){
        fputs("\n\x1b[38;2;255;50;50mInit: Can only be executed by the kernel. (Err: INVALID_ARGC)\x1b[0m\n", stderr);
        exit(1);
    } else if(strcmp(argv[0], "kernel") != 0) {
        fputs("\n\x1b[38;2;255;50;50mInit: Can only be executed by the kernel. (Err: INVALID_SIGN)\x1b[0m\n", stderr);
        exit(2);     
    }

    FILE *fp = fopen("/mnt/drv0/sys/init.run", "r");
    if (!fp) {
        fputs("\x1b[38;2;255;50;50mInit: fatal: /sys/init.run not found.\x1b[0m\n", stderr);
        exit(3);
    }

    char line[MAX_LINE];
    int launched = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *args[MAX_ARGS];
        int narg = 0;
        pid_t pid;

        line[strcspn(line, "\n")] = 0;
        if (line[0] == '\0' || line[0] == '#') continue;

        narg = parse_args(line, args, MAX_ARGS);
        if (narg <= 0) continue;

        pid = fork();
        if (pid < 0) {
            fputs("\x1b[38;2;255;50;50mInit: fork failed for ", stderr);
            fputs(args[0], stderr);
            fputs("\x1b[0m\n", stderr);
            continue;
        }

        if (pid == 0) {
            if (execv(args[0], args) < 0) {
                fputs("\x1b[38;2;255;50;50mInit: failed to execute ", stderr);
                fputs(args[0], stderr);
                fputs("\x1b[0m\n", stderr);
                _exit(4);
            }
        }

        zen_set_focus(pid);
        launched++;
    }

    fclose(fp);

    while (launched > 0) {
        int st = 0;
        pid_t dead = waitpid(-1, &st, 0);
        if (dead < 0) break;
        launched--;
    }

    for (;;) {
        int st = 0;
        if (waitpid(-1, &st, 0) < 0)
            break;
    }

    exit(0);
    return 0;
}
