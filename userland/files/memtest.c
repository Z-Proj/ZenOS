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

static void print_hex(uint64_t addr) {
    char hex[17];
    int idx = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        int digit = (addr >> shift) & 0xF;
        hex[idx++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
    }
    hex[idx] = '\0';
    prints(hex);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    pid_t pid = getpid();

    prints("\033[33m[MemTest ");
    print_int(pid);
    prints("] Testing memory allocation...\033[0m\n");

    void *ptr1 = sbrk(4096);
    if (ptr1 == (void *)-1) {
        prints("\033[31m[MemTest ");
        print_int(pid);
        prints("] sbrk failed!\033[0m\n");
        exit(1);
        return 1;
    }

    prints("\033[32m[MemTest ");
    print_int(pid);
    prints("] sbrk: allocated 4096 bytes at 0x");
    print_hex((uint64_t)ptr1);
    prints("\033[0m\n");

    for (int j = 0; j < 256; j++)
        ((char *)ptr1)[j] = 'A' + (j % 26);

    int ok = 1;
    for (int j = 0; j < 256; j++) {
        if (((char *)ptr1)[j] != 'A' + (j % 26)) { ok = 0; break; }
    }

    prints(ok ? "\033[32m[MemTest " : "\033[31m[MemTest ");
    print_int(pid);
    prints(ok ? "] Memory verification: PASS\033[0m\n" : "] Memory verification: FAIL\033[0m\n");

    void *ptr2 = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr2 == (void *)-1) {
        prints("\033[31m[MemTest ");
        print_int(pid);
        prints("] mmap failed!\033[0m\n");
    } else {
        prints("\033[32m[MemTest ");
        print_int(pid);
        prints("] mmap: allocated 8192 bytes\033[0m\n");

        memset(ptr2, 0xAA, 512);

        ok = 1;
        for (int j = 0; j < 512; j++) {
            if (((unsigned char *)ptr2)[j] != 0xAA) { ok = 0; break; }
        }

        prints(ok ? "\033[32m[MemTest " : "\033[31m[MemTest ");
        print_int(pid);
        prints(ok ? "] mmap verification: PASS\033[0m\n" : "] mmap verification: FAIL\033[0m\n");

        if (munmap(ptr2, 8192) == 0) {
            prints("\033[32m[MemTest ");
            print_int(pid);
            prints("] munmap: success\033[0m\n");
        }
    }

    prints("\033[32m[MemTest ");
    print_int(pid);
    prints("] All tests complete!\033[0m\n");

    exit(0);
    return 0;
}