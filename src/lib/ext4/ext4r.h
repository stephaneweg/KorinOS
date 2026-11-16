/* ext4r.h : lire un volume ext4 (et ext2, ext3) : le service ext4fs (src/svc/ext4fs), docs/etude/SYSTEME-FICHIERS.md.
 * Autonome (memcpy, memset), comme mkext4 : l'appelant lit les blocs (et les garde en cache). Jugé sous Linux
 * contre des images faites par mke2fs -d (src/lib/ext4/test/run_read.sh).
 *
 * Pris en charge : blocs de 1 à 64 Kio, extents et blocs indirects (ext2, ext3), 64bit, flex_bg, les dossiers
 * indexés (htree : lus en parcours linéaire), les données en ligne (inline_data : i_block et « system.data »), les
 * liens symboliques (suivis par ext4r_lookup, 8 au plus), un journal à rejouer (needs_recovery : lu tel quel,
 * signalé par EXT4R_DIRTY). Refusé : meta_bg, journal externe, dirdata, chiffrement. */
#ifndef KORIN_EXT4R_H
#define KORIN_EXT4R_H

#include <stdint.h>

/* le bloc block du volume (taille fs->bs) -> ses octets, 0 si erreur ; le pointeur reste valable jusqu'au
 * prochain appel */
typedef const uint8_t *(*ext4r_rd)(void *ctx, uint64_t block);

#define EXT4R_DIRTY 1 /* (flags) le journal n'a pas été rejoué : le volume peut ne pas être à jour */

struct ext4r {
	ext4r_rd rd;
	void *ctx;
	uint32_t bs;          /* octets par bloc */
	uint64_t blocks, free_blocks;
	uint32_t inodes, ipg, bpg, isz, descsz, first_data;
	uint64_t ngroups;
	uint32_t incompat, ro_compat, flags;
	char label[17];
};

struct ext4r_inode {
	uint32_t ino;
	uint16_t mode;        /* S_IF* (0x4000 dossier, 0x8000 fichier, 0xA000 lien) */
	uint32_t flags;
	uint64_t size;
	uint32_t mtime;
	uint8_t block[60];    /* i_block : extents, blocs, ou les données en ligne */
	uint32_t xlen;        /* inline_data : la suite des données (attribut « system.data » de l'inode) */
	uint8_t xdata[192];
};

#define EXT4R_ROOT 2
#define EXT4R_ISDIR(m) (((m) & 0xF000) == 0x4000)
#define EXT4R_ISLNK(m) (((m) & 0xF000) == 0xA000)

/* -> 0 ; -22 pas de l'ext (ou un format refusé), -5 lecture */
long ext4r_mount(struct ext4r *fs, ext4r_rd rd, void *ctx);
long ext4r_inode(struct ext4r *fs, uint32_t ino, struct ext4r_inode *in);
/* bloc logique -> le bloc du volume (0 : un trou), *run blocs contigus à partir de là (1 au moins) ; < 0 erreur */
long ext4r_map(struct ext4r *fs, const struct ext4r_inode *in, uint64_t lblk, uint64_t *pblk, uint64_t *run);
/* lit n octets à pos (bornés par la taille) -> octets lus, < 0 erreur */
long ext4r_read(struct ext4r *fs, const struct ext4r_inode *in, uint64_t pos, void *buf, uint64_t n);
/* chemin (« a/b », « /a/b » ; casse exacte, sinon sans la casse ASCII ; liens suivis) -> 0, *in rempli ;
 * -2 absent, -20 un élément n'est pas un dossier */
long ext4r_lookup(struct ext4r *fs, const char *path, struct ext4r_inode *in);
/* l'entrée suivante du dossier à partir de *pos (octets) -> 1 (name terminé par 0, *ino, *type : 1 fichier,
 * 2 dossier, 7 lien, 0 inconnu), 0 en fin, < 0 erreur ; « . » et « .. » sautés */
long ext4r_readdir(struct ext4r *fs, const struct ext4r_inode *dir, uint64_t *pos, char *name, uint32_t *ino,
		   uint8_t *type);

#endif
