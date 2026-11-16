/*
 * applets/padconf.cpp -- le volet Manette du panneau de configuration (le padconf d'Onyx). /etc/gamepad.ini, lu
 * par HIDManager (src/inc/pad.inc, gamepad.h) :
 *   * onglet Clavier : la touche de chaque bouton de la manette 0, le clavier (une ligne touchée, puis la touche) ;
 *     section [keyboard] ; le bouton du bas (« Par défaut ») remet les touches de départ ;
 *   * onglets Manette : ce qu'envoie la manette en direct (WEV_PAD : boutons bruts, axes) et ce que les applications
 *     voient (le dessin) ; Configurer : appuyer sur chaque bouton demandé, Échap : ce bouton n'existe pas, Retour
 *     arrière : abandon ; section [vvvv:pppp] du modèle ; Oublier la retire.
 * Une section écrite, HIDManager relit le fichier (HID_OP_PADMAP).
 *
 * Port en C++ de applets/padconf.inc (Korin), qui fait foi pour ce qui se voit : le padconf d'Onyx (user/Apps/
 * padconf) dessinait avec l'uikit C++ d'Onyx et lisait la manette par kapi ; d'Onyx viennent les étapes de
 * l'assistant (dans l'ordre des bits PAD_*), le modèle « vvvv:pppp » et les noms des boutons du fichier.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../control.h"
#include "../../../inc/gamepad.h"

enum
{
	GP_TOP = AP_HEAD + 42,			/* sous les onglets */
	GP_TAB_W = 112,
	GP_ROW_H = 24,
	GP_COLS = 2,
	GP_ROWS = 9,				/* lignes par colonne (17 boutons) */
	GP_COL_W = 330,
	GP_WORD = 12,				/* un mot du fichier */
	GP_NB = PAD_NBUTTONS,
};

/* un bouton de la manette dessinée : son bit, sa place (dx, dy), sa taille, son texte */
struct GpKey { unsigned char bit, dy, w, h; unsigned short dx; const char *text; };
static const GpKey gp_shape[17] = {
	{ 8, 0, 60, 16, 10, "L" }, { 10, 0, 50, 16, 80, "L2" }, { 11, 0, 50, 16, 170, "R2" }, { 9, 0, 60, 16, 230, "R" },
	{ 0, 40, 22, 22, 44, "" }, { 1, 84, 22, 22, 44, "" }, { 2, 62, 22, 22, 22, "" }, { 3, 62, 22, 22, 66, "" },
	{ 7, 40, 24, 22, 234, "Y" }, { 4, 84, 24, 22, 234, "A" }, { 6, 62, 24, 22, 210, "X" }, { 5, 62, 24, 22, 258, "B" },
	{ 12, 56, 34, 14, 112, "SEL" }, { 13, 56, 34, 14, 154, "STA" }, { 16, 34, 28, 14, 136, "H" },
	{ 14, 104, 34, 18, 104, "L3" }, { 15, 104, 34, 18, 162, "R3" },
};

