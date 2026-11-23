/* login : l'écran de connexion (docs/DESIGN-CONNEXION.md ; étape 6 de docs/etude/MULTI-UTILISATEUR.md). Lancé par
 * Elegant (uid 0) avant la session ; il écrit le compte ouvert dans /tmp/session.ini (« uid = N ») puis se termine :
 * Elegant lance alors la session sous ce compte.
 *
 *   - rien à demander, rien à montrer : « autologin = nom » (/etc/system.ini), ou un seul compte sans mot de passe ;
 *   - depuis le 23/11/2026, comme celui de Fedora (GNOME) : tout l'écran, sans cadre, le fond sombre de la vue
 *     d'ensemble ; la liste des comptes (disque d'initiale de 48 coloré par l'uid, nom) ; un clic, ou Entrée, montre
 *     le compte en grand (disque de 96, nom, champ du mot de passe : l'œil tenu le montre, la flèche = Entrée ; un
 *     compte sans mot de passe : « Ouvrir la session ») et, à gauche du champ, un bouton rond qui revient à la liste
 *     (Échap aussi, champ vide) ; un seul compte : directement en grand ; en haut au milieu la date et l'heure, à
 *     droite la zone d'état (réseau, son, alimentation) qui ouvre les réglages rapides sans Réglages (/sys/quickmenu
 *     « L… ») ; en bas au milieu « Korin OS » ;
 *   - mauvais mot de passe : « Mot de passe incorrect. Réessayez. » en rouge, le champ vidé (authd attend aussi) ;
 *     pas de blocage du compte.
 * authd vérifie le mot de passe (AUTH_VERIFY) : login ne lit jamais /etc/shadow.ini. */
#include "sys_consts.h"
#include "uikit_import.h"

#define AUTH_VERIFY 9
#define AUTH_HASPW 10
#define AUTH_FORGET 11 /* (une session s'ouvre : les élévations retenues 5 min sont oubliées) */
#define ACC_MAX 6
#define M64(m, off) (*(u64 *)((u8 *)(m) + (off)))
#define M32(m, off) (*(u32 *)((u8 *)(m) + (off)))
#define TR(s) ((const char *)ui_tr((long)(s)))
#define HID_ENTER 0x28
#define HID_ESC 0x29
#define HID_BACK 0x2A
#define HID_DOWN 0x51
#define HID_UP 0x52
#define HID_KPENTER 0x58
#define EVF(off) ((long)*(i32 *)((u8 *)ui.ev + (off))) /* un champ de l'evenement (comme le panel) */

static struct krt_ui ui;
static u8 msg[MSG_SIZE];
static long auth;

static struct acc {
	char name[32], full[64];
	long uid;
	int admin, haspw;
} accs[ACC_MAX];
static int nacc, sel;
static char autologin[32], machine[48] = "Korin OS", keyb[8] = "FR";
static char pw[128];
static int pwlen, show_pw, err, busy;
static long scr[4]; /* l'écran */
static char buf[8192];

static u32 tok(int off)
{
	return *(const u32 *)((const u8 *)ui_tokens() + off);
}

/* ---------------------------------------------------------------- fichiers */

static int sp(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static char *ligne(char **p)
{
	char *s = *p;
	if (!*s)
		return 0;
	char *e = s;
	while (*e && *e != '\n')
		e++;
	*p = *e ? e + 1 : e;
	*e = 0;
	while (sp(*s))
		s++;
	while (e > s && sp(e[-1]))
		*--e = 0;
	return s;
}

static char *cle(char *l, char **val)
{
	char *eq = l;
	while (*eq && *eq != '=')
		eq++;
	if (!*eq)
		return 0;
	char *v = eq + 1, *k = eq;
	*eq = 0;
	while (k > l && sp(k[-1]))
		*--k = 0;
	while (sp(*v))
		v++;
	*val = v;
	return l;
}

static char *lire(const char *path)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, buf, sizeof buf - 1);
	sys_close(fd);
	if (n < 0)
		return 0;
	buf[n] = 0;
	return buf;
}

