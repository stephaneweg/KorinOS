/*
 * gamefb.h -- cadre commun des jeux en C qui dessinent leurs pixels : la traduction de src/inc/gamefb.inc
 * (memes noms gf_*, meme comportement). Le jeu a une taille logique fixe GF_W x GF_H ; ses images sont faites
 * dans un tampon (gf_frame), puis mises a l'echelle (proportions gardees, plus proche voisin) dans une surface
 * attachee a toute la fenetre.
 *
 * Le canevas (gf_cv, ses pixels gf_layer) est la couche fixe du jeu : le decor et les textes y sont dessines
 * avec UIKit (entre ui_canvas_begin et _end) ; chaque image en repart (gf_copy) et y ajoute ce qui bouge.
 *
 * Le jeu definit GF_W, GF_H (largeurs multiples de 8), puis inclut gamesound.h, gamefb.h, gamedraw.h et
 * gamepause.h (un seul fichier .c : tout est static). Boucle : gf_run (evenement, pas, dessin) ; pas : 60 fois
 * par seconde. Les jeux sont en plein ecran : une fenetre sans cadre, au-dessus de tout, de la taille de
 * l'ecran (comme le plein ecran d'EmuKit) ; elle suit un changement de resolution. Le menu de pause est dans
 * gamepause.h. Les variables de l'interface (ui) sont ici.
 */
#ifndef KORIN_GAMEFB_H
#define KORIN_GAMEFB_H

#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <uikit.h>
#include "gamesound.h"

#if !defined(GF_W) || !defined(GF_H)
#error "gamefb.h : definir GF_W et GF_H"
#endif

#define GF_PITCH	(GF_W * 4)
#define GF_FRAMES	60
#define GF_MAXW		4096

static struct ui_vars ui;
/* un champ 32 bits de l'evenement (EV_* ; WEV_PAD_BUTTONS...) */
#define GF_EV(off)	(*(int32_t *) (ui.ev + (off)))

typedef uint32_t gf_sprite_t;			/* un sprite : largeur, hauteur, puis les points (0 : transparent) */

static const unsigned *gf_tok;			/* les jetons du theme (ui_tokens) */
static long gf_cv;
static uint32_t *gf_layer;			/* pixels de la couche fixe */
static long gf_sf;
static uint32_t *gf_dst;			/* ou dessinent gf_rect, gf_sprite */
static unsigned long gf_rng;
static long gf_quit, gf_still, gf_redraw;
static unsigned long gf_frac;			/* (gf_run) ou en est le pas suivant, 0..999 : un jeu peut dessiner entre deux pas */
static long gf_up;				/* 1 : focus pris et fenetre remontee */
static unsigned long gf_dw[4];			/* image a l'ecran : l, h, x, y */
static unsigned char gf_held[256];		/* touches tenues */
static uint32_t gf_xmap[GF_MAXW];
#ifdef GF_SMOOTH
/* (GF_SMOOTH : une echelle qui n'est pas entiere est lissee, bilineaire : au plus proche voisin, des colonnes et des
 * lignes doublees ou sautees changent a chaque pas d'un defilement, et les traits fins scintillent) */
static uint32_t gf_xfrac[GF_MAXW];		/* (poids 0..256 de la colonne suivante) */
static uint32_t gf_hrow[2][GF_MAXW];		/* deux lignes de l'image deja etirees en largeur */
static long gf_hrow_y[2];
static int gf_smooth;
#endif
static uint32_t gf_frame[GF_W * GF_H] __attribute__ ((aligned (16)));
/* GF_DOUBLE : deux images dans le tampon de la surface ; on ecrit dans celle qu'Elegant ne lit pas, puis on la lui
 * montre (ui_surface_source) : sans cela, il compose parfois pendant que l'image s'ecrit, et l'on voit les lignes
 * se retracer (la moitie du haut deja a l'image suivante) au defilement (docs/DESIGN-ELEGANT.md 17, « Dechirure ») */
