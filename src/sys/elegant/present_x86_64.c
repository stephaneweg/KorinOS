/* present_x86_64.c : la présentation sur le PC (present.inc) : ni firmware ni DMA ; la copie
 * par le processeur du tampon arrière vers le framebuffer GOP (mappé par le noyau en écriture
 * combinée : WS_DISPLAY, src/kernel/core/ws.c ; 1920 x 1080, BGR 32 bits, l'ordre des octets de
 * nos pixels 0x00RRGGBB).
 *
 * En écriture combinée, l'écran aime des lignes écrites à la suite, par paquets de 64 octets
 * (une ligne du tampon de combinaison) : quatre mots de 128 bits à la suite (SSE2, toujours là
 * en x86-64), puis le reste ; un sfence à la fin vide les tampons de combinaison. */
#include "elegant.h"

typedef u32 v4u __attribute__((vector_size(16), aligned(4), may_alias));

/* zone (x0, y0, x1, y1) : copie du tampon arrière vers le framebuffer, par le processeur (de
 * core.inc) ; l'octet alpha forcé comme sur le Pi (fb_alpha : pas un Pi 4, l'écran est dit le
 * lire ; le GOP l'ignore) */
void flush_rect(long x0, long y0, long x1, long y1)
{
	long w = x1 - x0, h = y1 - y0;
	if (w <= 0 || h <= 0)
		return;
	const u8 *s = (const u8 *)bb + y0 * bb_pitch + x0 * 4;
	u8 *d = (u8 *)fb_va + y0 * fb_pitch + x0 * 4;
	const u32 a32 = (u32)fb_alpha;
	const v4u a = { a32, a32, a32, a32 };
	for (; h; h--, s += bb_pitch, d += fb_pitch) {
		const v4u *sp = (const v4u *)s;
		v4u *dp = (v4u *)d;
		long n = w;
		for (; n >= 16; n -= 16, sp += 4, dp += 4) {
			v4u p0 = sp[0], p1 = sp[1], p2 = sp[2], p3 = sp[3];
			dp[0] = p0 | a;
			dp[1] = p1 | a;
			dp[2] = p2 | a;
			dp[3] = p3 | a;
		}
		for (; n >= 4; n -= 4)
			*dp++ = *sp++ | a;
		const u32 *s1 = (const u32 *)sp;
		u32 *d1 = (u32 *)dp;
		for (; n; n--)
			*d1++ = *s1++ | a32;
	}
	__asm__ volatile("sfence" ::: "memory");
}

/* pas de firmware : pas un Pi 4 (le journal le dit, comme l'asm ailleurs qu'un Pi 4) */
long board_pi4(void)
{
	return 0;
}

/* pas de DMA de l'écran sur le PC (jamais appelées : pres_mode reste 0) */
long present_dma_init(long size, const char **why)
{
	(void)size;
	(void)why;
	return 0;
}

long present_dma_finish(long ms)
{
	(void)ms;
	return 1;
}

void present_dma(void)
{
}
