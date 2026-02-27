#include "../userlib.h"

static void print2(int n) {
    char s[3] = { '0' + n / 10, '0' + n % 10, '\0' };
    prints(s);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    prints("\033[36mZenOS Clock - press any key to exit\033[0m\n");

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
            prints("\r\033[33m");
            print2(hours); prints(":");
            print2(mins);  prints(":");
            print2(secs);
            prints("\033[0m  ");
        }

        char k = getkey();
        if (k != 0) break;

        sleep(200);
        yield();
    }

    prints("\n");
    exit(0);
    return 0;
}
