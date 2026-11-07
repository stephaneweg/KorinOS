//
// budget/grid.h -- la grille editable de Budget mensuel, sur une ui_view, a la maniere du tableur (src/apps/sheet,
// grid.h) : en-tetes de colonnes et de lignes, cellule courante, saisie dans la cellule (une ligne d'edition dessinee
// par la vue), colonnes redimensionnables, ligne nouvelle « * » en bas, completion des libelles.
//
// Clavier, comme DataGridView / Excel :
//   fleches, Debut, Fin, Ctrl+Debut / Fin, Page      se deplacer
//   Tab / Maj+Tab, Entree / Maj+Entree              cellule suivante / precedente, ligne suivante / precedente
//   une touche, F2, Retour arriere                  saisir (F2 : garder le texte, les fleches restent dans la cellule)
//   Entree, Tab, haut / bas                          valider la saisie ; Echap : l'abandonner
//   Espace                                          cocher une case ; Suppr : vider la cellule
//   Inser : inserer une ligne au-dessus ; Ctrl+Suppr : supprimer la ligne ; Ctrl+C / Ctrl+V : copier, coller (Excel)
//   Menu, Maj+F10 : le menu de la ligne
// Souris : clic (case : cocher), double-clic : saisir, bouton droit : le menu, bord d'un en-tete : largeur, molette.
//
#ifndef BUDGET_GRID_H
#define BUDGET_GRID_H

#include <string>
#include <vector>
#include <uikit.h>

// ---- le theme : les jetons du bureau (ui_jetons), comme Notes -----------------------------------------------
static struct ui_vars ui;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline unsigned mix (unsigned a, unsigned b, int n) { return (unsigned) ui_mix (a, b, n); }
#define C_BG	jet (J_FACE)
#define C_FIELD	jet (J_SURFACE)
#define C_TEXT	jet (J_TEXTE1)
#define C_DIM	jet (J_TEXTE3)
#define C_LINE	jet (J_LIGNE)
#define C_ACCENT jet (J_ACCENT)
#define C_RED	jet (J_DANGER)
#define C_GREEN	jet (J_SUCCES)
#define C_ORANGE jet (J_ALERTE)

static const int PX = 13;
static long font_of (int bold) { return bold ? (long) ui.font_b : (long) ui.font; }
static int  text_w (const char *s, int bold = 0, int px = PX) { return (int) ui_text_width ((long) s, px, font_of (bold)); }
static void text_at (int x, int y, int h, const char *s, unsigned c, int bold = 0, int px = PX) { ui_text ((long) s, x, y + h / 2 + px * 37 / 100, c, px, font_of (bold), 0); }
static void text_fit (int x, int y, int w, int h, const char *s, unsigned c, int bold = 0) { if (w > 8) ui_text_fit ((long) s, x, y + h / 2 + PX * 37 / 100, c, PX, font_of (bold), w); }
static void rbox (int x, int y, int w, int h, int r, unsigned c) { ui_rbox (x, y, w, h, r, c, c); }
static int ctl_h () { int h = (int) ui_metric (UMT_CTL); return h < 24 ? 24 : h; }

// les touches (codes HID)
enum { K_ENTER = 0x28, K_ESC = 0x29, K_BACK = 0x2A, K_TAB = 0x2B, K_SPACE = 0x2C, K_F2 = 0x3B, K_F10 = 0x43, K_INSERT = 0x49,
       K_HOME = 0x4A, K_PGUP = 0x4B, K_DELETE = 0x4C, K_END = 0x4D, K_PGDN = 0x4E, K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51,
       K_UP = 0x52, K_MENU = 0x65, K_C = 0x06, K_V = 0x19, K_A = 0x04, K_KPENTER = 0x58 };

static int u8_enc (unsigned cp, char *b)
{
	if (cp < 0x80) { b[0] = (char) cp; return 1; }
	if (cp < 0x800) { b[0] = (char) (0xC0 | cp >> 6); b[1] = (char) (0x80 | (cp & 63)); return 2; }
	if (cp < 0x10000) { b[0] = (char) (0xE0 | cp >> 12); b[1] = (char) (0x80 | (cp >> 6 & 63)); b[2] = (char) (0x80 | (cp & 63)); return 3; }
	b[0] = (char) (0xF0 | cp >> 18); b[1] = (char) (0x80 | (cp >> 12 & 63)); b[2] = (char) (0x80 | (cp >> 6 & 63)); b[3] = (char) (0x80 | (cp & 63)); return 4;
}
// minuscules sans accents, pour comparer des libelles (ASCII et Latin-1 en UTF-8)
static std::string fold (const std::string &s)
{
	std::string r;
	for (size_t i = 0; i < s.size (); i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c == 0xC3 && i + 1 < s.size ())
		{
			unsigned char d = (unsigned char) s[++i] | 0x20;		// (majuscules -> minuscules)
			const char *map = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";	// 0xA0..0xBF
			r += d >= 0xA0 && d <= 0xBF ? map[d - 0xA0] : '?';
			continue;
		}
		r += (char) (c >= 'A' && c <= 'Z' ? c + 32 : c);
	}
	return r;
}

