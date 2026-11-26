/* uk_surface.c -- UIKit : surface attachée à la fenêtre (uikit_surface.inc),
 * port ligne à ligne de l'assembleur (src/kits/uikit/PORT.md).
 *
 * Des pixels à soi dans un tampon partagé (XRGB8888 ou RGB565) qu'Elegant
 * compose directement dans une zone du contenu de la fenêtre
 * (WS_OP_SURFACE_ATTACH / WS_OP_SURFACE_DAMAGE). Poignée : bloc US_* du kit. */
#include "uk.h"

/* uk_sf_alloc : x0 = surface : un tampon partagé pour sa zone (US_W, US_H,
 * US_FMT) -> 1 (US_BUF, US_ADDR, US_PITCH, US_CAP remplis), 0 : échec */
static long sf_alloc(long s)
{
	long pitch = LDX(s, US_W);
	long bpp = 4;
	if (LDX(s, US_FMT))
		bpp = 2;
	pitch = pitch * bpp;
	pitch = (pitch + 15) & ~15L;         /* lignes alignées sur 16 octets */
	STX(s, US_PITCH, pitch);
	long cap = pitch * LDX(s, US_H);
	if (UGT(cap, BUF_SLOT))
		return 0;
	STX(s, US_CAP, cap);
	long id = SYS1(SYS_BUF_CREATE, cap);
	if (id < 0)
		return 0;
	STX(s, US_BUF, id);
	STX(s, US_ADDR, id * (long)BUF_SLOT + (long)BUF_VA_BASE);
	return 1;
}

/* uk_sf_attach : x0 = surface : WS_OP_SURFACE_ATTACH (le tampon autorisé à
 * Elegant au besoin) -> 0, ou erreur < 0 */
static long sf_attach(long s)
{
	long tries = 2;                      /* deux essais (le second après BUF_GRANT) */
	for (;;) {
		u8 *m = uk.msg;
		STX(m, WM_A, LDX(s, US_WIN));
		long b = LDX(s, US_SRC) - 1;     /* (ui_surface_source) */
		if (b == -1)
			b = LDX(s, US_BUF);
		STX(m, WM_B, b);
		STX(m, WM_C, LDX(s, US_X) | LSL(LDX(s, US_Y), 32));
		STX(m, WM_D, LDX(s, US_W) | LSL(LDX(s, US_H), 32));
		long fmt = LDX(s, US_FMT);
		long pitch = LDX(s, US_SRC_PITCH);
		if (pitch)
			fmt |= WS_SF_NOREDRAW;       /* (ailleurs : image suivante, DAMAGE suit) */
		else
			pitch = LDX(s, US_PITCH);
		STW(m, WM_DATA, fmt);
		STW(m, WM_DATA + 4, pitch);
		STX(m, WM_DATA + 8, LDX(s, US_SRC_OFF));   /* décalage */
		STW(m, WM_LEN, 16);
		long r = uk_call(WS_OP_SURFACE_ATTACH);
		if (r != -1)                     /* E_PERM : autoriser Elegant (pid rendu) */
			return r;
		if (--tries == 0)
			return r;
		/* le tampon attaché : celui de ui_surface_source s'il y en a un (26/11/2026 : US_BUF était accordé à sa place ;
		   Jet sans surface ouverte à tous montrait des pages blanches, E47) */
		SYS2(SYS_BUF_GRANT, b, LDX(uk.msg, WM_A));
	}
}

/* x0 = x, x1 = y, x2 = largeur, x3 = hauteur (dans le contenu de la fenêtre
 * courante), x4 = format (WS_SF_XRGB8888 / WS_SF_RGB565) -> surface (0 : échec) */
long ui_surface(long a0, long a1, long a2, long a3, long a4)
{
	if (UGT(a4, WS_SF_RGB565))
		return 0;
	if (a2 < 1 || a3 < 1)
		return 0;
	if (a2 > US_MAX_SIDE || a3 > US_MAX_SIDE)
		return 0;
	long s = uk_alloc(US_SIZE);
	if (!s)
		return 0;
	STX(s, US_X, a0);
	STX(s, US_Y, a1);
	STX(s, US_W, a2);
	STX(s, US_H, a3);
	STX(s, US_FMT, a4);
	STX(s, US_SRC, 0);
	STX(s, US_SRC_PITCH, 0);
	STX(s, US_SRC_OFF, 0);
	STX(s, US_WIN, uk_win());
	if (sf_alloc(s)) {                   /* tampon à la taille de la zone */
		if (sf_attach(s) >= 0)
			return s;
		SYS1(SYS_BUF_FREE, LDX(s, US_BUF));
	}
	uk_free(s);
	return 0;
}

/* x0 = surface, x1 = tampon partagé écrit ailleurs (-1 : les pixels de la
 * surface), x2 = son pas, x3 = décalage de la zone dans ce tampon -> 1, 0 : échec */
long ui_surface_source(long a0, long a1, long a2, long a3)
{
	if (!a0)
		return 0;
	STX(a0, US_SRC, a1 + 1);
	STX(a0, US_SRC_PITCH, a2);
	STX(a0, US_SRC_OFF, a3);
	if (sf_attach(a0) < 0)
		return 0;
	return 1;
}

/* x0 = surface -> x0 = pixels, x1 = pas (octets), x2 = largeur, x3 = hauteur */
ukr ui_surface_pixels(long a0)
{
	ukr r;
	if (!a0) {
		r.r0 = 0;
		r.r1 = 0;
		r.r2 = 0;
		r.r3 = 0;
		return r;
	}
	r.r1 = LDX(a0, US_PITCH);
	r.r2 = LDX(a0, US_W);
	r.r3 = LDX(a0, US_H);
	r.r0 = LDX(a0, US_ADDR);
	return r;
}

