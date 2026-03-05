#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../userlib.h"

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

static int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    pid_t pid = getpid();

    fputs("\033[35m[Fibonacci PID=", stdout);
    print_int(pid);
    fputs("] Computing...\033[0m\n", stdout);

    for (int num = 0; num < 15; num++) {
        fputs("\033[35m[", stdout);
        print_int(pid);
        fputs("] fib(", stdout);
        print_int(num);
        fputs(") = ", stdout);
        print_int(fib(num));
        fputs("\033[0m\n", stdout);
        sched_yield();
    }

    fputs("\033[32m[Fibonacci ", stdout);
    print_int(pid);
    fputs("] Complete!\033[0m\n", stdout);

    exit(0);
    return 0;
}