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

#endif
