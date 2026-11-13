/*
 * freecell -- FreeCell. Les 52 cartes sont en huit colonnes ; quatre cases libres (en haut a gauche) tiennent
 * une carte chacune ; on monte les quatre fondations (en haut a droite) par enseigne depuis l'as. On glisse
 * les cartes a la souris ou au doigt : une colonne prend une carte de un de moins et de l'autre couleur
 * (n'importe laquelle si elle est vide), une suite se deplace d'un coup si les cases libres et les colonnes
 * vides le permettent. Un toucher sans glisser envoie la carte a la destination evidente. Les cartes qui ne
 * servent plus montent seules. Les parties sont numerotees de 1 a 32000, les memes que FreeCell de
 * Microsoft. `freecell <numero>` lance cette partie.
 *
 * Port en C++ depuis user/Apps/freecell/main.cpp d'Onyx (le modele, la donne de Microsoft, les regles,
 * les noms des methodes) ; ce que Korin avait ajoute dans freecell.asm est garde tel quel : la table qui
 * suit la taille de la fenetre et ses cartes (src/inc/cards.h, traduction de cards.inc), le bandeau et ses
 * boutons, le toucher qui envoie la carte, le menu Partie, les textes et lang/. Un seul source pour le Pi et
 * le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../inc/cards.h"

#define WIN_W		720
#define WIN_H		540
#define MIN_W		540
#define MIN_H		400
#define NCOL		8
#define UNDO_MAX	128
#define SRC_CELL	8			/* source : 0..7 colonne, 8..11 case */
#define GAME_MAX	32000
#define MSG_MS		2500

enum { ID_NEW = 1, ID_SELECT, ID_RESTART, ID_UNDO, ID_QUIT };

static const char s_title[] = "FreeCell";
static const char s_game[] = "Partie n° ";
static const char s_moves[] = "    Coups : ";
static const char s_nomove[] = "Pas assez de cases libres pour autant de cartes";
static const char s_won[] = "Gagné !";
static const char s_again[] = "Toucher pour une nouvelle partie";
static const char s_sel_t[] = "Choisir une partie";
static const char s_sel_q[] = "Numéro de partie (1 à 32000) :";
static const char s_btns[] = "Nouvelle\0Annuler\0Partie…\0";
static const char s_undo_k[] = "Annuler le coup";	/* (« Annuler » seul : Cancel dans les dictionnaires) */
static const unsigned char s_btn_ids[3] = { ID_NEW, ID_UNDO, ID_SELECT };
static const char s_menus[] =
	"MPartie\n"
	"I1\tNouvelle partie\tCtrl+N\n"
	"I2\tChoisir une partie…\n"
	"I3\tRecommencer\n"
	"I4\tAnnuler le coup\tCtrl+Z\n"
	"-\n"
	"I5\tQuitter\n";

struct Col { int n; signed char c[52]; };
struct FState { Col col[NCOL]; signed char cell[4]; signed char found[4]; int moves; };	/* found = rang du sommet (-1 aucun) */

/* chaine -> valeur decimale (parse_dec d'AppKit : les chiffres du debut) */
static unsigned long parse_dec (const char *s)
{
	unsigned long v = 0;
	while ((unsigned) (*s - '0') <= 9)
		v = v * 10 + (unsigned) (*s++ - '0');
	return v;
}

/* destination, morceau de texte -> fin : la traduction du morceau ajoutee ; les espaces autour du morceau
 * restent ici (un dictionnaire ne garde pas ceux d'une cle) */
static char *tr_cat (char *d, const char *s)
{
	while (*s == ' ')
		*d++ = *s++;
	char key[240];
	int n = 0, len = 0;
	for (; s[n] && n < 239; n++)
	{
		key[n] = s[n];
		if (s[n] != ' ')
			len = n + 1;
	}
	key[len] = 0;
	d = stpcpy (d, TR (key));
	return stpcpy (d, s + len);
}

class FreeCell
{
public:
	FState s;
	FState undo[UNDO_MAX]; int nundo;
	unsigned game;
	bool won;
	bool pressed; long pressX, pressY, offX, offY; int src, idx;	/* l'appui en cours (src : 0..7 colonne, 8..11 case) */
	unsigned long sentT; long sentX, sentY;		/* le dernier toucher qui a envoye une carte */
	unsigned long msgT;				/* instant du message (0 : aucun) */
	unsigned long animT;
	char txt[96];

