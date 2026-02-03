#include "../userlib.h"

// Recursive fibonacci (intentionally slow to test CPU scheduling)
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    pid_t pid = getpid();
    
    prints("\033[35m[Fibonacci PID=");
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
    prints(pidbuf);
    prints("] Computing...\033[0m\n");
    
    // Compute first 15 fibonacci numbers
    for (int num = 0; num < 15; num++) {
        int result = fib(num);
        
        prints("\033[35m[");
        prints(pidbuf);
        prints("] fib(");
        
        char numbuf[16];
        int temp = num;
        i = 0;
        do {
            numbuf[i++] = '0' + (temp % 10);
            temp /= 10;
        } while (temp > 0);
        numbuf[i] = '\0';
        for (int j = 0; j < i/2; j++) {
            char t = numbuf[j];
            numbuf[j] = numbuf[i-1-j];
            numbuf[i-1-j] = t;
        }
        prints(numbuf);
        
        prints(") = ");
        
        temp = result;
        i = 0;
        do {
            numbuf[i++] = '0' + (temp % 10);
            temp /= 10;
        } while (temp > 0);
        numbuf[i] = '\0';
        for (int j = 0; j < i/2; j++) {
            char t = numbuf[j];
            numbuf[j] = numbuf[i-1-j];
            numbuf[i-1-j] = t;
        }
        prints(numbuf);
        prints("\033[0m\n");
        
        yield();  // Give other tasks a chance
    }
    
    prints("\033[32m[Fibonacci ");
    prints(pidbuf);
    prints("] Complete!\033[0m\n");
    
    exit(0);
    return 0;
}