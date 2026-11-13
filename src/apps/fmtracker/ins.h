//
// ins.h -- FM Tracker : les dialogues (instrument, préréglages, infos du morceau). Traduit de
// fmt_ins.inc et fmt_tables.inc (l'asm de Korin) ; inclus par main.cpp.
//
// L'éditeur d'instrument : son nom, un préréglage, l'algorithme (FM : l'opérateur 1 module le 2 ;
// additif : on entend les deux) et la rétroaction, puis pour chaque opérateur sa forme d'onde (quatre
// dessinées, un clic), son enveloppe dessinée, ses glissières et ses cases. Tester le joue ; chaque
// changement aussi. Les dessins sont ceux d'Onyx (InsDialog), calculés en entiers comme l'asm
// (sinus en table ; les tables de durées viennent du main.cpp d'Onyx).
//
// Un dialogue a sa propre boucle, modal_run : la lecture continue (tick), la note d'essai
// s'arrête, l'onde s'anime. La fenêtre principale n'est pas repeinte pendant ce temps (g_in_dlg).
//

enum { DI_OK = 1, DI_CANCEL = 2, DI_TEST = 3, DI_LOAD = 4, DI_SAVE = 5, DI_PRESET = 6, DI_NAME = 7,
       DI_CTRL = 8 };					// (DI_CTRL : glissières, cases, algorithme)

#define DLG_W		664
#define DLG_H		530
#define COLW		308
#define D_YB		50				// rangée de l'algorithme
#define D_YO		90				// haut des opérateurs
#define D_YS		202				// premières glissières
#define D_RH		22				// pas des glissières
#define D_YC		(D_YS + 7 * D_RH + 4)		// cases
#define D_YW		(D_YC + 2 * D_RH + 22)		// onde de sortie
#define D_WH		52
#define OFF_T		7000				// touche relâchée (1/10 ms)

// tables de l'éditeur (dessin seulement ; entiers, 4096 = 1 ; durées en 1/10 ms, d'après Onyx)
static const unsigned rate_a[16] = { 300000, 12304, 8006, 5209, 3390, 2206, 1435, 934, 608, 395, 257, 167, 109, 71, 46, 30 };
static const unsigned rate_d[16] = { 300000, 82030, 53374, 34729, 22597, 14703, 9567, 6225, 4050, 2635, 1715, 1116, 726, 472, 307, 200 };
static const unsigned short sus_tab[16] = { 4096, 2900, 2053, 1453, 1029, 728, 516, 365, 258, 183, 130, 92, 65, 46, 33, 23 };
static const unsigned short tl_tab[64] = {
	4096, 3757, 3446, 3161, 2900, 2660, 2440, 2238, 2053, 1883, 1727, 1584, 1453, 1333, 1223, 1122,
	1029, 944, 866, 794, 728, 668, 613, 562, 516, 473, 434, 398, 365, 335, 307, 282,
	258, 237, 217, 199, 183, 168, 154, 141, 130, 119, 109, 100, 92, 84, 77, 71,
	65, 60, 55, 50, 46, 42, 39, 35, 33, 30, 27, 25, 23, 21, 19, 18 };

static const char *const sl_names[7] = { "Attack", "Decay", "Sustain", "Release", "Volume", "Multiplier", "Key scale" };
static const char *const chk_names[4] = { "Held (sustain)", "Tremolo", "Vibrato", "Key scale rate" };
static const unsigned char sl_field[7] = { P_AR, P_DR, P_SL, P_RR, P_TL, P_MULT, P_KSL };
static const unsigned char sl_hi[7] = { 15, 15, 15, 15, 63, 15, 3 };
static const unsigned char chk_field[4] = { P_EGT, P_AM, P_VIB, P_KSR };

