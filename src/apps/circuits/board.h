//
// board.h -- Circuits' board (the window's, included by main.cpp after the game's state; Onyx's board.h on a ui_view): the
// grid with the level's switches and lamps in their strips, the gates in their shapes, the wires routed by the engine and
// coloured by their level, the step mode's badges, the overlays (the ghost of a gate being placed or moved, the wire
// being drawn, the empty board's hint); and the mouse: a press on an output pin draws a wire, on a fed input pin picks
// its wire up, on a gate selects it (a drag moves it), on a switch toggles it, on a wire selects it; a right click
// deletes. A palette button arms a gate on the press, so a gate is placed by a click then a click, or by a drag from the
// palette (the palette's view holds the drag and hands its moves and its release to the board).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _circuits_board_h
#define _circuits_board_h

struct Board
{
	int width, height;				// the view's size
	int c, ox, oy;					// the cell (px), the grid's origin in the board
	enum { D_NONE, D_WIRE, D_PRESS, D_MOVE };
	int drag;					// what the held button does
	int src;					// D_WIRE: the source part
	int pickDst, pickPin;				// D_WIRE from a picked-up wire: where it was plugged (-1: a new wire)
	int mvI, mvDX, mvDY, mvX, mvY;			// D_PRESS / D_MOVE: the gate, the pointer in its box (1/16 cell), its new cell
	int pressX, pressY;				// where the press was (px)
	int px, py;					// the pointer (px; -1: out)
	int tgt, tpin; bool tok;			// D_WIRE: the input pin under the pointer, accepted or not
	bool lbl;					// the left button as last seen
	int hoverGate, hoverOut;			// the gate / the output pin under the pointer (-1)
	bool inside;					// the pointer over the board
	int kx, ky; bool kheld;				// the pointer the arrows move (g_keys; kx < 0: not shown yet)

	void init ()
	{
		width = height = 100; c = 12; ox = oy = 0; drag = D_NONE; src = -1; pickDst = -1; pickPin = 0;
		mvI = -1; mvDX = mvDY = mvX = mvY = 0; pressX = pressY = 0; px = py = -1; tgt = -1; tpin = 0; tok = false;
		lbl = false; hoverGate = hoverOut = -1; inside = false; kx = ky = -1; kheld = false;
	}
	// The cell: the most the board's room gives (12 px at the window's default size; 6 at the least), the grid centred;
	// the strokes and the texts follow it (Z: px at a 12 px cell -> px at this one)
	void fit ()
	{
		int m = height < 260 ? 4 : 20;			// (a low board: its margin goes first)
		c = (width - m) / BOARD_W; int ch = (height - m) / BOARD_H; if (ch < c) c = ch; if (c < 6) c = 6;
		ox = (width - BOARD_W * c) / 2; oy = (height - BOARD_H * c) / 2;
	}
	void resize (int w, int h) { width = w; height = h; fit (); }
	int Z (int v) { return c == 12 ? v : (v * c + 6) / 12; }
	int VZ (int v) { return c == 12 ? v : v * c / 12; }	// (1/16 px)
	int pxSmall () { int s = Z (11); return s < 8 ? 8 : s; }	// the gates' names, a name too long, the rest
	int pxTiny () { int s = Z (9); return s < 7 ? 7 : s; }
	int pxText () { int s = Z (13); return s < 10 ? 10 : s; }
	// cells -> px; 1/16 cell -> 1/16 px; px -> 1/16 cell
	int X (int gx) { return ox + gx * c; }
	int Y (int gy) { return oy + gy * c; }
	int X16 (int g16) { return ox * 16 + g16 * c; }
	int Y16 (int g16) { return oy * 16 + g16 * c; }
	int G16x (int mx) { return (mx - ox) * 16 / c; }
	int G16y (int my) { return (my - oy) * 16 / c; }
	static int fdiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

