#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include "../../userlib.h"

static void print2(int n) {
    char s[3] = { '0' + n / 10, '0' + n % 10, '\0' };
    fputs(s, stdout);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    fputs("\033[36mZenOS Clock - press any key to exit\033[0m\n", stdout);

    struct timeval tv;
    int last_sec = -1;
    int hours = 0, mins = 0, secs = 0;
    struct termios oldt, raw;
    int have_termios = 0;

    if (tcgetattr(STDIN_FILENO, &oldt) == 0) {
        raw = oldt;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
            have_termios = 1;
    }

    while (1) {
        gettimeofday(&tv, NULL);
        int total = (int)(tv.tv_sec);
        secs  = total % 60;
        mins  = (total / 60) % 60;
        hours = (total / 3600) % 24;

        if (secs != last_sec) {
            last_sec = secs;
            fputs("\r\033[33m", stdout);
            print2(hours); fputs(":", stdout);
            print2(mins);  fputs(":", stdout);
            print2(secs);
            fputs("\033[0m  ", stdout);
            fflush(stdout);
        }

        char k = 0;
        if (read(STDIN_FILENO, &k, 1) > 0) break;
    }

    if (have_termios)
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    fputs("\n", stdout);
    exit(0);
    return 0;
}
