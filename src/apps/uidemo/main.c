/*
 * uidemo -- demonstration et essai des widgets de UIKit (docs/KITS.md, docs/UIKIT-MANQUES.md) : chaque etape
 * du chantier y ajoute une page.
 *
 * A gauche, la liste des pages (une ui_list) ; a droite, les widgets de la page choisie (les autres sont
 * caches : ui_w_show). Ligne d'etat en bas : le dernier evenement rendu par ui_w_event.
 *
 * Port en C d'uidemo.asm (memes pages, memes widgets, memes textes), un seul source pour le Pi et le PC ;
 * l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <stdio.h>
#include <string.h>
#include <uikit.h>

#define WIN_W	780
#define WIN_H	540
#define M	16			/* marge */
#define NAV_W	150
#define CX	(M + NAV_W + M)		/* x du contenu */
#define CW	(WIN_W - CX - M)	/* sa largeur */
#define ST_H	34			/* ligne d'etat */
#define MAXW	128			/* widgets enregistres */
#define TB_W	320
#define NDLG	6
#define NTOOLS	9
#define VROWS	1000
#define TODAY	((2026 << 16) | (10 << 8) | 29)

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static const unsigned *jetons;
static unsigned tok (int off) { return jetons[off / 4]; }

static long page, cur_w = WIN_W, cur_h = WIN_H, grid, ndocs = 3, vdesc;
static long w_nav, w_list, w_sb, w_tb[4], w_area, w_slider, w_prog, w_vu, w_lcd, w_dlg[NDLG];
static long w_left, w_split, w_right, w_drop, w_grid, w_canvas, w_cdlg, w_tree, w_lang;
static long w_tbar, w_seg, w_tabs, w_strip, w_tools[16];
static struct { long w, page; } wtab[MAXW];
static long nw;
static char numbuf[512], answer[256], dropbuf[260], line[256], vbuf[64];

/* ---- textes ---- */

static const char *const pages[] = { "Liste", "Texte", "Dialogues", "Choix", "Multiligne", "Valeurs", "Volets",
	"Onglets", "Interaction", "Dates, arbre", "Grille, canevas", 0 };
static const char *const items[] = {
	"kernel8.img\t412 288\tNoyau", "uikit.so\t61 440\tKit", "appkit.so\t18 432\tKit", "netkit.so\t9 216\tKit",
	"elegant\t46 200\tProgramme", "dock\t34 376\tProgramme", "menubar\t28 104\tProgramme", "theme.txt\t1 204\tTexte",
	"system.ini\t312\tTexte", "DejaVuSans.ttf\t757 076\tPolice", "DejaVuSans-Bold.ttf\t705 684\tPolice",
	"DejaVuSansMono.ttf\t340 712\tPolice", "readme.txt\t58\tTexte", "paint.bmp\t1 440 054\tImage",
	"Un nom de fichier vraiment tr\xC3\xA8s long pour voir l'ellipse.txt\t7\tTexte", "music\t0\tDossier",
	"pictures\t0\tDossier", "documents\t0\tDossier", "video\t0\tDossier", "fasmarm\t262 144\tProgramme",
	"selfhost\t12 288\tProgramme", "hello.asm\t1 024\tSource", "clock.cfg\t64\tTexte", 0 };
static const char *const s_side[] = { "#Acc\xC3\xA8s rapides", "Documents", "Images", "Musique", "#Volumes", "SD0:",
	"RAM0:", "IMG0:", 0 };
static const char *const lay_labels[] = { "Un", "Deux", "Trois", "Quatre" };
static const char *const s_ctx[] = { "Copier", "Exporter", "-", "Propri\xC3\xA9t\xC3\xA9s", 0 };
static const char *const s_fruits[] = { "Pomme", "Poire", "Cerise", "Abricot", "Prune", "Figue", 0 };
static const char *const font_names[] = { "Italique (DejaVu Sans Oblique)", "Gras italique", "Mono gras : mov x0, #42",
	"Serif : Portez ce vieux whisky au juge blond", "Serif gras", "Serif italique", "Serif gras italique" };
static const char *const radio_labels[] = { "Petite", "Moyenne", "Grande" };
static const char *const dlg_labels[] = { "Message", "Oui / Non / Annuler", "Saisie", "Ouvrir un fichier",
	"Enregistrer sous", "Choisir un dossier" };
