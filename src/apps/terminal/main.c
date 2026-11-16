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
 * programme). sh fini (fin lue sur le maitre) : le Terminal se ferme.
 *
 * Comme l'asm, le Terminal parle a vterm par ses messages (SYS_SVC_OPEN, SYS_SVC_CALL) et lance le
 * shell par f_spawn_stdio d'AppKit (les poignees de vterm passent telles quelles en entree / sortie).
 */
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
long scroll, adv100, font_bold, full_redraw;
static long vtc;				/* canal de vterm */
static long master;				/* bout maitre du pty (0 : aucun) */
static struct msg msg;

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

/* vterm, un pty, bash (sinon sh) sur l'esclave ; un message s'il manque quelque chose */
static void pty_start (void)
{
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

/* taille de la fenetre -> lignes et colonnes de la grille ; tout a redessiner ; la taille au pty */
static void vt_fit (void)
{
	long w = ui.w - 2 * PAD, h = ui.h - 2 * PAD;
	if (w < 0)
		w = 0;
	if (h < 0)
		h = 0;
	vt_resize ((long) ((unsigned long) (w * 100) / (unsigned long) adv100), (long) ((unsigned long) h / LH));
	full_redraw = 1;
	pty_winsz ();
}

/* ---- clavier : Maj+Page haut / bas font defiler la vue ; tout le reste va au programme ---- */
static void on_key (void)
{
	if (ui_ev_field (&ui, EV_KIND) == 0)
		return;					/* relache */
	unsigned code = (unsigned) ui_ev_field (&ui, EV_CODE), ch = (unsigned) ui_ev_field (&ui, EV_CHAR);
	unsigned mods = (unsigned) ui_ev_field (&ui, EV_MODS);
	if (mods & K_MOD_SHIFT)
	{
		if (code == KEY_PGUP)
		{
			scroll_by (page_rows ());
			term_draw ();
			return;
		}
		if (code == KEY_PGDN)
		{
			scroll_by (-page_rows ());
			term_draw ();
			return;
		}
	}
	unsigned char buf[32];
	int n = key_encode (code, ch, mods, buf);
	if (!n)
		return;
	pty_write (buf, n);
	if (scroll)
	{						/* toute frappe ramene en bas */
		scroll = 0;
		term_draw ();
	}
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
	if (vt_init () < 0)
		return 0;
	vt_fit ();
	pty_start ();
	term_draw ();
	for (;;)
	{
		int r = pty_poll ();			/* la sortie du programme */
		if (r < 0)
			break;				/* sh fini : le Terminal aussi */
		long wait = 15;
		if (r)
		{
			term_draw ();
			wait = 1;			/* (encore de la sortie, sans doute) */
		}
		long t = ui_event (wait);
		if (t == 0)
			continue;
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_KEY)
			on_key ();
		else if (t == WEV_RESIZE)
		{					/* nouvelle taille : nouvelle grille */
			if (ui_resize ((unsigned) ui_ev_field (&ui, EV_X), (unsigned) ui_ev_field (&ui, EV_Y)) < 0)
				continue;
			vt_fit ();
			term_draw ();
		}
		else if (t == WEV_WHEEL)
		{					/* molette : 3 lignes par cran */
			scroll_by ((long) ui_ev_field (&ui, EV_Y) * 3);
			term_draw ();
		}
	}
	return 0;
}
