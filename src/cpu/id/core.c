#include "core.h"
#include "../../libk/debug/log.h"
#include "../../drv/local_apic.h"

void ap_main(void)
{
    __asm__ __volatile__("sti");
    for (;;)
        __asm__ __volatile__("hlt");
}