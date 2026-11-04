#ifndef __ASM_ISH_SYSCALL_H
#define __ASM_ISH_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/thread_info.h>
#include <linux/err.h>

extern void *sys_call_table[];
extern void *sys_call_table64[];
extern const unsigned int sys_call_table64_len;

/* Which ABI does this task use? (per-task TIF_64BIT from the ELF class) */
static inline bool is_task_a64(struct task_struct *task)
{
	return test_tsk_thread_flag(task, TIF_64BIT);
}

/*
 * Syscall accessors. i386 keeps the number in orig_ax and args in
 * bx/cx/dx/si/di/bp; AArch64 keeps the number in x8 (syscallno) and args in
 * x0..x5, with the return value in x0. All generic code goes through these
 * so seccomp/tracing/restart work for both ABIs.
 */
static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	if (is_task_a64(task))
		return regs->syscallno;
	return regs->orig_ax;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long ret = is_task_a64(task) ? regs->regs[0] : regs->ax;
	return IS_ERR_VALUE(ret) ? ret : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return is_task_a64(task) ? regs->regs[0] : regs->ax;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	unsigned long ret = (error && error != -ENOSYS) ? (unsigned long) error
							: (unsigned long) val;
	if (is_task_a64(task))
		regs->regs[0] = ret;
	else
		regs->ax = ret;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	if (is_task_a64(task)) {
		args[0] = regs->regs[0];
		args[1] = regs->regs[1];
		args[2] = regs->regs[2];
		args[3] = regs->regs[3];
		args[4] = regs->regs[4];
		args[5] = regs->regs[5];
	} else {
		args[0] = regs->bx;
		args[1] = regs->cx;
		args[2] = regs->dx;
		args[3] = regs->si;
		args[4] = regs->di;
		args[5] = regs->bp;
	}
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	if (is_task_a64(task)) {
		regs->regs[0] = args[0];
		regs->regs[1] = args[1];
		regs->regs[2] = args[2];
		regs->regs[3] = args[3];
		regs->regs[4] = args[4];
		regs->regs[5] = args[5];
	} else {
		regs->bx = args[0];
		regs->cx = args[1];
		regs->dx = args[2];
		regs->si = args[3];
		regs->di = args[4];
		regs->bp = args[5];
	}
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return is_task_a64(task) ? AUDIT_ARCH_AARCH64 : AUDIT_ARCH_I386;
}

#endif
