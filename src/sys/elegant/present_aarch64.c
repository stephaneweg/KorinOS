/* present_aarch64.c : la présentation sur le Pi (present.inc) : la boîte aux lettres du
 * firmware, le DMA 2D du BCM2711, la copie par le processeur.
 *
 *  * tampon arrière : dma_alloc DMA_CACHED (contigu, caché : le dessin reste rapide) ; chaque
 *    ligne d'une zone est nettoyée (DC CVAC, permis en EL0 par SCTLR_EL1.UCI) avant le lancement ;
 *  * blocs de contrôle et essai : une page dma_alloc non cachée ;
 *  * canal : le plus haut des canaux complets (0-6) que le firmware laisse à l'ARM
 *    (GET_DMA_CHANNELS) : 6 sur le Pi 4 (comme le framebuffer de Circle), 5 sous QEMU. Le son du
 *    noyau prend les canaux « lite » (7 et plus : audio.asm), les autres pilotes (sd, usbd,
 *    genet) ont leur propre DMA ; rien d'autre ne touche aux canaux 0-6 ;
 *  * adresses de bus : physique | 0xC0000000 (premier Go, que le noyau gère seul) ;
 *  * essai au démarrage : 2 lignes de 16 octets (YLENGTH = 1) d'une zone à une autre de la page
 *    non cachée. Le vrai BCM2711 copie YLENGTH + 1 lignes (Circle : nBlockCount - 1), QEMU en
 *    copie YLENGTH : l'essai donne le décalage (dma_ybias). En 128 bits (comme Circle) : QEMU ne
 *    l'émule pas (erreur du canal), d'où la copie processeur sous QEMU ; l'option
 *    « dispdma_qemu » y essaie alors 32 bits (bon pour vérifier la chaîne de blocs, mais lent :
 *    QEMU copie mot à mot, ~100 ms par écran). Échec (rien de copié, erreur, pas de réponse en
 *    50 ms) -> copie par le processeur ;
 *  * en marche : un transfert sans fin après DMA_TIMEOUT ms, ou une erreur du canal -> canal remis
 *    à zéro, retour définitif à la copie processeur, tout l'écran recomposé (present.c). */
#include "elegant.h"

#define DMA_PHYS 0xFE007000UL /* DMA du BCM2711 (canaux 0-14), adresse ARM */
#define DMA_ENABLE_R 0xFF0
#define DMA_CS 0x00
#define DMA_CONBLK 0x04
#define DMA_DEBUG 0x20
#define CS_RESET (1u << 31)
#define CS_ERROR (1u << 8)
#define CS_ACTIVE 1u
/* WAIT_WRITES, PANIC 15, PRIO 1, END (efface), ACTIVE */
#define CS_GO ((1u << 28) | (15u << 20) | (1u << 16) | (1u << 1) | 1u)
/* rafale 2, 128 bits, SRC_INC, DEST_INC, TDMODE */
#define TI_2D ((2u << 12) | (1u << 9) | (1u << 8) | (1u << 5) | (1u << 4) | (1u << 1))
#define TI_2D_32 (TI_2D & ~((1u << 9) | (1u << 5))) /* (QEMU : 128 bits non émulés) */
#define DEBUG_LITE 28 /* (bit) canal « lite » : pas de mode 2D */
#define DMA_IRQ_BASE 112 /* ligne du GIC du canal 0 (canaux 0-10 : une par canal) */
#define TI_INTEN 1u /* interruption à la fin du bloc */
#define CS_INT 4u
#define DMA_CHANNELS_DEF 0x71F5 /* masque par défaut (Circle, Pi 4) */

typedef u64 u64u __attribute__((aligned(4), may_alias));

static u64 pres_phys;       /* (dma_alloc) adresse physique rendue */
static u8 *dma_base;        /* registres du DMA (dev_map), du canal */
static u8 *dma_regs;
static long dma_chan;
static u8 *dma_cb;          /* blocs de contrôle (page non cachée) : adresse, adresse de bus */
static u64 dma_cb_bus;
static long dma_ybias;      /* 1 : YLENGTH = lignes - 1 (BCM2711), 0 : QEMU */
static u32 dma_ti;          /* TI des blocs (largeur 128 ou 32 bits) */
static long dma_irq;        /* ligne du canal (0 : pas d'interruption) */
static u32 pres_fw[8] __attribute__((aligned(16))); /* message au firmware */

