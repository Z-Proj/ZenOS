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

spinlock_t loglock __attribute__((section(".data"))) = {0};
char *os_version = debug ? "0.90.0 DEBUG_ENABLED" : "0.90.0 Unstable";

void sound_err()
{
    speaker_note(0, 0);
    for (volatile int i = 0; i < 2000000; i++)
        ;
    speaker_pause();
}

static void format_time(uint64_t ms, char *out, size_t size)
{
    if (ms < 1000) {
        snprintf(out, size, "%llu ms", ms);
        return;
    }

    uint64_t s = ms / 1000;
    if (s < 60) {
        snprintf(out, size, "%llu s", s);
        return;
    }

    uint64_t m = s / 60;
    if (m < 60) {
        snprintf(out, size, "%llu min", m);
        return;
    }

    uint64_t h = m / 60;
    snprintf(out, size, "%llu h", h);
}

void log_internal(const char *file, int line, const char *fmt, int level, int visibility, ...)
{
    char *logline = kmalloc(1280);
    if (!logline)
    {
        serial_write_string("\x1b[38;2;255;50;50m[log.c]- CRITICAL: kmalloc failed in log_internal!\n");
        return;
    }

    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));

    spinlock_acquire(&loglock);

    const char *color_seq;
    int cpuid = LocalApicGetId();

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
        if (!debug)
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

    char *header = kmalloc(256);
    if (!header)
    {
        kfree(logline);
        spinlock_release(&loglock);
        if (rflags & 0x200)
            asm volatile("sti");
        return;
    }

    if (level < 1 || level > 4)
    {
        sound_err();
        snprintf(header, 256, "\nInduced Kernel Panic\n\n    - At : %s\n    - Line : %d.\n\n    - Error Log : ", file, line);
    }
    else
    {
        uint64_t ticks = rtc_get_ticks();
        uint64_t ms = (ticks * 1000) / 1024;

        char timebuf[32];
        format_time(ms, timebuf, sizeof(timebuf));

        snprintf(header, 256, "[%s][%s:%d]- ", timebuf, file, line);
    }

    char *message = kmalloc(1024);
    if (!message)
    {
        kfree(header);
        kfree(logline);
        spinlock_release(&loglock);
        if (rflags & 0x200)
            asm volatile("sti");
        return;
    }

    vsnprintf(message, 1024, fmt, args);
    va_end(args);

    char *cpuid_str = kmalloc(16);
    if (!cpuid_str)
    {
        kfree(message);
        kfree(header);
        kfree(logline);
        spinlock_release(&loglock);
        if (rflags & 0x200)
            asm volatile("sti");
        return;
    }

    if (cpuid != 0)
        snprintf(cpuid_str, 16, "[CPU%d]- ", cpuid);
    else
        cpuid_str[0] = '\0';

    strcpy(logline, header);
    strcat(logline, cpuid_str);
    strcat(logline, message);
    strcat(logline, "\n");

    serial_write_string(color_seq);
    serial_write_string(logline);
    serial_write_string("\x1b[0m");

    if (visibility == 1 || debug)
    {
        prints(color_seq);
        prints(logline);
        prints("\x1b[0m");
    }

    kfree(cpuid_str);
    kfree(message);
    kfree(header);
    kfree(logline);

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



extern void load_idt(idt_ptr_t *);

__attribute__((noreturn))
void triple_fault(void) {
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr = { 0, 0 };

    __asm__ __volatile__("cli");
    load_idt(&idtr);
    __asm__ __volatile__("ud2");

    for (;;)
        __asm__ __volatile__("hlt");
}

__attribute__((noreturn))
void shutdown(void) {
    AcpiShutdown(); //1

    triple_fault(); //2

    __asm__ __volatile__("cli");
    for (;;) {
        __asm__ __volatile__("hlt");
    } //3: Last resort

    __builtin_unreachable();
}