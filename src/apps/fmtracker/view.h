//
// view.h -- FM Tracker : dessin de la fenêtre (grille, en-têtes, liste des motifs, piano, barre
// d'état), souris et clavier de ces zones. Traduit de fmt_view.inc (l'asm de Korin) : mêmes
// coordonnées, mêmes teintes, mêmes calculs entiers, donc mêmes pixels ; inclus par main.cpp.
//
// Ces zones sont dessinées à la main dans la surface de la fenêtre ; les boutons, les nombres et la
// barre de défilement sont des widgets. flush repeint ce que g_redraw demande (mark).
//

// les couleurs d'après le thème
static unsigned c_face, c_field, c_text, c_text2, c_ln, c_acc, c_acc_ink, c_hollow, c_gline, c_row4, c_row16,
	c_gnum, c_cont, c_gmute, c_gutter, c_playbg, c_gplay, c_gcur, c_headcur;

static unsigned mix (unsigned a, unsigned b, long t) { return (unsigned) ui_mix (a, b, t); }
// 0..256 -> teinte : fond de la grille vers son texte
static unsigned f_mix (long t) { return mix (c_field, c_text, t); }

static void colors_init (void)
{
	const unsigned *tk = (const unsigned *) ui_tokens ();
	c_face = tk[TJ_FACE / 4];
	c_field = tk[TJ_SURFACE / 4];
	c_text = tk[TJ_TEXT1 / 4];
	c_text2 = tk[TJ_TEXT2 / 4];
	c_ln = tk[TJ_LINE / 4];
	c_acc = tk[TJ_ACCENT / 4];
	c_hollow = tk[TJ_SUNKEN / 4];
	c_acc_ink = (unsigned) ui_ink (tk[TJ_ACCENT / 4]);
	c_gline = f_mix (26);					// teintes du fond de la grille
	c_row4 = f_mix (14);
	c_row16 = f_mix (30);
	c_gnum = f_mix (120);
	c_cont = f_mix (76);
	c_gmute = f_mix (70);
	c_gutter = mix (c_field, c_face, 150);
	c_playbg = mix (c_field, PLAY_COL, 96);
	c_gplay = mix (c_gutter, PLAY_COL, 90);
	c_gcur = mix (c_gutter, c_acc, 90);
	c_headcur = mix (c_face, c_acc, 46);
}

// texte de 13 px (gras : txtb)
static void txt (const char *s, long x, long base, unsigned c) { ui_text ((long) s, x, base, c, 13, (long) ui.font, 0); }
static void txtb (const char *s, long x, long base, unsigned c) { ui_text ((long) s, x, base, c, 13, (long) ui.font_b, 0); }

// cadre LIGNE, fond bg
static void sunken (long x, long y, long w, long h, long r, unsigned bg)
{
	ui_rbox (x, y, w, h, r, c_ln, c_ln);
	ui_rbox (x + 1, y + 1, w - 2, h - 2, r - 1, bg, bg);
}

// ---- le fond et le panneau de gauche ----------------------------------------------------------

// titre de section (y : son haut)
static void heading (const char *s, long y) { ui_text ((long) TR (s), 10, y + 13, c_text2, 12, (long) ui.font_b, 0); }
// libellé à gauche d'un nombre (y : le haut du nombre)
static void field_label (const char *s, long y) { txt (TR (s), 10, y + 19, c_text); }

static void paint_side (void)
{
	heading ("PATTERNS", TOOL_H + 8);
	long y = g_side_y;
	heading ("THIS PATTERN", y);
	field_label ("Rows", y + SD_ROWS);
	field_label ("Speed", y + SD_SPEED);
	heading ("TYPING", y + SD_HTYPE);
	field_label ("Octave", y + SD_OCT);
	field_label ("Step", y + SD_STEP);
	heading ("SONG", y + SD_HSONG);
}

