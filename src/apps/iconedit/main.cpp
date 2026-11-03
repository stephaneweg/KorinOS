//
// iconedit -- l'editeur d'icones (l'Icon Editor d'Onyx sur UIKit) : des icones BMP 24 bits, le magenta 0xFF00FF pour le
// transparent. La grille agrandie au milieu, les outils a gauche, la palette et les apercus (1x, 2x, sur clair et sur
// fonce) a droite. Bouton gauche : la 1re couleur, bouton droit : la 2e (X les echange : au toucher, il n'y a qu'un bouton).
// Outils : Crayon (P), Ligne (L), Cadre (R), Boite (B), Ellipse (O), Remplir (F), Pipette (K), Gomme (E). Ctrl+Z, Ctrl+Y,
// G la grille. Menu Image : retourner, tourner, decaler, vider. Tailles : 16, 24, 32, 40, 48, 64. Au clavier : les fleches
// bougent le pixel (Maj : de quatre), Entree dessine ; une forme commence a Entree, les fleches l'etirent, Entree la garde,
// Echap l'abandonne ; « , » et « . » changent de couleur. Un BMP ou un PNG s'ouvre (le PNG : son alpha devient le
// transparent), un fichier depose sur la fenetre aussi ; on enregistre en BMP.
//
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <uikit.h>
#define TR(s)	((const char *) ui_tr ((long) (s)))	// (data/lang-en.txt : le source est en francais)

static struct ui_vars ui;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3, J_ACCENT };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }

#define W_WANT	720
#define H_WANT	560
#define TOP	36			// le haut de la fenetre : elle s'y deplace
#define TOOL_W	84
#define SIDE_W	176
#define FOOT	30
#define MAXS	64
#define TRANSP	0x00FF00FFu
#define NUNDO	24
#define K_LEFT	0x50
#define K_RIGHT	0x4F
#define K_UP	0x52
#define K_DOWN	0x51
#define K_ENTER	0x28
#define K_ESC	0x29

enum { T_PEN, T_LINE, T_RECT, T_BOX, T_ELLIPSE, T_FILL, T_PICK, T_ERASE, NTOOL };
static const char *const TOOL_NAME[NTOOL] = { "Crayon", "Ligne", "Cadre", "Boîte", "Ellipse", "Remplir", "Pipette", "Gomme" };
static const char TOOL_KEY[NTOOL] = { 'p', 'l', 'r', 'b', 'o', 'f', 'k', 'e' };
enum { M_NEW40 = 1, M_NEW16, M_NEW24, M_NEW32, M_NEW48, M_NEW64, M_OPEN, M_SAVE, M_SAVEAS, M_QUIT, M_UNDO, M_REDO, M_SWAP, M_MORE,
       M_FLIPH, M_FLIPV, M_ROT, M_SL, M_SR, M_SU, M_SD, M_CLEAR, M_GRID };

static unsigned g_img[MAXS * MAXS], g_over[MAXS * MAXS], g_tmp[MAXS * MAXS];
static int      g_w = 40, g_h = 40;
static unsigned g_undo[NUNDO][MAXS * MAXS], g_redo[NUNDO][MAXS * MAXS];
static int      g_uw[NUNDO], g_uh[NUNDO], g_rw[NUNDO], g_rh[NUNDO], g_nundo, g_nredo;
static int      g_tool = T_PEN, g_prevTool = T_PEN, g_shownTool = -1;
static unsigned g_col[2] = { 0x00000000, TRANSP };	// la 1re, la 2e
static bool     g_gridOn = true, g_showOver, g_quit;
static char     g_path[300] = "SD0:/home/pictures/icon.bmp";
static unsigned g_saved;
static int      g_hx = -1, g_hy = -1;			// le pixel sous la souris (ou le clavier)
static long     g_vgrid, g_vprev, g_vpal, g_status, g_more, g_tools[NTOOL];