static const char s_dispdma_qemu[] = "dispdma_qemu";
static const char m_r_size[] = "écran trop grand pour le mode 2D)\n";
static const char m_r_mem[] = "pas de mémoire DMA)\n";
static const char m_r_fb[] = "adresse de l'écran inconnue)\n";
static const char m_r_chan[] = "aucun canal DMA libre)\n";
static const char m_r_map[] = "registres du DMA refusés)\n";
static const char m_r_lite[] = "canal DMA « lite »)\n";
static const char m_r_reset[] = "canal DMA bloqué)\n";
static const char m_r_emul[] = "DMA 128 bits absent : émulateur ; dispdma_qemu pour l'essayer)\n";
static const char m_r_trial[] = "essai du DMA 2D raté)\n";
static const char m_dma[] = "elegant : copie vers l'écran par DMA 2D asynchrone, canal ";
static const char m_dma_32[] = ", 32 bits (128 non émulés)";
static const char m_dma_ylen[] = ", YLENGTH = lignes";

static inline u32 rd32(const u8 *r, long off)
{
	return *(volatile const u32 *)(r + off);
}

static inline void wr32(u8 *r, long off, u32 v)
{
	*(volatile u32 *)(r + off) = v;
}

static inline long now_ms(void)
{
	return korin_sys0(SYS_TIME_MS);
}

/* zone (x0, y0, x1, y1) : copie du tampon arrière vers le framebuffer, par le processeur (de
 * core.inc) ; deux pixels par mot, l'octet alpha forcé si l'écran le lit (fb_alpha) */
void flush_rect(long x0, long y0, long x1, long y1)
{
	long w = x1 - x0, h = y1 - y0;
	if (w <= 0 || h <= 0)
		return;
	const u8 *s = (const u8 *)bb + y0 * bb_pitch + x0 * 4;
	u8 *d = (u8 *)fb_va + y0 * fb_pitch + x0 * 4;
	const u64 a = fb_alpha;
	const long n2 = w >> 1;
	for (; h; h--, s += bb_pitch, d += fb_pitch) {
		const u64u *sp = (const u64u *)s;
		u64u *dp = (u64u *)d;
		for (long i = 0; i < n2; i++)
			dp[i] = sp[i] | a;
		if (w & 1)
			((u32 *)d)[w - 1] = ((const u32 *)s)[w - 1] | (u32)a;
	}
}

/* étiquette « property tags » (une valeur de 32 bits en réponse) -> la valeur, -1 si échec */
static long fw_get(u32 tag)
{
	pres_fw[0] = 28; /* taille */
	pres_fw[1] = 0;  /* requête */
	pres_fw[2] = tag;
	pres_fw[3] = 4; /* tampon de valeur */
	pres_fw[4] = 0;
	pres_fw[5] = 0; /* valeur */
	pres_fw[6] = 0; /* fin */
	if (korin_sys1(SYS_FW_CALL, pres_fw))
		return -1;
	if (((volatile u32 *)pres_fw)[1] != 0x80000000u)
		return -1;
	return ((volatile u32 *)pres_fw)[5];
}

/* -> 1 sur un Pi 4 (4 B, 400, CM4 ; QEMU raspi4b), 0 sinon (le DMA du Pi 5 est ailleurs) */
long board_pi4(void)
{
	long v = fw_get(0x00010002); /* GET_BOARD_REVISION */
	if (v < 0 || !(v & (1L << 23))) /* nouveau format */
		return 0;
	long type = (v >> 4) & 0xFF;
	return type == 0x11 || type == 0x13 || type == 0x14;
}

/* -> canal : le plus haut des canaux complets (6..0) laissés par le firmware ; -1 si aucun */
static long dma_pick(void)
{
	long m = fw_get(0x00060001); /* GET_DMA_CHANNELS */
	if (m <= 0)
		m = DMA_CHANNELS_DEF;
	for (long c = 6; c >= 0; c--)
		if ((m >> c) & 1)
			return c;
	return -1;
}

/* active le canal (DMA_ENABLE) et le remet à zéro -> 1, 0 si le canal ne sort pas de sa remise à
 * zéro */
static long dma_reset(void)
{
	u32 en = rd32(dma_base, DMA_ENABLE_R);
	if (!((en >> dma_chan) & 1))
		wr32(dma_base, DMA_ENABLE_R, en | 1u << dma_chan);
	wr32(dma_regs, DMA_CS, CS_RESET);
	long t0 = now_ms();
	for (;;) {
		if (!(rd32(dma_regs, DMA_CS) & CS_RESET))
			return 1;
		korin_sys0(SYS_YIELD);
		if ((u64)(now_ms() - t0) >= 20)
			return 0;
	}
}

