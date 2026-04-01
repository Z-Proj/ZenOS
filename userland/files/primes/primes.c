#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../userlib.h"

#define MAX_N 100000

static uint8_t sieve[MAX_N];

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
    int limit = 100;
    if (argc >= 2) {
        limit = atoi(argv[1]);
        if (limit < 2) limit = 2;
        if (limit >= MAX_N) limit = MAX_N - 1;
    }

    memset(sieve, 1, limit + 1);
    sieve[0] = 0;
    sieve[1] = 0;

    for (int i = 2; (i * i) <= limit; i++) {
        if (sieve[i]) {
            for (int j = i * i; j <= limit; j += i)
                sieve[j] = 0;
        }
    }

    fputs("\033[33mPrimes up to ", stdout);
    print_num(limit);
    fputs(":\033[0m\n", stdout);

    int count = 0;
    for (int i = 2; i <= limit; i++) {
        if (sieve[i]) {
            print_num(i);
            fputs("  ", stdout);
            count++;
            if (count % 10 == 0) fputs("\n", stdout);
            zen_halt();
        }
    }

    if (count % 10 != 0) fputs("\n", stdout);

    fputs("\033[32mFound ", stdout);
    print_num(count);
    fputs(" primes.\033[0m\n", stdout);

    exit(0);
    return 0;
}