static const unsigned PALETTE[] = {
	0x00000000, 0x00404040, 0x00808080, 0x00C0C0C0, 0x00FFFFFF, 0x00800000, 0x00FF0000, 0x00FF8080,
	0x00804000, 0x00FF8000, 0x00FFC080, 0x00808000, 0x00FFFF00, 0x00FFFF80, 0x00008000, 0x0000FF00,
	0x0080FF80, 0x00008080, 0x0000FFFF, 0x0080FFFF, 0x00000080, 0x000000FF, 0x008080FF, 0x00400080,
	0x008000FF, 0x00C080FF, 0x00800040, 0x00FF0080, 0x00FF80C0, 0x00566074, 0x00303D4D, 0x005A6E88 };
#define NPAL ((int) (sizeof PALETTE / sizeof PALETTE[0]))
#define SW	20				// une pastille
#define COLS	8

static unsigned hash_img ()
{
	unsigned h = 2166136261u ^ (unsigned) (g_w * 131 + g_h);
	for (int i = 0; i < MAXS * MAXS; i++) h = (h ^ g_img[i]) * 16777619u;
	return h;
}
static bool changed () { return hash_img () != g_saved; }
static void refresh ();

static void push_undo ()
{
	if (g_nundo == NUNDO)
	{
		memmove (g_undo[0], g_undo[1], sizeof g_undo[0] * (NUNDO - 1));
		memmove (g_uw, g_uw + 1, sizeof g_uw[0] * (NUNDO - 1)); memmove (g_uh, g_uh + 1, sizeof g_uh[0] * (NUNDO - 1));
		g_nundo--;
	}
	memcpy (g_undo[g_nundo], g_img, sizeof g_img); g_uw[g_nundo] = g_w; g_uh[g_nundo] = g_h; g_nundo++;
	g_nredo = 0;
}
// ---- le dessin sur l'image -----------------------------------------------------------------------------------------
static void put (int x, int y, unsigned c, unsigned *dst = g_img) { if (x >= 0 && x < g_w && y >= 0 && y < g_h) dst[y * MAXS + x] = c; }
static void img_line (int x0, int y0, int x1, int y1, unsigned c, unsigned *dst = g_img)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
	for (;;)
	{
		put (x0, y0, c, dst);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x0 += sx; }
		if (e2 < dx) { err += dx; y0 += sy; }
	}
}
static void img_rect (int x0, int y0, int x1, int y1, unsigned c, bool fill, unsigned *dst = g_img)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
		if (fill || y == y0 || y == y1 || x == x0 || x == x1) put (x, y, c, dst);
}
static void img_ellipse (int x0, int y0, int x1, int y1, unsigned c, unsigned *dst = g_img)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	long cx2 = x0 + x1, cy2 = y0 + y1, rx2 = x1 - x0 + 1, ry2 = y1 - y0 + 1;
	auto inside = [&] (int x, int y) -> bool
	{
		long dx = 2 * x - cx2, dy = 2 * y - cy2;
		return dx * dx * ry2 * ry2 + dy * dy * rx2 * rx2 <= rx2 * rx2 * ry2 * ry2;
	};
	for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
		if (inside (x, y) && (!inside (x - 1, y) || !inside (x + 1, y) || !inside (x, y - 1) || !inside (x, y + 1))) put (x, y, c, dst);
}
static void img_fill (int x, int y, unsigned c)
{
	unsigned old = g_img[y * MAXS + x];
	if (old == c) return;
	static short stk[MAXS * MAXS * 4][2];
	int sp = 0;
	stk[sp][0] = (short) x; stk[sp][1] = (short) y; sp++;
	while (sp)
	{
		sp--;
		int px = stk[sp][0], py = stk[sp][1];
		if (px < 0 || px >= g_w || py < 0 || py >= g_h || g_img[py * MAXS + px] != old) continue;
		g_img[py * MAXS + px] = c;
		if (sp + 4 <= MAXS * MAXS * 4)
		{
			stk[sp][0] = (short) (px + 1); stk[sp][1] = (short) py; sp++;
			stk[sp][0] = (short) (px - 1); stk[sp][1] = (short) py; sp++;
			stk[sp][0] = (short) px; stk[sp][1] = (short) (py + 1); sp++;
			stk[sp][0] = (short) px; stk[sp][1] = (short) (py - 1); sp++;
		}
	}
}