static int gf_dbl;				/* 1 : deux images (sinon une : trop grande, ou pas de GF_DOUBLE) */
static int gf_back;				/* l'image ou l'on ecrit, 0 ou 1 */

static inline void gp_draw (void);			/* (gamepause.h) */

static inline unsigned long gf_time_ms (void) { return gs_time_ms (); }

/* x0 = surface -> pixels ; *pitch (octets), *w, *h : les quatre valeurs rendues par ui_surface_pixels */
static inline uint32_t *gf_surface_pixels (long sf, long *pitch, long *w, long *h)
{
#if defined(__x86_64__)
	long r0 = ui_surface_pixels (sf);
	*pitch = uikit__ret[1];
	*w = uikit__ret[2];
	*h = uikit__ret[3];
	return (uint32_t *) r0;
#else
	register long x0 __asm__ ("x0") = sf;
	register long x1 __asm__ ("x1") = 0;
	register long x2 __asm__ ("x2") = 0;
	register long x3 __asm__ ("x3") = 0;
	register long (*x9) () __asm__ ("x9") = (long (*) ()) ui_surface_pixels;
	__asm__ volatile ("blr x9" : "+r" (x0), "+r" (x1), "+r" (x2), "+r" (x3), "+r" (x9) : :
		"x4", "x5", "x6", "x7", "x8", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30", "memory", "cc");
	*pitch = x1;
	*w = x2;
	*h = x3;
	return (uint32_t *) x0;
#endif
}

/* la surface couvre toute la fenetre ; l'image y est centree, proportions gardees (appelee aussi par
 * ui_on_resize) */
static inline void gf_layout (void)
{
	unsigned long W = (unsigned long) ui.w, H = (unsigned long) ui.h;
	unsigned long dw = W, dh = W * GF_H / GF_W;
	if (dh > H)
	{
		dh = H;
		dw = H * GF_W / GF_H;
	}
	if (dw > GF_MAXW)
		dw = GF_MAXW;
	gf_dw[0] = dw;
	gf_dw[1] = dh;
	gf_dw[2] = (W - dw) >> 1;
	gf_dw[3] = (H - dh) >> 1;
#ifdef GF_DOUBLE
	/* une surface de deux images de haut, ramenee a une : le tampon reste (ui_surface_move le garde s'il suffit) */
	if (gf_sf)
		ui_surface_free (gf_sf);
	gf_sf = 0;
	gf_dbl = 0;
	gf_back = 0;
	if (2 * H <= 4096 && (gf_sf = ui_surface (0, 0, (long) W, (long) (2 * H), WS_SF_XRGB8888)) != 0)
	{
		if (ui_surface_move (gf_sf, 0, 0, (long) W, (long) H))
			gf_dbl = 1;
		else
		{
			ui_surface_free (gf_sf);
			gf_sf = 0;
		}
	}
	if (!gf_sf)
		gf_sf = ui_surface (0, 0, (long) W, (long) H, WS_SF_XRGB8888);
#else
	if (gf_sf)
		ui_surface_move (gf_sf, 0, 0, (long) W, (long) H);
	else
		gf_sf = ui_surface (0, 0, (long) W, (long) H, WS_SF_XRGB8888);
#endif
	long pitch, w, h;
	uint32_t *p = gf_surface_pixels (gf_sf, &pitch, &w, &h);
	if (p)						/* les bandes : en noir, ou en degrade */
	{
		for (long y = 0; y < h * (gf_dbl + 1); y++)	/* GF_BAND_TOP -> GF_BAND_BOT si le jeu les definit (les deux images) */
		{
			uint32_t *row = (uint32_t *) ((char *) p + y * pitch);
#ifdef GF_BAND_TOP
			uint32_t k = (uint32_t) (((unsigned long) (y % h) << 8) / (unsigned long) h), ik = 256 - k;
			uint32_t t = GF_BAND_TOP, b = GF_BAND_BOT;
			uint32_t c = ((((t & 0xFF00FF) * ik + (b & 0xFF00FF) * k) >> 8) & 0xFF00FF)
				| ((((t & 0xFF00) * ik + (b & 0xFF00) * k) >> 8) & 0xFF00);
#else
			uint32_t c = 0;
#endif
			for (long x = 0; x < w; x++)
				row[x] = c;
		}
	}
	for (unsigned long x = 0; x < dw; x++)		/* colonne de chaque point de l'ecran */
		gf_xmap[x] = (uint32_t) (x * GF_W / dw);
#ifdef GF_SMOOTH
	gf_smooth = !(dw % GF_W == 0 && dh % GF_H == 0 && dw / GF_W == dh / GF_H);	/* (echelle entiere : nette) */
	for (unsigned long x = 0; x < dw && gf_smooth; x++)
	{						/* le centre du point, dans l'image : colonne et poids */
		long fx = (long) (((2 * x + 1) * GF_W * 256) / (2 * dw)) - 128;
		if (fx < 0) fx = 0;
		if (fx > (GF_W - 1) * 256) fx = (GF_W - 1) * 256;
		gf_xmap[x] = (uint32_t) (fx >> 8);
		gf_xfrac[x] = gf_xmap[x] + 1 < GF_W ? (uint32_t) (fx & 255) : 0;
	}
#endif
	gf_redraw = 1;
}

static inline void gf_layout_cb (long w, long h)
{
	(void) w;
	(void) h;
	gf_layout ();
}

/* titre -> 0 (< 0 : pas de fenetre) */
static inline long gf_open (const char *title)
{
	if (!uikit_open (&ui))
		return -1;
	long scr[4] = { 0, 0, 0, 0 };
	ui_screen ((long) scr);
	if (ui_window_ex (0, 0, scr[0], scr[1], (long) title, WF_FRAMELESS | WF_TOP) < 0)
		return -1;
	ui_raise (ui.win);				/* (au-dessus du panel, WF_TOP lui aussi) */
	gf_tok = (const unsigned *) ui_tokens ();
	ui_on_resize ((long) gf_layout_cb, 0);	/* (le .abi lit deux arguments dans son commentaire) */
	gf_cv = ui_canvas (0, 0, GF_W, GF_H);		/* (sous la surface, jamais vu) */
	gf_layer = (uint32_t *) ui_canvas_pixels (gf_cv);
	gf_dst = gf_frame;
	gf_rng = gf_time_ms () ^ 0x9E3779B97F4A7C15ul;
	gf_layout ();
	return 0;
}

/* destination, source, octets */
static inline void gf_copy (void *d, const void *s, unsigned long n) { memcpy (d, s, n); }

/* image GF_W x GF_H : a l'ecran */
static inline void gf_present (const uint32_t *src)
{
	if (!gf_sf)
		return;
	long pitch, w, h;
	uint32_t *p = gf_surface_pixels (gf_sf, &pitch, &w, &h);
	if (!p)
		return;
	if (gf_dbl)					/* (l'image qu'Elegant ne lit pas) */
		p = (uint32_t *) ((char *) p + (long) gf_back * pitch * h);
	unsigned long dw = gf_dw[0], dh = gf_dw[1];
	char *d = (char *) p + gf_dw[3] * pitch + gf_dw[2] * 4;
	if (dw == GF_W && dh == GF_H)
	{
		for (int y = 0; y < GF_H; y++, d += pitch, src += GF_W)
			memcpy (d, src, GF_PITCH);
	}
#ifdef GF_SMOOTH
	else if (gf_smooth)
	{
		gf_hrow_y[0] = gf_hrow_y[1] = -1;
		for (unsigned long y = 0; y < dh; y++, d += pitch)
		{
			long fy = (long) (((2 * y + 1) * GF_H * 256) / (2 * dh)) - 128;
			if (fy < 0) fy = 0;
			if (fy > (GF_H - 1) * 256) fy = (GF_H - 1) * 256;
			long y0 = fy >> 8, y1 = y0 + 1 < GF_H ? y0 + 1 : y0;
			uint32_t wy = (uint32_t) (fy & 255), iwy = 256 - wy;
			uint32_t *r[2];
			for (int k = 0; k < 2; k++)
			{				/* la ligne de l'image, etiree en largeur (gardee pour la ligne suivante) */
				long sy = k ? y1 : y0;
				int slot = gf_hrow_y[0] == sy ? 0 : gf_hrow_y[1] == sy ? 1 : -1;
				if (slot < 0)
				{
					slot = gf_hrow_y[0] == (k ? y0 : -2) ? 1 : (gf_hrow_y[1] == (k ? y0 : -2) ? 0 : k);
					const uint32_t *s = src + sy * GF_W;
					uint32_t *o = gf_hrow[slot];
					for (unsigned long x = 0; x < dw; x++)
					{
						uint32_t a = s[gf_xmap[x]], w = gf_xfrac[x];
						if (!w) { o[x] = a; continue; }
						uint32_t b = s[gf_xmap[x] + 1], iw = 256 - w;
						o[x] = ((((a & 0xFF00FF) * iw + (b & 0xFF00FF) * w) >> 8) & 0xFF00FF)
						     | ((((a & 0xFF00) * iw + (b & 0xFF00) * w) >> 8) & 0xFF00);
					}
					gf_hrow_y[slot] = sy;
				}
				r[k] = gf_hrow[slot];
			}
			uint32_t *o = (uint32_t *) d;
			if (!wy || r[0] == r[1])
				memcpy (o, r[0], dw * 4);
			else
				for (unsigned long x = 0; x < dw; x++)
				{
					uint32_t a = r[0][x], b = r[1][x];
					o[x] = ((((a & 0xFF00FF) * iwy + (b & 0xFF00FF) * wy) >> 8) & 0xFF00FF)
					     | ((((a & 0xFF00) * iwy + (b & 0xFF00) * wy) >> 8) & 0xFF00);
				}
		}
	}
#endif
	else
	{
		long prev = -1;
		for (unsigned long y = 0; y < dh; y++, d += pitch)
		{
			uint32_t *o = (uint32_t *) d;
			long sy = (long) (y * GF_H / dh);
			if (sy == prev)			/* la meme : recopie */
				memcpy (o, d - pitch, dw * 4);
			else
			{
				prev = sy;
				const uint32_t *s = src + sy * GF_W;
				for (unsigned long x = 0; x < dw; x++)
					o[x] = s[gf_xmap[x]];
			}
		}
	}
	if (gf_dbl)					/* elle est finie : Elegant la prend, l'autre sera ecrite */
	{
		if (ui_surface_source (gf_sf, -1, pitch, (long) gf_back * pitch * h))
			gf_back ^= 1;
		else
		{					/* (refusee : une seule image, comme sans GF_DOUBLE) */
			gf_dbl = 0;
			ui_surface_source (gf_sf, -1, 0, 0);
		}
	}
	ui_surface_damage (gf_sf, (long) gf_dw[2], (long) gf_dw[3], (long) dw, (long) dh);
}

/* l'evenement (souris) -> le point dans l'image logique (borne) */
static inline void gf_mouse (long *px, long *py)
{
	long x = (long) GF_EV (EV_X) - (long) gf_dw[2], y = (long) GF_EV (EV_Y) - (long) gf_dw[3];
	x = x * GF_W / (long) gf_dw[0];
	y = y * GF_H / (long) gf_dw[1];
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > GF_W - 1) x = GF_W - 1;
	if (y > GF_H - 1) y = GF_H - 1;
	*px = x;
	*py = y;
}

