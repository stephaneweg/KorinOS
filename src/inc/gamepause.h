/*
 * gamepause.h -- ce qui est commun aux jeux en C en plein ecran (sur gamefb.h et gamedraw.h) : le menu de pause et
 * la manette. La traduction de src/inc/gamepause.inc (memes noms gp_*, meme comportement) :
 *
 *   Start (manette), Pause ou Echap (clavier), ou le bouton || en haut de l'image : le jeu s'arrete, le menu
 *   s'ouvre : Reprendre, Recommencer, Quitter ; dans le menu : croix ou fleches, A / Entree / Espace / clic
 *   validant ; B, Start, Pause ou Echap : retour au jeu. Au clavier, les lettres a, b, x, y sont les boutons A,
 *   B, X, Y et Pause est Start (le caractere, quelle que soit la disposition) : le jeu les recoit par sa fonction
 *   de manette, comme ceux d'une vraie manette. La perte du focus pendant une partie ouvre aussi le menu.
 *
 * Apres ga_init (hors de ui_canvas_begin) : gp_init (pause, reprise, nouvelle partie, x, y du bouton || dans
 * l'image logique (x < 0 : pas de bouton), fonction de manette du jeu, appelee pour les boutons du clavier).
 * pause : le jeu s'arrete -> 1 si une partie etait en cours ; reprise : la partie repart (si elle etait arretee).
 * En tete de la fonction d'evenements du jeu : if (gp_event (t)) return. Pour WEV_PAD, gp_event a deja calcule
 * gp_now (boutons PAD_*) et gp_new (ceux qui viennent d'etre appuyes) des manettes 1..3 et des lettres du
 * clavier : le jeu les lit ; la manette 0 (le clavier mappe par HIDManager, deja arrive en touches) ne parvient pas
 * au jeu. Essai sous QEMU, sans manette : -DGP_PAD0=1 (le clavier compte alors comme une manette). gf_run dessine
 * le menu par-dessus l'image du jeu (gp_draw).
 */
#ifndef KORIN_GAMEPAUSE_H
#define KORIN_GAMEPAUSE_H

#include "gamedraw.h"
#include "gamepad.h"

#define GP_N		3			/* Reprendre, Recommencer, Quitter */
#define GP_PW		300			/* panneau */
#define GP_IW		236			/* une entree */
#define GP_IH		40
#define GP_IS		48			/* d'une entree a la suivante */
#define GP_PH		(92 + GP_N * GP_IS + 30)
#define GP_PX		((GF_W - GP_PW) / 2)
#define GP_PY		((GF_H - GP_PH) / 2)
#define GP_IX		((GF_W - GP_IW) / 2)
#define GP_IY		(GP_PY + 78)
#define GP_BS		28			/* le bouton || */

#define GP_K_ENTER	0x28
#define GP_K_ESC	0x29
#define GP_K_SPACE	0x2C
#define GP_K_DOWN	0x51
#define GP_K_UP		0x52
#define GP_K_PAUSE	0x48
#define GP_NK		5			/* a b x y, Pause : les boutons du clavier */

#define GPT_TITLE	0
#define GPT_ON		1
#define GPT_OFF		(GPT_ON + GP_N)
#define GPT_HINT	(GPT_OFF + GP_N)
#define GP_NTX		(GPT_HINT + 1)

static long (*gp_fn_pause) (void);
static void (*gp_fn_resume) (void);
static void (*gp_fn_new) (void);
static void (*gp_fn_pad) (void);
static long gp_bx, gp_by;
static long gp_on, gp_sel, gp_down;
static unsigned long gp_now, gp_new;
static unsigned long gp_pd;			/* les boutons des manettes */
static unsigned long gp_kb;			/* ceux du clavier */
static unsigned long gp_kc[GP_NK];		/* le code HID de chaque bouton du clavier tenu */
static gf_sprite_t *gp_txt[GP_NTX];

static const struct ga_txe gp_tx[GP_NTX] = {
	{ "Pause", 28, 1, 0x00FFFFFF, 0x00C8D8F0 },
	{ "Reprendre", 17, 1, 0x00FFFFFF, 0x00F0F4FF },
	{ "Recommencer", 17, 1, 0x00FFFFFF, 0x00F0F4FF },
	{ "Quitter", 17, 1, 0x00FFFFFF, 0x00F0F4FF },
	{ "Reprendre", 17, 0, 0x00C8D0E0, 0x00A8B0C4 },
	{ "Recommencer", 17, 0, 0x00C8D0E0, 0x00A8B0C4 },
	{ "Quitter", 17, 0, 0x00C8D0E0, 0x00A8B0C4 },
	{ "A : valider    B : retour", 12, 0, 0x008890A8, 0x00788098 },
};
static const char gp_kch[4] = { 'a', 'b', 'x', 'y' };
static const unsigned gp_kbit[GP_NK] = { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_START };