static short sin_tab[1024];
static long g_m_done, g_m_res, g_dlg, g_dch, g_anim;
static unsigned long g_anim_t0, g_anim_t;
static long d_name, d_conn, d_fb, d_list, d_sl[14], d_chk[8], d_wave[2];
static FmsIns d_ins, g_ins_tmp, g_ins_keep;
static char g_presets[MAX_PRESETS][PRESET_SIZE];
static long g_npreset;

// ---- sinus, ondes -----------------------------------------------------------------------------

// table du sinus (1024 pas, 4096 = 1) par récurrence : s(n+1) = 2 cos(w) s(n) - s(n-1) (les
// mêmes doubles que l'asm, sans contraction : mêmes valeurs ; arrondi au plus près, comme fcvtns)
__attribute__ ((optimize ("fp-contract=off")))
static void sin_init (void)
{
	const double c2 = 1.9999623505652, s1 = 0.00613588464915, one = 4096.0;
	double prev = 0, cur = s1;
	sin_tab[0] = 0;
	for (int i = 1; i < 1024; i++)
	{
		sin_tab[i] = (short) lrint (cur * one);
		double next = cur * c2;
		next = next - prev;
		prev = cur;
		cur = next;
	}
}

// forme (0..3), phase (1024 = un tour) -> valeur (-4096..4096)
static long wave_at (long form, long ph)
{
	long s = sin_tab[ph & 1023];
	switch (form & 3)
	{
	case 0: return s;
	case 1: return s > 0 ? s : 0;
	case 2: return s < 0 ? -s : s;
	default: return (ph & 511) < 256 ? (s < 0 ? -s : s) : 0;
	}
}

// ---- la boucle des dialogues ------------------------------------------------------------------

// fn (widget, code ; 0, 0 : souris hors des widgets), idle (0 : aucune) -> g_m_res (Échap, case de
// fermeture : 0). Comme l'asm, g_m_done et g_m_res sont communs à tous les dialogues : OK ou Annuler
// dans les préréglages ferment aussi l'éditeur d'instrument (Échap n'y ferme que les préréglages).
static long modal_run (void (*fn) (long, long), void (*idle) (void))
{
	g_m_done = 0; g_m_res = 0;
	while (!g_m_done)
	{
		long t = ui_event (20);
		tick ();
		if (idle) idle ();
		if (!t) { ui_w_idle (); continue; }
		if (ev (EV_WIN) != (int) ui.win)		// l'autre fenêtre : un clic ramène le dialogue
		{
			if (t == WEV_MOUSE && ev (EV_KIND) == 1) ui_raise (ui.win);
			continue;
		}
		if (t == WEV_CLOSE) { g_m_res = 0; break; }
		long code, taken, w = uikit_w_event (&code, &taken);
		if (taken) { if (code) fn (w, code); continue; }
		if (t == WEV_MOUSE) { fn (0, 0); continue; }
		if (t != WEV_KEY || !ev (EV_KIND)) continue;
		if (ev (EV_CODE) == KEY_ESC) { g_m_res = 0; break; }
		if (ev (EV_CODE) == KEY_ENTER) { g_m_res = 1; g_m_done = 1; }
	}
	return g_m_res;
}

// ferme le dialogue
static void modal_end (long r) { g_m_res = r; g_m_done = 1; }

static long dlg_button (long x, long y, long w, long h, const char *label, long flags, long id)
{
	long b = ui_pushbutton (x, y, w, h, (long) TR (label), flags);
	ui_w_on (b, 0, id);
	return b;
}

// ---- l'instrument en cours (d_ins) <-> les widgets --------------------------------------------

// rangée, octet -> position de la glissière (maintien, volume : atténuations à l'envers)
static long to_slider (int r, long v) { return r == 2 || r == 4 ? sl_hi[r] - v : v; }

