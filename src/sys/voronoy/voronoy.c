/* voronoy - le fond d'écran du bureau Elegant (SD0:/sys/voronoy, lancé par la session, comme le
 * voronoy d'Onyx) : Voronoï « facettes douces » (docs/DESIGN-ELEGANT.md 5.1) : environ une cellule
 * pour 28 000 pixels, germes espacés (rejet), couleurs de la palette du thème, plus clair en haut
 * et plus sombre en bas, liseré clair le long des arêtes. Fenêtre sans cadre, sous toutes les
 * autres. Relance : nouvelles cellules.
 *
 * Traduit ligne à ligne de voronoy.asm. */
#include "sys_consts.h"
#include "uikit_import.h"

#define NMAX 64
#define OFF_SCREEN 20 /* germes un peu hors de l'écran */

static struct krt_ui ui;
static u64 rng;
static u64 nseeds;
static long scr[4];
static u32 pal[5];
static i64 seed_x[NMAX], seed_y[NMAX];
static u32 seed_c[NMAX]; /* couleur de la cellule */
static u32 seed_e[NMAX]; /* son liseré */
static u32 row_c[NMAX];  /* couleurs de la ligne courante */

/* Givre, Onyx Noir : couleurs fixes de l'asm (la palette du fond, pas des jetons) */
static const u32 pal_givre[5] = { 0x00D6DEE9, 0x00DCE3ED, 0x00D1DAE6, 0x00E1E7EF, 0x00CDD7E4 };
static const u32 pal_noir[5] = { 0x001A1E26, 0x001D222B, 0x00181C23, 0x0020252F, 0x001B2029 };

static u32 token(long off)
{
	return *(const u32 *)((const u8 *)ui_tokens() + off);
}

/* nombre pseudo-aléatoire (xorshift64) */
static u64 rnd(void)
{
	u64 x = rng;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rng = x;
	return x;
}

/* hasard dans [0, n[ */
static u64 rnd_n(u64 n)
{
	return (rnd() >> 1) % n;
}

/* pal <- 5 couleurs : celles de Givre ou d'Onyx Noir */
static void palette(void)
{
	const u32 *p = token(TJ_DARK) ? pal_noir : pal_givre;
	for (int i = 0; i < 5; i++)
		pal[i] = p[i];
}

/* germes : nombre selon l'écran, distance minimale entre eux, couleurs */
static void seeds(void)
{
	rng = ((u64)sys_time_ms() ^ 0x9E3779B97F4A7C15ull) | 1;
	u64 w = (u64)ui.w, h = (u64)ui.h;
	u64 n = (w * h + 14000) / 28000; /* N = arrondi(l x h / 28 000), 8 au moins */
	if (n < 8)
		n = 8;
	if (n > NMAX)
		n = NMAX;
	nseeds = n;
	u64 dmin = w * h * 3844 / 10000 / n; /* distance minimale² = 0,62² x l x h / N */
	for (u64 k = 0; k < n; k++) {
		long tries = 200;
		i64 x, y;
	again:
		x = (i64)rnd_n(w + 2 * OFF_SCREEN) - OFF_SCREEN;
		y = (i64)rnd_n(h + 2 * OFF_SCREEN) - OFF_SCREEN;
		if (--tries) { /* (trop d'essais : tant pis) */
			for (u64 j = 0; j < k; j++) { /* loin des germes déjà placés ? */
				u64 dx = (u64)(x - seed_x[j]), dy = (u64)(y - seed_y[j]);
				if (dx * dx + dy * dy < dmin)
					goto again;
			}
		}
		seed_x[k] = x;
		seed_y[k] = y;
		seed_c[k] = pal[rnd_n(5)]; /* une des 5 de la palette */
		seed_e[k] = (u32)ui_tone(seed_c[k], 134); /* liseré : à peine plus clair */
	}
}

/* ligne y : row_c <- couleur de chaque cellule à cette hauteur, ton(c, 136 - 30 y / h)
 * (sombre : 128 - 24 y / h) */
static void row_colours(u64 y)
{
	u64 k = 30, base = 136;
	if (token(TJ_DARK)) {
		k = 24;
		base = 128;
	}
	u64 level = base - k * y / (u64)ui.h;
	for (u64 i = 0; i < nseeds; i++)
		row_c[i] = (u32)ui_tone(seed_c[i], (long)level);
}

/* peint le champ : couleur du germe le plus proche, nuancée selon la ligne ; liseré quand le
 * pixel est à moins d'un pixel de la médiatrice des deux germes les plus proches :
 * (d2² - d1²)² < 4 |g2 - g1|² */
static void paint(void)
{
	u64 w = (u64)ui.w, h = (u64)ui.h;
	u32 *px = ui.surf;
	for (u64 y = 0; y < h; y++) {
		row_colours(y);
		for (u64 x = 0; x < w; x++) {
			u64 d1 = (u64)-1, d2 = (u64)-1, k1 = 0, k2 = 0;
			u64 i = 0;
			do { /* (au moins un germe, comme l'asm) */
				u64 dx = x - (u64)seed_x[i], dy = y - (u64)seed_y[i];
				u64 d = dx * dx + dy * dy;
				if (d < d1) { /* ancien premier -> second */
					d2 = d1;
					k2 = k1;
					d1 = d;
					k1 = i;
				} else if (d < d2) {
					d2 = d;
					k2 = i;
				}
			} while (++i < nseeds);
			u64 dd = d2 - d1;
			u64 gx = (u64)seed_x[k1] - (u64)seed_x[k2], gy = (u64)seed_y[k1] - (u64)seed_y[k2];
			u64 g = (gx * gx + gy * gy) << 2;
			*px++ = dd * dd < g ? seed_e[k1] : row_c[k1];
		}
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ui_init((long)&ui);
	if (ui_screen((long)scr) < 0)
		return 0;
	if (ui_window_ex(0, 0, scr[0], scr[1], (long)lang_tr("Fond"),
			 WF_FRAMELESS | WF_BACK | WF_NOFOCUS | WF_NOLIST) < 0)
		return 0;
	palette();
	seeds();
	paint();
	ui_update_all();
	for (;;) {
		long e = ui_event(600000);
		if (e == WEV_SCREEN) { /* nouvelle résolution : nouvelles cellules */
			if (ui_screen((long)scr) < 0)
				continue;
			ui_resize(scr[0], scr[1]);
			seeds();
			paint();
			ui_update_all();
		} else if (e == WEV_THEME) { /* autre thème : ses couleurs (tirées par cellule : de
						nouvelles cellules) */
			palette();
			seeds();
			paint();
			ui_update_all();
		} else if (e == WEV_CLOSE)
			return 0;
	}
}
