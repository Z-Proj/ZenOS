#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../userlib.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

static void print_int(int n) {
    char buf[32];
    int i = 0;
    if (n == 0) { fputs("0", stdout); return; }
    if (n < 0) { fputs("-", stdout); n = -n; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    for (int j = i - 1; j >= 0; j--) {
        char s[2] = { buf[j], '\0' };
        fputs(s, stdout);
    }
}

int main(int argc, char *argv[]) {
    fputs(COLOR_BOLD COLOR_CYAN "argc: " COLOR_RESET, stdout);
    print_int(argc);
    fputs("\n", stdout);

    for (int i = 0; i < argc; i++) {
        fputs(COLOR_YELLOW "argv[" COLOR_RESET, stdout);
        print_int(i);
        fputs(COLOR_YELLOW "] = " COLOR_RESET, stdout);
        fputs(argv[i], stdout);
        fputs("\n", stdout);
    }

    exit(0);
    return 0;
}