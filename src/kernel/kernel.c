/**
 * ┌─────────────────────────────────────────────────────────────────────────────┐
 * │                                   ZenOS                                     │
 * │                              Kernel Entry Point                             │
 * └─────────────────────────────────────────────────────────────────────────────┘
 * 
 * @file : /src/kernel/main.c
 * @brief : The main kernel file, brings up and manages all subsystems
 *          and drivers, then hands off control to the userspace.
 * 
 * ───────────────────────────────────────────────────────────────────────────────
 * 
 *                                  MIT License
 * 
 *                        Copyright (c) 2026 Rishies2010
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * ───────────────────────────────────────────────────────────────────────────────
 */

#include "../libk/debug/serial.h"
#include "../libk/debug/log.h"
#include "../libk/core/mem.h"
#include "../libk/core/socket.h"
#include "../libk/core/unix_sock.h"
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
#include "../drv/pit.h"
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
#include "../drv/usb/usb.h"

void _start(void)
{
    serial_init(); // Serial logging
    init_pmm();
    init_vmm();
    init_kernel_heap();
    log("\nZenOS v%s\n", 1, 0, os_version);
    enable_sse_and_fpu();
    vga_init(); // Display initializes here
    vga_boot_splash_show("Initializing kernel");
    smp_prepare(); // prep and check core needs
    vga_boot_splash_status("Initializing CPU tables");
    init_gdt_for_cpu(smp_bsp_cpu_index());
    init_idt();
    AcpiInit();
    LocalApicInit();
    init_syscalls();
    IoApicInit();
    IoApicSetIrqMapped(14, 0x2E);
    IoApicSetIrqMapped(15, 0x2F);
    IoApicSetIrqMapped(8, 0x28);
    vga_boot_splash_status("Initializing timers and PCI");
    rtc_initialize();
    sched_init();
    pci_initialize_system();
    IoApicSetIrqMapped(0, TIMER_IRQ_VECTOR);
    if (!hpet_init(200))
        pit_init(200);
    LocalApicTimerInit(200);
    input_init();
    IoApicSetIrqMapped(1, 0x21);
    init_keyboard();
    vga_boot_splash_status("Mounting boot disk");
    ata_init();
    uint8_t boot_drive = 0;
    for (int i = 0; i < 4; i++)
    {
        if (ata_drive_exists(i) == ATA_SUCCESS)
        {
            boot_drive = i; // For now first master drive found is taken 
            log("Drive selected for use: %i", 1, 0, i);
            break;
        }
    }
    if (fat_init(boot_drive) != FAT_OK)
    {
        log("Unable to mount drive. Format it? (y/*)", 2, 1); // Probably not great to just format it
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
    vga_boot_splash_status("Initializing USB"); // xHCI is better than nothing
    usb_init();
    init_smp();
    vga_boot_splash_status("Initializing Networking");
    e1000_init(); // ehh, mostly VMs only :(
    net_init();
    socket_init();
    unix_sock_init();
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
        log("\nZenOS only supports 32bpp displays right now.\n", -1, 1);
    if (vga_boot_splash_load_tga("/mnt/drv0/sys/splash.tga") < 0)
        vga_boot_splash_status("Starting apps..."); // Boot screen
    char *init_argv[] = {"kernel"};
    if (elf_exec("/mnt/drv0/bin/init", 1, init_argv) < 0)
        log("No init program found.", 0, 1);
    for(int i = 0; i < framebuffer_width; i++)
        for(int j = 0; j < framebuffer_height; j++)
            put_pixel(i, j, 0x000000);
    sched_start(); // Hand off. Kernel success!
    for (;;)
        ;
}
