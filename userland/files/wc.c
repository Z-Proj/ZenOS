#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include "../userlib.h"

static void print_num(uint64_t n) {
    if (n == 0) { fputs("0", stdout); return; }
    char buf[20];
    int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    for (int j = i - 1; j >= 0; j--) {
        char s[2] = { buf[j], '\0' };
        fputs(s, stdout);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("\033[31mUsage: wc <file>\033[0m\n", stdout);
        exit(1);
    }

    int file = open(argv[1], 0);
    if (file < 0) {
        fputs("\033[31mwc: cannot open: ", stdout);
        fputs(argv[1], stdout);
        fputs("\033[0m\n", stdout);
        exit(1);
    }

    static char buf[4096];
    uint64_t lines = 0, words = 0, chars = 0;
    int in_word = 0;
    ssize_t n;

    while ((n = read(file, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            chars++;
            if (c == '\n') lines++;
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }

    close(file);

    print_num(lines); fputs(" ", stdout);
    print_num(words); fputs(" ", stdout);
    print_num(chars); fputs(" ", stdout);
    fputs(argv[1], stdout);
    fputs("\n", stdout);
    exit(0);
    return 0;
}
