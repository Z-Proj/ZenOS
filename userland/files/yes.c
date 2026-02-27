#include "../userlib.h"

int main(int argc, char *argv[]) {
    if (argc > 1) {
        while (1) {
            for (int i = 1; i < argc; i++) {
                prints(argv[i]);
                if (i < argc - 1)
                    prints(" ");
            }
            prints("\n");
            yield();
        }
    } else {
        while (1) {
            prints("y\n");
            yield();
        }
    }
    return 0;
}
