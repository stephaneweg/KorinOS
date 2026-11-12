/* uk_more.c -- UIKit : calendrier, sélecteur de date, palette, sélecteur de
 * couleur et son dialogue. Port ligne à ligne de uikit_more.inc
 * (src/kits/uikit/PORT.md).
 *
 *	UT_CAL		calendrier d'un mois (Onyx calendar.cpp) : valeur = date
 *			(année << 16 | mois << 8 | jour)
 *	UT_DATE		sélecteur de date (Onyx DatePicker) : un champ
 *			« AAAA-MM-JJ » qui ouvre un calendrier dessous
 *	UT_PALETTE	grille de 8 x 5 couleurs (Onyx colorpick.cpp)
 *	UT_COLOR	sélecteur de couleur : une pastille qui ouvre la palette
 * ui_color_dialog : dialogue de couleur (palette, rouge / vert / bleu). */
#include "uk.h"

/* ---------------------------------------------------------------------------
 *  Dates
 * ------------------------------------------------------------------------- */

static const u8 mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; /* uk_mdays */
static const u8 wd_t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};               /* uk_wd_t */

/* table[m - 1] comme ldrb w,[table, m - 1] : mois 0 -> l'octet d'avant la
 * table, le dernier de l'instruction ret qui la précède (0xD6) ; au-delà de
 * 12, l'asm lisait le code qui suit (ici 0) */
static long month_byte(const u8 *t, long m)
{
	if (ULT(m - 1, 12))
		return t[m - 1];
	return m == 0 ? 0xD6 : 0; /* comme l'asm (mois 0) */
}

/* uk_month_days : x0 = année, x1 = mois (1..12) -> x0 = jours du mois */
static long month_days(long y, long m)
{
	if (m != 2)
		return month_byte(mdays, m);
	/* février : bissextile ? */
	if (y & 3)
		return 28;
	if (UREM(y, 100))
		return 29;
	if (UREM(y, 400))
		return 28;
	return 29;
}

/* uk_weekday : x0 = année, x1 = mois, x2 = jour -> x0 = jour de la semaine
 * (0 lundi .. 6 dimanche) (Sakamoto) */
static long weekday(long y, long m, long d)
{
	long t = month_byte(wd_t, m);
	long s;
	if (ULT(m, 3))
		y -= 1;
	s = y + LSR(y, 2); /* y + y/4 - y/100 + y/400 */
	s -= UDIV(y, 100);
	s += UDIV(y, 400);
	s += t;
	s += d;
	s = UREM(s, 7); /* 0 dimanche -> lundi d'abord */
	s += 6;
	return UREM(s, 7);
}

/* x0 = date (a << 16 | m << 8 | j), x1 = tampon -> « AAAA-MM-JJ » ; rend
 * r0 = la date (x0 inchangé), r1 = le NUL (x1 : lu par uikit_dialog.inc) */
ukr uk_date_text(long date, long buf)
{
	ukr r;
	long p = buf;
	long v = LSR(date, 16); /* année : 4 chiffres */
	long div = 1000, n = 4, q;
	do {
		q = UDIV(v, div);
		STB(p, 0, q + '0');
		p++;
		v -= q * div;
		div = UDIV(div, 10);
	} while (--n);
	STB(p, 0, '-');
	p++;
	/* .two : deux chiffres */
	v = (date >> 8) & 0xFF;
	STB(p, 0, UDIV(v, 10) + '0');
	STB(p, 1, v - UDIV(v, 10) * 10 + '0');
	p += 2;
	STB(p, 0, '-');
	p++;
	v = date & 0xFF;
	STB(p, 0, UDIV(v, 10) + '0');
	STB(p, 1, v - UDIV(v, 10) * 10 + '0');
	p += 2;
	STB(p, 0, 0);
	r.r0 = date;
	r.r1 = p;
	r.r2 = r.r3 = 0;
	return r;
}

/* ---------------------------------------------------------------------------
 *  Calendrier
 * ------------------------------------------------------------------------- */

