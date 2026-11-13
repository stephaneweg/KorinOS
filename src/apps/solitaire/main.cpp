/*
 * solitaire -- Klondike, la reussite classique. On glisse les cartes a la souris ou au doigt : sept colonnes
 * descendantes en couleurs alternees (un roi sur une colonne vide), quatre fondations montantes par enseigne
 * depuis l'as. Un toucher du talon retourne une carte (ou trois : Partie > Tirer trois cartes) ; vide, il
 * reprend le rebut. Un toucher sans glisser envoie la carte a la destination evidente ; Auto ou le bouton
 * droit monte tout ce qui peut monter. Les cartes cachees se retournent seules. Score a la Windows et
 * chronometre ; les cartes rebondissent a la fin. `solitaire <n>` : n seme la donne.
 *
 * Port en C++ depuis user/Apps/solitaire/main.cpp d'Onyx (le modele des piles, la donne, les regles, le score
 * de Windows et sa prime de temps, les noms des methodes) ; ce que Korin avait ajoute dans solitaire.asm est
 * garde tel quel : la table qui suit la taille de la fenetre et ses cartes (src/inc/cards.h, traduction de
 * cards.inc), le bandeau et ses boutons, le toucher qui envoie la carte, une fondation par enseigne, le
 * menu Partie, les textes et lang/. Un seul source pour le Pi et le PC ; l'asm reste dans le dossier jusqu'a
 * sa validation sur le Pi.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../inc/cards.h"

#define WIN_W		640
#define WIN_H		540
#define MIN_W		500
#define MIN_H		400
#define UNDO_MAX	64

enum { STOCK = 0, WASTE = 1, FOUND = 2, TAB = 6, NPILE = 13 };
enum { ID_NEW = 1, ID_UNDO, ID_AUTO, ID_DRAW1, ID_DRAW3, ID_QUIT };

static const char s_title[] = "Solitaire";
static const char s_score[] = "Score : ";
static const char s_time[] = "    Temps : ";
static const char s_sec[] = " s";
static const char s_again_g[] = "↻";
static const char s_won[] = "Gagné !";
static const char s_replay[] = "Toucher pour rejouer";
static const char s_btns[] = "Nouvelle\0Défaire\0Auto\0";
static const unsigned char s_btn_ids[3] = { ID_NEW, ID_UNDO, ID_AUTO };
static const char s_menus[] =
	"MPartie\n"
	"I1\tDistribuer\tCtrl+N\n"
	"I2\tAnnuler le coup\tCtrl+Z\n"
	"I3\tCartes vers les fondations\n"
	"-\n"
	"I4\tTirer une carte\n"
	"I5\tTirer trois cartes\n"
	"-\n"
	"I6\tQuitter\n";

struct Pile { int n; signed char c[52]; bool up[52]; };
struct State { Pile p[NPILE]; int score; };

/* chaine -> valeur decimale (parse_dec d'AppKit : les chiffres du debut) */
static unsigned long parse_dec (const char *s)
{
	unsigned long v = 0;
	while ((unsigned) (*s - '0') <= 9)
		v = v * 10 + (unsigned) (*s++ - '0');
	return v;
}

class Klondike
{
public:
	State s;
	State undo[UNDO_MAX]; int nundo;
	bool draw3;
	unsigned long t0, elapsed; bool running;
	bool won;
	long down, fan;					/* pas d'une carte cachee, rebut en eventail */
	bool pressed; long pressX, pressY, offX, offY; int src, idx;	/* l'appui en cours */
	unsigned long sentT; long sentX, sentY;		/* le dernier toucher qui a envoye une carte */
	unsigned long animT;
	char txt[96];

	long pileX (int p) const
	{
		if (p == STOCK || p == WASTE) return ck_col_x (p);
		if (p < TAB) return ck_col_x (p + 1);	/* fondations : colonnes 3 a 6 */
		return ck_col_x (p - TAB);
	}
	/* pas des cartes visibles d'une colonne (serre pour tenir) */
	long upDy (int p) const
	{
		const Pile &q = s.p[p];
		long up = 0;
		for (int i = 0; i < q.n; i++) up += q.up[i];
		long hidden = q.n - up, dy = c_dy_max;
		if (up < 2) return dy;
		long a = (g_h - 6 - l_tab - c_h - down * hidden) / (up - 1);
		if (a < dy) dy = a;
		if (dy < c_dy_min) dy = c_dy_min;
		return dy;
	}
	/* place de la carte i de la pile p */
	void cardPos (int p, int i, long &x, long &y) const
	{
		x = pileX (p);
		if (p < TAB)
		{
			y = l_top;
			if (p == WASTE && draw3)
			{
				int first = s.p[WASTE].n - 3 < 0 ? 0 : s.p[WASTE].n - 3;	/* premiere de l'eventail */
				if (i >= first) x += (i - first) * fan;
			}
			return;
		}
		y = l_tab;
		long dy = upDy (p);
		for (int k = 0; k < i; k++) y += s.p[p].up[k] ? dy : down;
	}