static const unsigned char tb_flags[] = { 0, UTF_PASSWORD, UTF_READONLY, UTF_MONO };
static const char *const tb_labels[] = { "Nom", "Mot de passe", "Lecture seule", "Mono" };
static const char *const tb_hints[] = { "Votre nom", "Au moins 8 caract\xC3\xA8res", "", "" };
/* la barre d'outils : glyphe, libelle, drapeaux */
static const struct { const char *glyph, *label; long flags; } tools[NTOOLS] = {
	{ "\xE2\x86\x90", 0, 0 },			/* « ← » */
	{ "\xE2\x86\x92", 0, 0 },			/* « → » */
	{ "\xE2\x86\xBB", 0, 0 },			/* « ↻ » */
	{ "\xE2\x8C\x82", 0, 0 },			/* « ⌂ » */
	{ "\xE2\x9C\x82", "Couper", UTB_SEP },		/* « ✂ » */
	{ "\xE2\x97\xAB", "Copier", 0 },		/* « ◫ » */
	{ 0, "Gras", UTB_SEP | UTB_TOGGLE },
	{ "\xE2\x9A\x99", 0, UTB_SEP },			/* « ⚙ » */
	{ "\xE2\x96\xB6", "Lire", 0 },			/* « ▶ » */
};
static const int star[20] = { 110 * 16, 10 * 16, 118 * 16, 32 * 16, 140 * 16, 32 * 16, 122 * 16, 46 * 16, 130 * 16, 68 * 16,
	110 * 16, 54 * 16, 90 * 16, 68 * 16, 98 * 16, 46 * 16, 80 * 16, 32 * 16, 102 * 16, 32 * 16 };

static const char s_area[] =
	"; hello.asm - le premier programme de Korin OS\n"
	"include '../../inc/armpi.inc'\n"
	"app_header\n\n"
	"segment readable executable\n"
	"start:\n"
	"\tadr\tx0,msg\t\t\t; le message\n"
	"\tsys\tSYS_PRINT\n"
	"\tmov\tx0,0\n"
	"\tsys\tSYS_EXIT\n\n"
	"segment readable writeable\n"
	"msg\tdb 'Bonjour, Korin OS !',10,0\n\n"
	"; Essayez : clic, glisser, Maj + fl\xC3\xA8" "ches, Ctrl+A / C / X / V,\n"
	"; Tab (quatre espaces), Entr\xC3\xA9" "e, la molette et la barre de d\xC3\xA9" "filement.\n"
	"; Une ligne tr\xC3\xA8s longue fait d\xC3\xA9" "filer le texte horizontalement quand le curseur va au-del\xC3\xA0 du bord droit de la zone.\n"
	"\nligne 18\nligne 19\nligne 20\nligne 21\nligne 22\nligne 23\nligne 24\nligne 25\nligne 26\nligne 27\nligne 28\nligne 29\nligne 30\nfin";
static const char s_split[] = "Glissez la poign\xC3\xA9" "e entre les deux volets.\n\nLa fen\xC3\xAA" "tre se redimensionne par son bord "
	"droit ou bas, ou par le bouton agrandir : les volets, la liste des pages et le texte multiligne suivent (ui_on_resize).";
static const char s_dlg_help[] = "Chaque bouton ouvre un dialogue modal : une seconde fenetre du programme, centr\xC3\xA9" "e. "
	"\xC3\x89" "chap annule, Entr\xC3\xA9" "e choisit le bouton principal. La r\xC3\xA9" "ponse s'affiche en bas.";
static const char s_dlg_r[] = "R\xC3\xA9ponse : ";
static const char s_sel[] = "Choisi : ";
static const char s_act[] = "Activ\xC3\xA9 : ";
static const char s_val[] = "Valeur : ";
static const char s_colordlg[] = "Couleur\xE2\x80\xA6";

/* ---- ligne d'etat ---- */

static void face_fill (long x, long y, long w, long h) { ui_fill (x, y, w, h, tok (TJ_FACE)); }

/* un texte : ligne d'etat en bas de la fenetre */
static void status (const char *s)
{
	s = TR (s);
	face_fill (0, cur_h - ST_H, cur_w, ST_H);
	ui_fill (0, cur_h - ST_H, cur_w, 1, tok (TJ_LINE));
	ui_text_fit ((long) s, M, cur_h - 12, tok (TJ_TEXT2), 13, (long) ui.font, cur_w - 2 * M);
	ui_update (0, cur_h - ST_H, cur_w, ST_H);
}

/* « prefixe texte » (tabulations -> « | ») dans la ligne d'etat */
static void status2 (const char *prefix, const char *s)
{
	prefix = TR (prefix);
	size_t n = strlen (prefix);
	memcpy (line, prefix, n);
	char *d = line + n;
	int i = 0;
	if (s)
		for (; s[i] && i < 200; i++)
			d[i] = s[i] == 9 ? '|' : s[i];
	d[i] = 0;
	status (line);
}

static void heading (const char *s, long x, long y) { ui_text ((long) TR (s), x, y, tok (TJ_TEXT2), 12, (long) ui.font_b, 0); }
static void draw_label (const char *s, long x, long y) { ui_text ((long) TR (s), x, y, tok (TJ_TEXT1), 13, (long) ui.font, 0); }