/* x0 = surface, x1 = x, x2 = y, x3 = largeur, x4 = hauteur (dans la surface ;
 * largeur <= 0 : toute la surface) : la zone changée, à recomposer */
long ui_surface_damage(long a0, long a1, long a2, long a3, long a4)
{
	if (!a0)
		return a0;
	u8 *m = uk.msg;
	STX(m, WM_A, LDX(a0, US_WIN));
	long n = 0;
	if (a3 > 0) {
		STH(m, WM_DATA, a1);
		STH(m, WM_DATA + 2, a2);
		STH(m, WM_DATA + 4, a3);
		STH(m, WM_DATA + 6, a4);
		n = 1;
	}
	STX(m, WM_B, n);
	STW(m, WM_LEN, n << 3);
	return uk_call(WS_OP_SURFACE_DAMAGE);
}

/* x0 = surface, x1 = rectangles (x, y, l, h : 16 bits chacun), x2 = nombre
 * (WS_DAMAGE_MAX par message ; plus : en plusieurs messages) */
long ui_surface_damage_list(long a0, long a1, long a2)
{
	long r = a0;
	if (!a0)
		return r;
	long src = a1, left = a2;
	while (left > 0) {
		long n = left;
		if (UGT(n, WS_DAMAGE_MAX))
			n = WS_DAMAGE_MAX;
		u8 *m = uk.msg;
		STX(m, WM_A, LDX(a0, US_WIN));
		STX(m, WM_B, n);
		STW(m, WM_LEN, n << 3);
		long d = (long)(uintptr_t)(m + WM_DATA);
		long k = n;
		do {                             /* comme l'asm : subs + b.ne */
			STX(d, 0, LDX(src, 0));
			src += 8;
			d += 8;
		} while (--k != 0);
		r = uk_call(WS_OP_SURFACE_DAMAGE);
		left -= n;
	}
	return r;
}

/* x0 = surface, x1 = x, x2 = y, x3 = largeur, x4 = hauteur : la zone change
 * de place ou de taille ; un tampon neuf si l'ancien est trop petit -> 1,
 * 0 : échec (surface inchangée) */
long ui_surface_move(long a0, long a1, long a2, long a3, long a4)
{
	long s = a0;
	if (!s)
		return 0;
	if (a3 < 1 || a4 < 1)
		return 0;
	if (a3 > US_MAX_SIDE || a4 > US_MAX_SIDE)
		return 0;
	long ox = LDX(s, US_X), oy = LDX(s, US_Y);      /* ancienne zone (pour revenir) */
	long ow = LDX(s, US_W), oh = LDX(s, US_H);
	STX(s, US_X, a1);
	STX(s, US_Y, a2);
	STX(s, US_W, a3);
	STX(s, US_H, a4);
	long bpp = 4;                        /* la place suffit-elle ? */
	if (LDX(s, US_FMT))
		bpp = 2;
	long row = a3 * bpp;
	long pitch = LDX(s, US_PITCH);
	if (!UGT(row, pitch) && !UGT(pitch * a4, LDX(s, US_CAP))) {
		if (sf_attach(s) >= 0)
			return 1;
	} else {
		long oldbuf = LDX(s, US_BUF);   /* tampon neuf, puis l'ancien rendu */
		long opitch = LDX(s, US_PITCH);
		long ocap = LDX(s, US_CAP);
		long oaddr = LDX(s, US_ADDR);
		if (sf_alloc(s)) {
			if (sf_attach(s) >= 0) {
				SYS1(SYS_BUF_FREE, oldbuf);
				return 1;
			}
			SYS1(SYS_BUF_FREE, LDX(s, US_BUF));
		}
		STX(s, US_BUF, oldbuf);
		STX(s, US_PITCH, opitch);
		STX(s, US_CAP, ocap);
		STX(s, US_ADDR, oaddr);
	}
	STX(s, US_X, ox);
	STX(s, US_Y, oy);
	STX(s, US_W, ow);
	STX(s, US_H, oh);
	sf_attach(s);
	return 0;
}

/* x0 = surface : détachée de la fenêtre, tampon rendu, poignée libérée */
long ui_surface_free(long a0)
{
	if (!a0)
		return a0;
	u8 *m = uk.msg;
	STX(m, WM_A, LDX(a0, US_WIN));
	STX(m, WM_B, -1);
	STW(m, WM_LEN, 0);
	uk_call(WS_OP_SURFACE_ATTACH);
	SYS1(SYS_BUF_FREE, LDX(a0, US_BUF));
	return uk_free(a0);
}

/* ui_surface_overlay : x0 = zones (x, y, largeur, hauteur : 16 bits chacun, dans le contenu de la
 * fenêtre courante), x1 = nombre (0 : aucune ; WS_OVER_MAX au plus) : Elegant compose ces zones du
 * contenu par-dessus la surface de la fenêtre, quelle qu'elle soit (ui_surface, ou celle que GlKit
 * attache pour le GPU), avec la transparence de l'octet haut (0 opaque, 255 invisible) : des
 * widgets d'UIKit sur une vue GL ou vidéo (docs/DESIGN-CAO.md §11) -> 0, ou erreur < 0 */
long ui_surface_overlay(long a0, long a1)
{
	if (a1 < 0 || UGT(a1, WS_OVER_MAX))
		return E_INVAL;
	u8 *m = uk.msg;
	STX(m, WM_A, uk_win());
	STX(m, WM_B, a1);
	STW(m, WM_LEN, a1 << 3);
	for (long i = 0; i < a1; i++)
		STX(m, WM_DATA + i * 8, LDX(a0, i * 8));
	return uk_call(WS_OP_SURFACE_OVER);
}
