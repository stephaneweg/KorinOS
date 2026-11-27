/* uk_core.c -- UIKit : uikit.asm (initialisation, polices, fenêtre, dessin de
 * base, texte, événements, menus de l'application), ses variables et ses
 * chaînes. Port ligne à ligne de l'assembleur (src/kits/uikit/PORT.md). */
#include "uk.h"

/* les variables du kit (segment inscriptible d'uikit.asm) */
struct uk_state uk = {
	.scale = -1,
	.winact = 1,
	.minw = 120,
	.minh = 80,
	.cur_shape = -1,
};

/* ---- chaînes (uikit.asm) ------------------------------------------------- */
/* les polices du système : /res/fonts sur le PC (27/11/2026, sans repli), /fonts sur le Pi */
#if defined(__x86_64__)
#define UK_FONTS "/res/fonts/"
#else
#define UK_FONTS "/fonts/"
#endif
const char ui_path_sans[] = UK_FONTS "Inter-Regular.ttf";
const char ui_path_bold[] = UK_FONTS "Inter-SemiBold.ttf";
const char ui_path_sans2[] = UK_FONTS "DejaVuSans.ttf";
const char ui_path_bold2[] = UK_FONTS "DejaVuSans-Bold.ttf";
const char ui_path_mono[] = UK_FONTS "DejaVuSansMono.ttf";
const char uk_s_ellipsis[] = "\xE2\x80\xA6";
const char uk_s_up[] = "\xE2\x96\xB2";
const char uk_s_down[] = "\xE2\x96\xBC";
const char uk_empty[] = "";
const char uk_s_cross[] = "\xC3\x97";
const char uk_s_space[] = " ";
const char uk_s_minus[] = "\xE2\x88\x92";
const char uk_s_plus[] = "+";
const char uk_s_tab[] = "    ";
const char uk_s_check[] = "\xE2\x9C\x93";
const char uk_s_chev[] = "\xE2\x96\xBE";
const char uk_s_ok[] = "OK";
const char uk_s_cancel[] = "Annuler";
const char uk_s_yes[] = "Oui";
const char uk_s_no[] = "Non";
const char uk_s_open[] = "Ouvrir un fichier";
const char uk_s_save[] = "Enregistrer";
const char uk_s_folder[] = "Choisir un dossier";
const char uk_s_sd0[] = "/";
const char uk_s_upar[] = "\xE2\x86\x91";
const char uk_s_left[] = "\xE2\x86\x90";
const char uk_s_right[] = "\xE2\x86\x92";
const char uk_s_date[] = "Date";
const char uk_s_pickdate[] = "Choisir une date";
const char uk_s_color[] = "Choisir une couleur";
const char uk_s_places[] = "Emplacements";
const char uk_s_volumes[] = "Volumes";
const char uk_s_homel[] = "Accueil";
const char uk_s_home[] = "HOME:";
const char uk_s_sd0c[] = "Système";
const char uk_s_placesf[] = "/etc/places.ini";
const char uk_s_vfs[] = "vfs";
const char uk_s_allfiles[] = "Tous les fichiers";
const char uk_s_items[] = "\xC3\xA9l\xC3\xA9ments";
const char uk_s_name[] = "Nom";
const char uk_s_size[] = "Taille";
const char uk_s_namec[] = "Nom :";
const char uk_s_bopen[] = "Ouvrir";
const char uk_s_bsave[] = "Enregistrer";
const char uk_s_bchoose[] = "Choisir";
const char uk_s_ko[] = " Ko";
const char uk_s_mo[] = " Mo";
const char uk_s_go[] = " Go";
const char uk_s_oct[] = " o";
const char uk_s_dir[] = "Dossier";
const char uk_s_replace[] = "Ce fichier existe d\xC3\xA9j\xC3\xA0. Le remplacer ?";

/* ======================================================================== */

/* uk_kits : AppKit ouvert au premier besoin. En C, les relais d'import
 * (abi2import.py) font LIB_OPEN au premier appel d'une fonction d'AppKit :
 * rien à faire ici. */
void uk_kits(void)
{
}

/* uk_font_repli : (F_FALLBACK) x0 = police -> DejaVu Sans (uk.fallback), 0 si
 * elle ne se charge pas (un seul essai) */
static long uk_font_fallback(long font)
{
	long st = uk.fallback_state;
	(void)font;
	if (st == 0) {
		uk.fallback_state = -1;
		if (!ui_load_font(S(ui_path_sans2), UKA(fallback)))
			return 0;
		uk.fallback_state = 1;
		st = 1;
	}
	if (st != 1)
		return 0;
	return UKA(fallback);
}

