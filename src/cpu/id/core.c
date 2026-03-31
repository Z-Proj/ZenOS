#include "core.h"
#include "../../libk/debug/log.h"
#include "../../drv/local_apic.h"
#include "../../kernel/sched.h"

void ap_main(void)
{
    sched_ap_entry();
}
