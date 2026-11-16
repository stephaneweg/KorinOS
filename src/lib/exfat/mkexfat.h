/* mkexfat.h : formater un volume en exFAT (docs/etude/INSTALLATEUR.md §10 : FAT32 ou exFAT pour / ; mkfs -t exfat).
 * Autonome (memcpy, memset), comme src/lib/ext4 : l'appelant écrit les secteurs. Jugé par fsck.exfat sous Linux
 * (src/lib/exfat/test/run.sh) ; lu par fatfs (src/svc/fatfs/exfat.c).
 *
 * Le format : secteurs de 512 octets ; la région de démarrage (secteurs 0 à 11) et sa copie (12 à 23) ; une FAT
 * à partir du secteur 2048 (1 Mio) ; le tas des clusters aligné sur 1 Mio. Dans le tas : le bitmap d'allocation,
 * la table des majuscules (upcase.inc), la racine (un cluster : étiquette, bitmap, majuscules). Clusters par
 * défaut, comme Windows : 4 Kio jusqu'à 256 Mio, 32 Kio jusqu'à 32 Gio, 128 Kio au-delà. */
#ifndef KORIN_MKEXFAT_H
#define KORIN_MKEXFAT_H

#include <stdint.h>

#define EXFAT_SECTOR 512
#define EXFAT_WR_MAX 128 /* secteurs au plus par appel de wr (64 Kio) */

/* écrire count secteurs (count <= EXFAT_WR_MAX) à partir du secteur sector du volume -> 0 ou une erreur */
typedef long (*exfat_wr)(void *ctx, uint64_t sector, uint64_t count, const void *buf);

struct mkexfat_opts {
	uint64_t bytes;          /* taille du volume */
	uint32_t cluster;        /* octets par cluster (puissance de 2, 512 à 32 Mio ; 0 : le défaut) */
	const char *label;       /* 11 caractères au plus, UTF-8 (0 : aucun) */
	uint32_t serial;         /* numéro de série, aléatoire */
	uint64_t part_offset;    /* premier secteur de la partition sur le disque (0 : inconnu) */
};

struct mkexfat_info {        /* rempli par mkexfat */
	uint64_t sectors, fat_offset, fat_length, heap_offset, clusters, cluster_bytes;
};

/* -> 0 ; -22 : taille ou cluster impossibles (8 Mio au moins) ; sinon le code de wr */
long mkexfat(const struct mkexfat_opts *o, exfat_wr wr, void *ctx, struct mkexfat_info *info);

#endif