/* uk_month_names, uk_s_wdays, uk_s_prev, uk_s_next */
static const char month_names[] = "janvier\0f\xC3\xA9vrier\0mars\0avril\0mai\0juin\0"
				  "juillet\0ao\xC3\xBBt\0septembre\0octobre\0novembre\0"
				  "d\xC3\xA9" "cembre";
static const char s_wdays[] = "L\0M\0M\0J\0V\0S\0D";
static const char s_prev[] = "\xE2\x80\xB9"; /* « ‹ » */
static const char s_next[] = "\xE2\x80\xBA"; /* « › » */

/* x0..x3 = rectangle (0 : 7 x 34 sur 30 + 22 + 6 x 30), x4 = date ->
 * poignée (valeur : la date choisie) */
long ui_calendar(long a0, long a1, long a2, long a3, long a4)
{
	long w;
	if (!a2)
		a2 = 7 * 34;
	if (!a3)
		a3 = UK_CAL_HEAD + UK_CAL_WD + 6 * 30;
	w = uk_w_new(UT_CAL, a0, a1, a2, a3);
	if (!w)
		return 0;
	STX(w, UW_VALUE, a4);
	STX(w, UW_AUX2, a4 & ~0xFFL); /* mois montré */
	STX(w, UW_HOT, -1);
	return w;
}

/* x0 = calendrier, x1 = date : montrée et choisie (son mois suit) */
long ui_calendar_show(long a0, long a1)
{
	STX(a0, UW_VALUE, a1);
	STX(a0, UW_AUX2, a1 & ~0xFFL);
	return a0;
}

/* x0 = calendrier, x1 = date d'aujourd'hui (entourée ; 0 : aucune) */
long ui_calendar_today(long a0, long a1)
{
	if (!a0)
		return 0;
	STX(a0, UW_AUX, a1);
	return uk_w_refresh(a0);
}

/* uk_cal_arrow : (uk_cal_draw) x0 = flèche, x1 = x : dans la ligne du mois
 * (y : x21 de l'appelant), TEXTE2 */
static long cal_arrow(long s, long x, long y)
{
	return uk_text_box(s, x, y, UK_CAL_HEAD, JET(TJ_TEXT2), uk_bold(), 0, 16);
}

