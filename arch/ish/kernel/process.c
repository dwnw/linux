#include <linux/kallsyms.h>
#include <linux/moduleparam.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <asm/ldt.h>
#include <asm/mmu_context.h>
#include <asm/ptrace.h>
#include <asm/syscall.h>
#include <emu/exec.h>
#include <user/user.h>
#include "threads_user.h"
#include "irq_user.h"

#include <asm/unistd.h>

void show_regs(struct pt_regs *regs)
{
	printk("<regs would go here>\n");
}

void start_thread(struct pt_regs *regs, unsigned long entry, unsigned long stack)
{
	if (is_compat_task()) {
		regs->ip = entry;
		regs->sp = stack;
	} else {
		regs->pc = entry;
		regs->sp = stack;
		regs->pstate = PSR_MODE_EL0t;
		regs->regs[0] = 0;
	}
}
EXPORT_SYMBOL(start_thread);
void flush_thread(void)
{
}

/* TODO put this in a header */
extern int handle_page_fault(unsigned long address, int is_write, int *code_out);
extern int handle_page_fault_emu(struct emu_mm *emu_mm, unsigned long address,
				 int is_write, int *code_out);

int show_unhandled_signals = 1; /* TODO this may not be the place */
static void show_signal(struct task_struct *task, const char *desc, unsigned long addr) {
	if (show_unhandled_signals) {
		struct pt_regs *regs = task_pt_regs(task);
		printk("%s[%d] %s addr:%lx ip:%lx sp:%lx\n", task->comm,
		       task_pid_nr(task), desc, addr, instruction_pointer(regs), regs->sp);
	}
}

struct pt_regs *emu_pt_regs(struct emu *emu)
{
	return &container_of(emu, struct thread_struct, emu)->regs;
}

struct task_struct *emu_task(struct emu *emu)
{
	struct thread_struct *thread = container_of(emu, struct thread_struct, emu);
	return container_of(thread, struct task_struct, thread);
}

/* On Darwin `current` (pthread-specific) can be stale across iSH's longjmp
 * stack moves. Set it to the task whose emu we are about to run. */
void emu_set_current(struct emu *emu)
{
	__set_current(emu_task(emu));
}

/* The task's current mm (thread.emu.mm is not updated across execve). */
struct emu_mm *emu_current_mm(struct emu *emu)
{
	struct task_struct *t = emu_task(emu);
	if (t->mm)
		return &t->mm->context.emu_mm;
	if (t->active_mm)
		return &t->active_mm->context.emu_mm;
	return NULL;
}

void *emu_fpsimd(struct emu *emu)
{
	return &container_of(emu, struct thread_struct, emu)->fpsimd;
}

static bool log_syscalls;
core_param(log_syscalls, log_syscalls, bool, 0644);

