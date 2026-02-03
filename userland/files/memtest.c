#include "../userlib.h"

int main(void) {
    pid_t pid = getpid();
    
    char pidbuf[16];
    int n = pid;
    int i = 0;
    while (n > 0 || i == 0) {
        pidbuf[i++] = '0' + (n % 10);
        n /= 10;
    }
    pidbuf[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = pidbuf[j];
        pidbuf[j] = pidbuf[i-1-j];
        pidbuf[i-1-j] = tmp;
    }
    
    prints("\033[33m[MemTest ");
    prints(pidbuf);
    prints("] Testing memory allocation...\033[0m\n");
    
    // Test sbrk allocation
    void *ptr1 = sbrk(4096);
    if (ptr1 == (void*)-1) {
        prints("\033[31m[MemTest ");
        prints(pidbuf);
        prints("] sbrk failed!\033[0m\n");
        exit(1);
        return 1;
    }
    
    prints("\033[32m[MemTest ");
    prints(pidbuf);
    prints("] sbrk: allocated 4096 bytes at 0x");
    
    // Print address
    uint64_t addr = (uint64_t)ptr1;
    char hex[20];
    int idx = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        int digit = (addr >> shift) & 0xF;
        hex[idx++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
    }
    hex[idx] = '\0';
    prints(hex);
    prints("\033[0m\n");
    
    // Write pattern to memory
    for (int j = 0; j < 256; j++) {
        ((char*)ptr1)[j] = 'A' + (j % 26);
    }
    
    yield();
    
    // Verify pattern
    int ok = 1;
    for (int j = 0; j < 256; j++) {
        if (((char*)ptr1)[j] != 'A' + (j % 26)) {
            ok = 0;
            break;
        }
    }
    
    if (ok) {
        prints("\033[32m[MemTest ");
        prints(pidbuf);
        prints("] Memory verification: PASS\033[0m\n");
    } else {
        prints("\033[31m[MemTest ");
        prints(pidbuf);
        prints("] Memory verification: FAIL\033[0m\n");
    }
    
    yield();
    
    // Test mmap
    void *ptr2 = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr2 == (void*)-1) {
        prints("\033[31m[MemTest ");
        prints(pidbuf);
        prints("] mmap failed!\033[0m\n");
    } else {
        prints("\033[32m[MemTest ");
        prints(pidbuf);
        prints("] mmap: allocated 8192 bytes\033[0m\n");
        
        // Write to mmap'd memory
        memset(ptr2, 0xAA, 512);
        
        yield();
        
        // Verify
        ok = 1;
        for (int j = 0; j < 512; j++) {
            if (((unsigned char*)ptr2)[j] != 0xAA) {
                ok = 0;
                break;
            }
        }
        
        if (ok) {
            prints("\033[32m[MemTest ");
            prints(pidbuf);
            prints("] mmap verification: PASS\033[0m\n");
        } else {
            prints("\033[31m[MemTest ");
            prints(pidbuf);
            prints("] mmap verification: FAIL\033[0m\n");
        }
        
        // Unmap
        if (munmap(ptr2, 8192) == 0) {
            prints("\033[32m[MemTest ");
            prints(pidbuf);
            prints("] munmap: success\033[0m\n");
        }
    }
    
    prints("\033[32m[MemTest ");
    prints(pidbuf);
    prints("] All tests complete!\033[0m\n");
    
    exit(0);
    return 0;
}