/* les noms du fichier, dans l'ordre des bits PAD_* */
static const char *const gp_names[GP_NB] = {
	"up", "down", "left", "right", "a", "b", "x", "y", "l", "r", "l2", "r2", "select", "start", "l3", "r3", "home",
};
static const char *const gp_labels[GP_NB] = {
	"Haut", "Bas", "Gauche", "Droite", "A (bas)", "B (droite)", "X (gauche)", "Y (haut)", "L", "R", "L2", "R2",
	"Select", "Start", "L3", "R3", "Home",
};
/* les touches de départ de HIDManager (pad_def_keys) */
static const char *const gp_def[GP_NB] = {
	"up", "down", "left", "right", "a", "b", "x", "y", "l", "r", "none", "none", "backspace", "enter", "none", "none",
	"none",
};
/* les touches nommées : code HID -> mot du fichier (les autres : leur lettre ou chiffre) */
static const unsigned char gp_codes[10] = { 0x52, 0x51, 0x50, 0x4F, 0x28, 0x2A, 0x2C, 0x2B, 0x46, 0x48 };
static const char *const gp_words[10] = { "up", "down", "left", "right", "enter", "backspace", "space", "tab", "prtsc", "pause" };
/* les étapes de l'assistant */
static const char *const gp_steps[GP_NB] = {
	"HAUT de la croix", "BAS de la croix", "GAUCHE de la croix", "DROITE de la croix", "le bouton du BAS (Xbox A)",
	"le bouton de DROITE (Xbox B)", "le bouton de GAUCHE (Xbox X)", "le bouton du HAUT (Xbox Y)",
	"l'\xC3\xA9paule GAUCHE (L)", "l'\xC3\xA9paule DROITE (R)", "la g\xC3\xA2" "chette GAUCHE (L2)",
	"la g\xC3\xA2" "chette DROITE (R2)", "SELECT", "START", "le clic du stick GAUCHE (L3)", "le clic du stick DROIT (R3)",
	"HOME",
};
static const char *const gp_tabs[PAD_MAX] = { "Clavier", "Manette 1", "Manette 2", "Manette 3" };
static const char gp_s_default[] = "Par d\xC3\xA9" "faut";
static const char gp_s_config[] = "Configurer";
static const char gp_s_forget[] = "Oublier";
static const char gp_s_keyboard[] = "[keyboard]";
static const char gp_s_hint[] = "Touchez une ligne, puis la touche de ce bouton.";
static const char gp_s_press[] = "Appuyez sur la touche (\xC3\x89" "chap : annuler).";
static const char gp_s_nopad[] = "Appuyez d'abord sur un bouton de la manette.";
static const char gp_s_none1[] = "Aucune manette vue ici : branchez-la, appuyez sur un bouton.";
static const char gp_s_model[] = "Mod\xC3\xA8le ";
static const char gp_s_push[] = "Appuyez sur ";
static const char gp_s_wizhint[] = "\xC3\x89" "chap : ce bouton n'existe pas. Retour arri\xC3\xA8re : abandon.";
static const char gp_s_ok[] = "Enregistr\xC3\xA9 : les applications l'utilisent.";
static const char gp_s_err[] = "/etc/gamepad.ini non modifi\xC3\xA9";
static const char gp_s_forgotten[] = "Ce mod\xC3\xA8le reprend les boutons par d\xC3\xA9" "faut.";
static const char gp_s_abort[] = "Abandon.";
#define P_GPINI		"/etc/gamepad.ini"

static long gp_tab;				/* onglet : 0 le clavier, 1..3 la manette de ce numéro */
static long gp_sel;				/* (Clavier) la ligne choisie */
static long gp_wait;				/* (Clavier) 1 : elle attend sa touche */
static long gp_step = -1;			/* l'assistant : le bouton demandé (-1 : aucun) */
static uint32_t gp_btn[PAD_MAX], gp_raw[PAD_MAX], gp_id[PAD_MAX];	/* boutons PAD_*, bruts, modèle (vid:pid) */
static uint64_t gp_ax[PAD_MAX];			/* quatre axes de 16 bits */
static char gp_keys[GP_NB][GP_WORD];
static unsigned char gp_map[GP_NB];		/* l'assistant : bouton brut + 1 (0 : absent, ou le chapeau) */
static char gp_head[16];			/* « [vvvv:pppp] » */

/* le bouton du bas selon l'onglet */
static void gp_button_label (void) { ap.button = gp_tab ? gp_s_config : gp_s_default; }

/* ---- le fichier ---- */

/* s = texte, start = début attendu -> 1 s'il commence ainsi */
static int gp_prefix (const char *s, const char *start) { return !strncmp (s, start, strlen (start)); }

/* [p, e[ = une ligne « bouton = touche » de [keyboard] : dans gp_keys */
static void gp_line (const char *p, const char *e)
{
	const char *q = p;			/* le mot : jusqu'au blanc ou au = */
	while (q < e && *q != ' ' && *q != '=')
		q++;
	if (q >= e)				/* (comme l'asm : une ligne sans valeur) */
		return;
	size_t l = (size_t) (q - p);
	if (!l)
		return;
	long b;
	for (b = 0; b < GP_NB; b++)		/* quel bouton ? */
		if (!strncmp (p, gp_names[b], l) && !gp_names[b][l])
			break;
	if (b >= GP_NB)
		return;
	for (;;)				/* la valeur, après le = (un = en fin de ligne : rien, comme l'asm) */
	{
		char c = *q++;
		if (q >= e)
			return;
		if (c == '=')
			break;
	}
	while (*q == ' ')
		q++;
	char *k = gp_keys[b];
	long n = 0;
	while (q < e && (unsigned char) *q > ' ' && n < GP_WORD - 1)
		k[n++] = *q++;
	k[n] = 0;
}