	static unsigned randomGame () { return (unsigned) ck_rnd_n (GAME_MAX) + 1; }

	/* Microsoft's deal : son rand () de la bibliotheque C, seme par le numero */
	static void msDeal (unsigned n, Col col[NCOL])
	{
		unsigned seed = n;
		int deck[52];
		for (int i = 0; i < 52; i++) deck[i] = 51 - i;		/* numerotation de MS : rang * 4 + enseigne (T K C P) */
		for (int i = 0; i < 51; i++)
		{
			seed = (seed * 214013u + 2531011u) & 0x7FFFFFFFu;
			int j = 51 - (int) ((seed >> 16) % (unsigned) (52 - i));
			int t = deck[i]; deck[i] = deck[j]; deck[j] = t;
		}
		for (int c = 0; c < NCOL; c++) col[c].n = 0;
		for (int i = 0; i < 52; i++)
		{
			int ms = deck[i];
			Col &k = col[i % NCOL];
			k.c[k.n++] = (signed char) card_make (ms / 4, ms % 4);	/* nos enseignes : T K C P aussi */
		}
	}

	void newGame (unsigned n)
	{
		game = n; msDeal (n, s.col);
		for (int i = 0; i < 4; i++) { s.cell[i] = -1; s.found[i] = -1; }
		s.moves = 0; nundo = 0; won = false; pressed = false; dr_on = 0; win_on = 0; msgT = 0;
		autoPlay ();
		paint ();
	}

	/* pas vertical des cartes d'une colonne (serre pour tenir) */
	long dy (int c) const
	{
		long d = c_dy_max, n = s.col[c].n;
		if (n < 2) return d;
		long a = (g_h - l_tab - c_h - 6) / (n - 1);
		if (a < d) d = a;
		if (d < c_dy_min) d = c_dy_min;
		return d;
	}

	int freeCells () const { int n = 0; for (int i = 0; i < 4; i++) n += s.cell[i] < 0; return n; }
	int emptyCols (int except) const { int n = 0; for (int c = 0; c < NCOL; c++) n += c != except && s.col[c].n == 0; return n; }
	int maxRun (int dest) const { return (freeCells () + 1) << emptyCols (dest); }

	bool canFound (int card) const { return card_rank (card) == s.found[card_suit (card)] + 1; }
	bool canStack (int card, int c) const
	{
		const Col &k = s.col[c];
		if (k.n == 0) return true;
		int t = k.c[k.n - 1];
		return card_red (t) != card_red (card) && card_rank (card) + 1 == card_rank (t);
	}
	bool isRun (int c, int from) const		/* [from .. sommet] descendante, couleurs alternees */
	{
		const Col &k = s.col[c];
		for (int i = from; i + 1 < k.n; i++)
			if (card_red (k.c[i]) == card_red (k.c[i + 1]) || card_rank (k.c[i]) != card_rank (k.c[i + 1]) + 1) return false;
		return true;
	}

	void pushUndo () { if (nundo == UNDO_MAX) { memmove (undo, undo + 1, sizeof undo[0] * (UNDO_MAX - 1)); nundo--; } undo[nundo++] = s; }
	void doUndo () { if (nundo && !won) { s = undo[--nundo]; paint (); } }

	/* sans risque : plus rien ne peut en avoir besoin */
	bool safe (int card) const
	{
		int r = card_rank (card);
		if (r <= 1) return true;
		bool red = card_red (card);
		for (int f = 0; f < 4; f++)
		{
			bool fr = f == 1 || f == 2;
			if (fr != red && s.found[f] < r - 1) return false;
		}
		return true;
	}
	/* les cartes qui ne servent plus montent seules */
	void autoPlay ()
	{
		bool any = true;
		while (any)
		{
			any = false;
			for (int i = 0; i < 4; i++)
				if (s.cell[i] >= 0 && canFound (s.cell[i]) && safe (s.cell[i]))
				{ s.found[card_suit (s.cell[i])]++; s.cell[i] = -1; any = true; }
			for (int c = 0; c < NCOL; c++)
			{
				Col &k = s.col[c];
				if (k.n && canFound (k.c[k.n - 1]) && safe (k.c[k.n - 1]))
				{ s.found[card_suit (k.c[k.n - 1])]++; k.n--; any = true; }
			}
		}
		checkWin ();
	}
	void checkWin ()
	{
		for (int f = 0; f < 4; f++) if (s.found[f] != 12) return;
		if (won) return;
		won = true;
		for (int f = 0; f < 4; f++) wa.fx[f] = ck_col_x (4 + f);
		win_start (l_top);
		animT = ck_time_ms ();
	}
	/* un coup joue */
	void after ()
	{
		s.moves++;
		msgT = 0;
		autoPlay ();
	}