/* adresse de bus du premier bloc de contrôle : lance la chaîne */
static void dma_start(u64 cb_bus)
{
	__asm__ volatile("dsb sy" ::: "memory"); /* blocs (non cachés) et lignes nettoyées en mémoire
						    avant le lancement */
	wr32(dma_regs, DMA_CONBLK, (u32)cb_bus);
	wr32(dma_regs, DMA_CS, CS_GO);
}

/* attente maximale (ms) -> 1 fini sans erreur, 0 erreur ou pas de réponse (le canal est alors
 * remis à zéro) */
long present_dma_finish(long ms)
{
	long t0 = now_ms();
	for (;;) {
		u32 cs = rd32(dma_regs, DMA_CS);
		if (cs & CS_ERROR)
			break;
		if (!(cs & CS_ACTIVE)) { /* plus ACTIVE */
			wr32(dma_regs, DMA_CS, CS_INT);
			return 1;
		}
		if (dma_irq) {
			/* (la borne, puis l'échéance ci-dessous) */
			if (korin_sys2(SYS_IRQ_WAIT, dma_irq, ms) == 1) {
				dma_irqs++;
				wr32(dma_regs, DMA_CS, CS_INT);
			}
		} else {
			korin_sys0(SYS_YIELD); /* (le cœur reste aux autres) */
		}
		if ((u64)(now_ms() - t0) >= (u64)ms)
			break;
	}
	wr32(dma_regs, DMA_CS, CS_RESET);
	return 0;
}

/* après l'essai du canal : son interruption, si le noyau la donne */
static void dma_irq_init(void)
{
	long line = dma_chan + DMA_IRQ_BASE;
	if (korin_sys3(SYS_IRQ_BIND, line, -1, 0) >= 0)
		dma_irq = line;
}

/* essai : 2 lignes de 16 octets (YLENGTH = 1) de +0x800 vers +0xC00 de la page des blocs (pas 64
 * des deux côtés), TI -> 1, dma_ti et dma_ybias ; 0 si échec */
static long dma_trial(u32 ti)
{
	dma_ti = ti;
	volatile u32 *cb = (volatile u32 *)dma_cb;
	volatile u32 *src = (volatile u32 *)(dma_cb + 0x800);
	volatile u32 *dst = (volatile u32 *)(dma_cb + 0xC00);
	for (u32 i = 0; i < 48; i++) { /* source : 3 lignes reconnaissables (3 x 64 octets) ;
					  destination : à zéro */
		src[i] = 0xA5C30000u + i;
		dst[i] = 0;
	}
	cb[0] = ti;
	cb[1] = (u32)dma_cb_bus + 0x800;  /* SOURCE_AD */
	cb[2] = (u32)dma_cb_bus + 0xC00;  /* DEST_AD */
	cb[3] = (1u << 16) | 16;          /* TXFR_LEN : YLENGTH 1, XLENGTH 16 */
	cb[4] = (48u << 16) | 48;         /* STRIDE : 64 - 16 des deux côtés */
	cb[5] = 0;                        /* NEXTCONBK */
	cb[6] = 0;
	cb[7] = 0;
	dma_start(dma_cb_bus);
	if (!present_dma_finish(50))
		return 0;
	volatile u64 *s64 = (volatile u64 *)src, *d64 = (volatile u64 *)dst;
	if (s64[0] != d64[0] || s64[1] != d64[1]) /* ligne 0 : copiée */
		return 0;
	if (d64[16] | d64[17]) /* ligne 2 : intacte */
		return 0;
	if (d64[8] | d64[9]) { /* ligne 1 : copiée (BCM2711) ou intacte (QEMU) */
		if (s64[8] != d64[8] || s64[9] != d64[9])
			return 0;
		dma_ybias = 1;
	} else {
		dma_ybias = 0;
	}
	return 1;
}

/* le DMA 2D (la suite de present_init de l'asm, après « pas un Pi 4 ») -> 1 s'il est pris (la
 * ligne du journal dans pres_line) ; sinon *why = la raison */
