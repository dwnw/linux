#ifndef __ASM_ISH_PTRACE_H
#define __ASM_ISH_PTRACE_H

#ifndef __ASSEMBLY__

/*
 * One pt_regs for two guest ABIs (checked in by TIF_64BIT):
 *  - i386 (compat):  ax..flags, tls, orig_ax, cr2/trap_nr/error_code
 *  - AArch64 (native): regs[0..30], pc, pstate, orig_x0, syscallno,
 *                                     tpidr_el0, far, esr
 * The x86 field order is load-bearing (the asbestos bridge copies by name,
 * but keep it stable anyway); the AArch64 fields are appended.
 * `sp` is shared: both ABIs mean "user stack pointer".
 */
struct pt_regs {
	/* ---- i386 (compat) ---- */
	unsigned long ax;
	unsigned long bx;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
	unsigned long bp;
	unsigned long sp;
	unsigned long ip;
	unsigned long flags;

	unsigned long tls;
	unsigned long orig_ax;

	unsigned long cr2;
	unsigned long trap_nr;
	unsigned long error_code;

	/* ---- AArch64 (native) ---- */
	unsigned long regs[31];	/* x0..x30 */
	unsigned long pc;
	unsigned long pstate;
	unsigned long orig_x0;
	long syscallno;		/* x8 */
	unsigned long tpidr_el0;	/* TLS */
	unsigned long far;	/* fault address (data/insn abort) */
	unsigned long esr;	/* exception syndrome */
};

#ifdef __KERNEL__
#include <linux/build_bug.h>
/* Guard the i386 layout the x86 bridge/offsets depend on. */
static_assert(offsetof(struct pt_regs, ax) == 0, "pt_regs.ax");
static_assert(offsetof(struct pt_regs, bx) == 8, "pt_regs.bx");
static_assert(offsetof(struct pt_regs, cx) == 16, "pt_regs.cx");
static_assert(offsetof(struct pt_regs, dx) == 24, "pt_regs.dx");
static_assert(offsetof(struct pt_regs, si) == 32, "pt_regs.si");
static_assert(offsetof(struct pt_regs, di) == 40, "pt_regs.di");
static_assert(offsetof(struct pt_regs, bp) == 48, "pt_regs.bp");
static_assert(offsetof(struct pt_regs, sp) == 56, "pt_regs.sp");
static_assert(offsetof(struct pt_regs, ip) == 64, "pt_regs.ip");
static_assert(offsetof(struct pt_regs, flags) == 72, "pt_regs.flags");
static_assert(offsetof(struct pt_regs, tls) == 80, "pt_regs.tls");
static_assert(offsetof(struct pt_regs, orig_ax) == 88, "pt_regs.orig_ax");
static_assert(offsetof(struct pt_regs, cr2) == 96, "pt_regs.cr2");
static_assert(offsetof(struct pt_regs, trap_nr) == 104, "pt_regs.trap_nr");
static_assert(offsetof(struct pt_regs, error_code) == 112, "pt_regs.error_code");
#endif

/* AArch64 pstate mode bits (EL0t = user). */
#define PSR_MODE_MASK	0xfUL
#define PSR_MODE_EL0t	0x0UL

/* Trap discriminators reported by the emulator bridges in regs->trap_nr. */
#define A64_TRAP_DATA_ABORT	0x1000	/* far = faulting VA, esr = ESR_EL1  */
#define A64_TRAP_INSN_ABORT	0x1001	/* far = faulting PC, esr = ESR_EL1  */
#define A64_TRAP_UNDEF		0x1002	/* far = faulting PC, esr = ESR_EL1  */
#define A64_TRAP_SVC		0x1003	/* syscallno = x8                    */

/* ESR_EL1 fields we care about. */
#define ESR_EL1_WNR		(1UL << 6)	/* data abort was a write */

/*
 * This is a usermode-emulated port: the guest never executes kernel code, so
 * native (AArch64) pt_regs are always user mode. The i386 path historically
 * reported 0; leave it unchanged. Kept as a macro so it expands where
 * is_compat_task() (thread_info.h) is available.
 */
#define user_mode(regs)	(is_compat_task() ? 0 : 1)

#define user_stack_pointer(regs) ((regs)->sp)
#define instruction_pointer(regs) \
	(is_compat_task() ? (regs)->ip : (regs)->pc)

int trace_syscall_enter(struct pt_regs *regs);
void trace_syscall_exit(struct pt_regs *regs);

#endif

#endif