static long auth_call(u64 op, const char *a, const char *b)
{
	if (auth < 0)
		return E_PERM;
	memset(msg, 0, MSG_SIZE);
	M64(msg, 0) = op;
	char *d = (char *)msg + MSG_DATA;
	u64 na = (u64)str_len(a), nb = b ? (u64)str_len(b) : 0;
	if (na + nb + 2 > 400)
		return E_INVAL;
	memcpy(d, a, na + 1);
	if (b)
		memcpy(d + na + 1, b, nb + 1);
	M32(msg, 40) = (u32)(na + nb + 2);
	long r = korin_sys2(SYS_SVC_CALL, auth, msg);
	r = r < 0 ? r : (long)M64(msg, 0);
	memset(msg, 0, MSG_SIZE);
	return r;
}

static void charger(void)
{
	char *p = lire("/etc/users.ini"), *l;
	struct acc cur;
	memset(&cur, 0, sizeof cur);
	cur.uid = -1;
	int in = 0;
	while (p) {
		l = ligne(&p);
		if (!l || *l == '[') {
			if (in && cur.uid >= 1000 && nacc < ACC_MAX)
				accs[nacc++] = cur;
			if (!l)
				break;
			memset(&cur, 0, sizeof cur);
			cur.uid = -1;
			char *e = l + 1;
			while (*e && *e != ']')
				e++;
			*e = 0;
			str_copy_n(cur.name, l + 1, sizeof cur.name);
			in = 1;
			continue;
		}
		char *v, *k = cle(l, &v);
		if (!k || !in)
			continue;
		if (str_eq(k, "uid"))
			cur.uid = (long)parse_dec(v);
		else if (str_eq(k, "name"))
			str_copy_n(cur.full, v, sizeof cur.full);
		else if (str_eq(k, "groups")) {
			for (const char *g = v; *g; g++)
				if (!memcmp(g, "admin", 5) && (g == v || g[-1] == ' ' || g[-1] == ',') &&
				    (!g[5] || g[5] == ' ' || g[5] == ','))
					cur.admin = 1;
		}
	}
	p = lire("/etc/system.ini");
	while (p && (l = ligne(&p))) {
		char *v, *k = cle(l, &v);
		if (k && str_eq(k, "autologin"))
			str_copy_n(autologin, v, sizeof autologin);
		else if (k && (str_eq(k, "hostname") || str_eq(k, "name")) && *v)
			str_copy_n(machine, v, sizeof machine);
	}
	p = lire("/etc/options.txt");
	while (p && (l = ligne(&p))) {
		char *v, *k = cle(l, &v);
		if (k && str_eq(k, "keyb") && *v)
			str_copy_n(keyb, v, sizeof keyb);
	}
}

/* la session est ouverte pour ce compte : /tmp/session.ini, puis fin */
static int ouvrir(const struct acc *a)
{
	char line[96] = "uid = ";
	char *o = line + 6;
	char t[24];
	int k = 0;
	u64 v = (u64)a->uid;
	do
		t[k++] = (char)('0' + v % 10);
	while ((v /= 10));
	while (k)
		*o++ = t[--k];
	o = str_cat(str_cat(str_cat(o, "\nname = "), a->name), "\n");
	long fd = sys_open("/tmp/session.ini", O_WRITE | O_CREATE | O_TRUNC);
	if (fd >= 0) {
		sys_write(fd, line, (u64)(o - line));
		sys_close(fd);
	}
	sys_unlink("/tmp/login.manual"); /* (la prochaine fois, la connexion automatique de nouveau) */
	sys_unlink("/tmp/login.halt");   /* (un arrêt demandé qui n'a pas eu lieu : la session s'ouvre quand même) */
	return 0;
}

/* ---------------------------------------------------------------- dessin */

static const char *const jours[] = {"dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam."};
static const char *const mois[] = {"janv.", "févr.", "mars", "avr.", "mai", "juin",
				   "juil.", "août", "sept.", "oct.", "nov.", "déc."};

static void two(char *o, u64 v)
{
	o[0] = (char)('0' + v / 10 % 10);
	o[1] = (char)('0' + v % 10);
	o[2] = 0;
}

