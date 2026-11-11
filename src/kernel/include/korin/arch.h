/* Interface entre le noyau portable (src/kernel/core, en C) et une
 * architecture (src/kernel/arch/<arch>, <carte>). Ce que le noyau commun au
 * Pi et au PC attend de chaque côté. */
#ifndef KORIN_ARCH_H
#define KORIN_ARCH_H

#include <stdint.h>

/* adresse virtuelle d'une adresse physique (carte de toute la mémoire) */
void *arch_phys_to_virt(uint64_t phys);

/* millisecondes depuis le démarrage */
uint64_t arch_ticks_ms(void);

/* attendre la prochaine interruption */
void arch_wait(void);

/* interruptions du cœur courant */
void arch_irq_enable(void);
void arch_irq_disable(void);

/* caractère sur la sortie de débogage (port série, debugcon de QEMU) */
void arch_debug_putc(char c);

/* touche lue au clavier du moniteur : caractère Latin-1, ou -1 */
int arch_getc(void);

/* redémarrage de la machine (ne revient pas) */
void arch_reboot(void);

/* résumé de la machine pour le moniteur (processeur, ACPI, minuterie) */
void arch_print_info(void);

/* arrêt définitif du cœur courant */
__attribute__((noreturn)) void arch_halt_forever(void);

/* coupe les interruptions et rend l'état d'avant, pour arch_irq_restore */
uint64_t arch_irq_save(void);
void arch_irq_restore(uint64_t flags);

/* e_machine des exécutables ELF de cette architecture */
int arch_elf_machine(void);

struct thread;
/* prépare la pile du noyau d'un thread neuf : la première bascule vers lui
 * appelle fn(arg), interruptions permises */
void arch_thread_setup(struct thread *t, void (*fn)(void *), void *arg);
/* bascule de prev vers next (interruptions coupées) */
void arch_context_switch(struct thread *prev, struct thread *next);
/* passe en mode utilisateur : entrée, sommet de pile, argument */
__attribute__((noreturn)) void arch_enter_user(uint64_t entry, uint64_t stack, uint64_t arg);

#endif
