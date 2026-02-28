#include "../userlib.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (execv("shell", NULL) < 0) {
        prints("init: fatal: shell not found\n");
        exit(1);
    }
    
    exit(0);
    return 0;
}
