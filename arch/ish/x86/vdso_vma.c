#include <linux/binfmts.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/stringify.h>
#include <asm/page.h>
#include <asm/vdso.h>

#ifdef __linux__
#define sym
#else
#define sym "_"
#endif
asm(".data\n\t"
    ".balign " __stringify(PAGE_SIZE) "\n\t"
    sym"vdso_data:\n\t"
    ".incbin \"" __stringify(LIBVDSO_SO) "\"\n\t"
    ".balign " __stringify(PAGE_SIZE) "\n\t"
    sym"vdso_data_end:");
extern char vdso_data[], vdso_data_end[];

struct vdso_syms vdso_syms = {
#include <generated/vdso-offsets.h>
};

static unsigned vdso_pages;
static struct page **vdso_pagelist;

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	unsigned long vdso_base, vdso_len;
	int err = 0;

	/*
	 * This is the i386 vDSO. Native (AArch64) tasks must not get it; until
	 * the aarch64 vDSO exists, skip mapping for them. (AT_SYSINFO_EHDR is
	 * only emitted when this is actually installed.)
	 */
	if (!is_compat_task())
		return 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;
	vdso_len = vdso_pages << PAGE_SHIFT;
	vdso_base = get_unmapped_area(NULL, 0, vdso_len, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		err = vdso_base;
		goto out;
	}
	mm->context.vdso = (void __user *) vdso_base;

	err = install_special_mapping(mm, vdso_base, vdso_len,
				      VM_READ | VM_EXEC | VM_MAYREAD |
				      VM_MAYWRITE | VM_MAYEXEC, vdso_pagelist);
out:
	mmap_write_unlock(mm);
	return err;
}

static int __init init_vdso(void)
{
	int i;

	vdso_pages = (vdso_data_end - vdso_data) >> PAGE_SHIFT;
	vdso_pagelist = kcalloc(vdso_pages, sizeof(struct page *), GFP_KERNEL);
	if (unlikely(!vdso_pagelist))
		goto oom;
	for (i = 0; i < vdso_pages; i++) {
		struct page *page = alloc_page(GFP_KERNEL);
		if (unlikely(!page))
			goto oom;
		copy_page(page_address(page), vdso_data + (i * PAGE_SIZE));
		vdso_pagelist[i] = page;
	}

	return 0;

oom:
	pr_err("cannot allocate memory for vdso\n");
	if (vdso_pagelist)
		kfree(vdso_pagelist);
	return -ENOMEM;
}
subsys_initcall(init_vdso);
