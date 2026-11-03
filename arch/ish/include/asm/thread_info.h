#ifndef __ASM_ISH_THREAD_INFO_H
#define __ASM_ISH_THREAD_INFO_H

#include <asm/types.h>
#include <asm/processor.h>

#define THREAD_SIZE_ORDER (3)
#define THREAD_SIZE (PAGE_SIZE << (THREAD_SIZE_ORDER))

#ifndef __ASSEMBLY__

// TODO is all this necessary?
struct thread_info {
	unsigned long		flags;		/* low level flags */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	mm_segment_t		addr_limit;	/* thread address space */
#ifdef CONFIG_SMP
	int			cpu;
#endif
};
#define INIT_THREAD_INFO(tsk) \
{ \
	.addr_limit	= KERNEL_DS, \
}

#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_NOTIFY_SIGNAL	3	/* signal notifications exist */
#define TIF_NOTIFY_RESUME	4	/* callback before returning to user */
#define TIF_MEMDIE		5	/* is terminating due to OOM killer */
#define TIF_64BIT		6	/* native 64-bit (AArch64) ABI task */

#ifndef __ASSEMBLY__
/* Tasks are 32-bit (compat) unless they exec a 64-bit ELF and set TIF_64BIT.
 * Defaulting to compat keeps the first exec (kernel_init, before it knows the
 * binary class) and all kernel threads within the 4 GiB x86 address space;
 * the PUD self-test marks its own kthread native explicitly. */
#define is_compat_task()	(!test_thread_flag(TIF_64BIT))
#endif

#endif

#endif
