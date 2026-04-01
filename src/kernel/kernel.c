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
#include "../drv/input.h"
#include "../drv/mouse.h"
#include "../drv/local_apic.h"
#include "../drv/ioapic.h"
#include "../drv/net/e1000.h"
#include "../drv/net/pci.h"
#include "../drv/net/net.h"
#include "../cpu/acpi/acpi.h"
#include "../drv/speaker.h"
#include "../libk/core/elf.h"
#include "../drv/keyboard.h"
#include "../drv/disk/ata.h"
#include "../drv/disk/fat.h"
#include "../drv/disk/vfs.h"

void _start(void)
{
    serial_init();
    init_pmm();
    init_vmm();
    init_kernel_heap();
    enable_sse_and_fpu();
    vga_init();
    smp_prepare();
    init_gdt_for_cpu(smp_bsp_cpu_index());
    init_idt();
    AcpiInit();
    LocalApicInit();
    init_syscalls();
    IoApicInit();
    IoApicSetIrqMapped(14, 0x2E);
    IoApicSetIrqMapped(15, 0x2F);
    IoApicSetIrqMapped(8, 0x28);
    rtc_initialize();
    sched_init();
    pci_initialize_system();
    IoApicSetIrqMapped(0, 0x22);
    hpet_init(200);
    LocalApicTimerInit(200);
    input_init();
    IoApicSetIrqMapped(1, 0x21);
    init_keyboard();
    ata_init();
    uint8_t boot_drive = 0;
    for (int i = 0; i < 4; i++)
    {
        if (ata_drive_exists(i) == ATA_SUCCESS)
        {
            boot_drive = i;
            log("Drive selected for use: %i", 1, 0, i);
            break;
        }
    }
    if (fat_init(boot_drive) != FAT_OK)
    {
        log("Unable to mount FAT. Format drive? (y/*)", 2, 1);
        __asm__ __volatile__("sti");
        char flag = wait_for_key();
        __asm__ __volatile__("cli");
        if (flag == 'y' || flag == 'Y')
        {
            log("Formatting drive %d...", 1, 0, boot_drive);
            fat_format(boot_drive);
        }
        else
        {
            log("Leaving drive unattached.", 1, 0);
        }
    }
    IoApicSetIrqMapped(12, 0x2C);
    mouse_init();
    init_smp();
    e1000_init();
    net_init();
    socket_init();
#if debug
    log("Running In Debug Mode.", 2, 1);
    detect_cpu_info(0);
    print_mem_info(1);
    char fat_debug_buf[4096];
    fat_list(fat_debug_buf, sizeof(fat_debug_buf));
    log(fat_debug_buf, 1, 1);
#endif
    vfs_init();
    input_register_devfs();
    if (!(framebuffer_bpp == 32))
        log("\nZenOS only supports 32bpp displays right now.\n", 2, 1);
    char *init_argv[] = {"kernel"};
    if (elf_exec("/mnt/drv0/bin/init", 1, init_argv) < 0)
        log("No init program found.", 0, 1);
    kbd_init_focus();
    sched_start();
    for (;;)
        ;
}
