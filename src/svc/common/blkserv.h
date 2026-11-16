/* blkserv.h : la couche commune des périphériques de blocs (protocole
 * src/inc/blk.inc, inchangé : vfs, fatfs, les pilotes asm du Pi), traduction de
 * src/svc/inc/blkserv.inc, seule copie en C (12/11 : réunit l'ancienne de
 * src/svc/common, celle d'imgblk, usbstorage et nvme, et celle de src/svc/sdmmc,
 * celle de sdhci et rtsx). Docs : docs/etude/PC-PILOTES.md, « Couche commune des
 * périphériques de blocs ».
 *
 * Le pilote ne connaît que son matériel : il remplit une struct blk_driver
 * (propriétés et opérations, avec son contexte) et appelle blk_serve, qui ne
 * revient pas. La couche commune ne sait rien du matériel : elle lit la table de
 * partitions (MBR et logiques, ou GPT derrière un MBR protecteur 0xEE, CRC
 * vérifiés), enregistre et sert le disque (« blk.<nom> »), chaque partition
 * (« blk.<nom>p1 »…, GPT : numéro = rang de l'entrée + 1) et les noms de plus
 * (« blk.<alias> »), la partition d'où le PC a démarré en plus (« blk.boot »,
 * module « bootpart » du noyau), un thread par périphérique, un appel au pilote à la fois ;
 * elle tient les clients et leurs tampons, les bornes, la lecture seule, les
 * signaux et, pour un support amovible, le retrait et la remise (table relue).
 *
 * ---- struct blk_driver : future table de module ----
 * Plus tard (docs/etude/PC-PILOTES.md, « blkdev et modules »), un serveur
 * générique blkdev chargera le pilote comme un module, qui lui rendra cette
 * structure. Elle est donc figée comme une table de kit : on AJOUTE des champs à
 * la FIN seulement, on n'en retire ni n'en déplace aucun ; version dit combien
 * de champs le pilote connaît (BLK_DRIVER_V1 : jusqu'à log compris). Un champ
 * d'opération à 0 veut dire « pas fourni » ; la couche fait alors ce que dit son
 * commentaire.
 *
 * Toutes les opérations reçoivent ctx et sont appelées une à la fois (verrou de
 * la couche), sauf denied et log. lba est un secteur du DISQUE (la couche ajoute
 * le début de la partition) ; phys est l'adresse physique de buf (0 : inconnue,
 * le pilote passe alors par son propre tampon). Retour : 0 ou une erreur E_*,
 * rendue telle quelle au client. */
#ifndef KORIN_BLKSERV_H
#define KORIN_BLKSERV_H

#include "fssvc.h"

/* ---- le protocole (src/inc/blk.inc) */
#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_READ 3
#define BLK_WRITE 4
#define BLK_DMA 5
#define BLK_READ_D 6
#define BLK_STATS 7
#define BLK_UHS 8
#define BLK_PLAN 9    /* (disque entier) le plan de disque validé de l'installateur : a drapeaux PLAN_*, b la somme de
		       * l'en-tête GPT principal sur lequel il a été fait (0 : pas de GPT), c nombre de plages ; données :
		       * c x {premier, dernier secteur} à ouvrir en écriture -> 0 : le disque entier devient écrivable dans
		       * ces plages et celles de la GPT (le pilote vérifie et garde) ; E_PERM : pas de mode installation */
#define BLK_RESCAN 10 /* la table de partitions relue (après l'écriture de la GPT) : les nouvelles partitions servies */
#define PLAN_WIPE 1   /* (BLK_PLAN) disque entier : les plages peuvent recouvrir n'importe quelle partition */
#define PLAN_RANGES 24 /* plages au plus */
#define BLK_D_MAX 2048 /* secteurs (1 Mo) */

/* SVC_SIGNAL sur un canal blk.* (« nvme stop », « nvme flush ») */
#define BLK_SIG_STOP 1  /* stop, puis fin du pilote */
#define BLK_SIG_FLUSH 2 /* flush */

/* ---- la table de partitions, vue par l'opération table */
#define BLK_GPT_MAX 16  /* partitions servies au plus (entrées GPT lues : 1024 au plus) */
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
	long ro;          /* proposé (read_only, type_ro) ; l'opération table décide */
	char alias[16];   /* rempli par l'opération table : servie aussi en « blk.<alias> » ("" : non) */
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

/* ---- le pilote */
#define BLK_DRIVER_V1 1
#define BLK_DRIVER_V2 2 /* + plan */

