/* gpt.h : lire et écrire une table de partitions GPT (UEFI 2.10, chapitre 5), pour l'installateur, nvme (la
 * vérification du plan de disque) et la commande gpt (docs/etude/INSTALLATEUR.md §3.4). Autonome : seulement
 * memcpy, memset et memcmp ; le disque se lit et s'écrit par deux fonctions de l'appelant, par secteurs de 512
 * octets (un disque de secteurs de 4096 octets : plus tard).
 *
 *   struct gpt g;
 *   gpt_read(&g, rd, ctx, secteurs)          la table du disque (la principale, sinon celle de secours)
 *   gpt_init(&g, secteurs, guid)             une table neuve, vide
 *   gpt_add(&g, type, debut, fin, nom, guid) une partition (debut, fin : secteurs, fin comprise)
 *   gpt_free_space(&g, &debut, &fin)         le plus grand espace libre, aligné sur 1 Mio
 *   gpt_write(&g, wr, ctx)                   MBR protecteur, en-tête et entrées, copie de secours, sommes
 */
#ifndef KORIN_GPT_H
#define KORIN_GPT_H

#include <stdint.h>

#define GPT_SECTOR 512
#define GPT_ENTRIES 128      /* entrées (128 octets chacune : 32 secteurs) */
#define GPT_ALIGN 2048       /* 1 Mio, en secteurs */

typedef long (*gpt_rw)(void *ctx, uint64_t sector, uint64_t count, void *buf);

struct gpt_entry {
	uint8_t type[16];    /* tout à zéro : entrée libre */
	uint8_t guid[16];    /* GUID unique de la partition */
	uint64_t first, last; /* secteurs, le dernier compris */
	uint64_t attrs;
	uint16_t name[36];   /* UTF-16LE */
};

struct gpt {
	uint64_t disk_sectors;
	uint8_t disk_guid[16];
	uint64_t first_usable, last_usable;
	uint32_t entries_crc; /* (lu, ou calculé par gpt_write) */
	uint32_t header_crc;
	int from_backup;      /* gpt_read : la principale était abîmée, la copie de secours a servi */
	struct gpt_entry e[GPT_ENTRIES];
};

/* les types connus (GUID au format du disque : les trois premiers champs en petit-boutiste) */
extern const uint8_t GPT_T_EFI[16], GPT_T_BASIC[16], GPT_T_MSR[16], GPT_T_RECOVERY[16], GPT_T_LINUX[16];

uint32_t gpt_crc32(uint32_t crc, const void *p, uint64_t n);

/* -> 0 ; < 0 : pas de GPT (-2), sommes fausses partout (-3), lecture (code de rd) */
long gpt_read(struct gpt *g, gpt_rw rd, void *ctx, uint64_t disk_sectors);
/* une table vide pour un disque de disk_sectors secteurs (guid : 16 octets aléatoires de l'appelant) */
void gpt_init(struct gpt *g, uint64_t disk_sectors, const uint8_t guid[16]);
/* -> le numéro de l'entrée (1..128) ; -1 : plus de place, -2 : hors de la zone utile, -3 : chevauche une autre */
int gpt_add(struct gpt *g, const uint8_t type[16], uint64_t first, uint64_t last, const char *name,
	    const uint8_t guid[16]);
void gpt_del(struct gpt *g, int num);
/* le plus grand espace libre (aligné sur GPT_ALIGN) -> 1, 0 : aucun */
int gpt_free_space(const struct gpt *g, uint64_t *first, uint64_t *last);
/* les sommes, puis MBR protecteur, en-tête et entrées principaux, entrées et en-tête de secours -> 0 ou code de wr */
long gpt_write(struct gpt *g, gpt_rw wr, void *ctx);
/* « 0FC63DAF-8483-4772-8E79-3D69D8477DE4 » <-> 16 octets au format du disque */
int gpt_guid_parse(const char *s, uint8_t out[16]);
void gpt_guid_text(const uint8_t g[16], char out[37]);
/* le nom (UTF-16LE, ASCII seulement pour l'instant) <-> texte */
void gpt_name_set(struct gpt_entry *e, const char *s);
void gpt_name_get(const struct gpt_entry *e, char *out, int cap);
int gpt_used(const struct gpt_entry *e);

#endif
