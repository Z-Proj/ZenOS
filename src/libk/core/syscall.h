#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYSCALL_EXEC        0
#define SYSCALL_EXIT        1
#define SYSCALL_GETKEY      2
#define SYSCALL_PRINTS      3
#define SYSCALL_MOUSE_X     4
#define SYSCALL_MOUSE_Y     5
#define SYSCALL_MOUSE_BTN   6
#define SYSCALL_SPEAKER     7
#define SYSCALL_SPEAKER_OFF 8
#define SYSCALL_OPEN        9
#define SYSCALL_READ        10
#define SYSCALL_WRITE       11
#define SYSCALL_CLOSE       12
#define SYSCALL_SEEK        13
#define SYSCALL_CREATE      14
#define SYSCALL_DELETE      15
#define SYSCALL_LOG         16
#define SYSCALL_SLEEP       17
#define SYSCALL_SHUTDOWN    18

void init_syscalls(void);
uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

#endif