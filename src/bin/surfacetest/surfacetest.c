/* surfacetest - essai de la surface attachée d'Elegant (WS_OP_SURFACE_ATTACH,
 * WS_OP_SURFACE_DAMAGE ; UIKit ui_surface_*, docs/DESIGN-ELEGANT.md 17), comme
 * surfacetest.asm :
 *
 *	surfacetest [secondes] [rgb565]
 *
 * Une fenêtre, un texte dessiné par UIKit dans son contenu, et une surface
 * animée (dégradé qui défile, barre blanche qui avance) composée par Elegant
 * depuis le tampon du programme. Trois phases : zone 320 x 240, zone déplacée
 * et réduite (même tampon, ui_surface_move), zone agrandie (tampon neuf).
 * Sortie : images écrites par seconde (images signalées à Elegant ; il en
 * compose au plus une par tour de sa boucle), « surfacetest : PASS / FAIL ».
 *
 * Comme l'asm : les mots de la ligne d'arguments du noyau (séparés par des
 * espaces) ; un mot qui commence par un chiffre donne la durée (parse_dec ;
 * 0 : 1 s), tout mot qui commence par « r » le format RGB565, les autres sont
 * ignorés ; 5 s par défaut. Sortie sans tampon (puts, print_dec) ; code 0
 * (PASS) ou 1 (FAIL : pas de fenêtre, pas de surface, déplacement refusé). */
#include "korinrt.h"
#include "uikit_import.h"

#define ST_WIN_W 400
#define ST_WIN_H 300
#define ST_TOP 40 /* la surface sous la ligne de texte */
#define ST_BG 0x00303846

static struct krt_ui ui;
static long surf;
static long fmt;
static u64 t;     /* images (x23 de l'asm) */
static u64 phase; /* x24 */

/* phase (1, 2) : la zone change (1 : déplacée et réduite, même tampon ;
 * 2 : agrandie, tampon neuf) ; le fond du contenu redessiné -> 1 si réussi */
static long next_phase(void)
{
	ui_fill(0, ST_TOP, ST_WIN_W, ST_WIN_H - ST_TOP, ST_BG); /* (la zone d'avant ne doit pas rester) */
	ui_update(0, ST_TOP, ST_WIN_W, ST_WIN_H - ST_TOP);
	long r;
	const char *m;
	if (phase == 1) {
		r = ui_surface_move(surf, 56, ST_TOP + 30, 240, 180);
		m = "surfacetest : zone déplacée (même tampon)\n";
	} else {
		r = ui_surface_move(surf, 8, ST_TOP, 384, ST_WIN_H - ST_TOP - 8);
		m = "surfacetest : zone agrandie (tampon neuf)\n";
	}
	ak_puts(m);
	return r;
}

/* une image dans la surface : dégradé qui défile (t), barre blanche */
static void draw_frame(void)
{
	struct krt_r4 p = krt_call_r4(ui_surface_pixels, surf, 0, 0, 0); /* pixels, pas, l, h */
	u8 *pix = (u8 *)p.r0;
	u64 pitch = (u64)p.r1, w = (u64)p.r2, h = (u64)p.r3;
	u64 bar = (t << 2) % w; /* barre : x = 4t mod l */
	u32 tt = (u32)t;
	for (u64 y = 0; y < h; y++) {
		u8 *row = pix + y * pitch;
		u32 g = ((u32)y + (tt << 1)) & 255; /* vert = y + 2t */
		for (u64 x = 0; x < w; x++) {
			u32 r = ((u32)x + tt) & 255;                     /* rouge = x + t */
			u32 b = ((((u32)x ^ (u32)y) + tt) + (tt << 1)) & 255; /* bleu = (x ^ y) + 3t */
			if (x - bar < 6) /* dans la barre (6 px) ? */
				r = g = b = 255;
			if (!fmt) {
				*(u32 *)row = (r << 16) | (g << 8) | b;
				row += 4;
			} else {
				*(u16 *)row = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
				row += 2;
			}
			if (x - bar < 6) /* (barre : vert gardé par le pixel suivant) */
				g = ((u32)y + (tt << 1)) & 255;
		}
	}
}

static NORETURN void fail(const char *m)
{
	ak_puts(m);
	ak_puts("surfacetest : FAIL\n");
	_exit(1);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	u64 secs = 5; /* secondes par défaut */
	long f = WS_SF_XRGB8888;
	const char *s = krt_line;
	if (s) {
		for (;;) { /* mots : un nombre, ou « rgb565 » */
			while (*s == ' ')
				s++;
			if (!*s)
				break;
			if ((u32)(u8)*s - '0' <= 9) {
				long v = (long)parse_dec(s);
				secs = v >= 1 ? (u64)v : 1;
			} else if (*s == 'r') {
				f = WS_SF_RGB565;
			}
			while (*s && *s != ' ')
				s++;
			if (!*s)
				break;
		}
	}
	fmt = f;
	ui_init((long)&ui);
	if (ui_window(ST_WIN_W, ST_WIN_H, (long)"Surface attachée") < 0)
		fail("surfacetest : pas de fenêtre (Elegant ?)\n");
	ui_fill(0, 0, ST_WIN_W, ST_WIN_H, ST_BG); /* fond, texte */
	ui_text((long)(fmt ? "Surface attachée RGB565 (Elegant)" : "Surface attachée XRGB8888 (Elegant)"), 12, 26,
		0x00F0F0F0, 15, (long)ui.font, 0);
	ui_update_all();
	surf = ui_surface(16, ST_TOP, 320, 240, fmt); /* phase 0 : 320 x 240 */
	if (!surf)
		fail("surfacetest : ui_surface a échoué (Elegant ou UIKit trop anciens ?)\n");
	ak_puts("surfacetest : surface attachée, animation\n");
	u64 t0 = (u64)sys_time_ms();
	t = 0;
	phase = 0;
	u64 dur = secs * 1000;
	u64 el;
	for (;;) {
		draw_frame();
		ui_surface_damage(surf, 0, 0, 0, 0);
		t++;
		if (ui_event(0) == WEV_CLOSE) /* événements (sans attente) */
			break;
		korin_sys0(SYS_YIELD);
		el = (u64)sys_time_ms() - t0;
		if (el >= dur)
			break;
		u64 ph = el * 3 / dur; /* phase = écoulé * 3 / durée */
		if (ph == phase)
			continue;
		phase = ph;
		if (!next_phase())
			fail("surfacetest : ui_surface_move a échoué\n");
	}
	el = (u64)sys_time_ms() - t0;
	ak_puts("surfacetest : "); /* « N images en T ms : F images/s » */
	print_dec((long)t);
	ak_puts(" images en ");
	print_dec((long)el);
	ak_puts(" ms : ");
	if ((long)el < 1)
		el = 1;
	u64 fps = t * 10000 / el; /* images/s (une décimale) */
	print_dec((long)(fps / 10));
	ak_puts(",");
	print_dec((long)(fps % 10));
	ak_puts(" images/s\n");
	ak_puts("surfacetest : PASS\n");
	ui_surface_free(surf);
	_exit(0);
}
