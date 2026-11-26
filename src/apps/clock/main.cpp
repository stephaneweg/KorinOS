//
// clock -- l'Horloge : une horloge analogique anti-crénelée et l'heure en chiffres (onglet Horloge), les alarmes
// (onglet Alarmes) que clockd sonne même l'Horloge fermée.
//
// Port en C++ de l'Horloge d'Onyx (user/Apps/clock : son modèle d'alarmes, alarms.{h,cpp}, et ses dates,
// clocktime.cpp ; ses sons, sounds.h) sur les kits de Korin en direct (ui_* d'UIKit, bus_* de BusKit, les voix FM
// d'AudioKit par src/inc/gamesound.h), avec ce que l'Horloge de Korin en assembleur (clock.asm, alarms.inc) a
// fait de différent et que l'utilisateur a validé : deux onglets seulement (Horloge, Alarmes), le cadran
// analogique de Korin dessiné au pixel (mêmes calculs en flottants simples, donc mêmes pixels), le réglage de
// l'heure par trois boutons (/etc/clock.cfg, korin.clock.offset), le fichier /etc/alarms.txt de Korin
// (alarms.cpp), le dialogue d'une alarme de l'asm. Un seul source pour le Pi et le PC.
//
// L'heure affichée : le décalage (secondes de la journée au démarrage) plus le temps depuis le démarrage, comme la
// barre de menus ; le décalage vient de /etc/clock.cfg, puis du bus (korin.clock.offset, gardé : netd après
// NTP sur le Pi, busd au démarrage sur le PC d'après l'horloge CMOS), lu au lancement et à chaque publication.
// Les alarmes prennent l'heure locale du noyau (SYS_CLOCK_GET 2), comme clockd.
//
// Fenêtre de taille fixe ; agrandie (mode poche d'Elegant, WEV_RESIZE : docs/MODE-POCHE.md), le cadran reste centré
// (g_ox, g_oy) et la page Alarmes suit ses ancres (ui_anchor).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
// (le cadran : chaque opération flottante comme l'asm, sans fusion implicite ; les fmadd / fmsub de l'asm sont
// des fmaf explicites)
#pragma GCC optimize ("fp-contract=off")
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <armpi/syscall.h>
#include <uikit.h>
#include <buskit_abi.h>
#include "../../inc/gamesound.h"
#include "alarms.h"

#define TR(s)		((const char *) ui_tr ((long) (s)))
#define SYS_TIME_MS	0x19

enum
{
	WIN_W = 400, WIN_H = 484,
	CX = 200, CY = 200, RAD = 150,
	TOP = 40,				// sous les onglets
	BTN_Y = 428, BTN_W = 96, BTN_H = 34, BTN_X0 = 44,
	M = 16,					// marge de l'onglet Alarmes
	LIST_Y = 84, LIST_H = 300,
	ROW_Y = 394,				// les boutons de l'onglet Alarmes
	DLG_W = 420, DLG_H = 340,
};

static struct ui_vars ui;
static unsigned tok (int off) { return ((const unsigned *) ui_tokens ())[off / 4]; }
static unsigned long time_ms (void) { return (unsigned long) armpi_syscall (SYS_TIME_MS, 0, 0, 0, 0, 0, 0); }

static const char P_CFG[] = "/etc/clock.cfg";
static const char CLK_TOPIC[] = "korin.clock.offset";

// ---- les sons des alarmes (sounds.h d'Onyx : carillon, bips, marimba ; un tour de chacun) ----
static const struct gs_ent SND_CHIMES[] = {		// un arpège montant, des cloches
	{ 0, 1047, 900, AKW_SINE, 170 }, { 250, 1319, 900, AKW_SINE, 170 }, { 500, 1568, 900, AKW_SINE, 170 },
	{ 750, 2093, 1200, AKW_SINE, 170 }, { 0, 0, 0, 0, 0 } };
static const struct gs_ent SND_BEEPS[] = {		// des bips aigus et courts
	{ 0, 2093, 90, AKW_SQUARE, 70 }, { 180, 2093, 90, AKW_SQUARE, 70 }, { 360, 2093, 90, AKW_SQUARE, 70 },
	{ 540, 2093, 90, AKW_SQUARE, 70 }, { 0, 0, 0, 0, 0 } };
static const struct gs_ent SND_MARIMBA[] = {		// une phrase douce, du bois
	{ 0, 784, 400, AKW_TRIANGLE, 200 }, { 200, 1047, 400, AKW_TRIANGLE, 200 }, { 400, 1319, 400, AKW_TRIANGLE, 200 },
	{ 600, 1047, 400, AKW_TRIANGLE, 200 }, { 1000, 1568, 700, AKW_TRIANGLE, 200 }, { 0, 0, 0, 0, 0 } };
static const struct gs_ent *const SOUNDS[ALARM_SOUNDS] = { SND_CHIMES, SND_BEEPS, SND_MARIMBA };

