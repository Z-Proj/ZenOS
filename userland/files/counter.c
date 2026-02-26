#include "../userlib.h"

static void print_int(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) { prints("0"); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    for (int j = i - 1; j >= 0; j--) {
        char s[2] = { buf[j], '\0' };
        prints(s);
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    pid_t pid = getpid();

    prints("\033[33m[Counter ");
    print_int(pid);
    prints("] Starting...\033[0m\n");

    for (int count = 0; count < 20; count++) {
        prints("\033[36m[");
        print_int(pid);
        prints("] Count: ");
        print_int(count);
        prints("\033[0m\n");
        sleep(100);
        yield();
    }

    prints("\033[32m[Counter ");
    print_int(pid);
    prints("] Finished!\033[0m\n");

    exit(0);
    return 0;
}