// les widgets -> d_ins
static void dlg_collect (void)
{
	for (int o = 0; o < 2; o++)
	{
		for (int r = 0; r < 7; r++) d_ins.p[sl_field[r] + o] = (unsigned char) to_slider (r, ui_w_get_value (d_sl[o * 7 + r]));
		d_ins.p[P_WAVE + o] = (unsigned char) d_wave[o];
		for (int f = 0; f < 4; f++) d_ins.p[chk_field[f] + o] = (unsigned char) ui_w_get_value (d_chk[o * 4 + f]);
	}
	d_ins.p[P_FB] = (unsigned char) ui_w_get_value (d_fb);
	d_ins.p[P_CON] = (unsigned char) ui_w_get_value (d_conn);
	str_copy (d_ins.name, (const char *) ui_w_get_text (d_name), 9);
}

// d_ins -> les widgets
static void dlg_show (void)
{
	ui_w_set_text (d_name, (long) d_ins.name);
	for (int o = 0; o < 2; o++)
	{
		for (int r = 0; r < 7; r++) ui_w_set_value (d_sl[o * 7 + r], to_slider (r, d_ins.p[sl_field[r] + o]));
		d_wave[o] = d_ins.p[P_WAVE + o] & 3;
		for (int f = 0; f < 4; f++) ui_w_set_value (d_chk[o * 4 + f], d_ins.p[chk_field[f] + o] != 0);
	}
	ui_w_set_value (d_fb, d_ins.p[P_FB] & 7);
	ui_w_set_value (d_conn, d_ins.p[P_CON] & 1);
}

// d_ins à la voix d'essai, jouée
static void dlg_sound (void)
{
	ak_fm_from_fms ((long) g_akins, (long) d_ins.p);
	ak_fm_instrument (g_fm, TEST_VOICE, (long) g_akins);
	ak_fm_start (g_fm, TEST_VOICE, fms_note_mhz (fms_make_note (1, 0, (int) g_oct)), AKW_FM, 220);
	g_pv_voice = TEST_VOICE;
	g_pv_end = now_ms () + TEST_MS;
}

// Tester : la note, l'onde qui la suit
static void dlg_play (void)
{
	dlg_sound ();
	g_anim = 1;
	g_anim_t0 = g_anim_t = now_ms ();
}

static void dlg_wave_title (void);
static void dlg_output_wave (void);

// (attente du dialogue) l'onde suit la note
static void dlg_idle (void)
{
	if (!g_anim) return;
	unsigned long now = now_ms ();
	if (now - g_anim_t0 > 1700) { g_anim = 0; dlg_wave_title (); }
	else
	{
		if (now - g_anim_t < 40) return;
		g_anim_t = now;
	}
	dlg_output_wave ();
	ui_update (0, D_YW - 20, DLG_W, D_WH + 20);
}

// ---- dessin -----------------------------------------------------------------------------------

static long col_x (long o) { return o * (COLW + 16) + 16; }

// les quatre ondes d'un opérateur
static void draw_waves (long o)
{
	for (long k = 0; k < 4; k++)
	{
		long x = col_x (o) + 77 * k;
		bool on = d_wave[o] == k;
		unsigned bg = on ? mix (c_field, c_acc, 50) : c_field, ink = on ? c_acc : c_gnum;
		sunken (x, D_YO + 20, 71, 30, 5, bg);
		if (on) ui_fill (x, D_YO + 20, 71, 2, ink);	// choisie : cadre accent
		long py = -1;
		for (long i = 0; i <= 59; i++)			// deux périodes
		{
			long y = D_YO + 35 - ((wave_at (k, i * 2048 / 59) * 9) >> 12);
			long a = y, b = y;
			if (py >= 0) { a = py < y ? py : y; b = py < y ? y : py; }
			ui_fill (x + 6 + i, a, 2, b - a + 2, ink);
			py = y;
		}
	}
}

// un trait de 2 px, couleur accent
static void seg_line (long x0, long y0, long x1, long y1) { ui_vp_line (x0 << 4, y0 << 4, x1 << 4, y1 << 4, 32, c_acc); }

