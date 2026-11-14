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
struct image *image_get(const char *path, uint64_t fsize, int need_sys);
void image_put(struct image *im);
/* une référence de plus sur une image déjà tenue (fork : le fils garde ses pages de code) */
void image_hold(struct image *im);
/* mappe l'image dans un espace : pages partagées, copie sur écriture, .bss paresseux */
int image_map(struct image *im, struct vm_space *vm, struct mm *mm, uint64_t *entry, uint64_t *top);
void image_proc_exit(struct process *p);

#endif