// ---- les colonnes, le style d'une cellule, ce que la grille demande au programme ------------------------------
enum ColKind { CK_TEXT, CK_MONEY, CK_INT, CK_DATE, CK_CHECK };
struct GCol
{
	std::string title;
	int w;				// largeur en px (celle de l'utilisateur)
	ColKind kind;
	bool ro;			// calculee
	bool flex;			// prend la place qui reste (le libelle)
	const char *tip;
};
struct CellStyle { unsigned fg; bool bold; bool hasBg; unsigned bg; };

struct Grid;
struct GridModel
{
	virtual ~GridModel () {}
	virtual int rows () = 0;
	virtual bool can_add () { return true; }
	virtual std::string text (int r, int c) = 0;			// affiche
	virtual std::string edit_text (int r, int c) { return text (r, c); }
	// la saisie d'une cellule (r == rows () : une ligne nouvelle) -> 0, ou le message d'erreur
	virtual const char *set (int r, int c, const std::string &t) = 0;
	virtual bool checked (int r, int c) { (void) r; (void) c; return false; }
	virtual void toggle (int r, int c) { (void) r; (void) c; }
	virtual void clear (int r, int c) { set (r, c, ""); }
	virtual void style (int r, int c, CellStyle &s) { (void) r; (void) c; (void) s; }
	virtual bool locked (int r) { (void) r; return false; }	// ligne calculee : ni saisie, ni effacee, ni supprimee
	virtual void insert_row (int r) { (void) r; }
	virtual void delete_row (int r) { (void) r; }
	virtual void context (Grid *g, int r, int c, int wx, int wy) { (void) g; (void) r; (void) c; (void) wx; (void) wy; }
	virtual void complete (int c, std::vector<std::string> &out) { (void) c; (void) out; }	// libelles connus d'une colonne
	virtual void message (const char *s) { (void) s; }
	virtual void widths_changed () {}
	virtual void focus_out (int dir) { (void) dir; }	// Ctrl+Tab : au widget suivant
};

struct Grid
{
	GridModel *m = 0;
	std::vector<GCol> cols;
	long view = 0;
	int x = 0, y = 0, w = 10, h = 10;		// place dans la fenetre
	int curR = 0, curC = 0, top = 0, sx = 0;	// cellule courante, premiere ligne montree, defilement horizontal
	bool readOnly = false, rowHeaders = true;
	// la saisie
	bool editing = false, typed = false, bad = false;
	std::string et;					// le texte
	int caret = 0, anchor = 0, er = 0, ec = 0;
	std::vector<std::string> ac;			// completion : propositions
	int acSel = -1;
	// la souris
	int dragCol = -1, dragX0 = 0, dragW0 = 0, rightR = -2, rightC = -1, hoverCursor = 0;

	int rowh () const { int r = (int) ui_metric (UMT_ROW); return r < 22 ? 22 : r; }
	int hdrh () const { int r = (int) ui_metric (UMT_HDR); return r < 22 ? 22 : r; }
	int rhw () const { return rowHeaders ? 34 : 0; }
	int nrows () const { return m->rows () + (m->can_add () && !readOnly ? 1 : 0); }
	bool ro_at (int r, int c) const { return cols[(size_t) c].ro || (r < m->rows () && m->locked (r)); }
	int body_h () const { return h - hdrh () - (total_w () > w - rhw () ? 6 : 0); }
	int vis_rows () const { int n = body_h () / rowh (); return n < 1 ? 1 : n; }
	// les largeurs montrees : la colonne souple prend ce qui reste
	int col_w (int c) const
	{
		const GCol &k = cols[(size_t) c];
		if (!k.flex) return k.w;
		int fixed = 0; for (auto &o : cols) if (!o.flex) fixed += o.w;
		int rest = w - rhw () - 8 - fixed;
		return rest > k.w ? rest : k.w;
	}
	int total_w () const { int t = 0; for (int c = 0; c < (int) cols.size (); c++) t += col_w (c); return t; }
	int col_x (int c) const { int xx = rhw () - sx; for (int i = 0; i < c; i++) xx += col_w (i); return xx; }