/* le widget de cette page */
static long reg (long w, long pg)
{
	if (nw < MAXW)
	{
		wtab[nw].w = w;
		wtab[nw].page = pg;
		nw++;
	}
	return w;
}

static void add_all (long list, const char *const *s, int tr)
{
	for (; *s; s++)
		ui_list_add (list, (long) (tr ? TR (*s) : *s), 0);
}

/* ---- rappels ---- */

/* fonction de la barre (ui_w_on) */
static long on_scroll (long w, long code)
{
	(void) code;
	snprintf (line, sizeof line, "%s%ld", TR ("Barre de d\xC3\xA9" "filement : "), ui_w_get_value (w));
	status (line);
	return 0;
}

/* fonction de la grille virtuelle : ligne -> texte */
static long grid_row (long list, long row)
{
	(void) list;
	long r = vdesc ? VROWS - 1 - row : row;
	snprintf (vbuf, sizeof vbuf, "%s%ld\t%ld\t%ld", TR ("Ligne "), r + 1, (r + 1) * (r + 1), (r + 1) % 7);
	return (long) vbuf;
}

/* ---- pages ---- */

static void new_doc (long n)
{
	snprintf (numbuf, sizeof numbuf, "%s%ld", TR ("Document "), n);
	ui_tabstrip_add (w_strip, (long) numbuf, 0);
	ui_w_draw (w_strip);
}

/* page 0 : liste et defilement */
static void build_list (void)
{
	w_list = reg (ui_list (CX, M + 24, 440, 300, UL_HEADER), 0);
	ui_list_column (w_list, (long) TR ("Nom"), 200, 0);
	ui_list_column (w_list, (long) TR ("Taille"), 100, ULC_RIGHT);
	ui_list_column (w_list, (long) TR ("Type"), 140, 0);
	add_all (w_list, items, 1);
	ui_w_set_value (w_list, 0);
	ui_w_focus (w_list);
	w_sb = reg (ui_scrollbar (CX + 440 + 24, M + 24, 12, 300, 0), 0);	/* barre de defilement seule */
	ui_w_set_range (w_sb, 0, 100, 20, 1);
	ui_w_on (w_sb, (long) on_scroll, 0);
}

/* page 1 : champs de texte */
static void build_text (void)
{
	for (int i = 0; i < 4; i++)
	{
		w_tb[i] = reg (ui_textbox (CX + 140, M + 24 + 52 * i, TB_W, 30, tb_flags[i]), 1);
		ui_textbox_hint (w_tb[i], (long) TR (tb_hints[i]));
	}
	ui_w_set_text (w_tb[2], (long) TR ("Lecture seule : s\xC3\xA9lectionnez et copiez (Ctrl+C)."));
	ui_w_set_text (w_tb[3], (long) TR ("mov x0,#42 ; police mono"));
}

/* page 2 : dialogues */
static void build_dialogs (void)
{
	for (int i = 0; i < NDLG; i++)
		w_dlg[i] = reg (ui_pushbutton (CX, M + 24 + 44 * i, 240, 0, (long) dlg_labels[i], i == 0), 2);	/* (le premier : principal) */
	reg (ui_label (CX + 270, M + 24, CW - 270, 200, (long) s_dlg_help, ULB_WRAP | ULB_DIM), 2);	/* explication */
}

/* le numero du bouton : ouvre son dialogue, la reponse dans la ligne d'etat */
static void run_dialog (int i)
{
	long r;
	if (i < 2)
		r = ui_message ((long) "UIKit", (long) TR (i == 0 ? "Ceci est un message de UIKit. Le texte est coup\xC3\xA9 en lignes "
			"aux espaces ; un saut de ligne\ncommence une nouvelle ligne." : "Enregistrer les modifications avant de fermer ?"),
			i == 0 ? UM_OK : UM_YESNOCANCEL);
	else
	{
		if (i == 2)
		{
			strcpy (answer, "pocketterm");
			r = ui_ask ((long) "UIKit", (long) TR ("Comment s'appelle votre machine ?"), (long) answer, 200);
		}
		else if (i == 3)
			r = ui_file_open ((long) "SD0:/", (long) answer, 256);
		else if (i == 4)
			r = ui_file_save ((long) "SD0:/home/documents/", (long) TR ("nouveau.txt"), (long) answer, 256);
		else
			r = ui_folder_choose ((long) "SD0:/", (long) answer, 256);
		if (r)
		{
			status2 (s_dlg_r, answer);
			return;
		}
	}
	snprintf (numbuf, sizeof numbuf, "%ld", r);	/* reponse : un nombre */
	status2 (s_dlg_r, numbuf);
}