long uk_cal_draw(long w)
{
	/* le titre allait dans les 64 octets de pile de l'asm (un nom de mois
	 * traduit de plus de ~40 octets les débordait) : ici 160 */
	u8 title[160];
	long x = SXTW(LDW(w, UW_X));
	long y = SXTW(LDW(w, UW_Y));
	long wd = LDW(w, UW_W);
	long ht = LDW(w, UW_H);
	long shown, year, month, s, k, p, t, end, bold, tw, cw, ch, i, wds, first, ndays, day;

	ui_fill(x, y, wd, ht, JET(TJ_SURFACE));
	shown = LDX(w, UW_AUX2); /* mois montré */
	year = LSR(shown, 16);
	month = (shown >> 8) & 0xFF;
	/* titre : « octobre 2026 » (TR) */
	/* comme l'asm : mois 0 -> k = -1, la boucle lit au-delà des noms jusqu'à
	 * une faute (date 0 : le calendrier plante, dans l'asm aussi) */
	s = S(month_names);
	k = month - 1;
	while (k) {
		long c = LDB(s, 0);
		s++;
		if (!c)
			k--;
	}
	t = ui_tr(s);
	p = (long)(uintptr_t)title;
	for (;;) {
		long c = LDB(t, 0);
		t++;
		if (!c)
			break;
		STB(p, 0, c);
		p++;
	}
	STB(p, 0, ' ');
	p++;
	end = fmt_dec(year, p);
	STB(end, 0, 0);
	bold = uk_bold();
	tw = ui_text_width((long)(uintptr_t)title, 14, bold);
	uk_text_box((long)(uintptr_t)title, x + LSR(wd - tw, 1), y, UK_CAL_HEAD, JET(TJ_TEXT1), bold, 0, 14);
	cal_arrow(S(s_prev), x + 10, y); /* « ‹ » et « › » */
	cal_arrow(S(s_next), x + wd - 20, y);
	cw = UDIV(wd, 7);                                 /* largeur d'une case */
	ch = UDIV(ht - (UK_CAL_HEAD + UK_CAL_WD), 6);     /* hauteur d'une case */
	/* jours de la semaine (TR) */
	wds = S(s_wdays);
	for (i = 0; ULT(i, 7); i++) {
		t = ui_tr(wds);
		tw = ui_text_width(t, 12, uk_font_r());
		long tx = x + (i * cw + LSR(cw - tw, 1));
		uk_text_box(t, tx, y + UK_CAL_HEAD, UK_CAL_WD, JET(TJ_TEXT3), uk_font_r(), 0, 12);
		while (LDB(wds, 0)) /* .ws : jusqu'après le NUL */
			wds++;
		wds++;
	}
	/* le 1er : sa case */
	shown = LDX(w, UW_AUX2);
	first = weekday(LSR(shown, 16), (shown >> 8) & 0xFF, 1);
	ndays = month_days(LSR(shown, 16), (shown >> 8) & 0xFF);
	for (day = 1; ULE(day, ndays); day++) {
		long cell = first + day - 1; /* case 0..41 */
		long row = UDIV(cell, 7);
		long col = cell - row * 7; /* colonne, ligne */
		long cx = x + col * cw;
		long cy = row * ch + y + (UK_CAL_HEAD + UK_CAL_WD);
		long date = LDX(w, UW_AUX2) | day; /* cette date */
		long ink = JET(TJ_TEXT1);
		long c;
		if (date == LDX(w, UW_VALUE)) {
			/* choisie : disque ACCENT, texte blanc */
			c = JET(TJ_ACCENT);
			ink = JET(TJ_ACCENT_INK);
			ui_rbox(cx + 2, cy + 2, cw - 4, ch - 4, 6, c, c);
		} else if (date == LDX(w, UW_HOT)) {
			/* survolée : CREUX */
			c = JET(TJ_SUNKEN);
			ui_rbox(cx + 2, cy + 2, cw - 4, ch - 4, 6, c, c);
		}
		/* aujourd'hui : entourée ACCENT */
		if (date == LDX(w, UW_AUX))
			ui_frame(cx + 2, cy + 2, cw - 4, ch - 4, JET(TJ_ACCENT));
		/* le numéro, centré */
		end = fmt_dec(day, UKA(numbuf));
		STB(end, 0, 0);
		long px = LDX(uk_met(), MT_PX);
		tw = ui_text_width(UKA(numbuf), px, uk_font_r());
		uk_text_box(UKA(numbuf), cx + LSR(cw - tw, 1), cy, ch, ink, uk_font_r(), 0, px);
	}
	return 0;
}

/* uk_cal_month : x0 = calendrier, x1 = -1 / +1 : mois montré précédent /
 * suivant */
static long cal_month(long w, long d)
{
	long v = LDX(w, UW_AUX2);
	long y = LSR(v, 16);
	long m = ((v >> 8) & 0xFF) + d;
	if (!m) {
		m = 12;
		y -= 1;
	}
	if (m == 13) {
		m = 1;
		y += 1;
	}
	STX(w, UW_AUX2, LSL(y, 16) | LSL(m, 8));
	return uk_w_refresh(w);
}

/* uk_cal_at : x0 = calendrier, x1, x2 = point local -> x0 = date de la case
 * (0 : aucune) */
static long cal_at(long w, long x, long y)
{
	long ly, cw, ch, col, row, cell, shown, day;
	if (y < UK_CAL_HEAD + UK_CAL_WD)
		return 0;
	ly = y - (UK_CAL_HEAD + UK_CAL_WD);
	cw = UDIV(LDW(w, UW_W), 7);                             /* largeur d'une case */
	ch = UDIV(LDW(w, UW_H) - (UK_CAL_HEAD + UK_CAL_WD), 6); /* hauteur */
	col = UDIV(x, cw);                                      /* colonne */
	if (UGE(col, 7))
		return 0;
	row = UDIV(ly, ch); /* ligne */
	if (UGE(row, 6))
		return 0;
	cell = row * 7 + col; /* case */
	shown = LDX(w, UW_AUX2);
	day = cell - weekday(LSR(shown, 16), (shown >> 8) & 0xFF, 1) + 1; /* jour */
	if (day < 1)
		return 0;
	if (day > month_days(LSR(shown, 16), (shown >> 8) & 0xFF))
		return 0;
	return shown | day;
}

