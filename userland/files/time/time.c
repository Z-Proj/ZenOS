#include <stdio.h>
#include <sys/time.h>

int main(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        fputs("time: failed\n", stdout);
        return 1;
    }
    printf("%lld.%06lld seconds since epoch\n",
           (long long)tv.tv_sec, (long long)tv.tv_usec);
    return 0;
}