/* uk_font_ui : chemin, chemin de secours, décalage de la police dans les
 * variables du programme -> 1 si chargée ; la police de l'interface prend
 * DejaVu Sans pour ses glyphes absents */
static long uk_font_ui(long path, long path2, long off)
{
	long font = uk.vars + off;
	long r = ui_load_font(path, font);
	if (r) {
		STX(font, F_FALLBACK, (long)(uintptr_t)uk_font_fallback);
		return r;
	}
	return ui_load_font(path2, font);
}

/* ui_init : x0 = variables du programme -> 1 si les polices sont chargées */
long ui_init(long vars)
{
	long r;
	uk.vars = vars;
	r = uk_font_ui(S(ui_path_sans), S(ui_path_sans2), UV_FONT);
	uk_font_ui(S(ui_path_bold), S(ui_path_bold2), UV_FONT_B);
	ui_load_font(S(ui_path_mono), uk.vars + UV_FONT_M);
	uk_theme_load();
	uk_theme_watch();
	return r;
}

/* uk_conf_path : nom d'un fichier de réglages (« theme.txt »), write -> son chemin (tampon commun) :
 * à lire, HOME:/etc/<nom> s'il existe, sinon HOME:/.config/korin/<nom> (la place du 15/11), sinon
 * /etc/<nom> ; à écrire, toujours HOME:/etc/<nom> (dossier créé). Comme armpi/userconf.h de libposix
 * (docs/etude/MULTI-UTILISATEUR.md §2.5, docs/DESIGN-CONNEXION.md §2.6) */
static char uk_confbuf[160];

static void uk_conf_join(const char *d, const char *name)
{
	long n = 0;
	while (*d)
		uk_confbuf[n++] = *d++;
	for (const char *s = name; *s && n < 158; s++)
		uk_confbuf[n++] = *s;
	uk_confbuf[n] = 0;
}

long uk_conf_path(const char *name, long write)
{
	long st[2];
	uk_conf_join("HOME:/etc/", name);
	if (write) {
		SYS1(SYS_MKDIR, S("HOME:/etc"));
		return S(uk_confbuf);
	}
	if (SYS2(SYS_STAT, S(uk_confbuf), (long)(uintptr_t)st) != 0) {
		uk_conf_join("HOME:/.config/korin/", name);
		if (SYS2(SYS_STAT, S(uk_confbuf), (long)(uintptr_t)st) != 0)
			uk_conf_join("/etc/", name);
	}
	return S(uk_confbuf);
}

/* ui_elevate : x0 = raison (« Logiciels veut installer Tetris ») -> 0 : ce programme est passé en uid 0,
 * après la carte de confirmation d'authd (/sys/authcard : le mot de passe de l'utilisateur, ou Autoriser ;
 * docs/etude/MULTI-UTILISATEUR.md §2.6) ; E_CANCELED (-125) : refusé ; E_PERM : le compte n'est pas dans
 * /etc/elevate.ini ; E_BUSY : une autre carte est ouverte ; E_NOSYS : pas d'authd. Déjà en uid 0, ou un noyau
 * sans uid (le Pi) : 0.
 * Le programme revient à son uid par SYS_SETUID quand il a fini. */
long ui_elevate(long reason)
{
	static u64 m[64];
	if (SYS0(SYS_GETUID) <= 0)
		return 0;
	long ch = SYS2(SYS_SVC_OPEN, S("auth"), 3000);
	if (ch < 0)
		return -38; /* E_NOSYS : un compte sans authd ne peut pas s'élever ; le dire, plutôt que de laisser
			       l'appelant écrire en croyant être en uid 0 (E72) */
	long op = 5; /* AUTH_ELEVATE, puis AUTH_WAIT (7) tant qu'authd rend E_AGAIN (-11) */
	for (;;) {
		for (int i = 0; i < 64; i++)
			m[i] = 0;
		m[0] = (u64)op;
		long n = 0;
		const char *r = (const char *)(uintptr_t)reason;
		char *d = (char *)&m[6];
		while (r && r[n] && n < 400) {
			d[n] = r[n];
			n++;
		}
		((u32 *)m)[10] = (u32)(n + 1);
		long e = SYS2(SYS_SVC_CALL, ch, S(m));
		long res = e < 0 ? e : (long)m[0];
		if (res != -11)
			return res;
		op = 7;
	}
}