/* hauteur du bandeau -> 0 : en plein ecran, la fenetre ne se deplace plus (garde pour les jeux qui l'appellent) */
static inline long gf_drag_top (long h) { (void) h; return 0; }

/* --- generateur ------------------------------------------------------------------------------------------------- */
/* -> pseudo-aleatoire (xorshift64) */
static inline unsigned long gf_rand (void)
{
	unsigned long x = gf_rng;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	gf_rng = x;
	return x;
}

/* borne -> dans [0, borne[ (borne 0 : comme l'asm, udiv par 0 rend 0, le tirage brut) */
static inline unsigned long gf_below (unsigned long n)
{
	unsigned long r = gf_rand () >> 16;
	return n ? r % n : r;
}

/* --- dessin dans gf_dst --------------------------------------------------------------------------------------- */
/* rectangle, couleur (decoupe) */
static inline void gf_rect (long x, long y, long w, long h, uint32_t c)
{
	long x1 = x + w, y1 = y + h;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > GF_W) x1 = GF_W;
	if (y1 > GF_H) y1 = GF_H;
	if (x >= x1 || y >= y1)
		return;
	for (; y < y1; y++)
	{
		uint32_t *r = gf_dst + y * GF_W;
		for (long i = x; i < x1; i++)
			r[i] = c;
	}
}

