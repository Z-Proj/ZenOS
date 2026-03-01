#include "../userlib.h"
#include "../libs/lib.h"

static void print2(int n) {
    char s[3] = { '0' + n / 10, '0' + n % 10, '\0' };
    fputs(s, stdout);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    fputs("\033[36mZenOS Clock - press any key to exit\033[0m\n", stdout);

    timeval_t tv;
    int last_sec = -1;
    int hours = 0, mins = 0, secs = 0;

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
        }

        char k = zen_getkey();
        if (k != 0) break;
        zen_halt();
    }

    fputs("\n", stdout);
    exit(0);
    return 0;
}