/* ui_elevate_system : x0 = raison, x1 = 1 : poser la question de la version de secours -> comme ui_elevate,
 * pour une mise à jour du système (A/B, docs/etude/MISES-A-JOUR-AB.md) : la carte d'authd s'ouvre toujours (le
 * « retenir 5 minutes » ne s'y applique pas) ; avec x1 = 1, elle porte la case « Garder la version actuelle comme
 * version de secours ». -> 1 : passé en uid 0, case cochée ; 0 : passé en uid 0 (ou déjà, ou le Pi) sans ;
 * < 0 : comme ui_elevate */
long ui_elevate_system(long reason, long ask)
{
	static u64 m[64];
	if (SYS0(SYS_GETUID) <= 0)
		return 0;
	long ch = SYS2(SYS_SVC_OPEN, S("auth"), 3000);
	if (ch < 0)
		return -38; /* (E_NOSYS : comme ui_elevate) */
	long op = 5;
	for (;;) {
		for (int i = 0; i < 64; i++)
			m[i] = 0;
		m[0] = (u64)op;
		m[2] = ask ? 3 : 2; /* (b : le mode mise à jour du système) */
		long n = 0;
		const char *r = (const char *)(uintptr_t)reason;
		char *d = (char *)&m[6];
		while (r && r[n] && n < 400) {
			d[n] = r[n];
			n++;
		}
		((u32 *)m)[10] = (u32)(n + 1);
		long e = SYS2(SYS_SVC_CALL, ch, S(m));
		long res = e < 0 ? e : (long)m[0];
		if (res != -11)
			return res == 0 ? (m[1] == 1 ? 1 : 0) : res;
		op = 7;
	}
}

/* ui_load_font : x0 = chemin, x1 = police -> 1 si chargée */
long ui_load_font(long path, long font)
{
	long st[2];
	long size, data, fd, n, scratch;
	uk_kits();
	if (SYS2(SYS_STAT, path, (long)(uintptr_t)st) != 0)
		return 0;
	size = st[0];
	data = uk_malloc(size);
	if (!data)
		return 0;
	fd = SYS2(SYS_OPEN, path, O_READ);
	if (fd < 0)
		return 0;
	n = SYS3(SYS_READ, fd, data, size);
	SYS1(SYS_CLOSE, fd);
	if (n != size)
		return 0;
	scratch = uk_malloc(UI_SCRATCH + UI_CACHE);
	if (!scratch)
		return 0;
	if (!ttf_open(font, data, size, scratch, UI_SCRATCH))
		return 0;
	ttf_set_cache(font, scratch + UI_SCRATCH, UI_CACHE);
	return 1;
}

/* uk_call : x0 = opération (arguments déjà dans uk.msg) -> réponse */
long uk_call(long op)
{
	STX(uk.msg, WM_OP, op);
	return SYS1(SYS_WS_CALL, UKA(msg));
}

/* uk_title_copy : copie bornée (120 octets) */
long uk_title_copy(long dst, long src)
{
	long i = 0;
	for (;;) {
		u8 c;
		if (UGE(i, 120)) {
			STB(dst, i, 0);
			return dst;
		}
		c = (u8)LDB(src, i);
		STB(dst, i, c);
		if (!c)
			return dst;
		i++;
	}
}

/* uk_win : poignée de la fenêtre courante */
long uk_win(void)
{
	return LDX(uk.vars, UV_WIN);
}

/* ui_close : ferme la fenêtre courante */
long ui_close(void)
{
	STX(uk.msg, WM_A, uk_win());
	uk_call(WS_OP_CLOSE);
	SYS1(SYS_BUF_FREE, uk.buf);
	return uk_wb_drop();
}

/* ui_screen : x0 = tampon <- largeur, hauteur, réserves en bas, en haut */
long ui_screen(long buf)
{
	long r = uk_call(WS_OP_SCREEN);
	if (r < 0)
		return r;
	STX(buf, 0, LDX(uk.msg, WM_A));
	STX(buf, 8, LDX(uk.msg, WM_B));
	STX(buf, 16, LDX(uk.msg, WM_C));
	STX(buf, 24, LDX(uk.msg, WM_D));
	return r;
}

/* ui_fill : rectangle plein (découpé à la surface et à ui_clip) ; bornes en
 * 64 bits signés, comme l'asm */