/* 32 bits : chaque octet ajoute, sature (uqadd) */
static inline uint32_t gf_addsat (uint32_t a, uint32_t b)
{
	uint32_t s = (a & 0x7F7F7F7F) + (b & 0x7F7F7F7F);	/* sans les bits hauts */
	uint32_t c = (a & b) | ((a | b) & s);			/* retenue sortie de chaque octet (bit 7) */
	c &= 0x80808080;
	s = (s ^ ((a ^ b) & 0x80808080));
	return s | ((c >> 7) * 0xFF);
}

/* sprite : glow 0 opaque (gf_sprite), 1 ajoute la lumiere (gf_glow), 2 le decor seul (gf_bglow : pas sur les
 * points d'octet haut 0xFF) */
static inline void gf_blit (long x, long y, const gf_sprite_t *s, int glow)
{
	long w = s[0], h = s[1];
	const uint32_t *px = s + 2;
	long c0 = x < 0 ? -x : 0, c1 = w;
	if (c1 > GF_W - x) c1 = GF_W - x;
	long r0 = y < 0 ? -y : 0, r1 = h;
	if (r1 > GF_H - y) r1 = GF_H - y;
	if (r0 >= r1 || c0 >= c1)
		return;
	uint32_t *d = gf_dst + (y + r0) * GF_W + x;
	px += r0 * w;
	for (long r = r0; r < r1; r++, px += w, d += GF_W)
		for (long c = c0; c < c1; c++)
		{
			uint32_t v = px[c];
			if (!v)
				continue;
			if (glow == 2 && (d[c] >> 24) == 255)
				continue;
			d[c] = glow ? gf_addsat (d[c], v) : v;
		}
}
static inline void gf_sprite (long x, long y, const gf_sprite_t *s) { gf_blit (x, y, s, 0); }
static inline void gf_glow (long x, long y, const gf_sprite_t *s) { gf_blit (x, y, s, 1); }
static inline void gf_bglow (long x, long y, const gf_sprite_t *s) { gf_blit (x, y, s, 2); }

