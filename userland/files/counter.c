#include "../userlib.h"
#include "../libs/lib.h"

static void print_int(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) { fputs("0", stdout); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    for (int j = i - 1; j >= 0; j--) {
        char s[2] = { buf[j], '\0' };
        fputs(s, stdout);
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    pid_t pid = getpid();

    fputs("\033[33m[Counter ", stdout);
    print_int(pid);
    fputs("] Starting...\033[0m\n", stdout);

    for (int count = 0; count < 20; count++) {
        fputs("\033[36m[", stdout);
        print_int(pid);
        fputs("] Count: ", stdout);
        print_int(count);
        fputs("\033[0m\n", stdout);
        zen_sleep_ms(100);
        zen_halt();
    }

    fputs("\033[32m[Counter ", stdout);
    print_int(pid);
    fputs("] Finished!\033[0m\n", stdout);

    exit(0);
    return 0;
}