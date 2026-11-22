/* fatfs.h : structures communes de fatfs (fatfs.c : service, cache, fichiers
 * ouverts ; fat32.c, exfat.c : les deux formats). Traduction de fatfs.asm,
 * fat32.asm, exfat.asm : mêmes champs (V_*, DE_*, IT_*, FO_*, NO_*), mêmes
 * règles. */
#ifndef KORIN_FATFS_H
#define KORIN_FATFS_H

#include "fsowner.h"

/* ---- volume (V_*) */
struct vol {
	u64 type;   /* 0 aucun, 1 FAT32, 2 exFAT */
	u64 pstart; /* premier secteur de la partition */
	u64 spc;    /* secteurs par cluster */
	u64 cshift; /* log2(octets par cluster) */
	u64 fat;    /* secteur de la 1re FAT */
	u64 fatsz;  /* secteurs par FAT */
	u64 nfats;
	u64 data;   /* secteur du cluster 2 */
	u64 root;   /* cluster (exFAT : poignée) de la racine */
	u64 nclust; /* clusters de données */
	u64 hint;   /* prochain cluster libre cherché */
	i64 free;   /* FAT32 : clusters libres (-1 : inconnu), FSInfo */
	u64 bitmap; /* exFAT : cluster du bitmap d'allocation */
	u64 bitmap_len;
	u16 *upcase; /* exFAT : table de majuscules (0 : ASCII) */
	u64 ex_bmlba; /* exFAT : secteur du bitmap */
	u64 fsinfo;   /* FAT32 : secteur FSInfo (0 : aucun) */
	u64 fsi_dirty;
};

#define FAT_EOC 0x0FFFFFF8u
#define FAT_MASK 0x0FFFFFFFu
#define ATTR_LFN 0x0F
#define ATTR_VOLUME 0x08
#define ATTR_ARCHIVE 0x20
#define NAME_BYTES 768 /* 255 caractères UTF-16 -> 765 octets UTF-8 au plus */
#define DE_LFN_SIZE 528
#define LFN_MAX 20
#define NAME_UNITS 255

/* les 48 premiers octets d'une DE ; un nœud les a aussi (vol_maj l'accepte) */
struct dehead {
	u64 cluster; /* premier cluster (exFAT : poignée) */
	u64 fsize;
	u64 attr;
	u64 dir;   /* premier cluster du répertoire parent */
	u64 index; /* index de l'entrée 8.3 (exFAT : 0x85) */
	u64 first; /* index de la 1re entrée LFN (= index si aucune) */
};
struct de {
	struct dehead h;
	u64 mtime; /* date << 16 | heure de la dernière écriture */
	u64 pad;
	char name[NAME_BYTES];
	u8 lfn[DE_LFN_SIZE]; /* nom long UTF-16 en cours de lecture (dir_read) */
};

struct it {
	struct vol *vol;
	u64 start;   /* premier cluster du répertoire */
	u64 cluster; /* cluster courant (fichier : FO_CCLUS) */
	u64 index;   /* prochaine entrée (fichier : FO_CIDX) */
};

/* ---- nœud : un fichier ouvert, partagé par ses ouvertures */
struct no {
	struct dehead h; /* NO_START, NO_SIZE, NO_ATTR, NO_DIR, NO_INDEX, NO_FIRST */
	u64 refs;  /* ouvertures (0 : libre) */
	u64 dirty; /* entrée du répertoire à réécrire */
	u64 cclus, cidx; /* curseur de chaîne */
	u64 efface; /* effacé pendant son ouverture : plus de clé */
	u64 pad;
};

