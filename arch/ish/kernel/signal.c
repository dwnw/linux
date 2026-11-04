#include <linux/compat.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/signal_types.h>
#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/sighandling.h>
#include <asm/signal.h>
#include <asm/syscall.h>
#include <asm/unistd.h>

extern int ia32_setup_rt_frame(int sig, struct ksignal *ksig,
			compat_sigset_t *set, struct pt_regs *regs);
extern int ia32_setup_frame(int sig, struct ksignal *ksig,
		     compat_sigset_t *set, struct pt_regs *regs);
extern int setup_rt_frame_a64(int sig, struct ksignal *ksig,
			      sigset_t *set, struct pt_regs *regs);

static void set_retval_eintr(struct pt_regs *regs)
{
	if (is_compat_task())
		regs->ax = -EINTR;
	else
		regs->regs[0] = -EINTR;
}

/*
 * Rewind to just before the syscall instruction and restore the first
 * argument so the syscall re-executes. i386 uses int 0x80 (2 bytes) and
 * restores ax from orig_ax; AArch64 uses svc (4 bytes) and restores x0 from
 * orig_x0. The restart_syscall NR differs too (0 vs 128).
 */
static void rewind_syscall(struct pt_regs *regs, bool restartblock)
{
	if (is_compat_task()) {
		regs->ax = restartblock ? __NR_restart_syscall : regs->orig_ax;
		regs->ip -= 2;
	} else {
		regs->regs[0] = restartblock ? 128 /* generic __NR_restart_syscall */
					     : regs->orig_x0;
		regs->pc -= 4;
	}
}

static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	int err;
	sigset_t *set;
	compat_sigset_t *cset;

	/* Did we come from a system call? */
	if (syscall_get_nr(current, regs) >= 0) {
		/* If so, check system call restarting.. */
		switch (syscall_get_error(current, regs)) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			set_retval_eintr(regs);
			break;

		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				set_retval_eintr(regs);
				break;
			}
		/* fallthrough */
		case -ERESTARTNOINTR:
			rewind_syscall(regs, false);
			break;
		}
	}

	rseq_signal_deliver(ksig, regs);

	set = sigmask_to_save();
	cset = (compat_sigset_t *) set;

	if (!is_compat_task()) {
		err = setup_rt_frame_a64(ksig->sig, ksig, set, regs);
	} else if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		err = ia32_setup_rt_frame(ksig->sig, ksig, cset, regs);
	} else {
		err = ia32_setup_frame(ksig->sig, ksig, cset, regs);
	}
	signal_setup_done(err, ksig, 0 /* TODO @ptrace single_step */);
}

void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (get_signal(&ksig)) {
		handle_signal(&ksig, regs);
		return;
	}

	/* Did we come from a system call? */
	if (syscall_get_nr(current, regs) >= 0) {
		/* Restart the system call - no handlers present */
		switch (syscall_get_error(current, regs)) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			rewind_syscall(regs, false);
			break;

		case -ERESTART_RESTARTBLOCK:
			rewind_syscall(regs, true);
			break;
		}
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask
	 * back.
	 */
	restore_saved_sigmask();
}

void signal_fault(struct pt_regs *regs, void __user *frame, char *where)
{
	struct task_struct *me = current;

	if (show_unhandled_signals && printk_ratelimit()) {
		printk("%s"
		       "%s[%d] bad frame in %s frame:%p ip:%lx sp:%lx orax:%lx",
		       task_pid_nr(current) > 1 ? KERN_INFO : KERN_EMERG,
		       me->comm, me->pid, where, frame,
		       instruction_pointer(regs), regs->sp, regs->orig_ax);
		print_vma_addr(KERN_CONT " in ", instruction_pointer(regs));
		pr_cont("\n");
	}

	force_sig(SIGSEGV);
}

