#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <emu/kernel.h>

/* TODO: put this in a header */
extern int handle_page_fault(unsigned long address, int is_write, int *code_out);

/* stolen from um */

pte_t *virt_to_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	if (mm == NULL)
		return NULL;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(*p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (!pud_present(*pud))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		return NULL;

	return pte_offset_kernel(pmd, addr);
}

/* exported for gdb, I guess */
void *user_to_kernel(struct mm_struct *mm, unsigned long virt, int is_write)
{
	pte_t *pte = virt_to_pte(mm, virt);
	if (pte == NULL || !pte_present(*pte))
		return NULL;
	if (is_write && !pte_write(*pte))
		return NULL;
	return pfn_to_virt(pte_pfn(*pte)) + (virt & ~PAGE_MASK);
}

/* for emulators to call */
void *user_to_kernel_emu(struct emu_mm *emu_mm, unsigned long virt, bool *writable)
{
	struct mm_struct *mm;
	pte_t *pte;

	mm = container_of(emu_mm, struct mm_struct, context.emu_mm);
	/* The emulator translates for the task whose emu_mm it was given.  On
	 * Darwin `current` is pthread-specific (not __thread) because __thread
	 * breaks across the longjmp-based stack moves, so it can be momentarily
	 * stale after a context switch; asserting here needlessly panics.
	 * Use `mm` (which is what the access is actually against). */
	if (mm != (current ? current->mm : NULL))
		pr_warn_once("user_to_kernel_emu: mm %px != current->mm %px (comm=%s)\n",
			     mm, current ? current->mm : NULL,
			     current ? current->comm : "?");

	pte = virt_to_pte(mm, virt);
	if (pte == NULL || !pte_present(*pte))
		return NULL;
	if (writable) *writable = !!pte_write(*pte);
	return pfn_to_virt(pte_pfn(*pte)) + (virt & ~PAGE_MASK);
}

static pte_t *maybe_map(unsigned long virt, int is_write)
{
	pte_t *pte = virt_to_pte(current->mm, virt);
	int err, code_dummy;

	if ((pte == NULL) || !pte_present(*pte) ||
	    (is_write && !pte_write(*pte))) {
		err = handle_page_fault(virt, is_write, &code_dummy);
		if (err)
			return NULL;
		pte = virt_to_pte(current->mm, virt);
	}
	if (!pte_present(*pte))
		pte = NULL;

	return pte;
}

static int do_op_one_page(unsigned long addr, int len, int is_write,
		 int (*op)(unsigned long addr, int len, void *arg), void *arg)
{
	pte_t *pte;
	int n;

	pte = maybe_map(addr, is_write);
	if (pte == NULL)
		return -1;

	addr = (unsigned long) pfn_to_virt(pte_pfn(*pte)) +
		(addr & ~PAGE_MASK);
	n = (*op)(addr, len, arg);

	return n;
}

static long buffer_op(unsigned long addr, int len, int is_write,
		      int (*op)(unsigned long, int, void *), void *arg)
{
	long size, remain, n;

	size = min(PAGE_ALIGN(addr) - addr, (unsigned long) len);
	remain = len;

	n = do_op_one_page(addr, size, is_write, op, arg);
	if (n != 0) {
		remain = (n < 0 ? remain : 0);
		goto out;
	}

	addr += size;
	remain -= size;
	if (remain == 0)
		goto out;

	while (addr < ((addr + remain) & PAGE_MASK)) {
		n = do_op_one_page(addr, PAGE_SIZE, is_write, op, arg);
		if (n != 0) {
			remain = (n < 0 ? remain : 0);
			goto out;
		}

		addr += PAGE_SIZE;
		remain -= PAGE_SIZE;
	}
	if (remain == 0)
		goto out;

	n = do_op_one_page(addr, remain, is_write, op, arg);
	if (n != 0) {
		remain = (n < 0 ? remain : 0);
		goto out;
	}

	return 0;
 out:
	return remain;
}

static int copy_chunk_from_user(unsigned long from, int len, void *arg)
{
	unsigned long *to_ptr = arg, to = *to_ptr;

	memcpy((void *) to, (void *) from, len);
	*to_ptr += len;
	return 0;
}

unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (uaccess_kernel()) {
		memcpy(to, (__force void*)from, n);
		return 0;
	}

	return buffer_op((unsigned long) from, n, 0, copy_chunk_from_user, &to);
}
EXPORT_SYMBOL(raw_copy_from_user);

static int copy_chunk_to_user(unsigned long to, int len, void *arg)
{
	unsigned long *from_ptr = arg, from = *from_ptr;

	memcpy((void *) to, (void *) from, len);
	*from_ptr += len;
	return 0;
}

unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (uaccess_kernel()) {
		memcpy((__force void *) to, from, n);
		return 0;
	}

	return buffer_op((unsigned long) to, n, 1, copy_chunk_to_user, &from);
}
EXPORT_SYMBOL(raw_copy_to_user);

unsigned long raw_copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	void *buf = kmalloc(n, GFP_ATOMIC);
	int res = 1;

	if (raw_copy_from_user(buf, from, n))
		goto out;
	if (raw_copy_to_user(to, buf, n))
		goto out;

	res = 0;
out:
	kfree(buf);
	return res;
}

static int strncpy_chunk_from_user(unsigned long from, int len, void *arg)
{
	char **to_ptr = arg, *to = *to_ptr;
	int n;

	strncpy(to, (void *) from, len);
	n = strnlen(to, len);
	*to_ptr += n;

	if (n < len)
	        return 1;
	return 0;
}

long __strncpy_from_user(char *dst, const char __user *src, long count)
{
	long n;
	char *ptr = dst;

	if (uaccess_kernel()) {
		strncpy(dst, (__force void *) src, count);
		return strnlen(dst, count);
	}

	n = buffer_op((unsigned long) src, count, 0, strncpy_chunk_from_user,
		      &ptr);
	if (n != 0)
		return -EFAULT;
	return strnlen(dst, count);
}
EXPORT_SYMBOL(__strncpy_from_user);

static int clear_chunk(unsigned long addr, int len, void *unused)
{
	memset((void *) addr, 0, len);
	return 0;
}

unsigned long __clear_user(void __user *mem, unsigned long len)
{
	if (uaccess_kernel()) {
		memset((__force void*)mem, 0, len);
		return 0;
	}

	return buffer_op((unsigned long) mem, len, 1, clear_chunk, NULL);
}
EXPORT_SYMBOL(__clear_user);

static int strnlen_chunk(unsigned long str, int len, void *arg)
{
	int *len_ptr = arg, n;

	n = strnlen((void *) str, len);
	*len_ptr += n;

	if (n < len)
		return 1;
	return 0;
}

long __strnlen_user(const void __user *str, long len)
{
	int count = 0, n;

	if (uaccess_kernel())
		return strnlen((__force char*)str, len) + 1;

	n = buffer_op((unsigned long) str, len, 0, strnlen_chunk, &count);
	if (n == 0)
		return count + 1;
	return 0;
}
EXPORT_SYMBOL(__strnlen_user);

bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size)
{
	if ((unsigned long) unsafe_src < ish_phys_base)
		return false;
	if ((unsigned long) unsafe_src + size > ish_phys_base + ish_phys_size)
		return false;
	return true;
}
