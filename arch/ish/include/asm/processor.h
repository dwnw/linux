#ifndef __ASM_ISH_PROCESSOR_H
#define __ASM_ISH_PROCESSOR_H

#ifndef __ASSEMBLY__

#include <linux/ktime.h>
#include <asm/ptrace.h>
#include <emu/exec.h>
#include <user/setjmp.h>

struct task_struct;

extern void cpu_relax(void);

static inline unsigned long __get_wchan(struct task_struct *p)
{
	// TODO
	return 0;
}

extern void release_thread(struct task_struct *task);

struct thread_struct {
	struct emu emu;
	struct pt_regs regs;
	kjmp_buf kernel_regs;
	struct {
		void (*func)(void *);
		void *arg;
	} request;
	ktime_t last_trap_time;
};
#define INIT_THREAD \
{ \
}

/*
 * Native (64-bit/AArch64) user VA limit. Page tables cover this range for
 * every task regardless of ABI (USER_PTRS_PER_PGD is derived from it), so
 * allocation and teardown are ABI-independent.
 */
#define TASK_SIZE_MAX	0x00007ffffffff000UL

/*
 * Per-task user VA limit. i386 (compat) tasks keep the historical 4 GiB
 * layout; native 64-bit tasks get the full 47-bit range. Kernel threads have
 * TIF_32BIT clear and therefore see TASK_SIZE_MAX.
 */
#define TASK_SIZE	(is_compat_task() ? 0xffffe000UL : TASK_SIZE_MAX)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 3))

#define KSTK_ESP(t)	((t)->thread.kernel_regs->jb_sp)
#define KSTK_EIP(t)	((t)->thread.kernel_regs->jb_ip)
#define task_pt_regs(t)	(&(t)->thread.regs)

typedef struct {
	unsigned long seg;
} mm_segment_t;

/* Do necessary setup to start up a newly executed thread. */
extern void start_thread(struct pt_regs *regs, unsigned long entry, 
			 unsigned long stack);

#define STACK_TOP	TASK_SIZE
/*
 * bprm_mm_init() runs before SET_PERSONALITY2(), and forked children already
 * carry the inherited ABI flag, so STACK_TOP_MAX can follow the per-task
 * TASK_SIZE. This avoids a pointless (and expensive) 47-bit -> 32-bit
 * stack shift for i386 execs.
 */
#define STACK_TOP_MAX	TASK_SIZE

#endif

#endif
