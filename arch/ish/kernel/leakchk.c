// Temporary debug aid: expose global page counters so a userspace test can
// check that repeated fork/exec (with the 47-bit temporary stack shift) does
// not leak free pages / page-table pages.
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/swap.h>

static int leakchk_show(struct seq_file *m, void *v)
{
	seq_printf(m, "free=%lu pgtables=%lu\n",
		   nr_free_pages(),
		   global_node_page_state(NR_PAGETABLE));
	return 0;
}

static int __init leakchk_init(void)
{
	proc_create_single("leakchk", 0, NULL, leakchk_show);
	return 0;
}
late_initcall(leakchk_init);