	// ---- drawing ------------------------------------------------------------------------------------------------
	unsigned level_col (int part) { int v = g_ev.v[part]; return v == LX ? WIRE_X : v ? WIRE_1 : WIRE_0; }
	int level_w (unsigned col) { return VZ (col == WIRE_1 ? V (3) : col == WIRE_X ? V (2) : V (2) + 8); }
	void dashed (int x0, int y0, int x1, int y1, unsigned col, int w)				// (1/16 px)
	{
		int len = x1 > x0 ? x1 - x0 : x0 - x1; len += y1 > y0 ? y1 - y0 : y0 - y1;
		if (len <= 0) return;
		int dash = c * 16 * 45 / 100, gap = c * 16 * 30 / 100; if (dash < 48) dash = 48; if (gap < 32) gap = 32;
		for (int s = 0; s < len; s += dash + gap)
		{
			int e = s + dash < len ? s + dash : len;
			seg (x0 + (int) ((long long) (x1 - x0) * s / len), y0 + (int) ((long long) (y1 - y0) * s / len),
			     x0 + (int) ((long long) (x1 - x0) * e / len), y0 + (int) ((long long) (y1 - y0) * e / len), col, w);
		}
	}
	void pin_dot (int gx16, int gy16, unsigned col) { disc (X16 (gx16), Y16 (gy16), VZ (V (2) + 8), col); }
	void ring (int cx, int cy, int r0, int r1, unsigned col)	// (1/16 px; radii in px at a 12 px cell)
	{ circle_line (cx, cy, V (Z (r0) + Z (r1)) / 2, V (Z (r1) - Z (r0)), col); }
	void draw_wire (int dst, int pin)
	{
		int xy[12], n = g_c.route (dst, pin, xy, 12);
		if (n < 2) return;
		int s = g_c.p[dst].in[pin];
		unsigned col = level_col (s);
		bool sel = dst == g_selDst && pin == g_selPin;
		if (sel) for (int k = 0; k + 1 < n; k++) seg (X16 (xy[2 * k] * 16), Y16 (xy[2 * k + 1] * 16), X16 (xy[2 * k + 2] * 16), Y16 (xy[2 * k + 3] * 16), mix (BOARD_BG, C_ACCENT, 90), VZ (V (8)));
		if (s >= 0 && g_ev.v[s] == LX)
		{
			for (int k = 0; k + 1 < n; k++) dashed (X16 (xy[2 * k] * 16), Y16 (xy[2 * k + 1] * 16), X16 (xy[2 * k + 2] * 16), Y16 (xy[2 * k + 3] * 16), col, VZ (V (2)));
			return;
		}
		for (int k = 0; k + 1 < n; k++) seg (X16 (xy[2 * k] * 16), Y16 (xy[2 * k + 1] * 16), X16 (xy[2 * k + 2] * 16), Y16 (xy[2 * k + 3] * 16), col, level_w (col));
	}
	// A gate (i: the board's; -1: the ghost of `type` at (gx, gy), ok: it fits)
	void draw_gate (int i, int type, int gx, int gy, bool ok)
	{
		bool ghost = i < 0;
		const Part *q = ghost ? 0 : &g_c.p[i];
		int n = type == P_NOT ? 1 : 2;
		for (int k = 0; k < n; k++)			// the input stubs (the body covers their ends)
		{
			int y = type == P_NOT ? gy + 2 : gy + (k ? 3 : 1), s = ghost ? -1 : q->in[k];
			unsigned col = s >= 0 ? level_col (s) : WIRE_OPEN;
			seg (X16 (gx * 16), Y16 (y * 16), X16 (gx * 16 + 32), Y16 (y * 16), col, VZ (s >= 0 && col == WIRE_1 ? V (3) : V (2)));
		}
		unsigned oc = ghost ? WIRE_OPEN : level_col (i);
		seg (X16 (gx * 16 + 56), Y16 (gy * 16 + 32), X16 (gx * 16 + 80), Y16 (gy * 16 + 32), oc, VZ (!ghost && oc == WIRE_1 ? V (3) : V (2)));
		if (!ghost && (i == g_sel || i == hoverGate))
		{
			int x0 = X16 (gx * 16 + 9) / 16, y0 = Y16 (gy * 16 + 3) / 16, x1 = X16 (gx * 16 + 76) / 16, y1 = Y16 (gy * 16 + 61) / 16;
			panel (x0, y0, x1 - x0, y1 - y0, Z (6), i == g_sel ? mix (BOARD_BG, C_ACCENT, 50) : BOARD_BG, i == g_sel ? C_ACCENT : mix (BOARD_BG, C_ACCENT, 90));
		}
		int L = X16 (gx * 16 + 16), R = X16 (gx * 16 + 66), cy = Y16 (gy * 16 + 32), h2 = c * 16 * 145 / 100, rb = c * 16 * 3 / 10;
		unsigned face = ghost ? mix (BOARD_BG, ok ? OK_GREEN : ERR_RED, 50) : g_ev.v[i] == LX ? GATE_FACEX : g_ev.v[i] ? GATE_FACE1 : GATE_FACE;
		unsigned ink = ghost ? (ok ? OK_GREEN : ERR_RED) : i == g_errPart ? ERR_RED : GATE_INK;
		draw_gate_shape (type, L, cy - h2, R, cy + h2, rb, face, ink, VZ (V (1) + 10));
		{						// its name, small, inside
			int tx = type == P_NOT ? 18 : type == P_XOR || type == P_OR || type == P_NOR ? 30 : 22;
			int w = type == P_NOT ? 26 : type == P_AND || type == P_NAND ? 34 : 32;
			int x0 = X16 (gx * 16 + tx) / 16, w0 = X16 (gx * 16 + tx + w) / 16 - x0;
			const char *nm = gate_word (type);
			unsigned col = ghost ? ink : mix (face, GATE_INK, 190);
			int sz = tw (nm, 0, pxSmall ()) > w0 + Z (6) ? pxTiny () : pxSmall ();
			text_c (x0 - Z (4), cy / 16 - Z (8), w0 + Z (8), Z (16), nm, col, 0, sz);
		}
		if (g_step >= 0 && !ghost)			// step mode: the gate's depth in a disc above it
		{
			int bx = X (gx) + c * 5 / 2, by = Y (gy) - Z (2), br = Z (7);
			unsigned col = g_ev.v[i] == LX ? WIRE_X : C_ACCENT;
			disc (V (bx), V (by), V (br), col);
			char t[8]; snprintf (t, sizeof t, "%d", g_ev.depth[i]);
			text_c (bx - br, by - br, 2 * br, 2 * br, t, ink_on (col), 2, pxSmall ());
		}
		for (int k = 0; k < n; k++) { int y = type == P_NOT ? gy + 2 : gy + (k ? 3 : 1); pin_dot (gx * 16, y * 16, ghost ? WIRE_OPEN : WIRE_0); }
		pin_dot (gx * 16 + 80, gy * 16 + 32, oc);
	}
	void draw_switch (int i)				// a key showing its digit, its name at its left; green when 1
	{
		const Part &q = g_c.p[i];
		bool on = g_ev.v[i] == L1;
		int kx = X16 (q.x * 16 + 38) / 16, ky = Y16 (q.y * 16 + 1) / 16, ks = Y16 (q.y * 16 + 31) / 16 - ky, kw = X16 (q.x * 16 + 72) / 16 - kx;
		seg (X16 (q.x * 16 + 69), Y16 (q.y * 16 + 16), X16 (q.x * 16 + 80), Y16 (q.y * 16 + 16), on ? WIRE_1 : WIRE_0, VZ (on ? V (3) : V (2) + 8));
		int sz = pxText ();
		text_l (kx - Z (5) - tw (q.name, 2, sz), ky, ks, q.name, GATE_INK, 2, sz);
		panel (kx, ky, kw, ks, Z (5), on ? WIRE_1 : GATE_FACE, on ? mix (WIRE_1, 0, 70) : jet (J_LIGNE_FORTE));
		text_c (kx, ky, kw, ks, on ? "1" : "0", on ? ink_on (WIRE_1) : C_TEXT, 2, sz);
		pin_dot (q.x * 16 + 80, q.y * 16 + 16, on ? WIRE_1 : WIRE_0);
	}
	void draw_lamp (int i)
	{
		const Part &q = g_c.p[i];
		int s = q.in[0];
		unsigned col = s >= 0 ? level_col (s) : WIRE_OPEN;
		seg (X16 (q.x * 16), Y16 (q.y * 16 + 16), X16 (q.x * 16 + 16), Y16 (q.y * 16 + 16), col, VZ (s >= 0 && col == WIRE_1 ? V (3) : V (2)));
		int cx = X16 (q.x * 16 + 28) / 16, cy = Y (q.y + 1), r = c * 9 / 10 + 1;
		bool unknown = g_ev.v[i] == LX, on = !unknown && g_ev.v[i] == L1 && s >= 0;
		if (i == g_errPart) circle_line (V (cx), V (cy), V (r + Z (5)), V (Z (3)), ERR_RED);
		if (on)						// (its glow, the strip seen through it)
		{
			disc (V (cx), V (cy), V (r + Z (7)), mix (STRIP_BG, LAMP_ON, 50));
			disc (V (cx), V (cy), V (r + Z (3)), mix (STRIP_BG, LAMP_ON, 120));
		}
		disc (V (cx), V (cy), V (r), on ? mix (LAMP_ON, 0, 60) : unknown ? WIRE_X : mix (LAMP_OFF, 0, 120));
		disc (V (cx), V (cy), V (r - Z (2)), on ? LAMP_ON : unknown ? mix (WIRE_X, BOARD_BG, 100) : LAMP_OFF);
		disc (V (cx - r / 3), V (cy - r / 3), V (r / 3), mix (on ? LAMP_ON : unknown ? WIRE_X : LAMP_OFF, 0xFFFFFF, on ? 170 : 60));
		int sz = pxText ();
		if (unknown) text_c (cx - r, cy - r, 2 * r, 2 * r, "?", C_TEXT, 2, sz);
		text_l (cx + r + Z (6), cy - Z (10), Z (20), q.name, GATE_INK, 2, sz);
		pin_dot (q.x * 16, q.y * 16 + 16, col);
	}
	// The wire being drawn: its future route, dashed, from the source's pin to the pointer (or the input pin under it)
	void draw_rubber ()
	{
		int x0, y0; g_c.pinOut (src, x0, y0);
		int sx = X16 (x0 * 16), sy = Y16 (y0 * 16), ex = V (px), ey = V (py), t16 = G16x (px);
		if (tgt >= 0) { int tx, ty; g_c.pinIn (tgt, tpin, tx, ty); ex = X16 (tx * 16); ey = Y16 (ty * 16); t16 = tx * 16; }
		int xm16 = t16 > x0 * 16 + 16 ? (x0 * 16 + t16) / 2 / 16 * 16 : x0 * 16 + 16, xm = X16 (xm16);
		int dw = VZ (V (2) + 8); dashed (sx, sy, xm, sy, C_ACCENT, dw); dashed (xm, sy, xm, ey, C_ACCENT, dw); dashed (xm, ey, ex, ey, C_ACCENT, dw);
		if (tgt >= 0) ring (ex, ey, 5, 7, tok ? OK_GREEN : ERR_RED);
		ring (sx, sy, 4, 6, C_ACCENT);
	}
	void draw_hint ()
	{
		bool first = g_L && g_L->parts == 0;
		const char *t = first ? TR ("Press on the switch's pin, drag to the lamp's pin, let go.")
				      : TR ("Take a gate from the palette and put it on the board, then drag from a pin to a pin to wire it.");
		if (g_keys)					// (pocket, a keyboard: by the keys)
			t = first ? TR ("The arrows move the pointer. Enter on the switch's pin starts a wire, Enter on the lamp's pin lays it.")
				  : TR ("1 to 6 take a gate, the arrows move it, Enter puts it. Enter on a pin starts a wire, Enter on another pin lays it.");
		int bx = X (9), by = Y (6), bw = X (31) - bx;
		if (c < 12) { bw = width - 16 < 300 ? width - 16 : 300; bx = (width - bw) / 2; }	// (a small cell: its text all the same)
		int ml = g_keys ? 6 : 4;			// (the keys' words are longer)
		int n = wrap_count (t, bw - 32, ml), bh = 44 + n * 18 + 12;
		unsigned line = mix (BOARD_BG, C_TEXT, 90);
		panel (bx, by, bw, bh, 10, mix (BOARD_BG, C_BG, 128), mix (BOARD_BG, C_BG, 128));
		for (int x = bx + 8; x < bx + bw - 8; x += 10) { ui_fill (x, by, 5, 1, line); ui_fill (x, by + bh - 1, 5, 1, line); }
		for (int y = by + 8; y < by + bh - 8; y += 10) { ui_fill (bx, y, 1, 5, line); ui_fill (bx + bw - 1, y, 1, 5, line); }
		text_c (bx, by + 12, bw, 24, first ? TR ("Lay a wire") : TR ("Build your circuit"), C_TEXT, 2, PX_BIG);
		wrap_draw (bx + 16, by + 44, bw - 32, t, C_DIM, ml, 18, 0, true);
	}
	void draw_pointer ()					// the pointer the arrows move: a cross, the accent's ring round it
	{
		if (kx < 0) return;
		int r = c * 7 / 10; if (r < 6) r = 6;
		circle_line (V (kx), V (ky), V (r), V (2), kheld ? OK_GREEN : C_ACCENT);
		seg (V (kx - r - 3), V (ky), V (kx + r + 3), V (ky), C_ACCENT, V (1));
		seg (V (kx), V (ky - r - 3), V (kx), V (ky + r + 3), C_ACCENT, V (1));
	}
	void paint ()
	{
		fit ();
		int r = ui_theme_flat () ? 10 : 0;
		ui_fill (0, 0, width, height, C_BG);
		panel (0, 0, width, height, r, STRIP_BG, C_LINE);
		ui_fill (X (GATE_MINX), 1, X (GATE_MAXX) - X (GATE_MINX), height - 2, BOARD_BG);
		unsigned dot = BOARD_DOT;
		for (int gy = 0; gy <= BOARD_H; gy++) for (int gx = GATE_MINX + 1; gx < GATE_MAXX; gx++) ui_fill (X (gx), Y (gy), 2, 1, dot);
		{
			unsigned cap = mix (STRIP_BG, GATE_INK, 120);
			text_c (0, Z (4), X (GATE_MINX), Z (14), TR ("INPUTS"), cap, 2, pxSmall ());
			text_c (X (GATE_MAXX), Z (4), width - X (GATE_MAXX), Z (14), TR ("OUTPUTS"), cap, 2, pxSmall ());
		}
		if (!g_L) return;
		// the wires (the one selected last, over the others), their junctions
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < g_c.n; i++)
				for (int k = 0; k < g_c.pins (i); k++)
					if (g_c.p[i].in[k] >= 0 && (pass == 1) == (i == g_selDst && k == g_selPin)) draw_wire (i, k);
		{
			int xy[2 * 128], m = g_c.junctions (xy, 2 * 128);
			for (int j = 0; j < m; j++)
			{
				int pin = 0, d = g_c.wireAt (xy[2 * j] * 16, xy[2 * j + 1] * 16, &pin);
				unsigned col = d >= 0 && g_c.p[d].in[pin] >= 0 ? level_col (g_c.p[d].in[pin]) : WIRE_0;
				disc (X16 (xy[2 * j] * 16), Y16 (xy[2 * j + 1] * 16), VZ (V (3) + 8), col);
			}
		}
		for (int i = 0; i < g_c.n; i++)
		{
			const Part &q = g_c.p[i];
			if (q.type == P_SWITCH) draw_switch (i);
			else if (q.type == P_LAMP) draw_lamp (i);
			else if (!(drag == D_MOVE && i == mvI)) draw_gate (i, q.type, q.x, q.y, true);
		}
		if (g_c.gates () == 0 && g_c.wires () == 0 && drag == D_NONE && g_armed < 0) draw_hint ();
		if (drag == D_MOVE) draw_gate (-1, g_c.p[mvI].type, mvX, mvY, g_c.placeErr (g_c.p[mvI].type, mvX, mvY, mvI) == E_OK);
		else if (g_armed >= 0 && inside) { int gx, gy; ghost_cell (gx, gy); draw_gate (-1, g_armed, gx, gy, g_c.fits (g_armed, gx, gy)); }
		if (drag == D_WIRE) draw_rubber ();
		else if (hoverOut >= 0 && g_armed < 0) { int x, y; g_c.pinOut (hoverOut, x, y); ring (X16 (x * 16), Y16 (y * 16), 4, 6, mix (BOARD_BG, C_ACCENT, 110)); }
		if (g_keys) draw_pointer ();
	}

	// ---- the keys (a pocket: no mouse needed) -- the board's own pointer, on the grid's points: the arrows move it, a
	// cell at a time (Shift: four); Enter is the button, pressed and let go at once (a switch toggled, an armed gate
	// put, a wire or nothing selected) or, when the press took something (a gate, a wire from a pin), held until the next Enter
	bool key (int hid, int mods)
	{
		fit ();
		if (hid == KEY_LEFT || hid == KEY_RIGHT || hid == KEY_UP || hid == KEY_DOWN)
		{
			int step = c * (mods & UI_MOD_SHIFT ? 4 : 1);
			if (kx < 0) { kx = X (BOARD_W / 2); ky = Y (BOARD_H / 2); }
			else
			{
				kx += hid == KEY_LEFT ? -step : hid == KEY_RIGHT ? step : 0; ky += hid == KEY_UP ? -step : hid == KEY_DOWN ? step : 0;
				if (kx < X (0)) kx = X (0); if (kx > X (BOARD_W)) kx = X (BOARD_W);
				if (ky < Y (0)) ky = Y (0); if (ky > Y (BOARD_H)) ky = Y (BOARD_H);
			}
			kx = X ((kx - ox + c / 2) / c); ky = Y ((ky - oy + c / 2) / c);
			mouse (0, kx, ky, kheld ? 1 : 0);
			dirty ();
			return true;
		}
		if (hid != KEY_ENTER) return false;
		if (kx < 0) { kx = X (BOARD_W / 2); ky = Y (BOARD_H / 2); dirty (); return true; }
		if (!kheld)
		{
			mouse (0, kx, ky, 0);
			mouse (1, kx, ky, 1);
			if (drag != D_NONE) kheld = true; else mouse (2, kx, ky, 0);
		}
		else { kheld = false; mouse (2, kx, ky, 0); }
		dirty ();
		return true;
	}

	// ---- the mouse ------------------------------------------------------------------------------------------------------
	void dirty () { view_dirty (g_vBoard); }
	void ghost_cell (int &gx, int &gy)			// the cell of a gate centred under the pointer
	{ gx = fdiv (G16x (px) - 40 + 8, 16); gy = fdiv (G16y (py) - 32 + 8, 16); }
	// The pin under (mx, my) within 0.6 cell (7 px at least): its part, *isOut, *pin -> -1 none
	int pin_at (int mx, int my, bool *isOut, int *pin)
	{
		int r = c * 6 / 10; if (r < 7) r = 7;
		int best = -1, bestD = r * r + 1;
		for (int i = 0; i < g_c.n; i++)
		{
			int x, y;
			if (g_c.p[i].type != P_LAMP)
			{
				g_c.pinOut (i, x, y); int dx = mx - X (x), dy = my - Y (y), d = dx * dx + dy * dy;
				if (d < bestD) { bestD = d; best = i; *isOut = true; *pin = 0; }
			}
			for (int k = 0; k < g_c.pins (i); k++)
			{
				g_c.pinIn (i, k, x, y); int dx = mx - X (x), dy = my - Y (y), d = dx * dx + dy * dy;
				if (d < bestD) { bestD = d; best = i; *isOut = false; *pin = k; }
			}
		}
		return best;
	}
	int gate_at (int mx, int my) { return g_c.at (fdiv (mx - ox, c), fdiv (my - oy, c)); }
	void cancel ()						// Esc: the drag undone
	{
		if (drag == D_WIRE && pickDst >= 0) g_c.connect (src, pickDst, pickPin);	// (the wire picked up: back)
		drag = D_NONE; tgt = -1;
		refresh ();
	}
	void update_target ()
	{
		tgt = -1; tok = false;
		bool out = false; int pin = 0, i = pin_at (px, py, &out, &pin);
		if (i >= 0 && !out) { Err e; tgt = i; tpin = pin; tok = g_c.canConnect (src, i, pin, &e); }
	}
	void press (int mx, int my)
	{
		pressX = mx; pressY = my;
		if (g_armed >= 0) return;			// (placed on the release)
		bool out = false; int pin = 0, i = pin_at (mx, my, &out, &pin);
		if (i >= 0 && out) { start_wire (i, -1, 0); return; }
		if (i >= 0 && !out && g_c.p[i].in[pin] >= 0)	// a fed input: its wire picked up
		{
			int s = g_c.p[i].in[pin];
			g_c.disconnect (i, pin);
			start_wire (s, i, pin);
			return;
		}
		int g = gate_at (mx, my);
		if (g >= 0 && g_c.p[g].type == P_SWITCH) { toggle_switch (g); return; }
		if (g >= 0 && gate_type (g_c.p[g].type))
		{
			select_gate (g);
			drag = D_PRESS; mvI = g; mvDX = G16x (mx) - g_c.p[g].x * 16; mvDY = G16y (my) - g_c.p[g].y * 16;
			mvX = g_c.p[g].x; mvY = g_c.p[g].y;
			return;
		}
		int wp = 0, d = g_c.wireAt (G16x (mx), G16y (my), &wp);
		if (d >= 0) { select_wire (d, wp); return; }
		select_gate (-1);
	}
	void start_wire (int s, int dst, int pin)
	{
		drag = D_WIRE; src = s; pickDst = dst; pickPin = pin;
		select_gate (-1);
		set_message (M_INFO, g_keys ? TR ("The arrows to an input pin, then Enter lays the wire (Esc: cancel).") : TR ("Let go on an input pin to lay the wire (Esc: cancel)."));
		update_target ();
		if (dst >= 0) refresh ();
	}
	void release (int mx, int my)
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (g_armed >= 0 && drag == D_NONE)
		{
			if (!in) return;
			int gx, gy; ghost_cell (gx, gy);
			Err e; int i = g_c.add (g_armed, gx, gy, &e);
			if (i < 0) { set_message (M_ERR, TR (err_word (e))); dirty (); return; }
			arm (-1); select_gate (i); board_changed ();
			return;
		}
		int d = drag; drag = D_NONE;
		if (d == D_WIRE)
		{
			if (!in) { drag = D_WIRE; cancel (); prompt (); return; }
			update_target ();
			int t = tgt; tgt = -1;
			if (t >= 0)
			{
				Err e = g_c.connect (src, t, tpin);
				if (e != E_OK)
				{
					if (pickDst >= 0) g_c.connect (src, pickDst, pickPin);
					set_message (M_ERR, TR (err_word (e))); refresh (); return;
				}
			}
			else if (pickDst < 0) { prompt (); refresh (); return; }	// (released elsewhere: nothing made)
			prompt (); board_changed ();
			return;
		}
		if (d == D_MOVE)
		{
			if (!in) { refresh (); return; }
			Err e = g_c.move (mvI, mvX, mvY);
			if (e != E_OK) { set_message (M_ERR, TR (err_word (e))); refresh (); return; }
			board_changed ();
			return;
		}
	}
	void right_press (int mx, int my)			// disarms, or deletes the gate / the wire
	{
		if (g_armed >= 0) { arm (-1); return; }
		bool out = false; int pin = 0;
		int g = gate_at (mx, my);
		if (g >= 0 && gate_type (g_c.p[g].type) && pin_at (mx, my, &out, &pin) < 0) { g_c.remove (g); select_gate (-1); board_changed (); }
		else { int wp = 0, d = g_c.wireAt (G16x (mx), G16y (my), &wp); if (d >= 0) { g_c.disconnect (d, wp); select_wire (-1, 0); board_changed (); } }
	}
	// kind: 0 a move, 1 a press, 2 a release, 3 out (ui_view's); btn: the buttons (1 left, 2 right)
	void mouse (int kind, int mx, int my, int btn)
	{
		if (kind == 3)
		{
			px = py = -1; inside = false; lbl = false;
			if (hoverGate >= 0 || hoverOut >= 0 || g_armed >= 0) { hoverGate = hoverOut = -1; dirty (); }
			return;
		}
		px = mx; py = my;
		inside = mx >= 0 && my >= 0 && mx < width && my < height;
		bool bl = (btn & 1) != 0, was = drag != D_NONE;
		if (kind == 1 && (btn & 2)) { if (drag == D_NONE) right_press (mx, my); return; }
		if (kind == 1 && inside) { lbl = true; press (mx, my); bl = true; }
		else if (kind == 2) { if (lbl) { lbl = false; bl = false; release (mx, my); } }
		else if (bl && drag == D_WIRE) { update_target (); }
		else if (bl && (drag == D_PRESS || drag == D_MOVE))
		{
			int dx = mx - pressX, dy = my - pressY;
			if (drag == D_PRESS && dx * dx + dy * dy >= (c / 2) * (c / 2)) drag = D_MOVE;
			if (drag == D_MOVE) { mvX = fdiv (G16x (mx) - mvDX + 8, 16); mvY = fdiv (G16y (my) - mvDY + 8, 16); }
		}
		// the hover
		int hg = -1, ho = -1;
		if (!bl && inside && g_armed < 0)
		{
			bool out = false; int pin = 0, i = pin_at (mx, my, &out, &pin);
			if (i >= 0 && out) ho = i;
			else { int g = gate_at (mx, my); if (g >= 0 && gate_type (g_c.p[g].type)) hg = g; }
		}
		// (a repaint costs: only when what shows has changed)
		if (hg != hoverGate || ho != hoverOut || was || drag != D_NONE || g_armed >= 0 || kind != 0) dirty ();
		hoverGate = hg; hoverOut = ho;
	}
};

#endif