	/* la (les) carte(s) deplacee(s) : source 0..7 colonne (depuis l'indice idx), 8..11 case */
	int srcCard (int sr, int i) const { return sr < SRC_CELL ? s.col[sr].c[i] : s.cell[sr - SRC_CELL]; }
	int srcCount (int sr, int i) const { return sr < SRC_CELL ? s.col[sr].n - i : 1; }
	void take (int sr, int i) { if (sr < SRC_CELL) s.col[sr].n = i; else s.cell[sr - SRC_CELL] = -1; }

	bool dropOnFound (int sr, int i)
	{
		if (srcCount (sr, i) != 1) return false;
		int card = srcCard (sr, i);
		if (!canFound (card)) return false;
		pushUndo (); take (sr, i); s.found[card_suit (card)]++; after ();
		return true;
	}
	bool dropOnCell (int sr, int i, int cell)
	{
		if (srcCount (sr, i) != 1 || s.cell[cell] >= 0) return false;
		int card = srcCard (sr, i);
		pushUndo (); take (sr, i); s.cell[cell] = (signed char) card; after ();
		return true;
	}
	bool dropOnCol (int sr, int i, int c)
	{
		if (sr == c) return false;
		int n = srcCount (sr, i);
		/* sur une colonne vide, la plus longue fin de suite qui passe */
		if (sr < SRC_CELL && s.col[c].n == 0 && n > maxRun (c)) { n = maxRun (c); i = s.col[sr].n - n; }
		if (!canStack (srcCard (sr, i), c)) return false;
		if (n > maxRun (c))
		{
			msgT = ck_time_ms () | 1;	/* (0 serait « pas de message ») */
			paint ();
			return false;
		}
		pushUndo ();
		Col &d = s.col[c];
		for (int k = 0; k < n; k++) d.c[d.n++] = (signed char) srcCard (sr, i + k);
		take (sr, i);
		after ();
		return true;
	}

	/* le toucher sans glisser, vers la destination evidente : fondation, colonne garnie, case libre,
	 * colonne vide (Onyx : le double clic, vers la fondation ou une case) */
	void clickSend (int sr, int i)
	{
		int card = srcCard (sr, i);
		if (dropOnFound (sr, i)) return;
		for (int c = 0; c < NCOL; c++)
			if (s.col[c].n && c != sr && canStack (card, c) && srcCount (sr, i) <= maxRun (c))
			{ dropOnCol (sr, i, c); return; }
		if (sr < SRC_CELL)			/* (d'une case : pas vers une autre case) */
		{
			for (int k = 0; k < 4; k++) if (dropOnCell (sr, i, k)) return;
			if (i == 0) return;		/* (la colonne entiere : deja seule) */
		}
		for (int c = 0; c < NCOL; c++)		/* une colonne vide, si ca change quelque chose */
			if (s.col[c].n == 0) { dropOnCol (sr, i, c); return; }
	}

	bool hitSrc (long x, long y, int &sr, int &i) const
	{
		if (y >= l_top && y < l_top + c_h)	/* les cases */
		{
			for (int k = 0; k < 4; k++)
				if (x >= ck_col_x (k) && x < ck_col_x (k) + c_w)
				{
					if (s.cell[k] < 0) return false;
					sr = SRC_CELL + k; i = 0; return true;
				}
			return false;
		}
		for (int c = 0; c < NCOL; c++)
		{
			if (x < ck_col_x (c) || x >= ck_col_x (c) + c_w) continue;
			long d = dy (c);
			for (int k = s.col[c].n - 1; k >= 0; k--)	/* du sommet vers le bas */
			{
				long cy = l_tab + k * d;
				if (y < cy) continue;
				if (y >= cy + c_h) return false;
				sr = c; i = k; return true;
			}
			return false;
		}
		return false;
	}

