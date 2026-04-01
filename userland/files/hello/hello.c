#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    static const char msg[] = "\033[32mHello world!\033[0m\n";
    write(1, msg, sizeof(msg) - 1);
    return 0;
}
