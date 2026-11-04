#include <asm/unistd.h>

#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, compat)

/* TODO: very i386 specific */
#define __SYSCALL(nr, call)	extern long call(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
#include <asm/syscalls_32.h>

#undef __SYSCALL
#define __SYSCALL(nr, call)	[nr] = (call),
void *sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = (void *) sys_ni_syscall,
#include <asm/syscalls_32.h>
};

/*
 * AArch64 uses the generic (asm-generic) syscall numbering. The functions are
 * the same raw-argument sys_* wrappers; only the numbers differ. Declare them
 * with the 6-argument calling convention (the extra args are ignored) so the
 * kernel can call any of them uniformly.
 */
#undef __SYSCALL
#define __SYSCALL(nr, call)	extern long call(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
#include <uapi/asm-generic/unistd.h>

#undef __SYSCALL
#define __SYSCALL(nr, call)	[nr] = (call),
void *sys_call_table64[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = (void *) sys_ni_syscall,
#include <uapi/asm-generic/unistd.h>
};
const unsigned int sys_call_table64_len = __NR_syscalls;

