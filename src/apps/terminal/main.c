/*
 * terminal -- console graphique de Korin OS : un emulateur VT/xterm sur un pseudo-terminal (service
 * vterm, docs/VTERM.md). Port en C de terminal.asm (vt.c, vt_draw.c, vt_keys.c : vt.inc, vt_draw.inc,
 * vt_keys.inc), un seul source pour le Pi et le PC.
 *
 * Au demarrage : un pty (VT_OPEN), le shell lance avec l'esclave en entree, sortie et erreur
 * (interactif) : /bin/bash s'il est sur la carte (docs/BASH.md), sinon /bin/sh ; le Terminal
 * garde le maitre. Les touches, traduites en octets et sequences (vt_keys.c : key_encode), sont ecrites
 * sur le maitre (la discipline de ligne de vterm fait l'echo, l'edition, Ctrl+C -> SIGINT) ; la sortie
 * lue sur le maitre passe a l'analyseur (vt.c : grille, sequences ; vt_draw.c : dessin) ; les reponses
 * DSR / DA y repartent. Fenetre redimensionnable : nouvelle grille, TIOCSWINSZ (VT_SWINSZ : SIGWINCH au
 * programme). sh fini (fin lue sur le maitre) : son onglet se ferme ; le dernier ferme le Terminal.
 *
 * Onglets : chacun a son terminal (struct tab : grille, historique, pty) ; l'onglet « charge » est dans
 * les globales de vt.c (tab_load), les autres y passent le temps de lire leur sortie. En haut, la barre
 * de titre (HDR_H, la zone qui deplace la fenetre) : le menu a trois traits a gauche (ui_popup_ex), le
 * nom de l'app au milieu ; dessous, la bande des onglets s'il y en a plus d'un (top_y : haut de la
 * grille). Les memes commandes dans le menu de la barre du bureau (Onglets) et au clavier.
 *
 * Comme l'asm, le Terminal parle a vterm par ses messages (SYS_SVC_OPEN, SYS_SVC_CALL) et lance le
 * shell par f_spawn_stdio d'AppKit (les poignees de vterm passent telles quelles en entree / sortie).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <armpi/syscall.h>
#include <armpi/kit.h>
#include "term.h"

/* appels systeme (src/inc/syscalls.inc) */
enum { SYS_SLEEP_ = 0x17, SYS_SVC_OPEN_ = 0x31, SYS_SVC_CALL_ = 0x32, SYS_SETPGID_ = 0x6D };
/* protocole de vterm (src/inc/vterm.inc) */
enum { VT_OPEN = 1, VT_READ = 2, VT_WRITE = 3, VT_CLOSE = 4, VT_SWINSZ = 8, VT_SPGRP = 10 };
#define VT_NOWAIT	1
#define FS_DATA_MAX	464
/* f_spawn_stdio d'AppKit (src/kits/appkit/appkit.abi : rang 28) */
#define AK_F_SPAWN_STDIO 28

/* un message de service (512 o, src/inc/fs.inc : MSG_*) */
struct msg
{
	long op, a, b, c, d;
	uint32_t len, pid;
	unsigned char data[FS_DATA_MAX];
};

struct ui_vars ui;
long scroll, adv100, font_bold, full_redraw, top_y = HDR_H;
static long vtc = -1;				/* canal de vterm (-1 : pas encore ouvert) */
static long master;				/* bout maitre du pty de l'onglet courant (0 : aucun) */
static struct msg msg;

/* un onglet : un terminal et son pty. L'onglet dont l'etat est dans les globales (vt, rowp_main,
 * rowp_alt, vt_sb, vt_mem, scroll, master) est « charge » ; tab_load en change (vt.c et vt_draw.c ne
 * connaissent que les globales). La sortie de chaque onglet passe par l'analyseur meme cache ; seul
 * l'onglet actif est dessine. */
struct tab
{
	struct vt vt;
	struct cell *rm[VT_MAXROWS], *ra[VT_MAXROWS];
	struct cell *sb, *mem;
	long scroll, master, num;
};
static struct tab *tabs[TAB_MAX];
static long ntabs, cur;				/* nombre, onglet actif */
static long loaded = -1;			/* onglet charge (-1 : aucun) */
static long tab_count;				/* numero du dernier onglet ouvert */
static long font_title;				/* police de la barre de titre */

