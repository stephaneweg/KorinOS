/* sdmmc.h : la carte SD en C, commune au Pi et au PC (docs/etude/PC-PILOTES.md,
 * section « Carte SD »).
 *
 * Trois couches, séparées :
 *  * l'hôte (le contrôleur) : sdhci.c (SDHCI de la spécification publique :
 *    EMMC2 et Arasan du Pi, sdhci-pci de QEMU, lecteurs PCI de classe 08/05),
 *    rtsx.c (lecteur Realtek PCIe RTS5261 du ZBook, jeu de registres « rtsx ») ;
 *    il ne sait qu'envoyer une commande (avec ses données) et régler bus et
 *    horloge : struct sd_host ;
 *  * la carte (sdcard.c) : le protocole de la spécification simplifiée de la SD
 *    Association (identification CMD0/8/55/41, CID, CSD, SCR, RCA, adresse en
 *    octets ou en blocs, bus 4 bits, CMD6 haute vitesse, blocs multiples,
 *    erreurs, retrait) ;
 *  * le service des blocs (blkserv.c) : le protocole blk.* de src/inc/blk.inc,
 *    inchangé (vfs et fatfs), le disque « blk.sd0 » et ses partitions MBR (et
 *    logiques) ou GPT « blk.sd0p1 »…, comme src/svc/inc/blkserv.inc.
 *
 * Toutes les traces vont au journal du noyau (SYS_PRINT : écran et journal) :
 * sd_log (toujours), sd_dbg (avec -v). */
#ifndef KORIN_SDMMC_H
#define KORIN_SDMMC_H

#include "korinrt.h"

/* ---- erreurs (en plus de korinrt.h) */
#define E_BUSY -16
#define E_NODEV -19
#define E_TIMEDOUT -110
#define E_NOMEDIUM -123

/* ---- réponses (spécification SD, partie 1, §4.9) */
#define SD_RSP_NONE 0
#define SD_RSP_R1 1  /* 48 bits, CRC, index */
#define SD_RSP_R1B 2 /* R1 et occupation sur DAT0 */
#define SD_RSP_R2 3  /* 136 bits (CID, CSD) */
#define SD_RSP_R3 4  /* OCR, sans CRC ni index */
#define SD_RSP_R6 5  /* RCA */
#define SD_RSP_R7 6  /* CMD8 */

/* une commande : resp[] dans la convention SDHCI (comme l'asm) : réponse
 * courte dans resp[0] (bits 39-8 de la réponse) ; R2 : le bit n du registre
 * (CID, CSD) est le bit n-8 de resp[0..3] (l'octet du CRC est retiré) */
struct sd_cmd {
	unsigned idx;
	uint32_t arg;
	unsigned rsp;
	uint32_t resp[4];
	/* données (blocks = 0 : aucune) */
	void *buf;        /* adresse dans ce process */
	uint64_t phys;    /* son adresse physique (0 : inconnue : l'hôte passe par son tampon) */
	unsigned blksz;   /* 512, ou 64 (CMD6), 8 (ACMD51) */
	unsigned blocks;
	int write;
	/* l'hôte remplit, sur une erreur, ce qu'il a lu de son état (trace) */
	uint32_t err_state;
};

/* ---- l'hôte */
struct sd_host {
	const char *name;      /* « sdhci », « rtsx » */
	void *priv;
	unsigned max_blocks;   /* blocs de 512 par commande au plus (son tampon) */
	unsigned caps;         /* SDH_* */
	/* contrôleur remis à zéro, carte alimentée (3,3 V), 400 kHz, bus 1 bit ->
	 * 0, E_NOMEDIUM (pas de carte), < 0 */
	int (*power_up)(struct sd_host *h);
	void (*power_off)(struct sd_host *h);
	/* 1 carte présente, 0 absente (lecture du registre, sans commande) */
	int (*present)(struct sd_host *h);
	/* 1 : protégée en écriture (le petit curseur de la carte), 0 sinon */
	int (*write_protected)(struct sd_host *h);
	/* hz : 400000, 25000000, 50000000 ; hs : 1 haute vitesse (front, CMD6 pris) */
	int (*set_clock)(struct sd_host *h, unsigned hz, int hs);
	int (*set_width)(struct sd_host *h, int bits);
	/* une commande ; avec données de plusieurs blocs (CMD18, CMD25) l'hôte
	 * termine lui-même par CMD12 (SDHCI : Auto CMD12 ; rtsx : CMD12 envoyé)
	 * -> 0, E_TIMEDOUT (pas de réponse), E_IO (CRC, données), E_NOMEDIUM */
	int (*cmd)(struct sd_host *h, struct sd_cmd *c);
	/* tout ce qui est en cours arrêté, lignes remises (après une erreur) */
	void (*recover)(struct sd_host *h);
	/* diagnostic : registres décrits dans le journal */
	void (*dump)(struct sd_host *h, const char *why);
	/* une ligne d'état courte (mode de transfert, interruptions) */
	const char *(*describe)(struct sd_host *h);
};

