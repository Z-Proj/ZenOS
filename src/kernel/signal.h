/**
 * 
 * @file : /src/kernel/signal.h
 * @brief : Signal definitions and sigaction struct for userspace signal handling.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#ifndef SIGNAL_H
#define SIGNAL_H

#include <stdint.h>

#define NSIG        36

// Signal list
#define SIGHUP      1
#define SIGINT      2
#define SIGQUIT     3
#define SIGILL      4
#define SIGTRAP     5
#define SIGABRT     6
#define SIGBUS      7
#define SIGFPE      8
#define SIGKILL     9
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGSTKFLT   16
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20
#define SIGTTIN     21
#define SIGTTOU     22
#define SIGURG      23
#define SIGXCPU     24
#define SIGXFSZ     25
#define SIGVTALRM   26
#define SIGPROF     27
#define SIGWINCH    28
#define SIGIO       29
#define SIGPWR      30
#define SIGSYS      31
#define SIGRTMIN    32
#define SIGRTMAX    33
#define SIGCANCEL   34
#define SIGTIMER    35

#define SIG_DFL     ((uint64_t)0)
#define SIG_IGN     ((uint64_t)1)

typedef struct {
    uint64_t handler;
    uint64_t flags;
    uint64_t mask;
} zen_sigaction_t;

#endif
