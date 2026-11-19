/* login : l'écran de connexion (docs/DESIGN-CONNEXION.md, variante A « la carte », sans barre de menus ; maquettes
 * docs/maquettes/connexion/a-fenetree.html A1, A2, A3, A4, A6 ; étape 6 de docs/etude/MULTI-UTILISATEUR.md).
 * Lancé par Elegant (uid 0) avant la session, sur le fond à facettes ; il écrit le compte ouvert dans
 * /tmp/session.ini (« uid = N ») puis se termine : Elegant lance alors la session sous ce compte.
 *
 *   - rien à demander, rien à montrer : « autologin = nom » (/etc/system.ini), ou un seul compte sans mot de passe ;
 *   - la carte (400 px, rayon 12, jeton RAISED) : en-tête « Ouvrir une session », le nom de la machine, l'heure et
 *     la date ; les comptes (disque d'initiale de 32 coloré par l'uid, nom affiché, « Administrateur », « Compte
 *     standard » ou « Sans mot de passe ») ; le compte choisi dans un bloc ACCENT_LIGHT avec son champ (32 px,
 *     l'œil tenu montre le mot de passe, la flèche ACCENT = Entrée), ou le bouton « Ouvrir la session » sans mot de
 *     passe ; le pied : clavier (code de keyb=), accessibilité (plus tard), réseau, alimentation (menu) ;
 *   - un seul compte (A4) : carte de 320, l'heure en grand, le disque de 64, le nom, le champ ;
 *   - clavier d'abord : le champ a le focus, ↑ ↓ changent de compte, Entrée ouvre, Échap vide le champ ;
 *   - mauvais mot de passe : « Mot de passe incorrect. Réessayez. » en DANGER, le champ vidé, la flèche grisée une
 *     seconde (authd attend aussi) ; pas de blocage du compte.
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
#define SH 4 /* l'ombre autour de la carte (ui_shadow) */
#define PAD 20
#define ROW 48
#define FIELD 32
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
static long cw, ch, scr[4], wx, wy; /* taille de la carte, l'écran, la place de la fenêtre */
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

static const char *role(const struct acc *a)
{
	if (!a->haspw)
		return TR("Sans mot de passe");
	return TR(a->admin ? "Administrateur" : "Compte standard");
}

static long field_y, go_x, eye_x, foot_y, pow_x, row0; /* (zones cliquables, posées au dessin) */

/* le champ du mot de passe, ou le bouton « Ouvrir la session » d'un compte sans mot de passe */
static void field(long x, long y, long w)
{
	field_y = y;
	if (!accs[sel].haspw) {
		ui_rbox(x, y, w, FIELD, 8, tok(TJ_ACCENT), tok(TJ_ACCENT));
		const char *t = TR("Ouvrir la session");
		long tw = ui_text_width((long)t, 14, (long)ui.font_b);
		ui_text((long)t, x + (w - tw - 22) / 2, y + 21, tok(TJ_ACCENT_INK), 14, (long)ui.font_b, 0);
		ui_glyph((long)"glyph-tb-right", x + (w + tw - 22) / 2 + 6, y + 8, 16, tok(TJ_ACCENT_INK));
		go_x = x;
		eye_x = -1000;
		return;
	}
	u32 edge = tok(err ? TJ_DANGER : TJ_ACCENT);
	ui_rbox(x - 2, y - 2, w + 4, FIELD + 4, 10, edge, edge); /* anneau de focus */
	ui_rbox(x, y, w, FIELD, 8, tok(TJ_SURFACE), tok(TJ_SURFACE));
	if (!pwlen)
		ui_text((long)TR("Mot de passe"), x + 10, y + 21, tok(TJ_TEXT3), 14, (long)ui.font, 0);
	else if (show_pw)
		ui_text((long)pw, x + 10, y + 21, tok(TJ_TEXT1), 14, (long)ui.font, w - 80);
	else {
		long n = pwlen > 24 ? 24 : pwlen;
		for (long i = 0; i < n; i++)
			ui_rbox(x + 12 + i * 11, y + 13, 6, 6, 3, tok(TJ_TEXT1), tok(TJ_TEXT1));
	}
	eye_x = x + w - 60;
	ui_glyph((long)"glyph-tb-eye", eye_x + 4, y + 8, 16, tok(show_pw ? TJ_ACCENT : TJ_TEXT3));
	go_x = x + w - 30;
	u32 g = (pwlen && !busy) ? tok(TJ_ACCENT) : tok(TJ_LINE_STRONG);
	ui_rbox(go_x, y + 3, 26, 26, 6, g, g);
	ui_glyph((long)"glyph-tb-right", go_x + 5, y + 8, 16, tok(TJ_ACCENT_INK));
}