/* souris : flèches du mois, survol, clic d'un jour (double : UN_ACTIVATE) */
long uk_cal_mouse(long w, long kind, long x, long y, long clicks)
{
	long d;
	if (kind == 3) {
		d = -1;
		goto hot;
	}
	if (y < UK_CAL_HEAD) {
		/* ligne du mois : les flèches */
		if (kind != 1)
			return 0;
		long wd = LDW(w, UW_W);
		if (x < 30) {
			cal_month(w, -1);
		} else {
			if (x < wd - 30)
				return 0;
			cal_month(w, 1);
		}
		return 0;
	}
	d = cal_at(w, x, y);
	if (kind == 1) {
		if (!d)
			return 0;
		STX(w, UW_VALUE, d);
		uk_w_refresh(w);
		if (clicks == 2)
			return UN_ACTIVATE;
		return UN_CHANGE;
	}
	if (!d)
		d = -1;
hot:
	if (d == LDX(w, UW_HOT))
		return 0;
	STX(w, UW_HOT, d);
	uk_w_refresh(w);
	return 0;
}

/* uk_cal_shift : x0 = calendrier, x1 = -1 / +1 : la date choisie passe au
 * mois voisin (jour inchangé ici ; uk_cal_key le corrige) */
static long cal_shift(long w, long d)
{
	long v = LDX(w, UW_VALUE);
	long y = LSR(v, 16);
	long m = ((v >> 8) & 0xFF) + d;
	if (!m) {
		m = 12;
		y -= 1;
	}
	if (m == 13) {
		m = 1;
		y += 1;
	}
	v = (v & 0xFF) | LSL(y, 16) | LSL(m, 8);
	STX(w, UW_VALUE, v);
	return v; /* (x2 de l'asm : la nouvelle date, que uk_cal_key reprend) */
}

/* clavier : flèches (jour, semaine), Page (mois), Entrée (UN_ACTIVATE) */
long uk_cal_key(long w, long code, long ch, long mods)
{
	u32 k = (u32)code;
	long d, v, day, md;
	(void)ch;
	(void)mods;
	if (k == KEY_ENTER)
		return UN_ACTIVATE;
	d = 0; /* jours à ajouter */
	if (k == KEY_LEFT)
		d = -1;
	if (k == KEY_RIGHT)
		d = 1;
	if (k == KEY_UP)
		d = -7;
	if (k == KEY_DOWN)
		d = 7;
	if (k == KEY_PGUP) {
		cal_month(w, -1);
		return 0;
	}
	if (k == KEY_PGDN) {
		cal_month(w, 1);
		return 0;
	}
	if (!d)
		return -1;
	/* jour +/- d, dans le mois voisin */
	v = LDX(w, UW_VALUE);
	day = (v & 0xFF) + d;
	if (day < 1) {
		/* avant le 1er : mois d'avant */
		/* comme l'asm : x2 (le jour) n'est pas gardé autour de uk_cal_shift,
		 * qui y laisse la nouvelle date : jour = date + jours du mois, puis
		 * « orr » avec la date (le 1er moins un jour donne le 31 d'un mois de 30) */
		day = cal_shift(w, -1);
		v = LDX(w, UW_VALUE);
		day += month_days(LSR(v, 16), (v >> 8) & 0xFF);
	} else {
		md = month_days(LSR(v, 16), (v >> 8) & 0xFF);
		if (day > md) {
			day -= md; /* après le dernier : mois d'après */
			cal_shift(w, 1);
		}
	}
	v = (LDX(w, UW_VALUE) & ~0xFFL) | day;
	STX(w, UW_VALUE, v);
	STX(w, UW_AUX2, v & ~0xFFL); /* le mois montré suit */
	uk_w_refresh(w);
	return UN_CHANGE;
}

