/* overlaytest - essai des widgets d'UIKit par-dessus une surface (ui_surface_overlay, WS_OP_SURFACE_OVER ;
 * docs/DESIGN-CAO.md §11, manques 1, 2 et 13), qui se vérifie lui-même :
 *
 *	overlaytest [secondes]
 *
 * Une fenêtre ; une surface (ici remplie par le processeur, à la place d'une vue GL : Elegant les compose de
 * la même façon) sous la ligne du titre ; par-dessus, trois zones du contenu passées à ui_surface_overlay :
 * un panneau arrondi avec un bouton (opaque), un trou (octet haut 255 : la surface doit se voir), un voile
 * rouge à moitié transparent (octet haut 128 : mélangé). L'écran composé est relu (ui_screen_grab) et des
 * pixels connus sont comparés. Puis la fenêtre est agrandie (ui_resize), la surface suit (ui_surface_move),
 * le panneau passe en bas à droite : l'ancienne place doit montrer la surface, la nouvelle le panneau.
 * Enfin les jetons TJ_HOVER et TJ_AXIS_X/Y/Z du thème courant.
 * Sortie : chaque vérification, puis « overlaytest : PASS / FAIL » ; « secondes » : la fenêtre reste
 * ouverte ce temps à la fin (capture), 0 par défaut. Code 0 (PASS) ou 1. */
#include "korinrt.h"
#include "uikit_import.h"

#define W0 640
#define H0 420
#define W1 800
#define H1 500
#define SY 40          /* la surface sous la ligne du titre */
#define MARK 0x00FE01FD /* repère (4, 4) du contenu, retrouvé dans l'écran relu */
#define HALF 0x80C00000 /* voile : rouge, octet haut 128 */

static struct krt_ui ui;
static long surf;
static u32 *grab;
static long sw, sh, ox, oy; /* écran ; coin du contenu dans l'écran */
static long fails;

/* le motif de la surface, en coordonnées de la surface */
static u32 pat(long x, long y)
{
	return (u32)(((x * 3) & 255) << 16 | ((y * 2) & 255) << 8 | 0x60);
}

static void fill_surface(void)
{
	struct krt_r4 p = krt_call_r4(ui_surface_pixels, surf, 0, 0, 0); /* pixels, pas, l, h */
	u8 *pix = (u8 *)p.r0;
	for (long y = 0; y < p.r3; y++) {
		u32 *row = (u32 *)(pix + y * p.r1);
		for (long x = 0; x < p.r2; x++)
			row[x] = pat(x, y);
	}
	ui_surface_damage(surf, 0, 0, 0, 0);
}

static void say(const char *a, long v)
{
	ak_puts(a);
	if (v >= 0)
		print_dec(v);
}

/* le contenu : fond, titre, repère ; les trois zones par-dessus (panneau en px, py) -> donne les zones */
static void draw_content(long w, long h, long px, long py)
{
	const u32 *j = (const u32 *)ui_tokens();
	ui_fill(0, 0, w, h, j[0]); /* TJ_FACE */
	ui_fill(4, 4, 6, 6, MARK);
	ui_text((long)"Widgets par-dessus une surface", 20, 26, j[7], 14, (long)ui.font, 0);
	ui_fill(px, py, 220, 140, 0xFF000000); /* le panneau : invisible autour de ses coins arrondis */
	ui_rbox(px, py, 220, 140, 10, j[1], j[1]); /* TJ_SURFACE */
	ui_text((long)"Panneau flottant", px + 16, py + 30, j[7], 14, (long)ui.font, 0);
	ui_button(px + 16, py + 84, 120, 32, (long)"Bouton", 1);
	ui_fill(260, 56, 120, 80, 0xFF000000); /* le trou */
	ui_fill(400, 56, 120, 80, HALF);       /* le voile */
	ui_update_all();
	u16 r[12] = {(u16)px, (u16)py, 220, 140, 260, 56, 120, 80, 400, 56, 120, 80};
	long e = ui_surface_overlay((long)r, 3);
	if (e < 0) {
		say("overlaytest : ui_surface_overlay a échoué, erreur ", -e);
		ak_puts("\n");
		fails++;
	}
}

static void wait_ms(long ms)
{
	long t0 = sys_time_ms();
	while (sys_time_ms() - t0 < ms)
		ui_event(20);
}

/* l'écran relu ; le coin du contenu par le repère -> 1 */
static long take(void)
{
	if (!ui_screen_grab((long)grab, sw, sh))
		return 0;
	for (long y = 0; y + 6 <= sh; y++)
		for (long x = 0; x + 6 <= sw; x++) {
			const u32 *p = grab + y * sw + x;
			if ((p[0] & 0xFFFFFF) == MARK && (p[5] & 0xFFFFFF) == MARK && (p[5 * sw] & 0xFFFFFF) == MARK) {
				ox = x - 4;
				oy = y - 4;
				return 1;
			}
		}
	return 0;
}

