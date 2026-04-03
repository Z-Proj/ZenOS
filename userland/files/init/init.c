#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../../userlib.h"

#define MAX_LINE 512
#define MAX_ARGS 64

static int parse_args(char *line, char *argv[], int max_args)
{
    int argc = 0;
    int i = 0;

    while (line[i] && argc < max_args - 1) {
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (!line[i]) break;

        if (line[i] == '"') {
            i++;
            argv[argc++] = &line[i];
            while (line[i] && line[i] != '"') i++;
            if (line[i] == '"') line[i++] = '\0';
        } else {
            argv[argc++] = &line[i];
            while (line[i] && line[i] != ' ' && line[i] != '\t') i++;
            if (line[i]) line[i++] = '\0';
        }
    }

    argv[argc] = NULL;
    return argc;
}

static void handle_sleep(const char *arg)
{
    if (!arg) {
        fputs("\x1b[38;2;255;165;0mInit: !sleep missing argument, skipping.\x1b[0m\n", stderr);
        return;
    }

    char *end;
    long secs = strtol(arg, &end, 10);
    if (*end != '\0' || secs < 0) {
        fputs("\x1b[38;2;255;165;0mInit: !sleep invalid argument: ", stderr);
        fputs(arg, stderr);
        fputs("\x1b[0m\n", stderr);
        return;
    }

    sleep((unsigned int)secs);
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
        line[strcspn(line, "\n")] = 0;

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '\0' || *p == '#') continue;

        if (*p == '!') {
            p++;
            char *cmd = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
            while (*p == ' ' || *p == '\t') p++;
            char *arg = *p ? p : NULL;

            if (strcmp(cmd, "sleep") == 0) {
                handle_sleep(arg);
            } else {
                fputs("\x1b[38;2;255;165;0mInit: unknown directive: !", stderr);
                fputs(cmd, stderr);
                fputs("\x1b[0m\n", stderr);
            }
            continue;
        }

        char *args[MAX_ARGS];
        int narg = parse_args(p, args, MAX_ARGS);
        if (narg <= 0) continue;

        pid_t pid = fork();
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