	/* appui */
	void press (long x, long y)
	{
		if (win_on) { win_on = 0; paint (); return; }	/* l'animation : passe au message */
		if (won) { newGame (randomGame ()); return; }
		if (y < BAND_H)
		{
			long drag, b = band_hit (x, &drag);
			if (b >= 0) command (s_btn_ids[b]);
			else if (drag) ui_window_drag ();
			return;
		}
		unsigned long now = ck_time_ms ();	/* second appui d'un double-clic : deja parti */
		if (now - sentT < CK_TAP_MS && labs (sentX - x) < 16 && labs (sentY - y) < 16) return;
		int sr, i;
		if (!hitSrc (x, y, sr, i)) return;
		long cx, cy;
		if (sr < SRC_CELL)
		{
			if (!isRun (sr, i)) return;
			cx = ck_col_x (sr); cy = l_tab + i * dy (sr);
		}
		else { cx = ck_col_x (sr - SRC_CELL); cy = l_top; }
		offX = x - cx; offY = y - cy; pressX = x; pressY = y; src = sr; idx = i;
		pressed = true;
	}
	/* le doigt bouge */
	void move (long x, long y)
	{
		long cx = x - offX, cy = y - offY;	/* coin du paquet */
		if (dr_on) { drag_move (cx, cy); return; }
		if (labs (cx - (pressX - offX)) + labs (cy - (pressY - offY)) < DRAG_MIN) return;	/* encore un simple appui ? */
		dr_n = srcCount (src, idx);		/* le paquet quitte sa place */
		for (long k = 0; k < dr_n; k++) dr_cards[k] = (unsigned char) srcCard (src, idx + (int) k);
		dr_dy = c_dy_max; dr_x = cx; dr_y = cy; dr_on = 1;
		paint ();
	}
	/* relache */
	void release (long x, long y)
	{
		if (!pressed) return;
		pressed = false;
		if (!dr_on)
		{
			sentX = x; sentY = y; sentT = ck_time_ms ();
			clickSend (src, idx);
			paint ();
			return;
		}
		dr_on = 0;
		long cx = x - offX + (c_w >> 1), cy = y - offY + (c_h >> 1);	/* le centre de la carte du dessus */
		int c = (int) ck_col_at (cx, NCOL);
		if (cy < l_top + c_h + 10)		/* rangee du haut */
		{
			if (c < 4) dropOnCell (src, idx, c);
			else dropOnFound (src, idx);
		}
		else dropOnCol (src, idx, c);
		paint ();
	}
	void mouse ()
	{
		int kind = ui_ev_field (&ui, EV_KIND), code = ui_ev_field (&ui, EV_CODE);
		long x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
		if (kind == 1) { if (code & 1) press (x, y); }
		else if (kind == 2) release (x, y);
		else if (pressed) { if (code & 1) move (x, y); else release (x, y); }	/* (relache perdu) */
	}

	/* Partie > Choisir une partie */
	void selectGame ()
	{
		char ask[32];
		snprintf (ask, sizeof ask, "%u", game);
		if (!ui_ask ((long) TR (s_sel_t), (long) TR (s_sel_q), (long) ask, 16)) return;
		unsigned long n = parse_dec (ask);
		if (n == 0 || n > GAME_MAX) return;
		newGame ((unsigned) n);
	}

	/* ID_* de la barre de menus ou du bandeau */
	void command (long id)
	{
		switch (id)
		{
		case ID_NEW: newGame (randomGame ()); break;
		case ID_SELECT: selectGame (); break;
		case ID_RESTART: newGame (game); break;
		case ID_UNDO: doUndo (); break;
		case ID_QUIT: exit (0);
		}
	}

	/* le temps qui passe : animation de fin, message */
	void tick ()
	{
		if (win_on)
		{
			unsigned long now = ck_time_ms ();
			if (now - animT >= 16)
			{
				animT = now;
				if (!win_step ()) paint ();	/* finie : le message */
			}
		}
		if (msgT && ck_time_ms () - msgT >= MSG_MS) { msgT = 0; paint (); }
	}