void handle_cpu_trap(struct emu *emu)
{
	struct pt_regs *regs = emu_pt_regs(emu);
	unsigned long run_time_us;

	/* This task's emu is what is running; make `current` agree even if a
	 * prior schedule() on this thread left it pointing at another task. */
	__set_current(emu_task(emu));
	run_time_us = ktime_to_us(ktime_sub(ktime_get(), current->thread.last_trap_time));
	if (run_time_us > 50000)
		pr_warn("emulator ran for %luus!", run_time_us);

	check_irqs();

	if (is_compat_task()) {
		regs->orig_ax = regs->ax;
	} else if (regs->trap_nr != A64_TRAP_SVC) {
		/* Not a syscall: make sure restart logic does not misfire. */
		regs->syscallno = -1;
	}

	if (regs->trap_nr == 6) {
		/* undefined instruction */
		char buf[16];
		show_signal(current, "invalid opcode", regs->ip);
		if (copy_from_user_nofault(buf, (void * __user) regs->ip, sizeof(buf)) == 0)
			printk("%s[%d] invalid code: %16ph\n", current->comm, task_pid_nr(current), buf);
		force_sig_fault(SIGILL, SI_KERNEL, (void __user *) regs->ip);
	} else if (regs->trap_nr == 13 || regs->trap_nr == 14) {
		/* GPF or page fault */
		int code;
		int err = handle_page_fault(regs->cr2, regs->error_code & 2, &code);
		if (err != 0) {
			show_signal(current, "page fault", regs->cr2);
			force_sig_fault(SIGSEGV, code, (void __user *) regs->cr2);
		}
	} else if (regs->trap_nr == 3) {
		/* int3 */
		force_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *) regs->ip);
	} else if (regs->trap_nr == 0x80) {
		/* syscall */
		unsigned long (*syscall)(unsigned long, unsigned long,
					 unsigned long, unsigned long,
					 unsigned long, unsigned long);

		if (regs->orig_ax > NR_syscalls) {
			show_signal(current, "syscall out of range", regs->orig_ax);
			force_sig_fault(SIGSYS, SI_KERNEL, 0);
			goto signal;
		}
		syscall = sys_call_table[regs->orig_ax];

		if (trace_syscall_enter(regs))
			goto signal;
		regs->ax = syscall(regs->bx, regs->cx, regs->dx,
				   regs->si, regs->di, regs->bp);
		if (log_syscalls) {
			printk("%s[%d] syscall %d(%#x, %#x, %#x) -> %d\n",
			       current->comm, current->pid,
			       regs->orig_ax, regs->bx, regs->cx,
			       regs->dx, regs->ax);
		}
		trace_syscall_exit(regs);
	} else if (regs->trap_nr == A64_TRAP_SVC) {
		/* AArch64 syscall: number in x8, args in x0..x5, result in x0. */
		unsigned long (*syscall)(unsigned long, unsigned long,
					 unsigned long, unsigned long,
					 unsigned long, unsigned long);

		if (regs->syscallno < 0 ||
		    (unsigned long) regs->syscallno >= sys_call_table64_len) {
			show_signal(current, "syscall out of range", regs->syscallno);
			force_sig_fault(SIGSYS, SI_KERNEL, 0);
			goto signal;
		}
		syscall = sys_call_table64[regs->syscallno];
		if (log_syscalls)
			printk("a64[%d] svc %ld x0=%#lx x1=%#lx x2=%#lx pc=%#lx\n",
			       task_pid_nr(current), regs->syscallno, regs->regs[0],
			       regs->regs[1], regs->regs[2], regs->pc);
		regs->orig_x0 = regs->regs[0];	/* saved for restart */
		if (trace_syscall_enter(regs))
			goto signal;
		regs->regs[0] = syscall(regs->regs[0], regs->regs[1],
					regs->regs[2], regs->regs[3],
					regs->regs[4], regs->regs[5]);
		trace_syscall_exit(regs);
	} else if (regs->trap_nr == A64_TRAP_DATA_ABORT) {
		int code;
		int err = handle_page_fault_emu(emu->mm, regs->far,
						!!(regs->esr & ESR_EL1_WNR), &code);
		if (err != 0) {
			show_signal(current, "data abort", regs->far);
			force_sig_fault(SIGSEGV, code, (void __user *) regs->far);
		}
	} else if (regs->trap_nr == A64_TRAP_INSN_ABORT) {
		int code;
		int err = handle_page_fault_emu(emu->mm, regs->far, 0, &code);
		if (err != 0) {
			show_signal(current, "insn abort", regs->far);
			force_sig_fault(SIGSEGV, code, (void __user *) regs->far);
		}
	} else if (regs->trap_nr == A64_TRAP_UNDEF) {
		show_signal(current, "undefined", regs->far);
		force_sig_fault(SIGILL, SI_KERNEL, (void __user *) regs->far);
	} else if (regs->trap_nr == 0x20) {
		/* Periodic preemption quantum / poke. This is a safe point, so
		 * force a reschedule: otherwise a CPU-bound guest thread that
		 * never traps (e.g. a spinlock) would starve its siblings, since
		 * the host runs one guest thread per task. */
		set_tsk_need_resched(current);
	} else {
		show_signal(current, "mysterious interrupt", regs->trap_nr);
		force_sig_fault(SIGSEGV, SI_KERNEL, 0);
	}

signal:
	if (need_resched())
		schedule();
	do_signal(regs);

	current->thread.last_trap_time = ktime_get();
}

static void __user_thread(void)
{
	emu_run(&current->thread.emu);
}

static void __kernel_thread(struct task_struct *last)
{
	schedule_tail(last);
	if (current->thread.request.func)
		current->thread.request.func(current->thread.request.arg);
	__user_thread();
}

/* TODO put this in a header */
int do_set_thread_area(struct task_struct *task, struct user_desc __user *u_info);

int copy_thread(unsigned long clone_flags, unsigned long usp,
		unsigned long arg, struct task_struct *p, unsigned long tls)
{
	*task_pt_regs(p) = *current_pt_regs();
	/* Child returns 0 from fork/clone in its ABI's return register. */
	if (is_compat_task())
		task_pt_regs(p)->ax = 0;
	else
		task_pt_regs(p)->regs[0] = 0;
	if (clone_flags & CLONE_SETTLS) {
		if (is_compat_task()) {
			int err = do_set_thread_area(p, (struct user_desc __user *) tls);
			if (err < 0)
				return err;
		} else {
			/* AArch64: the clone tls argument is the thread pointer
			 * itself, which lives in tpidr_el0 (no LDT). */
			task_pt_regs(p)->tpidr_el0 = tls;
		}
	}
	if (usp) {
		task_pt_regs(p)->sp = usp;
	}
	if (p->thread.emu.ctx != NULL) {
		emu_finish_fork(&p->thread.emu);
	}

	KSTK_ESP(p) = (unsigned long) task_stack_page(p) + THREAD_SIZE - sizeof(void *);
#ifdef __aarch64__
	// AAPCS requires that that stack is quadword aligned.
	KSTK_ESP(p) &= ~0xf;
#endif
	KSTK_EIP(p) = (unsigned long) __kernel_thread;
	p->thread.request.func = NULL;
	if (unlikely(p->flags & PF_KTHREAD)) {
		p->thread.request.func = (void (*)(void *)) usp;
		p->thread.request.arg = (void (*)(void *)) arg;
	}
	return 0;
}

void *__switch_to(struct task_struct *from, struct task_struct *to)
{
	struct task_struct *last;

	BUG_ON(!from);
	__set_current(to);
	last = (void *) ksetjmp(from->thread.kernel_regs);
	if (last == NULL)
		klongjmp(to->thread.kernel_regs, (unsigned long) from);
	/* switch_mm(last->mm, current->mm, current); */
	return last;
}

void release_thread(struct task_struct *task) {
	emu_destroy(&task->thread.emu);
}

unsigned long init_stack[THREAD_SIZE / sizeof(unsigned long)];
