// SPDX-License-Identifier: GPL-2.0
// AArch64 (native) syscall entry points that the generic syscall table needs
// but that the i386-oriented arch code did not provide.
#include <linux/syscalls.h>
#include <linux/mm.h>

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
