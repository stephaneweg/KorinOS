/* Démarrage (core/boot.c) : modules de l'initrd, fichiers donnés par init,
 * verrou du démarrage, options du noyau. */
#ifndef KORIN_BOOT_H
#define KORIN_BOOT_H

#include <stdint.h>

int64_t sys_mod_lancer(uint64_t name, uint64_t args);
int64_t sys_mod_lire(uint64_t name, uint64_t buf, uint64_t max);
int64_t sys_boot_file(uint64_t name, uint64_t image, uint64_t size);
int64_t sys_demarrage_pret(void);
/* un mot de SD0:/etc/options.txt (après DEMARRAGE_PRET) */
int kernel_option(const char *word);
/* attend DEMARRAGE_PRET, ms au plus : 1 s'il est venu */
int boot_wait_ready(uint64_t ms);

#endif
