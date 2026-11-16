/* mkext4.h : formater un volume en ext4 (docs/etude/SYSTEME-FICHIERS.md §4.2, §6 étape 5 ; l'installateur et mkfs).
 * Autonome (memcpy, memset), comme src/lib/gpt : l'appelant écrit les blocs. Jugé par e2fsck -fn sous Linux
 * (src/lib/ext4/test/run.sh).
 *
 * Le format : blocs de 4 Kio, groupes de 32 768 blocs, inodes de 256 octets (un pour 16 Kio), extents, journal
 * jbd2 (dans un groupe sans copie du superbloc), metadata_csum (crc32c), sparse_super, large_file, huge_file,
 * dir_nlink, extra_isize ; pas de 64bit (16 Tio au plus), ni dir_index, ni flex_bg, ni resize_inode. Les tables
 * d'inodes des groupes 1 et suivants ne sont pas mises à zéro (INODE_UNINIT, bg_itable_unused), comme mke2fs avec
 * lazy_itable_init : formater 512 Go écrit quelques dizaines de Mo. */
#ifndef KORIN_MKEXT4_H
#define KORIN_MKEXT4_H

#include <stdint.h>

#define EXT4_BLOCK 4096

/* écrire count blocs de 4096 octets à partir du bloc block (du volume) -> 0 ou une erreur */
typedef long (*ext4_wr)(void *ctx, uint64_t block, uint64_t count, const void *buf);

struct mkext4_opts {
	uint64_t bytes;       /* taille du volume */
	const char *label;    /* 16 caractères au plus (0 : aucun) */
	uint8_t uuid[16];     /* aléatoire, de l'appelant */
	uint32_t hash_seed[4];/* aléatoire */
	uint32_t now;         /* secondes UTC */
};

struct mkext4_info {   /* rempli par mkext4 */
	uint64_t blocks, inodes, groups, journal_blocks, free_blocks;
};

/* -> 0 ; E_INVAL-like (-22) : volume trop petit (8 Mio au moins) ; sinon le code de wr */
long mkext4(const struct mkext4_opts *o, ext4_wr wr, void *ctx, struct mkext4_info *info);

uint32_t ext4_crc32c(uint32_t crc, const void *p, uint64_t n); /* sans inversion finale (comme le noyau) */

#endif