static void heure(char *h, char *d)
{
	struct civil c;
	civil_from_secs((u64)sys_clock(2), &c);
	two(h, c.hour);
	h[2] = ':';
	two(h + 3, c.min);
	char n[8];
	u64 day = c.day;
	int k = 0;
	char t[4];
	do
		t[k++] = (char)('0' + day % 10);
	while ((day /= 10));
	int j = 0;
	while (k)
		n[j++] = t[--k];
	n[j] = 0;
	str_cat(str_cat(str_cat(str_cat(str_cat(d, TR(jours[c.wday % 7])), " "), n), " "), TR(mois[(c.month + 11) % 12]));
}

static u32 avatar_col(long uid)
{
	static const int toks[] = {TJ_ACCENT, TJ_SUCCESS, TJ_WARNING, TJ_ACCENT_DARK, TJ_DANGER, TJ_ACCENT_VIVID};
	return tok(toks[uid % 6]);
}

static void avatar(const struct acc *a, long x, long y, long d)
{
	u32 c = avatar_col(a->uid);
	ui_rbox(x, y, d, d, d / 2, c, c);
	char ini[2] = {(char)(a->full[0] ? a->full[0] : a->name[0]), 0};
	if (ini[0] >= 'a' && ini[0] <= 'z')
		ini[0] = (char)(ini[0] - 32);
	long px = d * 7 / 16;
	long tw = ui_text_width((long)ini, px, (long)ui.font_b);
	ui_text((long)ini, x + (d - tw) / 2, y + d / 2 + px * 3 / 8, 0x00FFFFFF, px, (long)ui.font_b, 0);
}

/* ---- l'écran (23/11/2026, demande de l'utilisateur : « comme le login de Fedora ») : plein écran, sans cadre, le
 * fond gris sombre de la vue d'ensemble ; la liste des comptes au milieu, un clic sur l'un l'agrandit (disque de 96, le
 * nom, le champ du mot de passe, à gauche un bouton rond pour revenir à la liste) ; en haut au milieu la date et
 * l'heure, à droite la zone d'état (réseau, son, alimentation) qui ouvre les réglages rapides (/sys/quickmenu « L… » :
 * sans Réglages, on n'est pas encore authentifié) ; en bas au milieu le nom du système. */
#define BG 0x002A2B2D /* (le fond de la vue d'ensemble : le fond d'écran flouté et assombri, ≈ uni) */
#define INK 0x00FFFFFF
#define DIM 0x00B9BBBF
#define BOX 0x003B3D40 /* (survol, champ, bouton retour : la teinte du dash) */
#define BOX_HI 0x00484A4E
#define ERR 0x00FF7B72
#define TOP_H 34
#define ROW_H 68
#define LIST_W 340
#define BIG 96
#define FIELD_W 300
#define FIELD_H 40
#define BACK_D 40

static int prompt;   /* 0 : la liste des comptes ; 1 : le compte choisi, son mot de passe */
static int hov = -1; /* sous le pointeur : un compte (0..), ou HV_* */
enum { HV_TRAY = 100, HV_BACK, HV_GO, HV_EYE, HV_OPEN };
static long tray_x, list_y, fx, fy, back_x; /* (posés au dessin) */

/* le haut : la date et l'heure au milieu, la zone d'état à droite */
static void draw_top(void)
{
	long W = ui.w;
	ui_fill(0, 0, W, TOP_H, BG);
	char h[8], d[48];
	d[0] = 0;
	heure(h, d);
	str_cat(str_cat(d + str_len(d), "  "), h); /* (str_cat écrit à l'adresse donnée et rend la fin) */
	long tw = ui_text_width((long)d, 14, (long)ui.font_b);
	ui_text((long)d, (W - tw) / 2, 22, INK, 14, (long)ui.font_b, 0);
	/* la zone d'état : réseau, son, alimentation, dans une pilule au survol */
	long tw2 = 3 * 26 + 12;
	tray_x = W - 8 - tw2;
	if (hov == HV_TRAY)
		ui_rbox(tray_x, 4, tw2, TOP_H - 8, (TOP_H - 8) / 2, BOX_HI, BOX_HI);
	long gx = tray_x + 8;
	ui_glyph((long)"glyph-tb-network", gx, 9, 16, INK);
	gx += 26; /* le haut-parleur : un corps, un pavillon, deux ondes */
	ui_fill(gx + 1, 15, 4, 6, INK);
	for (long i = 0; i < 5; i++)
		ui_fill(gx + 5 + i, 15 - i, 1, 6 + 2 * i, INK);
	ui_fill(gx + 12, 14, 2, 8, INK);
	ui_fill(gx + 15, 12, 2, 12, INK);
	gx += 26;
	ui_glyph((long)"glyph-power", gx, 9, 16, INK);
}

