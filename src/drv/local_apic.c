/**
 * 
 * @file : /src/drv/local_apic.c
 * @brief : Local APIC setup - timer calibration, IPIs, etc.
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

#include "local_apic.h"
#include "../cpu/idt.h"
#include "../cpu/isr.h"
#include "../cpu/acpi/acpi.h"
#include "../drv/hpet.h"
#include "../kernel/sched.h"
#include "../libk/debug/log.h"

uint8_t *g_localApicAddr = (uint8_t*)0xFEE00000;

#define LAPIC_ID                        0x0020  // Local APIC ID
#define LAPIC_VER                       0x0030  // Local APIC Version
#define LAPIC_TPR                       0x0080  // Task Priority
#define LAPIC_APR                       0x0090  // Arbitration Priority
#define LAPIC_PPR                       0x00a0  // Processor Priority
#define LAPIC_EOI                       0x00b0  // EOI
#define LAPIC_RRD                       0x00c0  // Remote Read
#define LAPIC_LDR                       0x00d0  // Logical Destination
#define LAPIC_DFR                       0x00e0  // Destination Format
#define LAPIC_SVR                       0x00f0  // Spurious Interrupt Vector
#define LAPIC_ISR                       0x0100  // In-Service (8 registers)
#define LAPIC_TMR                       0x0180  // Trigger Mode (8 registers)
#define LAPIC_IRR                       0x0200  // Interrupt Request (8 registers)
#define LAPIC_ESR                       0x0280  // Error Status
#define LAPIC_ICRLO                     0x0300  // Interrupt Command
#define LAPIC_ICRHI                     0x0310  // Interrupt Command [63:32]
#define LAPIC_TIMER                     0x0320  // LVT Timer
#define LAPIC_THERMAL                   0x0330  // LVT Thermal Sensor
#define LAPIC_PERF                      0x0340  // LVT Performance Counter
#define LAPIC_LINT0                     0x0350  // LVT LINT0
#define LAPIC_LINT1                     0x0360  // LVT LINT1
#define LAPIC_ERROR                     0x0370  // LVT Error
#define LAPIC_TICR                      0x0380  // Initial Count (for Timer)
#define LAPIC_TCCR                      0x0390  // Current Count (for Timer)
#define LAPIC_TDCR                      0x03e0  // Divide Configuration (for Timer)

#define LAPIC_TIMER_VECTOR              IRQ3
#define LAPIC_TIMER_PERIODIC            (1u << 17)
#define LAPIC_TIMER_MASKED              (1u << 16)
#define LAPIC_TIMER_DIVIDE_16           0x3


#define ICR_FIXED                       0x00000000
#define ICR_LOWEST                      0x00000100
#define ICR_SMI                         0x00000200
#define ICR_NMI                         0x00000400
#define ICR_INIT                        0x00000500
#define ICR_STARTUP                     0x00000600

#define ICR_PHYSICAL                    0x00000000
#define ICR_LOGICAL                     0x00000800

#define ICR_IDLE                        0x00000000
#define ICR_SEND_PENDING                0x00001000

#define ICR_DEASSERT                    0x00000000
#define ICR_ASSERT                      0x00004000

#define ICR_EDGE                        0x00000000
#define ICR_LEVEL                       0x00008000

#define ICR_NO_SHORTHAND                0x00000000
#define ICR_SELF                        0x00040000
#define ICR_ALL_INCLUDING_SELF          0x00080000
#define ICR_ALL_EXCLUDING_SELF          0x000c0000

#define ICR_DESTINATION_SHIFT           24


static uint32_t LocalApicIn(int reg)
{
    return *(volatile uint32_t *)(g_localApicAddr + reg);
}


static void LocalApicOut(int reg, uint32_t data)
{
    *(volatile uint32_t *)(g_localApicAddr + reg) = data;
}

static void lapic_timer_handler(registers_t *regs)
{
    sched_tick(regs);
}

static uint32_t lapic_timer_calibrate(uint32_t frequency_hz)
{
    if (!frequency_hz)
        return 0;

    LocalApicOut(LAPIC_TDCR, LAPIC_TIMER_DIVIDE_16);
    LocalApicOut(LAPIC_TIMER, LAPIC_TIMER_MASKED | LAPIC_TIMER_VECTOR);
    LocalApicOut(LAPIC_TICR, 0xffffffffu);

    uint64_t start_ns = hpet_monotonic_ns();
    while ((hpet_monotonic_ns() - start_ns) < 10000000ULL)
        __asm__ volatile("pause" ::: "memory");

    uint32_t elapsed = 0xffffffffu - LocalApicIn(LAPIC_TCCR);
    uint32_t initial_count = (uint32_t)(((uint64_t)elapsed * 100ULL) / frequency_hz);
    if (initial_count < 16)
        initial_count = 16;

    LocalApicOut(LAPIC_TIMER, LAPIC_TIMER_MASKED | LAPIC_TIMER_VECTOR);
    LocalApicOut(LAPIC_TICR, 0);

    return initial_count;
}


void LocalApicInit()
{
    LocalApicOut(LAPIC_TPR, 0);

    LocalApicOut(LAPIC_DFR, 0xffffffff);
    LocalApicOut(LAPIC_LDR, 0x01000000);

    LocalApicOut(LAPIC_SVR, 0x100 | 0xff);
    log("Local APIC Initialized.", 4, 0);
}

void LocalApicTimerInit(uint32_t frequency_hz)
{
    uint32_t initial_count = lapic_timer_calibrate(frequency_hz);
    if (!initial_count)
        return;

    register_interrupt_handler(LAPIC_TIMER_VECTOR, lapic_timer_handler, "LAPIC Timer");
    LocalApicOut(LAPIC_TDCR, LAPIC_TIMER_DIVIDE_16);
    LocalApicOut(LAPIC_TIMER, LAPIC_TIMER_VECTOR | LAPIC_TIMER_PERIODIC);
    LocalApicOut(LAPIC_TICR, initial_count);
}


int LocalApicGetId()
{
    return LocalApicIn(LAPIC_ID) >> 24;
}


void LocalApicSendInit(int apic_id)
{
    LocalApicOut(LAPIC_ICRHI, apic_id << ICR_DESTINATION_SHIFT);
    LocalApicOut(LAPIC_ICRLO, ICR_INIT | ICR_PHYSICAL
        | ICR_ASSERT | ICR_EDGE | ICR_NO_SHORTHAND);

    while (LocalApicIn(LAPIC_ICRLO) & ICR_SEND_PENDING)
        ;
}


void LocalApicSendStartup(int apic_id, int vector)
{
    LocalApicOut(LAPIC_ICRHI, apic_id << ICR_DESTINATION_SHIFT);
    LocalApicOut(LAPIC_ICRLO, vector | ICR_STARTUP
        | ICR_PHYSICAL | ICR_ASSERT | ICR_EDGE | ICR_NO_SHORTHAND);

    while (LocalApicIn(LAPIC_ICRLO) & ICR_SEND_PENDING)
        ;
}

void LocalApicSendFixed(int apic_id, int vector)
{
    LocalApicOut(LAPIC_ICRHI, apic_id << ICR_DESTINATION_SHIFT);
    LocalApicOut(LAPIC_ICRLO, (uint32_t)vector | ICR_FIXED
        | ICR_PHYSICAL | ICR_ASSERT | ICR_EDGE | ICR_NO_SHORTHAND);

    while (LocalApicIn(LAPIC_ICRLO) & ICR_SEND_PENDING)
        ;
}

void LocalApicBroadcastFixed(int vector)
{
    LocalApicOut(LAPIC_ICRHI, 0);
    LocalApicOut(LAPIC_ICRLO, (uint32_t)vector | ICR_FIXED
        | ICR_PHYSICAL | ICR_ASSERT | ICR_EDGE | ICR_ALL_EXCLUDING_SELF);

    while (LocalApicIn(LAPIC_ICRLO) & ICR_SEND_PENDING)
        ;
}

void LocalApicSendEOI() {
    *((volatile uint32_t*)(g_localApicAddr + 0xB0)) = 0;
}
