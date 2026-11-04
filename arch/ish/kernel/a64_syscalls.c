// SPDX-License-Identifier: GPL-2.0
// AArch64 (native) syscall entry points that the generic syscall table needs
// but that the i386-oriented arch code did not provide.
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/uidgid.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <asm/syscall.h>

/*
 * arch/ish maps uapi/asm/stat.h to arch/x86's struct stat (144 bytes, for the
 * i386/x86_64 guest), but the AArch64 ABI uses the generic 128-byte layout.
 * Provide aarch64-layout wrappers for the stat family and re-point the
 * sys_call_table64 entries at them.
 */
struct a64_stat {
	__u64 st_dev;
	__u64 st_ino;
	__u32 st_mode;
	__u32 st_nlink;
	__u32 st_uid;
	__u32 st_gid;
	__u64 st_rdev;
	__u64 __pad1;
	__s64 st_size;
	__s32 st_blksize;
	__s32 __pad2;
	__s64 st_blocks;
	struct { __s64 tv_sec; __s64 tv_nsec; } st_atim;
	struct { __s64 tv_sec; __s64 tv_nsec; } st_mtim;
	struct { __s64 tv_sec; __s64 tv_nsec; } st_ctim;
	__u32 __unused4;
	__u32 __unused5;
};

static long cp_a64_stat(struct kstat *ks, struct a64_stat __user *st)
{
	struct a64_stat tmp = { };

	tmp.st_dev = new_encode_dev(ks->dev);
	tmp.st_ino = ks->ino;
	tmp.st_mode = ks->mode;
	tmp.st_nlink = ks->nlink;
	tmp.st_uid = from_kuid_munged(current_user_ns(), ks->uid);
	tmp.st_gid = from_kgid_munged(current_user_ns(), ks->gid);
	tmp.st_rdev = new_encode_dev(ks->rdev);
	tmp.st_size = ks->size;
	tmp.st_blksize = ks->blksize;
	tmp.st_blocks = ks->blocks;
	tmp.st_atim.tv_sec = ks->atime.tv_sec;
	tmp.st_atim.tv_nsec = ks->atime.tv_nsec;
	tmp.st_mtim.tv_sec = ks->mtime.tv_sec;
	tmp.st_mtim.tv_nsec = ks->mtime.tv_nsec;
	tmp.st_ctim.tv_sec = ks->ctime.tv_sec;
	tmp.st_ctim.tv_nsec = ks->ctime.tv_nsec;
	return copy_to_user(st, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

long sys_a64_fstat(unsigned long fd, unsigned long statbuf, unsigned long a2,
		   unsigned long a3, unsigned long a4, unsigned long a5)
{
	struct kstat ks;
	int err = vfs_fstat((int) fd, &ks);

	if (err)
		return err;
	return cp_a64_stat(&ks, (struct a64_stat __user *) statbuf);
}

long sys_a64_newfstatat(unsigned long dfd, unsigned long filename,
			unsigned long statbuf, unsigned long flag,
			unsigned long a4, unsigned long a5)
{
	struct kstat ks;
	int err = vfs_fstatat((int) dfd, (const char __user *) filename,
			      &ks, (int) flag);

	if (err)
		return err;
	return cp_a64_stat(&ks, (struct a64_stat __user *) statbuf);
}

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

/* Re-point the stat family at the aarch64-layout wrappers. */
extern void *sys_call_table64[];
static int __init a64_stat_family_override(void)
{
	/* AArch64 numbers are ABI-stable: __NR_fstat = 80, __NR_newfstatat = 79. */
	sys_call_table64[80] = sys_a64_fstat;
	sys_call_table64[79] = sys_a64_newfstatat;
	return 0;
}
late_initcall(a64_stat_family_override);