// ---- la grille -----------------------------------------------------------------------------------------------------
static int  g_cell = 8, g_ox, g_oy;
static bool g_down, g_keyDrag; static int g_btn, g_sx, g_sy, g_lx, g_ly;
static void lay_cells (int w, int h)
{
	int s = g_w > g_h ? g_w : g_h;
	g_cell = (w < h ? w : h) / s;
	if (g_cell < 2) g_cell = 2;
	g_ox = (w - g_cell * g_w) / 2; g_oy = (h - g_cell * g_h) / 2;
}
static void check_cell (int px, int py, int s)
{
	int h = s / 2 > 0 ? s / 2 : 1;
	ui_fill (px, py, s, s, 0x00E8EBEF);
	ui_fill (px, py, h, h, 0x00C8CCD2);
	ui_fill (px + h, py + h, s - h, s - h, 0x00C8CCD2);
}
static long grid_paint (long, long w, long h)
{
	lay_cells ((int) w, (int) h);
	ui_fill (0, 0, w, h, jet (J_CREUX));
	const unsigned *src = g_showOver ? g_over : g_img;
	for (int y = 0; y < g_h; y++) for (int x = 0; x < g_w; x++)
	{
		unsigned v = src[y * MAXS + x];
		int px = g_ox + x * g_cell, py = g_oy + y * g_cell;
		if (v == TRANSP) check_cell (px, py, g_cell);
		else ui_fill (px, py, g_cell, g_cell, v);
	}
	if (g_gridOn && g_cell >= 5)
	{
		for (int x = 0; x <= g_w; x++) ui_fill (g_ox + x * g_cell, g_oy, 1, g_h * g_cell, (x % 8) ? 0x00A0A8B0 : 0x00707880);
		for (int y = 0; y <= g_h; y++) ui_fill (g_ox, g_oy + y * g_cell, g_w * g_cell, 1, (y % 8) ? 0x00A0A8B0 : 0x00707880);
	}
	if (g_hx >= 0) ui_frame (g_ox + g_hx * g_cell - 1, g_oy + g_hy * g_cell - 1, g_cell + 2, g_cell + 2, 0x00FFE040);
	ui_frame (g_ox - 1, g_oy - 1, g_w * g_cell + 2, g_h * g_cell + 2, jet (J_LIGNE_FORTE));
	return 0;
}
static bool to_img (int mx, int my, int &x, int &y)
{
	x = mx < g_ox ? -1 : (mx - g_ox) / g_cell; y = my < g_oy ? -1 : (my - g_oy) / g_cell;
	return x >= 0 && x < g_w && y >= 0 && y < g_h;
}
static void shape_preview (int x, int y)
{
	memcpy (g_over, g_img, sizeof g_over);
	unsigned c = g_col[g_btn];
	if (g_tool == T_LINE) img_line (g_sx, g_sy, x, y, c, g_over);
	else if (g_tool == T_RECT) img_rect (g_sx, g_sy, x, y, c, false, g_over);
	else if (g_tool == T_BOX) img_rect (g_sx, g_sy, x, y, c, true, g_over);
	else if (g_tool == T_ELLIPSE) img_ellipse (g_sx, g_sy, x, y, c, g_over);
	g_showOver = true;
}
static void pointer (int mx, int my, bool bl, bool br)
{
	int x, y;
	bool in = to_img (mx, my, x, y);
	int hx = in ? x : -1, hy = in ? y : -1;
	if (hx != g_hx || hy != g_hy) { g_hx = hx; g_hy = hy; refresh (); }
	if (!g_down && (bl || br) && in)
	{
		g_down = true; g_btn = bl ? 0 : 1; g_sx = g_lx = x; g_sy = g_ly = y;
		unsigned c = g_col[g_btn];
		switch (g_tool)
		{
		case T_PICK: g_col[g_btn] = g_img[y * MAXS + x]; g_tool = g_prevTool; g_down = false; refresh (); return;
		case T_FILL: push_undo (); img_fill (x, y, c); g_down = false; refresh (); return;
		case T_PEN: push_undo (); put (x, y, c); break;
		case T_ERASE: push_undo (); put (x, y, TRANSP); break;
		default: push_undo (); shape_preview (x, y); break;
		}
		refresh ();
		return;
	}
	if (g_down && (bl || br))
	{
		if (x < 0) x = 0;
		if (x >= g_w) x = g_w - 1;
		if (y < 0) y = 0;
		if (y >= g_h) y = g_h - 1;
		if (x == g_lx && y == g_ly) return;
		if (g_tool == T_PEN) img_line (g_lx, g_ly, x, y, g_col[g_btn]);
		else if (g_tool == T_ERASE) img_line (g_lx, g_ly, x, y, TRANSP);
		else shape_preview (x, y);
		g_lx = x; g_ly = y;
		refresh ();
		return;
	}
	if (g_down && !bl && !br)
	{
		g_down = false;
		if (g_showOver) { memcpy (g_img, g_over, sizeof g_over); g_showOver = false; }
		refresh ();
	}
}
static long grid_mouse (long, long kind, long x, long y, long info)
{
	if (kind == 3) { if (g_hx >= 0 && !g_keyDrag) { g_hx = g_hy = -1; refresh (); } return 0; }
	int b = (int) (info >> 8) & 0xFF;
	bool held = kind != 2;
	pointer ((int) x, (int) y, held && (b & 1), held && (b & 2));
	return 0;
}
// le clavier passe par pointer : un seul code (fleches, Entree, Echap)
static void at (int x, int y, bool bl) { pointer (g_ox + x * g_cell + g_cell / 2, g_oy + y * g_cell + g_cell / 2, bl, false); }
static long grid_key (long, long code, long ch, long mods)
{
	bool shift = (mods & UI_MOD_SHIFT) != 0;
	if (mods & (UI_MOD_CTRL | UI_MOD_ALT)) return -1;
	bool arrow = code == K_LEFT || code == K_RIGHT || code == K_UP || code == K_DOWN;
	if (arrow || code == K_ENTER)
	{
		if (g_hx < 0 || g_hx >= g_w || g_hy < 0 || g_hy >= g_h)
		{
			at (g_w / 2, g_h / 2, false);
			if (arrow) return 0;
		}
		int x = g_hx, y = g_hy;
		if (arrow)
		{
			int n = shift && !g_keyDrag && g_tool != T_PEN && g_tool != T_ERASE ? 4 : 1;
			bool run = shift && !g_keyDrag && (g_tool == T_PEN || g_tool == T_ERASE);
			int nx = x + (code == K_LEFT ? -n : code == K_RIGHT ? n : 0), ny = y + (code == K_UP ? -n : code == K_DOWN ? n : 0);
			nx = nx < 0 ? 0 : nx >= g_w ? g_w - 1 : nx; ny = ny < 0 ? 0 : ny >= g_h ? g_h - 1 : ny;
			if (run) { at (x, y, true); at (nx, ny, true); at (nx, ny, false); }
			else at (nx, ny, g_keyDrag);
			return 0;
		}
		if (g_keyDrag) { g_keyDrag = false; at (x, y, false); return 0; }
		at (x, y, true);
		if (g_down && (g_tool == T_LINE || g_tool == T_RECT || g_tool == T_BOX || g_tool == T_ELLIPSE)) g_keyDrag = true;
		else at (x, y, false);
		return 0;
	}
	if (code == K_ESC && g_keyDrag)
	{
		g_keyDrag = false; g_down = false; g_showOver = false;
		if (g_nundo > 0) g_nundo--;
		refresh ();
		return 0;
	}
	if (ch == ',' || ch == '.')
	{
		int i = NPAL;
		for (int j = 0; j < NPAL; j++) if (PALETTE[j] == g_col[0]) i = j;
		i = (i + (ch == ',' ? NPAL : 1)) % (NPAL + 1);
		g_col[0] = i == NPAL ? TRANSP : PALETTE[i];
		refresh ();
		return 0;
	}
	for (int i = 0; i < NTOOL; i++) if (ch == TOOL_KEY[i] || ch == TOOL_KEY[i] - 32)
	{
		if (i == T_PICK && g_tool != T_PICK) g_prevTool = g_tool;
		g_tool = i; refresh (); return 0;
	}
	if (ch == 'g' || ch == 'G') { g_gridOn = !g_gridOn; refresh (); return 0; }
	if (ch == 'x' || ch == 'X') { unsigned t = g_col[0]; g_col[0] = g_col[1]; g_col[1] = t; refresh (); return 0; }
	return -1;
}