// le titre et l'auteur du morceau
static void draw_song_labels (void)
{
	long y = g_side_y + SD_TITLE;
	ui_fill (10, y, SIDE_W - 14, 40, c_face);
	const char *t = g_song.title;
	if (!t[0]) t = g_path[0] ? base_name (g_path) : TR ("Untitled");	// sans titre : le nom du fichier
	ui_text_fit ((long) t, 12, y + 14, c_text, 13, (long) ui.font, SIDE_W - 22);
	ui_text_fit ((long) g_song.author, 12, y + 34, c_text2, 13, (long) ui.font, SIDE_W - 22);
	ui_update (0, y, SIDE_W, 40);
}

// tout le fond, les widgets
static void paint_all (void)
{
	colors_init ();
	ui_fill (0, 0, g_w, g_h, c_face);
	ui_fill (0, TOOL_H - 1, g_w, 1, c_ln);			// trait sous la barre, bord du panneau
	ui_fill (SIDE_W - 1, TOOL_H, 1, g_h - (TOOL_H + ST_H), c_ln);
	paint_side ();
	ui_w_draw (0);
}

// ---- la position -----------------------------------------------------------------------------

// l'afficheur : motif:ligne temps
static void update_lcd (void)
{
	long p = g_pat, r = g_row;
	if (g_playing) { p = g_play_pat; r = g_shown_row < 0 ? 0 : g_shown_row; }
	if (p >= g_song.npat) p = g_song.npat - 1;
	unsigned long cs = 0;					// centièmes (une ligne : vitesse / 20 s)
	for (long i = 0; i < p; i++) cs += (unsigned long) pat_at (i).rows * pat_at (i).speed * 5;
	cs += (unsigned long) r * pat_at (p).speed * 5;
	static char t[32];
	char *d = put_2d (t, p + 1);
	*d++ = ':';
	if (r < 100) { *d++ = '0'; if (r < 10) *d++ = '0'; }
	d += sprintf (d, "%ld %lu:", r, cs / 6000);
	d = put_2d (d, (long) (cs / 100 % 60));
	*d++ = '.';
	*d++ = (char) ('0' + cs / 10 % 10);
	*d = 0;
	ui_w_set_text (w_lcd, (long) t);
}

// ---- en-têtes des canaux ---------------------------------------------------------------------

// une pastille (M, S) : allumée de sa couleur, sinon creuse (survolée : ligne)
static void head_pill (long x, const char *s, unsigned on_col, bool on, bool hot)
{
	unsigned c = on ? on_col : hot ? c_ln : c_hollow;
	ui_rbox (x, TOOL_H + 26, 20, 16, 4, c, c);
	long tw = ui_text_width ((long) s, 12, (long) ui.font_b);
	ui_text ((long) s, x + ((20 - tw) >> 1), TOOL_H + 38, on ? 0x00FFFFFFu : c_text, 12, (long) ui.font_b, 0);
}

static void draw_head (long c)
{
	long cw = g_col_w, x = c * cw + SIDE_W + NUM_W;
	ui_fill (x, TOOL_H, cw, HEAD_H, g_ch == c ? c_headcur : c_face);
	ui_fill (x, TOOL_H, cw - 1, 3, ch_col[c]);		// la couleur du canal
	ui_fill (x + cw - 1, TOOL_H, 1, HEAD_H, c_ln);
	if (g_hot_head == c * 4)				// le nom survolé ou appuyé : un bouton
		ui_rbox (x + 3, TOOL_H + 5, cw - 8, 19, 4, c_hollow, c_hollow);
	bool quiet = silenced (c);
	const char *name = g_song.ins[c].name;
	if (!name[0]) name = TR ("(none)");
	char t[128];
	fmt (t, "%d  ", c + 1);
	str_copy (t + strlen (t), name, 10);
	ui_text_fit ((long) t, x + 8, TOOL_H + 19, quiet ? c_gmute : c_text, 13, (long) ui.font_b, cw - 16);
	head_pill (x + 4, "M", 0x00D9534A, cur_pat ().mute[c] != 0, g_hot_head == c * 4 + 1);
	head_pill (x + 26, "S", 0x00D08A1E, g_solo == c, g_hot_head == c * 4 + 2);
	long vx = x + 52, vw = cw - 60;				// le niveau
	if (vw < 6) return;
	ui_rbox (vx, TOOL_H + 31, vw, 6, 3, c_hollow, c_hollow);
	if (quiet) return;
	unsigned long fill = (unsigned long) (g_vu[c] * vw) / 100;
	if (fill <= 2) return;
	ui_rbox (vx, TOOL_H + 31, (long) fill, 6, 3, 0x0062B87A, 0x004A9E62);
}

