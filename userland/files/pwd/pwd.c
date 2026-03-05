#include <stdio.h>
#include <unistd.h>

int main(void) {
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        fputs(cwd, stdout);
        fputs("\n", stdout);
        return 0;
    }
    fputs("pwd: failed\n", stdout);
    return 1;
}