	void dirty () { if (view) { ui_view_dirty (view); ui_view_flush (); } }
	void place (int x_, int y_, int w_, int h_) { x = x_; y = y_; w = w_; h = h_; ui_w_move (view, x, y, w, h); clamp (); ui_view_dirty (view); }
	void clamp ()
	{
		int n = nrows ();
		if (curR >= n) curR = n - 1;
		if (curR < 0) curR = 0;
		if (curC >= (int) cols.size ()) curC = (int) cols.size () - 1;
		if (curC < 0) curC = 0;
		int vr = vis_rows ();
		if (top > n - vr) top = n - vr;
		if (top < 0) top = 0;
		int most = total_w () - (w - rhw ());
		if (sx > most) sx = most;
		if (sx < 0) sx = 0;
	}
	void ensure_visible ()
	{
		clamp ();
		int vr = vis_rows ();
		if (curR < top) top = curR;
		if (curR >= top + vr) top = curR - vr + 1;
		int cx = col_x (curC) + sx, cw = col_w (curC), vw = w - rhw ();
		if (cx - rhw () < sx) sx = cx - rhw ();
		if (cx - rhw () + cw > sx + vw) sx = cx - rhw () + cw - vw;
		clamp ();
	}
	void go (int r, int c)
	{
		if (editing && !commit ()) return;
		curR = r; curC = c;
		ensure_visible ();
		dirty ();
	}
	void reset () { editing = false; ac.clear (); top = 0; sx = 0; curR = 0; curC = 0; bad = false; }

	// ---- la saisie
	void begin_edit (bool typed_, const char *initial)
	{
		if (readOnly || editing || ro_at (curR, curC) || cols[(size_t) curC].kind == CK_CHECK) return;
		if (curR >= nrows ()) return;
		editing = true; typed = typed_; bad = false; er = curR; ec = curC;
		et = initial ? initial : (curR < m->rows () ? m->edit_text (curR, curC) : std::string ());
		caret = anchor = (int) et.size ();
		ac.clear (); acSel = -1;
		if (initial && *initial) suggest ();
		ensure_visible ();
		dirty ();
	}
	void cancel_edit () { if (!editing) return; editing = false; bad = false; ac.clear (); m->message (""); dirty (); }
	bool commit ()
	{
		if (!editing) return true;
		if (acSel >= 0 && acSel < (int) ac.size ()) et = ac[(size_t) acSel];
		const char *err = m->set (er, ec, et);
		if (err) { bad = true; ac.clear (); m->message (err); dirty (); return false; }
		editing = false; bad = false; ac.clear ();
		m->message ("");
		clamp ();
		dirty ();
		return true;
	}
	int s0 () const { return caret < anchor ? caret : anchor; }
	int s1 () const { return caret < anchor ? anchor : caret; }
	void replace_sel (const std::string &s) { et = et.substr (0, (size_t) s0 ()) + s + et.substr ((size_t) s1 ()); caret = anchor = s0 () + (int) s.size (); }
	int prev (int i) const { if (i <= 0) return 0; i--; while (i > 0 && ((unsigned char) et[(size_t) i] & 0xC0) == 0x80) i--; return i; }
	int next (int i) const { int n = (int) et.size (); if (i >= n) return n; i++; while (i < n && ((unsigned char) et[(size_t) i] & 0xC0) == 0x80) i++; return i; }
	// la completion (SuggestAppend) : la fin du premier libelle connu, choisie ; la liste des autres sous la cellule
	void suggest ()
	{
		ac.clear (); acSel = -1;
		if (cols[(size_t) ec].kind != CK_TEXT || caret != (int) et.size () || et.empty ()) return;
		std::vector<std::string> all;
		m->complete (ec, all);
		std::string p = fold (et);
		for (auto &s : all)
		{
			if (s.size () <= et.size () || fold (s).compare (0, p.size (), p)) continue;
			bool dup = false; for (auto &a : ac) if (fold (a) == fold (s)) dup = true;
			if (!dup) ac.push_back (s);
			if (ac.size () >= 6) break;
		}
		if (!ac.empty ())
		{
			int typedLen = (int) et.size ();
			et = et + ac[0].substr ((size_t) typedLen);
			anchor = typedLen; caret = (int) et.size ();
		}
	}