// l'enveloppe d'un opérateur (monte au volume, redescend au maintien, tenue ou chutant encore, puis
// le relâchement une fois la touche lâchée)
static void draw_env (long o)
{
	long x = col_x (o);
	sunken (x, D_YO + 54, COLW, 54, 5, c_field);
	const unsigned char *p = d_ins.p;
	long ar = p[P_AR + o] & 15, dr = p[P_DR + o] & 15, sl = p[P_SL + o] & 15, rr = p[P_RR + o] & 15, tl = p[P_TL + o] & 63;
	long top = tl * (54 - 22) / 63 + D_YO + 54 + 8, bot = D_YO + 54 + 54 - 7;
	long sy = top + (bot - top) * sl / 15;
	long xa = x + 10 + (15 - ar) * 3, xd = xa + 6 + (15 - dr) * 3, xs = xd + 70, xr = xs + 6 + (15 - rr) * 4;
	if (xr > x + COLW - 8) xr = x + COLW - 8;
	long ey = p[P_EGT + o] ? sy : sy + ((bot - sy) >> 1);	// pas tenu : ça continue de chuter
	ui_fill (x + 6, bot + 1, COLW - 12, 1, c_gline);	// ligne de base, touche lâchée
	ui_fill (xs, D_YO + 54 + 6, 1, bot - (D_YO + 54 + 5), c_gline);
	seg_line (x + 10, bot, xa, top);
	seg_line (xa, top, xd, sy);
	seg_line (xd, sy, xs, ey);
	seg_line (xs, ey, xr, bot);
}

// opérateur, t (ms depuis l'appui ; touche lâchée à 700 ms) -> son niveau (0..4096) : esquisse de
// l'enveloppe
static unsigned long env_at (long o, unsigned long t)
{
	const unsigned char *p = d_ins.p + o;
	unsigned long ta = rate_a[p[P_AR] & 15], td = rate_d[p[P_DR] & 15], tr = rate_d[p[P_RR] & 15], sus = sus_tab[p[P_SL] & 15];
	t *= 10;						// (1/10 ms)
	unsigned long held = t > OFF_T ? OFF_T : t, v;
	if (held < ta) v = 4096 * held / ta;
	else
	{
		held -= ta;
		if (held < td) v = 4096 - (4096 - sus) * held / td;
		else
		{
			v = sus;
			if (!p[P_EGT])
			{
				held -= td;
				if (held >= tr) return 0;
				v = v * (tr - held) / tr;
			}
		}
	}
	if (t <= OFF_T) return v;
	unsigned long r = t - OFF_T;
	if (r >= tr) return 0;
	return v * (tr - r) / tr;
}

// opérateur -> amplitude (0..4096) : volume, et enveloppe en animation
static long op_amp (long o)
{
	unsigned long a = tl_tab[d_ins.p[P_TL + o] & 63];
	if (g_anim) a = a * env_at (o, now_ms () - g_anim_t0) >> 12;
	return (long) a;
}

// le titre de l'onde
static void dlg_wave_title (void)
{
	ui_fill (16, D_YW - 20, DLG_W - 32, 20, c_face);
	txtb (TR (g_anim ? "The sound's wave, as the note sounds" : "The sound's wave (two periods, at full volume)"), 16, D_YW - 6, c_text);
}