/* ---------------------------------------------------------------------------
 *  Sélecteur de date
 * ------------------------------------------------------------------------- */

/* x0..x3 = rectangle (largeur 0 : 140, hauteur 0 : un contrôle), x4 = date
 * -> poignée (valeur : la date) */
long ui_datepicker(long a0, long a1, long a2, long a3, long a4)
{
	long w;
	if (!a2)
		a2 = 140;
	w = uk_new_sized(UT_DATE, a0, a1, a2, a3, 0, 0);
	if (w)
		STX(w, UW_VALUE, a4);
	return w;
}

long uk_dp_draw(long w)
{
	uk_date_text(LDX(w, UW_VALUE), UKA(numbuf)); /* le texte : la date */
	uk_set_text(w, UKA(numbuf), -1);
	return uk_dd_draw(w); /* dessin d'une liste déroulante */
}

/* uk_dp_open : x0 = sélecteur : le calendrier sous lui -> x0 = UN_CHANGE si
 * une autre date est choisie */
static long dp_open(long w)
{
	long d = ui_date_popup(w, LDX(w, UW_VALUE), LDX(w, UW_AUX));
	if (d < 0)
		return 0;
	if (d == LDX(w, UW_VALUE))
		return 0;
	STX(w, UW_VALUE, d);
	uk_w_refresh(w);
	return UN_CHANGE;
}

/* uk_dp_fn : (dialogue de date) x0 = dialogue, x1 = widget, x2 = code */
static long dp_fn(long dlg, long w, long code)
{
	if (w != LDX(dlg, DG_LIST))
		return uk_dlg_buttons_fn(dlg, w, code);
	if (code != UN_ACTIVATE)
		return dlg; /* comme l'asm : x0 (le dialogue) inchangé */
	return ui_dialog_close(dlg, 1);
}

/* x0 = widget, x1 = date, x2 = aujourd'hui : le calendrier dans une fenêtre
 * modale sous ce widget (un clic choisit le jour ; double-clic, Entrée ou OK
 * valident) -> x0 = jour choisi (-1 : Annuler, Échap). Pour un champ de date à
 * soi (ui_datepicker ne sait ni être vide, ni se taper) */
long ui_date_popup(long a0, long a1, long a2)
{
	long date = a1, today = a2;
	long ht = LDX(uk_met(), MT_CTL) + UK_DP_H;
	long dlg, cal, b, r, v;

	dlg = uk_dialog_under(a0, UK_DP_W, ht, S(uk_s_pickdate));
	if (!dlg)
		return -1;
	cal = ui_calendar((UK_DP_W - 7 * 34) / 2, 8, 0, 0, date);
	STX(dlg, DG_LIST, cal);
	STX(cal, UW_AUX, today); /* (aujourd'hui) ; comme l'asm : sans test de cal */
	uk_dlg_bar(dlg);
	b = ui_dialog_button(dlg, 0, S(uk_s_ok), 1, UB_DEFAULT, UK_DP_W - 12);
	ui_dialog_button(dlg, 1, S(uk_s_cancel), 0, 0, b);
	ui_w_focus(cal);
	STX(dlg, DG_FN, (long)(uintptr_t)dp_fn);
	r = ui_dialog_run(dlg, 0);
	v = LDX(cal, UW_VALUE);
	ui_dialog_end(dlg);
	if (!r)
		return -1;
	return v;
}

/* x0 = sélecteur, x1 = aujourd'hui (entouré dans son calendrier) */
long ui_datepicker_today(long a0, long a1)
{
	if (a0)
		STX(a0, UW_AUX, a1);
	return a0;
}

/* uk_dd_hover : souris d'un widget à menu : survol (UWF_HOVER), sortie */
static long dd_hover(long w, long kind)
{
	long f = LDW(w, UW_FLAGS);
	long nf = f | UWF_HOVER;
	if (kind == 3)
		nf = f & ~(long)UWF_HOVER;
	if ((u32)f == (u32)nf)
		return 0;
	STW(w, UW_FLAGS, nf);
	uk_w_refresh(w);
	return 0;
}