#define TR(s)	((const char *) ui_tr ((long) (s)))

static long sys (long n, long a, long b) { return armpi_syscall (n, a, b, 0, 0, 0, 0); }
static long vt_call (void) { return sys (SYS_SVC_CALL_, vtc, (long) &msg); }

/* ---- sortie : tout passe par l'analyseur (vt.c) ---- */

static void term_write (const unsigned char *p, long n)
{
	for (; n > 0; n--)
		term_putb (*p++);
}

static void term_puts (const char *s)
{
	while (*s)
		term_putb ((unsigned char) *s++);
}

/* une valeur signee en decimal */
static void term_dec (long v)
{
	char s[32], *p = s + sizeof s;
	unsigned long u = v < 0 ? 0 - (unsigned long) v : (unsigned long) v;
	*--p = 0;
	do
		*--p = (char) ('0' + u % 10);
	while (u /= 10);
	if (v < 0)
		*--p = '-';
	term_puts (p);
}

/* ---- le pseudo-terminal ---- */

/* des octets sur le bout maitre (ce que le programme lira) */
static void pty_write (const unsigned char *p, long n)
{
	int tries = 50;					/* (pty plein : 50 essais de 2 ms) */
	if (!master)
		return;
	while (n)
	{
		long k = n > FS_DATA_MAX ? FS_DATA_MAX : n;
		msg.op = VT_WRITE;
		msg.a = master;
		msg.len = (uint32_t) k;
		memmove (msg.data, p, (size_t) k);
		long r = vt_call ();
		if (r < 0)
			return;
		if (r == 0)
		{
			if (!--tries)
				return;
			sys (SYS_SLEEP_, 2, 0);
			continue;
		}
		p += r;
		n -= r;
	}
}

/* une reponse au programme (DSR, DA : vt.c) */
void term_reply_s (const char *s)
{
	pty_write ((const unsigned char *) s, (long) strlen (s));
}

/* la taille de la grille (et de la fenetre en pixels) au pty : VT_SWINSZ (SIGWINCH au groupe de premier
 * plan si elle change) */
static void pty_winsz (void)
{
	if (!master)
		return;
	msg.op = VT_SWINSZ;
	msg.a = master;
	msg.b = vt.rows;
	msg.c = vt.cols;
	msg.d = (ui.w & 0xFFFF) | (ui.h & 0xFFFF) << 16;
	msg.len = 0;
	vt_call ();
}

/* (onglet courant) vterm, un pty, bash (sinon sh) sur l'esclave ; un message s'il manque quelque
 * chose */
static void pty_start (void)
{
	if (vtc < 0)
		vtc = sys (SYS_SVC_OPEN_, (long) "vterm", 5000);
	if (vtc >= 0)
	{
		msg.op = VT_OPEN;
		msg.len = 0;
		if (vt_call () >= 0)
		{
			long slave = msg.b;
			master = msg.a;
			pty_winsz ();			/* la taille avant le shell */
			long fds[3] = { slave, slave, slave };	/* sh : entree, sortie, erreur = l'esclave */
			long (*spawn) (long, long, long) = 0;
			void *ak = armpi_kit_open ("appkit", AK_F_SPAWN_STDIO + 1);
			if (ak)
				spawn = (long (*) (long, long, long)) armpi_kit_fn (ak, AK_F_SPAWN_STDIO);
			long pid = -2;			/* (E_NOENT : AppKit absent) */
			if (spawn)
			{
				pid = spawn ((long) "/bin/bash", 0, (long) fds);	/* bash, sinon sh */
				if (pid < 0)
					pid = spawn ((long) "/bin/sh", 0, (long) fds);
			}
			if (pid >= 0)
			{	/* le shell : un groupe a lui, au premier plan du pty (comme login sous Unix : Ctrl+C va
				 * a bash et a ses commandes) */
				sys (SYS_SETPGID_, pid, pid);
				msg.op = VT_SPGRP;
				msg.a = master;
				msg.b = pid;
				msg.len = 0;
				vt_call ();
			}
			msg.op = VT_CLOSE;		/* notre copie de l'esclave : rendue (sh la tient ; sa fin = fin
							 * du maitre) */
			msg.a = slave;
			msg.len = 0;
			vt_call ();
			if (pid < 0)
			{
				term_puts (TR ("Terminal : /bin/sh ne se lance pas, erreur "));
				term_dec (pid);
				term_puts ("\r\n");
			}
			return;
		}
	}
	master = 0;
	term_puts (TR ("Terminal : pas de pseudo-terminal (service vterm absent)."));
	term_puts ("\r\n");
}