/* ---- objet fichier (FO_*) */
struct fo {
	u64 type; /* 0 libre, 1 fichier, 2 répertoire */
	struct it it; /* FO_VOL, FO_START, FO_CCLUS, FO_CIDX */
	u64 size, pos, pdir, index, first, attr, flags, dirty;
	u64 rang;   /* répertoire : rang de la prochaine entrée */
	i64 posp;   /* position de l'objet (FS_POS_CUR, FS_SEEK) */
	struct no *node; /* fichier : son nœud (0 : répertoire) */
	u64 busy;        /* (fils) une op s'en sert : les autres attendent (fatfs.c, fo_tenir) */
};
#define FO_START(f) ((f)->it.start)
#define FO_CCLUS(f) ((f)->it.cluster)
#define FO_CIDX(f) ((f)->it.index)

/* ---- fatfs.c : périphérique et cache, ce que fat32.c et exfat.c attendent */
extern u64 lecture_seule, cc_disk;
extern u64 dos_date_forcee;
u8 *bc_get(u64 sector);
long bc_write(u64 sector);
void bc_defer_on(void);
long bc_defer_off(void);
long blk_zero(u64 sector, u64 count);
long blk_read(u64 sector, u64 count, void *dst);
long blk_write(u64 sector, u64 count, const void *src);
u64 dos_maintenant(void);
long fat_fsinfo_flush(struct vol *v);

/* ---- fat32.c */
long fat_mount(struct vol *v, u64 first);
void fat_clus_lba(struct vol *v, u64 cluster, u64 *sector, u64 *cshift);
u64 fat_next(struct vol *v, u64 cluster);
u64 fat_alloc_x(struct vol *v, u64 prev, long whole);
void fat_free_chain(struct vol *v, u64 first);
void dir_open(struct it *it, struct vol *v, u64 start);
long dir_read(struct it *it, struct de *de);
long fat_lookup(struct vol *v, const char *path, struct de *de);
u8 *dir_entry_at(struct vol *v, u64 dir, u64 index, u64 *sector);
void fat_update_entry(struct vol *v, struct dehead *de);
void fat_erase_entry(struct vol *v, struct dehead *de);
long fat_create(struct vol *v, u64 parent, const char *name, u64 attr, struct de *de);
void utf16_to_utf8(char *dst, const u8 *src, u64 max);
long utf8_to_utf16(const char *s, u16 *dst, u64 max);
long fo_rw(struct fo *f, u8 *buf, u64 len, int write);

long fat_name_equal(const char *a, const char *b);

/* ---- exfat.c */
long exfat_mount(struct vol *v, u64 first);
u8 *ex_entry_at(struct vol *v, u64 dir, u64 index, u64 *sector);
long exfat_dir_read(struct it *it, struct de *de);
long exfat_lookup(struct vol *v, const char *path, struct de *de);
void ex_free_alloc(struct vol *v, u64 handle, u64 len);
long exfat_rw(struct fo *f, u8 *buf, u64 len, int write);
void exfat_update_entry(struct vol *v, struct dehead *de);
void exfat_erase(struct vol *v, struct dehead *de);
void exfat_erase_entries(struct vol *v, struct dehead *de);
long exfat_create(struct vol *v, u64 parent, const char *name, u64 attr, struct de *de);
long exfat_name_equal(struct vol *v, const char *a, const char *b);

/* ---- fatfs.c : fils (lectures en parallèle) */
long vol_lire_rep(struct it *it, struct de *de);
void ft_cond_wait(void);      /* (verrou tenu) relâché jusqu'au prochain ft_cond_broadcast */
void ft_cond_broadcast(void);

/* ---- dirindex.c : un index par répertoire */
long di_lookup(struct vol *v, u64 dir, const char *name, struct de *de); /* 1, 0 absent, -1 pas d'index */
void di_created(struct vol *v, u64 parent, const char *name, const struct de *de);
void di_updated(struct vol *v, const struct dehead *h);
void di_erased(struct vol *v, const struct dehead *h);
void di_drop(u64 dir);
void di_drop_all(void);
void di_stats(u64 *bytes, u64 *dirs);
extern u64 di_hits, di_builds, di_drops;

#endif
