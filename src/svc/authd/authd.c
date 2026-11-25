/* authd : l'élévation de Korin (docs/etude/MULTI-UTILISATEUR.md §2.6, étape 4). Service « auth », lancé par
 * init2 (services.txt) avec l'uid 0. Il dit qui peut s'élever, vérifie les mots de passe et passe un process
 * en uid 0 (sudo). Fichiers :
 *
 *   /etc/users.ini    les comptes ([nom] uid, groups...)
 *   /etc/elevate.ini  qui peut s'élever : [elevate] users = a, b ; groups = admin, dev
 *                     (sans le fichier : le groupe admin)
 *   /etc/shadow.ini   [shadow] nom = pbkdf2-sha256$<tours>$<sel hex>$<empreinte hex> ; lisible par l'uid 0
 *                     seulement (règles de vfs) ; pas de ligne, ou une valeur vide : pas de mot de passe
 *
 * L'élévation demande le mot de passe de l'utilisateur (décision du 16/11), comme sudo ; un compte sans mot de
 * passe s'élève sans rien taper. Un mauvais mot de passe coûte une seconde (pas de blocage du compte : le
 * projet ne se défend pas contre un attaquant, décision du 29/10).
 *
 * Opérations (a, b, c, d, données ; l'appelant : MSG_PID, son uid par le noyau) :
 *   AUTH_CAN     -> 0 : ne peut pas s'élever ; 1 : peut, sans mot de passe ; 2 : peut, avec son mot de passe
 *   AUTH_SUDO    données : mot de passe -> 0 : l'appelant est passé en uid 0 ; E_ACCES : mauvais mot de passe ;
 *                E_PERM : pas dans elevate.ini
 *   AUTH_CHECK   données : mot de passe -> 0 s'il est celui du compte de l'appelant (ou le compte n'en a pas)
 *   AUTH_PASSWD  données : ancien, 0, nouveau, 0 -> 0 : le mot de passe du compte de l'appelant est changé
 *                (nouveau vide : plus de mot de passe)
 *   AUTH_ELEVATE données : la raison (« Logiciels veut installer Tetris ») -> 0 : l'appelant (une app) est
 *                passé en uid 0 ; E_CANCELED : refusé à la carte ; E_PERM : pas dans elevate.ini ; E_BUSY :
 *                une autre carte est ouverte. La réponse attend la carte de confirmation (/sys/authcard,
 *                lancée par authd en uid 0 : ce n'est pas l'app qui la dessine), un mot de passe à la fois.
 *   AUTH_CARD    (la carte seulement) a = jeton, b = 1 annuler ; données : mot de passe -> 0 : accepté (ou
 *                annulé) ; E_ACCES : mauvais mot de passe, la carte reste
 *   AUTH_WAIT    (l'app, après E_AGAIN) -> comme AUTH_ELEVATE. Le noyau abandonne un appel au bout de 30 s :
 *                authd rend E_AGAIN au bout de 20 s, et ui_elevate redemande par AUTH_WAIT
 *   AUTH_FORGET  oublier les élévations retenues : l'uid 0 (l'écran de connexion, à chaque ouverture de
 *                session) les oublie toutes ; un compte, les siennes (sudo -k)
 *
 * Retenir pendant 5 minutes (maquette docs/maquettes/elevation-retenir.html) : la case de la carte (AUTH_CARD
 * c = 1) ou la réponse « o » de sudo (AUTH_SUDO c = 1) retient le compte pour une portée : 0, la session
 * graphique (la carte) ; pour sudo, le pid du shell qui l'a lancé (AUTH_CAN et AUTH_SUDO b = SYS_GETPPID, dit
 * par sudo : les pids ne reviennent pas, un terminal fermé n'a plus rien de retenu). Tant que le délai court,
 * AUTH_CAN rend 1 et l'élévation est accordée sans mot de passe ni carte. Le délai part de la saisie et ne
 * s'allonge pas ; il n'est qu'en mémoire (un redémarrage l'oublie).
 *
 * Le coffre à secrets (docs/KEYVAULT.md) : sa clé dépend du mot de passe du compte. authd passe donc au service
 * keyvault le mot de passe vérifié à la connexion (KV_UNLOCK), l'ancien et le nouveau quand il change
 * (KV_PASSWD), le nouveau seul quand l'uid 0 le remet (KV_SETPW : le coffre fermé est alors perdu). keyvault
 * n'appelle jamais authd (pas d'attente croisée). */
