#include <errno.h>
#include <sys/mman.h>
#include <bits/ensure.h>
#include <mlibc/all-sysdeps.hpp>
#include <mlibc/tcb.hpp>

extern "C" void __mlibc_thread_trampoline(void *(*fn)(void *), Tcb *tcb, void *arg) {
	if(mlibc::sys_tcb_set(tcb))
		__ensure(!"sys_tcb_set() failed");

	while(__atomic_load_n(&tcb->tid, __ATOMIC_RELAXED) == 0)
		mlibc::sys_futex_wait(&tcb->tid, 0, nullptr);

	tcb->invokeThreadFunc(reinterpret_cast<void *>(fn), arg);

	__atomic_store_n(&tcb->didExit, 1, __ATOMIC_RELEASE);
	mlibc::sys_futex_wake(&tcb->didExit);

	mlibc::sys_thread_exit();
}

namespace mlibc {

int sys_prepare_stack(void **stack, void *entry, void *arg, void *tcb, size_t *stack_size, size_t *guard_size, void **stack_base) {
	static constexpr size_t default_stack = 0x400000;

	*guard_size = 0;
	*stack_size = *stack_size ? *stack_size : default_stack;

	if(!*stack) {
		*stack_base = mmap(nullptr, *stack_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if(*stack_base == MAP_FAILED)
			return errno;
	} else {
		*stack_base = *stack;
	}

	void **sp = reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(*stack_base) + *stack_size);
	*--sp = arg;
	*--sp = tcb;
	*--sp = entry;
	*stack = sp;
	return 0;
}

} // namespace mlibc