/* -> 1 : sortie lue (a dessiner), 0 rien, -1 : fin (le programme est parti) */
static int pty_poll (void)
{
	int got = 0;
	if (!master)
		return 0;
	for (int i = 0; i < RD_MAX; i++)
	{
		msg.op = VT_READ;
		msg.a = master;
		msg.b = FS_DATA_MAX;
		msg.c = VT_NOWAIT;
		msg.len = 0;
		long r = vt_call ();
		if (r < 0)				/* (E_AGAIN : rien pour l'instant) */
			break;
		if (r == 0)
			return -1;
		term_write (msg.data, r);
		got = 1;
	}
	return got;
}

/* ---- vue ---- */

/* deplacement (+ = vers le haut) ; au plus l'historique (rien sur l'ecran alternatif) */
static void scroll_by (long d)
{
	long s = scroll + d, max = vt.alt ? 0 : vt.sbn;
	if (s < 0)
		s = 0;
	if (s > max)
		s = max;
	scroll = s;
}

/* -> lignes d'une page (Maj+Page haut, Maj+Page bas) */
static long page_rows (void)
{
	long n = vt.rows - 2;
	return n >= 1 ? n : 1;
}

/* taille de la fenetre -> lignes et colonnes de la grille (sous la barre et les onglets) ; tout a
 * redessiner ; la taille au pty */
static void vt_fit (void)
{
	long w = ui.w - 2 * PAD, h = ui.h - top_y - 2 * PAD;
	if (w < 0)
		w = 0;
	if (h < 0)
		h = 0;
	vt_resize ((long) ((unsigned long) (w * 100) / (unsigned long) adv100), (long) ((unsigned long) h / LH));
	full_redraw = 1;
	pty_winsz ();
}

/* ---- onglets ---- */

/* l'etat des globales rendu a l'onglet charge */
static void tab_save (void)
{
	if (loaded < 0)
		return;
	struct tab *t = tabs[loaded];
	t->vt = vt;
	memcpy (t->rm, rowp_main, sizeof t->rm);
	memcpy (t->ra, rowp_alt, sizeof t->ra);
	t->sb = vt_sb;
	t->mem = vt_mem;
	t->scroll = scroll;
	t->master = master;
}

/* l'onglet i dans les globales */
static void tab_load (long i)
{
	if (i == loaded)
		return;
	tab_save ();
	struct tab *t = tabs[i];
	vt = t->vt;
	memcpy (rowp_main, t->rm, sizeof t->rm);
	memcpy (rowp_alt, t->ra, sizeof t->ra);
	vt.tbl = vt.alt ? rowp_alt : rowp_main;		/* (la table de l'ecran montre : celle des globales) */
	vt_sb = t->sb;
	vt_mem = t->mem;
	scroll = t->scroll;
	master = t->master;
	loaded = i;
}

/* 1 : l'onglet charge est celui qu'on voit (term_draw ne dessine que lui) */
int term_active (void)
{
	return loaded == cur;
}

/* x et largeur de l'onglet i dans la bande */
static long tab_w (void)
{
	long w = ntabs ? ui.w / ntabs : ui.w;
	return w > TAB_WMAX ? TAB_WMAX : w;
}

/* le nom d'un onglet : « Terminal 2 » */
static void tab_label (const struct tab *t, char *out, size_t n)
{
	const char *base = TR ("Terminal");
	size_t k = strlen (base);
	if (k > n - 24)
		k = n - 24;
	memcpy (out, base, k);
	char d[24], *p = d + sizeof d;
	unsigned long u = (unsigned long) t->num;
	*--p = 0;
	do
		*--p = (char) ('0' + u % 10);
	while (u /= 10);
	out[k++] = ' ';
	strcpy (out + k, p);
}

