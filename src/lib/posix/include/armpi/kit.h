/*
 * armpi/kit.h -- les kits d'ARMPI depuis le C (docs/KITS.md, docs/POSIX.md) : un kit est une table de
 * fonctions AAPCS64 (SD0:/lib/<nom>.so, en assembleur), partagee par tout le systeme et mappee dans
 * le process a l'ouverture. Les rangs sont ceux de src/kits/<kit>/<kit>.abi (ligne kit_fn n, a
 * partir de 0) ; la version d'un kit est son nombre de fonctions.
 *
 *   void *ak = armpi_kit_open ("audiokit", 11);
 *   long (*ak_open) (long, long, long, long) = armpi_kit_fn (ak, 0);
 *
 * Les en-tetes C d'un kit (<armpi/uikit.h>...) declarent ses fonctions avec leurs types et les
 * resolvent par ces deux appels (relais generes depuis le .abi : tools/kits/abi2c.py).
 */
#ifndef _ARMPI_KIT_H
#define _ARMPI_KIT_H

#ifdef __cplusplus
extern "C" {
#endif

/* le kit <nom> (au moins min_version fonctions) mappe dans le process : sa table, NULL s'il manque
 * (lu sur la carte par AppKit la premiere fois, comme kits_open des programmes en assembleur) */
void *armpi_kit_open (const char *name, unsigned min_version);
/* la fonction de rang <rank> de la table (NULL si le kit en a moins) */
void *armpi_kit_fn (const void *kit, unsigned rank);
/* le nombre de fonctions du kit (sa version) */
unsigned armpi_kit_version (const void *kit);

#ifdef __cplusplus
}
#endif

#endif