static void draw_heads (void)
{
	for (long c = 0; c < FMS_CH; c++) draw_head (c);
	ui_update (SIDE_W, TOOL_H, g_w - SIDE_W, HEAD_H);
}

// ---- la grille --------------------------------------------------------------------------------
static void draw_grid (void)
{
	long r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
	FmsPattern &p = cur_pat ();
	long gh = g_gh, cw = g_col_w;
	ui_clip (SIDE_W, GRID_Y, g_gw, gh);
	ui_fill (SIDE_W, GRID_Y, g_gw, gh, c_field);
	ui_fill (SIDE_W, GRID_Y, NUM_W, gh, c_gutter);
	for (unsigned long i = 0; i <= (unsigned long) g_vis; i++)	// (une ligne de plus : coupée)
	{
		long r = g_top + (long) i;
		if ((unsigned long) r >= (unsigned long) p.rows) break;
		long y = (long) i * ROW_H + GRID_Y;
		bool play = g_playing && g_play_pat == g_pat && g_shown_row == r;
		unsigned bg = (r & 3) ? c_field : (r & 15) ? c_row4 : c_row16;	// fond de la ligne
		if (play) bg = c_playbg;
		else if (g_row == r) bg = mix (bg, c_acc, 36);
		ui_fill (SIDE_W + NUM_W, y, g_gw - NUM_W, ROW_H, bg);
		if (g_row == r || play) ui_fill (SIDE_W, y, NUM_W, ROW_H, play ? c_gplay : c_gcur);	// numéro
		char num[16];
		*put_2d (num, r) = 0;
		bool strong = (r & 3) == 0;			// (toutes les 4 lignes : gras)
		long fnt = strong ? (long) ui.font_b : (long) ui.font;
		long tw = ui_text_width ((long) num, 13, fnt);
		ui_text ((long) num, SIDE_W + NUM_W - 8 - tw, y + 13, strong ? c_text : c_gnum, 13, fnt, 0);
		for (long c = 0; c < FMS_CH; c++)
		{
			long x = c * cw + SIDE_W + NUM_W;
			if (g_sel && r >= r0 && r <= r1 && c >= c0 && c <= c1)	// dans le bloc
				ui_fill (x, y, cw, ROW_H, mix (bg, c_acc, 96));
			ui_fill (x, y, 1, ROW_H, c_gline);		// trait du canal
			bool cur = g_row == r && g_ch == c;
			if (cur) ui_rbox (x + 1, y, cw - 1, ROW_H, 4, c_acc, c_acc);	// le curseur
			unsigned char v = cell (p, c, r);
			char t[4]; fms_note_text (v, t);
			if (!t[0]) continue;				// silence : rien
			unsigned col = silenced (c) ? c_gmute : (v & 7) ? c_text : c_cont;
			if (cur) col = c_acc_ink;
			ui_text ((long) t, x + ((cw - g_cell_tw) >> 1), y + 13, col, 13, (v & 7) ? g_font_note : (long) ui.font_m, 0);
		}
	}
	ui_fill (SIDE_W + NUM_W - 1, GRID_Y, 1, gh, c_gline);	// bord de la gouttière
	ui_clip (0, 0, 0, 0);
	ui_update (SIDE_W, GRID_Y, g_gw, gh);
}

