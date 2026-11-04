// SPDX-License-Identifier: GPL-2.0
// AArch64 (native) syscall entry points that the generic syscall table needs
// but that the i386-oriented arch code did not provide.
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <asm/syscall.h>

/* AArch64 mmap takes a full 64-bit byte offset in x5 (not a page number). */
SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, off)
{
	if (offset_in_page(off))
		return -EINVAL;
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}

/*
 * AArch64 rt_sigreturn is implemented together with the aarch64 rt_sigframe
 * in a later step; keep a valid table entry until then.
 */
SYSCALL_DEFINE0(rt_sigreturn)
{
	return -ENOSYS;
}

/*
 * Sanity check that the generic AArch64 table is fully populated: every slot
 * must be a real entry (holes are sys_ni_syscall, never NULL). Runs at boot.
 */
static int __init a64_syscall_table_check(void)
{
	unsigned int i, ni = 0, null = 0, real = 0;

	for (i = 0; i < sys_call_table64_len; i++) {
		if (sys_call_table64[i] == NULL)
			null++;
		else if (sys_call_table64[i] == (void *) sys_ni_syscall)
			ni++;
		else
			real++;
	}
	pr_info("a64 syscall table: %u entries, %u implemented, %u ni_syscall, %u null\n",
		sys_call_table64_len, real, ni, null);
	if (null) {
		pr_err("a64 syscall table has %u NULL entries!\n", null);
		return -EINVAL;
	}
	return 0;
}
late_initcall(a64_syscall_table_check);