/* gp_keys : les touches de la section [keyboard], les défauts de HIDManager sinon */
static void gp_read (void)
{
	for (long i = 0; i < GP_NB; i++)
		strcpy (gp_keys[i], gp_def[i]);
	int fd = open (P_GPINI, O_RDONLY);
	if (fd < 0)
		return;
	long n = read (fd, ap_file, AP_FILE_MAX - 1);
	close (fd);
	if (n <= 0)
		return;
	ap_file[n] = 0;
	int in_kb = 0;				/* dans [keyboard] */
	const char *p = ap_file;
	while (*p)
	{
		const char *e = p;		/* fin de ligne */
		while (*e && *e != '\n')
			e++;
		if (*p == '[')
			in_kb = gp_prefix (p, gp_s_keyboard);
		else if (in_kb)
			gp_line (p, e);
		if (!*e)
			break;
		p = e + 1;
	}
}

/* head = en-tête « [xxx] », body = corps (vide : la section part) : la section remplacée dans /etc/gamepad.ini,
 * à la fin ; HIDManager la relit -> < 0 si erreur */
static long gp_section (const char *head, const char *body)
{
	long n = 0;				/* octets du fichier */
	int fd = open (P_GPINI, O_RDONLY);
	if (fd >= 0)
	{
		n = read (fd, ap_file, AP_FILE_MAX - 1);
		if (n < 0)
			n = 0;
		close (fd);
	}
	ap_file[n] = 0;
	const char *p = ap_file;
	char *o = ap_out;			/* sortie */
	int skip = 0;				/* 1 : dans la section remplacée */
	while (*p)
	{
		if (*p == '[')
			skip = gp_prefix (p, head);
		const char *e = p;		/* fin de ligne (saut compris) */
		while (*e && *e++ != '\n')
			;
		if (skip)
			p = e;
		else
			while (p < e)
				*o++ = *p++;
	}
	if (*body)				/* la nouvelle section, à la fin */
	{
		o = stpcpy (o, head);
		*o++ = '\n';
		o = stpcpy (o, body);
	}
	*o = 0;
	long r = file_write (P_GPINI, ap_out);
	long ch = svc_open ("hid", 500);	/* HIDManager relit */
	if (ch >= 0)
	{
		long msg[64] = { HID_OP_PADMAP };
		svc_call (ch, msg);
	}
	return r;
}

/* r = résultat d'écriture : le message */
static void gp_msg_written (long r) { ap.msg = r < 0 ? gp_s_err : gp_s_ok; }

/* [keyboard] depuis gp_keys */
static void gp_save_keyboard (void)
{
	char body[512], *o = body;
	for (long i = 0; i < GP_NB; i++)
	{
		o = stpcpy (stpcpy (stpcpy (o, gp_names[i]), " = "), gp_keys[i]);
		*o++ = '\n';
	}
	*o = 0;
	gp_msg_written (gp_section (gp_s_keyboard, body));
}

/* « [vvvv:pppp] » du modèle de l'onglet -> dans gp_head */
static void gp_model_head (void)
{
	uint32_t id = gp_id[gp_tab];
	char *o = gp_head;
	*o++ = '[';
	for (int s = 28; s >= 0; s -= 4)
	{
		if (s == 12)
			*o++ = ':';
		unsigned d = id >> s & 15;
		*o++ = (char) (d < 10 ? '0' + d : 'a' + d - 10);
	}
	*o++ = ']';
	*o = 0;
}

/* l'assistant fini : [vvvv:pppp], un numéro de bouton brut par bouton */
static void gp_save_model (void)
{
	char body[512], *o = body;
	*o = 0;
	for (long i = 0; i < GP_NB; i++)
	{
		if (!gp_map[i])			/* (absent, ou le chapeau) */
			continue;
		o = stpcpy (stpcpy (o, gp_names[i]), " = ");
		long v = gp_map[i];
		char t[4];
		int k = 0;
		do
			t[k++] = (char) ('0' + v % 10);
		while (v /= 10);
		while (k)
			*o++ = t[--k];
		*o++ = '\n';
		*o = 0;
	}
	gp_model_head ();
	gp_msg_written (gp_section (gp_head, body));
}