	// ---- le dessin
	void paint (int vw, int vh)
	{
		(void) vw; (void) vh;
		int rh = rowh (), hh = hdrh (), rw = rhw ();
		unsigned head = mix (C_FIELD, C_TEXT, 16), grid = mix (C_FIELD, C_TEXT, 28), ro = mix (C_FIELD, C_TEXT, 7);
		bool focus = ui_w_get_focus () == view;
		ui_fill (0, 0, w, h, C_FIELD);
		int n = nrows (), nd = m->rows ();
		int bodyBottom = hh + body_h ();
		// les lignes
		ui_clip (rw, hh, w - rw, bodyBottom - hh);
		for (int r = top; r < n; r++)
		{
			int yy = hh + (r - top) * rh;
			if (yy >= bodyBottom) break;
			for (int c = 0; c < (int) cols.size (); c++)
			{
				int cx = col_x (c), cw = col_w (c);
				if (cx + cw <= rw || cx >= w) continue;
				const GCol &k = cols[(size_t) c];
				CellStyle st { C_TEXT, false, false, 0 };
				if (r < nd) m->style (r, c, st);
				unsigned bg = st.hasBg ? st.bg : k.ro || (r < nd && m->locked (r)) ? ro : C_FIELD;
				bool cur = r == curR && c == curC && !readOnly;
				if (r == curR && !readOnly) bg = mix (bg, C_ACCENT, focus ? 22 : 12);
				ui_fill (cx, yy, cw, rh, bg);
				ui_fill (cx + cw - 1, yy, 1, rh, grid);
				ui_fill (cx, yy + rh - 1, cw, 1, grid);
				if (r >= nd) continue;
				if (k.kind == CK_CHECK)
				{
					int b = 16, bx = cx + (cw - b) / 2, by = yy + (rh - b) / 2;
					bool on = m->checked (r, c);
					if (on) { rbox (bx, by, b, b, 3, C_ACCENT); text_at (bx + 3, by, b, "\xE2\x9C\x93", 0xFFFFFF, 1); }
					else { rbox (bx, by, b, b, 3, mix (C_FIELD, C_TEXT, 110)); rbox (bx + 1, by + 1, b - 2, b - 2, 2, C_FIELD); }
					continue;
				}
				if (editing && r == er && c == ec) continue;
				std::string t = m->text (r, c);
				if (t.empty ()) continue;
				bool right = k.kind == CK_MONEY, center = k.kind == CK_INT;
				int tw = text_w (t.c_str (), st.bold);
				int tx = right ? cx + cw - 6 - tw : center ? cx + (cw - tw) / 2 : cx + 6;
				if (tw > cw - 12) text_fit (cx + 6, yy, cw - 12, rh, t.c_str (), st.fg, st.bold);
				else text_at (tx, yy, rh, t.c_str (), st.fg, st.bold);
				(void) cur;
			}
		}
		// la cellule courante, la saisie
		if (!readOnly && curR >= top && curR < n)
		{
			int yy = hh + (curR - top) * rh, cx = col_x (curC), cw = col_w (curC);
			if (yy < bodyBottom)
			{
				unsigned edge = bad ? C_RED : focus ? C_ACCENT : mix (C_FIELD, C_ACCENT, 140);
				if (editing && curR == er && curC == ec)
				{
					ui_fill (cx, yy, cw, rh, C_FIELD);
					int pre = (int) ui_text_prefix ((long) et.c_str (), caret, PX, font_of (0));
					int ox = pre > cw - 14 ? pre - (cw - 14) : 0;
					ui_clip (cx + 2, yy, cw - 4, rh);
					if (s0 () != s1 ())
					{
						int a = (int) ui_text_prefix ((long) et.c_str (), s0 (), PX, font_of (0)), b = (int) ui_text_prefix ((long) et.c_str (), s1 (), PX, font_of (0));
						ui_fill (cx + 5 - ox + a, yy + 4, b - a, rh - 8, mix (C_FIELD, C_ACCENT, 70));
					}
					text_at (cx + 5 - ox, yy, rh, et.c_str (), C_TEXT);
					if (focus) ui_fill (cx + 5 - ox + pre, yy + 4, 1, rh - 8, C_TEXT);
					ui_clip (rw, hh, w - rw, bodyBottom - hh);
				}
				ui_fill (cx, yy, cw, 2, edge); ui_fill (cx, yy + rh - 2, cw, 2, edge);
				ui_fill (cx, yy, 2, rh, edge); ui_fill (cx + cw - 2, yy, 2, rh, edge);
			}
		}
		ui_clip (0, 0, 0, 0);
		// en-tetes des lignes
		if (rw)
		{
			ui_fill (0, hh, rw, bodyBottom - hh, head);
			ui_clip (0, hh, rw, bodyBottom - hh);
			for (int r = top; r < n; r++)
			{
				int yy = hh + (r - top) * rh;
				if (yy >= bodyBottom) break;
				char b[16]; if (r < nd) snprintf (b, sizeof b, "%d", r + 1); else snprintf (b, sizeof b, "*");
				if (r == curR && !readOnly) ui_fill (0, yy, rw, rh, mix (head, C_ACCENT, 60));
				text_at ((rw - text_w (b)) / 2, yy, rh, b, r == curR ? C_TEXT : C_DIM, r == curR);
				ui_fill (0, yy + rh - 1, rw, 1, grid);
			}
			ui_clip (0, 0, 0, 0);
			ui_fill (rw - 1, hh, 1, bodyBottom - hh, grid);
		}
		// en-tetes des colonnes
		ui_fill (0, 0, w, hh, head);
		ui_clip (rw, 0, w - rw, hh);
		for (int c = 0; c < (int) cols.size (); c++)
		{
			int cx = col_x (c), cw = col_w (c);
			const GCol &k = cols[(size_t) c];
			const char *t = k.title.c_str ();
			int tw = text_w (t, 1);
			bool cur = c == curC && !readOnly;
			if (cur) ui_fill (cx, 0, cw, hh, mix (head, C_ACCENT, 60));
			int tx = k.kind == CK_MONEY ? cx + cw - 6 - tw : k.kind == CK_CHECK || k.kind == CK_INT ? cx + (cw - tw) / 2 : cx + 6;
			if (tw > cw - 8) { ui_clip (cx > rw ? cx : rw, 0, cw - 2, hh); text_at (cx + 4, 0, hh, t, C_TEXT, 1); ui_clip (rw, 0, w - rw, hh); }
			else text_at (tx, 0, hh, t, C_TEXT, 1);
			ui_fill (cx + cw - 1, 4, 1, hh - 8, grid);
		}
		ui_clip (0, 0, 0, 0);
		ui_fill (0, hh - 1, w, 1, grid);
		// les barres de defilement (indicateurs)
		if (n > vis_rows ())
		{
			int track = body_h () - 4, th = track * vis_rows () / n; if (th < 16) th = 16;
			int ty = hh + 2 + (track - th) * top / (n - vis_rows () > 0 ? n - vis_rows () : 1);
			rbox (w - 6, ty, 4, th, 2, mix (C_FIELD, C_TEXT, 90));
		}
		if (total_w () > w - rw)
		{
			int track = w - rw - 4, tw = track * (w - rw) / total_w (), most = total_w () - (w - rw);
			int tx = rw + 2 + (track - tw) * sx / (most > 0 ? most : 1);
			rbox (tx, h - 5, tw, 4, 2, mix (C_FIELD, C_TEXT, 90));
		}
		if (focus) { ui_fill (0, 0, w, 1, C_ACCENT); }
		// la liste de la completion, sous la cellule
		if (editing && ac.size () > 1)
		{
			int yy = hh + (er - top + 1) * rh, cx = col_x (ec), lw = col_w (ec) + 60, ih = rh - 2;
			int lh = (int) ac.size () * ih + 4;
			if (yy + lh > h) yy = hh + (er - top) * rh - lh;
			if (cx + lw > w) cx = w - lw;
			rbox (cx, yy, lw, lh, 4, mix (C_FIELD, C_TEXT, 60));
			rbox (cx + 1, yy + 1, lw - 2, lh - 2, 4, C_FIELD);
			for (int i = 0; i < (int) ac.size (); i++)
			{
				int iy = yy + 2 + i * ih;
				if (i == acSel) ui_fill (cx + 2, iy, lw - 4, ih, mix (C_FIELD, C_ACCENT, 70));
				text_fit (cx + 8, iy, lw - 14, ih, ac[(size_t) i].c_str (), C_TEXT);
			}
		}
	}

