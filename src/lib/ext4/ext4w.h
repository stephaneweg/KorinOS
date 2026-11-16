/* ext4w.h : écrire sur un volume ext4 (le service ext4fs), sur ext4r.h. Autonome, comme ext4r et mkext4 ; jugé sous
 * Linux par e2fsck -fn après des séries d'opérations (src/lib/ext4/test/run_write.sh).
 *
 * Sans journal pour l'instant (docs/etude/SYSTEME-FICHIERS.md §6, étape 3) : chaque bloc changé est écrit tout de
 * suite ; le superbloc est marqué « pas propre » à la première écriture et « propre » à ext4w_sync. Une coupure
 * pendant une écriture peut laisser une incohérence que seul e2fsck répare.
 *
 * Écrit : les fichiers en extents (arbre de profondeur 0 ou 1 : 4 extents dans l'inode, ou 4 feuilles), les dossiers
 * linéaires (pas ceux indexés par htree : -38), metadata_csum, 64bit, flex_bg, les groupes non initialisés
 * (BLOCK_UNINIT, INODE_UNINIT). Refusé (ext4w_init -> -30, le volume reste en lecture seule) : pas d'extents, un
 * journal à rejouer, gdt_csum sans metadata_csum, bigalloc, quota, mmp, et les fonctions inconnues. Un fichier en
 * blocs indirects ou en ligne (inline_data) n'est pas modifiable (-38). */
#ifndef KORIN_EXT4W_H
#define KORIN_EXT4W_H

#include "ext4r.h"

/* écrire le bloc block (fs->bs octets) ; l'appelant met aussi son cache à jour -> 0 ou une erreur */
typedef long (*ext4w_wr)(void *ctx, uint64_t block, const void *buf);

/* -> 0 : le volume s'écrit ; -30 : lecture seule (format), -5 lecture */
long ext4w_init(struct ext4r *fs, ext4w_wr wr);
void ext4w_time(uint32_t now); /* l'heure des dates écrites (secondes UTC) */

/* créer un fichier vide ou un dossier (le dernier élément du chemin ; son dossier doit exister) -> 0, *out ;
 * -17 existe, -2 dossier absent, -20, -28 plus de place, -36 nom trop long */
long ext4w_create(const char *path, int dir, struct ext4r_inode *out);
/* écrire n octets à pos (le fichier grandit ; *in mis à jour) -> n, ou < 0 */
long ext4w_write(struct ext4r_inode *in, uint64_t pos, const void *buf, uint64_t n);
long ext4w_truncate(struct ext4r_inode *in, uint64_t size);
/* effacer un fichier, ou un dossier vide (-39 sinon) */
long ext4w_unlink(const char *path);
/* renommer (un fichier d'arrivée est remplacé ; un dossier d'arrivée doit être vide) */
long ext4w_rename(const char *from, const char *to);
long ext4w_utime(const char *path, uint32_t t);
/* le superbloc à jour (compteurs) et « propre » */
long ext4w_sync(void);

#endif