	/* -> texte du bandeau, *col = sa couleur */
	const char *statusText (uint32_t *col)
	{
		if (msgT) { *col = TAPIS_NOTE; return TR (s_nomove); }
		char *p = tr_cat (txt, s_game);
		p += sprintf (p, "%u", game);
		p = tr_cat (p, s_moves);
		sprintf (p, "%u", (unsigned) s.moves);
		*col = TAPIS_TEXT;
		return txt;
	}

	/* la table, puis tout est montre */
	void paint ()
	{
		lay_begin ();
		uint32_t col;
		const char *t = statusText (&col);
		table_bg (t, col);
		for (int i = 0; i < 4; i++)		/* rangee du haut */
		{
			long x = ck_col_x (i);
			card_slot (x, l_top, -1);
			if (s.cell[i] >= 0 && !(dr_on && src == SRC_CELL + i)) card_blit (s.cell[i], x, l_top);	/* (glissee ?) */
		}
		for (int f = 0; f < 4; f++)
		{
			long x = ck_col_x (4 + f);
			if (s.found[f] < 0) card_slot (x, l_top, f);
			else card_blit (f * 13 + s.found[f], x, l_top);
		}
		for (int c = 0; c < NCOL; c++)		/* les colonnes */
		{
			long x = ck_col_x (c), d = dy (c);
			int n = s.col[c].n;
			if (dr_on && src == c && idx < n) n = idx;	/* (les cartes glissees n'y sont plus) */
			if (n == 0) { card_slot (x, l_tab, -1); continue; }
			for (int i = 0; i < n; i++) card_blit (s.col[c].c[i], x, l_tab + i * d);
		}
		if (won && !win_on) win_box (TR (s_won), TR (s_again));
		lay_end ();
		compose_all ();
	}

	void setSize (long w, long h)
	{
		layers_size (w, h);
		cards_layout (NCOL);
	}
};

static FreeCell fc;
static char g_btns[80];

/* g_btns <- les libelles du bandeau (s_btns), traduits. Le deuxieme, Annuler, a pour cle celle du menu :
 * seul, il dirait Cancel. */
static void band_text (void)
{
	char *d = g_btns;
	for (const char *s = s_btns; *s; s += strlen (s) + 1)
	{
		const char *t = TR (s);
		if (s == s_btns + 9)
		{
			t = TR (s_undo_k);
			if (t == s_undo_k) t = s;
		}
		d = stpcpy (d, t) + 1;
	}
	*d = 0;
}

/* largeur, hauteur : apres un redimensionnement */
static void on_resize (long w, long h)
{
	fc.setSize (w, h);
	fc.pressed = false;			/* (un glisser en cours est abandonne) */
	dr_on = 0;
	win_on = 0;				/* (l'animation aussi) */
	fc.paint ();
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui))
		return 1;
	long w = WIN_W, h = WIN_H;
	cards_fit (&w, &h);
	if (ui_window_ex (-1, -1, w, h, (long) s_title, WF_RESIZABLE | WF_GLYPH_LIGHT) < 0)
		return 0;
	ui_menu ((long) s_menus);
	ui_min_size (MIN_W, MIN_H);
	ui_on_resize ((long) on_resize, 0);
	cards_init ();
	band_text ();
	band_set (g_btns);
	fc.setSize (w, h);
	unsigned long n = 0;
	if (argc > 1 && (unsigned) (argv[1][0] - '1') <= 8)	/* numero donne ? */
	{
		n = parse_dec (argv[1]);
		if (n > GAME_MAX) n = 0;
	}
	fc.newGame (n ? (unsigned) n : FreeCell::randomGame ());
	for (;;)
	{
		long t = ui_event (win_on ? 16 : 500);
		fc.tick ();
		if (t == 0)
			continue;
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MENU)
			fc.command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_KEY)
		{
			if (ui_ev_field (&ui, EV_KIND) == 0)
				continue;
			long id = ui_menu_key (0);
			if (id >= 0)
				fc.command (id);
		}
		else if (t == WEV_MOUSE)
			fc.mouse ();
		else
			ui_w_event (0);
	}
	return 0;
}
