#include <stdio.h>
#include "../../userlib.h"

int main(void) {
    task_info_t tasks[32];
    int count = zen_list_tasks(tasks, 32);
    fputs("  PID  NAME\n", stdout);
    for (int i = 0; i < count; i++)
        printf("%5d  %s\n", (int)tasks[i].pid, tasks[i].name);
    return 0;
}