// ---- la liste des motifs ---------------------------------------------------------------------

// g_pat visible dans la liste
static void list_show (void)
{
	if (g_pat < g_ltop) g_ltop = g_pat;
	else if (g_pat >= g_ltop + g_lvis) g_ltop = g_pat - g_lvis + 1;
}

static void draw_plist (void)
{
	ui_fill (0, LIST_Y, SIDE_W - 1, g_list_h, c_face);
	long mx = g_song.npat - g_lvis;				// g_ltop borné
	if (mx < 0) mx = 0;
	if (g_ltop > mx) g_ltop = mx;
	if (g_ltop < 0) g_ltop = 0;
	for (long k = 0; k < g_lvis; k++)
	{
		long i = g_ltop + k;
		if (i >= g_song.npat) break;
		long y = k * LIST_ROW + LIST_Y;
		bool on = i == g_pat;
		if (on) ui_rbox (4, y + 1, SIDE_W - 13, LIST_ROW - 2, 4, c_acc, c_acc);
		else if (i == g_hot_list) ui_rbox (4, y + 1, SIDE_W - 13, LIST_ROW - 2, 4, c_hollow, c_hollow);
		unsigned ink = on ? c_acc_ink : c_text, dim = on ? c_acc_ink : c_text2;
		if (g_playing && i == g_play_pat)		// le motif joué
			ui_text ((long) "\xE2\x96\xB6", 10, y + 16, ink, 9, (long) ui.font, 0);
		char t[64];
		*put_2d (t, i + 1) = 0;
		txtb (t, 24, y + 16, ink);
		fmt (t, TR ("%d rows"), pat_at (i).rows);
		ui_text_fit ((long) t, 50, y + 16, dim, 13, (long) ui.font, SIDE_W - 62);
	}
	ui_update (0, LIST_Y, SIDE_W - 1, g_list_h);
}

// ---- le piano ---------------------------------------------------------------------------------

// touche blanche, noire ? -> la couleur du canal qui la joue (0 : aucun)
static unsigned piano_lit (long white, long black)
{
	for (int c = 0; c < FMS_CH; c++)
	{
		unsigned char v = g_snd[c];
		if ((v & 128) || !(v & 7)) continue;
		if (((v >> 3) & 7) * 7 + (v & 7) - 1 == white && ((v >> 6) & 1) == black) return ch_col[c];
	}
	return 0;
}
// une noire suit cette blanche ? (pas après mi, si)
static bool piano_has_black (long white) { long n = white % 7; return n != 2 && n != 6; }

static void draw_piano (void)
{
	long top = g_h - (ST_H + PIANO_H), pw = g_w - SIDE_W;
	ui_fill (SIDE_W, top, pw, PIANO_H, c_face);
	ui_fill (SIDE_W, top, pw, 1, c_ln);
	long k = g_kw, x0 = g_kx, y = top + 7;
	ui_fill (x0 - 1, y - 1, PIANO_KEYS * k + 1, PIANO_KH + 2, 0x00989CA4);	// le lit des touches : leurs joints se voient
	for (long i = 0; i < PIANO_KEYS; i++)
	{
		unsigned c = piano_lit (i, 0);
		ui_rbox (i * k + x0, y, k - 1, PIANO_KH, 2, c ? c : 0x00FFFFFF, c ? c : 0x00ECECEC);
	}
	long bw = (long) ((unsigned long) (k * 3) / 10);		// (demi-largeur des noires : k * 3 / 10)
	for (long i = 0; i < PIANO_KEYS; i++)
	{
		if (!piano_has_black (i)) continue;
		unsigned c = piano_lit (i, 1);
		ui_rbox ((i + 1) * k + x0 - bw - 1, y, bw * 2 + 1, PIANO_BH, 2, c ? c : 0x00303030, c ? c : 0x00181818);
	}
	ui_fill (g_oct * 7 * k + x0, y + PIANO_KH + 3, 7 * k - 1, 3, c_acc);	// l'octave où le clavier écrit
	ui_update (SIDE_W, top, pw, PIANO_H);
}