static void gp_forget (void)
{
	if (!gp_id[gp_tab])
		return;
	gp_model_head ();
	gp_section (gp_head, "");
	ap.msg = gp_s_forgotten;
}

/* Appliquer : Clavier, les touches de départ ; Manette, l'assistant */
static void gp_apply (void)
{
	if (!gp_tab)
	{
		gp_section (gp_s_keyboard, "");	/* la section part : les touches de départ */
		gp_read ();
		return;
	}
	if (!gp_id[gp_tab])
	{
		ap.msg = gp_s_nopad;
		return;
	}
	gp_step = 0;
	memset (gp_map, 0, sizeof gp_map);
	ap.msg = 0;
}

/* ---- les évènements ---- */

/* l'assistant passe au bouton suivant ; après le dernier, le fichier */
static void gp_next (void)
{
	if (++gp_step < GP_NB)
		return;
	gp_step = -1;
	gp_save_model ();
}

/* (ap.pad) WEV_PAD (ui.ev : +4 manette, +8 boutons, +12 bruts, +16 axes, +24 modèle) -> 1 : redessiner */
static int gp_pad (void)
{
	uint32_t pad, btn, raw, id;
	uint64_t axes;
	memcpy (&pad, ui.ev + 4, 4);
	if (pad >= PAD_MAX)
		return 0;
	memcpy (&btn, ui.ev + WEV_PAD_BUTTONS, 4);
	memcpy (&raw, ui.ev + WEV_PAD_RAW_B, 4);
	memcpy (&axes, ui.ev + WEV_PAD_AXES, 8);
	memcpy (&id, ui.ev + 24, 4);
	/* (l'asm a un essai sans manette, GP_TEST, que rien ne définit : pas repris) */
	uint32_t oldraw = gp_raw[pad], oldbtn = gp_btn[pad];
	gp_raw[pad] = raw;
	gp_btn[pad] = btn;
	gp_ax[pad] = axes;
	gp_id[pad] = id;
	if (gp_step < 0 || gp_tab != (long) pad)	/* l'assistant */
		return 1;
	long got;
	uint32_t bit = 1u << gp_step;
	if (gp_step < 4 && (btn & bit) && !(oldbtn & bit))	/* (la croix : le chapeau suffit) */
		got = 0;			/* ce bouton est le chapeau */
	else
	{
		uint32_t pressed = raw & ~oldraw & 0x0FFFFFFF;	/* un bouton vient d'être pressé */
		if (!pressed)
			return 1;
		got = __builtin_ctz (pressed) + 1;
	}
	gp_map[gp_step] = (unsigned char) got;
	gp_next ();
	return 1;
}

/* code = code HID -> le mot du fichier, 0 : aucun */
static const char *gp_word (long code)
{
	static char one[2];
	for (int i = 0; i < 10; i++)
		if (gp_codes[i] == code)
			return gp_words[i];
	unsigned c = (unsigned) ui_ev_field (&ui, EV_CHAR);
	unsigned l = c | 0x20;
	if (l - 'a' <= 25)
		c = l;				/* une lettre (en minuscule) */
	else if (c - '0' > 9)
		return 0;			/* ni lettre ni chiffre */
	one[0] = (char) c;
	one[1] = 0;
	return one;
}

/* une touche ne fait qu'un bouton : celle de gp_sel quitte les autres lignes */
static void gp_unique (void)
{
	for (long i = 0; i < GP_NB; i++)
		if (i != gp_sel && !strcmp (gp_keys[i], gp_keys[gp_sel]))
			strcpy (gp_keys[i], "none");
}