// L'onde du son sur deux périodes : l'opérateur 1 (réinjecté en lui-même) courbe la phase du 2 (FM)
// ou s'y ajoute ; chacun à son volume et, pendant Tester, au niveau de son enveloppe.
static void dlg_output_wave (void)
{
	sunken (16, D_YW, DLG_W - 32, D_WH, 5, c_field);
	ui_fill (22, D_YW + D_WH / 2, DLG_W - 44, 1, c_gline);	// l'axe
	long a0 = op_amp (0), a1 = op_amp (1);
	const unsigned char *p = d_ins.p;
	long m0 = (p[P_MULT] & 15) * 2, m1 = (p[P_MULT + 1] & 15) * 2;	// (en demis ; 0 : 1/2)
	if (!m0) m0 = 1;
	if (!m1) m1 = 1;
	long fbk = p[P_FB] & 7, depth = fbk ? 16L << fbk : 0;	// profondeur de la rétroaction
	bool add = p[P_CON] & 1;
	long w0 = p[P_WAVE], w1 = p[P_WAVE + 1];
	long e1 = 0, e2 = 0, py = -1;
	const unsigned long n = (DLG_W - 44) * 8;
	for (unsigned long i = 0; i <= n; i++)
	{
		long ph = (long) (((unsigned long) m0 * i << 10) / n) + (((e1 + e2) >> 1) * depth >> 12);	// + rétroaction
		long mo = wave_at (w0, ph) * a0 >> 12;
		e2 = e1; e1 = mo;
		long ph2 = (long) (((unsigned long) m1 * i << 10) / n), out;
		if (!add) out = wave_at (w1, ph2 + (mo >> 1)) * a1 >> 12;	// FM
		else out = ((wave_at (w1, ph2) * a1 >> 12) + mo) >> 1;
		if (i & 7) continue;					// une colonne
		long y = D_YW + D_WH / 2 - (out * (D_WH / 2 - 7) >> 12);
		long a = y, b = y;
		if (py >= 0) { a = py < y ? py : y; b = py < y ? y : py; }
		ui_fill ((long) (i >> 3) + 22, a, 2, b - a + 2, c_acc);
		py = y;
	}
}

// une boîte d'opérateur du dessin de l'algorithme
static void algo_box (long x, long y, const char *s)
{
	sunken (x, y, 50, 22, 4, c_field);
	txt (TR (s), x + 12, y + 16, c_text);
}

// les dessins de l'algorithme
static void draw_algo (void)
{
	ui_fill (270, D_YB - 12, 170, 50, c_face);
	long ax = 276, ay = D_YB + 2, ox;
	if (d_ins.p[P_CON] & 1)					// additif : les deux à la sortie
	{
		algo_box (ax, ay - 10, "Op 1");
		algo_box (ax, ay + 14, "Op 2");
		seg_line (ax + 50, ay + 1, ax + 70, ay + 1);
		seg_line (ax + 50, ay + 25, ax + 70, ay + 25);
		seg_line (ax + 70, ay + 1, ax + 70, ay + 25);
		seg_line (ax + 70, ay + 13, ax + 92, ay + 13);
		ox = ax + 98;
	}
	else
	{
		algo_box (ax, ay, "Op 1");
		algo_box (ax + 70, ay, "Op 2");
		seg_line (ax + 50, ay + 11, ax + 70, ay + 11);
		seg_line (ax + 120, ay + 11, ax + 136, ay + 11);
		ox = ax + 140;
	}
	txt ("out", ox, ay + 16, c_text2);
}

static void d_label (const char *s, long x, long base) { txt (TR (s), x, base, c_text); }

// tout le dialogue
static void dlg_paint (void)
{
	char t[32];
	ui_fill (0, 0, DLG_W, DLG_H, c_face);
	d_label ("Name", 16, 12 + 18);
	d_label ("Algorithm", 16, D_YB + 18);
	draw_algo ();
	d_label ("Feedback", 442, D_YB + 18);
	sprintf (t, "%d", d_ins.p[P_FB] & 7);
	txt (t, 624, D_YB + 18, c_text);
	bool add = d_ins.p[P_CON] & 1;
	for (long o = 0; o < 2; o++)				// opérateurs
	{
		long x = col_x (o);
		txtb (TR (add ? (o ? "Operator 2" : "Operator 1")
			     : (o ? "Operator 2 - carrier (what is heard)" : "Operator 1 - modulator (the timbre)")), x, D_YO + 14, c_text);
		draw_waves (o);
		draw_env (o);
		for (int r = 0; r < 7; r++)
		{
			long base = D_RH * r + D_YS + 15;
			d_label (sl_names[r], x, base);
			long v = ui_w_get_value (d_sl[o * 7 + r]);	// la valeur ; multiplicateur : x1/2, x1, x2...
			if (r == 5) { if (v) sprintf (t, "x%ld", v); else strcpy (t, "x1/2"); }
			else sprintf (t, "%ld", v);
			txt (t, x + COLW - 30, base, c_text);
		}
	}
	dlg_wave_title ();
	dlg_output_wave ();
	txt (TR ("Test plays it: the wave moves with the note."), 118, DLG_H - 22, c_text2);
	ui_w_draw (0);
	ui_update_all ();
}

