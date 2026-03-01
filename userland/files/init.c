#include "../userlib.h"
#include "../libs/lib.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (execv("shell", NULL) < 0) {
        fputs("init: fatal: shell not found\n", stdout);
        exit(1);
    }
    
    exit(0);
    return 0;
}