static inline void gp_init (long (*pause) (void), void (*resume) (void), void (*newgame) (void), long bx, long by,
		     void (*pad) (void))
{
	gp_fn_pause = pause;
	gp_fn_resume = resume;
	gp_fn_new = newgame;
	gp_fn_pad = pad;
	gp_bx = bx;
	gp_by = by;
	ui_canvas_begin (gf_cv);
	ga_texts (gp_tx, gp_txt, GP_NTX);
	ui_canvas_end ();
}

static inline void gp_blip (void) { gs_later (0, 880, 25, AKW_SINE, 70); }

/* le menu se montre (le jeu est deja arrete) */
static inline void gp_show (void)
{
	gp_on = 1;
	gp_sel = 0;
	gf_redraw = 1;
	gp_down = 0;
	gp_blip ();
}

/* le menu s'ouvre (la partie s'arrete d'abord) */
static inline void gp_open (void)
{
	if (gp_on)
		return;
	gp_fn_pause ();
	gp_show ();
}

/* retour au jeu */
static inline void gp_close (void)
{
	gp_on = 0;
	gf_redraw = 1;
	gp_fn_resume ();
}

/* entree : elle agit */
static inline void gp_do (unsigned long i)
{
	if (i < 1)
		gp_close ();
	else if (i == 1)
	{
		gp_on = 0;
		gf_redraw = 1;
		gp_fn_new ();
	}
	else
		gf_quit = 1;
}

/* +1 / -1 : la selection bouge */
static inline void gp_move (long d)
{
	long s = gp_sel + d;
	if (s < 0)
		s = GP_N - 1;
	if (s >= GP_N)
		s = 0;
	gp_sel = s;
	gf_redraw = 1;
	gp_blip ();
}

/* point logique -> entree sous le point, -1 : aucune */
static inline long gp_hit (long x, long y)
{
	if ((unsigned long) (x - GP_IX) >= GP_IW)
		return -1;
	if (y < GP_IY)
		return -1;
	unsigned long d = (unsigned long) (y - GP_IY), i = d / GP_IS;
	if (d % GP_IS >= GP_IH || i >= GP_N)
		return -1;
	return (long) i;
}

/* WEV_KEY -> le bouton PAD_* de la touche, 0 : aucun. A l'appui, par le caractere (a b x y) ou le code (Pause) ;
 * le code est garde pour retrouver le bouton au relachement. */
static inline unsigned long gp_keypad (void)
{
	unsigned long code = (uint32_t) GF_EV (EV_CODE);
	int i;
	if (!GF_EV (EV_KIND))
	{
		for (i = 0; i < GP_NK; i++)
			if (gp_kc[i] == code)
			{
				gp_kc[i] = 0;
				return gp_kbit[i];
			}
		return 0;
	}
	if (code == GP_K_PAUSE)
		i = 4;
	else
	{
		unsigned ch = (uint32_t) GF_EV (EV_CHAR) | 0x20;	/* (majuscule ou minuscule) */
		for (i = 0; i < 4; i++)
			if ((unsigned char) gp_kch[i] == ch)
				break;
		if (i == 4)
			return 0;
	}
	gp_kc[i] = code;
	return gp_kbit[i];
}

