#ifndef __ASM_ISH_PGTABLE_H
#define __ASM_ISH_PGTABLE_H

#include <asm-generic/pgtable-nop4d.h>

// 48-bit virtual address space with 4-level page tables (matching arm64's
// 4K-page layout): PGD(9) -> PUD(9) -> PMD(9) -> PTE(9) = 36 bits of page
// number plus PAGE_SHIFT(12) = 48.
#define _POINTER_BITS 48
#define _PGD_BITS 9
#define _PUD_BITS 9
#define _PMD_BITS 9
#define _PTE_BITS 9

#define PTRS_PER_PGD	(1UL << _PGD_BITS)
#define PTRS_PER_PUD	(1UL << _PUD_BITS)
#define PTRS_PER_PMD	(1UL << _PMD_BITS)
#define PTRS_PER_PTE	(1UL << _PTE_BITS)
#define PGDIR_SHIFT	(_POINTER_BITS - _PGD_BITS)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))
#define PUD_SHIFT	(PGDIR_SHIFT - _PUD_BITS)
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))
#define PMD_SHIFT	(PUD_SHIFT - _PMD_BITS)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define USER_PTRS_PER_PGD ((TASK_SIZE + (PGDIR_SIZE - 1)) / PGDIR_SIZE)
#define FIRST_USER_ADDRESS 0

#if _PGD_BITS + _PUD_BITS + _PMD_BITS + _PTE_BITS + PAGE_SHIFT != _POINTER_BITS
#error "pagetables don't line up!"
#endif

#define _PAGE_PRESENT	(1 << 0)
#define _PAGE_ACCESSED	(1 << 1)
#define _PAGE_DIRTY	(1 << 2)
#define _PAGE_WRITE	(1 << 3)
#define _PAGE_CHG_MASK	(~(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_WRITE))

#define pte_none(x)	(pte_val(x) == 0)
#define pte_present(x)	(pte_val(x) & _PAGE_PRESENT)
#define pte_young(x)	(pte_val(x) & _PAGE_ACCESSED)
#define pte_dirty(x)	(pte_val(x) & _PAGE_DIRTY)
#define pte_write(x)	(pte_val(x) & _PAGE_WRITE)
#define pte_mkold(x)	(__pte(pte_val(x) & ~_PAGE_ACCESSED))
#define pte_mkyoung(x)	(__pte(pte_val(x) | _PAGE_ACCESSED))
#define pte_mkclean(x)	(__pte(pte_val(x) & ~_PAGE_DIRTY))
#define pte_mkdirty(x)	(__pte(pte_val(x) | _PAGE_DIRTY))
#define pte_wrprotect(x)	(__pte(pte_val(x) & ~_PAGE_WRITE))
#define pte_mkwrite(x)	(__pte(pte_val(x) | _PAGE_WRITE))

#define pte_page(x)	pfn_to_page(pte_pfn(x))
#define pte_clear(mm, addr, pte)	do { pte_val(*(pte)) = 0; } while (0)

#define pmd_page(x)		virt_to_page(pmd_val(x) & PAGE_MASK)
#define pmd_page_vaddr(x)	(pmd_val(x) & _PAGE_CHG_MASK)
#define pud_pgtable(x)		((pmd_t *) (pud_val(x) & _PAGE_CHG_MASK))
#define pud_page(x)		virt_to_page(pud_val(x) & PAGE_MASK)
#define pud_page_vaddr(x)	(pud_val(x) & _PAGE_CHG_MASK)
#define p4d_pgtable(p4d)	((pud_t *) (p4d_val(p4d) & _PAGE_CHG_MASK))
#define p4d_page(p4d)		virt_to_page(p4d_val(p4d) & PAGE_MASK)

/* appears to be garbage */
#define pmd_bad(x)	0
#define pmd_none(x)	(pmd_val(x) == 0)
#define pmd_present(x)	(pmd_val(x) != 0)
#define pmd_clear(pmd)	do { pmd_val(*(pmd)) = 0; } while (0)
#define pud_bad(x)	0
#define pud_none(x)	(pud_val(x) == 0)
#define pud_present(x)	(pud_val(x) != 0)
#define pud_clear(pud)	do { pud_val(*(pud)) = 0; } while (0)
#define set_pud(pudp, pud)	do { *(pudp) = pud; } while (0)
#define set_pud_at(mm, addr, pudp, pud)	set_pud(pudp, pud)

/* p4d is folded into pgd (pgtable-nop4d.h); the pgd entry is really the p4d
 * entry and points at a pud table, so these test the folded value. */
#define p4d_none(x)		(p4d_val(x) == 0)
#define p4d_present(x)		(p4d_val(x) != 0)
#define p4d_bad(x)		0
#define p4d_clear(p4d)		do { p4d_val(*(p4d)) = 0; } while (0)

#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))
#define pte_modify(pte, prot)	(__pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(prot)))
#define pte_pfn(x)		phys_to_pfn(pte_val(x))
#define pfn_pte(pfn, pgprot)	__pte((pfn << PAGE_SHIFT) | pgprot_val(pgprot))
#define set_pte(ptep, pte)	do { *ptep = pte; } while (0)
#define set_pte_at(mm, addr, ptep, pte)	set_pte(ptep, pte)

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pud_ERROR(e) \
	printk("%s:%d: bad pud %08lx.\n", __FILE__, __LINE__, pud_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

#define PAGE_NONE	__pgprot(_PAGE_PRESENT)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_WRITE)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_WRITE)

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern char *empty_zero_page;
#define ZERO_PAGE(vaddr) virt_to_page(empty_zero_page)

#include <asm-generic/memory_model.h>

#define __swp_type(x)			(((x).val >> 1) & 0x1f)
#define __swp_offset(x)			((x).val >> PAGE_SHIFT)
#define __swp_entry(type, offset)	((swp_entry_t) { ((type) << 1) | ((offset) << PAGE_SHIFT) })
#define __swp_entry_to_pte(x)		((pte_t) { .pte = (x).val })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })

// TODO! maybe don't
#define update_mmu_cache(vma, addr, ptep) flush_tlb_page(vma, addr)

#endif
