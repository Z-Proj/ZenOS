#include "../userlib.h"
#include "../libs/lib.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    utsname_t u;
    if (uname(&u) != 0) {
        fputs("\033[31muname: failed\033[0m\n", stdout);
        exit(1);
    }
    fputs("\033[36m", stdout);
    fputs(u.sysname, stdout);
    fputs(" ", stdout);
    fputs(u.nodename, stdout);
    fputs(" ", stdout);
    fputs(u.release, stdout);
    fputs(" ", stdout);
    fputs(u.version, stdout);
    fputs(" ", stdout);
    fputs(u.machine, stdout);
    fputs("\033[0m\n", stdout);
    exit(0);
    return 0;
}
