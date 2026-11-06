#include <linux/mm.h>
#include <linux/uaccess.h>
#include <emu/kernel.h>

/* stolen from um */

int handle_page_fault_mm(struct mm_struct *mm, unsigned long address,
			 int is_write, int *code_out)
{
	struct vm_area_struct *vma;
	vm_fault_t fault;
	int err = -EFAULT;
	unsigned int flags = FAULT_FLAG_DEFAULT | FAULT_FLAG_USER;

	*code_out = SEGV_MAPERR;

retry:
	mmap_read_lock(mm);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;

good_area:
	*code_out = SEGV_ACCERR;
	if (is_write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
		flags |= FAULT_FLAG_WRITE;
	} else {
		/* Don't require VM_READ|VM_EXEC for write faults! */
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}

	fault = handle_mm_fault(vma, address, flags, NULL);

	/* TODO: this choice of pt_regs may not make any sense */
	if (fault_signal_pending(fault, task_pt_regs(current)))
		return -EFAULT;

	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGSEGV)
			goto bad_area;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}

	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		if (fault & VM_FAULT_MAJOR)
			current->maj_flt++;
		else
			current->min_flt++;
		if (fault & VM_FAULT_RETRY) {
			flags |= FAULT_FLAG_TRIED;

			goto retry;
		}
	}
	
	err = 0;

bad_area:
	mmap_read_unlock(mm);
	return err;

do_sigbus:
	err = -EACCES;
	goto bad_area;

out_of_memory:
	/*
	 * We ran out of memory, call the OOM killer, and return the userspace
	 * (which will retry the fault, or kill us if we got oom-killed).
	 */
	mmap_read_unlock(mm);
	pagefault_out_of_memory();
	return 0;
}

int handle_page_fault(unsigned long address, int is_write, int *code_out)
{
	return handle_page_fault_mm(current->mm, address, is_write, code_out);
}

/* Fault for the mm the emulator is running, which is authoritative even when
 * `current` (pthread-specific on Darwin) is momentarily stale. */
int handle_page_fault_emu(struct emu_mm *emu_mm, unsigned long address,
			  int is_write, int *code_out)
{
	struct mm_struct *mm = container_of(emu_mm, struct mm_struct, context.emu_mm);
	return handle_page_fault_mm(mm, address, is_write, code_out);
}