#include "korinrt.h"

#define M64(m, off) (*(u64 *)((u8 *)(m) + (off)))
#define M32(m, off) (*(u32 *)((u8 *)(m) + (off)))

#define AUTH_CAN 1
#define AUTH_SUDO 2
#define AUTH_CHECK 3
#define AUTH_PASSWD 4
#define AUTH_ELEVATE 5
#define AUTH_CARD 6
#define AUTH_WAIT 7
#define AUTH_SETPW 8 /* (uid 0) données : nom, 0, nouveau, 0 : le mot de passe d'un compte (sudo passwd nom) */
#define AUTH_VERIFY 9 /* (uid 0 : l'écran de connexion) données : nom, 0, mot de passe -> 0 / E_ACCES */
#define AUTH_HASPW 10 /* données : nom -> 1 : le compte a un mot de passe, 0 : non */
#define AUTH_FORGET 11
#define GRACE_MS (5 * 60 * 1000)
#define GRACES 32
#define E_AGAIN -11
#define WAIT_MS 20000
#define E_BUSY -16
#define E_CANCELED -125
#define DEFER 1L << 40 /* traiter : la réponse attend (la carte) */
#define SVC_OP_END 0xFFFF

#define E_ACCES -13
#define ITERATIONS 20000
#define FILE_MAX 8192

static u8 msg[MSG_SIZE];
static u8 msg2[MSG_SIZE]; /* la réponse différée de AUTH_ELEVATE */
static long cur_req;      /* la requête en cours (traiter) */

/* l'élévation qui attend sa carte (une seule à la fois) */
static struct {
	int active; /* une carte est ouverte (ou sa réponse attend d'être prise) */
	int done;   /* la carte a répondu, result attend AUTH_WAIT */
	long result;
	long req;   /* la requête de l'app qui attend (0 : aucune pour l'instant) */
	u64 since;  /* depuis quand elle attend */
	long pid;   /* l'app */
	long card;  /* le pid de /sys/authcard */
	long uid;   /* le compte de l'app */
	u64 token;
	char name[32];
} pending;

#include "pwhash.h" /* SHA-256, HMAC, PBKDF2 */

/* ---------------------------------------------------------------- fichiers */

static char fbuf[FILE_MAX];

static char *lire(const char *path)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, fbuf, FILE_MAX - 1);
	sys_close(fd);
	if (n < 0)
		return 0;
	fbuf[n] = 0;
	return fbuf;
}

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

static void copie(char *d, u64 cap, const char *s)
{
	u64 n = (u64)str_len(s);
	if (n >= cap)
		n = cap - 1;
	memcpy(d, s, n);
	d[n] = 0;
}

/* « a, b, c » contient-il le mot w ? */
static int dans_liste(const char *list, const char *w)
{
	u64 n = (u64)str_len(w);
	const char *p = list;
	while (*p) {
		while (*p == ' ' || *p == ',' || *p == '\t')
			p++;
		const char *s = p;
		while (*p && *p != ',' && *p != ' ' && *p != '\t')
			p++;
		if ((u64)(p - s) == n && n && !memcmp(s, w, n))
			return 1;
	}
	return 0;
}

struct compte {
	char name[32];
	char groups[128];
	long uid;
};

/* le compte d'un uid -> 1 */
static int compte_de(long uid, struct compte *c)
{
	char *p = lire("/etc/users.ini"), *l;
	int found = 0;
	struct compte cur;
	cur.uid = -1;
	cur.name[0] = cur.groups[0] = 0;
	while (p && (l = ligne(&p))) {
		if (*l == '[') {
			if (cur.uid == uid && cur.name[0]) {
				*c = cur;
				found = 1;
			}
			char *e = l + 1;
			while (*e && *e != ']')
				e++;
			*e = 0;
			copie(cur.name, sizeof cur.name, l + 1);
			cur.groups[0] = 0;
			cur.uid = -1;
			continue;
		}
		char *v, *k = cle(l, &v);
		if (!k)
			continue;
		if (str_eq(k, "uid"))
			cur.uid = (long)parse_dec(v);
		else if (str_eq(k, "groups"))
			copie(cur.groups, sizeof cur.groups, v);
	}
	if (!found && cur.uid == uid && cur.name[0]) {
		*c = cur;
		found = 1;
	}
	return found;
}