long ui_fill(long x, long y, long w, long h, long color)
{
	long bx = LDX(uk.vars, UV_W), by = LDX(uk.vars, UV_H), cx = 0, cy = 0;
	long x2, y2, pitch, row;
	u32 c = (u32)color;
	if (uk.clip_on) {
		if (uk.clip[2] < bx)
			bx = uk.clip[2];
		if (uk.clip[3] < by)
			by = uk.clip[3];
		cx = uk.clip[0];
		cy = uk.clip[1];
	}
	x2 = x + w;
	y2 = y + h;
	if (x < cx)
		x = cx;
	if (y < cy)
		y = cy;
	if (x2 > bx)
		x2 = bx;
	if (y2 > by)
		y2 = by;
	w = x2 - x;
	if (w <= 0)
		return x;
	h = y2 - y;
	if (h <= 0)
		return x;
	pitch = LDX(uk.vars, UV_W) * 4;
	row = LDX(uk.vars, UV_SURF) + y * pitch + x * 4;
	do {
		uk_u32u *p = (uk_u32u *)(uintptr_t)row;
		long n = w;
		while (n--)
			*p++ = c;
		row += pitch;
	} while (--h);
	return x;
}

/* ui_frame : contour de 1 px */
long ui_frame(long x, long y, long w, long h, long color)
{
	ui_fill(x, y, w, 1, color);
	ui_fill(x, y + h - 1, w, 1, color);
	ui_fill(x, y, 1, h, color);
	return ui_fill(x + w - 1, y, 1, h, color);
}

/* ui_pixel : un pixel (surface : bornes non signées ; ui_clip : signées) */
long ui_pixel(long x, long y, long color)
{
	long w;
	if (uk.clip_on) {
		if (x < uk.clip[0] || y < uk.clip[1])
			return x;
		if (x >= uk.clip[2] || y >= uk.clip[3])
			return x;
	}
	w = LDX(uk.vars, UV_W);
	if (UGE(x, w) || UGE(y, LDX(uk.vars, UV_H)))
		return x;
	STW(LDX(uk.vars, UV_SURF) + y * (w << 2), x * 4, color);
	return x;
}

/* ui_text : x0 = UTF-8, x1 = x, x2 = ligne de base, w3 = couleur, x4 = px,
 * x5 = police, x6 = largeur max -> x final */
long ui_text(long s, long x, long base, long color, long px, long font, long max)
{
	long x7, x8, x25 = 0, x26 = 0, pitch, surf;
	ttf_set_size(font, px);
	x7 = LDX(uk.vars, UV_W);
	x8 = LDX(uk.vars, UV_H);
	if (uk.clip_on) {
		x25 = uk.clip[0];
		x26 = uk.clip[1];
		if (uk.clip[2] < x7)
			x7 = uk.clip[2];
		if (uk.clip[3] < x8)
			x8 = uk.clip[3];
	}
	if (max && x >= 0) {
		if (x > x25)
			x25 = x;
		if (x + max < x7)
			x7 = x + max;
	}
	if (x7 <= x25 || x8 <= x26)
		return x;
	pitch = LDX(uk.vars, UV_W) << 2;
	surf = LDX(uk.vars, UV_SURF) + x26 * pitch + (x25 << 2);
	x7 -= x25;
	x8 -= x26;
	return ttf_draw_text(font, s, surf, pitch, x - x25, base - x26, (u32)color, x7 | LSL(x8, 32)) + x25;
}

/* ui_text_width : largeur en pixels */
long ui_text_width(long s, long px, long font)
{
	ttf_set_size(font, px);
	return ttf_text_width(font, s);
}

/* ui_button : bouton plat, libellé centré */
long ui_button(long x, long y, long w, long h, long label, long accent)
{
	long c, tw;
	ui_fill(x, y, w, h, JET(TJ_FACE));
	c = accent ? JET(TJ_ACCENT) : JET(TJ_LINE_STRONG);
	ui_rbox(x, y, w, h, 8, c, c);
	if (!accent)
		ui_rbox(x + 1, y + 1, w - 2, h - 2, 7, JET(TJ_RAISED), JET(TJ_RAISED));
	tw = ui_text_width(label, UK_PX, uk.vars + UV_FONT);
	return ui_text(label, ASR(w - tw, 1) + x, LSR(h, 1) + y + 5, accent ? JET(TJ_ACCENT_INK) : JET(TJ_TEXT1),
		       UK_PX, uk.vars + UV_FONT, 0);
}