// ---- l'état ----
static long g_offset;				// décalage de l'heure (secondes de la journée)
static long g_ox, g_oy;				// décalage du cadran (centré dans une fenêtre agrandie)
static int  g_page;				// 0 l'horloge, 1 les alarmes
static long g_sub, g_sub_clk;			// abonnements : korin.clock.alarms, korin.clock.offset
static bool g_bus;				// BusKit ouvert
static long g_minute = -1;			// la minute de la dernière mise à jour des alarmes
static int  g_sel_id = -1;			// l'alarme choisie (son numéro)
static AlarmSet g_al;				// le fichier, relu avant chaque changement
static int  g_ord[ALARMS_MAX];			// les rangs, dans l'ordre des heures
static Alarm g_edit;				// l'alarme du dialogue
static long w_seg;
// les widgets de la page Alarmes, dans l'ordre de leurs ancres
enum { AW_NEXT, AW_LIST, AW_ADD, AW_EDIT, AW_TOGGLE, AW_DEL, AW_NOTE, AW_N };
static long g_aw[AW_N];
static const long ANCHORS[AW_N] = { UA_LEFT | UA_RIGHT | UA_TOP, UA_FILL, UA_LEFT | UA_BOTTOM, UA_LEFT | UA_BOTTOM,
				    UA_LEFT | UA_BOTTOM, UA_LEFT | UA_BOTTOM, UA_LEFT | UA_RIGHT | UA_BOTTOM };
// le dialogue
static long d_hh, d_mm, d_name, d_rep, d_snd, d_test, d_on, d_days[7];

static const char *const BTN_LABELS[3] = { "+1 h", "+1 min", "\xE2\x88\x92" "1 min" };
static const long ADJUST[3] = { 3600, 60, -60 };
static const char S_DAYS[7][4] = { "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim" };
static const char *const S_WDAYS[7] = { "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche" };
static const unsigned REP_MASKS[4] = { 0, AL_EVERYDAY, AL_WEEKDAYS, AL_WEEKENDS };