static inline const char *gf_tr (const char *s) { return (const char *) ui_tr ((long) s); }

/* texte, x, base, couleur, corps, police (dans la couche) */
static inline void gf_text (const char *s, long x, long base, uint32_t c, long px, long font)
{
	ui_text ((long) gf_tr (s), x, base, c, px, font, 0);
}

/* texte, x du centre, base, couleur, corps, police */
static inline void gf_text_c (const char *s, long cx, long base, uint32_t c, long px, long font)
{
	s = gf_tr (s);
	long w = ui_text_width ((long) s, px, font);
	ui_text ((long) s, cx - (w >> 1), base, c, px, font, 0);
}

/* --- meilleur score -------------------------------------------------------------------------------------------- */
/* chemin -> valeur lue (0 : pas de fichier) */
static inline unsigned long gf_load_best (const char *path)
{
	char b[32];
	unsigned long v = 0;
	int fd = open (path, O_RDONLY);
	if (fd < 0)
		return 0;
	long n = read (fd, b, 31);
	close (fd);
	for (long i = 0; i < n; i++)
	{
		unsigned d = (unsigned char) b[i] - '0';
		if (d > 9)
			break;
		v = v * 10 + d;
	}
	return v;
}

/* dossier, chemin, valeur */
static inline void gf_save_best (const char *dir, const char *path, unsigned long v)
{
	char b[32], t[24];
	int n = 0, k = 0;
	mkdir (dir, 0755);
	do
		t[k++] = (char) ('0' + v % 10);
	while ((v /= 10) && k < 22);
	while (k)
		b[n++] = t[--k];
	b[n++] = '\n';
	int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	write (fd, b, (size_t) n);
	close (fd);
}

