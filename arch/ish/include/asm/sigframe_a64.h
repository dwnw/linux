#ifndef __ASM_ISH_SIGFRAME_A64_H
#define __ASM_ISH_SIGFRAME_A64_H

#include <linux/signal.h>

/* AArch64 signal frame, matching the arm64 uapi layout (a64_ prefix to avoid
 * colliding with the x86 sigcontext/sigframe types). */
struct a64_aarch64_ctx {
	__u32 magic;
	__u32 size;
};

#define FPSIMD_MAGIC	0x46508001
#define ESR_MAGIC	0x45535201

struct a64_fpsimd_context {
	struct a64_aarch64_ctx head;
	__u32 fpsr;
	__u32 fpcr;
	__uint128_t vregs[32];
};

struct a64_esr_context {
	struct a64_aarch64_ctx head;
	__u64 esr;
};

struct a64_sigcontext {
	__u64 fault_address;
	__u64 regs[31];
	__u64 sp;
	__u64 pc;
	__u64 pstate;
	__u8 __reserved[4096] __attribute__((__aligned__(16)));
};

struct a64_ucontext {
	unsigned long uc_flags;
	struct a64_ucontext *uc_link;
	stack_t uc_stack;
	sigset_t uc_sigmask;
	__u8 __unused[1024 / 8 - sizeof(sigset_t)];
	struct a64_sigcontext uc_mcontext;
};

struct a64_rt_sigframe {
	siginfo_t info;
	struct a64_ucontext uc;
};

#ifdef __KERNEL__
#include <linux/build_bug.h>
static_assert(offsetof(struct a64_sigcontext, regs) == 8, "sc.regs");
static_assert(offsetof(struct a64_sigcontext, sp) == 256, "sc.sp");
static_assert(offsetof(struct a64_sigcontext, pc) == 264, "sc.pc");
static_assert(offsetof(struct a64_sigcontext, pstate) == 272, "sc.pstate");
static_assert(offsetof(struct a64_sigcontext, __reserved) == 288, "sc.reserved");
static_assert(offsetof(struct a64_ucontext, uc_stack) == 16, "uc.uc_stack");
static_assert(offsetof(struct a64_ucontext, uc_sigmask) == 40, "uc.uc_sigmask");
static_assert(offsetof(struct a64_ucontext, uc_mcontext) == 176, "uc.uc_mcontext");
static_assert(offsetof(struct a64_rt_sigframe, uc) == 128, "rt.uc");
#endif

#endif
