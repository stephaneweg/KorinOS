/* i2chid.h - le touchpad I2C du PC (docs/etude/TOUCHPAD-PC.md) */
#ifndef I2CHID_H
#define I2CHID_H

#include "gesture.h"
#include "hidrd.h"
#include "korinrt.h"
#include "pci.h"
#include "sysfront.h"

/* ---- dw.c : contrôleur I2C DesignWare (LPSS d'Intel), en sondage */
struct dw {
	volatile u8 *regs;   /* BAR0 mappée (4 Ko : DesignWare en 0, registres privés du LPSS en 0x200) */
	u64 phys;            /* son adresse physique */
	u32 clk_khz;         /* horloge d'entrée du contrôleur */
	u32 khz;             /* vitesse du bus : 100 ou 400 */
	u32 hcnt, lcnt;      /* 0 : calculés */
	u32 sda_hold_ns;     /* 0 : 300 ns */
	u32 sda_hold_cycles; /* (FMCN : en cycles, prioritaire) */
	u32 tx_depth, rx_depth;
	u32 tar;             /* adresse de la cible en cours */
	u32 abort_src;       /* IC_TX_ABRT_SOURCE du dernier abandon */
	int verbose;
};

/* registres privés du LPSS : sortie de reset, horloge -> 0, < 0 (pas un I2C du LPSS) */
int lpss_init(struct dw *d);
/* le contrôleur DesignWare : maître, vitesse -> 0, < 0 */
int dw_init(struct dw *d);
/* écriture de wn octets puis (redémarrage) lecture de rn octets, une transaction -> 0 ;
 * -1 abandon (adresse sans réponse : abort_src bit 0), -2 délai dépassé */
int dw_xfer(struct dw *d, u32 addr, const u8 *w, u32 wn, u8 *r, u32 rn);
void dw_disable(struct dw *d);

#endif
