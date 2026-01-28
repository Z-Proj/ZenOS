/**
 * The ZenOS Kernel
 */

#include "../libk/debug/serial.h"
#include "../libk/debug/log.h"
#include "../libk/core/mem.h"
#include "../libk/core/socket.h"
#include "../libk/core/syscall.h"
#include "../libk/string.h"
#include "../libk/ports.h"
#include "../cpu/gdt.h"
#include "../cpu/idt.h"
#include "../cpu/sse_fpu.h"
#include "../cpu/isr.h"
#include "../libk/spinlock.h"
#include "../cpu/smp.h"
#include "../kernel/sched.h"
#include "../cpu/id/cpuid.h"
#include "../drv/rtc.h"
#include "../drv/hpet.h"
#include "../drv/vga.h"
#include "../drv/mouse.h"
#include "../drv/local_apic.h"
#include "../drv/ioapic.h"
#include "../drv/net/e1000.h"
#include "../drv/net/pci.h"
#include "../cpu/acpi/acpi.h"
#include "../drv/speaker.h"
#include "../libk/core/elf.h"
#include "../drv/keyboard.h"
#include "../drv/disk/ata.h"
#include "../drv/disk/zfs.h"

void play_bootup_sequence()
{
    speaker_note(3, 0);
    for (int i = 0; i < 7000000; i++)
        asm volatile("nop");
    speaker_note(3, 2);
    for (int i = 0; i < 7000000; i++)
        asm volatile("nop");
    speaker_note(3, 4);
    for (int i = 0; i < 7000000; i++)
        asm volatile("nop");
    speaker_note(4, 0);
    for (int i = 0; i < 14000000; i++)
        asm volatile("nop");
    speaker_note(4, 7);
    for (int i = 0; i < 7000000; i++)
        asm volatile("nop");
    speaker_note(5, 0);
    for (int i = 0; i < 30000000; i++)
        asm volatile("nop");
    speaker_pause();
    for(;;)__asm__ __volatile__("hlt");
}

void init(void){
    log("Starting init.", 1, 0);
    if(elf_exec("init", 0, NULL) != ZFS_OK)
        log("No init program found.", 0, 0);
}

void idle(void){
    for(;;)sched_yield();
}

void _start(void)
{
    serial_init();
    init_pmm();
    init_vmm();
    enable_sse_and_fpu();
    init_kernel_heap();
    vga_init();
    init_gdt();
    init_idt();
    init_syscalls();
    AcpiInit();
    LocalApicInit();
    IoApicInit();
    sched_init();
    rtc_initialize();
    hpet_init(100);
    ata_init();
    if(zfs_init(0) != ZFS_OK){
        log("Unable to mount filesystem. Formatting the drive...", 2, 1);
        if(zfs_format(0) != ZFS_OK){
            log("Error formatting the drive.", 3, 1);
        }
    }
    init_keyboard();
    mouse_init();
    init_smp();
    IoApicSetIrq(g_ioApicAddr, 8, 0x28, LocalApicGetId());
    IoApicSetIrq(g_ioApicAddr, 2, 0x22, LocalApicGetId());
    IoApicSetIrq(g_ioApicAddr, 0, 0x20, LocalApicGetId());
    IoApicSetIrq(g_ioApicAddr, 1, 0x21, LocalApicGetId());
    pci_initialize_system();
    e1000_init();
    socket_init();

#if debug
    log("Running In Debug Mode.", 2, 1);
    detect_cpu_info(0);
    print_mem_info(1);
    zfs_list();
#else
    task_create(play_bootup_sequence, "Bootup Music");
#endif
    task_create(idle, "Idle");
    task_create(init, "Init");
    asm volatile("sti");
    sched_start();
    for (;;)
        ;
}