/* page 3 : choix */
static void build_choice (void)
{
	reg (ui_checkbox (CX, M + 24, 0, 0, (long) TR ("Afficher les fichiers cach\xC3\xA9s"), 1), 3);
	reg (ui_checkbox (CX, M + 24 + 36, 0, 0, (long) TR ("Confirmer avant de supprimer"), 0), 3);
	ui_w_enable (reg (ui_checkbox (CX, M + 24 + 72, 0, 0, (long) TR ("Option gris\xC3\xA9" "e"), 1), 3), 0);	/* (grisee) */
	for (int i = 0; i < 3; i++)
	{					/* radios du groupe 1 */
		long w = reg (ui_radio (CX, M + 24 + 132 + 36 * i, 0, 0, (long) TR (radio_labels[i]), 1), 3);
		if (i == 1)
			ui_w_set_value (w, 1);
	}
	reg (ui_switch (CX + 280, M + 24, 0, 0, (long) "Wi-Fi", 1), 3);	/* interrupteurs */
	reg (ui_switch (CX + 280, M + 24 + 36, 0, 0, (long) "Bluetooth", 0), 3);
	reg (ui_dropdown (CX + 280, M + 24 + 132, 220, 0, (long) "Givre|Onyx Noir"), 3);	/* liste deroulante */
	long cb = reg (ui_combobox (CX + 280, M + 24 + 132 + 72, 220, 0,
				    (long) "Paris|Lyon|Marseille|Bruxelles|Gen\xC3\xA8ve|Montr\xC3\xA9" "al"), 3);	/* liste modifiable */
	ui_w_set_text (cb, (long) "Lausanne");
}

/* page 4 : texte multiligne */
static void build_area (void)
{
	w_area = reg (ui_textarea (CX, M + 24, CW, WIN_H - ST_H - M - 24 - M,
				   UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL | UTA_CODE), 4);
	ui_w_set_text (w_area, (long) TR (s_area));
}

/* page 5 : valeurs */
static void build_values (void)
{
	w_slider = reg (ui_slider (CX, M + 24, 300, 28, 0, 100, 40), 5);	/* glissiere horizontale */
	w_prog = reg (ui_progress (CX, M + 24 + 48, 300, 10, 100, 40), 5);	/* barre de progression */
	w_vu = reg (ui_vumeter (CX, M + 24 + 80, 300, 14, 100), 5);		/* vu-metre */
	w_lcd = reg (ui_lcd (CX, M + 24 + 120, 200, 56, (long) "12:40"), 5);	/* afficheur */
	reg (ui_numeric (CX, M + 24 + 200, 160, 0, 0, 999, 42), 5);		/* nombre +/- */
	reg (ui_slider (CX + 340, M + 24, 28, 200, 0, 10, 7), 5);		/* glissiere verticale */
	reg (ui_knob (CX + 400, M + 24, 120, 120, 0, 100, 70), 5);		/* molette */
}

/* place les widgets qui suivent la taille de la fenetre (cur_w, cur_h) */
static void layout (void)
{
	ui_w_move (w_nav, M, M, NAV_W, cur_h - (ST_H + 2 * M));		/* liste des pages : toute la hauteur */
	ui_w_move (w_area, CX, M + 24, cur_w - (CX + M), cur_h - (ST_H + M + 24 + M));	/* texte multiligne : tout le contenu */
	ui_w_set_range (w_split, CX + 120, cur_w - (M + 160), 0, 0);	/* volets : (bornes du separateur) */
	long x = ui_w_get_value (w_split);
	long h = cur_h - (ST_H + M + 24 + M);
	ui_w_move (w_split, x, M + 24, 8, h);
	ui_w_move (w_left, CX, M + 24, x - CX, h);
	ui_w_move (w_right, x + 8, M + 24, cur_w - M - (x + 8), h);
}

/* page 6 : volets (separateur) */
static void build_split (void)
{
	w_left = reg (ui_list (CX, M + 24, 200, 100, UL_HEADER), 6);
	ui_list_column (w_left, (long) TR ("Nom"), 300, 0);
	add_all (w_left, items, 0);		/* les memes elements (premiere colonne) */
	w_split = reg (ui_splitter (CX + 200, M + 24, 8, 100, CX + 120, CX + CW - 160), 6);
	w_right = reg (ui_textarea (CX + 208, M + 24, 100, 100, UTA_VSCROLL), 6);	/* volet de droite */
	ui_w_set_text (w_right, (long) TR (s_split));
	layout ();
}

static void to_page (long w, long tabs, long n) { ui_w_set_parent (w, ui_tabs_page (tabs, n)); }