/* --- la boucle ------------------------------------------------------------------------------------------------ */
typedef void (*gf_event_fn) (long type);
static gf_event_fn gf_game_event;

/* attente (ms) : un evenement traite (le jeu recoit WEV_KEY, WEV_MOUSE, WEV_MENU, WEV_FOCUS, WEV_PAD) -> son type */
static inline long gf_pump (long ms)
{
	long t = ui_event (ms);
	if (!t)
		return 0;
	if (t == WEV_CLOSE)
	{
		gf_quit = 1;
		return t;
	}
	if (t == WEV_RESIZE)
	{
		ui_w_event (0);
		return t;
	}
	if (t == WEV_SCREEN)				/* une autre resolution : la fenetre la suit */
	{
		uint32_t w = (uint32_t) GF_EV (8), h = (uint32_t) GF_EV (12);
		if (w && h)
		{
			ui_resize (w, h);
			gf_layout ();
		}
		return t;
	}
	if (t == WEV_KEY)				/* touches tenues */
	{
		uint32_t code = (uint32_t) GF_EV (EV_CODE);
		if (code < 256)
			gf_held[code] = GF_EV (EV_KIND) != 0;
	}
	else if (t == WEV_FOCUS)
	{						/* le focus revient : au-dessus du panel et des autres fenetres WF_TOP */
		long k = (uint32_t) GF_EV (EV_KIND), old = gf_up;
		gf_up = k;
		if (k && !old)
			ui_raise (ui.win);
		memset (gf_held, 0, sizeof gf_held);
	}
	else if (t != WEV_MOUSE && t != WEV_PAD && t != WEV_MENU)
		return t;
	gf_game_event (t);
	return t;
}

/* evenement, pas, dessin (fait gf_frame). Rendue quand gf_quit est leve ou a la fermeture. gf_still = 1 : plus
 * d'images (pause), sauf si gf_redraw est leve. */
static inline void gf_run (gf_event_fn ev, void (*step) (void), void (*draw) (void))
{
	gf_game_event = ev;
	unsigned long last = gf_time_ms (), acc = 0;	/* heure du dernier passage, reste (milliemes de pas) */
	for (;;)
	{
		if (gf_quit)
			return;
		if (gf_still && !gf_redraw)
		{
			gs_tick ();
			gf_pump (50);
			last = gf_time_ms ();
			acc = 0;
			continue;
		}
		if (!gf_still)
		{
			unsigned long t = gf_time_ms ();
			acc += (t - last) * GF_FRAMES;
			last = t;
			unsigned long n = acc / 1000;
			acc %= 1000;
			if (!n)
			{
				gf_pump (2);
				continue;
			}
			if (n > 5)			/* (trop de retard : on l'oublie) */
				n = 5;
			while (n--)
				step ();
			gs_tick ();
			gf_frac = acc;
		}
		gf_redraw = 0;
		draw ();
		gp_draw ();				/* le menu de pause, le bouton || */
		gf_present (gf_frame);
		if (ui_frame_request ())		/* la suivante quand celle-ci est prise */
		{
			unsigned long end = gf_time_ms () + 40;
			for (;;)
			{
				if (gf_pump (8) == WEV_FRAME)
					break;
				if (gf_quit)
					return;
				if (gf_time_ms () >= end)
					break;
			}
		}
	}
}

#endif