// ---- les évènements de l'éditeur --------------------------------------------------------------

// une commande a changé : le dessin, le son
static void dlg_changed (void) { dlg_collect (); dlg_paint (); dlg_sound (); }

// souris hors des widgets : les ondes
static void dlg_click (void)
{
	if (ev (EV_KIND) != 1) return;
	long x = ev (EV_X), y = ev (EV_Y);
	if ((unsigned long) (y - (D_YO + 20)) >= 30) return;
	for (long o = 0; o < 2; o++)
	{
		long rx = x - col_x (o);
		if (rx < 0 || rx >= COLW) continue;
		long k = rx / 77;
		if (k > 3) return;
		d_wave[o] = k;
		dlg_changed ();
		return;
	}
}

// g_ins_tmp devient l'instrument en cours
static void dlg_take (void) { d_ins = g_ins_tmp; dlg_show (); dlg_play (); }

// ---- les préréglages --------------------------------------------------------------------------

// ordre sans casse (ASCII)
static int ci_cmp (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int x = (unsigned char) *a, y = (unsigned char) *b;
		if (x >= 'a' && x <= 'z') x -= 32;
		if (y >= 'a' && y <= 'z') y -= 32;
		if (x != y) return x - y;
		if (!x) return 0;
	}
}

// les .FMI de SD0:/apps/fmtracker.app/ins, triés (au premier besoin)
static void load_presets (void)
{
	if (g_npreset) return;
	DIR *d = opendir (s_ins_dir);
	if (!d) return;
	long n = 0;
	struct dirent *e;
	while (n < MAX_PRESETS && (e = readdir (d)) != 0)
	{
		if (e->d_type == DT_DIR) continue;
		size_t l = strlen (e->d_name);
		if (l < 5 || e->d_name[l - 4] != '.' || (e->d_name[l - 1] | 32) != 'i') continue;
		char b[260];
		str_copy (b, e->d_name, sizeof b);
		b[l - 4] = 0;					// sans l'extension
		str_copy (g_presets[n++], b, PRESET_SIZE);
	}
	closedir (d);
	g_npreset = n;
	for (long i = 1; i < n; i++)				// tri par insertion
		for (long j = i; j > 0 && ci_cmp (g_presets[j - 1], g_presets[j]) > 0; j--)
		{
			char t[PRESET_SIZE];
			memcpy (t, g_presets[j], PRESET_SIZE); memcpy (g_presets[j], g_presets[j - 1], PRESET_SIZE); memcpy (g_presets[j - 1], t, PRESET_SIZE);
		}
}

// ce préréglage dans g_ins_tmp -> true si lu
static bool preset_read (long i)
{
	snprintf (g_dlgpath, sizeof g_dlgpath, "%s/%s.FMI", s_ins_dir, g_presets[i]);
	return read_fmi (g_dlgpath, &g_ins_tmp);
}

