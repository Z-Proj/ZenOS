#include "../userlib.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    prints("\033[32mHello from ZenOS userland!\033[0m\n");
    exit(0);
    return 0;
}