/* uk_update_call : (ui_update) par requête, fenêtre sans en-tête */
static long uk_update_call(long x, long y, long w, long h)
{
	STX(uk.msg, WM_B, x);
	STX(uk.msg, WM_C, y);
	STX(uk.msg, WM_D, w);
	STX(uk.msg, WM_DATA, h);
	STX(uk.msg, WM_A, LDX(uk.vars, UV_WIN));
	return uk_call(WS_OP_UPDATE);
}

/* ui_update : zone modifiée, réunie à celle en attente dans l'en-tête WD_*
 * (échange atomique) ; WS_SIG_DAMAGE si elle passe de 0 à une zone */
long ui_update(long x, long y, long w, long h)
{
	u64 *d = (u64 *)(uintptr_t)uk.dmg;
	long x10, x11, x12, x13, bw, bh;
	u64 old, nw;
	if (!d)
		return uk_update_call(x, y, w, h);
	bw = LDX(uk.vars, UV_W);
	bh = LDX(uk.vars, UV_H);
	x12 = x + w;
	x13 = y + h;
	x10 = x < 0 ? 0 : x;
	x11 = y < 0 ? 0 : y;
	if (x12 > bw)
		x12 = bw;
	if (x13 > bh)
		x13 = bh;
	if (x12 <= x10 || x13 <= x11)
		return 0;
	old = __atomic_load_n(d, __ATOMIC_ACQUIRE);
	do {
		if (old) {
			u64 v;
			v = old & 0xFFFF;
			if (v < (u64)x10)
				x10 = (long)v;
			v = (old >> 16) & 0xFFFF;
			if (v < (u64)x11)
				x11 = (long)v;
			v = (old >> 32) & 0xFFFF;
			if (v > (u64)x12)
				x12 = (long)v;
			v = old >> 48;
			if (v > (u64)x13)
				x13 = (long)v;
		}
		nw = (u64)x10 | ((u64)x11 << 16) | ((u64)x12 << 32) | ((u64)x13 << 48);
	} while (!__atomic_compare_exchange_n(d, &old, nw, 1, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE));
	if (!old) /* déjà signalée : Elegant viendra */
		SYS1(SYS_WS_SIGNAL, WS_SIG_DAMAGE);
	return 0;
}

/* ui_frame_request : -> 1 : un WEV_FRAME suivra */
long ui_frame_request(void)
{
	if (!uk.dmg)
		return 0;
	STX(uk.dmg, WD_FRAME, 1);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	SYS1(SYS_WS_SIGNAL, WS_SIG_DAMAGE);
	return 1;
}

long ui_update_all(void)
{
	return ui_update(0, 0, LDX(uk.vars, UV_W), LDX(uk.vars, UV_H));
}

/* ui_event : x0 = attente (ms) -> type (0 : aucun), événement dans ui_ev.
 * Un appui du bouton gauche dans le haut d'une fenêtre à cadre sans widgets
 * attend : s'il bouge, la fenêtre se déplace ; sinon le programme reçoit
 * l'appui puis le relâchement. korin.theme : thème relu, WEV_THEME. */
long ui_event(long wait)
{
	long ev = uk.vars + UV_EV, held = UKA(ev_held), r, win, w;
	if (LDW(uk.ev_next, 0)) {
		r = LDW(uk.ev_next, 0);
		memcpy((void *)(uintptr_t)ev, uk.ev_next, 32);
		STW(uk.ev_next, 0, 0);
		return r;
	}
	r = SYS2(SYS_EV_WAIT, ev, wait);
	if (r == WEV_BUS) {
		if (LDW(ev, BEV_SUB) != uk.theme_sub)
			return r;
		uk_retheme();
		STW(ev, 0, WEV_THEME);
		return WEV_THEME;
	}
	if (r == WEV_SCREEN) {
		uk.scale = -1;
		uk.mt_ok = 0;
	}
	if (r != WEV_MOUSE)
		return r;
	if (LDW(held, 0)) {
		long dx, dy;
		if (LDW(ev, EV_KIND)) {
			/* le clic : l'appui, puis cet événement */
			memcpy(uk.ev_next, (void *)(uintptr_t)ev, 32);
			memcpy((void *)(uintptr_t)ev, uk.ev_held, 32);
			STW(held, 0, 0);
			return WEV_MOUSE;
		}
		dx = LDSW(ev, EV_X) - LDSW(held, EV_X);
		if (dx < 0)
			dx = -dx;
		dy = LDSW(ev, EV_Y) - LDSW(held, EV_Y);
		if (dy < 0)
			dy = -dy;
		if (UGE(dx, UK_INTENT_PX) || UGE(dy, UK_INTENT_PX)) {
			STW(held, 0, 0);
			ui_window_drag();
		}
		return 0;
	}
	if (LDW(ev, EV_KIND) != 1)
		return r;
	if (UGE(LDW(ev, EV_Y), UK_INTENT_H))
		return r;
	if (LDW(ev, EV_CODE) & 2) /* (bouton droit : l'application) */
		return r;
	win = uk_win();
	if (win != LDW(ev, EV_WIN))
		return WEV_MOUSE;
	if (uk.flags & 1) /* WF_FRAMELESS */
		return WEV_MOUSE;
	for (w = uk.wfirst; w; w = LDX(w, UW_NEXT))
		if (LDX(w, UW_WIN) == win)
			return WEV_MOUSE;
	memcpy(uk.ev_held, (void *)(uintptr_t)ev, 32);
	return 0;
}

