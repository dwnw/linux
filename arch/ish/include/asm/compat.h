#ifndef __ASM_ISH_COMPAT_H
#define __ASM_ISH_COMPAT_H

static inline bool in_ia32_syscall(void)
{
	/* True only for a 32-bit (i386 compat) task, not for native AArch64. */
	return is_compat_task();
}
static inline void __user *arch_compat_alloc_user_space(long len)
{
	unsigned long sp = task_pt_regs(current)->sp;
	return (void __user *) round_down(sp - len, 16);
}


#include "../../../x86/include/asm/compat.h"

#endif