	void push (int p, int card, bool up) { Pile &q = s.p[p]; q.c[q.n] = (signed char) card; q.up[q.n] = up; q.n++; }

	/* la donne */
	void deal ()
	{
		signed char deck[52];
		for (int i = 0; i < 52; i++) deck[i] = (signed char) i;
		for (int i = 51; i > 0; i--) { int j = (int) ck_rnd_n (i + 1); signed char t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
		for (int i = 0; i < NPILE; i++) s.p[i].n = 0;
		int k = 0;
		for (int col = 0; col < 7; col++)
			for (int r = 0; r <= col; r++) push (TAB + col, deck[k++], r == col);
		while (k < 52) push (STOCK, deck[k++], false);
		s.score = 0; nundo = 0;
		elapsed = 0; running = false; won = false; pressed = false; dr_on = 0; win_on = 0;
		paint ();
	}

	void push_undo () { if (nundo == UNDO_MAX) { memmove (undo, undo + 1, sizeof undo[0] * (UNDO_MAX - 1)); nundo--; } undo[nundo++] = s; }
	void do_undo () { if (nundo && !won) { s = undo[--nundo]; addScore (-2); paint (); } }
	/* points (le score ne descend pas sous 0, a chaque ajout : comme l'asm) */
	void addScore (int v) { s.score += v; if (s.score < 0) s.score = 0; }
	void start_clock () { if (!running && !won) { t0 = ck_time_ms () - elapsed * 1000; running = true; } }	/* (reprend apres une pause de zero seconde) */

	int top (int p) const { return s.p[p].n ? s.p[p].c[s.p[p].n - 1] : -1; }

	/* sur la fondation de son enseigne (Onyx : n'importe laquelle) */
	bool canFound (int card) const
	{
		int t = top (FOUND + card_suit (card));
		if (t < 0) return card_rank (card) == 0;
		return card_rank (card) == card_rank (t) + 1;
	}
	bool canTab (int card, int p) const
	{
		int t = top (p);
		if (t < 0) return card_rank (card) == 12;
		if (!s.p[p].up[s.p[p].n - 1]) return false;
		return card_red (t) != card_red (card) && card_rank (card) + 1 == card_rank (t);
	}

	/* deplace les cartes [idx ..] de la pile from sur to (regles deja verifiees) */
	void doMove (int from, int i, int to)
	{
		push_undo ();
		Pile &a = s.p[from];
		for (int k = i; k < a.n; k++) push (to, a.c[k], true);
		a.n = i;
		/* le score de Windows */
		bool toF = to >= FOUND && to < TAB, fromF = from >= FOUND && from < TAB;
		if (from == WASTE && to >= TAB) addScore (5);
		if (toF && !fromF) addScore (10);
		if (fromF && to >= TAB) addScore (-15);
		if (from >= TAB && a.n && !a.up[a.n - 1]) { a.up[a.n - 1] = true; addScore (5); }	/* la carte cachee se retourne */
		start_clock ();
		checkWin ();
	}

	void checkWin ()
	{
		int n = 0; for (int f = FOUND; f < TAB; f++) n += s.p[f].n;
		if (n < 52) return;
		won = true; running = false;
		elapsed = (ck_time_ms () - t0) / 1000;
		if (elapsed > 30) addScore ((int) (700000 / elapsed / 10));	/* prime de temps */
		for (int f = 0; f < 4; f++) wa.fx[f] = pileX (FOUND + f);
		win_start (l_top);
		animT = ck_time_ms ();
	}

	/* le talon : une ou trois cartes, ou le rebut qui revient */
	void turnStock ()
	{
		if (won) return;
		Pile &st = s.p[STOCK], &wa_ = s.p[WASTE];
		if (st.n == 0)
		{
			if (wa_.n == 0) return;
			push_undo ();
			for (int i = wa_.n - 1; i >= 0; i--) push (STOCK, wa_.c[i], false);
			wa_.n = 0;
			addScore (draw3 ? -20 : -100);
		}
		else
		{
			push_undo ();
			for (int k = 0; k < (draw3 ? 3 : 1) && st.n; k++) { st.n--; push (WASTE, st.c[st.n], true); }
		}
		start_clock ();
		paint ();
	}

	/* le sommet de la pile p sur sa fondation */
	bool toFoundation (int p)
	{
		int c = top (p);
		if ((p >= FOUND && p < TAB) || c < 0 || !s.p[p].up[s.p[p].n - 1] || !canFound (c)) return false;
		doMove (p, s.p[p].n - 1, FOUND + card_suit (c));
		return true;
	}
	/* Auto : tout ce qui peut monter */
	void autoAll ()
	{
		bool any = true;
		while (any && !won)
		{
			any = false;
			if (toFoundation (WASTE)) any = true;
			for (int p = TAB; p < NPILE; p++) if (toFoundation (p)) any = true;
		}
		paint ();
	}

	/* quelle carte (pile, indice) est sous (x, y) ? indice -1 : la pile vide elle-meme */
	bool hit (long x, long y, int &pile, int &i) const
	{
		for (int p = 0; p < NPILE; p++)
		{
			for (int k = s.p[p].n - 1; k >= 0; k--)
			{
				long cx, cy; cardPos (p, k, cx, cy);
				if (x >= cx && x < cx + c_w && y >= cy && y < cy + c_h) { pile = p; i = k; return true; }
			}
			long px = pileX (p), py = p < TAB ? l_top : l_tab;
			if (x >= px && x < px + c_w && y >= py && y < py + c_h) { pile = p; i = -1; return true; }
		}
		return false;
	}

	/* le toucher sans glisser, vers la destination evidente : fondation (la carte du sommet), colonne
	 * (Onyx : le double clic, vers la fondation) */
	void clickSend (int p, int i)
	{
		int card = s.p[p].c[i];
		if (i == s.p[p].n - 1 && toFoundation (p)) return;
		for (int t = TAB; t < NPILE; t++)
		{
			if (t == p || !canTab (card, t)) continue;
			if (p >= TAB && i == 0 && s.p[t].n == 0) continue;	/* un roi deja au fond ne bouge pas */
			doMove (p, i, t);
			return;
		}
	}

	/* appui */
	void press (long x, long y)
	{
		if (win_on) { win_on = 0; paint (); return; }	/* l'animation : passe au message */
		if (won) { deal (); return; }
		if (y < BAND_H)
		{
			long drag, b = band_hit (x, &drag);
			if (b >= 0) command (s_btn_ids[b]);
			else if (drag) ui_window_drag ();
			return;
		}
		int p, i;
		if (!hit (x, y, p, i)) return;
		if (p == STOCK) { turnStock (); return; }
		if (i < 0) return;
		unsigned long now = ck_time_ms ();	/* second appui d'un double-clic : deja parti */
		if (now - sentT < CK_TAP_MS && labs (sentX - x) < 16 && labs (sentY - y) < 16) return;
		const Pile &q = s.p[p];
		if (p < TAB) i = q.n - 1;		/* rebut, fondation : le sommet seul */
		if (!q.up[i]) return;			/* (cachee : on n'y touche pas) */
		long cx, cy; cardPos (p, i, cx, cy);
		offX = x - cx; offY = y - cy; pressX = x; pressY = y; src = p; idx = i;
		pressed = true;
	}
	/* le doigt bouge */
	void move (long x, long y)
	{
		long cx = x - offX, cy = y - offY;	/* coin du paquet */
		if (dr_on) { drag_move (cx, cy); return; }
		if (labs (x - pressX) + labs (y - pressY) < DRAG_MIN) return;	/* encore un simple appui ? */
		const Pile &q = s.p[src];		/* le paquet quitte sa place */
		dr_n = q.n - idx;
		for (long k = 0; k < dr_n; k++) dr_cards[k] = (unsigned char) q.c[idx + k];
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
		int card = s.p[src].c[idx], n = s.p[src].n - idx;
		if (cy < l_top + c_h + 8)		/* rangee du haut : sa fondation */
		{
			int f = FOUND + card_suit (card);
			if (cx >= ck_col_x (3) - 8 && n == 1 && f != src && canFound (card)) doMove (src, idx, f);
		}
		else
		{
			int t = TAB + (int) ck_col_at (cx, 7);	/* la colonne sous le centre */
			if (t != src && canTab (card, t)) doMove (src, idx, t);
		}
		paint ();
	}
	void mouse ()
	{
		int kind = ui_ev_field (&ui, EV_KIND), code = ui_ev_field (&ui, EV_CODE);
		long x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
		if (kind == 1)
		{
			if (code & 1) press (x, y);
			else if ((code & 2) && !won) autoAll ();	/* bouton droit */
		}
		else if (kind == 2) release (x, y);
		else if (pressed) { if (code & 1) move (x, y); else release (x, y); }	/* (relache perdu) */
	}

	/* ID_* de la barre de menus ou du bandeau */
	void command (long id)
	{
		switch (id)
		{
		case ID_NEW: deal (); break;
		case ID_UNDO: do_undo (); break;
		case ID_AUTO: autoAll (); break;
		case ID_DRAW1:
		case ID_DRAW3:
			draw3 = id == ID_DRAW3;
			ui_menu_state (ID_DRAW3, draw3 ? UMS_CHECKED : 0);
			ui_menu_state (ID_DRAW1, draw3 ? 0 : UMS_CHECKED);
			deal ();
			break;
		case ID_QUIT: exit (0);
		}
	}

	/* -> texte du bandeau */
	const char *statusText ()
	{
		char *p = stpcpy (txt, TR (s_score));
		p += sprintf (p, "%u", (unsigned) s.score);
		p = stpcpy (p, TR (s_time));
		p += sprintf (p, "%lu", elapsed);
		strcpy (p, s_sec);
		return txt;
	}

	/* le temps qui passe : animation de fin, chronometre */
	void tick ()
	{
		unsigned long now = ck_time_ms ();
		if (win_on)
		{
			if (now - animT < 16) return;
			animT = now;
			if (!win_step ()) paint ();	/* finie : le message */
			return;
		}
		if (!running) return;
		unsigned long e = (now - t0) / 1000;
		if (e == elapsed) return;
		elapsed = e;
		lay_begin ();
		band_draw (statusText (), TAPIS_TEXT);
		lay_end ();
		compose (0, 0, g_w, BAND_H);
	}

	/* la table, puis tout est montre */
	void paint ()
	{
		lay_begin ();
		table_bg (statusText (), TAPIS_TEXT);
		long x = pileX (STOCK);			/* le talon */
		int n = s.p[STOCK].n;
		if (n)
		{
			if (n > 1) card_blit (CARD_BACK, x, l_top - 2);	/* (une epaisseur) */
			card_blit (CARD_BACK, x, l_top);
		}
		else
		{
			card_slot (x, l_top, -1);
			if (s.p[WASTE].n)		/* rien que le signe : on reprend le rebut */
			{
				long size = c_h >> 1;
				long tw = ui_text_width ((long) s_again_g, size, (long) ui.font_b);
				long y = l_top + ((c_h + (size >> 1)) >> 1) + (size >> 2);
				ui_text ((long) s_again_g, x + ((c_w - tw) >> 1), y, TAPIS_SLOT_L, size, (long) ui.font_b, 0);
			}
		}
		card_slot (pileX (WASTE), l_top, -1);	/* les places vides */
		for (int f = 0; f < 4; f++) card_slot (pileX (FOUND + f), l_top, f);
		for (int t = 0; t < 7; t++) card_slot (ck_col_x (t), l_tab, -1);
		for (int p = WASTE; p < NPILE; p++)	/* les cartes */
		{
			const Pile &q = s.p[p];
			int end = dr_on && src == p ? idx : q.n;	/* (les cartes glissees n'y sont plus) */
			int from = 0;			/* premiere carte visible */
			if (p == WASTE) from = q.n - (draw3 ? 3 : 1);
			else if (p < TAB) from = q.n - 2;
			if (from < 0) from = 0;
			for (int i = from; i < end; i++)
			{
				long cx, cy; cardPos (p, i, cx, cy);
				card_blit (q.up[i] ? q.c[i] : CARD_BACK, cx, cy);
			}
		}
		if (won && !win_on) win_box (s_won, s_replay);
		lay_end ();
		compose_all ();
	}

	void setSize (long w, long h)
	{
		layers_size (w, h);
		cards_layout (7);
		down = c_h >> 4 < 3 ? 3 : c_h >> 4;	/* pas d'une carte cachee */
		fan = c_w * 7 >> 5;			/* rebut en eventail */
	}
};

static Klondike ks;

/* largeur, hauteur : apres un redimensionnement */
static void on_resize (long w, long h)
{
	ks.setSize (w, h);
	ks.pressed = false;			/* (un glisser en cours est abandonne) */
	dr_on = 0;
	win_on = 0;				/* (l'animation aussi) */
	ks.paint ();
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
	ui_menu_state (ID_DRAW1, UMS_CHECKED);
	ui_min_size (MIN_W, MIN_H);
	ui_on_resize ((long) on_resize, 0);
	cards_init ();
	band_set (s_btns);
	ks.setSize (w, h);
	if (argc > 1 && (unsigned) (argv[1][0] - '0') <= 9)	/* graine donnee ? */
		ck_seed = (uint32_t) parse_dec (argv[1]);
	ks.deal ();
	for (;;)
	{
		long t = ui_event (win_on ? 16 : ks.running ? 250 : 1000);
		ks.tick ();
		if (t == 0)
			continue;
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MENU)
			ks.command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_KEY)
		{
			if (ui_ev_field (&ui, EV_KIND) == 0)
				continue;
			if (ui_ev_field (&ui, EV_CHAR) == ' ')
			{
				ks.turnStock ();
				continue;
			}
			long id = ui_menu_key (0);
			if (id >= 0)
				ks.command (id);
		}
		else if (t == WEV_MOUSE)
			ks.mouse ();
		else
			ui_w_event (0);
	}
	return 0;
}