/* le compte peut-il s'élever (elevate.ini ; sans le fichier : le groupe admin) ? */
static int peut_elever(const struct compte *c)
{
	char users[256] = "", groups[256] = "admin";
	char *p = lire("/etc/elevate.ini"), *l;
	if (p)
		groups[0] = 0;
	while (p && (l = ligne(&p))) {
		char *v, *k = cle(l, &v);
		if (k && str_eq(k, "users"))
			copie(users, sizeof users, v);
		else if (k && str_eq(k, "groups"))
			copie(groups, sizeof groups, v);
	}
	if (dans_liste(users, c->name))
		return 1;
	const char *g = c->groups;
	while (*g) {
		while (*g == ' ' || *g == ',')
			g++;
		char one[32];
		u64 n = 0;
		while (*g && *g != ',' && *g != ' ' && n < 31)
			one[n++] = *g++;
		one[n] = 0;
		while (*g && *g != ',' && *g != ' ')
			g++;
		if (n && dans_liste(groups, one))
			return 1;
	}
	return 0;
}

static int hexval(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static int unhex(const char *s, u8 *out, int max)
{
	int n = 0;
	while (s[0] && s[1] && s[0] != '$' && n < max) {
		int a = hexval(s[0]), b = hexval(s[1]);
		if (a < 0 || b < 0)
			return -1;
		out[n++] = (u8)(a << 4 | b);
		s += 2;
	}
	return n;
}

static char empreinte[200]; /* la ligne de shadow.ini du compte */

/* l'empreinte du compte -> 1 (dans empreinte ; vide : pas de mot de passe), 0 : pas de ligne, -1 :
 * shadow.ini illisible (autre chose qu'absent) : alors ni « pas de mot de passe », ni connexion (D10, E40) */
static int empreinte_de(const char *name)
{
	empreinte[0] = 0;
	long fd = sys_open("/etc/shadow.ini", O_READ);
	if (fd < 0)
		return fd == E_NOENT ? 0 : -1;
	sys_close(fd);
	char *p = lire("/etc/shadow.ini"), *l;
	if (!p)
		return -1;
	while (p && (l = ligne(&p))) {
		char *v, *k = cle(l, &v);
		if (k && str_eq(k, name)) {
			copie(empreinte, sizeof empreinte, v);
			return 1;
		}
	}
	return 0;
}

/* le mot de passe est-il le bon pour le compte ? (pas de mot de passe : oui pour tout) */
static int verifier(const char *name, const char *pw)
{
	int found = empreinte_de(name);
	if (found < 0)
		return 0; /* (illisible : refusé, pas « sans mot de passe ») */
	if (!found || !empreinte[0])
		return 1;
	const char *e = empreinte;
	const char *pre = "pbkdf2-sha256$";
	for (; *pre; pre++, e++)
		if (*e != *pre)
			return 0;
	u32 iter = (u32)parse_dec(e);
	while (*e && *e != '$')
		e++;
	if (!*e++ || !iter)
		return 0;
	u8 salt[32], want[32], got[32];
	int sl = unhex(e, salt, 32);
	while (*e && *e != '$')
		e++;
	if (sl <= 0 || !*e++ || unhex(e, want, 32) != 32)
		return 0;
	pbkdf2(pw, salt, (u64)sl, iter, got);
	u8 diff = 0;
	for (int i = 0; i < 32; i++)
		diff |= got[i] ^ want[i];
	return diff == 0;
}

static void hex(const u8 *b, int n, char *o)
{
	static const char d[] = "0123456789abcdef";
	for (int i = 0; i < n; i++) {
		*o++ = d[b[i] >> 4];
		*o++ = d[b[i] & 15];
	}
	*o = 0;
}

static char nouveau[FILE_MAX + 256];

/* un nom de compte permis (la règle de useradd, uf_name_ok) : minuscules, chiffres, « - » et « _ »,
 * 1 à 31 caractères, une lettre d'abord. Sinon un nom pourrait déborder ligne_cpt, ou ajouter des
 * lignes à shadow.ini (« \n », « = »). */
static int nom_ok(const char *n)
{
	if (!(n[0] >= 'a' && n[0] <= 'z'))
		return 0;
	u64 k = 0;
	for (; n[k]; k++)
		if (!((n[k] >= 'a' && n[k] <= 'z') || (n[k] >= '0' && n[k] <= '9') || n[k] == '-' || n[k] == '_') ||
		    k >= 31)
			return 0;
	return 1;
}

/* le mot de passe du compte devient pw (vide : aucun) : shadow.ini récrit, les autres lignes gardées */
static long ecrire_mdp(const char *name, const char *pw)
{
	char ligne_cpt[200];
	if (!nom_ok(name))
		return E_INVAL;
	if (*pw) {
		u8 salt[16], h[32];
		if (korin_sys2(SYS_GETRANDOM, salt, sizeof salt) != (long)sizeof salt)
			return E_IO;
		pbkdf2(pw, salt, sizeof salt, ITERATIONS, h);
		char sh[40], hh[70], it[16];
		hex(salt, 16, sh);
		hex(h, 32, hh);
		u32 v = ITERATIONS;
		int n = 0;
		char t[16];
		do
			t[n++] = (char)('0' + v % 10);
		while ((v /= 10));
		for (int i = 0; i < n; i++)
			it[i] = t[n - 1 - i];
		it[n] = 0;
		str_cat(str_cat(str_cat(str_cat(str_cat(str_cat(str_cat(str_cat(ligne_cpt, name), " = pbkdf2-sha256$"), it), "$"),
						sh), "$"), hh), "\n");
	} else
		str_cat(str_cat(ligne_cpt, name), " =\n");
	char *p = lire("/etc/shadow.ini");
	char *o = nouveau;
	int fait = 0;
	o = str_cat(o, "# /etc/shadow.ini : les empreintes des mots de passe (authd), lisible par l'uid 0 seulement\n");
	char *l;
	while (p && (l = ligne(&p))) {
		if (!*l || *l == '#')
			continue;
		char tmp[256];
		copie(tmp, sizeof tmp, l);
		char *v, *k = cle(tmp, &v);
		if (k && str_eq(k, name)) {
			o = str_cat(o, ligne_cpt);
			fait = 1;
			continue;
		}
		o = str_cat(str_cat(o, l), "\n");
	}
	if (!fait)
		o = str_cat(o, ligne_cpt);
	long fd = sys_open("/etc/shadow.ini", O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long r = sys_write(fd, nouveau, (u64)(o - nouveau));
	sys_close(fd);
	return r < 0 ? r : 0;
}

/* ---------------------------------------------------------------- le coffre (keyvault) */

#define KV_UNLOCK 6
#define KV_PASSWD 8
#define KV_SETPW 9

/* données : a, 0, b, 0, c (c peut manquer) -> la réponse de keyvault ; pas de coffre : 0 */
static long coffre(long op, const char *a, const char *b, const char *c)
{
	static long ch = -1; /* (le canal se garde : pas de fermeture des canaux) */
	if (ch < 0)
		ch = korin_sys2(SYS_SVC_OPEN, "keyvault", 0);
	if (ch < 0)
		return 0;
	static u8 m[MSG_SIZE];
	memset(m, 0, MSG_SIZE);
	M64(m, 0) = (u64)op;
	u8 *d = m + MSG_DATA;
	u64 o = 0, cap = MSG_SIZE - MSG_DATA - 1;
	const char *p[3] = {a, b, c};
	for (int i = 0; i < 3 && p[i]; i++) {
		u64 n = (u64)str_len(p[i]);
		if (o + n + 1 > cap)
			return E_INVAL;
		memcpy(d + o, p[i], n);
		o += n + 1;
	}
	M32(m, 40) = (u32)o;
	long r = korin_sys2(SYS_SVC_CALL, ch, m);
	if (r >= 0)
		r = (long)M64(m, 0);
	memset(m, 0, MSG_SIZE);
	if (r == E_PIPE) /* (keyvault relancé : un autre canal la prochaine fois) */
		ch = -1;
	return r;
}

/* ---------------------------------------------------------------- retenir 5 minutes */

static struct {
	long uid;
	u64 scope; /* 0 : la session graphique ; sinon le pid du shell de sudo */
	u64 until; /* SYS_TIME_MS ; 0 : place libre */
} graces[GRACES];

static int retenu(long uid, u64 scope)
{
	u64 now = korin_sys0(SYS_TIME_MS);
	for (int i = 0; i < GRACES; i++) {
		if (graces[i].until && graces[i].until <= now)
			graces[i].until = 0;
		if (graces[i].until && graces[i].uid == uid && graces[i].scope == scope)
			return 1;
	}
	return 0;
}

static void retenir(long uid, u64 scope)
{
	u64 now = korin_sys0(SYS_TIME_MS);
	int k = -1;
	for (int i = 0; i < GRACES; i++) {
		if (graces[i].until && graces[i].uid == uid && graces[i].scope == scope) {
			k = i;
			break;
		}
		if (k < 0 && (!graces[i].until || graces[i].until <= now))
			k = i;
	}
	if (k < 0) { /* (tout est pris : la plus proche de sa fin cède la place) */
		k = 0;
		for (int i = 1; i < GRACES; i++)
			if (graces[i].until < graces[k].until)
				k = i;
	}
	graces[k].uid = uid;
	graces[k].scope = scope;
	graces[k].until = now + GRACE_MS;
}

/* uid < 0 : tout */
static void oublier(long uid)
{
	for (int i = 0; i < GRACES; i++)
		if (uid < 0 || graces[i].uid == uid)
			graces[i].until = 0;
}

/* ---------------------------------------------------------------- le service */

static void journal(const char *what, const struct compte *c, long pid, const char *res)
{
	char n[24];
	u64 v = (u64)pid;
	int k = 0;
	char t[24];
	do
		t[k++] = (char)('0' + v % 10);
	while ((v /= 10));
	for (int i = 0; i < k; i++)
		n[i] = t[k - 1 - i];
	n[k] = 0;
	ak_puts("authd : ");
	ak_puts(what);
	ak_puts(" par ");
	ak_puts(c->name[0] ? c->name : "?");
	ak_puts(" (pid ");
	ak_puts(n);
	ak_puts(") : ");
	ak_puts(res);
	ak_puts("\n");
}

static void repondre(long req, long r)
{
	memset(msg2, 0, MSG_SIZE);
	M64(msg2, 0) = (u64)r;
	korin_sys2(SYS_SVC_REPLY, req, msg2);
}

/* la carte a répondu (r) : à l'app qui attend, sinon gardé pour son AUTH_WAIT */
static void finir(long r)
{
	if (!pending.active)
		return;
	pending.card = 0;
	if (pending.req) {
		repondre(pending.req, r);
		pending.req = 0;
		pending.active = 0;
	} else {
		pending.done = 1;
		pending.result = r;
	}
}

/* toutes les secondes : une app qui attend depuis 20 s reçoit E_AGAIN (elle redemandera) */
static void veiller(void)
{
	if (pending.active && pending.req && korin_sys0(SYS_TIME_MS) - pending.since > WAIT_MS) {
		repondre(pending.req, E_AGAIN);
		pending.req = 0;
	}
}

static void dec_to(u64 v, char *o)
{
	char t[24];
	int k = 0;
	do
		t[k++] = (char)('0' + v % 10);
	while ((v /= 10));
	while (k)
		*o++ = t[--k];
	*o = 0;
}

static long elever(const struct compte *c, long pid, const char *raison)
{
	if (pending.active)
		return E_BUSY;
	if (c->uid != 0 && !peut_elever(c)) {
		journal("elevation", c, pid, "refusee (pas dans /etc/elevate.ini)");
		return E_PERM;
	}
	if (c->uid == 0)
		return 0; /* (déjà system) */
	if (retenu(c->uid, 0)) {
		long r = korin_sys2(SYS_PROC_SETUID, pid, 0);
		journal("elevation", c, pid, r ? "echec du changement d'uid" : "accordee (retenue)");
		return r;
	}
	u64 tok = 0;
	korin_sys2(SYS_GETRANDOM, &tok, sizeof tok);
	tok |= 1;
	/* « jeton|raison|compte|mot de passe demandé (1/0) » */
	static char args[400];
	char t[24];
	dec_to(tok, t);
	char *o = str_cat(str_cat(args, t), "|");
	u64 rl = (u64)str_len(raison);
	if (rl > 200)
		rl = 200;
	for (u64 i = 0; i < rl; i++)
		*o++ = raison[i] == '|' ? '/' : raison[i];
	*o = 0;
	o = str_cat(str_cat(str_cat(o, "|"), c->name), "|");
	o = str_cat(o, empreinte_de(c->name) && empreinte[0] ? "1" : "0");
	long card = ksys(SYS_SPAWN, (long)"/sys/authcard", (long)args, 0, 0, 0, 0);
	if (card < 0) {
		journal("elevation", c, pid, "carte /sys/authcard introuvable");
		return card;
	}
	pending.active = 1;
	pending.done = 0;
	pending.req = cur_req;
	pending.since = korin_sys0(SYS_TIME_MS);
	pending.pid = pid;
	pending.card = card;
	pending.token = tok;
	pending.uid = c->uid;
	copie(pending.name, sizeof pending.name, c->name);
	return DEFER;
}

static long carte(long pid, const char *data)
{
	struct compte c;
	memset(&c, 0, sizeof c);
	copie(c.name, sizeof c.name, pending.name);
	if (!pending.active || pending.done || pid != pending.card || M64(msg, 8) != pending.token)
		return E_PERM;
	if (M64(msg, 16) == 1) {
		journal("elevation", &c, pending.pid, "annulee a la carte");
		finir(E_CANCELED);
		return 0;
	}
	if (!verifier(pending.name, data)) {
		journal("elevation", &c, pending.pid, "mauvais mot de passe");
		korin_sys1(SYS_SLEEP, 1000);
		return E_ACCES;
	}
	long r = korin_sys2(SYS_PROC_SETUID, pending.pid, 0);
	int keep = !r && M64(msg, 24) == 1 && empreinte_de(pending.name) && empreinte[0];
	if (keep)
		retenir(pending.uid, 0);
	journal("elevation", &c, pending.pid, r ? "echec du changement d'uid" : keep ? "accordee, retenue 5 min" : "accordee");
	finir(r);
	return 0;
}

static long traiter(void)
{
	u64 op = M64(msg, 0);
	long pid = (long)M32(msg, 44);
	msg[MSG_SIZE - 1] = 0;
	const char *data = (const char *)msg + MSG_DATA;
	if (op == SVC_OP_END) { /* un process fini : la carte fermée sans réponse vaut un refus */
		long gone = (long)M64(msg, 8);
		if (pending.active && !pending.done && gone == pending.card)
			finir(E_CANCELED);
		else if (pending.active && gone == pending.pid) {
			if (pending.card)
				korin_sys1(SYS_KILL, pending.card); /* (l'app est partie : la carte aussi) */
			pending.active = 0;
			pending.req = 0;
		}
		return DEFER;
	}
	long uid = korin_sys1(SYS_PROC_UID, pid);
	struct compte c;
	memset(&c, 0, sizeof c);
	if (uid < 0)
		return E_PERM;
	if (op == AUTH_CARD)
		return uid == 0 ? carte(pid, data) : E_PERM;
	if (!compte_de(uid, &c)) {
		if (uid == 0) /* (system sans users.ini) */
			copie(c.name, sizeof c.name, "system");
		else
			return E_PERM;
	}
	c.uid = uid;
	switch (op) {
	case AUTH_CAN:
		if (uid != 0 && !peut_elever(&c))
			return 0;
		if (retenu(uid, M64(msg, 8)))
			return 1;
		{
			int e = empreinte_de(c.name);
			return e < 0 || (e > 0 && empreinte[0]) ? 2 : 1; /* (illisible : un mot de passe, refusé) */
		}
	case AUTH_FORGET:
		oublier(uid == 0 ? -1 : uid);
		journal("oubli des elevations retenues", &c, pid, "fait");
		return 0;
	case AUTH_CHECK:
		if (verifier(c.name, data))
			return 0;
		korin_sys1(SYS_SLEEP, 1000);
		return E_ACCES;
	case AUTH_SUDO:
		if (uid != 0 && !peut_elever(&c)) {
			journal("sudo", &c, pid, "refuse (pas dans /etc/elevate.ini)");
			return E_PERM;
		}
		if (uid != 0 && retenu(uid, M64(msg, 8))) {
			journal("sudo", &c, pid, "accorde (retenu)");
			return korin_sys2(SYS_PROC_SETUID, pid, 0);
		}
		if (!verifier(c.name, data)) {
			journal("sudo", &c, pid, "mauvais mot de passe");
			korin_sys1(SYS_SLEEP, 1000);
			return E_ACCES;
		}
		{
			int keep = uid != 0 && M64(msg, 16) == 1 && M64(msg, 8) && empreinte_de(c.name) && empreinte[0];
			if (keep)
				retenir(uid, M64(msg, 8));
			journal("sudo", &c, pid, keep ? "accorde, retenu 5 min" : "accorde");
		}
		return korin_sys2(SYS_PROC_SETUID, pid, 0);
	case AUTH_PASSWD: {
		const char *old = data, *nw = data + str_len(data) + 1;
		if ((const u8 *)nw >= msg + MSG_SIZE)
			return E_INVAL;
		if (!verifier(c.name, old)) {
			journal("passwd", &c, pid, "mauvais mot de passe");
			korin_sys1(SYS_SLEEP, 1000);
			return E_ACCES;
		}
		if (coffre(KV_PASSWD, c.name, old, nw) < 0) { /* (d'abord le coffre : s'il refuse, rien ne change) */
			journal("passwd", &c, pid, "refuse par le coffre");
			return E_IO;
		}
		long r = ecrire_mdp(c.name, nw);
		journal("passwd", &c, pid, r ? "echec de l'ecriture de /etc/shadow.ini" : "change");
		return r;
	}
	case AUTH_ELEVATE:
		return elever(&c, pid, data);
	case AUTH_VERIFY: {
		if (uid != 0)
			return E_PERM;
		const char *who = data, *pw = data + str_len(data) + 1;
		if ((const u8 *)pw >= msg + MSG_SIZE || !nom_ok(who))
			return E_INVAL;
		struct compte t;
		memset(&t, 0, sizeof t);
		copie(t.name, sizeof t.name, who);
		if (verifier(who, pw)) {
			journal("connexion", &t, pid, "accordee");
			if (coffre(KV_UNLOCK, who, pw, 0) < 0)
				journal("connexion", &t, pid, "le coffre ne s'ouvre pas avec ce mot de passe");
			return 0;
		}
		journal("connexion", &t, pid, "mauvais mot de passe");
		korin_sys1(SYS_SLEEP, 1000);
		return E_ACCES;
	}
	case AUTH_HASPW: { /* (ouvert à tous : l'écran de connexion le montre déjà, « Sans mot de passe ») */
		int e = empreinte_de(data);
		return e < 0 ? E_IO : e && empreinte[0] ? 1 : 0;
	}
	case AUTH_SETPW: {
		if (uid != 0)
			return E_PERM;
		const char *who = data, *nw = data + str_len(data) + 1;
		if ((const u8 *)nw >= msg + MSG_SIZE || !nom_ok(who))
			return E_INVAL;
		long r = ecrire_mdp(who, nw);
		if (!r && coffre(KV_SETPW, who, nw, 0) == 1)
			ak_puts("authd : coffre vide (mot de passe remis alors qu'il etait ferme)\n");
		struct compte t;
		memset(&t, 0, sizeof t);
		copie(t.name, sizeof t.name, who);
		journal("passwd (system)", &t, pid, r ? "echec de l'ecriture de /etc/shadow.ini" : "change");
		return r;
	}
	case AUTH_WAIT:
		if (!pending.active || pid != pending.pid || pending.req)
			return E_INVAL;
		if (pending.done) {
			pending.active = 0;
			return pending.result;
		}
		pending.req = cur_req;
		pending.since = korin_sys0(SYS_TIME_MS);
		return DEFER;
	}
	return E_INVAL;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long canal = korin_sys2(SYS_SVC_REGISTER, "auth", 1); /* (SVF_ENDS : la fin de la carte) */
	if (canal < 0) {
		ak_puts("authd : service auth deja pris\n");
		return 1;
	}
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, pending.active ? 1000 : 60000);
		if (req < 0)
			return 0;
		veiller();
		if (!req)
			continue;
		cur_req = req;
		long r = traiter();
		if (r == DEFER)
			continue; /* (la carte répondra ; un avis de fin n'a pas de réponse) */
		memset(msg + MSG_DATA, 0, MSG_SIZE - MSG_DATA); /* (ni mot de passe ni empreinte dans la réponse) */
		M64(msg, 0) = (u64)r;
		M32(msg, 40) = 0;
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
}