/* trois traits du menu, nom de l'app au milieu, puis les onglets s'il y en a plus d'un */
static void hdr_draw (void)
{
	const unsigned *jet = (const unsigned *) ui_tokens ();
	uint32_t accent = jet[TJ_ACCENT / 4] & 0xFFFFFF;
	ui_fill (0, 0, ui.w, top_y, COL_BG);
	for (int i = 0; i < 3; i++)			/* le menu : trois traits de 16 x 2 */
		ui_fill (MENU_W / 2 - 8, HDR_H / 2 - 6 + i * 5, 16, 2, COL_TXT);
	const char *title = TR ("Terminal");		/* le nom, centre (sans deborder sur les boutons) */
	long tw = ui_text_width ((long) title, 14, font_title);
	long x = (ui.w - tw) / 2, xmax = ui.w - FRAME_CAP_W - 8;
	if (x + tw > xmax)
		x = xmax - tw;
	if (x < MENU_W + 4)
		x = MENU_W + 4;
	ui_text ((long) title, x, HDR_H / 2 + 5, COL_TXT, 14, font_title, xmax - x);
	if (ntabs > 1)
	{						/* la bande : onglets cote a cote, l'actif au fond du terminal */
		long w = tab_w ();
		ui_fill (0, HDR_H, ui.w, TAB_H, COL_TABS);
		for (long i = 0; i < ntabs; i++)
		{
			long x0 = i * w;
			char lab[96];
			tab_label (tabs[i], lab, sizeof lab);
			if (i == cur)
			{
				ui_fill (x0, HDR_H, w, TAB_H, COL_BG);
				ui_fill (x0, HDR_H, w, 2, accent);
			}
			else if (i + 1 != cur && i + 1 < ntabs)	/* separateur entre deux onglets inactifs */
				ui_fill (x0 + w - 1, HDR_H + 7, 1, TAB_H - 14, 0x2A3038);
			uint32_t ink = i == cur ? COL_TXT : COL_TXT_DIM;
			long lw = w - TAB_CLOSE_W - 16;
			if (lw > 0)
			{
				ui_clip (x0, HDR_H, w - TAB_CLOSE_W, TAB_H);
				ui_text ((long) lab, x0 + 12, HDR_H + TAB_H / 2 + 5, ink, 13, (long) ui.font, lw);
				ui_clip (0, 0, 0, 0);
			}
			ui_text ((long) "\xC3\x97", x0 + w - TAB_CLOSE_W, HDR_H + TAB_H / 2 + 6, ink, 16, (long) ui.font, TAB_CLOSE_W);	/* la croix */
		}
	}
	ui_update (0, 0, ui.w, top_y);
}

/* la hauteur du haut suit le nombre d'onglets ; si elle change, toutes les grilles aussi */
static void relayout (void)
{
	long ny = HDR_H + (ntabs > 1 ? TAB_H : 0);
	if (ny != top_y)
	{
		top_y = ny;
		for (long i = 0; i < ntabs; i++)
		{
			tab_load (i);
			vt_fit ();
		}
		tab_load (cur);
	}
	full_redraw = 1;
	hdr_draw ();
	term_draw ();
}

/* l'onglet i devient l'onglet actif */
static void tab_select (long i)
{
	if (i < 0 || i >= ntabs)
		return;
	cur = i;
	tab_load (cur);
	full_redraw = 1;
	hdr_draw ();
	term_draw ();
}

/* un onglet de plus, avec son shell, devient l'actif -> 0 (< 0 : trop d'onglets ou pas de memoire) */
static int tab_new (void)
{
	if (ntabs >= TAB_MAX)
		return -1;
	struct tab *t = calloc (1, sizeof *t);
	if (!t)
		return -1;
	long prev = loaded, oy = top_y;
	tab_save ();
	loaded = -1;					/* les globales : au nouveau terminal */
	if (vt_init () < 0)
	{
		free (t);
		if (prev >= 0)
			tab_load (prev);
		return -1;
	}
	master = 0;
	t->num = ++tab_count;
	tabs[ntabs] = t;
	loaded = cur = ntabs++;
	top_y = HDR_H + (ntabs > 1 ? TAB_H : 0);
	vt_fit ();					/* (la taille avant le shell) */
	pty_start ();
	if (top_y != oy)				/* la bande des onglets parait : les autres grilles aussi */
		for (long i = 0; i < ntabs - 1; i++)
		{
			tab_load (i);
			vt_fit ();
		}
	tab_load (cur);
	full_redraw = 1;
	hdr_draw ();
	term_draw ();
	return 0;
}