	// ---- le clavier
	long key (long hid, long ch, long mods)
	{
		bool shift = mods & UI_MOD_SHIFT, ctrl = mods & UI_MOD_CTRL, alt = mods & UI_MOD_ALT;
		if (hid >= 0xE0) return -1;
		int n = nrows ();
		if (hid == K_KPENTER) hid = K_ENTER;
		if (editing)
		{
			if (ac.size () > 1 && (hid == K_DOWN || hid == K_UP))
			{
				int k = (int) ac.size ();
				acSel = hid == K_DOWN ? (acSel + 1) % k : (acSel <= 0 ? k - 1 : acSel - 1);
				dirty (); return 0;
			}
			switch (hid)
			{
			case K_ENTER: if (commit ()) go (shift ? curR - 1 : curR + 1 < nrows () ? curR + 1 : curR, curC); return 0;
			case K_TAB: if (commit ()) tab (shift); return 0;
			case K_ESC: if (!ac.empty () && anchor != caret) { et = et.substr (0, (size_t) s0 ()); caret = anchor = (int) et.size (); ac.clear (); dirty (); return 0; } cancel_edit (); return 0;
			case K_F2: typed = !typed; dirty (); return 0;
			case K_UP: case K_DOWN:
				if (commit ()) go (hid == K_UP ? curR - 1 : curR + 1 < nrows () ? curR + 1 : curR, curC);
				return 0;
			case K_LEFT: case K_RIGHT:
				if (typed && !ac.size ()) { if (commit ()) go (curR, hid == K_LEFT ? (curC > 0 ? curC - 1 : 0) : curC + 1 < (int) cols.size () ? curC + 1 : curC); return 0; }
				{
					int to = hid == K_LEFT ? (s0 () != s1 () && !shift ? s0 () : prev (caret)) : (s0 () != s1 () && !shift ? s1 () : next (caret));
					caret = to; if (!shift) anchor = to;
					ac.clear (); dirty (); return 0;
				}
			case K_HOME: caret = 0; if (!shift) anchor = 0; ac.clear (); dirty (); return 0;
			case K_END: caret = (int) et.size (); if (!shift) anchor = caret; ac.clear (); dirty (); return 0;
			case K_BACK:
				if (s0 () != s1 ()) replace_sel ("");
				else if (caret > 0) { int p = prev (caret); et.erase ((size_t) p, (size_t) (caret - p)); caret = anchor = p; }
				ac.clear (); bad = false; dirty (); return 0;
			case K_DELETE:
				if (s0 () != s1 ()) replace_sel ("");
				else if (caret < (int) et.size ()) { int q = next (caret); et.erase ((size_t) caret, (size_t) (q - caret)); }
				ac.clear (); bad = false; dirty (); return 0;
			}
			if (ctrl && hid == K_A) { anchor = 0; caret = (int) et.size (); dirty (); return 0; }
			if (ctrl && hid == K_V)
			{
				char b[512]; long k = ui_clip_get ((long) b, sizeof b);
				if (k > 0) { b[k < (long) sizeof b ? k : (long) sizeof b - 1] = 0; for (char *p = b; *p; p++) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' '; replace_sel (b); dirty (); }
				return 0;
			}
			if (ctrl && hid == K_C) { if (s0 () != s1 ()) ui_clip_set ((long) et.substr ((size_t) s0 (), (size_t) (s1 () - s0 ())).c_str (), s1 () - s0 ()); return 0; }
			if (ctrl || alt) return -1;
			if (ch >= 32 && ch != 127)
			{
				char b[5]; int k = u8_enc ((unsigned) ch, b); b[k] = 0;
				replace_sel (b); bad = false;
				suggest ();
				dirty (); return 0;
			}
			return 0;
		}
		if (readOnly)
		{
			if (hid == K_UP || hid == K_PGUP) { top -= hid == K_UP ? 1 : vis_rows (); clamp (); dirty (); return 0; }
			if (hid == K_DOWN || hid == K_PGDN) { top += hid == K_DOWN ? 1 : vis_rows (); clamp (); dirty (); return 0; }
			if (hid == K_TAB && ctrl) { m->focus_out (shift ? -1 : 1); return 0; }
			return -1;
		}
		int last = (int) cols.size () - 1;
		switch (hid)
		{
		case K_UP: go (curR - 1, curC); return 0;
		case K_DOWN: go (curR + 1 < n ? curR + 1 : curR, curC); return 0;
		case K_LEFT: go (curR, curC > 0 ? curC - 1 : 0); return 0;
		case K_RIGHT: go (curR, curC < last ? curC + 1 : last); return 0;
		case K_HOME: go (ctrl ? 0 : curR, 0); return 0;
		case K_END: go (ctrl ? n - 1 : curR, last); return 0;
		case K_PGUP: if (ctrl) return -1; go (curR - vis_rows (), curC); return 0;
		case K_PGDN: if (ctrl) return -1; go (curR + vis_rows () < n ? curR + vis_rows () : n - 1, curC); return 0;
		case K_ENTER:
			if (cols[(size_t) curC].kind == CK_CHECK && curR < m->rows () && !cols[(size_t) curC].ro && false) return 0;
			go (shift ? curR - 1 : curR + 1 < n ? curR + 1 : curR, curC); return 0;
		case K_TAB: if (ctrl) { m->focus_out (shift ? -1 : 1); return 0; } tab (shift); return 0;
		case K_F2: begin_edit (false, 0); return 0;
		case K_BACK: begin_edit (true, ""); return 0;
		case K_SPACE:
			if (cols[(size_t) curC].kind == CK_CHECK) { toggle_cur (); return 0; }
			break;
		case K_DELETE:
			if (ctrl) { if (curR < m->rows () && !m->locked (curR)) { m->delete_row (curR); clamp (); dirty (); } return 0; }
			if (curR < m->rows () && !ro_at (curR, curC)) { m->clear (curR, curC); dirty (); }
			return 0;
		case K_INSERT: if (!ctrl && !shift) { m->insert_row (curR); clamp (); dirty (); return 0; } break;
		case K_MENU: open_context (curR, curC); return 0;
		case K_F10: if (shift) { open_context (curR, curC); return 0; } break;
		case K_ESC: return -1;
		}
		if (ctrl && hid == K_C) { copy (); return 0; }
		if (ctrl && hid == K_V) { paste (); return 0; }
		if (ctrl || alt) return -1;
		if (ch > 32 && ch != 127)
		{
			if (cols[(size_t) curC].kind == CK_CHECK) return 0;
			char b[5]; int k = u8_enc ((unsigned) ch, b); b[k] = 0;
			begin_edit (true, b);
			return 0;
		}
		return -1;
	}
	void tab (bool back)
	{
		int last = (int) cols.size () - 1, r = curR, c = curC;
		do
		{
			if (back) { if (--c < 0) { c = last; if (--r < 0) { r = 0; c = 0; break; } } }
			else if (++c > last) { c = 0; if (++r >= nrows ()) { r = nrows () - 1; c = last; break; } }
		} while (cols[(size_t) c].ro);
		go (r, c);
	}
	void toggle_cur ()
	{
		if (curR >= m->rows () || ro_at (curR, curC)) return;
		m->toggle (curR, curC);
		dirty ();
	}
	void open_context (int r, int c)
	{
		int yy = hdrh () + (r - top + 1) * rowh (), xx = c >= 0 ? col_x (c) : 0;
		m->context (this, r, c, x + (xx > 0 ? xx : 0), y + (yy < h ? yy : h - 4));
	}
	void copy ()
	{
		if (curR >= m->rows ()) return;
		std::string t = m->edit_text (curR, curC);
		if (cols[(size_t) curC].kind == CK_CHECK) t = m->checked (curR, curC) ? "1" : "0";
		ui_clip_set ((long) t.c_str (), (long) t.size ());
	}
	// un bloc copie depuis Excel (lignes, tabulations) a partir de la cellule courante
	void paste ()
	{
		static char b[16384];
		long k = ui_clip_get ((long) b, sizeof b);
		if (k <= 0) return;
		b[k < (long) sizeof b ? k : (long) sizeof b - 1] = 0;
		std::string all (b);
		while (!all.empty () && (all.back () == '\n' || all.back () == '\r')) all.pop_back ();
		int r = curR, bads = 0;
		size_t a = 0;
		while (a <= all.size ())
		{
			size_t e = all.find ('\n', a); if (e == std::string::npos) e = all.size ();
			std::string line = all.substr (a, e - a);
			if (!line.empty () && line.back () == '\r') line.pop_back ();
			int c = curC; size_t p = 0;
			for (;;)
			{
				size_t q = line.find ('\t', p); if (q == std::string::npos) q = line.size ();
				if (c < (int) cols.size () && r <= m->rows () && !ro_at (r, c))
				{
					std::string cell = line.substr (p, q - p);
					if (cols[(size_t) c].kind == CK_CHECK)
					{
						std::string f = fold (trim_s (cell));
						bool on = f == "1" || f == "x" || f == "oui" || f == "vrai" || f == "true" || f == "ok";
						if (r < m->rows () && m->checked (r, c) != on) m->toggle (r, c);	// (une ligne nouvelle : la case attend)
					}
					else if (m->set (r, c, cell)) bads++;
				}
				c++;
				if (q >= line.size ()) break;
				p = q + 1;
			}
			r++;
			a = e + 1;
		}
		m->message (bads ? "Certaines valeurs coll\xC3\xA9" "es n'ont pas pu \xC3\xAA" "tre lues" : "");
		clamp (); dirty ();
	}
	static std::string trim_s (const std::string &s) { size_t a = 0, b = s.size (); while (a < b && (unsigned char) s[a] <= ' ') a++; while (b > a && (unsigned char) s[b - 1] <= ' ') b--; return s.substr (a, b - a); }

