// SPDX-License-Identifier: GPL-2.0
//
// Kernel-side self-test for the 4th (PUD) page-table level.
//
// Enabled by CONFIG_ISH_PUD_SELFTEST. The test kthread marks itself native
// (TIF_64BIT) so it gets the 47-bit task size and can reach addresses above
// 4 GiB. It runs with a throwaway mm, so it never touches a real user process.
//
// It maps and faults pages at addresses that span several PGD and PUD slots
// (including above 4 GiB and near the 47-bit limit), clones them fork-style
// with COW via copy_page_range(), tears everything down, and compares the
// free-page count and the mm's page-table byte accounting before and after
// to catch leaked PUD/PMD/PTE pages.
//
// It also exercises partial teardown. Note that arch/ish defines pud_bad()
// and pmd_bad() as 0, so the __pud_clear_bad()/__pmd_clear_bad() paths are
// degenerate (they never fire on entries this arch can produce); the real
// failure-path coverage therefore comes from partial teardown, which is
// what free_pud_range() actually does here.
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mmap_lock.h>
#include <linux/resource.h>
#include <linux/rmap.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/swap.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>

/* Provided by arch/ish/kernel/uaccess.c and arch/ish/mm/fault.c */
extern pte_t *virt_to_pte(struct mm_struct *mm, unsigned long addr);
extern int handle_page_fault(unsigned long address, int is_write, int *code_out);

static int passed;
static int failed;
static DECLARE_COMPLETION(pud_done);

