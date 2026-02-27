#include "../userlib.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    utsname_t u;
    if (uname(&u) != 0) {
        prints("\033[31muname: failed\033[0m\n");
        exit(1);
    }
    prints("\033[36m");
    prints(u.sysname);
    prints(" ");
    prints(u.nodename);
    prints(" ");
    prints(u.release);
    prints(" ");
    prints(u.version);
    prints(" ");
    prints(u.machine);
    prints("\033[0m\n");
    exit(0);
    return 0;
}