// ---- les apercus, la palette -----------------------------------------------------------------------------------------
static void blit (int x0, int y0, int scale, unsigned bg)
{
	for (int y = 0; y < g_h; y++) for (int x = 0; x < g_w; x++)
	{
		unsigned v = g_img[y * MAXS + x];
		ui_fill (x0 + x * scale, y0 + y * scale, scale, scale, v == TRANSP ? bg : v);
	}
}
static long prev_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	int s2 = g_w <= 48 ? 2 : 1;
	unsigned ol = jet (J_LIGNE_FORTE);
	ui_frame (3, 3, g_w + 2, g_h + 2, ol);
	ui_frame (3 + g_w + 8, 3, g_w + 2, g_h + 2, ol);
	ui_frame (3, 3 + g_h + 8, g_w * s2 + 2, g_h * s2 + 2, ol);
	blit (4, 4, 1, 0x00F0F0F0);
	blit (4 + g_w + 8, 4, 1, 0x00202830);
	blit (4, 4 + g_h + 8, s2, 0x00303D4D);
	return 0;
}
static void swatch (int x, int y, int w, int h, unsigned c)
{
	if (c == TRANSP)
	{
		ui_fill (x, y, w, h, 0x00E8EBEF);
		ui_fill (x, y, w / 2, h / 2, 0x00C8CCD2);
		ui_fill (x + w / 2, y + h / 2, w - w / 2, h - h / 2, 0x00C8CCD2);
	}
	else ui_fill (x, y, w, h, c);
}
static long pal_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	for (int i = 0; i <= NPAL; i++)
	{
		int x = 1 + (i % COLS) * SW, y = 1 + (i / COLS) * SW;
		swatch (x, y, SW - 2, SW - 2, i == NPAL ? TRANSP : PALETTE[i]);
		ui_frame (x - 1, y - 1, SW, SW, jet (J_LIGNE_FORTE));
	}
	int by = (NPAL / COLS + 1) * SW + 8;
	for (int k = 1; k >= 0; k--)
	{
		int x = 8 + k * 14, y = by + k * 10;
		swatch (x, y, 24, 20, g_col[k]);
		ui_frame (x, y, 24, 20, jet (J_TEXTE2));
	}
	return 0;
}
static long pal_mouse (long, long kind, long mx, long my, long info)
{
	if (kind != 1) return 0;
	int b = (int) (info >> 8) & 0xFF;
	int i = (int) ((my - 1) / SW) * COLS + (int) ((mx - 1) / SW);
	if (mx < 1 || mx >= COLS * SW + 1 || i < 0 || i > NPAL) return 0;
	g_col[(b & 2) ? 1 : 0] = i == NPAL ? TRANSP : PALETTE[i];
	refresh ();
	return 0;
}