#define CHECK(cond, fmt, ...)						\
	do {								\
		if (cond) {						\
			passed++;					\
		} else {						\
			failed++;					\
			pr_err("PUDTEST: FAIL: " fmt "\n", ##__VA_ARGS__); \
		}							\
	} while (0)

/*
 * Addresses chosen to land in distinct PGD/PUD slots:
 *   PGD index = addr >> 39, PUD index = (addr >> 30) & 511.
 */
static const unsigned long test_addrs[] = {
	0x0000000040000000UL, /*   1 GiB : PGD 0, PUD 1     */
	0x0000000100000000UL, /*   4 GiB : PGD 0, PUD 4     */
	0x0000004000000000UL, /* 256 GiB : PGD 0, PUD 256   */
	0x0000008000000000UL, /* 512 GiB : PGD 1, PUD 0     */
	0x0000010000000000UL, /*   1 TiB : PGD 2, PUD 0     */
	0x0000400000000000UL, /*  64 TiB : PGD 128, PUD 0   */
	0x00007fffffe00000UL, /* near 47-bit limit: PGD 255, PUD 510 */
};
#define N_ADDRS ARRAY_SIZE(test_addrs)

static pte_t *get_pte(struct mm_struct *mm, unsigned long addr)
{
	return virt_to_pte(mm, addr);
}

/* Fault a page in at addr and stamp a known byte through the host mapping. */
static int fault_and_stamp(struct mm_struct *mm, unsigned long addr, u8 val)
{
	int code = 0;
	pte_t *pte;
	u8 *host;

	if (handle_page_fault(addr, 1, &code) != 0)
		return -EFAULT;
	pte = get_pte(mm, addr);
	if (!pte || !pte_present(*pte))
		return -EFAULT;
	host = pfn_to_virt(pte_pfn(*pte)) + (addr & ~PAGE_MASK);
	*host = val;
	if (*(volatile u8 *)host != val)
		return -EIO;
	return 0;
}

static int map_one(struct mm_struct *mm, unsigned long addr)
{
	unsigned long r;

	(void)mm; /* mapping goes through current->mm */
	r = vm_mmap(NULL, addr, PAGE_SIZE, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, 0);
	return (r == addr) ? 0 : -ENOMEM;
}

static int pud_selftest_fn(void *arg)
{
	struct mm_struct *src, *dst;
	unsigned long free0, free1, pt_zero, pt_mapped, pt_after;
	unsigned long dst_zero, dst_mapped, dst_after;
	long delta;
	int i;

	/* Behave like a native (64-bit) task so TASK_SIZE is the 47-bit max. */
	set_thread_flag(TIF_64BIT);

	src = mm_alloc();
	if (!src) {
		pr_err("PUDTEST: mm_alloc(src) failed\n");
		failed++;
		goto done;
	}
	src->mmap_base = TASK_UNMAPPED_BASE;
	src->get_unmapped_area = arch_get_unmapped_area;
	kthread_use_mm(src);
	pr_info("PUDTEST: src=%px gup=%px cur=%px cur_gup=%px\n",
		src, src->get_unmapped_area, current->mm,
		current->mm ? current->mm->get_unmapped_area : NULL);

	/* pgtables_bytes starts at 0 for a fresh mm (the PGD page is not
	 * accounted there). */
	pt_zero = atomic_long_read(&src->pgtables_bytes);
	drain_all_pages(NULL);
	free0 = nr_free_pages();

	/* ---- Phase 1: map+fault across PGD/PUD slots ---- */
	{
		int ok = 0;
		for (i = 0; i < N_ADDRS; i++) {
			if (map_one(src, test_addrs[i]) != 0) {
				pr_err("PUDTEST: vm_mmap(%#lx) failed\n", test_addrs[i]);
				continue;
			}
			if (fault_and_stamp(src, test_addrs[i], (u8)(0xa0 + i)) != 0) {
				pr_err("PUDTEST: fault(%#lx) failed\n", test_addrs[i]);
				continue;
			}
			ok++;
		}
		CHECK(ok == N_ADDRS, "only %d/%zu high addresses faulted", ok, N_ADDRS);
	}

	pt_mapped = atomic_long_read(&src->pgtables_bytes);
	pr_info("PUDTEST: fp after map=%lu (base %lu)\n", nr_free_pages(), free0);
	CHECK(pt_mapped > pt_zero, "page tables did not grow (zero=%lu mapped=%lu)",
	      pt_zero, pt_mapped);
	pr_info("PUDTEST: mapped %zu pages, pgtables_bytes %lu -> %lu\n",
		N_ADDRS, pt_zero, pt_mapped);

	/* ---- Phase 2: partial teardown then re-fault (failure path) ---- */
	{
		unsigned long a = test_addrs[1]; /* 4 GiB */
		pte_t *pte;

		if (vm_munmap(a, PAGE_SIZE) != 0) {
			failed++;
			pr_err("PUDTEST: partial munmap(%#lx) failed\n", a);
		} else {
			pte = get_pte(src, a);
			CHECK(pte == NULL || !pte_present(*pte),
			      "PTE survived partial munmap at %#lx", a);
			CHECK(map_one(src, a) == 0 &&
			      fault_and_stamp(src, a, 0x5a) == 0,
			      "re-fault after partial munmap failed at %#lx", a);
		}
	}

	/* ---- Phase 3: fork-style COW clone across all slots ---- */
	kthread_unuse_mm(src);

	dst = mm_alloc();
	if (!dst) {
		pr_err("PUDTEST: mm_alloc(dst) failed\n");
		failed++;
		mmput(src);
		goto done;
	}
	dst->mmap_base = TASK_UNMAPPED_BASE;
	dst->get_unmapped_area = arch_get_unmapped_area;
	dst_zero = atomic_long_read(&dst->pgtables_bytes);

	kthread_use_mm(dst);
	for (i = 0; i < N_ADDRS; i++) {
		if (map_one(dst, test_addrs[i]) != 0) {
			failed++;
			pr_err("PUDTEST: dst vm_mmap(%#lx) failed\n", test_addrs[i]);
		}
	}
	kthread_unuse_mm(dst);

	mmap_write_lock(src);
	mmap_write_lock(dst);
	for (i = 0; i < N_ADDRS; i++) {
		unsigned long a = test_addrs[i];
		struct vm_area_struct *sv = find_vma(src, a);
		struct vm_area_struct *dv = find_vma(dst, a);
		int e;

		if (!sv || !dv) {
			failed++;
			pr_err("PUDTEST: missing vma at %#lx (src=%p dst=%p)\n",
			       a, sv, dv);
			continue;
		}
		if (anon_vma_fork(dv, sv) != 0) {
			failed++;
			pr_err("PUDTEST: anon_vma_fork(%#lx) failed\n", a);
			continue;
		}
		e = copy_page_range(dv, sv);
		CHECK(e == 0, "copy_page_range(%#lx) = %d", a, e);
	}
	/* Verify COW: dst PTE present, shares PFN with src, both read-only. */
	for (i = 0; i < N_ADDRS; i++) {
		unsigned long a = test_addrs[i];
		pte_t *sp = get_pte(src, a);
		pte_t *dp = get_pte(dst, a);

		if (!sp || !dp || !pte_present(*sp) || !pte_present(*dp)) {
			failed++;
			pr_err("PUDTEST: COW lost a present pte at %#lx\n", a);
			continue;
		}
		CHECK(pte_pfn(*sp) == pte_pfn(*dp),
		      "COW pfn mismatch at %#lx (src=%#lx dst=%#lx)",
		      a, (unsigned long)pte_pfn(*sp), (unsigned long)pte_pfn(*dp));
		CHECK(!pte_write(*sp) && !pte_write(*dp),
		      "COW pte not write-protected at %#lx", a);
	}
	dst_mapped = atomic_long_read(&dst->pgtables_bytes);
	pr_info("PUDTEST: fp after cow=%lu\n", nr_free_pages());
	mmap_write_unlock(dst);
	mmap_write_unlock(src);
	CHECK(dst_mapped > dst_zero,
	      "dst page tables did not grow (zero=%lu mapped=%lu)",
	      dst_zero, dst_mapped);

	/* ---- Phase 4: tear down both address spaces ---- */
	kthread_use_mm(dst);
	for (i = 0; i < N_ADDRS; i++)
		vm_munmap(test_addrs[i], PAGE_SIZE);
	kthread_unuse_mm(dst);
	dst_after = atomic_long_read(&dst->pgtables_bytes);
	pr_info("PUDTEST: fp after dst teardown=%lu\n", nr_free_pages());

	kthread_use_mm(src);
	for (i = 0; i < N_ADDRS; i++)
		vm_munmap(test_addrs[i], PAGE_SIZE);
	kthread_unuse_mm(src);
	pt_after = atomic_long_read(&src->pgtables_bytes);
	pr_info("PUDTEST: fp after src teardown=%lu\n", nr_free_pages());

	CHECK(pt_after == pt_zero,
	      "src page-table leak: zero=%lu after=%lu", pt_zero, pt_after);
	CHECK(dst_after == dst_zero,
	      "dst page-table leak: zero=%lu after=%lu", dst_zero, dst_after);

	/* ---- Phase 5: release and check the global free-page count ---- */
	mmput(dst);
	mmput(src);

	drain_all_pages(NULL);
	free1 = nr_free_pages();
	delta = (long)free0 - (long)free1; /* >0 means pages were leaked */
	/* Global free-page accounting is only approximate here: small
	 * alloc/free cycles stay in the per-cpu page lists and slab caches,
	 * so allow a noise margin. The authoritative page-table leak check is
	 * pgtables_bytes above, which counts PUD/PMD/PTE pages exactly. */
	CHECK(delta <= 64 && delta >= -64,
	      "free-page count changed by %ld (before=%lu after=%lu)",
	      delta, free0, free1);
	pr_info("PUDTEST: global free pages %lu -> %lu (delta %ld, exact page-table bytes back to %lu)\n",
		free0, free1, delta, pt_after);

done:
	pr_info("PUDTEST: %d passed, %d failed%s\n",
		passed, failed, failed ? " -- FAIL" : " -- OK");
	complete(&pud_done);
	return failed ? -1 : 0;
}

static int __init pud_selftest_init(void)
{
	struct task_struct *t;

	t = kthread_run(pud_selftest_fn, NULL, "pudselftest");
	if (IS_ERR(t)) {
		pr_err("PUDTEST: kthread_run failed: %ld\n", PTR_ERR(t));
		return 0;
	}
	wait_for_completion(&pud_done);
	return 0;
}
late_initcall(pud_selftest_init);
