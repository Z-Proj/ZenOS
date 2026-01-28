#include "../drv/rtc.h"
#include "../libk/ports.h"
#include "../libk/debug/log.h"
#include "../cpu/isr.h"
#include "../kernel/sched.h"
#include <stdint.h>
#define CMOS_ADDRESS 0x70
#define CMOS_DATA 0x71
static volatile uint64_t tick = 0;
static rtc_time_t boot_time;

uint64_t rtc_get_ticks(void) {
    return tick;
}

static uint8_t read_cmos_register(uint8_t reg)
{
    outportb(CMOS_ADDRESS, reg);
    return inportb(CMOS_DATA);
}

static uint8_t bcd_to_binary(uint8_t bcd)
{
    return ((bcd / 16) * 10) + (bcd & 0x0F);
}

static void rtc_enable_periodic_updates(void)
{
    uint8_t prev = read_cmos_register(0x0B);
    outportb(CMOS_ADDRESS, 0x0B);
    outportb(CMOS_DATA, prev | 0x40);
    outportb(CMOS_ADDRESS, 0x0A);
    uint8_t rate = (read_cmos_register(0x0A) & 0xF0) | 0x06;
    outportb(CMOS_DATA, rate);
}

void rtc_interrupt_handler(registers_t *regs)
{
    (void)regs;
    tick++;
    read_cmos_register(0x0C);
}

void rtc_initialize(void)
{
    boot_time = rtc_get_time();
    rtc_enable_periodic_updates();
    log("Real Time Clock Timesystem initialized.", 4, 0);
    register_interrupt_handler(0x28, rtc_interrupt_handler, "RTC System");
}

rtc_time_t rtc_get_time(void)
{
    rtc_time_t time;
    time.seconds = bcd_to_binary(read_cmos_register(0x00));
    time.minutes = bcd_to_binary(read_cmos_register(0x02));
    time.hours = bcd_to_binary(read_cmos_register(0x04));
    time.day = bcd_to_binary(read_cmos_register(0x07));
    time.month = bcd_to_binary(read_cmos_register(0x08));
    time.year = bcd_to_binary(read_cmos_register(0x09));
    time.milliseconds = tick;
    return time;
}

uint32_t rtc_calculate_uptime(const rtc_time_t *start, const rtc_time_t *end)
{
    uint32_t start_ms = start->hours * 3600000 + start->minutes * 60000 + start->seconds * 1000 + start->milliseconds;
    uint32_t end_ms = end->hours * 3600000 + end->minutes * 60000 + end->seconds * 1000 + end->milliseconds;
    if (end_ms < start_ms)
    {
        end_ms += 24 * 3600000;
    }
    return end_ms - start_ms;
}

rtc_time_t rtc_boottime(void)
{
    return boot_time;
}

void sleep(uint32_t ms)
{
    uint64_t start_tick = tick;
    uint64_t ticks_to_wait = (ms * 1000) / 1024;
    
    while ((tick - start_tick) < ticks_to_wait)
    {
        sched_yield();
    }
}