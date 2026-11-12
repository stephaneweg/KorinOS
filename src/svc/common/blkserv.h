/* blkserv.h : serveur commun des périphériques blocs (protocole src/inc/blk.inc),
 * traduction de src/svc/inc/blkserv.inc. Un pilote fournit lire et écrire des
 * secteurs du disque entier ; blk_start lit sa table de partitions (MBR, ou GPT
 * derrière un MBR protecteur) et sert le disque (« blk.<nom> ») et chaque
 * partition (« blk.<nom>p1 »...), un thread par périphérique ; les appels au
 * pilote sont sérialisés.
 *
 * Crochets facultatifs (0 : le comportement de l'asm, inchangé pour imgblk et
 * usbstorage) : blk_table_fn voit toute la table avant que rien ne soit servi
 * (lecture seule partition par partition, nom de plus) ; blk_deny_fn est appelé
 * à chaque écriture refusée (partition en lecture seule) ; blk_signal_fn reçoit
 * les SVC_SIGNAL du canal du disque (nvme : arrêt propre). */
#ifndef KORIN_BLKSERV_H
#define KORIN_BLKSERV_H

#include "fssvc.h"

#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_READ 3
#define BLK_WRITE 4
#define BLK_DMA 5
#define BLK_READ_D 6
#define BLK_STATS 7
#define BLK_UHS 8
#define BLK_D_MAX 2048 /* secteurs (1 Mo) */

/* le pilote : secteur du disque, nombre, adresse, son adresse physique (0 : inconnue) -> 0 ou erreur */
typedef long (*blk_fn)(u64 sector, u64 count, void *addr, u64 phys);
extern blk_fn blk_read_fn, blk_write_fn;
extern blk_fn blk_dma_fn, blk_uhs_fn; /* (0 : pas de DMA, pas d'UHS) */
/* lecture directe : x3 = adresses physiques des pages de l'appelant (0 : pas de lecture directe) */
extern long (*blk_sg_fn)(u64 sector, u64 count, void *addr, const u64 *phys);
extern long (*blk_stat_fn)(long busy_us, struct msg *m);
extern u8 blk_type_ro; /* un type de partition servi en lecture seule (0 : aucun) */

/* ---- la table vue par le pilote (blk_table_fn) */
#define BLK_GPT_MAX 16 /* partitions GPT servies au plus (entrées lues : 1024 au plus) */
#define BLK_ALIAS_MAX 4 /* noms de plus (« blk.<alias> ») */

struct blk_part {
	long num;         /* MBR : 1-4, logiques 5-12 ; GPT : rang de l'entrée + 1 */
	u64 first, count; /* secteurs du disque */
	u8 type;          /* type MBR (GPT : 0xEE) */
	u8 gpt;           /* 1 : entrée GPT */
	u8 type_guid[16]; /* GPT : tels qu'écrits sur le disque (3 premiers champs en petit-boutiste) */
	u8 uniq_guid[16];
	u64 attrs;        /* GPT : attributs */
	u16 name[36];     /* GPT : nom (UTF-16) */
	long ro;          /* proposé (disque en lecture seule, blk_type_ro) ; le crochet décide */
	char alias[16];   /* rempli par le crochet : servie aussi en « blk.<alias> » ("" : non) */
};

/* l'en-tête GPT (gpt = 0 : MBR ou pas de table) */
struct blk_gpt {
	int gpt;          /* 1 : table GPT lue */
	int crc_ok;       /* 1 : CRC de l'en-tête et des entrées justes */
	u64 first_usable, last_usable, alt_lba;
	u8 disk_guid[16];
	u32 entries, entry_size;
	u64 entries_lba;
};

/* toute la table, avant que rien ne soit servi ; le crochet peut changer ro et alias */
extern void (*blk_table_fn)(struct blk_part *t, int n, const struct blk_gpt *g);
/* une écriture refusée : nom du service, secteur (dans le périphérique), nombre, pid */
extern void (*blk_deny_fn)(const char *dev, u64 sector, u64 count, u32 pid);
/* SVC_SIGNAL sur le canal du disque : les bits (pas de réponse) */
extern void (*blk_signal_fn)(u64 bits);

/* nom du disque, 1 lecture seule, secteurs (0 : inconnu) : ne revient pas */
__attribute__((noreturn)) void blk_start(const char *name, long ro, u64 sectors);

#endif