// ---- la barre d'état --------------------------------------------------------------------------
static void draw_status (void)
{
	long y = g_h - ST_H;
	ui_fill (0, y, g_w, ST_H, c_hollow);
	ui_fill (0, y, g_w, 1, c_ln);
	ui_text_fit ((long) g_status, 8, y + 16, c_text, 13, (long) ui.font, g_w - 16);
	ui_update (0, y, g_w, ST_H);
}

// le titre de la fenêtre : « FM Tracker - nom * »
static void update_title (void)
{
	snprintf (g_tmp, sizeof g_tmp, "%s%s%s%s", s_app, g_path[0] ? " - " : "", g_path[0] ? base_name (g_path) : "",
		  g_modified ? " *" : "");
	ui_title ((long) g_tmp);
}

// ---- flush : repeint ce qui a changé ----------------------------------------------------------
static void flush (void)
{
	if (g_in_dlg || !g_redraw) return;
	long r = g_redraw;
	g_redraw = 0;
	if (r & D_ALL) { paint_all (); r |= D_SONG | D_BOXES | D_SCROLL | D_SIDE | D_STATUS; }
	if (r & D_BOXES)
	{
		ui_w_set_value (w_rows, cur_pat ().rows);
		ui_w_set_value (w_speed, cur_pat ().speed);
		ui_w_set_value (w_oct, g_oct);
		ui_w_set_value (w_step, g_step);
		ui_w_set_value (w_play, g_playing);
		ui_w_set_value (w_loop, g_loop);
		ui_w_set_value (w_follow, g_follow);
	}
	if (r & D_SCROLL)
	{
		long rows = cur_pat ().rows;
		if (g_sb_rows != rows || g_sb_vis != g_vis)	// la plage, si elle change
		{
			g_sb_rows = rows; g_sb_vis = g_vis;
			ui_w_set_range (w_sb, 0, rows, g_vis, 1);
		}
		if (g_top != g_sb_top) { g_sb_top = g_top; ui_w_set_value (w_sb, g_top); }
	}
	if (r & D_LCD) update_lcd ();
	if (r & D_GRID) draw_grid ();
	if (r & D_HEAD) draw_heads ();
	if (r & D_LIST) draw_plist ();
	if (r & D_PIANO) draw_piano ();
	if (r & D_STATUS) draw_status ();
	if (r & D_SIDE) draw_song_labels ();
	if (r & D_TITLE) update_title ();
	if (r & D_ALL) ui_update_all ();
}

// ---- clavier ----------------------------------------------------------------------------------