long present_dma_init(long size, const char **why)
{
	*why = m_r_size; /* limites du mode 2D : pas < 32 Ko, hauteur < 16384 */
	if ((u64)bb_pitch >= 0x8000 || (u64)fb_pitch >= 0x8000 || (u64)scr_h >= 0x4000)
		return 0;
	*why = m_r_mem; /* tampon arrière : contigu, caché */
	long a = korin_sys2(SYS_DMA_ALLOC, (u64)size | DMA_CACHED, &pres_phys);
	if (a <= 0)
		return 0;
	bb = (u32 *)a;
	bb_bus = phys_bus(pres_phys);
	if (!bb_bus)
		return 0;
	*why = m_r_fb;
	fb_bus = phys_bus(info[4]); /* (ws_display +32) */
	if (!fb_bus)
		return 0;
	*why = m_r_chan;
	long c = dma_pick();
	if (c < 0)
		return 0;
	dma_chan = c;
	*why = m_r_map;
	a = korin_sys2(SYS_DEV_MAP, DMA_PHYS, 0x1000);
	if (a <= 0)
		return 0;
	dma_base = (u8 *)a;
	dma_regs = dma_base + (dma_chan << 8);
	*why = m_r_mem; /* blocs de contrôle : page non cachée */
	a = korin_sys2(SYS_DMA_ALLOC, 0x1000, &pres_phys);
	if (a <= 0)
		return 0;
	dma_cb = (u8 *)a;
	dma_cb_bus = phys_bus(pres_phys);
	if (!dma_cb_bus)
		return 0;
	*why = m_r_lite;
	if ((rd32(dma_regs, DMA_DEBUG) >> DEBUG_LITE) & 1)
		return 0;
	*why = m_r_reset;
	if (!dma_reset())
		return 0;
	*why = m_r_trial;
	if (!dma_trial(TI_2D)) { /* largeur 128 bits (BCM2711), sinon : pas un vrai BCM2711 (QEMU) ;
				    32 bits seulement sur demande */
		*why = m_r_emul;
		if (!option_word(s_dispdma_qemu))
			return 0;
		*why = m_r_trial;
		if (!dma_reset())
			return 0;
		if (!dma_trial(TI_2D_32))
			return 0;
	}
	dma_irq_init();
	char *p = str_append(pres_line, m_dma);
	*p++ = (char)('0' + dma_chan); /* canal 0-6 : un chiffre */
	if (!(dma_ti & (1u << 9))) /* (QEMU : 32 bits) */
		p = str_append(p, m_dma_32);
	if (!dma_ybias)
		p = str_append(p, m_dma_ylen);
	str_append(p, "\n");
	return 1;
}

/* une chaîne de blocs de contrôle (un par zone, lue en place dans le tampon arrière), lignes
 * nettoyées, lancée ; present_wait en attendra la fin */
void present_dma(void)
{
	long n = cmp_n;
	if (!n)
		return;
	volatile u32 *cb = (volatile u32 *)dma_cb; /* bloc courant */
	for (long i = 0; i < n; i++, cb += 8) {
		const struct rect *z = &cmp_list[i];
		u64 w = (u64)(z->x1 - z->x0), h = (u64)(z->y1 - z->y0);
		u64 bpl = w << 2; /* octets par ligne */
		u64 obb = (u64)z->y0 * (u64)bb_pitch + ((u64)z->x0 << 2); /* décalage dans le tampon arrière */
		u64 ofb = (u64)z->y0 * (u64)fb_pitch + ((u64)z->x0 << 2); /* dans le framebuffer */
		u8 *row = (u8 *)bb + obb; /* nettoyer les lignes de la zone */
		for (u64 r = h; r; r--, row += bb_pitch) {
			u64 e = (u64)row + bpl;
			for (u64 l = (u64)row & ~63ULL; l < e; l += 64)
				__asm__ volatile("dc cvac, %0" : : "r"(l) : "memory");
		}
		u32 ti = dma_ti;
		if (i == n - 1) /* le dernier bloc : interruption */
			ti |= TI_INTEN;
		cb[0] = ti;
		cb[1] = (u32)(bb_bus + obb);  /* SOURCE_AD */
		cb[2] = (u32)(fb_bus + ofb);  /* DEST_AD */
		cb[3] = (u32)(bpl | (h - (u64)dma_ybias) << 16); /* TXFR_LEN : YLENGTH lignes - 1
								     (BCM2711), lignes (QEMU) */
		cb[4] = (u32)(((u64)bb_pitch - bpl) | ((u64)fb_pitch - bpl) << 16); /* STRIDE : source,
											destination */
		cb[5] = i == n - 1 ? 0 : (u32)(dma_cb_bus + (u64)(i + 1) * 32); /* NEXTCONBK (0 : fin) */
		cb[6] = 0;
		cb[7] = 0;
	}
	dma_start(dma_cb_bus);
	pres_t0 = now_ms();
	pres_busy = 1;
	dma_n++; /* (diagnostic F11) */
}
