/* nvme.h : le pilote NVMe en C (docs/etude/PC-PILOTES.md, « NVMe »).
 *
 * Trois morceaux :
 *  * nvme.c : le contrôleur (spécification publique « NVM Express Base » 1.4 et
 *    « NVM Command Set ») : remise à zéro, file d'administration, IDENTIFY,
 *    une file d'E/S, lecture, écriture (PRP, listes PRP), FLUSH, arrêt (CC.SHN) ;
 *    commun aux machines (rien de x86) ;
 *  * policy.c : qui a le droit d'écrire (règles de nvme.txt et de -w, refus
 *    impossibles à lever, garde des plages au plus bas) ;
 *  * plat_<arch>.c : la machine (PC : la fonction de pcid, BAR0, MSI-X ;
 *    Pi : aucun NVMe).
 * Le service blk.* est celui de src/svc/common/blkserv.c. */
#ifndef KORIN_NVME_H
#define KORIN_NVME_H

#include "blkserv.h"

#define E_NODEV -19
#define E_TIMEDOUT -110

/* ---- la machine */
struct nvme_plat {
	volatile u8 *regs;  /* BAR0 mappée (registres non cachés) */
	u64 regs_size;
	long irq_line;      /* ligne liée (IRQ_WAIT), -1 : sondage */
	int bounce_only;    /* tampons des clients cachés (pas de DMA cohérent) : tout par le tampon */
	char where[64];     /* pour le journal */
};

/* la fonction du contrôleur (adresse « SSSS:BB:DD.F », ou 0 : le premier) -> 0,
 * E_BUSY (déjà prise par un autre pilote), < 0 */
int nvme_plat_init(const char *arg, struct nvme_plat *p);
/* une interruption (MSI-X, sinon MSI) pour le vecteur 0 -> ligne, ou -1 : sondage */
long nvme_plat_irq(struct nvme_plat *p);

/* ---- le journal (SYS_PRINT : écran et journal du noyau) */
extern int nv_verbose;
void nv_log(const char *fmt, ...);
void nv_dbg(const char *fmt, ...);

/* ---- le contrôleur (nvme.c), pour policy.c */
/* lecture brute de secteurs du disque, hors service (au démarrage) -> 0, < 0 */
long nv_read_raw(u64 lba, u64 n, void *buf);
extern u64 nv_sectors;

/* ---- les droits d'écriture (policy.c) */
/* une ligne de règles (« write = … », « name = … ») ; src : pour le journal -> 0, < 0 */
int policy_line(const char *line, const char *src);
/* le fichier de règles (texte entier) */
void policy_text(char *text, const char *src);
/* -r : tout en lecture seule, les règles ne comptent plus */
extern int policy_all_ro;
/* -G : essai de la garde du plus bas niveau (le service blk ouvre tout) ; seulement sur le
 * contrôleur de QEMU (modèle « QEMU NVMe Ctrl »), jamais sur un vrai disque */
extern int policy_guard_test;
/* le nom du disque (« nvme0 ») pour les conditions disk= */
extern const char *policy_disk;
/* opération table de blkserv : décide de chaque partition */
void policy_table(struct blk_part *t, int n, const struct blk_gpt *g);
/* la garde du plus bas niveau : [lba, lba + n) entier dans une partition permise -> 1 */
int policy_write_ok(u64 lba, u64 n);
/* opération denied de blkserv : une écriture refusée, dite (les 20 premières, puis une sur 1000) */
void policy_denied(const char *dev, u64 sector, u64 count, u32 pid);
/* essai de la garde, sans aucune écriture : refus attendus -> nombre d'erreurs */
int policy_selftest(void);
extern int policy_install;                 /* « install = yes » (nvme.txt de la clé d'installation) */
long policy_plan(struct msg *m);           /* BLK_PLAN : le plan vérifié et ouvert -> 0 ou erreur */

/* barrières : DMA cohérent sur le PC ; sur une machine sans, dsb */
#if defined(__aarch64__)
#define nv_wmb() __asm__ volatile("dsb sy" ::: "memory")
#define nv_rmb() __asm__ volatile("dsb sy" ::: "memory")
#else
#define nv_wmb() __asm__ volatile("" ::: "memory")
#define nv_rmb() __asm__ volatile("" ::: "memory")
#endif

#endif