/* page 7 : onglets, barres */
static void build_tabs (void)
{
	w_tbar = reg (ui_toolbar (CX, M + 24, CW, 0), 7);	/* barre d'outils */
	for (int i = 0; i < NTOOLS; i++)
		w_tools[i] = ui_toolbar_add (w_tbar, (long) tools[i].glyph, tools[i].label ? (long) TR (tools[i].label) : 0,
					     tools[i].flags);
	w_seg = reg (ui_segmented (CX, M + 24 + 66, 0, 0, (long) TR ("Jour|Semaine|Mois|Ann\xC3\xA9" "e")), 7);	/* controle segmente */
	long t = w_tabs = reg (ui_tabs (CX, M + 24 + 112, 440, 160, (long) TR ("G\xC3\xA9n\xC3\xA9ral|Son|Compte")), 7);
	/* page 1 : une etiquette, une case */
	to_page (ui_label (CX + 16, M + 24 + 112 + 56, 400, 24, (long) "Chaque onglet a sa page (ui_tabs_page) : ses widgets.", 0), t, 0);
	to_page (ui_checkbox (CX + 16, M + 24 + 112 + 92, 0, 0, (long) TR ("Ouvrir au d\xC3\xA9marrage"), 1), t, 0);
	to_page (ui_slider (CX + 16, M + 24 + 112 + 60, 300, 28, 0, 100, 25), t, 1);	/* page 2 : une glissiere */
	long tb = ui_textbox (CX + 16, M + 24 + 112 + 60, 300, 30, 0);		/* page 3 : un champ */
	ui_textbox_hint (tb, (long) TR ("Nom du compte"));
	to_page (tb, t, 2);
	w_strip = reg (ui_tabstrip (CX, M + 24 + 296, CW, 34, UTS_CLOSE | UTS_PLUS), 7);	/* barre d'onglets de documents */
	for (long n = 1; n <= 3; n++)
		new_doc (n);
}

/* page 8 : interaction */
static void build_misc (void)
{
	long l = reg (ui_list (CX, M + 24, 200, 200, UL_DRAG), 8);	/* liste dont on glisse les elements */
	add_all (l, s_fruits, 1);
	ui_w_tip (l, (long) TR ("Glissez un fruit vers le champ ou la zone"));
	long tb = reg (ui_textbox (CX + 220, M + 24 + 20, 240, 30, 0), 8);	/* un champ ou deposer */
	ui_textbox_hint (tb, (long) TR ("D\xC3\xA9posez ici"));
	ui_w_tip (tb, (long) TR ("Un texte d\xC3\xA9pos\xC3\xA9 s'ins\xC3\xA8re au point"));
	w_drop = reg (ui_panel (CX + 220, M + 24 + 70, 240, 60, UPN_SURFACE), 8);	/* une zone de depot (UN_DROP) */
	ui_w_cursor (w_drop, UC_CROSS);
	ui_w_tip (w_drop, (long) TR ("Zone de d\xC3\xA9p\xC3\xB4t : curseur en croix"));
	long b = reg (ui_pushbutton (CX + 220, M + 24 + 150, 0, 0, (long) "Bouton main", 0), 8);	/* bouton « main » */
	ui_w_cursor (b, UC_HAND);
	ui_w_tip (b, (long) TR ("Le curseur devient une main"));
	w_lang = reg (ui_label (CX, M + 24 + 220, CW, 24, (long) "", 0), 8);	/* langue (TR) */
	snprintf (numbuf, sizeof numbuf, "%s%s", TR ("Langue : "), (const char *) ui_lang ());
	strncat (numbuf, TR ("  -  TR(\"Annuler\") = "), sizeof numbuf - strlen (numbuf) - 1);
	strncat (numbuf, TR ("Annuler"), sizeof numbuf - strlen (numbuf) - 1);
	ui_w_set_text (w_lang, (long) numbuf);
}

/* page 9 : dates, couleurs, arbre */
static void build_dates (void)
{
	long c = ui_calendar (CX, M + 24, 0, 0, TODAY);		/* calendrier */
	ui_calendar_today (c, TODAY);
	reg (c, 9);
	long d = ui_datepicker (CX, M + 24 + 262, 0, 0, (2026 << 16) | (12 << 8) | 24);	/* selecteur de date */
	ui_datepicker_today (d, TODAY);
	reg (d, 9);
	reg (ui_colorpicker (CX + 160, M + 24 + 262, 0, 0, 0x2F74E0), 9);	/* selecteur de couleur */
	w_cdlg = reg (ui_pushbutton (CX + 216, M + 24 + 262, 0, 0, (long) TR (s_colordlg), 0), 9);	/* dialogue de couleur */
	long t = w_tree = reg (ui_tree (CX + 270, M + 24, CW - 270, 250), 9);	/* arborescence */
	long sys = ui_tree_add (t, -1, (long) TR ("Syst\xC3\xA8me"), 0);	/* Systeme > Kits > uikit, appkit... */
	long kits = ui_tree_add (t, sys, (long) "Kits", 0);
	ui_tree_add (t, kits, (long) "uikit.so", 0);
	ui_tree_add (t, kits, (long) "appkit.so", 0);
	ui_tree_add (t, kits, (long) "netkit.so", 0);
	ui_tree_add (t, sys, (long) "elegant", 0);
	ui_tree_expand (t, sys, 1);
	long apps = ui_tree_add (t, -1, (long) TR ("Applications"), 0);
	ui_tree_add (t, apps, (long) "editor", 0);
	ui_tree_add (t, apps, (long) "viewer", 0);
	ui_tree_add (t, -1, (long) TR ("Documents"), 0);
}