/* souris : un appui ouvre le calendrier (survol : comme une liste) */
long uk_dp_mouse(long w, long kind, long x, long y, long clicks)
{
	(void)x;
	(void)y;
	(void)clicks;
	if (kind != 1)
		return dd_hover(w, kind);
	return dp_open(w);
}

/* clavier : Espace, Entrée (comme l'asm : pas d'Alt+bas, malgré son
 * commentaire) */
long uk_dp_key(long w, long code, long ch, long mods)
{
	(void)mods;
	if ((u32)ch == ' ')
		return dp_open(w);
	if ((u32)code == KEY_ENTER)
		return dp_open(w);
	return -1;
}

/* ---------------------------------------------------------------------------
 *  Palette, sélecteur de couleur
 * ------------------------------------------------------------------------- */

static const u32 palette_colors[UK_PAL_COLS * UK_PAL_ROWS] = { /* uk_palette_colors */
	0x000000, 0x404040, 0x808080, 0xA0A0A0, 0xC0C0C0, 0xE0E0E0, 0xF4F4F4, 0xFFFFFF,
	0x7F0000, 0xC00000, 0xFF0000, 0xFF6060, 0xFF9F40, 0xFFC000, 0xFFE060, 0xFFFF80,
	0x004000, 0x008000, 0x30A46C, 0x60C060, 0x90E090, 0x00A0A0, 0x40E0E0, 0xB0F0F0,
	0x000080, 0x0040C0, 0x2F74E0, 0x60A0FF, 0xA0C8FF, 0x6040C0, 0x9060E0, 0xC0A0FF,
	0x602000, 0x8A5A2B, 0xC08040, 0xE0B080, 0x800040, 0xC00080, 0xFF40A0, 0xFFB0D0,
};

/* x0..x3 = rectangle (0 : 8 x 22, 5 x 22), x4 = couleur -> palette */
long ui_palette(long a0, long a1, long a2, long a3, long a4)
{
	long w;
	if (!a2)
		a2 = UK_PAL_COLS * 22;
	if (!a3)
		a3 = UK_PAL_ROWS * 22;
	w = uk_w_new(UT_PALETTE, a0, a1, a2, a3);
	if (!w)
		return 0;
	STX(w, UW_VALUE, a4);
	STX(w, UW_HOT, -1);
	return w;
}

long uk_pal_draw(long w)
{
	long cw, ch, i, cx, cy, col, row, c, t;

	/* comme l'asm : ldp w0..w3 sans sxtw */
	ui_fill(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H), JET(TJ_SURFACE));
	cw = UDIV(LDW(w, UW_W), UK_PAL_COLS); /* case */
	ch = UDIV(LDW(w, UW_H), UK_PAL_ROWS);
	for (i = 0; ULT(i, UK_PAL_COLS * UK_PAL_ROWS); i++) {
		row = UDIV(i, UK_PAL_COLS);
		col = i - row * UK_PAL_COLS;
		cx = col * cw + SXTW(LDW(w, UW_X)); /* x */
		cy = row * ch + SXTW(LDW(w, UW_Y)); /* y */
		c = palette_colors[i];
		/* choisie : contour ACCENT de 2 px */
		if ((u32)LDX(w, UW_VALUE) == (u32)c) {
			t = JET(TJ_ACCENT);
			ui_rbox(cx, cy, cw, ch, 5, t, t);
		}
		/* survolée : contour TEXTE2 */
		if (LDX(w, UW_HOT) == i) {
			t = JET(TJ_TEXT2);
			ui_rbox(cx + 1, cy + 1, cw - 2, ch - 2, 4, t, t);
		}
		/* la pastille (contour LIGNE) */
		t = JET(TJ_LINE);
		ui_rbox(cx + 3, cy + 3, cw - 6, ch - 6, 3, t, t);
		ui_rbox(cx + 4, cy + 4, cw - 8, ch - 8, 2, c, c);
	}
	return 0;
}