/* ui_hit : 1 si la souris (ui_ev) est dans le rectangle */
long ui_hit(long x, long y, long w, long h)
{
	long ev = uk.vars + UV_EV, ex = LDSW(ev, EV_X), ey = LDSW(ev, EV_Y);
	if (ex < x || ex >= x + w || ey < y || ey >= y + h)
		return 0;
	return 1;
}

/* uk_menu_set : texte des menus (0 : aucun) envoyé en morceaux -> 0 / erreur */
long uk_menu_set(long text)
{
	long len = 0, off = 0;
	if (text)
		while (LDB(text, len))
			len++;
	if (UGT(len, WS_MENU_MAX))
		return -22;
	do {
		long piece = len - off, r, i;
		if (UGT(piece, WS_MENU_PIECE))
			piece = WS_MENU_PIECE;
		STX(uk.msg, WM_A, uk_win());
		STX(uk.msg, WM_B, off);
		STX(uk.msg, WM_C, len);
		STW(uk.msg, WM_LEN, piece);
		for (i = 0; ULT(i, piece); i++)
			uk.msg[WM_DATA + i] = (u8)LDB(text + off, i);
		r = uk_call(WS_OP_MENU_SET);
		if (r < 0)
			return r;
		off += piece;
	} while (ULT(off, len));
	return 0;
}

/* ui_menu_get : (barre de menus) x0 = tampon du texte, x1 = tampon du titre,
 * x2 = série connue -> r0 série (0 : aucun), r1 id de la fenêtre, r2 longueur */
ukr ui_menu_get(long buf, long tbuf, long known)
{
	ukr ret = {0, 0, 0, 0};
	long off, serial, r, total, n, i;
again:
	off = 0;
	serial = 0;
	for (;;) {
		STX(uk.msg, WM_A, off);
		r = uk_call(WS_OP_MENU_GET);
		if (r <= 0)
			return ret;
		if (serial) {
			if (r != serial)
				goto again;
		} else {
			serial = r;
			if (r == known) {
				ret.r2 = LDX(uk.msg, WM_C);
				goto id;
			}
			for (i = 0;;) {
				u8 c = uk.msg[WM_DATA + i];
				STB(tbuf, i, c);
				if (!c)
					break;
				i++;
				if (!ULT(i, WS_MENU_TITLE - 1)) {
					STB(tbuf, i, 0);
					break;
				}
			}
		}
		total = LDX(uk.msg, WM_C);
		n = LDX(uk.msg, WM_D);
		if (UGT(total, WS_MENU_MAX))
			total = WS_MENU_MAX;
		if (!ULE(off + n, total))
			n = total - off;
		for (i = 0; ULT(i, n); i++)
			STB(buf, off++, uk.msg[WM_DATA + WS_MENU_TITLE + i]);
		if (!n || !ULT(off, total))
			break;
	}
	STB(buf, off, 0);
	ret.r2 = off;
id:
	ret.r1 = LDX(uk.msg, WM_B);
	ret.r0 = serial;
	return ret;
}

/* ui_menu_command : x0 = entrée (-1 : Quitter), x1 = série -> 0 / E_AGAIN */
long ui_menu_command(long item, long serial)
{
	STX(uk.msg, WM_A, item);
	STX(uk.msg, WM_B, serial);
	return uk_call(WS_OP_MENU_COMMAND);
}

/* uk_malloc : bloc du tas du programme (sbrk), 0 si échec */
long uk_malloc(long size)
{
	long r = SYS1(SYS_SBRK, (size + 15) & ~15L);
	return r < 0 ? 0 : r;
}