// une touche pour la grille (ui.ev) -> true si prise
static bool grid_key (void)
{
	long code = ev (EV_CODE), ch = ev (EV_CHAR), mods = ev (EV_MODS);
	bool ctrl = (mods & MODS_CTRL) != 0, shift = (mods & MODS_SHIFT) != 0;
	if (code == KEY_ESC)
	{
		if (g_sel && !g_playing) { g_sel = 0; mark (D_GRID); }
		else op_stop ();
		return true;
	}
	if (!ctrl)
	{
		static const char notes[] = "cdefgab";		// une note (la majuscule : dièse)
		for (int i = 0; notes[i]; i++)
		{
			if (ch == notes[i]) { enter_note (i + 1, 0, (int) g_oct); return true; }
			if (ch == notes[i] - 32) { enter_note (i + 1, 1, (int) g_oct); return true; }
		}
		if ((unsigned long) (ch - '0') < 8) { g_oct = ch - '0'; mark (D_BOXES | D_PIANO); return true; }	// l'octave
		if (ch == ' ') { set_cell (FMS_OFF, g_step); return true; }
		if (code == KEY_DELETE)
		{
			if (g_sel) op_clear_block ();
			else set_cell (FMS_CONT, g_step);
			return true;
		}
		if (code == KEY_BACKSPACE)
		{
			if (g_row) { move_to (g_row - 1, g_ch); set_cell (FMS_CONT, 0); }
			return true;
		}
		if (ch == '#' || ch == '+')			// le dièse de la note sous le curseur
		{
			unsigned char v = cell (cur_pat (), g_ch, g_row);
			if (!(v & 128) && (v & 7)) set_cell (fms_make_note (v & 7, !(v & 64), (v >> 3) & 7), 0);
			return true;
		}
	}
	if (ctrl)						// Ctrl + haut / bas : demi-ton (Maj : octave)
	{
		if (code == KEY_UP) { transpose (shift ? 12 : 1); return true; }
		if (code == KEY_DOWN) { transpose (shift ? -12 : -1); return true; }
		return false;
	}
	long row = g_row, c = g_ch;
	switch (code)
	{
	case KEY_UP: row--; break;
	case KEY_DOWN: row++; break;
	case KEY_LEFT: c--; break;
	case KEY_RIGHT: case KEY_TAB: c++; break;
	case KEY_PGUP: row -= g_vis; break;
	case KEY_PGDN: row += g_vis; break;
	case KEY_HOME: row = 0; break;
	case KEY_END: row = cur_pat ().rows - 1; break;
	default: return false;
	}
	if (shift && code != KEY_TAB)				// Maj : le bloc s'étend
	{
		if (!g_sel) { g_ar = g_row; g_ac = g_ch; g_sel = 1; }
		if (c < 0) c = 0;
		if (c > FMS_CH - 1) c = FMS_CH - 1;
	}
	else g_sel = 0;
	move_to (row, c);
	return true;
}

// ---- souris -----------------------------------------------------------------------------------

// -> canal * 4 + partie (0 nom, 1 M, 2 S), -1 : rien
static long head_part (long x, long y)
{
	long gx = x - (SIDE_W + NUM_W), hy = y - TOOL_H;
	if (gx < 0 || hy < 0 || hy >= HEAD_H) return -1;
	long c = (long) ((unsigned long) gx / (unsigned long) g_col_w);
	if (c >= FMS_CH) return -1;
	long cx = gx - c * g_col_w;				// x dans la colonne
	if (hy < 25) return c * 4;
	if (cx < 4) return -1;
	if (cx < 24) return c * 4 + 1;
	if (cx < 26 || cx >= 46) return -1;
	return c * 4 + 2;
}

// -> true dans la grille (colonnes) ; r = ligne, c = canal
static bool grid_cell (long x, long y, long &r, long &c)
{
	long dy = y - GRID_Y;
	if (dy < 0) dy = 0;
	r = (long) ((unsigned long) dy / ROW_H) + g_top;
	long dx = x - (SIDE_W + NUM_W);
	c = (long) ((unsigned long) (dx < 0 ? 0 : dx) / (unsigned long) g_col_w);
	if (c > FMS_CH - 1) c = FMS_CH - 1;
	long rows = cur_pat ().rows;
	if ((unsigned long) r >= (unsigned long) rows) r = rows - 1;
	return dx >= 0;
}

// (g_mx, g_my) -> le motif sous le pointeur, -1 : aucun
static long list_hit (void)
{
	if ((unsigned long) g_mx >= SIDE_W - 4) return -1;
	long y = g_my - LIST_Y;
	if (y < 0 || y >= g_list_h) return -1;
	long i = g_ltop + y / LIST_ROW;
	return i < g_song.npat ? i : -1;
}