/* souris : survol, clic (double : UN_ACTIVATE) */
long uk_pal_mouse(long w, long kind, long x, long y, long clicks)
{
	long hot = -1, cw, ch, col, row;
	if (kind == 3)
		goto hot;
	cw = UDIV(LDW(w, UW_W), UK_PAL_COLS);
	ch = UDIV(LDW(w, UW_H), UK_PAL_ROWS);
	col = UDIV(x, cw);
	row = UDIV(y, ch);
	if (UGE(col, UK_PAL_COLS))
		goto hot;
	if (UGE(row, UK_PAL_ROWS))
		goto hot;
	hot = row * UK_PAL_COLS + col;
	if (kind != 1)
		goto hot;
	STX(w, UW_VALUE, (long)palette_colors[hot]);
	uk_w_refresh(w);
	if (clicks == 2)
		return UN_ACTIVATE;
	return UN_CHANGE;
hot:
	if (LDX(w, UW_HOT) == hot)
		return 0;
	STX(w, UW_HOT, hot);
	uk_w_refresh(w);
	return 0;
}

/* x0..x3 = rectangle (0 : 44 x un contrôle), x4 = couleur -> sélecteur de
 * couleur (valeur : 0x00RRGGBB) */
long ui_colorpicker(long a0, long a1, long a2, long a3, long a4)
{
	long w;
	if (!a2)
		a2 = 44;
	w = uk_new_sized(UT_COLOR, a0, a1, a2, a3, 0, 0);
	if (w)
		STX(w, UW_VALUE, a4);
	return w;
}

long uk_cp_draw(long w)
{
	long c;
	/* comme l'asm : ldp w0..w3 sans sxtw */
	ui_fill(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H), JET(TJ_FACE));
	/* contour LIGNE_FORTE (survol : ACCENT) */
	c = JET(TJ_LINE_STRONG);
	if (LDW(w, UW_FLAGS) & 4)
		c = JET(TJ_ACCENT);
	ui_rbox(LDW(w, UW_X), LDW(w, UW_Y), LDW(w, UW_W), LDW(w, UW_H), 6, c, c);
	/* la couleur */
	c = LDX(w, UW_VALUE) & 0x00FFFFFF;
	return ui_rbox(LDW(w, UW_X) + 3, LDW(w, UW_Y) + 3, LDW(w, UW_W) - 6, LDW(w, UW_H) - 6, 4, c, c);
}

/* uk_cp_open : x0 = sélecteur : le dialogue de couleur -> UN_CHANGE si une
 * autre couleur est choisie */
static long cp_open(long w)
{
	long c = ui_color_dialog(S(uk_s_color), LDX(w, UW_VALUE));
	if (c < 0)
		return 0;
	if (c == LDX(w, UW_VALUE))
		return 0;
	STX(w, UW_VALUE, c);
	uk_w_refresh(w);
	return UN_CHANGE;
}

long uk_cp_mouse(long w, long kind, long x, long y, long clicks)
{
	(void)x;
	(void)y;
	(void)clicks;
	if (kind != 1)
		return dd_hover(w, kind);
	return cp_open(w);
}

long uk_cp_key(long w, long code, long ch, long mods)
{
	(void)mods;
	if ((u32)ch == ' ')
		return cp_open(w);
	if ((u32)code == KEY_ENTER)
		return cp_open(w);
	return -1;
}

/* uk_cd_preview : (dialogue de couleur) x0 = dialogue : l'aperçu de la couleur
 * courante et son code « #RRGGBB » */