/* type d'evenement -> 1 s'il a servi au menu (le jeu l'ignore) */
static inline long gp_event (long t)
{
	unsigned long b;
	int kbd = 0;
	if (t == WEV_PAD)
	{						/* les fronts, pour le jeu aussi */
#ifdef GP_PAD0
		if (!GF_EV (EV_KIND))			/* (essai sous QEMU : le clavier mappe compte comme une manette) */
			GF_EV (EV_KIND) = 1;
#else
		if (!GF_EV (EV_KIND))			/* (manette 0 : le clavier, deja en touches) */
			return 1;
#endif
		gp_pd = (uint32_t) GF_EV (WEV_PAD_BUTTONS);
		b = gp_pd | gp_kb;			/* (une vraie manette : le jeu la recoit) */
	}
	else if (t == WEV_KEY && (b = gp_keypad ()) != 0)
	{						/* a b x y, Pause : des boutons */
		unsigned long k = gp_kb & ~b;
		if (GF_EV (EV_KIND))
			k |= b;
		gp_kb = k;
		b = k | gp_pd;
		GF_EV (EV_KIND) = 1;			/* (pour la fonction de manette : un appui) */
		kbd = 1;
	}
	else
	{
		if (t == WEV_KEY)
		{
			long kind = GF_EV (EV_KIND);
			uint32_t code = (uint32_t) GF_EV (EV_CODE);
			if (!gp_on)
			{
				if (!kind || code != GP_K_ESC)
					return 0;
				gp_open ();
				return 1;
			}
			if (!kind)
				return 1;
			if (code == GP_K_ESC)
				gp_close ();
			else if (code == GP_K_ENTER || code == GP_K_SPACE)
				gp_do ((unsigned long) gp_sel);
			else if (code == GP_K_UP)
				gp_move (-1);
			else if (code == GP_K_DOWN)
				gp_move (1);
			return 1;
		}
		if (t == WEV_MOUSE)
		{
			long mx, my;
			gf_mouse (&mx, &my);
			long kind = GF_EV (EV_KIND);
			if (!gp_on)
			{				/* un appui sur le bouton || */
				if (kind != 1 || !(GF_EV (EV_CODE) & 1) || gp_bx < 0)
					return 0;
				if ((unsigned long) (mx - gp_bx) >= GP_BS || (unsigned long) (my - gp_by) >= GP_BS)
					return 0;
				gp_open ();
				return 1;
			}
			long h = gp_hit (mx, my);	/* dans le menu : survol, appui, relache */
			if (h >= 0 && gp_sel != h)
			{
				gp_sel = h;
				gf_redraw = 1;
			}
			if (kind == 1)
			{
				if (GF_EV (EV_CODE) & 1)
					gp_down = h + 1;	/* l'entree appuyee (+1, 0 : dehors) */
			}
			else if (kind == 2)
			{
				long d = gp_down;
				gp_down = 0;
				if (d && d - 1 == h)
					gp_do ((unsigned long) h);
			}
			return 1;
		}
		if (t == WEV_FOCUS)
		{
			gp_now = gp_pd = gp_kb = 0;	/* (les boutons tenus sont oublies) */
			memset (gp_kc, 0, sizeof gp_kc);
			if (GF_EV (EV_KIND) || gp_on)
				return 0;
			if (gp_fn_pause ())		/* perdu en pleine partie : le menu */
				gp_show ();
			return 0;
		}
		if (t == WEV_MENU)
			return gp_on ? 1 : 0;
		return 0;
	}
	/* les boutons */
	unsigned long nw = b & ~gp_now;
	gp_now = b;
	gp_new = nw;
	if (!gp_on)
	{
		if (nw & PAD_START)
		{
			gp_open ();
			return 1;
		}
		if (!kbd)
			return 0;
		if (gp_fn_pad)				/* (le clavier : la fonction de manette) */
			gp_fn_pad ();
		return 1;
	}
	if (nw & (PAD_START | PAD_B))
		gp_close ();
	else if (nw & PAD_A)
		gp_do ((unsigned long) gp_sel);
	else if (nw & PAD_UP)
		gp_move (-1);
	else if (nw & PAD_DOWN)
		gp_move (1);
	return 1;
}

/* dans gf_frame : l'image s'assombrit, le panneau et ses entrees ; sinon le bouton || */
static inline void gp_draw (void)
{
	gf_dst = gf_frame;
	if (!gp_on)
	{
		if (gp_bx < 0)
			return;
		ga_panel (gp_bx, gp_by, GP_BS, GP_BS, 8, 0x00303A50, 0x00181E2C, 150, 0x00687890);
		gf_rect (gp_bx + 9, gp_by + 8, 4, GP_BS - 16, 0x00E8EEF8);
		gf_rect (gp_bx + GP_BS - 13, gp_by + 8, 4, GP_BS - 16, 0x00E8EEF8);
		return;
	}
	for (long i = 0; i < GF_W * GF_H; i++)		/* tout a 3/8, un peu bleui */
	{
		uint32_t v = gf_frame[i];
		gf_frame[i] = ((v >> 2) & 0x3F3F3F3F) + ((v >> 3) & 0x1F1F1F1F) + 0x06060606;
	}
	ga_panel (GP_PX + 4, GP_PY + 8, GP_PW, GP_PH, 22, 0, 0, 110, 0);	/* ombre, panneau */
	ga_panel (GP_PX, GP_PY, GP_PW, GP_PH, 22, 0x00262E44, 0x00141926, 245, 0x00586888);
	ga_show (gp_txt[GPT_TITLE], GF_W / 2, GP_PY + 22);
	for (long i = 0; i < GP_N; i++)
	{
		long y = GP_IY + i * GP_IS;
		const gf_sprite_t *s;
		if (gp_sel == i)
		{
			ga_panel (GP_IX, y, GP_IW, GP_IH, GP_IH / 2, 0x005A9CFF, 0x002F68E0, 256, 0x00A8C8FF);
			s = gp_txt[GPT_ON + i];
		}
		else
		{
			ga_panel (GP_IX, y, GP_IW, GP_IH, GP_IH / 2, 0x00303A52, 0x00262E44, 256, 0);
			s = gp_txt[GPT_OFF + i];
		}
		ga_show (s, GF_W / 2, y + ((GP_IH - (long) s[1]) >> 1));
	}
	ga_ablit_c (GF_W / 2, GP_PY + GP_PH - 26, gp_txt[GPT_HINT]);
}

#endif