static u32 at(long x, long y)
{
	return grab[(oy + y) * sw + ox + x] & 0xFFFFFF;
}

static long near(u32 a, u32 b, long tol)
{
	for (int s = 0; s < 24; s += 8) {
		long d = (long)((a >> s) & 255) - (long)((b >> s) & 255);
		if (d > tol || d < -tol)
			return 0;
	}
	return 1;
}

static void check(const char *what, long x, long y, u32 want, long tol)
{
	u32 got = at(x, y);
	long ok = near(got, want, tol);
	ak_puts(ok ? "ok    " : "ECHEC ");
	ak_puts(what);
	if (!ok) {
		ak_puts(" : lu ");
		print_dec(got);
		ak_puts(", attendu ");
		print_dec(want);
		fails++;
	}
	ak_puts("\n");
}

/* le voile mélangé, comme blend() d'Elegant (0x80 de transparence) */
static u32 half_over(u32 d)
{
	u32 s = HALF & 0xFFFFFF, t = HALF >> 24, u = 255 - t, out = 0;
	for (int k = 0; k < 24; k += 8) {
		u32 v = (((s >> k) & 255) * u + ((d >> k) & 255) * t + 127) / 255;
		out |= v << k;
	}
	return out;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long secs = krt_line && (u32)(u8)krt_line[0] - '0' <= 9 ? (long)parse_dec(krt_line) : 0;
	ui_init((long)&ui);
	if (ui_window(W0, H0, (long)"Widgets sur une surface") < 0) {
		ak_puts("overlaytest : pas de fenêtre (Elegant ?)\noverlaytest : FAIL\n");
		_exit(1);
	}
	long scr[4];
	ui_screen((long)scr);
	sw = scr[0];
	sh = scr[1];
	grab = (u32 *)ak_malloc((u64)(sw * sh * 4));
	surf = ui_surface(0, SY, W0, H0 - SY, WS_SF_XRGB8888);
	if (!surf || !grab) {
		ak_puts("overlaytest : pas de surface ou de mémoire\noverlaytest : FAIL\n");
		_exit(1);
	}
	const u32 *j = (const u32 *)ui_tokens();
	fill_surface();
	draw_content(W0, H0, 16, 56);
	wait_ms(600);
	if (!take()) {
		ak_puts("overlaytest : repère introuvable dans l'écran relu\noverlaytest : FAIL\n");
		_exit(1);
	}
	say("overlaytest : contenu en ", ox);
	say(", ", oy);
	ak_puts("\n");
	check("panneau opaque par-dessus la surface", 16 + 180, 56 + 50, j[1], 0);
	check("trou : la surface se voit", 320, 96, pat(320, 96 - SY), 0);
	check("voile : rouge à moitié transparent", 460, 96, half_over(pat(460, 96 - SY)), 3);
	check("hors des zones : la surface", 300, 300, pat(300, 300 - SY), 0);

	/* la fenêtre agrandie : la surface suit, le panneau va en bas à droite */
	if (ui_resize(W1, H1) < 0 || !ui_surface_move(surf, 0, SY, W1, H1 - SY)) {
		ak_puts("ECHEC ui_resize ou ui_surface_move\n");
		fails++;
	} else {
		fill_surface();
		draw_content(W1, H1, W1 - 236, H1 - 156);
		wait_ms(600);
		if (!take()) {
			ak_puts("ECHEC repère introuvable après l'agrandissement\n");
			fails++;
		} else {
			check("agrandie : l'ancienne place du panneau montre la surface", 16 + 180, 56 + 50,
			      pat(16 + 180, 56 + 50 - SY), 0);
			check("agrandie : le panneau à sa nouvelle place", W1 - 236 + 180, H1 - 156 + 50, j[1], 0);
			check("agrandie : la surface jusqu'au bord", W1 - 4, SY + 8, pat(W1 - 4, 8), 0);
		}
	}

	/* les jetons de la CAO */
	static const u32 more[2][4] = {{0x00E9ECF1, 0x00D93A3F, 0x001E8A5A, 0x002563D8},
				       {0x0023272E, 0x00E5534B, 0x0046A758, 0x004C8DF0}};
	long dark = j[22] != 0; /* TJ_DARK */
	long tok = 1;
	for (int i = 0; i < 4; i++)
		tok &= j[25 + i] == more[dark][i]; /* TJ_HOVER = 100 : 25e mot */
	ak_puts(tok ? "ok    " : "ECHEC ");
	ak_puts(dark ? "jetons TJ_HOVER, TJ_AXIS_X/Y/Z (Onyx Noir)\n" : "jetons TJ_HOVER, TJ_AXIS_X/Y/Z (Givre)\n");
	fails += !tok;

	if (secs > 0)
		wait_ms(secs * 1000);
	ak_puts(fails ? "overlaytest : FAIL\n" : "overlaytest : PASS\n");
	_exit(fails ? 1 : 0);
}