	// ---- la souris
	int col_at (int mx) const { for (int c = 0; c < (int) cols.size (); c++) { int cx = col_x (c); if (mx >= cx && mx < cx + col_w (c)) return c; } return -1; }
	int border_at (int mx) const	// le bord droit d'une colonne, a 4 px pres
	{
		for (int c = 0; c < (int) cols.size (); c++) { int e = col_x (c) + col_w (c); if (mx >= e - 4 && mx <= e + 3 && e > rhw ()) return c; }
		return -1;
	}
	void set_cursor (int c) { if (c != hoverCursor) { hoverCursor = c; ui_cursor (c); } }
	long mouse (long kind, long xl, long yl, long info)
	{
		int mx = (int) xl, my = (int) yl, clicks = (int) (info & 255), btn = (int) (info >> 8 & 255);
		int hh = hdrh (), rh = rowh ();
		if (kind == 3) { set_cursor (UC_ARROW); return 0; }
		if (kind == 0)
		{
			if (dragCol >= 0)
			{
				int nw = dragW0 + mx - dragX0; if (nw < 30) nw = 30; if (nw > 600) nw = 600;
				GCol &k = cols[(size_t) dragCol];
				if (k.flex) { int shown = col_w (dragCol); k.w = nw < shown ? nw : nw; }
				else k.w = nw;
				clamp (); dirty ();
				return 0;
			}
			set_cursor (my < hh && border_at (mx) >= 0 ? UC_HRESIZE : UC_ARROW);
			return 0;
		}
		if (kind == 2)
		{
			if (dragCol >= 0) { dragCol = -1; m->widths_changed (); return 0; }
			if (rightR > -2) { int r = rightR, c = rightC; rightR = -2; open_context (r, c); }
			return 0;
		}
		// un appui
		ui_w_focus (view);
		if (my < hh)
		{
			int b = border_at (mx);
			if (b >= 0 && (btn & 1)) { dragCol = b; dragX0 = mx; dragW0 = col_w (b); return 0; }
			int c = col_at (mx);
			if (c >= 0 && !readOnly) go (curR, c);
			return 0;
		}
		if (readOnly) return 0;
		int r = top + (my - hh) / rh;
		if (r >= nrows ()) r = nrows () - 1;
		int c = mx < rhw () ? -1 : col_at (mx);
		if (btn & 2) { go (r, c >= 0 ? c : curC); if (curR == r) { rightR = r; rightC = c; } return 0; }
		if (c < 0) { go (r, curC); return 0; }
		bool same = r == curR && c == curC;
		go (r, c);
		if (curR != r || curC != c) return 0;			// (la saisie d'avant est refusee)
		if (cols[(size_t) c].kind == CK_CHECK) { toggle_cur (); return 0; }
		if (clicks >= 2 || (same && !editing && false)) begin_edit (false, 0);
		return 0;
	}
	long wheel (long n) { top -= (int) n * 3; clamp (); dirty (); return 0; }
};

// les fonctions de la vue -> la grille
static Grid *g_grids[8];
static int g_ngrids;
static Grid *grid_of (long v) { for (int i = 0; i < g_ngrids; i++) if (g_grids[i]->view == v) return g_grids[i]; return 0; }
static long gv_paint (long v, long wl, long hl) { if (Grid *g = grid_of (v)) g->paint ((int) wl, (int) hl); return 0; }
static long gv_mouse (long v, long kind, long x, long y, long info) { if (Grid *g = grid_of (v)) return g->mouse (kind, x, y, info); return 0; }
static long gv_key (long v, long hid, long ch, long mods) { if (Grid *g = grid_of (v)) return g->key (hid, ch, mods); return -1; }
static long gv_wheel (long v, long n) { if (Grid *g = grid_of (v)) return g->wheel (n); return 0; }
static void grid_create (Grid &g, GridModel *m)
{
	g.m = m;
	g.view = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g.view, (long) gv_paint, (long) gv_mouse, (long) gv_key, (long) gv_wheel);
	bool known = false;
	for (int i = 0; i < g_ngrids; i++) if (g_grids[i] == &g) known = true;
	if (!known && g_ngrids < 8) g_grids[g_ngrids++] = &g;
}

#endif
