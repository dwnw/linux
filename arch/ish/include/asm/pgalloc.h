#ifndef __ASM_ISH_PGALLOC_H
#define __ASM_ISH_PGALLOC_H

#include <asm-generic/pgalloc.h>

// TODO none of this makes sense
static inline void pmd_populate_kernel(struct mm_struct *mm,
	pmd_t *pmd, pte_t *pte)
{
	*pmd = __pmd((unsigned long) pte);
}
static inline void pmd_populate(struct mm_struct *mm,
	pmd_t *pmd, pgtable_t pte)
{
	*pmd = __pmd((unsigned long) __va(page_to_phys(pte)));
}
#define pud_populate(mm, pud, pmd) do { *pud = __pud((unsigned long) pmd); } while (0)
static inline void pud_populate_kernel(struct mm_struct *mm,
	pud_t *pud, pmd_t *pmd)
{
	*pud = __pud((unsigned long) pmd);
}
static inline void p4d_populate(struct mm_struct *mm,
	p4d_t *p4d, pud_t *pud)
{
	*p4d = __p4d((unsigned long) pud);
}
static inline void p4d_populate_kernel(struct mm_struct *mm,
	p4d_t *p4d, pud_t *pud)
{
	*p4d = __p4d((unsigned long) pud);
}
#define __pte_free_tlb(tlb, pte, address)		\
do {							\
	pgtable_pte_page_dtor(pte);			\
	tlb_remove_page((tlb), (pte));			\
} while (0)
#define __pmd_free_tlb(tlb, pmd, address)		\
do {							\
	pgtable_pmd_page_dtor(virt_to_page(pmd));	\
	tlb_remove_page((tlb), virt_to_page(pmd));	\
} while (0)
#define __pud_free_tlb(tlb, pud, address)		\
	tlb_remove_page((tlb), virt_to_page(pud))
#define pmd_pgtable(pmd) pmd_page(pmd)

extern pgd_t *pgd_alloc(struct mm_struct *);

#endif