struct blk_driver {
	/* -- propriétés */
	u32 version;      /* BLK_DRIVER_V1 */
	u32 sector_size;  /* octets par secteur : 512 seulement (0 : 512) */
	void *ctx;        /* passé à chaque opération */
	const char *name; /* le disque : « sd0 », « usb0 », « nvme0 » (15 caractères au plus) */
	u64 sectors;      /* support fixe : capacité (0 : inconnue, pas de borne) ; amovible : voir capacity */
	long read_only;   /* 1 : tout en lecture seule (relu à chaque lecture de la table) */
	u8 type_ro;       /* un type de partition MBR servi en lecture seule (0 : aucun) */
	long poll_ms;     /* support amovible : intervalle de present (0 : 500 ms) */

	/* -- opérations du matériel */
	/* n secteurs du disque vers buf ; obligatoire */
	long (*read)(void *ctx, u64 lba, u64 n, void *buf, u64 phys);
	/* n secteurs de buf vers le disque ; 0 : tout en lecture seule (E_PERM) */
	long (*write)(void *ctx, u64 lba, u64 n, const void *buf, u64 phys);
	/* cache d'écriture du disque vidé (BLK_SIG_FLUSH) ; 0 : rien à vider */
	long (*flush)(void *ctx);
	/* support amovible : 1 présent et prêt (ré-identifié s'il a été remis), 0
	 * absent ; *changed = 1 si c'est un autre support que la fois d'avant.
	 * Appelée toutes les poll_ms par le thread principal ; 0 : support fixe (le
	 * thread principal sert alors le disque, comme l'asm) */
	long (*present)(void *ctx, int *changed);
	/* support amovible : capacité en secteurs (0 : absent) ; obligatoire avec present */
	u64 (*capacity)(void *ctx);
	/* BLK_READ_D : n secteurs vers buf, une fenêtre sur la mémoire de l'appelant,
	 * phys[] les adresses physiques de ses pages (buf % 4096 dans la première) ;
	 * 0 : pas de lecture directe (E_AGAIN : le client lit par son tampon) */
	long (*read_sg)(void *ctx, u64 lba, u64 n, void *buf, const u64 *phys);
	/* BLK_DMA : passer en DMA ; 0 : réponse 0 */
	long (*dma)(void *ctx);
	/* BLK_UHS : passer en 1,8 V ; 0 : réponse 0 */
	long (*uhs)(void *ctx);
	/* BLK_STATS : busy_us = attente active en µs (-1 : inchangée), m à remplir
	 * (a à d) -> la réponse ; 0 : E_INVAL */
	long (*stats)(void *ctx, long busy_us, struct msg *m);
	/* BLK_SIG_STOP : arrêt propre du matériel ; la couche finit ensuite le
	 * pilote (exit 0) ; 0 : le signal est ignoré */
	long (*stop)(void *ctx);

	/* -- décisions et journal (facultatifs) */
	/* toute la table, à chaque lecture, avant que rien ne soit servi (ou
	 * mis à jour) : peut changer ro et alias de chaque partition */
	void (*table)(void *ctx, struct blk_part *t, int n, const struct blk_gpt *g);
	/* une écriture refusée (lecture seule) : nom du service, secteur dans le
	 * périphérique, nombre, pid du client ; hors verrou */
	void (*denied)(void *ctx, const char *dev, u64 sector, u64 count, u32 pid);
	/* SVC_SIGNAL : les bits, après stop et flush ; pas de réponse */
	void (*signal)(void *ctx, u64 bits);
	/* une ligne du journal de la couche (table, partitions, retrait) ; 0 : rien
	 * n'est dit ; hors verrou */
	void (*log)(void *ctx, const char *line);
	/* -- BLK_DRIVER_V2 */
	/* BLK_PLAN sur le disque entier : le pilote vérifie le plan (m->a, b, c, données) et ouvre ses plages à sa
	 * garde -> 0 : la couche rend le disque entier écrivable (la garde du pilote décide de chaque secteur) ;
	 * 0 (champ) : E_PERM */
	long (*plan)(void *ctx, struct msg *m);
	/* (BLK_DRIVER_V3 et suivantes : les nouveaux champs ici, à la fin) */
};

/* sert le pilote : lit la table, enregistre et sert les périphériques ; ne
 * revient pas (version inconnue, sector_size autre que 512 : fin, code 1) */
NORETURN void blk_serve(struct blk_driver *d);

/* un nom libre « <préfixe>N » (blk.<préfixe>N pas encore enregistré, N de 0 à
 * 9 ; tous pris : le dernier essayé) dans buf */
void blk_free_name(const char *prefix, char *buf, int size);
long blk_boot_part(void); /* la partition servie en blk.boot (la racine), 0 : aucune sur ce disque */

#endif