static long cd_preview(long dlg)
{
	long c, p, v, sh, n;
	c = JET(TJ_LINE_STRONG);
	ui_rbox(UK_DLG_PAD + UK_PAL_COLS * 22 + 24, UK_DLG_PAD, 440 - UK_DLG_PAD - UK_PAL_COLS * 22 - 24 - UK_DLG_PAD,
		UK_PAL_ROWS * 22, 8, c, c);
	/* comme l'asm : x5 = la couleur entière (ldr x5), w6 = ses 32 bits bas */
	ui_rbox(UK_DLG_PAD + UK_PAL_COLS * 22 + 25, UK_DLG_PAD + 1,
		440 - UK_DLG_PAD - UK_PAL_COLS * 22 - 24 - UK_DLG_PAD - 2, UK_PAL_ROWS * 22 - 2, 7, LDX(dlg, DG_KIND),
		UXTW(LDX(dlg, DG_KIND)));
	/* « #RRGGBB » */
	p = UKA(numbuf);
	STB(p, 0, '#');
	p++;
	v = LDX(dlg, DG_KIND);
	sh = 20;
	do {
		n = LSR(v, sh) & 15;
		STB(p, 0, n >= 10 ? n + 'A' - 10 : n + '0');
		p++;
		sh -= 4;
	} while (sh >= 0);
	STB(p, 0, 0);
	ui_fill(UK_DLG_PAD + UK_PAL_COLS * 22 + 24, UK_DLG_PAD + UK_PAL_ROWS * 22 + 12, 140, 28, JET(TJ_FACE));
	uk_text_box(UKA(numbuf), UK_DLG_PAD + UK_PAL_COLS * 22 + 24, UK_DLG_PAD + UK_PAL_ROWS * 22 + 12, 28,
		    JET(TJ_TEXT1), uk_font_m(), 0, 15);
	return ui_update_all();
}

/* uk_cd_fn : (dialogue de couleur) x0 = dialogue, x1 = widget, x2 = code */
static long cd_fn(long dlg, long w, long code)
{
	long c, i;
	if (code == UN_CHANGE) {
		if (w == LDX(dlg, DG_LIST)) {
			/* la palette : les glissières suivent */
			c = LDX(w, UW_VALUE);
			STX(dlg, DG_KIND, c);
			for (i = 0; ULT(i, 3); i++)
				ui_w_set_value(LDX(dlg + DG_SL, LSL(i, 3)), LSR(c, LSL(2 - i, 3)) & 0xFF);
		} else {
			/* une glissière : la couleur */
			c = 0;
			for (i = 0; ULT(i, 3); i++)
				c = LSL(c, 8) | LDX(LDX(dlg + DG_SL, LSL(i, 3)), UW_VALUE);
			STX(dlg, DG_KIND, c);
		}
		return cd_preview(dlg);
	}
	if (code != UN_ACTIVATE)
		return dlg; /* comme l'asm : x0 (le dialogue) inchangé */
	/* double-clic dans la palette : OK */
	if (w != LDX(dlg, DG_LIST))
		return dlg;
	STX(dlg, DG_KIND, LDX(w, UW_VALUE));
	return ui_dialog_close(dlg, 1);
}

/* x0 = titre, x1 = couleur de départ -> x0 = couleur choisie (0x00RRGGBB ;
 * -1 : Annuler) : palette, rouge / vert / bleu, aperçu */
long ui_color_dialog(long a0, long a1)
{
	long color = a1;
	long dlg, i, sl, b, r, v;

	dlg = ui_dialog_begin(440, 316, a0);
	if (!dlg)
		return -1;
	STX(dlg, DG_LIST, ui_palette(UK_DLG_PAD, UK_DLG_PAD, 0, 0, color)); /* palette */
	/* trois glissières : rouge, vert, bleu (octet de la couleur : 2 - i) */
	for (i = 0; ULT(i, 3); i++) {
		sl = ui_slider(UK_DLG_PAD, 36 * i + UK_DLG_PAD + UK_PAL_ROWS * 22 + 12, UK_PAL_COLS * 22, 28, 0, 255,
			       LSR(color, LSL(2 - i, 3)) & 0xFF);
		STX(dlg + DG_SL, LSL(i, 3), sl); /* (DG_SL.. : trois glissières) */
	}
	uk_dlg_bar(dlg);
	STX(dlg, DG_KIND, color); /* couleur courante */
	cd_preview(dlg);
	b = ui_dialog_button(dlg, 0, S(uk_s_ok), 1, UB_DEFAULT, 440 - 16);
	ui_dialog_button(dlg, 1, S(uk_s_cancel), 0, 0, b);
	r = ui_dialog_run(dlg, (long)(uintptr_t)cd_fn);
	v = LDX(dlg, DG_KIND);
	ui_dialog_end(dlg);
	if (!r)
		return -1;
	return v;
}