/* le bas : le nom du système, un losange devant */
static void draw_bottom(void)
{
	long W = ui.w, H = ui.h;
	const char *n = "Korin OS";
	long tw = ui_text_width((long)n, 18, (long)ui.font_b);
	long x = (W - tw - 24) / 2, y = H - 44;
	for (long j = 0; j < 14; j++) { /* le losange de 14 */
		long half = j < 7 ? j + 1 : 14 - j;
		ui_fill(x + 7 - half, y + j, 2 * half, 1, DIM);
	}
	ui_text((long)n, x + 24, y + 13, DIM, 18, (long)ui.font_b, 0);
}

static const char *acc_name(const struct acc *a)
{
	return a->full[0] ? a->full : a->name;
}

/* la liste des comptes : disque de 48 et nom, au milieu de l'écran */
static void draw_list(void)
{
	long W = ui.w, H = ui.h;
	long x = (W - LIST_W) / 2;
	list_y = (H - nacc * ROW_H) / 2 - 20;
	for (int i = 0; i < nacc; i++) {
		long y = list_y + i * ROW_H;
		if (hov == i || (hov < 0 && sel == i))
			ui_rbox(x, y, LIST_W, ROW_H - 4, 14, hov == i ? BOX_HI : BOX, hov == i ? BOX_HI : BOX);
		avatar(&accs[i], x + 12, y + 8, 48);
		ui_text((long)acc_name(&accs[i]), x + 76, y + 39, INK, 17, (long)ui.font_b, LIST_W - 90);
	}
}

/* le compte choisi : grand disque, nom, champ (ou « Ouvrir la session »), le bouton retour à gauche du champ */
static void draw_prompt(void)
{
	long W = ui.w, H = ui.h;
	const struct acc *a = &accs[sel];
	long y = H / 2 - 150;
	avatar(a, (W - BIG) / 2, y, BIG);
	y += BIG + 36;
	long tw = ui_text_width((long)acc_name(a), 22, (long)ui.font_b);
	ui_text((long)acc_name(a), (W - tw) / 2, y, INK, 22, (long)ui.font_b, 0);
	fx = (W - FIELD_W) / 2;
	fy = y + 24;
	back_x = -1000;
	if (nacc > 1) { /* à gauche : revenir à la liste */
		back_x = fx - BACK_D - 12;
		u32 c = hov == HV_BACK ? BOX_HI : BOX;
		ui_rbox(back_x, fy, BACK_D, BACK_D, BACK_D / 2, c, c);
		ui_glyph((long)"glyph-tb-left", back_x + 12, fy + 12, 16, INK);
	}
	if (!a->haspw) {
		u32 c = hov == HV_OPEN ? (u32)ui_mix(tok(TJ_ACCENT), INK, 40) : tok(TJ_ACCENT);
		ui_rbox(fx, fy, FIELD_W, FIELD_H, 10, c, c);
		const char *t = TR("Ouvrir la session");
		tw = ui_text_width((long)t, 15, (long)ui.font_b);
		ui_text((long)t, fx + (FIELD_W - tw) / 2, fy + 26, tok(TJ_ACCENT_INK), 15, (long)ui.font_b, 0);
		return;
	}
	u32 edge = err ? ERR : tok(TJ_ACCENT);
	ui_rbox(fx - 2, fy - 2, FIELD_W + 4, FIELD_H + 4, 12, edge, edge); /* anneau de focus */
	ui_rbox(fx, fy, FIELD_W, FIELD_H, 10, BOX, BOX);
	if (!pwlen)
		ui_text((long)TR("Mot de passe"), fx + 14, fy + 26, DIM, 15, (long)ui.font, 0);
	else if (show_pw)
		ui_text((long)pw, fx + 14, fy + 26, INK, 15, (long)ui.font, FIELD_W - 90);
	else {
		long n = pwlen > 20 ? 20 : pwlen;
		for (long i = 0; i < n; i++)
			ui_rbox(fx + 16 + i * 11, fy + 17, 7, 7, 4, INK, INK);
	}
	ui_glyph((long)"glyph-tb-eye", fx + FIELD_W - 66, fy + 12, 16, show_pw || hov == HV_EYE ? INK : DIM);
	u32 g = (pwlen && !busy) ? tok(TJ_ACCENT) : BOX_HI;
	ui_rbox(fx + FIELD_W - 36, fy + 6, 28, 28, 14, g, g);
	ui_glyph((long)"glyph-tb-right", fx + FIELD_W - 30, fy + 12, 16, pwlen ? tok(TJ_ACCENT_INK) : DIM);
	if (err) {
		const char *t = TR("Mot de passe incorrect. Réessayez.");
		tw = ui_text_width((long)t, 13, (long)ui.font);
		ui_text((long)t, (W - tw) / 2, fy + FIELD_H + 30, ERR, 13, (long)ui.font, 0);
	}
}