static void foot(long y)
{
	foot_y = y;
	ui_fill(SH, y - 14, cw, 1, tok(TJ_LINE));
	long x = SH + PAD - 6;
	ui_glyph((long)"glyph-tb-keyboard", x + 6, y + 6, 16, tok(TJ_TEXT2));
	ui_text((long)keyb, x + 26, y + 19, tok(TJ_TEXT2), 12, (long)ui.font_b, 0);
	ui_glyph((long)"glyph-tb-access", x + 58, y + 6, 16, tok(TJ_TEXT3));
	pow_x = SH + cw - PAD - 22;
	ui_glyph((long)"glyph-tb-network", pow_x - 30, y + 6, 16, tok(TJ_TEXT2));
	ui_glyph((long)"glyph-power", pow_x + 3, y + 6, 16, tok(TJ_TEXT2));
}

static void draw(void)
{
	ui_shadow(cw + 2 * SH, ch + 2 * SH);
	ui_rbox(SH, SH, cw, ch, 12, tok(TJ_RAISED), tok(TJ_RAISED));
	char h[8], d[40];
	d[0] = 0;
	heure(h, d);
	long x = SH + PAD, w = cw - 2 * PAD, y = SH + PAD;
	if (nacc == 1) { /* A4 : un seul compte */
		long tw = ui_text_width((long)h, 28, (long)ui.font_b);
		ui_text((long)h, SH + (cw - tw) / 2, y + 28, tok(TJ_TEXT1), 28, (long)ui.font_b, 0);
		tw = ui_text_width((long)d, 12, (long)ui.font);
		ui_text((long)d, SH + (cw - tw) / 2, y + 48, tok(TJ_TEXT2), 12, (long)ui.font, 0);
		y += 66;
		avatar(&accs[0], SH + (cw - 64) / 2, y, 64);
		y += 72;
		const char *n = accs[0].full[0] ? accs[0].full : accs[0].name;
		tw = ui_text_width((long)n, 16, (long)ui.font_b);
		ui_text((long)n, SH + (cw - tw) / 2, y + 16, tok(TJ_TEXT1), 16, (long)ui.font_b, 0);
		y += 30;
		field(x, y, w);
		y += FIELD + 8;
	} else { /* A1 : plusieurs comptes */
		ui_text((long)TR("Ouvrir une session"), x, y + 16, tok(TJ_TEXT1), 16, (long)ui.font_b, 0);
		ui_text((long)machine, x, y + 36, tok(TJ_TEXT2), 13, (long)ui.font, 0);
		long tw = ui_text_width((long)h, 16, (long)ui.font_b);
		ui_text((long)h, x + w - tw, y + 16, tok(TJ_TEXT1), 16, (long)ui.font_b, 0);
		tw = ui_text_width((long)d, 12, (long)ui.font);
		ui_text((long)d, x + w - tw, y + 36, tok(TJ_TEXT2), 12, (long)ui.font, 0);
		y += 44 + 14;
		ui_fill(SH, y, cw, 1, tok(TJ_LINE));
		y += 14;
		row0 = y;
		for (int i = 0; i < nacc; i++) {
			long bh = i == sel ? ROW + FIELD + 12 : ROW;
			if (i == sel)
				ui_rbox(x - 8, y, w + 16, bh, 6, tok(TJ_ACCENT_LIGHT), tok(TJ_ACCENT_LIGHT));
			avatar(&accs[i], x, y + 8, 32);
			const char *n = accs[i].full[0] ? accs[i].full : accs[i].name;
			ui_text((long)n, x + 44, y + 22, tok(TJ_TEXT1), 14, (long)ui.font_b, 0);
			ui_text((long)role(&accs[i]), x + 44, y + 38, tok(TJ_TEXT3), 12, (long)ui.font, 0);
			if (i == sel)
				field(x, y + ROW, w);
			y += bh;
		}
		y += 4;
	}
	if (err)
		ui_text((long)TR("Mot de passe incorrect. Réessayez."), x, y + 12, tok(TJ_DANGER), 12, (long)ui.font, 0);
	y += 16 + 14;
	foot(y);
	ui_update_all();
}

