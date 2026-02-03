#include "../userlib.h"

// This program continuously counts and prints
// Tests: preemptive multitasking, that multiple instances don't interfere

int main(void) {
    pid_t pid = getpid();
    
    // Print startup message with our PID
    prints("\033[33m[Counter ");
    char pidbuf[16];
    int n = pid;
    int i = 0;
    if (n == 0) {
        pidbuf[i++] = '0';
    } else {
        while (n > 0) {
            pidbuf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    pidbuf[i] = '\0';
    // Reverse
    for (int j = 0; j < i/2; j++) {
        char tmp = pidbuf[j];
        pidbuf[j] = pidbuf[i-1-j];
        pidbuf[i-1-j] = tmp;
    }
    prints(pidbuf);
    prints("] Starting...\033[0m\n");
    
    // Count forever, yielding occasionally
    int count = 0;
    while (count < 20) {
        prints("\033[36m[");
        prints(pidbuf);
        prints("] Count: ");
        
        char countbuf[16];
        n = count;
        i = 0;
        if (n == 0) {
            countbuf[i++] = '0';
        } else {
            while (n > 0) {
                countbuf[i++] = '0' + (n % 10);
                n /= 10;
            }
        }
        countbuf[i] = '\0';
        for (int j = 0; j < i/2; j++) {
            char tmp = countbuf[j];
            countbuf[j] = countbuf[i-1-j];
            countbuf[i-1-j] = tmp;
        }
        prints(countbuf);
        prints("\033[0m\n");
        
        count++;
        
        // Sleep a bit then yield
        sleep(100);
        yield();
    }
    
    prints("\033[32m[Counter ");
    prints(pidbuf);
    prints("] Finished!\033[0m\n");
    
    exit(0);
    return 0;
}