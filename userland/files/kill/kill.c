#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { fputs("Usage: kill <pid>\n", stdout); return 1; }
    int pid = atoi(argv[1]);
    if (pid <= 0) { fputs("kill: invalid pid\n", stdout); return 1; }
    if (kill(pid, SIGKILL) != 0) {
        fputs("kill: no such process\n", stdout);
        return 1;
    }
    return 0;
}