/* la taille de la carte d'après le nombre de comptes */
static void size(void)
{
	if (nacc == 1) {
		cw = 320;
		ch = PAD + 66 + 72 + 30 + FIELD + 8 + 30 + 34 + PAD - 10;
	} else {
		cw = 400;
		ch = PAD + 58 + 14 + nacc * ROW + FIELD + 12 + 4 + 30 + 34 + PAD - 10;
	}
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

static void power_menu(void)
{
	long r = ui_popup(wx + pow_x - 190, wy + foot_y + 28, (long)TR("Redémarrer\nÉteindre"), 220);
	if (r < 0)
		return;
	long chn = korin_sys2(SYS_SVC_OPEN, "init2", 1000);
	if (chn < 0)
		return;
	/* la marque de l'arrêt : Elegant n'ouvre pas la session quand login tombe ensuite (session_poll) */
	long fd = sys_open("/tmp/login.halt", O_WRITE | O_CREATE | O_TRUNC);
	if (fd >= 0)
		sys_close(fd);
	memset(msg, 0, MSG_SIZE);
	M64(msg, 0) = 3; /* INIT_OP_REBOOT : a bit 1 = arrêt */
	M64(msg, 8) = r == 1 ? 2 : 0;
	korin_sys2(SYS_SVC_CALL, chn, msg);
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
	ui_init((long)&ui);
	ui_screen((long)scr);
	size();
	wx = (scr[0] - cw) / 2 - SH;
	wy = (scr[1] - ch) / 2 - SH;
	if (ui_window_ex(wx, wy, cw + 2 * SH, ch + 2 * SH, (long)TR("Ouvrir une session"),
			 WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_ALPHA) < 0)
		return ouvrir(&accs[0]); /* (pas de fenêtre : la session du premier compte plutôt que rien) */
	draw();
	for (;;) {
		long t = ui_event(1000);
		if (t == 0) { /* toutes les secondes : l'heure */
			draw();
			continue;
		}
		if (t == WEV_KEY) {
			if (EVF(EV_KIND) == 0)
				continue;
			long code = EVF(EV_CODE);
			if (code == HID_ENTER || code == HID_KPENTER) {
				if (submit())
					return ouvrir(&accs[sel]);
			} else if (code == HID_ESC) {
				memset(pw, 0, sizeof pw);
				pwlen = 0;
				err = 0;
			} else if (code == HID_UP || code == HID_DOWN) {
				sel = (sel + (code == HID_DOWN ? 1 : nacc - 1)) % nacc;
				memset(pw, 0, sizeof pw);
				pwlen = 0;
				err = 0;
			} else if (code == HID_BACK) {
				while (pwlen > 0 && ((unsigned char)pw[pwlen - 1] & 0xC0) == 0x80)
					pwlen--;
				if (pwlen > 0)
					pwlen--;
				pw[pwlen] = 0;
			} else
				put_char((unsigned)EVF(EV_CHAR));
			draw();
		} else if (t == WEV_MOUSE) {
			long kind = EVF(EV_KIND);
			long mx = EVF(EV_X), my = EVF(EV_Y);
			if (kind == 0 && show_pw) { /* l'œil relâché : le mot de passe se cache */
				show_pw = 0;
				draw();
				continue;
			}
			if (kind != 1)
				continue;
			if (my >= field_y && my < field_y + FIELD) {
				if (mx >= go_x && (mx < go_x + 26 || !accs[sel].haspw)) {
					if (submit())
						return ouvrir(&accs[sel]);
				} else if (mx >= eye_x && mx < eye_x + 26) {
					show_pw = 1;
					draw();
				}
			} else if (my >= foot_y && my < foot_y + 28 && mx >= pow_x) {
				power_menu();
			} else if (nacc > 1 && my >= row0) { /* une ligne de compte */
				long yy = row0;
				for (int i = 0; i < nacc; i++) {
					long bh = i == sel ? ROW + FIELD + 12 : ROW;
					if (my >= yy && my < yy + ROW && i != sel) {
						sel = i;
						memset(pw, 0, sizeof pw);
						pwlen = 0;
						err = 0;
						draw();
						break;
					}
					yy += bh;
				}
			}
		}
	}
}
