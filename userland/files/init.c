#include "../userlib.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (exec("gfxserver") < 0)
        prints("init: warning: gfxserver not found\n");

    if (execv("shell", NULL) < 0) {
        prints("init: fatal: shell not found\n");
        exit(1);
    }
    
    exit(0);
    return 0;
}