/* dessine le canevas : formes anticrenelees, texte */
static void canvas_paint (void)
{
	ui_canvas_begin (w_canvas);
	ui_fill (0, 0, 220, 120, 0x00FFFFFF);
	ui_vp_disc (50 * 16, 55 * 16, 30 * 16, 0x002F74E0);			/* disque */
	ui_vp_line (100 * 16, 20 * 16, 200 * 16, 100 * 16, 6 * 16, 0x00E5484D);	/* traits */
	ui_vp_line (100 * 16, 100 * 16, 200 * 16, 30 * 16, 24, 0x0030A46C);
	ui_vp_poly ((long) star, 10, 0x00F1B33B);				/* etoile */
	ui_text ((long) TR ("Canevas : cliquez pour peindre"), 8, 112, 0x00333333, 12, (long) ui.font, 0);
	ui_canvas_end ();
}

/* souris sur le canevas (UN_CHANGE) : un point tant que le bouton est tenu */
static void canvas_mouse (void)
{
	if (!(ui_ev_field (&ui, EV_CODE) & 1))		/* boutons */
		return;
	long x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
	ui_canvas_begin (w_canvas);
	ui_vp_disc ((x - CX) << 4, (y - (M + 24 + 216)) << 4, 3 * 16, 0x006040C0);	/* (coordonnees du canevas) */
	ui_canvas_end ();
	ui_w_draw (w_canvas);
}

/* page 10 : grille de donnees, panneau lateral, canevas, mise en page */
static void build_grid (void)
{
	long g = w_grid = reg (ui_list (CX, M + 24, 300, 200, UL_HEADER | UL_STRIPES | UL_GRID), 10);	/* grille virtuelle */
	ui_list_column (g, (long) TR ("Ligne"), 110, 0);
	ui_list_column (g, (long) TR ("Carr\xC3\xA9"), 110, ULC_RIGHT);
	ui_list_column (g, (long) "Mod 7", 80, ULC_RIGHT);
	ui_list_virtual (g, VROWS, (long) grid_row);
	long s = reg (ui_list (CX + 316, M + 24, 150, 200, UL_SIDEBAR), 10);	/* panneau lateral */
	add_all (s, s_side, 1);
	w_canvas = reg (ui_canvas (CX, M + 24 + 216, 220, 120), 10);		/* canevas */
	canvas_paint ();
	long p = reg (ui_panel (CX + 236, M + 24 + 216, 230, 120, UPN_SURFACE), 10);	/* panneau et sa grille de boutons */
	for (int i = 0; i < 4; i++)
		ui_w_set_parent (ui_pushbutton (0, 0, 0, 0, (long) lay_labels[i], 0), p);
	ui_layout (p, ULY_GRID, 10, 2);		/* grille de 2 colonnes, ecart 10 */
}

/* cree les widgets de toutes les pages */
static void build (void)
{
	w_nav = ui_list (M, M, NAV_W, WIN_H - ST_H - 2 * M, 0);	/* les pages */
	add_all (w_nav, pages, 1);
	ui_w_set_value (w_nav, 0);
	build_list ();
	build_text ();
	build_dialogs ();
	build_choice ();
	build_area ();
	build_values ();
	build_split ();
	build_tabs ();
	build_misc ();
	build_dates ();
	build_grid ();
}

