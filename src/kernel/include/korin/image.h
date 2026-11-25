/* Images des exécutables (core/image.c) et kits (core/kits.c) : docs/ABI.md,
 * 0x1A, 0x46-0x48, 0x64-0x67. */
#ifndef KORIN_IMAGE_H
#define KORIN_IMAGE_H

#include <stdint.h>

struct image;
struct process;
struct vm_space;
struct mm;

int64_t sys_image_create(uint64_t path, uint64_t fsize, uint64_t flags);
int64_t sys_image_seal(uint64_t window, uint64_t flags);
int64_t sys_image_forget(uint64_t path);
int64_t sys_image_list(uint64_t buf, uint64_t max);
int64_t sys_image_preload(uint64_t path, uint64_t image, uint64_t size);
int64_t sys_spawn_preloaded(uint64_t path, uint64_t fsize, uint64_t params);
int64_t sys_kit_register(uint64_t name, uint64_t image, uint64_t size);
int64_t sys_lib_open(uint64_t name, uint64_t vmin);

/* l'image valide de ce chemin et de cette taille (une référence de plus) */
/* uid : celui du process qui en sera lancé ; une image sans droits (im->sys = 0) n'est prise que par
 * cet uid (sinon un compte scelle un faux /bin/sudo que lance un autre) */
struct image *image_get(const char *path, uint64_t fsize, int need_sys, int uid);
void image_put(struct image *im);
/* une référence de plus sur une image déjà tenue (fork : le fils garde ses pages de code) */
void image_hold(struct image *im);
/* mappe l'image dans un espace : pages partagées, copie sur écriture, .bss paresseux */
int image_map(struct image *im, struct vm_space *vm, struct mm *mm, uint64_t *entry, uint64_t *top);
void image_proc_exit(struct process *p);
/* chemin d'une image : celui que vfs a résolu (PATH_MAX de vfs) ; 80 octets jusqu'au 21/11/2026, trop peu pour
 * les programmes de cargo (target/debug/build/<caisse>-<empreinte>/build-script-build : EINVAL à l'exec) */
#define IMAGE_PATH_MAX 256
/* chemin déclaré (tronqué à n octets) et SHA-256 du contenu d'une image tenue (SYS_PROC_EXE) */
void image_identity(struct image *im, char *path, uint64_t n, uint8_t hash[32]);
int image_is_sys(const struct image *im); /* créée par un process aux droits de /sys (vfs) : SYS_PROC_EXE bit 3 */

#endif
