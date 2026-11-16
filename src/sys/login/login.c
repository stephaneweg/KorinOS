/* login : l'écran de connexion (docs/DESIGN-CONNEXION.md, variante A « la carte » ; étape 6 du multi-utilisateur,
 * docs/etude/MULTI-UTILISATEUR.md §2.8). Lancé par Elegant (uid 0) avant la session ; il écrit le compte ouvert
 * dans /tmp/session.ini (« uid = N »), puis se termine : Elegant lance alors la session sous ce compte.
 *
 *   - connexion automatique, sans rien montrer : « autologin = nom » dans /etc/system.ini, ou un seul compte
 *     (uid 1000 ou plus) sans mot de passe ;
 *   - sinon la carte : les comptes de /etc/users.ini (nom affiché ; « Administrateur » s'il est dans le groupe
 *     admin), le mot de passe du compte choisi, « Ouvrir la session ». authd vérifie le mot de passe
 *     (AUTH_VERIFY) : login ne lit jamais /etc/shadow.ini. Une seconde d'attente après un mauvais mot de passe,
 *     pas de blocage du compte.
 *
 * Première version : l'heure dans l'en-tête, le pied (clavier, accessibilité, alimentation), les images des
 * comptes, le verrou et le changement d'utilisateur viendront ensuite. */
#include "sys_consts.h"
#include "uikit_import.h"

#define AUTH_VERIFY 9
#define AUTH_HASPW 10
#define E_ACCES -13
#define ACC_MAX 8
#define M64(m, off) (*(u64 *)((u8 *)(m) + (off)))
#define M32(m, off) (*(u32 *)((u8 *)(m) + (off)))
#define TR(s) ((const char *)ui_tr((long)(s)))

static struct krt_ui ui;
static u8 msg[MSG_SIZE];
static long auth;

static struct acc {
	char name[32], full[64], groups[96];
	long uid;
} accs[ACC_MAX];
static int nacc;
static char autologin[32];
static char buf[8192];

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
		else if (str_eq(k, "groups"))
			str_copy_n(cur.groups, v, sizeof cur.groups);
	}
	p = lire("/etc/system.ini");
	while (p && (l = ligne(&p))) {
		char *v, *k = cle(l, &v);
		if (k && str_eq(k, "autologin"))
			str_copy_n(autologin, v, sizeof autologin);
	}
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

/* la session est ouverte pour ce compte : /tmp/session.ini, puis fin */
static int ouvrir(const struct acc *a)
{
	char line[48] = "uid = ";
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
	return 0;
}

static int admin(const struct acc *a)
{
	const char *g = a->groups;
	while (*g) {
		while (*g == ' ' || *g == ',')
			g++;
		const char *s = g;
		while (*g && *g != ' ' && *g != ',')
			g++;
		if (g - s == 5 && !memcmp(s, "admin", 5))
			return 1;
	}
	return 0;
}

static long w_pw, w_radio[ACC_MAX];

static long card_fn(long d, long w, long code)
{
	if (code == UN_ACTIVATE && w == w_pw)
		ui_dialog_close(d, 1);
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	charger();
	auth = korin_sys2(SYS_SVC_OPEN, "auth", 5000);
	if (!nacc) /* (pas de compte : la session par défaut, comme avant le multi-utilisateur) */
		return 0;
	for (int i = 0; i < nacc; i++) /* connexion automatique */
		if (autologin[0] && str_eq(accs[i].name, autologin))
			return ouvrir(&accs[i]);
	if (nacc == 1 && auth_call(AUTH_HASPW, accs[0].name, 0) == 0)
		return ouvrir(&accs[0]);
	ui_init((long)&ui);
	int sel = 0;
	enum { DW = 400, M = 20, RH = 40 };
	for (;;) {
		long DH = 90 + nacc * RH + 120;
		long d = ui_dialog_begin(DW, DH, (long)TR("Ouvrir une session"));
		if (!d)
			return 0;
		long y = 16;
		ui_label(M, y, DW - 2 * M, 22, (long)TR("Choisissez votre compte :"), ULB_DIM);
		y += 30;
		for (int i = 0; i < nacc; i++) {
			static char lab[ACC_MAX][128];
			str_copy_n(lab[i], accs[i].full[0] ? accs[i].full : accs[i].name, 64);
			if (admin(&accs[i]))
				str_cat(str_cat(lab[i] + str_len(lab[i]), " - "), TR("Administrateur"));
			w_radio[i] = ui_radio(M, y, DW - 2 * M, 30, (long)lab[i], 1);
			if (i == sel)
				ui_w_set_value(w_radio[i], 1);
			y += RH;
		}
		y += 6;
		w_pw = ui_textbox(M, y, DW - 2 * M, 32, UTF_PASSWORD);
		ui_textbox_hint(w_pw, (long)TR("Mot de passe (vide : aucun)"));
		long e = ui_dialog_button(d, 0, (long)TR("Ouvrir la session"), 1, UB_DEFAULT, DW - 12);
		(void)e;
		ui_w_focus(w_pw);
		long r = ui_dialog_run(d, (long)card_fn, 0);
		static char pw[128];
		str_copy_n(pw, (const char *)ui_w_get_text(w_pw), sizeof pw);
		for (int i = 0; i < nacc; i++)
			if (ui_w_get_value(w_radio[i]))
				sel = i;
		ui_dialog_end(d);
		if (r != 1) /* (Échap, fermer : la carte revient) */
			continue;
		long v = auth_call(AUTH_VERIFY, accs[sel].name, pw);
		memset(pw, 0, sizeof pw);
		if (v == 0)
			return ouvrir(&accs[sel]);
		ui_message((long)TR("Ouvrir une session"), (long)TR("Mot de passe incorrect. Réessayez."), UM_OK);
	}
}