/* la page : la montre (les widgets des autres caches) */
static void show_page (long p)
{
	page = p;
	face_fill (CX - 4, 0, cur_w - (CX - 4), cur_h - ST_H);	/* fond du contenu */
	for (long i = 0; i < nw; i++)
		ui_w_show (wtab[i].w, wtab[i].page == p);
	switch (p)
	{
	case 0:
		heading ("LISTE \xC3\x80 COLONNES", CX, M + 12);
		heading ("D\xC3\x89" "FILEMENT", CX + 440 + 24 - 60, M + 12);	/* (a gauche des boutons de la fenetre) */
		break;
	case 1:
		heading ("CHAMPS DE TEXTE", CX, M + 12);
		for (int i = 0; i < 4; i++)	/* libelles des champs */
			draw_label (tb_labels[i], CX, M + 24 + 20 + 52 * i);
		break;
	case 2:
		heading ("DIALOGUES MODAUX", CX, M + 12);
		break;
	case 3:
		heading ("CASES, RADIOS, INTERRUPTEURS", CX, M + 12);
		heading ("LISTE D\xC3\x89ROULANTE", CX + 280, M + 24 + 124);
		heading ("LISTE MODIFIABLE", CX + 280, M + 24 + 196);
		break;
	case 4:
		heading ("TEXTE MULTILIGNE (MONO, NUM\xC3\x89ROS DE LIGNE)", CX, M + 12);
		break;
	case 5:
		heading ("GLISSI\xC3\x88RES, PROGRESSION, VU-M\xC3\x88TRE, LCD, NOMBRE, MOLETTE", CX, M + 12);
		break;
	case 6:
		heading ("VOLETS ET S\xC3\x89PARATEUR (FEN\xC3\x8ATRE REDIMENSIONNABLE)", CX, M + 12);
		break;
	case 7:
		heading ("BARRE D'OUTILS, SEGMENTS, ONGLETS", CX, M + 12);
		break;
	case 8:
		heading ("GLISSER-D\xC3\x89POSER, INFOBULLES, CURSEURS, TR(), MENUS", CX, M + 12);
		for (int i = 0; i < 7; i++)	/* une ligne par police (ui_font_get) */
			ui_text ((long) TR (font_names[i]), CX, M + 24 + 280 + 22 * i, tok (TJ_TEXT1), 15, ui_font_get (i + 3), 0);
		break;
	case 9:
		heading ("CALENDRIER, DATE, COULEURS, ARBORESCENCE", CX, M + 12);
		break;
	case 10:
		heading ("GRILLE VIRTUELLE, PANNEAU LAT\xC3\x89RAL, CANEVAS, MISE EN PAGE", CX, M + 12);
		ui_pad_hints ((long) TR ("A\tchoisir\nB\tretour\n+\td\xC3\xA9placer\nStart\tmenu"), CX, M + 24 + 350, 24, 0);	/* manette */
		break;
	}
	ui_update (CX - 4, 0, cur_w - (CX - 4), cur_h - ST_H);
}

/* fond, liste des pages, page courante */
static void paint (void)
{
	face_fill (0, 0, cur_w, cur_h);
	ui_w_draw (w_nav);
	show_page (page);
	ui_update_all ();
	status ("Liste : cliquez un en-t\xC3\xAAte pour trier, glissez son bord ; double-clic : activer.");
}

/* fonction de ui_on_resize */
static long on_resize (long w, long h)
{
	cur_w = w;
	cur_h = h;
	layout ();
	paint ();
	return 0;
}

/* menu contextuel de la grille, au point du clic */
static void ctx_menu (void)
{
	char tr[256];
	size_t n = 0;
	for (int i = 0; s_ctx[i]; i++)
	{					/* (les libelles, traduits, a la suite) */
		const char *t = TR (s_ctx[i]);
		size_t l = strlen (t) + 1;
		if (n + l >= sizeof tr - 1)
			break;
		memcpy (tr + n, t, l);
		n += l;
	}
	tr[n] = 0;
	long k = ui_popup_local (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), (long) tr, 160);
	if (k < 0)
		return;
	const char *p = tr;
	for (; k > 0; k--)
		p += strlen (p) + 1;
	status2 ("Menu contextuel : ", p);
}

/* l'entree de menu choisie (barre de menus ou raccourci) -> 1 : quitter */
static int on_menu (long id)
{
	if (id == 1)
		return 1;
	if (id == 2)
	{					/* Grille : cochee / decochee */
		grid ^= 1;
		ui_menu_state (2, grid << 1);	/* (UMS_CHECKED) */
		status (grid ? "Grille : cochee (Options > Grille, ou Ctrl+G)" : "Grille : d\xC3\xA9" "coch\xC3\xA9" "e");
	}
	return 0;
}