#define SDH_4BIT 1 /* bus 4 bits */
#define SDH_HS 2   /* haute vitesse 50 MHz */

/* ---- la carte (sdcard.c) */
struct sd_card {
	struct sd_host *host;
	int ready;         /* identifiée, sélectionnée, prête aux transferts */
	int hc;            /* SDHC/SDXC : adresse en blocs ; 0 : SDSC (octets) */
	int v2;            /* a répondu à CMD8 */
	uint32_t ocr;
	uint32_t rca;      /* << 16 (argument des commandes) */
	uint32_t cid[4], csd[4];
	uint8_t scr[8];
	uint64_t sectors;  /* capacité (CSD) */
	int width;         /* 1 ou 4 */
	int hs;            /* 1 : 50 MHz */
	int ro;            /* curseur de protection */
	unsigned gen;      /* augmente à chaque identification (retrait, remise) */
	/* compteurs (BLK_STATS) */
	uint64_t n_cmds, n_blocks, n_errors, n_retries;
};

int sd_card_init(struct sd_card *c, struct sd_host *h);
/* lba, nombre de blocs de 512, tampon, son physique (0 : inconnu) -> 0, < 0 */
int sd_read(struct sd_card *c, uint64_t lba, unsigned n, void *buf, uint64_t phys);
int sd_write(struct sd_card *c, uint64_t lba, unsigned n, const void *buf, uint64_t phys);
/* présence vérifiée ; carte retirée : ready = 0 -> 1 présente et prête, 0 */
int sd_card_check(struct sd_card *c);
void sd_card_describe(const struct sd_card *c);

/* ---- service des blocs (blkserv.c) */
struct blk_driver {
	/* x0 = secteur du disque, nombre, adresse, physique (0 : inconnue) -> 0, < 0 */
	int (*read)(void *ctx, uint64_t lba, unsigned n, void *buf, uint64_t phys);
	int (*write)(void *ctx, uint64_t lba, unsigned n, const void *buf, uint64_t phys);
	/* capacité (secteurs) ; 0 : support absent */
	uint64_t (*sectors)(void *ctx);
	/* 1 : support présent et prêt (re-identifié s'il a été remis) ;
	 * *changed = 1 si c'est un autre support depuis l'appel précédent */
	int (*check)(void *ctx, int *changed);
	/* compteurs de BLK_STATS : commandes, erreurs, reprises */
	void (*stats)(void *ctx, uint64_t out[4]);
	void *ctx;
	int read_only;      /* tout le disque en lecture seule */
	uint8_t type_ro;    /* un type de partition MBR en lecture seule (0 : aucun) */
};

/* « sd0 » : enregistre blk.sd0 et ses partitions et les sert ; ne revient pas
 * (le thread principal surveille le support toutes les poll_ms) */
NORETURN void blk_serve(const char *name, struct blk_driver *d, long poll_ms);
/* un nom libre « sdN » (blk.sdN pas encore enregistré) dans buf (8 o) */
void blk_free_name(const char *prefix, char *buf);

/* ---- outils communs */
extern int sd_verbose; /* -v : chaque commande dans le journal */
extern const char *sd_tag; /* début des lignes du journal (« sdhci », « rtsx ») */
void sd_log(const char *fmt, ...);
void sd_dbg(const char *fmt, ...);
void sd_sleep_ms(long ms);
long sd_now_ms(void);
/* verrou des appels au matériel (blkserv : un appel à la fois) */
void sd_lock(void);
void sd_unlock(void);
int sd_trylock(void);

/* barrières : DMA cohérent sur le PC ; mémoire non cachée et barrières sur le Pi */
#if defined(__aarch64__)
#define sd_wmb() __asm__ volatile("dsb sy" ::: "memory")
#define sd_rmb() __asm__ volatile("dsb sy" ::: "memory")
#else
#define sd_wmb() __asm__ volatile("" ::: "memory")
#define sd_rmb() __asm__ volatile("" ::: "memory")
#endif

#endif
