#ifndef __ISH_EMU_KERNEL_H
#define __ISH_EMU_KERNEL_H

struct emu_mm;

/* kernel functions for emulators to call */
extern struct pt_regs *emu_pt_regs(struct emu *emu);
extern void *emu_fpsimd(struct emu *emu);
extern void emu_set_current(struct emu *emu);
extern struct emu_mm *emu_current_mm(struct emu *emu);
extern void handle_cpu_trap(struct emu *emu);
extern void *user_to_kernel_emu(struct emu_mm *emu_mm, unsigned long virt, bool *writable);

#endif
