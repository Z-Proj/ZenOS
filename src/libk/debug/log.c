/**
 * 
 * @file : /src/libk/debug/log.c
 * @brief : Kernel logging, ZenOS's Heart, Liver, whatever.
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

#include "log.h"
#include <stddef.h>
#include <stdarg.h>
#include "serial.h"
#include "../../drv/vga.h"
#include "../ports.h"
#include "../../cpu/acpi/acpi.h"
#include "../../cpu/idt.h"
#include "../string.h"
#include "../core/mem.h"
#include "../../drv/local_apic.h"
#include "../../drv/speaker.h"
#include "../../drv/rtc.h"
#include "../../drv/net/e1000.h"
#include "../../drv/ioapic.h"
#include "../../drv/disk/fatfs/ff.h"
#include "../../kernel/sched.h"

spinlock_t loglock __attribute__((section(".data"))) = {0};
char *os_version = env == 0 ? "1.0.0 (Stable)" : (env == 1 ? "1.0.0 Developer" : "1.0.0 Debug (Unstable)");

void sound_err()
{
    speaker_note(0, 0);
    for (volatile int i = 0; i < 2000000; i++)
        ;
    speaker_pause();
}

void log_internal(const char *file, int line, const char *fmt, int level, int visibility, ...)
{
    char header[256];
    char message[1024];
    char cpuid_str[16];
    char logline[1408];
    uint64_t cpuid = LocalApicGetId();
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));

    spinlock_acquire(&loglock);

    const char *color_seq;

    switch (level)
    {
    case 1:
        color_seq = "\x1b[38;2;150;150;150m";
        break;
    case 2:
        color_seq = "\x1b[38;2;255;90;0m";
        break;
    case 3:
        color_seq = "\x1b[38;2;255;50;50m";
        if (env != 0 || env != 1)
            sound_err();
        break;
    case 4:
        color_seq = "\x1b[38;2;50;255;50m";
        break;
    default:
        color_seq = "\x1b[38;2;255;50;50m";
        break;
    }

    va_list args;
    va_start(args, visibility);

    if (level < 1 || level > 4)
    {
        sound_err();
        snprintf(header, sizeof(header), "\nInduced Kernel Panic\n\n    - At : %s\n    - Line : %d.\n\n    - Error Log : ", file, line);
    }
    else
    {
        const char *filename = file;
        const char *slash = strrchr(file, '/');
        if (slash)
        {
            filename = slash + 1;
        }
        snprintf(header, sizeof(header), "[%s:%d]- ", filename, line);
    }

    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (cpuid != 0)
        snprintf(cpuid_str, sizeof(cpuid_str), "[CPU%d]- ", cpuid);
    else
        cpuid_str[0] = '\0';

    snprintf(logline, sizeof(logline), "%s%s%s\n", header, cpuid_str, message);

    serial_write_string(color_seq);
    serial_write_string(logline);
    serial_write_string("\x1b[0m");

    if (visibility == 1){
        switch (env){
            case 0 : {
                break;
            }
            default : {
                prints(color_seq);
                prints(logline);
                prints("\x1b[0m");
            }
        }
    }

    spinlock_release(&loglock);

    if (rflags & 0x200)
        asm volatile("sti");

    if (level < 1 || level > 4)
    {
        __asm__ __volatile__("cli");
        for (;;)
            __asm__ __volatile__("hlt");
    }
}

__attribute__((noreturn)) void shutdown(void)
{

    __asm__ __volatile__("cli" ::: "memory");
    log("Shutting down...", 4, 1);
    ft_run(false);
    log("4...", 2, 0);
    task_t *tlist = sched_get_task_list();
    if (tlist)
    {
        task_t *t = tlist;
        do
        {
            if (t->state != TASK_DEAD)
                t->state = TASK_DEAD;
            t = t->next;
        } while (t != tlist);
    }

    log("3...", 2, 0);
    for (int i = 0; i < 4; i++)
    {
        char path[4] = {'0' + i, ':', '/', 0};
        f_mount(NULL, path, 0);
    }

    log("2...", 2, 0);
    e1000_disable_interrupts();

    log("1...", 2, 0);
    extern uint8_t *g_ioApicAddr;
    for (int i = 0; i < 24; i++)
        IoApicSetEntry(g_ioApicAddr, i, 1 << 16);

    log("Powering off...", 4, 1);
    AcpiShutdown();

    for (;;)
        __asm__ __volatile__("hlt");
}
