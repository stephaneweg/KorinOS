/* Pilotes en espace utilisateur (core/hw.c) : registres et interruptions
 * (docs/ABI.md, 0x40). Sur PC, une ligne est un GSI de l'IOAPIC. */
#ifndef KORIN_HW_H
#define KORIN_HW_H

#include <stdint.h>

struct process;

#define IRQ_LINES 192

int64_t sys_dev_map(uint64_t phys, uint64_t size);
int64_t sys_irq_bind(uint64_t line, int64_t chan, uint64_t bits);
int64_t sys_irq_ack(uint64_t line);
int64_t sys_irq_wait(uint64_t line, uint64_t ms);
int64_t sys_irq_unbind(uint64_t line);
void hw_proc_exit(struct process *p);
/* appelé par l'architecture quand une ligne se lève (ligne déjà masquée) */
void hw_irq_fired(unsigned line);

/* fournis par l'architecture */
int arch_irq_line_valid(unsigned line);
void arch_irq_line_enable(unsigned line, int on); /* démasque / masque */
int arch_phys_is_ram(uint64_t phys, uint64_t size);

#endif
