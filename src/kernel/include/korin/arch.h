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
/* fréquence du compteur que le mode utilisateur lit lui-même (PC : TSC invariant, rdtsc) ; 0 : aucun
 * (CLOCK_GET 3, libposix) */
uint64_t arch_counter_hz(void);

/* attendre la prochaine interruption */
void arch_wait(void);

/* interruptions du cœur courant */
void arch_irq_enable(void);
void arch_irq_disable(void);

/* caractère sur la sortie de débogage (port série, debugcon de QEMU) */
void arch_debug_putc(char c);

/* touche lue au clavier du moniteur : caractère Latin-1, ou -1 */
int arch_getc(void);

/* redémarrage de la machine (ne revient pas) : les autres cœurs arrêtés d'abord */
void arch_reboot(void);

/* extinction de la machine (ne revient pas) : les autres cœurs arrêtés ; si le
 * matériel refuse, un message à l'écran (« vous pouvez éteindre ») et l'arrêt */
__attribute__((noreturn)) void arch_power_off(void);

/* résumé de la machine pour le moniteur (processeur, ACPI, minuterie) */
void arch_print_info(void);

/* arrêt définitif du cœur courant */
__attribute__((noreturn)) void arch_halt_forever(void);

/* coupe les interruptions et rend l'état d'avant, pour arch_irq_restore */
uint64_t arch_irq_save(void);
void arch_irq_restore(uint64_t flags);

/* e_machine des exécutables ELF de cette architecture */
int arch_elf_machine(void);

/* attente active (dans un verrou tournant) */
void arch_cpu_relax(void);

/* cœurs : numéro du cœur courant (0 .. MAX_CPUS-1), nombre de cœurs en marche */
#define MAX_CPUS 256
unsigned arch_cpu_id(void);
unsigned arch_cpu_count(void);

struct thread;
/* prépare la pile du noyau d'un thread neuf : la première bascule vers lui
 * appelle sched_thread_start(fn, arg) */
void arch_thread_setup(struct thread *t, void (*fn)(void *), void *arg);
/* état propre à l'architecture d'un thread (FPU…) : création, libération */
int arch_thread_init(struct thread *t);
void arch_thread_free(struct thread *t);
/* bascule de prev vers next (interruptions coupées, verrou de l'ordonnanceur pris) */
void arch_context_switch(struct thread *prev, struct thread *next);
/* registre de thread du mode utilisateur (TLS) du thread courant */
void arch_set_tls(uint64_t v);
uint64_t arch_get_tls(void); /* celui du thread courant */
/* aléa du matériel (RDRAND…) : 1 si *v est rempli */
int arch_hw_random(uint64_t *v);
/* l'horloge du matériel (RTC) : secondes depuis 1970, 0 si inconnue */
uint64_t arch_rtc_seconds(void);
/* fork : le thread fils repart de la trame d'appel du père, résultat 0 */
void arch_fork_setup(struct thread *child, const void *parent_frame);
/* exec : la trame d'appel devient l'entrée du programme neuf */
void arch_exec_frame(void *frame, uint64_t entry, uint64_t sp, uint64_t arg);
/* passe en mode utilisateur : entrée, sommet de pile, argument */
__attribute__((noreturn)) void arch_enter_user(uint64_t entry, uint64_t stack, uint64_t arg);

#endif