// (preset_dialog) widget, code
static void pd_on (long w, long code)
{
	if (!w) return;
	if (w == d_list)
	{
		if (code == UN_ACTIVATE) { modal_end (1); return; }
		if (code != UN_CHANGE) return;
		long v = ui_w_get_value (w);
		if (v < 0 || !preset_read (v)) return;
		d_ins = g_ins_tmp;
		dlg_sound ();
		return;
	}
	if (code != UN_ACTIVATE) return;
	modal_end (ui_w_user (w) == DI_OK ? 1 : 0);
}

// la liste des préréglages : un clic l'essaie, OK le garde
static void preset_dialog (void)
{
	dlg_collect ();
	g_ins_keep = d_ins;					// pour Annuler
	load_presets ();
	long dlg = ui_dialog_begin (280, 430, (long) TR ("Presets"));
	if (!dlg) return;
	long l = ui_list (12, 12, 256, 364, UL_NOSORT);
	d_list = l;
	ui_list_column (l, (long) "", 230, 0);
	for (long i = 0; i < g_npreset; i++) ui_list_add (l, (long) g_presets[i], i);
	dlg_button (280 - 192, 430 - 42, 86, 30, "OK", UB_DEFAULT, DI_OK);
	dlg_button (280 - 98, 430 - 42, 86, 30, "Cancel", 0, DI_CANCEL);
	ui_fill (0, 0, 280, 430, c_face);
	ui_w_draw (0);
	ui_update_all ();
	ui_w_focus (l);
	long r = modal_run (pd_on, 0);
	ui_dialog_end (dlg);
	if (!r) d_ins = g_ins_keep;				// Annuler : l'instrument d'avant
	dlg_show ();
}

// widget (0 : souris libre), code
static void dlg_on (long w, long code)
{
	if (!w) { dlg_click (); return; }
	long id = ui_w_user (w);
	if (id == DI_CTRL) { if (code == UN_CHANGE) dlg_changed (); return; }
	if (code != UN_ACTIVATE) return;
	switch (id)
	{
	case DI_OK: dlg_collect (); modal_end (1); return;
	case DI_CANCEL: modal_end (0); return;
	case DI_TEST: dlg_collect (); dlg_play (); return;
	case DI_LOAD:
		if (ui_file_open ((long) s_ins_dir, (long) g_dlgpath, 256))
		{
			if (read_fmi (g_dlgpath, &g_ins_tmp)) dlg_take ();
			else ui_message ((long) TR ("Instrument"), (long) TR ("Not an .FMI instrument file."), UM_OK);
		}
		break;
	case DI_SAVE:
	{
		dlg_collect ();
		char name[64];					// nom proposé
		str_copy (name, d_ins.name[0] ? d_ins.name : "INSTR", 9);
		ensure_ext (name, ".FMI");
		if (ui_file_save ((long) s_ins_dir, (long) name, (long) g_dlgpath, 256))
		{
			static char b[1028];
			ensure_ext (g_dlgpath, ".FMI");
			long n = fmi_write (&d_ins, b);
			if (write_file (g_dlgpath, b, n) < 0) ui_message ((long) TR ("Instrument"), (long) TR ("Cannot write the file."), UM_OK);
		}
		break;
	}
	case DI_PRESET: preset_dialog (); break;
	default: return;
	}
	dlg_paint ();
}

