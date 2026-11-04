// SPDX-License-Identifier: GPL-2.0
// AArch64 (native) signal frames: rt_sigframe setup and rt_sigreturn.
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/vmalloc.h>
#include <asm/ptrace.h>
#include <asm/sigframe_a64.h>
#include <asm/unistd.h>

#define A64_VDSO_RT_SIGRETURN	0	/* filled in by the aarch64 vDSO */

/* Walk the record chain in sigcontext.__reserved. */
static int restore_extra_context(struct pt_regs *regs, struct a64_sigcontext __user *sc)
{
	void __user *base = (void __user *) sc->__reserved;
	unsigned long off = 0;

	while (off < sizeof(sc->__reserved)) {
		struct a64_aarch64_ctx head;

		if (copy_from_user(&head, base + off, sizeof(head)))
			return -EFAULT;
		if (head.magic == 0 || head.size == 0)
			break;
		if (head.magic == FPSIMD_MAGIC) {
			struct a64_fpsimd_context ctx;
			if (copy_from_user(&ctx, base + off, sizeof(ctx)))
				return -EFAULT;
			memcpy(current->thread.fpsimd.vregs, ctx.vregs,
			       sizeof(ctx.vregs));
			current->thread.fpsimd.fpcr = ctx.fpcr;
			current->thread.fpsimd.fpsr = ctx.fpsr;
		} else if (head.magic == ESR_MAGIC) {
			struct a64_esr_context ctx;
			if (copy_from_user(&ctx, base + off, sizeof(ctx)))
				return -EFAULT;
			regs->esr = ctx.esr;
		}
		off += head.size;
	}
	return 0;
}

static int restore_sigcontext(struct pt_regs *regs, struct a64_sigcontext __user *usc)
{
	struct a64_sigcontext sc;
	int i;

	current->restart_block.fn = do_no_restart_syscall;

	if (copy_from_user(&sc, usc, sizeof(sc)))
		return -EFAULT;

	for (i = 0; i < 31; i++)
		regs->regs[i] = sc.regs[i];
	regs->sp = sc.sp;
	regs->pc = sc.pc;
	regs->pstate = sc.pstate;
	regs->far = sc.fault_address;
	regs->syscallno = -1; /* disable syscall checks */

	return restore_extra_context(regs, usc);
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct a64_rt_sigframe __user *frame = (struct a64_rt_sigframe __user *) regs->sp;
	sigset_t set;

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;
	set_current_blocked(&set);
	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;
	return regs->regs[0];

badframe:
	force_sig(SIGSEGV);
	return 0;
}

static void __user *get_sigframe_a64(struct ksignal *ksig, struct pt_regs *regs,
				     size_t frame_size)
{
	unsigned long sp = regs->sp;

	/* X/Open alternate signal stack switching. */
	sp = sigsp(sp, ksig);
	/* AArch64 requires a 16-byte aligned stack pointer. */
	sp = (sp - frame_size) & ~15UL;
	return (void __user *) sp;
}

int setup_rt_frame_a64(int sig, struct ksignal *ksig, sigset_t *set,
		       struct pt_regs *regs)
{
	struct a64_rt_sigframe __user *frame;
	void __user *restorer;
	struct a64_sigcontext sc;
	struct a64_fpsimd_context fctx;
	struct a64_esr_context ectx;
	struct a64_aarch64_ctx term = { 0, 0 };
	void __user *res;
	int i;

	frame = get_sigframe_a64(ksig, regs, sizeof(*frame));
	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	if (copy_siginfo_to_user(&frame->info, &ksig->info))
		return -EFAULT;

	/* sigcontext (registers) */
	memset(&sc, 0, sizeof(sc));
	for (i = 0; i < 31; i++)
		sc.regs[i] = regs->regs[i];
	sc.sp = regs->sp;
	sc.pc = regs->pc;
	sc.pstate = regs->pstate;
	sc.fault_address = regs->far;
	if (copy_to_user(&frame->uc.uc_mcontext, &sc, sizeof(sc)))
		return -EFAULT;

	/* fpsimd / esr records in __reserved */
	res = (void __user *) frame->uc.uc_mcontext.__reserved;
	memset(&fctx, 0, sizeof(fctx));
	fctx.head.magic = FPSIMD_MAGIC;
	fctx.head.size = sizeof(fctx);
	fctx.fpsr = current->thread.fpsimd.fpsr;
	fctx.fpcr = current->thread.fpsimd.fpcr;
	memcpy(fctx.vregs, current->thread.fpsimd.vregs, sizeof(fctx.vregs));
	if (copy_to_user(res, &fctx, sizeof(fctx)))
		return -EFAULT;

	ectx.head.magic = ESR_MAGIC;
	ectx.head.size = sizeof(ectx);
	ectx.esr = regs->esr;
	if (copy_to_user(res + sizeof(fctx), &ectx, sizeof(ectx)))
		return -EFAULT;
	if (copy_to_user(res + sizeof(fctx) + sizeof(ectx), &term, sizeof(term)))
		return -EFAULT;

	/* ucontext */
	if (put_user(0UL, &frame->uc.uc_flags) ||
	    put_user(NULL, &frame->uc.uc_link))
		return -EFAULT;
	if (__save_altstack(&frame->uc.uc_stack, regs->sp))
		return -EFAULT;
	if (copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set)))
		return -EFAULT;

	/* A return trampoline: use the libc restorer if provided (musl sets
	 * SA_RESTORER); otherwise the aarch64 vDSO trampoline. An on-stack
	 * trampoline is not viable because arm64 stacks are non-executable. */
	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		restorer = ksig->ka.sa.sa_restorer;
	} else if (current->mm->context.vdso) {
		restorer = current->mm->context.vdso + A64_VDSO_RT_SIGRETURN;
	} else {
		return -EFAULT;
	}

	/* Set up the handler invocation. */
	regs->sp = (unsigned long) frame;
	regs->pc = (unsigned long) ksig->ka.sa.sa_handler;
	regs->regs[30] = (unsigned long) restorer;	/* lr */
	regs->regs[0] = sig;
	regs->regs[1] = (unsigned long) &frame->info;
	regs->regs[2] = (unsigned long) &frame->uc;
	return 0;
}