/* (ap.key) -> 1 : pris (le volet garde presque tout : une touche d'essai ne doit pas changer de volet) */
static int gp_key (void)
{
	long code = ui_ev_field (&ui, EV_CODE);
	if (gp_step >= 0)			/* l'assistant */
	{
		if (code == KEY_ESC)
			gp_next ();		/* ce bouton n'existe pas */
		else if (code == KEY_BACKSPACE)	/* abandon */
		{
			gp_step = -1;
			ap.msg = gp_s_abort;
		}
		return 1;
	}
	if (!gp_wait)
		return code == KEY_ESC ? 0 : 1;	/* Échap reste au panneau */
	gp_wait = 0;
	if (code == KEY_ESC)
		return 1;
	const char *w = gp_word (code);		/* la touche de la ligne choisie */
	if (!w)
		return 1;
	strncpy (gp_keys[gp_sel], w, GP_WORD - 1);
	gp_keys[gp_sel][GP_WORD - 1] = 0;
	gp_unique ();
	gp_save_keyboard ();
	return 1;
}

/* (ap.click) -> 1 : redessiner */
static int gp_click (long x, long y)
{
	long dy = y - (AP_HEAD + 2);		/* les onglets */
	if (dy < 0)
		return 0;
	if (dy < 30)
	{
		long dx = x - (SIDE_W + AP_MARGIN);
		if (dx < 0 || dx / GP_TAB_W >= PAD_MAX)
			return 0;
		gp_tab = dx / GP_TAB_W;
		gp_wait = 0;
		gp_step = -1;
		ap.msg = 0;
		gp_button_label ();
		return 1;
	}
	if (!gp_tab)				/* Clavier : une ligne */
	{
		long dx = x - (SIDE_W + AP_MARGIN), ry = y - GP_TOP;
		if (dx < 0 || dx / GP_COL_W >= GP_COLS || ry < 0 || ry / GP_ROW_H >= GP_ROWS)
			return 0;
		long i = dx / GP_COL_W * GP_ROWS + ry / GP_ROW_H;
		if (i >= GP_NB)
			return 0;
		gp_sel = i;
		gp_wait = 1;
		return 1;
	}
	long bx = ui.w - (AP_MARGIN + 2 * BTN_W + 8);	/* Oublier */
	if (y >= ui.h - (BTN_H + 12) && x >= bx && x < bx + BTN_W)
	{
		gp_forget ();
		draw ();			/* (le message reste) */
	}
	return 0;
}

/* ---- le dessin ---- */

/* text, x, y, w, h = rectangle, lit = 1 allumé : un capuchon */
static void gp_cap (const char *text, long x, long y, long w, long h, int lit)
{
	uint32_t c = tok (lit ? TJ_ACCENT_DARK : TJ_LINE_STRONG);
	ui_rbox (x, y, w, h, 4, c, c);
	if (lit)
		ui_rbox (x + 1, y + 1, w - 2, h - 2, 3, tok (TJ_ACCENT), tok (TJ_ACCENT));
	else
		ui_rbox (x + 1, y + 1, w - 2, h - 2, 3, tok (TJ_RAISED), tok (TJ_RAISED_LOW));
	if (!*text)
		return;
	long tw = ui_text_width ((long) text, 12, F ());
	ui_text ((long) text, x + ((w - tw) >> 1), y + (long) ((unsigned long) h >> 1) + 4, lit ? 0x00FFFFFF : tok (TJ_TEXT1),
		 12, F (), 0);			/* (blanc en dur, comme l'asm) */
}

/* l'onglet Clavier : un bouton par ligne, sa touche, allumée tenue */
static void gp_draw_keyboard (void)
{
	for (long i = 0; i < GP_NB; i++)
	{
		long x = i / GP_ROWS * GP_COL_W + SIDE_W + AP_MARGIN, y = i % GP_ROWS * GP_ROW_H + GP_TOP;
		int waiting = gp_sel == i && gp_wait;	/* la ligne qui attend sa touche */
		if (waiting)
			ui_rbox (x - 4, y, GP_COL_W - 14, GP_ROW_H - 2, 5, tok (TJ_ACCENT_LIGHT), tok (TJ_ACCENT_LIGHT));
		ui_text ((long) TR (gp_labels[i]), x + 4, y + 16, tok (TJ_TEXT1), 13, F (), 0);
		gp_cap (waiting ? "?" : gp_keys[i], x + 150, y + 1, 110, GP_ROW_H - 4, gp_btn[0] >> i & 1);	/* le capuchon */
	}
	ui_text ((long) TR (gp_wait ? gp_s_press : gp_s_hint), SIDE_W + AP_MARGIN, GP_TOP + GP_ROWS * GP_ROW_H + 24,
		 tok (TJ_TEXT2), 12, F (), 0);
}