static void draw(void)
{
	ui_fill(0, 0, ui.w, ui.h, BG);
	draw_top();
	if (prompt)
		draw_prompt();
	else
		draw_list();
	draw_bottom();
	ui_update_all();
}

/* x, y -> ce qui est dessous (un compte, HV_*, -1) */
static int hit(long x, long y)
{
	if (y < TOP_H)
		return x >= tray_x ? HV_TRAY : -1;
	if (!prompt) {
		long x0 = (ui.w - LIST_W) / 2;
		if (x < x0 || x >= x0 + LIST_W || y < list_y)
			return -1;
		long i = (y - list_y) / ROW_H;
		return i < nacc && (y - list_y) % ROW_H < ROW_H - 4 ? (int)i : -1;
	}
	if (y < fy || y >= fy + FIELD_H)
		return -1;
	if (x >= back_x && x < back_x + BACK_D)
		return HV_BACK;
	if (!accs[sel].haspw)
		return x >= fx && x < fx + FIELD_W ? HV_OPEN : -1;
	if (x >= fx + FIELD_W - 70 && x < fx + FIELD_W - 42)
		return HV_EYE;
	if (x >= fx + FIELD_W - 38 && x < fx + FIELD_W)
		return HV_GO;
	return -1;
}

static void clear_pw(void)
{
	memset(pw, 0, sizeof pw);
	pwlen = 0;
	err = 0;
	show_pw = 0;
}

/* ---------------------------------------------------------------- actions */

static int submit(void)
{
	if (busy)
		return 0;
	if (accs[sel].haspw && !pwlen)
		return 0;
	busy = 1;
	draw();
	pw[pwlen] = 0;
	long v = auth_call(AUTH_VERIFY, accs[sel].name, pw);
	memset(pw, 0, sizeof pw);
	pwlen = 0;
	busy = 0;
	if (v == 0)
		return 1;
	err = 1;
	draw();
	return 0;
}