// un clic sur le piano entre cette note
static void piano_click (long x, long y)
{
	long k = g_kw, rx = x - g_kx;				// x dans le clavier
	if (rx < 0 || (unsigned long) rx >= (unsigned long) (PIANO_KEYS * k)) return;
	long ry = y - (g_h - (ST_H + PIANO_H - 7));		// y dans le clavier
	if (ry < 0 || ry >= PIANO_KH) return;
	long white = rx / k;
	int sharp = 0;
	if (ry < PIANO_BH)					// une noire ?
	{
		long off = rx - white * k, bw = (long) ((unsigned long) (k * 3) / 10);
		if (off >= k - bw - 1 && piano_has_black (white)) sharp = 1;
		else if ((unsigned long) off <= (unsigned long) bw && white > 0 && piano_has_black (white - 1)) { white--; sharp = 1; }
	}
	g_oct = white / 7;
	enter_note ((int) (white % 7 + 1), sharp, (int) g_oct);
	mark (D_BOXES | D_PIANO);
}

// souris hors des widgets (ui.ev)
static void on_mouse (void)
{
	long kind = ev (EV_KIND), x = ev (EV_X), y = ev (EV_Y), btn = ev (EV_CODE);
	g_mx = x; g_my = y;
	if (g_drag == 1)					// glisser dans la grille : le bloc
	{
		if (kind == 2) { g_drag = 0; return; }
		if (kind != 0) return;
		long r, c;
		grid_cell (x, y, r, c);
		if (r == g_row && c == g_ch) return;
		g_sel = r != g_ar || c != g_ac;
		move_to (r, c);
		return;
	}
	if (g_drag == 2)					// le nom d'un canal : relâché dessus
	{
		if (kind != 2) return;
		g_drag = 0;
		if (head_part (x, y) == g_down_ch * 4) edit_instrument (g_down_ch);
		return;
	}
	if (kind == 0)						// survol : en-têtes, liste
	{
		long h = head_part (x, y);
		if (h != g_hot_head) { g_hot_head = h; mark (D_HEAD); }
		long l = list_hit ();
		if (l != g_hot_list) { g_hot_list = l; mark (D_LIST); }
		return;
	}
	if (kind != 1) return;
	ui_w_focus (0);						// le clavier revient à la grille
	if (!(btn & 3)) return;
	long hp = head_part (x, y);				// en-tête
	if (hp >= 0)
	{
		long c = hp >> 2, part = hp & 3;
		if (btn & 2) mute_channel (c);			// bouton droit : muet
		else if (part == 1) mute_channel (c);
		else if (part == 2) solo_channel (c);
		else { g_drag = 2; g_down_ch = c; }		// le nom : au relâché
		return;
	}
	if (x < SIDE_W)						// la liste
	{
		long i = list_hit ();
		if (i >= 0 && i != g_pat) { g_pat = i; g_sel = 0; refresh_pattern (); }
		return;
	}
	if (y < GRID_Y) return;
	if (y < GRID_Y + g_gh)					// la grille
	{
		if (x >= g_gw + SIDE_W) return;
		long r, c;
		if (!grid_cell (x, y, r, c)) return;
		if (ev (EV_MODS) & MODS_SHIFT) { if (!g_sel) { g_ar = g_row; g_ac = g_ch; g_sel = 1; } }
		else { g_sel = 0; g_ar = r; g_ac = c; }
		g_drag = 1;
		move_to (r, c);
		return;
	}
	if (y >= g_h - ST_H) return;
	piano_click (x, y);					// le piano
}

// crans (> 0 : vers le haut) : molette sur la grille ou la liste
static void on_wheel (long n)
{
	if ((unsigned long) g_mx < SIDE_W) { g_ltop -= n; mark (D_LIST); }
	else { g_top -= 3 * n; sync_scroll (); }
}

// fonction de la barre de défilement
static long on_scrollbar (long w, long code)
{
	(void) code;
	g_top = g_sb_top = ui_w_get_value (w);
	mark (D_GRID);
	return 0;
}
