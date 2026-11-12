/* blkserv.h : serveur commun des périphériques blocs (protocole src/inc/blk.inc),
 * traduction de src/svc/inc/blkserv.inc. Un pilote fournit lire et écrire des
 * secteurs du disque entier ; blk_start lit sa table de partitions (MBR) et sert
 * le disque (« blk.<nom> ») et chaque partition (« blk.<nom>p1 »...), un thread
 * par périphérique ; les appels au pilote sont sérialisés. */
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

/* nom du disque, 1 lecture seule, secteurs (0 : inconnu) : ne revient pas */
__attribute__((noreturn)) void blk_start(const char *name, long ro, u64 sectors);

#endif
