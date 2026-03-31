#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "../userlib.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    pid_t pid = getpid();
    char msg[96];

    for (int i = 0; i < 8; i++)
    {
        snprintf(msg, sizeof(msg), "ulog_b pid=%d iter=%d", (int)pid, i);
        zen_log(msg, 1, 1);
        zen_sleep_ms(50);
    }

    snprintf(msg, sizeof(msg), "ulog_b pid=%d done", (int)pid);
    zen_log(msg, 1, 1);
    return 0;
}