/* b = boutons PAD_*, x, y = coin (300 x 150) : la manette vue du dessus */
static void gp_draw_pad (uint32_t b, long x, long y)
{
	ui_rbox (x, y + 20, 300, 130, 14, tok (TJ_RAISED), tok (TJ_SUNKEN));
	for (const GpKey &k : gp_shape)
		gp_cap (k.text, x + k.dx, y + k.dy, k.w, k.h, b >> k.bit & 1);
}

/* l'onglet d'une manette */
static void gp_draw_gamepad (void)
{
	long x = SIDE_W + AP_MARGIN;
	if (!gp_id[gp_tab])
	{
		ui_text ((long) TR (gp_s_none1), x, GP_TOP + 20, tok (TJ_TEXT2), 13, F (), 0);
		return;
	}
	char t[96];				/* le modèle */
	gp_model_head ();
	t[0] = 0;
	strcpy (tr_cat (t, gp_s_model), gp_head);
	ui_text ((long) t, x, GP_TOP + 14, tok (TJ_TEXT2), 12, F (), 0);
	uint32_t raw = gp_raw[gp_tab];		/* boutons bruts, 1 à 16 */
	for (long i = 0; i < 16; i++)
	{
		char n[4];
		snprintf (n, sizeof n, "%ld", i + 1);
		gp_cap (n, x + 31 * i, GP_TOP + 24, 28, 20, raw >> i & 1);
	}
	uint64_t ax = gp_ax[gp_tab];		/* quatre axes, -1000..1000 */
	for (long i = 0; i < 4; i++)
	{
		long v = (int16_t) (ax >> (16 * i));
		ui_rbox (x + 124 * i, GP_TOP + 54, 112, 14, 4, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
		long fill = (v + 1000) * 112 / 2000;	/* 0..2000 -> 0..112 */
		if (fill >= 4)
			ui_rbox (x + 124 * i, GP_TOP + 54, fill, 14, 4, tok (TJ_ACCENT), tok (TJ_ACCENT));
	}
	gp_draw_pad (gp_btn[gp_tab], x + 150, GP_TOP + 84);	/* ce que voient les applications */
	if (gp_step >= 0)			/* l'assistant */
	{
		strcpy (t, "");
		tr_cat (t, gp_s_push);
		tr_cat (t, gp_steps[gp_step]);
		ui_text ((long) t, x, GP_TOP + 262, tok (TJ_ACCENT), 14, FB (), 0);
		ui_text ((long) TR (gp_s_wizhint), x, GP_TOP + 282, tok (TJ_TEXT2), 12, F (), 0);
	}
	ap_button (ui.w - (AP_MARGIN + 2 * BTN_W + 8), ui.h - (BTN_H + 12), gp_s_forget, 0);	/* Oublier, à gauche */
}							/* d'Appliquer */

static void gp_draw (void)
{
	for (long i = 0; i < PAD_MAX; i++)	/* les onglets */
	{
		long x = i * GP_TAB_W + SIDE_W + AP_MARGIN;
		if (gp_tab == i)
			ui_rbox (x, AP_HEAD + 2, GP_TAB_W - 6, 28, 6, tok (TJ_ACCENT_LIGHT), tok (TJ_ACCENT_LIGHT));
		ui_text ((long) TR (gp_tabs[i]), x + 10, AP_HEAD + 21, tok (TJ_TEXT1), 13, F (), 0);
	}
	if (gp_tab)
		gp_draw_gamepad ();
	else
		gp_draw_keyboard ();
}

/* ---- le volet ---- */

static void gp_open (void)
{
	ap.draw = gp_draw;
	ap.click = gp_click;
	ap.key = gp_key;
	ap.pad = gp_pad;
	gp_tab = 0;
	gp_wait = 0;
	gp_step = -1;
	gp_read ();
	gp_button_label ();
}

const Applet padconf_applet = { "padconf", "Manette", "Manette", "padconf", gp_open, gp_apply };