/* le widget et le code rendus par ui_w_event : l'evenement dans la ligne d'etat */
static void report (long w, long code)
{
	long data;
	if (w == w_nav)
	{					/* liste des pages */
		if (code == UN_CHANGE)
			show_page (ui_w_get_value (w));
		return;
	}
	if (w == w_list)
	{
		if (code == UN_SORT)
			status ("Liste tri\xC3\xA9" "e.");
		else
			status2 (code == UN_ACTIVATE ? s_act : s_sel, (const char *) uikit_list_get (w, ui_w_get_value (w), &data));
		return;
	}
	if (w == w_split)
	{					/* separateur : volets replaces */
		layout ();
		ui_w_draw (w_left);
		ui_w_draw (w_right);
		ui_w_draw (w_split);
		return;
	}
	if (w == w_canvas)
	{
		canvas_mouse ();
		return;
	}
	if (w == w_grid)
	{					/* grille : trier, menu contextuel */
		if (code == UN_SORT)
		{
			uikit_list_sorted (w, &vdesc);
			ui_w_draw (w);
		}
		else if (code == UN_CONTEXT)
			ctx_menu ();
		else
			status2 (s_sel, (const char *) uikit_list_get (w, ui_w_get_value (w), &data));
		return;
	}
	if (w == w_cdlg)
	{					/* dialogue de couleur */
		long c = ui_color_dialog ((long) TR (s_colordlg), 0x30A46C);
		if (c < 0)
			status ("Annul\xC3\xA9.");
		else
		{
			snprintf (numbuf, sizeof numbuf, "%ld", c);
			status2 (s_val, numbuf);
		}
		return;
	}
	if (w == w_tree)
	{					/* arbre : le libelle du noeud */
		status2 (code == UN_ACTIVATE ? s_act : "Noeud : ", (const char *) ui_tree_get (w, ui_w_get_value (w)));
		return;
	}
	if (code == UN_DROP)
	{					/* un depot (sur la zone) : son texte */
		long kind, n = uikit_drop_get (dropbuf, 256, &kind);
		if (n >= 0 && n < 256)
			dropbuf[n] = 0;
		dropbuf[256] = 0;
		status2 ("D\xC3\xA9pos\xC3\xA9 : ", dropbuf);
		return;
	}
	if (w == w_strip)
	{					/* barre d'onglets : choisir, fermer, + */
		if (code == UN_NEW)
			new_doc (++ndocs);
		else if (code == UN_CLOSE)
			ui_list_remove (w, ui_tabstrip_closing (w));
		status2 ("Onglet : ", (const char *) uikit_list_get (w, ui_w_get_value (w), &data));
		return;
	}
	if (page == 7)
	{
		status2 ("Outil : ", (const char *) ui_w_get_text (w));
		return;
	}
	switch (page)
	{
	case 1:
		status2 (code == UN_ACTIVATE ? "Entr\xC3\xA9" "e : " : "Texte : ", (const char *) ui_w_get_text (w));
		return;
	case 2:					/* un bouton de la page des dialogues */
		if (code != UN_ACTIVATE)
			return;
		for (int i = 0; i < NDLG; i++)
			if (w_dlg[i] == w)
			{
				run_dialog (i);
				return;
			}
		return;
	case 4:
	{					/* ligne et colonne du curseur */
		long col, ln = uikit_textarea_caret (w, &col);
		snprintf (numbuf, sizeof numbuf, "%ld%s%ld", ln + 1, TR (", colonne "), col + 1);
		status2 ("Modifi\xC3\xA9 : ligne ", numbuf);
		return;
	}
	case 5:
		if (w == w_slider)
		{				/* la glissiere mene les autres */
			long v = ui_w_get_value (w);
			ui_w_set_value (w_prog, v);
			ui_vumeter_level (w_vu, v);
			snprintf (numbuf, sizeof numbuf, "%ld", v);
			ui_w_set_text (w_lcd, (long) numbuf);
		}
		/* fallthrough */
	case 3:
	{					/* valeur et texte du widget */
		long v = ui_w_get_value (w);
		snprintf (numbuf, sizeof numbuf, "%ld %s", v, (const char *) ui_w_get_text (w));
		status2 (s_val, numbuf);
		return;
	}
	}
}

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	if (ui_window_ex (-1, -1, WIN_W, WIN_H, (long) TR ("UIKit : d\xC3\xA9monstration"), WF_RESIZABLE) < 0)	/* redimensionnable */
		return 0;
	ui_menu ((long) "MFichier\nI1\tFermer\tCtrl+W\nMOptions\nI2\tGrille\tCtrl+G\nI3\tExporter\tCtrl+E\n");	/* (etats : ui_menu_state) */
	ui_menu_state (3, UMS_DISABLED);		/* « Exporter » : grisee */
	ui_min_size (600, 420);
	ui_on_resize ((long) on_resize, 0);
	jetons = (const unsigned *) ui_tokens ();
	build ();
	paint ();
	for (;;)
	{
		long t = ui_event (200);	/* (200 ms : infobulles) */
		if (t == 0)
		{
			ui_w_idle ();
			continue;
		}
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MENU)
		{
			if (on_menu (ui_ev_field (&ui, EV_CODE)))
				break;
			continue;
		}
		if (t == WEV_KEY)
		{				/* raccourcis des menus (par le kit) */
			long id = ui_menu_key (0);
			if (id >= 0)
			{
				if (on_menu (id))
					break;
				continue;
			}
		}
		long code = 0, taken = 0;
		long w = uikit_w_event (&code, &taken);	/* les widgets d'abord */
		if (code)
			report (w, code);
	}
	return 0;
}
