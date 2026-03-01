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

static void print_hex(uint64_t addr) {
    char hex[17];
    int idx = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        int digit = (addr >> shift) & 0xF;
        hex[idx++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
    }
    hex[idx] = '\0';
    fputs(hex, stdout);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    pid_t pid = getpid();

    fputs("\033[33m[MemTest ", stdout);
    print_int(pid);
    fputs("] Testing memory allocation...\033[0m\n", stdout);

    void *ptr1 = sbrk(4096);
    if (ptr1 == (void *)-1) {
        fputs("\033[31m[MemTest ", stdout);
        print_int(pid);
        fputs("] sbrk failed!\033[0m\n", stdout);
        exit(1);
        return 1;
    }

    fputs("\033[32m[MemTest ", stdout);
    print_int(pid);
    fputs("] sbrk: allocated 4096 bytes at 0x", stdout);
    print_hex((uint64_t)ptr1);
    fputs("\033[0m\n", stdout);

    for (int j = 0; j < 256; j++)
        ((char *)ptr1)[j] = 'A' + (j % 26);

    int ok = 1;
    for (int j = 0; j < 256; j++) {
        if (((char *)ptr1)[j] != 'A' + (j % 26)) { ok = 0; break; }
    }

    fputs(ok ? "\033[32m[MemTest " : "\033[31m[MemTest ", stdout);
    print_int(pid);
    fputs(ok ? "] Memory verification: PASS\033[0m\n" : "] Memory verification: FAIL\033[0m\n", stdout);

    void *ptr2 = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr2 == (void *)-1) {
        fputs("\033[31m[MemTest ", stdout);
        print_int(pid);
        fputs("] mmap failed!\033[0m\n", stdout);
    } else {
        fputs("\033[32m[MemTest ", stdout);
        print_int(pid);
        fputs("] mmap: allocated 8192 bytes\033[0m\n", stdout);

        memset(ptr2, 0xAA, 512);

        ok = 1;
        for (int j = 0; j < 512; j++) {
            if (((unsigned char *)ptr2)[j] != 0xAA) { ok = 0; break; }
        }

        fputs(ok ? "\033[32m[MemTest " : "\033[31m[MemTest ", stdout);
        print_int(pid);
        fputs(ok ? "] mmap verification: PASS\033[0m\n" : "] mmap verification: FAIL\033[0m\n", stdout);

        if (munmap(ptr2, 8192) == 0) {
            fputs("\033[32m[MemTest ", stdout);
            print_int(pid);
            fputs("] munmap: success\033[0m\n", stdout);
        }
    }

    fputs("\033[32m[MemTest ", stdout);
    print_int(pid);
    fputs("] All tests complete!\033[0m\n", stdout);

    exit(0);
    return 0;
}