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

#ifdef CONFIG_ISH_PUD_SELFTEST
/* Test-only: native 47-bit user VA so the PUD self-test can reach >4 GiB. */
#define TASK_SIZE 0x00007ffffffff000UL
#else
#define TASK_SIZE 0xffffe000
#endif

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
#define STACK_TOP_MAX	STACK_TOP

#endif

#endif