static char *cat (char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *cat_dec (char *d, unsigned long v)
{
	char t[24]; int k = 0;
	do t[k++] = (char) ('0' + v % 10); while (v /= 10);
	while (k) *d++ = t[--k];
	*d = 0;
	return d;
}
static char *two_digits (char *d, unsigned long v) { *d++ = (char) ('0' + v / 10); *d++ = (char) ('0' + v % 10); *d = 0; return d; }
static unsigned long parse_dec (const char *s)
{
	unsigned long v = 0;
	for (; (unsigned) (*s - '0') <= 9; s++) v = v * 10 + (unsigned) (*s - '0');
	return v;
}

// ===========================================================================
//  Le cadran (clock.asm : couverture calculée par distance au segment / au cercle, en flottants simples)
// ===========================================================================

static float fbits (unsigned u) { float f; memcpy (&f, &u, 4); return f; }
static const float K_PI = fbits (0x40490FDB), K_TWO_PI = fbits (0x40C90FDB), K_SIX_DEG = fbits (0x3DD67750);
// 1/6 1/20 1/42 1/72 1/110 ; 1/2 1/12 1/30 1/56 1/90 1/132
static const float K_SIN[5] = { fbits (0x3E2AAAAB), fbits (0x3D4CCCCD), fbits (0x3CC30C31), fbits (0x3C638E39), fbits (0x3C14F209) };
static const float K_COS[6] = { fbits (0x3F000000), fbits (0x3DAAAAAB), fbits (0x3D088889), fbits (0x3C924925), fbits (0x3C360B61),
				fbits (0x3BF83E10) };

// x, y (repère du cadran), couleur, couverture (bornée à [0,1] ici) : mélange avec la surface
static void blend_px (long x, long y, unsigned c, float cov)
{
	if (!(cov > 0.0f)) return;
	x += g_ox; y += g_oy;
	if ((unsigned long) x >= (unsigned long) ui.w || (unsigned long) y >= (unsigned long) ui.h) return;
	uint32_t *p = ui.surf + y * ui.w + x;
	if (!(cov < 1.0f)) { *p = c; return; }
	int a = (int) (cov * 256.0f);			// alpha sur 0..256
	unsigned bg = *p, out = 0;
	for (int sh = 16; sh >= 0; sh -= 8)
	{
		int f = (int) ((c >> sh) & 0xFF), b = (int) ((bg >> sh) & 0xFF);
		out |= (unsigned) ((((f - b) * a) >> 8) + b) << sh;
	}
	*p = out;
}

// angle (0..2pi) -> sin, cos (séries, |erreur| < 3e-4 ; 0 = midi, sens horaire : sin(a) = -sin(a - pi))
static void sincos_series (float a, float *s, float *c)
{
	float x = a - K_PI, x2 = x * x, one = 1.0f, t;
	t = one - x2 * K_SIN[4];
	t = one - (t * K_SIN[3]) * x2;
	t = one - (t * K_SIN[2]) * x2;
	t = one - (t * K_SIN[1]) * x2;
	t = one - (t * K_SIN[0]) * x2;
	float sn = t * x;
	t = one - x2 * K_COS[5];
	t = one - (t * K_COS[4]) * x2;
	t = one - (t * K_COS[3]) * x2;
	t = one - (t * K_COS[2]) * x2;
	t = one - (t * K_COS[1]) * x2;
	t = one - (t * K_COS[0]) * x2;
	*s = -sn;
	*c = -t;
}

// A (ax, ay), B (bx, by), demi-épaisseur, couleur : couverture = demi-épaisseur + 0,5 - distance au segment
static void aa_segment (float ax, float ay, float bx, float by, float half, unsigned c)
{
	float abx = bx - ax, aby = by - ay, h = half + 0.5f;
	float l2 = fmaf (aby, aby, abx * abx);
	if (!(l2 >= 1.0f)) l2 = 1.0f;
	long x0 = (long) floorf (fminf (ax, bx) - h), x1 = (long) ceilf (fmaxf (ax, bx) + h);
	long y0 = (long) floorf (fminf (ay, by) - h), y1 = (long) ceilf (fmaxf (ay, by) + h);
	for (long y = y0; y <= y1; y++)
		for (long x = x0; x <= x1; x++)
		{
			float px = (float) x - ax, py = (float) y - ay;
			float t = fmaf (py, aby, px * abx) / l2;	// AP.AB / |AB|^2, borné à [0,1]
			t = fminf (fmaxf (t, 0.0f), 1.0f);
			px = fmaf (-t, abx, px);
			py = fmaf (-t, aby, py);
			blend_px (x, y, c, h - sqrtf (fmaf (py, py, px * px)));
		}
}

// angle, rayon intérieur (peut être < 0), rayon extérieur, couleur, demi-épaisseur
static void radial (float a, long rin, long rout, unsigned c, float half)
{
	float s, co;
	sincos_series (a, &s, &co);
	float cx = (float) CX, cy = (float) CY, ri = (float) rin, ro = (float) rout;
	aa_segment (cx + s * ri, cy - co * ri, cx + s * ro, cy - co * ro, half, c);
}

// rayon, couleur : un disque centré sur (CX, CY), bord anti-crénelé
static void disc (long r, unsigned c)
{
	float rr = (float) r + 0.5f;
	for (long y = CY - r - 1; y <= CY + r + 1; y++)
		for (long x = CX - r - 1; x <= CX + r + 1; x++)
		{
			float dx = (float) (x - CX), dy = (float) (y - CY);
			blend_px (x, y, c, rr - sqrtf (fmaf (dy, dy, dx * dx)));
		}
}

// un chiffre du cadran : texte, x, ligne de base
static void numeral (const char *s, long x, long y)
{
	ui_text ((long) s, x + g_ox, y + g_oy, 0x222B36, 22, (long) ui.font_b, 0);
}

// -> secondes de la journée
static unsigned long now_seconds (void) { return (time_ms () / 1000 + (unsigned long) g_offset) % 86400; }

static void btn_rect (int b, long *r)
{
	r[0] = (BTN_W + 12) * b + BTN_X0 + g_ox;
	r[1] = g_oy + BTN_Y;
	r[2] = BTN_W;
	r[3] = BTN_H;
}

static void draw_buttons (void)
{
	for (int b = 0; b < 3; b++)
	{
		long r[4];
		btn_rect (b, r);
		ui_button (r[0], r[1], r[2], r[3], (long) BTN_LABELS[b], 0);
	}
	ui_update (0, g_oy + BTN_Y, ui.w, BTN_H);
}

static void draw_clock (void)
{
	unsigned long s = now_seconds ();
	ui_fill (0, TOP, ui.w, g_oy + BTN_Y - 4 - TOP, tok (TJ_FACE));	// fond + cadran (sous les onglets)
	disc (RAD + 8, 0x303A46);
	disc (RAD + 2, 0xFFFFFF);
	for (int i = 0; i < 60; i++)					// graduations
	{
		float a = (float) i * K_SIX_DEG;
		if (i % 5) radial (a, RAD - 10, RAD - 2, 0x8A96A6, 1.0f);	// minute : fine
		else radial (a, RAD - 22, RAD - 2, 0x222B36, 2.5f);
	}
	numeral ("12", CX - 14, CY - RAD + 52);
	numeral ("3", CX + RAD - 46, CY + 9);
	numeral ("6", CX - 7, CY + RAD - 32);
	numeral ("9", CX - RAD + 32, CY + 9);
	// aiguilles : la fraction de tour, en radians
	radial ((float) (s % 43200) / (float) 43200 * K_TWO_PI, -14, RAD * 52 / 100, 0x222B36, 4.0f);
	radial ((float) (s % 3600) / (float) 3600 * K_TWO_PI, -18, RAD * 78 / 100, 0x222B36, 2.75f);
	radial ((float) (s % 60) / (float) 60 * K_TWO_PI, -26, RAD * 88 / 100, 0xD9534F, 1.0f);
	disc (7, 0xD9534F);						// axe
	disc (3, 0xFFFFFF);
	char t[32], *e = two_digits (t, s / 3600);			// heure numérique
	*e++ = ':';
	e = two_digits (e, s / 60 % 60);
	*e++ = ':';
	two_digits (e, s % 60);
	long w = ui_text_width ((long) t, 30, (long) ui.font_b);
	ui_text ((long) t, (long) ((unsigned long) (ui.w - w) >> 1), g_oy + CY + RAD + 50, tok (TJ_TEXT1), 30, (long) ui.font_b, 0);
	long y = g_oy + BTN_Y + BTN_H + 2;				// (la note : son fond d'abord, jusqu'en bas)
	ui_fill (0, y, ui.w, ui.h - y, tok (TJ_FACE));
	ui_text ((long) TR ("Pas d'horloge matérielle : réglez l'heure avec les boutons."), g_ox + 24, g_oy + BTN_Y + BTN_H + 16,
		 tok (TJ_TEXT2), 11, (long) ui.font, 0);
	ui_update_all ();
}

// ===========================================================================
//  Le décalage de l'heure : /etc/clock.cfg (secondes), korin.clock.offset
// ===========================================================================

static void load_cfg (void)
{
	char b[32];
	int fd = open (P_CFG, O_RDONLY);
	if (fd < 0) return;
	long n = read (fd, b, 31);
	close (fd);
	if (n <= 0) return;
	b[n] = 0;
	g_offset = (long) (parse_dec (b) % 86400);
}

// korin.clock.offset publié : le décalage (comme clock.cfg)
static void offset_from_bus (void)
{
	char b[32];
	long n = bus_get ((long) CLK_TOPIC, (long) b, 31);
	if (n <= 0) return;
	if (n > 31) n = 31;		// (l'asm ne bornait pas : une valeur de plus de 31 octets écrivait plus loin)
	b[n] = 0;
	g_offset = (long) (parse_dec (b) % 86400);
}

// le décalage publié (korin.clock.offset : la barre de menus et clockd le prennent tout de suite, sans relire le
// fichier) ; clock.cfg (/etc : uid 0) est écrit une fois, élevé, à la fermeture (write_cfg, E72)
static bool g_dirty;
static void save_cfg (void)
{
	char b[32];
	*cat_dec (b, (unsigned long) g_offset) = 0;
	if (g_bus) bus_publish ((long) CLK_TOPIC, (long) b, -1, BUS_RETAIN);
	g_dirty = true;
}

static void write_cfg (void)
{
	if (!g_dirty)
		return;
	unsigned me = (unsigned) getuid ();
	if (me && ui_elevate ((long) TR ("L'Horloge veut garder l'heure r\xC3\xA9gl\xC3\xA9" "e pour tous les comptes.")))
		return;					// (refusé : l'heure reste celle du bus jusqu'au redémarrage)
	char b[32];
	char *e = cat_dec (b, (unsigned long) g_offset);
	*e = '\n';
	int fd = open (P_CFG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0)
	{
		if (write (fd, b, (size_t) (e + 1 - b)) < 0) { /* (rien de plus que l'asm) */ }
		close (fd);
	}
	if (me)
		setuid (me);
	g_dirty = false;
}

// ===========================================================================
//  La liste des alarmes
// ===========================================================================

// le rang d'alarme -> clé de tri (l'heure ; invalides à la fin)
static long sort_key (int i) { const Alarm &a = g_al.a[i]; return a.valid ? a.hh * 60 + a.mm : 9999; }

// g_ord <- les rangs, dans l'ordre des heures (insertion : stable)
static void sort_alarms (void)
{
	for (int i = 0; i < g_al.n; i++) g_ord[i] = i;
	for (int i = 1; i < g_al.n; i++)
	{
		int k = g_ord[i], j = i;
		long key = sort_key (k);
		for (; j && sort_key (g_ord[j - 1]) > key; j--) g_ord[j] = g_ord[j - 1];
		g_ord[j] = k;
	}
}

// son nom (« Alarme » sans nom)
static char *put_label (char *d, const Alarm &a) { return cat (d, a.label[0] ? a.label : TR ("Alarme")); }

// « Une fois », « En semaine », ou « Lun, Mer, Ven »
static char *repeat_text (char *d, unsigned days)
{
	days &= AL_EVERYDAY;
	switch (alarm_repeat_kind (days))
	{
	case REP_ONCE: return cat (d, TR ("Une fois"));
	case REP_DAILY: return cat (d, TR ("Tous les jours"));
	case REP_WEEKDAYS: return cat (d, TR ("En semaine"));
	case REP_WEEKENDS: return cat (d, TR ("Le week-end"));
	}
	bool one = false;
	for (int k = 0; k < 7; k++)
		if (days & (1u << k))
		{
			if (one) d = cat (d, ", ");
			d = cat (d, TR (S_DAYS[k]));
			one = true;
		}
	return d;
}

// montrée active : valide, active, pas une alarme d'une fois déjà passée
static bool shown_on (const Alarm &a, long now) { return a.valid && a.on && !alarm_passed (a, now); }

// « 07:00 <tab> Nom <tab> Répétition <tab> État »
static void row_text (char *d, const Alarm &a, long now)
{
	if (a.valid) { d = two_digits (d, (unsigned long) a.hh); *d++ = ':'; d = two_digits (d, (unsigned long) a.mm); }
	else d = cat (d, "--:--");
	d = cat (d, "\t");
	d = cat (put_label (d, a), "\t");
	d = cat (repeat_text (d, a.days), "\t");
	const char *st = !a.valid ? "Invalide" : a.missed >= 0 ? "Manquée"	// (une sonnerie sans réponse : dite)
		       : !shown_on (a, now) ? "Désactivée" : a.snooze > now ? "Reportée" : "Active";
	cat (d, TR (st));
}

static char *put_hm (char *d, long min)
{
	min %= 1440;
	d = two_digits (d, (unsigned long) min / 60);
	*d++ = ':';
	return two_digits (d, (unsigned long) min % 60);
}

// « Prochaine alarme : demain 07:00 — dans 18 h 26 min », « Reportée jusqu'à 07:10 — Nom », « Aucune alarme active »
static void next_text (char *d)
{
	long now = g_minute;
	int i;
	long when = alarms_next (g_al, now, &i);
	if (when < 0) { cat (d, TR ("Aucune alarme active")); return; }
	const Alarm &a = g_al.a[i];
	if (a.snooze == when)
	{
		d = cat (cat (d, TR ("Reportée jusqu'à")), " ");
		d = cat (put_hm (d, when), " \xE2\x80\x94 ");
		put_label (d, a);
		return;
	}
	d = cat (cat (d, TR ("Prochaine alarme :")), " ");
	long k = when / 1440 - now / 1440;	// le jour : aujourd'hui, demain, lundi...
	d = cat (cat (d, TR (k == 0 ? "aujourd'hui" : k == 1 ? "demain" : S_WDAYS[clk_wday (when / 1440)])), " ");
	d = cat (put_hm (d, when), " \xE2\x80\x94 ");
	d = cat (cat (d, TR ("dans")), " ");
	long left = when - now, h = left / 60;	// d'ici là
	if (h) d = cat (cat_dec (d, (unsigned long) h), " h ");
	cat (cat_dec (d, (unsigned long) (left % 60)), " min");
}

// l'alarme choisie (0 : aucune)
static Alarm *sel_alarm (void)
{
	int i = g_sel_id >= 0 ? alarm_find (g_al, g_sel_id) : -1;
	return i >= 0 ? &g_al.a[i] : 0;
}

// Modifier, Activer/Désactiver, Supprimer : avec une alarme choisie ; Ajouter : 20 au plus
static void update_buttons (void)
{
	Alarm *a = sel_alarm ();
	ui_w_enable (g_aw[AW_EDIT], a != 0);
	ui_w_enable (g_aw[AW_TOGGLE], a != 0);
	ui_w_enable (g_aw[AW_DEL], a != 0);
	ui_w_enable (g_aw[AW_ADD], g_al.n < ALARMS_MAX);
	if (a) ui_w_set_text (g_aw[AW_TOGGLE], (long) TR (shown_on (*a, clk_now_minute ()) ? "Désactiver" : "Activer"));
}

// la page Alarmes refaite : la liste, la prochaine alarme, les boutons
static void alarms_refresh (void)
{
	long now = clk_now_minute ();
	g_minute = now;
	if (!g_page) return;			// (l'onglet Horloge : plus tard)
	sort_alarms ();
	long list = g_aw[AW_LIST];
	ui_list_clear (list);
	long row = -1;				// la ligne de l'alarme choisie
	for (int r = 0; r < g_al.n; r++)
	{
		const Alarm &a = g_al.a[g_ord[r]];
		char t[256];
		row_text (t, a, now);
		ui_list_add (list, (long) t, a.id);
		if (a.id == g_sel_id) row = r;
	}
	if (row < 0) g_sel_id = -1;
	else ui_w_set_value (list, row);
	char line[256];
	next_text (line);
	ui_w_set_text (g_aw[AW_NEXT], (long) line);
	update_buttons ();
}

// ===========================================================================
//  Changer les alarmes : le fichier relu, changé, écrit, publié
// ===========================================================================

static void commit (void)
{
	if (alarms_save (g_al))
		ui_message ((long) TR ("Horloge"), (long) TR ("Impossible d'écrire /etc/alarms.txt."), UM_OK);
	if (g_bus) alarms_publish ();
	alarms_refresh ();
}

// l'alarme choisie : activée ou désactivée
static void alarm_toggle (void)
{
	alarms_load (g_al);
	Alarm *a = sel_alarm ();
	if (!a) return;
	long now = clk_now_minute ();
	bool was = shown_on (*a, now);
	a->snooze = -1;
	if (was) a->on = false;
	else
	{
		a->on = true;			// activée : une alarme d'une fois prend sa prochaine heure
		a->missed = -1;
		if (!(a->days & AL_EVERYDAY)) a->date = alarm_once_day (a->hh, a->mm, now);
	}
	commit ();
}

// l'alarme choisie : supprimée (sans question, comme Onyx)
static void alarm_delete (void)
{
	if (g_sel_id < 0) return;
	alarms_load (g_al);
	if (!alarm_remove (g_al, g_sel_id)) return;
	g_sel_id = -1;
	commit ();
}

// ===========================================================================
//  Le dialogue d'une alarme
// ===========================================================================

static int rep_kind (unsigned days)
{
	days &= AL_EVERYDAY;
	int k = 0;
	while (k < 4 && REP_MASKS[k] != days) k++;
	return k;			// 0 une fois .. 3 le week-end, 4 jours choisis
}

static unsigned dlg_days (void)
{
	unsigned d = 0;
	for (int k = 0; k < 7; k++) if (ui_w_get_value (d_days[k])) d |= 1u << k;
	return d;
}

static void dlg_set_days (unsigned d)
{
	for (int k = 0; k < 7; k++) ui_w_set_value (d_days[k], (d >> k) & 1);
}

// (fonction du dialogue) la répétition et les jours se suivent ; Essayer joue un tour du son ; Entrée dans le nom : OK
static long dlg_on (long dlg, long w, long code)
{
	if (w == d_rep)
	{
		if (code != UN_CHANGE) return 0;
		long v = ui_w_get_value (d_rep);
		if ((unsigned long) v < 4) dlg_set_days (REP_MASKS[v]);	// (jours choisis : les cases restent)
		return 0;
	}
	if (code == UN_CHANGE)
	{
		for (int k = 0; k < 7; k++)
			if (d_days[k] == w)
			{
				ui_w_set_value (d_rep, rep_kind (dlg_days ()));
				break;
			}
		return 0;
	}
	if (code != UN_ACTIVATE) return 0;
	if (w == d_name) ui_dialog_close (dlg, 1);
	else if (w == d_test)
	{
		if (!gs_fm) gs_open ();
		unsigned long s = (unsigned long) ui_w_get_value (d_snd);
		gs_seq (SOUNDS[s < ALARM_SOUNDS ? s : 0]);
	}
	return 0;
}

// (minuteur du dialogue) le son d'Essayer avance
static long dlg_tick (long) { gs_tick (); return 0; }

// g_edit dans le fichier relu, puis écrit (id -1 : une nouvelle)
static void edit_commit (int id)
{
	g_edit.snooze = g_edit.missed = g_edit.date = -1;
	if (!(g_edit.days & AL_EVERYDAY))		// une fois : sa prochaine heure
		g_edit.date = alarm_once_day (g_edit.hh, g_edit.mm, clk_now_minute ());
	alarms_load (g_al);
	int i = id >= 0 ? alarm_find (g_al, id) : -1;
	if (i < 0)					// nouvelle : à la fin, un numéro neuf
	{
		if (g_al.n >= ALARMS_MAX) return;
		i = g_al.n++;
		g_edit.id = g_al.next_id++;
	}
	g_al.a[i] = g_edit;
	g_sel_id = g_edit.id;
	commit ();
}

static void dlg_label (long x, long y, long w, long h, const char *s) { ui_label (x, y, w, h, (long) s, 0); }

// id (-1 : une nouvelle) : le dialogue ; OK : écrite
static void alarm_edit (int id)
{
	const char *title;
	if (id >= 0)
	{
		int i = alarm_find (g_al, id);
		if (i < 0) return;
		g_edit = g_al.a[i];
		title = "Modifier l'alarme";
	}
	else
	{
		if (g_al.n >= ALARMS_MAX) return;
		alarm_blank (g_edit);
		g_edit.valid = true;
		title = "Nouvelle alarme";
	}
	long dlg = ui_dialog_begin (DLG_W, DLG_H, (long) TR (title));
	if (!dlg) return;
	dlg_label (M, 50, 90, 20, "Heure");			// l'heure
	d_hh = ui_numeric (110, 44, 110, 30, 0, 23, g_edit.hh);
	ui_label (222, 50, 16, 20, (long) ":", ULB_CENTER);
	d_mm = ui_numeric (240, 44, 110, 30, 0, 59, g_edit.mm);
	dlg_label (M, 90, 90, 20, "Nom");			// le nom
	d_name = ui_textbox (110, 84, DLG_W - 110 - M, 30, 0);
	ui_w_set_range (d_name, 0, 40, 0, 0);			// (40 caractères, comme Onyx)
	ui_w_set_text (d_name, (long) g_edit.label);
	ui_textbox_hint (d_name, (long) TR ("Alarme"));
	dlg_label (M, 130, 90, 20, "Répétition");		// la répétition
	d_rep = ui_dropdown (110, 124, DLG_W - 110 - M, 30, (long) "Une fois|Tous les jours|En semaine|Le week-end|Jours choisis");
	ui_w_set_value (d_rep, rep_kind (g_edit.days));
	for (int k = 0; k < 7; k++)				// les sept jours
		d_days[k] = ui_checkbox (57 * k + M, 164, 56, 30, (long) S_DAYS[k], (g_edit.days >> k) & 1);
	dlg_label (M, 210, 90, 20, "Son");			// le son
	d_snd = ui_dropdown (110, 204, 170, 30, (long) "Carillon|Bips|Marimba");
	ui_w_set_value (d_snd, g_edit.sound);
	d_test = ui_pushbutton (290, 204, DLG_W - 290 - M, 30, (long) "Essayer", 0);
	d_on = ui_switch (110, 248, DLG_W - 110 - M, 30, (long) "Active", g_edit.on);	// active
	long edge = ui_dialog_button (dlg, 0, (long) "OK", 1, UB_DEFAULT, DLG_W - 12);	// OK, Annuler
	ui_dialog_button (dlg, 1, (long) "Annuler", 0, 0, edge);
	ui_dialog_cancel (dlg, 0);
	ui_dialog_timer (dlg, (long) dlg_tick, 20);
	ui_w_focus (d_name);
	long ok = ui_dialog_run (dlg, (long) dlg_on, 0);
	if (ok)						// OK : les valeurs (avant ui_dialog_end)
	{
		unsigned long v = (unsigned long) ui_w_get_value (d_hh);
		g_edit.hh = v <= 23 ? (int) v : 0;
		v = (unsigned long) ui_w_get_value (d_mm);
		g_edit.mm = v <= 59 ? (int) v : 0;
		const char *t = (const char *) ui_w_get_text (d_name);
		if (t) alarm_set_label (g_edit, t);
		else g_edit.label[0] = 0;
		g_edit.days = dlg_days ();
		v = (unsigned long) ui_w_get_value (d_snd);
		g_edit.sound = v < ALARM_SOUNDS ? (int) v : 0;
		g_edit.on = ui_w_get_value (d_on) != 0;
	}
	ui_dialog_end (dlg);
	gs_close ();					// (le son d'Essayer)
	if (ok) edit_commit (id);
}

// ===========================================================================
//  Les onglets
// ===========================================================================

static void page_show (int p)
{
	g_page = p;
	ui_w_set_value (w_seg, p);
	for (int i = 0; i < AW_N; i++) ui_w_show (g_aw[i], p != 0);
	ui_fill (0, TOP, ui.w, ui.h - TOP, tok (TJ_FACE));	// le fond sous les onglets
	if (!p)
	{
		draw_buttons ();
		ui_w_draw (0);
		draw_clock ();
	}
	else
	{
		alarms_refresh ();
		ui_w_draw (0);
		ui_update_all ();
	}
}

// (ui_on_resize) une autre taille : le cadran centré, la page redessinée (les widgets de la page Alarmes ont suivi
// leurs ancres)
static long on_resized (long w, long h)
{
	w -= WIN_W;
	h -= WIN_H;
	g_ox = (w > 0 ? w : 0) >> 1;
	g_oy = (h > 0 ? h : 0) >> 1;
	page_show (g_page);
	return 0;
}

// les widgets : les onglets (à gauche : la droite du haut reste aux boutons du cadre), puis ceux de la page Alarmes
static void make_widgets (void)
{
	w_seg = ui_segmented (12, 6, 0, 0, (long) "Horloge|Alarmes");
	g_aw[AW_NEXT] = ui_label (M, 48, WIN_W - 2 * M, 24, (long) "", ULB_BOLD);	// la prochaine alarme
	long l = g_aw[AW_LIST] = ui_list (M, LIST_Y, WIN_W - 2 * M, LIST_H, UL_HEADER | UL_NOSORT);
	ui_list_column (l, (long) "Heure", 72, 0);
	ui_list_column (l, (long) "Nom", 100, 0);
	ui_list_column (l, (long) "Répétition", 104, 0);
	ui_list_column (l, (long) "État", 92, 0);
	g_aw[AW_ADD] = ui_pushbutton (M, ROW_Y, 84, 32, (long) "Ajouter", 0);
	g_aw[AW_EDIT] = ui_pushbutton (M + 88, ROW_Y, 84, 32, (long) "Modifier", 0);
	g_aw[AW_TOGGLE] = ui_pushbutton (M + 176, ROW_Y, 100, 32, (long) "Désactiver", 0);
	g_aw[AW_DEL] = ui_pushbutton (M + 280, ROW_Y, WIN_W - M - (M + 280), 32, (long) "Supprimer", 0);
	g_aw[AW_NOTE] = ui_label (M, ROW_Y + 40, WIN_W - 2 * M, 40, (long) "Les alarmes sonnent même quand l'Horloge est fermée.",
				  ULB_DIM | ULB_WRAP);			// ce que fait clockd
	for (int i = 0; i < AW_N; i++) ui_anchor (g_aw[i], ANCHORS[i]);
}

// widget, code (ui_w_event)
static void on_widget (long w, long code)
{
	if (w == w_seg)
	{
		if (code != UN_CHANGE) return;
		int p = ui_w_get_value (w) == 1;
		if (p != g_page) page_show (p);
		return;
	}
	if (w == g_aw[AW_LIST])
	{
		long r = ui_w_get_value (w);	// la ligne choisie : son alarme
		long id;
		if (r >= 0 && uikit_list_get (w, r, &id)) g_sel_id = (int) id;
		update_buttons ();
		if (code == UN_ACTIVATE && g_sel_id >= 0) alarm_edit (g_sel_id);	// double-clic : modifier
		return;
	}
	if (code != UN_ACTIVATE) return;
	if (w == g_aw[AW_ADD]) alarm_edit (-1);
	else if (w == g_aw[AW_EDIT]) { if (g_sel_id >= 0) alarm_edit (g_sel_id); }
	else if (w == g_aw[AW_TOGGLE]) alarm_toggle ();
	else if (w == g_aw[AW_DEL]) alarm_delete ();
}

int main ()
{
	if (!uikit_open (&ui)) return 1;
	if (ui_window_ex (-1, -1, WIN_W, WIN_H, (long) TR ("Horloge"), 0) < 0) return 0;
	g_bus = buskit_kit_open () != 0;
	load_cfg ();
	alarms_load (g_al);
	if (g_bus)
	{
		g_sub = bus_subscribe ((long) ALARMS_TOPIC, 0);	// clockd a changé le fichier
		if (g_sub < 0) g_sub = 0;
		g_sub_clk = bus_subscribe ((long) CLK_TOPIC, 0);	// l'heure réglée (NTP, horloge CMOS du PC) : le bus
		if (g_sub_clk < 0) g_sub_clk = 0;
		if (g_sub_clk) offset_from_bus ();		// (déjà publiée, gardée : comme la barre de menus)
	}
	make_widgets ();
	ui_on_resize ((long) on_resized, 0);
	page_show (0);
	unsigned long next = time_ms () / 1000 * 1000 + 1000;	// la prochaine seconde pleine
	for (;;)
	{
		long left = (long) (next - time_ms ());
		if (left <= 0)
		{
			next += 1000;
			if (!g_page) draw_clock ();
			else if (clk_now_minute () != g_minute) alarms_refresh ();	// Alarmes : une fois par minute
			continue;
		}
		long t = ui_event (left);
		if (!t) continue;
		if (t == WEV_CLOSE) { write_cfg (); break; }
		if (t == WEV_THEME) { page_show (g_page); continue; }	// autre thème : la page redessinée
		if (t == WEV_BUS)
		{
			long sub = ui_ev_field (&ui, BEV_SUB);
			if (sub == g_sub_clk) { offset_from_bus (); if (!g_page) draw_clock (); }	// le décalage
			else if (sub == g_sub) { alarms_load (g_al); alarms_refresh (); }	// les alarmes ont changé : relues
			continue;
		}
		if (t == WEV_KEY && g_page && ui_ev_field (&ui, EV_KIND) && ui_ev_field (&ui, EV_CODE) == KEY_DELETE)
		{
			alarm_delete ();				// Alarmes : Suppr supprime
			continue;
		}
		long code = 0, taken = 0;
		long w = uikit_w_event (&code, &taken);
		if (code) { on_widget (w, code); continue; }
		if (taken || g_page || t != WEV_MOUSE || ui_ev_field (&ui, EV_KIND) != 1) continue;
		for (int b = 0; b < 3; b++)					// les boutons de l'heure
		{
			long r[4];
			btn_rect (b, r);
			if (!ui_hit (r[0], r[1], r[2], r[3])) continue;
			long o = (g_offset + ADJUST[b]) % 86400;		// modulo 24 h, positif
			g_offset = o < 0 ? o + 86400 : o;
			save_cfg ();
			draw_clock ();
			break;
		}
	}
	return 0;
}
