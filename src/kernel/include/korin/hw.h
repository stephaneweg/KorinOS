/* Pilotes en espace utilisateur (core/hw.c) : registres et interruptions
 * (docs/ABI.md, 0x40). Sur PC, une ligne 0..IRQ_MSI_BASE-1 est un GSI de
 * l'IOAPIC ; les lignes IRQ_MSI_BASE.. sont des vecteurs MSI/MSI-X donnés par
 * IRQ_MSI (un vecteur d'un APIC local, docs/etude/PC-PILOTES.md). */
#ifndef KORIN_HW_H
#define KORIN_HW_H

#include <stdint.h>

struct process;

#define IRQ_MSI_BASE 0x100
#define IRQ_MSI_LINES 128
#define IRQ_MSI_MAX 32 /* vecteurs par appel d'IRQ_MSI */
#define IRQ_LINES (IRQ_MSI_BASE + IRQ_MSI_LINES)

int64_t sys_dev_map(uint64_t phys, uint64_t size);
int64_t sys_irq_bind(uint64_t line, int64_t chan, uint64_t bits);
int64_t sys_irq_ack(uint64_t line);
int64_t sys_irq_wait(uint64_t line, uint64_t ms);
int64_t sys_irq_unbind(uint64_t line);
int64_t sys_irq_msi(uint64_t count, uint64_t out);
int64_t sys_acpi_table(uint64_t sig, uint64_t idx, uint64_t buf, uint64_t max);
void hw_proc_exit(struct process *p);
/* appelé par l'architecture quand une ligne se lève (ligne déjà masquée) */
void hw_irq_fired(unsigned line);

/* fournis par l'architecture */
int arch_irq_line_valid(unsigned line);
void arch_irq_line_enable(unsigned line, int on); /* démasque / masque */
int arch_phys_is_ram(uint64_t phys, uint64_t size);
/* MSI : count lignes consécutives pour le process pid ; out reçoit, par
 * ligne, l'adresse et la donnée à écrire dans le périphérique ; rend la
 * première ligne, < 0 si refusé (E_NOSYS : pas de MSI sur cette machine) */
int64_t arch_irq_msi_alloc(unsigned count, int pid, uint64_t *out);
int arch_irq_msi_owner(unsigned line); /* pid, 0 si libre */
void arch_irq_msi_free(unsigned line);
/* table du micrologiciel (ACPI sur PC) : adresse et longueur, 0 si absente */
const void *arch_firmware_table(const char sig[4], unsigned idx, uint32_t *len);

#endif