// l'éditeur d'instrument d'un canal
static void edit_instrument (long ch)
{
	g_dch = ch;
	d_ins = g_song.ins[ch];
	fmt (g_tmp, TR ("Instrument of channel %d"), ch + 1);
	g_in_dlg = 1;
	g_anim = 0;
	g_dlg = ui_dialog_begin (DLG_W, DLG_H, (long) g_tmp);
	if (!g_dlg) { g_in_dlg = 0; return; }
	d_name = ui_textbox (64, 12, 110, 28, 0);			// nom
	ui_w_set_range (d_name, 0, 8, 0, 0);
	ui_w_on (d_name, 0, DI_NAME);
	dlg_button (186, 12, 190, 28, "(preset...)", 0, DI_PRESET);	// préréglages, charger, enregistrer
	dlg_button (DLG_W - 228, 12, 96, 28, "Load...", 0, DI_LOAD);
	dlg_button (DLG_W - 128, 12, 112, 28, "Save...", 0, DI_SAVE);
	snprintf (g_tmp, sizeof g_tmp, "FM|%s", TR ("Additive"));	// algorithme
	d_conn = ui_segmented (96, D_YB, 170, 26, (long) g_tmp);
	ui_w_on (d_conn, 0, DI_CTRL);
	d_fb = ui_slider (524, D_YB + 3, 92, 20, 0, 7, 0);		// rétroaction
	ui_w_on (d_fb, 0, DI_CTRL);
	for (long o = 0; o < 2; o++)					// opérateurs
	{
		long x = col_x (o);
		for (int r = 0; r < 7; r++)				// glissières
		{
			d_sl[o * 7 + r] = ui_slider (x + 92, D_RH * r + D_YS, COLW - 128, 20, 0, sl_hi[r], 0);
			ui_w_on (d_sl[o * 7 + r], 0, DI_CTRL);
		}
		for (int f = 0; f < 4; f++)				// cases
		{
			d_chk[o * 4 + f] = ui_checkbox ((f & 1) * 150 + x, (f >> 1) * D_RH + D_YC, 148, 22, (long) TR (chk_names[f]), 0);
			ui_w_on (d_chk[o * 4 + f], 0, DI_CTRL);
		}
	}
	dlg_button (16, DLG_H - 42, 90, 30, "Test", 0, DI_TEST);	// boutons du bas
	dlg_button (DLG_W - 196, DLG_H - 42, 86, 30, "OK", UB_DEFAULT, DI_OK);
	dlg_button (DLG_W - 102, DLG_H - 42, 86, 30, "Cancel", 0, DI_CANCEL);
	dlg_show ();
	dlg_paint ();
	long r = modal_run (dlg_on, dlg_idle);
	ui_dialog_end (g_dlg);
	g_in_dlg = 0;
	g_anim = 0;
	ak_fm_stop (g_fm, TEST_VOICE);
	g_pv_voice = -1;
	if (r)
	{
		g_song.ins[g_dch] = d_ins;
		upload (g_dch);
		set_modified ();
	}
	g_hot_head = -1;					// (le pointeur a bougé)
	mark (D_ALL);
}

// ---- les informations du morceau --------------------------------------------------------------

// y, libellé, texte, longueur max, largeur -> le champ
static long info_field (long y, const char *label, const char *text, long max, long w)
{
	ui_label (12, y + 4, 96, 20, (long) TR (label), 0);
	long tb = ui_textbox (112, y, w, 28, 0);
	ui_w_set_range (tb, 0, max, 0, 0);
	ui_w_set_text (tb, (long) text);
	return tb;
}

static void op_info (void)
{
	op_stop ();
	long dlg = ui_dialog_begin (460, 190, (long) "Song information");	// (comme l'asm : le titre passé tel quel)
	if (!dlg) return;
	long t = info_field (14, "Title", g_song.title, 20, 200);
	long a = info_field (14 + 34, "Author", g_song.author, 20, 200);
	long c = info_field (14 + 68, "Comment", g_song.comment, 50, 336);
	long x = ui_dialog_button (dlg, 0, (long) "OK", 1, UB_DEFAULT, 460 - 12);
	ui_dialog_button (dlg, 1, (long) "Cancel", 0, 0, x);
	ui_w_focus (t);
	if (ui_dialog_run (dlg, 0, 0))
	{
		str_copy (g_song.title, (const char *) ui_w_get_text (t), 21);
		str_copy (g_song.author, (const char *) ui_w_get_text (a), 21);
		str_copy (g_song.comment, (const char *) ui_w_get_text (c), 51);
		set_modified ();
		mark (D_SIDE);
	}
	ui_dialog_end (dlg);
}