/* ferme l'onglet i (son pty : SIGHUP au shell, par vterm) -> 0 : il en reste, 1 : plus aucun */
static int tab_close (long i)
{
	tab_load (i);
	if (master)
	{
		msg.op = VT_CLOSE;
		msg.a = master;
		msg.len = 0;
		vt_call ();
		master = 0;
	}
	free (vt_mem);
	free (tabs[i]);
	loaded = -1;
	for (long k = i; k + 1 < ntabs; k++)
		tabs[k] = tabs[k + 1];
	ntabs--;
	if (!ntabs)
		return 1;
	if (cur > i || cur >= ntabs)
		cur = cur > 0 ? cur - 1 : 0;
	tab_load (cur);
	relayout ();
	return 0;
}

/* ---- menu ---- */

enum { CMD_NEW = 1, CMD_CLOSE, CMD_NEXT, CMD_PREV };

/* -> 1 : plus d'onglet (le Terminal se ferme) */
static int command (long id)
{
	if (id == CMD_NEW)
		tab_new ();
	else if (id == CMD_CLOSE)
		return tab_close (cur);
	else if (id == CMD_NEXT && ntabs > 1)
		tab_select ((cur + 1) % ntabs);
	else if (id == CMD_PREV && ntabs > 1)
		tab_select ((cur + ntabs - 1) % ntabs);
	return 0;
}

/* le menu de la barre du bureau (et ses raccourcis, par ui_menu_key) */
static void menu_bar (void)
{
	static char m[512];
	char *o = m;
	o += sprintf (o, "M%s\n", TR ("Onglets"));
	o += sprintf (o, "I%d\t%s\tCtrl+Maj+T\n", CMD_NEW, TR ("Nouvel onglet"));
	o += sprintf (o, "I%d\t%s\tCtrl+Maj+W\n-\n", CMD_CLOSE, TR ("Fermer l'onglet"));
	o += sprintf (o, "I%d\t%s\tCtrl+Page bas\n", CMD_NEXT, TR ("Onglet suivant"));
	sprintf (o, "I%d\t%s\tCtrl+Page haut\n", CMD_PREV, TR ("Onglet précédent"));
	ui_menu ((long) m);
}

/* le menu des trois traits, ouvert sous le bouton -> 1 : plus d'onglet */
static int menu_popup (void)
{
	static const long ids[] = { CMD_NEW, CMD_CLOSE, 0, CMD_NEXT, CMD_PREV };
	char items[512], *o = items;
	long pos[2];
	o += sprintf (o, "%s\tCtrl+Maj+T", TR ("Nouvel onglet")) + 1;
	o += sprintf (o, "%s\tCtrl+Maj+W", TR ("Fermer l'onglet")) + 1;
	o += sprintf (o, "-") + 1;
	o += sprintf (o, "%s%s\tCtrl+Page bas", ntabs > 1 ? "" : "\x01", TR ("Onglet suivant")) + 1;
	o += sprintf (o, "%s%s\tCtrl+Page haut", ntabs > 1 ? "" : "\x01", TR ("Onglet précédent")) + 1;
	*o = 0;
	if (ui_window_pos ((long) pos) < 0)
		return 0;
	long r = ui_popup_ex (pos[0] + 4, pos[1] + HDR_H, (long) items, 260, 16);
	if (r < 0 || r >= (long) (sizeof ids / sizeof ids[0]) || !ids[r])
		return 0;
	return command (ids[r]);
}

/* un appui -> 1 : plus d'onglet */
static int on_click (long x, long y)
{
	if (y < HDR_H)
	{
		if (x < MENU_W)
			return menu_popup ();
		return 0;
	}
	if (ntabs > 1 && y < HDR_H + TAB_H)
	{
		long w = tab_w (), i = x / w;
		if (i >= ntabs)
			return 0;
		if (x >= i * w + w - TAB_CLOSE_W - 4)
			return tab_close (i);
		if (i != cur)
			tab_select (i);
	}
	return 0;
}

/* ---- clavier : Maj+Page haut / bas font defiler la vue, Ctrl+Page haut / bas changent d'onglet,
 * Ctrl+Maj+T / W ouvrent / ferment un onglet ; tout le reste va au programme ---- */