static void put_char(unsigned c)
{
	if (c < ' ' || pwlen + 4 >= (int)sizeof pw)
		return;
	if (c < 0x80)
		pw[pwlen++] = (char)c;
	else if (c < 0x800) {
		pw[pwlen++] = (char)(0xC0 | (c >> 6));
		pw[pwlen++] = (char)(0x80 | (c & 0x3F));
	} else {
		pw[pwlen++] = (char)(0xE0 | ((c >> 12) & 0x0F));
		pw[pwlen++] = (char)(0x80 | ((c >> 6) & 0x3F));
		pw[pwlen++] = (char)(0x80 | (c & 0x3F));
	}
	pw[pwlen] = 0;
	err = 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	charger();
	auth = korin_sys2(SYS_SVC_OPEN, "auth", 5000);
	auth_call(AUTH_FORGET, "", 0);
	if (!nacc)
		return 0; /* (pas de compte : la session par défaut, comme avant le multi-utilisateur) */
	/* une session fermée (init2, INIT_OP_LOGOUT) : l'écran se montre, sans la connexion automatique
	   (DESIGN-CONNEXION §3.4) */
	long st[2];
	int manuel = sys_stat("/tmp/login.manual", st) >= 0;
	for (int i = 0; i < nacc; i++) {
		if (!manuel && autologin[0] && str_eq(accs[i].name, autologin))
			return ouvrir(&accs[i]); /* connexion automatique */
		accs[i].haspw = auth_call(AUTH_HASPW, accs[i].name, 0) == 1;
		if (manuel && autologin[0] && str_eq(accs[i].name, autologin))
			sel = i; /* (le compte de la connexion automatique, choisi) */
	}
	if (nacc == 1 && !accs[0].haspw && !manuel)
		return ouvrir(&accs[0]);
	prompt = nacc == 1; /* (un seul compte : il est déjà choisi) */
	ui_init((long)&ui);
	ui_screen((long)scr);
	/* tout l'écran, couche normale (pas WF_TOP : les réglages rapides et le dialogue d'extinction passent devant ; pas
	   WF_BACK : un fond opaque va sous le fond d'écran, et n'a pas le focus) */
	if (ui_window_ex(0, 0, scr[0], scr[1], (long)TR("Ouvrir une session"), WF_FRAMELESS | WF_NOLIST) < 0)
		return ouvrir(&accs[0]); /* (pas de fenêtre : la session du premier compte plutôt que rien) */
	draw();
	for (;;) {
		long t = ui_event(1000);
		if (t == 0) { /* toutes les secondes : l'heure */
			draw_top();
			ui_update(0, 0, ui.w, TOP_H);
			continue;
		}
		if (t == WEV_SCREEN) { /* (une autre résolution) */
			if (ui_screen((long)scr) >= 0 && (scr[0] != ui.w || scr[1] != ui.h))
				ui_resize(scr[0], scr[1]);
			draw();
			continue;
		}
		if (t == WEV_KEY) {
			if (EVF(EV_KIND) == 0)
				continue;
			long code = EVF(EV_CODE);
			if (!prompt) { /* la liste : ↑ ↓ choisissent, Entrée ouvre le compte */
				if (code == HID_UP || code == HID_DOWN)
					sel = (sel + (code == HID_DOWN ? 1 : nacc - 1)) % nacc;
				else if (code == HID_ENTER || code == HID_KPENTER) {
					prompt = 1;
					clear_pw();
				} else
					continue;
			} else if (code == HID_ENTER || code == HID_KPENTER) {
				if (submit())
					return ouvrir(&accs[sel]);
			} else if (code == HID_ESC) { /* vide le champ, puis revient à la liste */
				if (pwlen || err)
					clear_pw();
				else if (nacc > 1)
					prompt = 0;
			} else if (code == HID_BACK) {
				while (pwlen > 0 && ((unsigned char)pw[pwlen - 1] & 0xC0) == 0x80)
					pwlen--;
				if (pwlen > 0)
					pwlen--;
				pw[pwlen] = 0;
			} else if (accs[sel].haspw)
				put_char((unsigned)EVF(EV_CHAR));
			draw();
		} else if (t == WEV_MOUSE) {
			long kind = EVF(EV_KIND);
			long mx = EVF(EV_X), my = EVF(EV_Y);
			if (kind == 2 && show_pw) { /* l'œil relâché : le mot de passe se cache */
				show_pw = 0;
				draw();
				continue;
			}
			int h = hit(mx, my);
			if (kind == 0) { /* survol (en (-1, -1) : le pointeur est parti) */
				if (h != hov) {
					hov = h;
					draw();
				}
				continue;
			}
			if (kind != 1)
				continue;
			if (h == HV_TRAY) { /* les réglages rapides, sous la zone d'état (« L » : ceux de l'écran de connexion) */
				char arg[24] = "L";
				*fmt_dec((u64)(ui.w - 4), arg + 1) = 0;
				ksys(SYS_SPAWN, (long)"/sys/quickmenu", (long)arg, 0, 0, 0, 0);
			} else if (!prompt && h >= 0 && h < nacc) {
				sel = h;
				prompt = 1;
				clear_pw();
				hov = -1;
				draw();
			} else if (h == HV_BACK) {
				prompt = 0;
				clear_pw();
				hov = -1;
				draw();
			} else if (h == HV_GO || h == HV_OPEN) {
				if (submit())
					return ouvrir(&accs[sel]);
			} else if (h == HV_EYE) {
				show_pw = 1;
				draw();
			}
		}
	}
}
