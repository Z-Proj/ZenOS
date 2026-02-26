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

static int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    pid_t pid = getpid();

    prints("\033[35m[Fibonacci PID=");
    print_int(pid);
    prints("] Computing...\033[0m\n");

    for (int num = 0; num < 15; num++) {
        prints("\033[35m[");
        print_int(pid);
        prints("] fib(");
        print_int(num);
        prints(") = ");
        print_int(fib(num));
        prints("\033[0m\n");
        yield();
    }

    prints("\033[32m[Fibonacci ");
    print_int(pid);
    prints("] Complete!\033[0m\n");

    exit(0);
    return 0;
}