/* -> 1 : plus d'onglet */
static int on_key (void)
{
	if (ui_ev_field (&ui, EV_KIND) == 0)
		return 0;				/* relache */
	unsigned code = (unsigned) ui_ev_field (&ui, EV_CODE), ch = (unsigned) ui_ev_field (&ui, EV_CHAR);
	unsigned mods = (unsigned) ui_ev_field (&ui, EV_MODS);
	if ((mods & K_MOD_CTRL) && !(mods & (K_MOD_SHIFT | K_MOD_ALT)))
	{
		if (code == KEY_PGDN)
			return command (CMD_NEXT);
		if (code == KEY_PGUP)
			return command (CMD_PREV);
	}
	if ((mods & K_MOD_CTRL) && (mods & K_MOD_SHIFT))
	{						/* (Ctrl+Maj seulement : Ctrl+W reste au shell) */
		long id = ui_menu_key (0);
		if (id > 0)
			return command (id);
	}
	if ((mods & K_MOD_SHIFT) && !(mods & K_MOD_CTRL))
	{
		if (code == KEY_PGUP)
		{
			scroll_by (page_rows ());
			term_draw ();
			return 0;
		}
		if (code == KEY_PGDN)
		{
			scroll_by (-page_rows ());
			term_draw ();
			return 0;
		}
	}
	unsigned char buf[32];
	int n = key_encode (code, ch, mods, buf);
	if (!n)
		return 0;
	pty_write (buf, n);
	if (scroll)
	{						/* toute frappe ramene en bas */
		scroll = 0;
		term_draw ();
	}
	return 0;
}

int main (void)
{
	if (!uikit_open (&ui))
		return 0;
	if (ui_window_ex (-1, -1, WIN_W, WIN_H, (long) "Terminal", WF_GLYPH_LIGHT | WF_BODY_DARK | WF_RESIZABLE) < 0)
		return 0;
	ui_min_size (MIN_W, MIN_H);
	/* avance de la police mono (x100) */
	adv100 = ui_text_width ((long) "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
			       FONT_PX, (long) ui.font_m);
	if (adv100 <= 0)
		adv100 = 900;
	font_bold = ui_font_get (UFN_MONOBOLD);		/* gras : DejaVu Sans Mono Bold */
	if (font_bold == (long) ui.font)		/* (absente, ui_font_get rend la sans) */
		font_bold = (long) ui.font_m;
	font_title = ui_font_get (UFN_BOLD);
	if (vt_shadow_init () < 0 || tab_new () < 0)
		return 0;
	menu_bar ();
	for (;;)
	{
		int drawn = 0, gone = 0;
		for (long i = 0; i < ntabs; i++)
		{					/* la sortie des programmes de tous les onglets */
			tab_load (i);
			int r = pty_poll ();
			if (r < 0)
			{				/* shell fini : son onglet se ferme */
				if (tab_close (i))
					return 0;	/* (le dernier : le Terminal aussi) */
				gone = 1;
				break;
			}
			if (r && i == cur)
				drawn = 1;
		}
		tab_load (cur);
		long wait = 15;
		if (drawn)
		{
			term_draw ();
			wait = 1;			/* (encore de la sortie, sans doute) */
		}
		if (gone)
			continue;
		long t = ui_event (wait);
		if (t == 0)
			continue;
		tab_load (cur);
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_KEY)
		{
			if (on_key ())
				break;
		}
		else if (t == WEV_MENU)
		{
			if (command (ui_ev_field (&ui, EV_CODE)))
				break;
		}
		else if (t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1)
		{
			if (on_click ((long) (int) ui_ev_field (&ui, EV_X), (long) (int) ui_ev_field (&ui, EV_Y)))
				break;
		}
		else if (t == WEV_RESIZE)
		{					/* nouvelle taille : nouvelles grilles, pour tous les onglets */
			if (ui_resize ((unsigned) ui_ev_field (&ui, EV_X), (unsigned) ui_ev_field (&ui, EV_Y)) < 0)
				continue;
			for (long i = 0; i < ntabs; i++)
			{
				tab_load (i);
				vt_fit ();
			}
			tab_load (cur);
			full_redraw = 1;
			hdr_draw ();
			term_draw ();
		}
		else if (t == WEV_WHEEL)
		{					/* molette : 3 lignes par cran */
			scroll_by ((long) ui_ev_field (&ui, EV_Y) * 3);
			term_draw ();
		}
		else if (t == WEV_THEME)
			hdr_draw ();			/* (l'accent) */
	}
	return 0;
}