static void refresh ()
{
	ui_view_dirty (g_vgrid); ui_view_dirty (g_vprev); ui_view_dirty (g_vpal);
	if (g_tool != g_shownTool)			// l'outil choisi : le bouton accentue
	{
		for (int i = 0; i < NTOOL; i++)
			if (i == g_tool || i == g_shownTool) { ui_w_style (g_tools[i], i == g_tool ? UB_DEFAULT : 0); ui_w_draw (g_tools[i]); }
		g_shownTool = g_tool;
	}
	char t[420]; int n = snprintf (t, sizeof t, "%d x %d   %s", g_w, g_h, TR (TOOL_NAME[g_tool]));
	if (g_hx >= 0)
	{
		unsigned v = g_img[g_hy * MAXS + g_hx];
		if (v == TRANSP) n += snprintf (t + n, sizeof t - (size_t) n, TR ("   (%d, %d)  transparent"), g_hx, g_hy);
		else n += snprintf (t + n, sizeof t - (size_t) n, "   (%d, %d)  #%06X", g_hx, g_hy, v);
	}
	snprintf (t + n, sizeof t - (size_t) n, "   %s%s", g_path, changed () ? " *" : "");
	ui_w_set_text (g_status, (long) t);
}

// ---- les fichiers ------------------------------------------------------------------------------------------------
static bool save_bmp (const char *path)
{
	int row = (g_w * 3 + 3) & ~3, size = 54 + row * g_h;
	static unsigned char out[54 + MAXS * 4 * MAXS];
	memset (out, 0, (size_t) size);
	auto u32 = [&] (int at, unsigned v) { out[at] = (unsigned char) v; out[at + 1] = (unsigned char) (v >> 8); out[at + 2] = (unsigned char) (v >> 16); out[at + 3] = (unsigned char) (v >> 24); };
	out[0] = 'B'; out[1] = 'M'; u32 (2, (unsigned) size); u32 (10, 54); u32 (14, 40);
	u32 (18, (unsigned) g_w); u32 (22, (unsigned) g_h); out[26] = 1; out[28] = 24; u32 (34, (unsigned) (row * g_h));
	u32 (38, 2835); u32 (42, 2835);
	for (int y = 0; y < g_h; y++)
	{
		unsigned char *p = out + 54 + (g_h - 1 - y) * row;
		for (int x = 0; x < g_w; x++)
		{
			unsigned v = g_img[y * MAXS + x];
			p[x * 3] = (unsigned char) v; p[x * 3 + 1] = (unsigned char) (v >> 8); p[x * 3 + 2] = (unsigned char) (v >> 16);
		}
	}
	int f = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (f < 0 || write (f, out, (size_t) size) != size)
	{
		if (f >= 0) close (f);
		ui_message ((long) TR ("Enregistrer"), (long) TR ("Impossible d'écrire le fichier."), UM_OK);
		ui_w_draw (0);
		return false;
	}
	close (f);
	g_saved = hash_img ();
	return true;
}
static bool load_image (const char *path)
{
	long img = ui_image_load ((long) path);
	if (!img) { ui_message ((long) TR ("Ouvrir"), (long) TR ("Ce n'est pas une image BMP ou PNG."), UM_OK); ui_w_draw (0); return false; }
	long h = 0, px = 0, w = uikit_image_size (img, &h, &px);
	if (w > MAXS || h > MAXS || w <= 0 || h <= 0)
	{
		ui_image_free (img);
		ui_message ((long) TR ("Ouvrir"), (long) TR ("Une icône fait au plus 64 x 64 pixels."), UM_OK);
		ui_w_draw (0);
		return false;
	}
	const unsigned *p = (const unsigned *) px;
	g_w = (int) w; g_h = (int) h;
	for (int i = 0; i < MAXS * MAXS; i++) g_img[i] = TRANSP;
	for (int y = 0; y < g_h; y++) for (int x = 0; x < g_w; x++)
	{
		unsigned v = p[y * g_w + x];
		g_img[y * MAXS + x] = (v >> 24) < 128 ? TRANSP : v & 0x00FFFFFF;
	}
	ui_image_free (img);
	g_nundo = g_nredo = 0;
	strncpy (g_path, path, sizeof g_path - 1);
	g_saved = hash_img ();
	return true;
}
static void on_save () { if (save_bmp (g_path)) refresh (); }
static void on_save_as ()
{
	static char p[300];
	const char *b = strrchr (g_path, '/');
	long r = ui_file_save ((long) "SD0:/home/pictures", (long) (b ? b + 1 : "icon.bmp"), (long) p, sizeof p);
	ui_w_draw (0);
	if (r == 1) { strncpy (g_path, p, sizeof g_path - 1); on_save (); }
}
// des changements a garder ? -> false : la commande est abandonnee
static bool confirm ()
{
	if (!changed ()) return true;
	char t[340]; const char *b = strrchr (g_path, '/');
	snprintf (t, sizeof t, TR ("Enregistrer les changements de %s ?"), b ? b + 1 : g_path);
	long r = ui_message ((long) TR ("Modifications"), (long) t, UM_YESNOCANCEL);
	ui_w_draw (0);
	if (r == 0) return false;
	if (r == 1) { on_save (); return !changed (); }
	return true;
}
static void new_icon (int s)
{
	if (!confirm ()) return;
	g_w = g_h = s;
	for (int i = 0; i < MAXS * MAXS; i++) g_img[i] = TRANSP;
	g_nundo = g_nredo = 0;
	strcpy (g_path, "SD0:/home/pictures/icon.bmp");
	g_saved = hash_img ();
	g_hx = g_hy = -1;
	refresh ();
}
static void on_open ()
{
	if (!confirm ()) return;
	static char p[300];
	ui_file_filter ((long) "*.bmp;*.png");
	long r = ui_file_open ((long) "SD0:/home/pictures", (long) p, sizeof p);
	ui_file_filter (0);
	ui_w_draw (0);
	if (r == 1) load_image (p);
	refresh ();
}
static void on_undo ()
{
	if (!g_nundo) return;
	memcpy (g_redo[g_nredo], g_img, sizeof g_img); g_rw[g_nredo] = g_w; g_rh[g_nredo] = g_h; if (g_nredo < NUNDO - 1) g_nredo++;
	g_nundo--; memcpy (g_img, g_undo[g_nundo], sizeof g_img); g_w = g_uw[g_nundo]; g_h = g_uh[g_nundo];
	refresh ();
}
static void on_redo ()
{
	if (!g_nredo) return;
	int keep = g_nredo;
	memcpy (g_undo[g_nundo], g_img, sizeof g_img); g_uw[g_nundo] = g_w; g_uh[g_nundo] = g_h; if (g_nundo < NUNDO - 1) g_nundo++;
	g_nredo = keep - 1; memcpy (g_img, g_redo[g_nredo], sizeof g_img); g_w = g_rw[g_nredo]; g_h = g_rh[g_nredo];
	refresh ();
}
static void transform (int kind)
{
	push_undo ();
	memcpy (g_tmp, g_img, sizeof g_img);
	int w = g_w, h = g_h;
	if (kind == 2) { g_w = h; g_h = w; }
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
	{
		unsigned v = g_tmp[y * MAXS + x];
		int nx = x, ny = y;
		switch (kind)
		{
		case 0: nx = w - 1 - x; break;
		case 1: ny = h - 1 - y; break;
		case 2: nx = h - 1 - y; ny = x; break;
		case 3: nx = (x + w - 1) % w; break;
		case 4: nx = (x + 1) % w; break;
		case 5: ny = (y + h - 1) % h; break;
		case 6: ny = (y + 1) % h; break;
		}
		g_img[ny * MAXS + nx] = v;
	}
	refresh ();
}
static void on_more ()
{
	long c = ui_color_dialog ((long) TR ("Première couleur"), g_col[0] == TRANSP ? 0x00808080 : g_col[0]);
	ui_w_draw (0);
	if (c < 0) return;
	g_col[0] = (unsigned) c & 0x00FFFFFF;
	if (g_col[0] == TRANSP) g_col[0] = 0x00FE00FE;
	refresh ();
}
static void command (int c)
{
	switch (c)
	{
	case M_NEW40: new_icon (40); break;
	case M_NEW16: new_icon (16); break;
	case M_NEW24: new_icon (24); break;
	case M_NEW32: new_icon (32); break;
	case M_NEW48: new_icon (48); break;
	case M_NEW64: new_icon (64); break;
	case M_OPEN: on_open (); break;
	case M_SAVE: on_save (); break;
	case M_SAVEAS: on_save_as (); break;
	case M_QUIT: if (confirm ()) g_quit = true; break;
	case M_UNDO: on_undo (); break;
	case M_REDO: on_redo (); break;
	case M_SWAP: { unsigned t = g_col[0]; g_col[0] = g_col[1]; g_col[1] = t; refresh (); break; }
	case M_MORE: on_more (); break;
	case M_FLIPH: transform (0); break;
	case M_FLIPV: transform (1); break;
	case M_ROT: transform (2); break;
	case M_SL: transform (3); break;
	case M_SR: transform (4); break;
	case M_SU: transform (5); break;
	case M_SD: transform (6); break;
	case M_CLEAR: push_undo (); for (int i = 0; i < MAXS * MAXS; i++) g_img[i] = TRANSP; refresh (); break;
	case M_GRID: g_gridOn = !g_gridOn; refresh (); break;
	}
}
static void on_drop ()
{
	static char b[1024];
	long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0 || kind != UDT_FILES) return;
	b[n] = 0;
	char *e = b; while (*e && *e != '\n' && *e != '\r') e++;
	*e = 0;
	if (confirm () && load_image (b)) refresh ();
}
static void lay_out (long w, long h)
{
	for (int i = 0; i < NTOOL; i++) ui_w_move (g_tools[i], 6, TOP + i * 34, TOOL_W - 12, 30);
	long gw = w - TOOL_W - SIDE_W, gh = h - TOP - FOOT - 4;
	ui_w_move (g_vgrid, TOOL_W, TOP, gw, gh);
	long sx = w - SIDE_W + 6;
	long ph = (NPAL / COLS + 1) * SW + 44;
	ui_w_move (g_vpal, sx, TOP, COLS * SW + 4, ph);
	ui_w_move (g_more, sx, TOP + ph + 2, 104, 26);
	ui_w_move (g_vprev, sx - 2, TOP + ph + 34, SIDE_W - 8, 2 * MAXS + 20);
	ui_w_move (g_status, 8, h - FOOT + 4, w - 16, 22);
	ui_view_dirty (g_vgrid); ui_view_dirty (g_vprev); ui_view_dirty (g_vpal);
}
static void on_resize (long w, long h) { ui_fill (0, 0, w, h, jet (J_FACE)); lay_out (w, h); ui_w_draw (0); }

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/iconedit.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, W_WANT, H_WANT, (long) TR ("Éditeur d'icônes"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (560, 520);
	ui_menu ((long)
		"MFichier\n" "I1\tNouvelle 40 x 40\tCtrl+N\n" "I2\tNouvelle 16 x 16\n" "I3\tNouvelle 24 x 24\n" "I4\tNouvelle 32 x 32\n" "I5\tNouvelle 48 x 48\n" "I6\tNouvelle 64 x 64\n" "-\n"
		"I7\tOuvrir...\tCtrl+O\n" "I8\tEnregistrer\tCtrl+S\n" "I9\tEnregistrer sous...\n" "-\n" "I10\tQuitter\tCtrl+Q\n"
		"MÉdition\n" "I11\tAnnuler\tCtrl+Z\n" "I12\tRétablir\tCtrl+Y\n" "-\n" "I13\tÉchanger les couleurs\n" "I14\tAutres couleurs...\n"
		"MImage\n" "I15\tRetourner à l'horizontale\n" "I16\tRetourner à la verticale\n" "I17\tTourner de 90°\n" "-\n"
		"I18\tDécaler à gauche\n" "I19\tDécaler à droite\n" "I20\tDécaler en haut\n" "I21\tDécaler en bas\n" "-\n" "I22\tVider\n" "I23\tGrille\n");
	for (int i = 0; i < MAXS * MAXS; i++) g_img[i] = TRANSP;
	for (int i = 0; i < NTOOL; i++) g_tools[i] = ui_pushbutton (0, 0, 72, 30, (long) TOOL_NAME[i], 0);
	g_vgrid = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_vgrid, (long) grid_paint, (long) grid_mouse, (long) grid_key, 0);
	g_vprev = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vprev, (long) prev_paint, 0, 0, 0);
	g_vpal = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vpal, (long) pal_paint, (long) pal_mouse, 0, 0);
	g_more = ui_pushbutton (0, 0, 104, 26, (long) "Autres...", 0);
	g_status = ui_label (0, 0, 100, 22, (long) "", ULB_DIM);
	ui_on_resize ((long) on_resize, 0);
	lay_out (ui.w, ui.h);
	if (argc > 1 && argv[1][0]) load_image (argv[1]);
	else g_saved = hash_img ();
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	refresh ();
	ui_w_focus (g_vgrid);
	ui_w_draw (0);
	ui_update_all ();
	while (!g_quit)
	{
		long t = ui_event (500);
		if (t == WEV_CLOSE) { if (confirm ()) break; continue; }
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_DROP) on_drop ();
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code == UN_ACTIVATE)
			{
				for (int i = 0; i < NTOOL; i++) if (w == g_tools[i])
				{
					if (i == T_PICK && g_tool != T_PICK) g_prevTool = g_tool;
					g_tool = i; refresh (); ui_w_focus (g_vgrid);
				}
				if (w == g_more) on_more ();
			}
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id);
			}
		}
		else ui_w_idle ();
		ui_view_flush ();
	}
	return 0;
}
