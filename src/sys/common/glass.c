/* glass.c : le verre des barres du bureau (glass.h). */
#include "glass.h"
#include "sys_consts.h"
#include "uikit_import.h"

#define GLASS_T_LIGHT 0x6C /* transparence du verre : Givre, */
#define GLASS_T_DARK 0x5C  /* Onyx Noir (Elegant floute le dessous : WF_BLUR) */

static long g_h = 1, g_tok = TJ_DOCK, g_edge = GLASS_EDGE_TOP;

static u32 tok(long off)
{
	return M32(ui_tokens(), off);
}

void glass_set(long h, long t, long edge)
{
	g_h = h > 1 ? h : 1;
	g_tok = t;
	g_edge = edge;
}

/* a, b (0xTTRRGGBB), k 0..256 : les quatre octets mêlés */
static u32 mix4(u32 a, u32 b, u32 k)
{
	u32 o = 0;
	for (u32 sh = 0; sh < 32; sh += 8) {
		u32 x = (a >> sh) & 0xFF, y = (b >> sh) & 0xFF;
		o |= ((x * (256 - k) + y * k) >> 8) << sh;
	}
	return o;
}

u32 glass_at(long y)
{
	u32 base = tok(g_tok) & 0xFFFFFF;
	int dark = tok(TJ_DARK) != 0;
	u32 t = dark ? GLASS_T_DARK : GLASS_T_LIGHT;
	/* le volume : haut vers la surface (la lumière vient d'en haut), bas un peu plus sombre */
	u32 top = (u32)ui_mix(base, tok(TJ_SURFACE), dark ? 110 : 220);
	u32 bot = (u32)ui_tone(base, dark ? 112 : 118);
	long h = g_h > 1 ? g_h - 1 : 1;
	u32 c = (u32)ui_mix(top, bot, (long)((u64)(y << 8) / (u64)h));
	return (c & 0xFFFFFF) | (t << 24);
}

void glass_draw(long w)
{
	for (long y = 0; y < g_h; y++)
		ui_fill(0, y, w, 1, glass_at(y));
	int dark = tok(TJ_DARK) != 0;
	/* bord, côté bureau : le trait fort du thème, translucide (l'ombre portée est d'Elegant) */
	u32 edge = (tok(dark ? TJ_FACE : TJ_LINE_STRONG) & 0xFFFFFF) | (dark ? 0x40000000 : 0xA8000000);
	/* reflet sur deux lignes en haut : la surface (blanc en Givre), net puis adouci */
	u32 hi = dark ? (u32)ui_mix(tok(g_tok) & 0xFFFFFF, 0x00FFFFFF, 56) : tok(TJ_SURFACE);
	u32 hi1 = (hi & 0xFFFFFF) | (dark ? 0x50000000 : 0x10000000);
	u32 hi2 = (hi & 0xFFFFFF) | (dark ? 0xC0000000 : 0x90000000);
	if (g_edge == GLASS_EDGE_TOP) { /* le panel : bord, puis reflet */
		ui_fill(0, 0, w, 1, edge);
		ui_fill(0, 1, w, 1, hi1);
		ui_fill(0, 2, w, 1, hi2);
	} else { /* la barre de menus : reflet en haut (le bord de l'écran), bord en bas */
		ui_fill(0, 0, w, 1, hi1);
		ui_fill(0, 1, w, 1, hi2);
		ui_fill(0, g_h - 1, w, 1, edge);
	}
}

/* les coins : chaque pixel mêle le verre de sa ligne et la couleur selon sa couverture (4 x 4) */
void glass_rbox(long x, long y, long w, long h, long r, u32 c)
{
	if (w <= 0 || h <= 0)
		return;
	if (2 * r > w)
		r = w / 2;
	if (2 * r > h)
		r = h / 2;
	for (long row = 0; row < h; row++) {
		long j = row < r ? row : h - 1 - row; /* ligne dans le coin */
		if (j >= r) {
			ui_fill(x, y + row, w, 1, c);
			continue;
		}
		u32 bg = glass_at(y + row);
		long inner = 0; /* pixels du coin à mêler */
		for (long k = 0; k < r; k++) {
			long cov = 0; /* couverture du pixel (k, j) du coin haut-gauche, sur 16 */
			for (long sy = 0; sy < 4; sy++)
				for (long sx = 0; sx < 4; sx++) {
					long px = 8 * k + 2 * sx + 1 - 8 * r; /* (x 8, centre du cercle à 0) */
					long py = 8 * j + 2 * sy + 1 - 8 * r;
					if (px * px + py * py <= 64 * r * r)
						cov++;
				}
			if (cov == 16)
				break;
			u32 m = mix4(bg, c, (u32)(cov * 16));
			ui_pixel(x + k, y + row, m);
			ui_pixel(x + w - 1 - k, y + row, m);
			inner = k + 1;
		}
		ui_fill(x + inner, y + row, w - 2 * inner, 1, c);
	}
}

u32 glass_hover(void)
{
	if (tok(TJ_DARK))
		return ((u32)ui_mix(tok(g_tok) & 0xFFFFFF, 0x00FFFFFF, 44) & 0xFFFFFF) | 0x30000000;
	return (tok(TJ_SURFACE) & 0xFFFFFF) | 0x20000000;
}
