/* remotepw.c : le mot de passe de l'accès à distance (telnetd, ftpd, vncd,
 * rdpd ; docs/RESEAU.md), traduction de src/inc/remotepw.inc. Un seul pour les
 * quatre (UTF-8, 63 octets au plus). Relu à chaque connexion. Sans mot de passe :
 * toute connexion est refusée. Un service en C l'ajoute par son svc.conf
 * (« sources = ../inc/remotepw.c »). rp_read et rp_equal se partagent rp_pw et
 * leur tampon : un thread à la fois (verrou de l'appelant, comme l'asm).
 *
 * Avec le coffre à secrets (le PC : service keyvault, docs/etude/COFFRE.md) : le
 * mot de passe est le secret « remote-password » de l'espace @system (les
 * services sont d'uid 0 ; Réglages, élevé, l'écrit) ; /etc/remote.txt ne garde
 * que ses autres lignes et « password_vault = 1 » (Réglages sait ainsi qu'un mot
 * de passe est défini, sans le lire). Une ligne « password = ... » encore dans
 * remote.txt (ancien système, écrivain sans coffre) est la plus récente : elle
 * passe dans le coffre, puis le fichier est réécrit sans elle. Sans coffre (le
 * Pi, ou keyvault absent) : « password = ... » dans /etc/remote.txt, comme
 * avant. */
#include "korinrt.h"
#include "remotepw.h"
#define KV_SYSCALL(n, a, b) korin_sys2((n), (a), (b))
#include "../../lib/keyvault/keyvault.h"

#define RP_BUF 1024
#define RP_FILE "/etc/remote.txt"
#define RP_VAULT "remote-password"	/* le secret, dans l'espace @system */
#define RP_MARK "password_vault"	/* la ligne de remote.txt : « dans le coffre » */
#define RP_VAULT_VNC "remote-vnc"	/* le mot de passe de VNC seul (S4), même espace */
#define RP_STATE "/etc/remote-state.ini"	/* lisible par tous (E72) : « password = 1 » quand un secret est défini */

char rp_pw[RP_MAX + 1];
static char rp_buf[RP_BUF];

/* -> 1 si le coffre répond (son programme d'abord : sans lui, pas d'attente de
 * 3 s à chaque connexion pour un service qui n'existe pas, le Pi) */
static int rp_vault(void)
{
	long st[2];
	if (sys_stat("/sys/svc/keyvault", st) < 0)
		return 0;
	return kv_request(KV_STATUS, KVF_SYSTEM, 0, 0, 0) >= 0;
}

/* p = début de ligne, key -> la valeur (après « clé », espaces, « = »,
 * espaces) si la ligne est celle de key, 0 sinon */
static const char *rp_key(const char *p, const char *key)
{
	while (*key && *key == *p) {
		key++;
		p++;
	}
	if (*key)
		return 0;
	while (*p == ' ' || *p == 9) /* espaces, « = », espaces */
		p++;
	if (*p != '=')
		return 0;
	p++;
	while (*p == ' ')
		p++;
	return p;
}

/* p -> la ligne suivante (0 à la fin) */
static const char *rp_next(const char *p)
{
	for (;;) {
		char c = *p++;
		if (!c)
			return 0;
		if (c == 10)
			return p;
	}
}

/* /etc/remote-state.ini (lisible par tous, sans secret : Réglages y voit qu'un mot de passe est défini, remote.txt
 * étant illisible pour un compte en mode enforce) : « password = 1 », ses autres lignes gardées */
static void rp_state_password(void)
{
	static char st[256];
	long fd = sys_open(RP_STATE, O_READ), n = 0;
	if (fd >= 0) {
		n = sys_read(fd, st, sizeof st - 1);
		sys_close(fd);
		if (n < 0)
			n = 0;
	}
	st[n] = 0;
	for (const char *p = st; p && *p; p = rp_next(p))
		if (rp_key(p, "password"))
			return; /* (déjà dit) */
	fd = sys_open(RP_STATE, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return;
	if (n)
		sys_write(fd, st, (u64)n);
	if (n && st[n - 1] != 10)
		sys_write(fd, "\n", 1);
	sys_write(fd, "password = 1\n", 13);
	sys_close(fd);
}

/* remote.txt (dans rp_buf, n octets) réécrit sans sa ligne « password = »
 * (line, jusqu'à end : 0 à la fin du fichier), avec RP_MARK -> 0 ou erreur */
static long rp_strip(long n, const char *line, const char *end)
{
	int mark = 0;
	for (const char *p = rp_buf; p && *p; p = rp_next(p))
		if (rp_key(p, RP_MARK))
			mark = 1;
	long fd = sys_open(RP_FILE, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long r = 0;
	if (line > rp_buf && sys_write(fd, rp_buf, (u64)(line - rp_buf)) < 0)
		r = -1;
	if (end && *end && sys_write(fd, end, (u64)(rp_buf + n - end)) < 0)
		r = -1;
	if (!mark) {
		const char *m = RP_MARK " = 1\n";
		if (end && *end && rp_buf[n - 1] != 10 && sys_write(fd, "\n", 1) < 0)
			r = -1;
		if (sys_write(fd, m, strlen(m)) < 0)
			r = -1;
	}
	sys_close(fd);
	rp_state_password();
	return r;
}

/* -> longueur du mot de passe (0 : aucun), lui dans rp_pw (terminé par 0) (rp_lire) */
long rp_read(void)
{
	rp_pw[0] = 0;
	long n = 0;
	long fd = sys_open(RP_FILE, O_READ);
	if (fd >= 0) {
		n = sys_read(fd, rp_buf, RP_BUF - 1);
		sys_close(fd);
		if (n < 0)
			n = 0;
	}
	rp_buf[n] = 0;
	long len = 0;
	const char *line = 0, *q = 0;
	for (const char *p = rp_buf; p && *p; p = rp_next(p)) /* une ligne « password = ... » */
		if ((q = rp_key(p, "password"))) {
			line = p;
			break;
		}
	if (line) {
		while (q[len] && q[len] != 10 && q[len] != 13 && len < RP_MAX) { /* la valeur, jusqu'à la fin de ligne */
			rp_pw[len] = q[len];
			len++;
		}
		rp_pw[len] = 0;
		while (len && rp_pw[len - 1] == ' ') /* (espaces de fin retirées) */
			rp_pw[--len] = 0;
	}
	if (rp_vault()) {
		if (len) { /* encore en clair : dans le coffre, puis retiré du fichier */
			if (kv_sys_set(RP_VAULT, rp_pw, len) >= 0 && n < RP_BUF - 1) /* (fichier lu en entier) */
				rp_strip(n, line, rp_next(line));
		} else {
			long r = kv_sys_get(RP_VAULT, rp_pw, RP_MAX);
			len = r > 0 ? r : 0; /* (absent ou refusé : aucun, connexions refusées) */
			rp_pw[len] = 0;
		}
	}
	memset(rp_buf, 0, sizeof rp_buf);
	return len;
}

/* ---- échecs (S3, E70) : après 5 mots de passe faux, plus d'essai pendant 30 s, puis le double à chaque
 * nouvel échec (dix minutes au plus : un tiers du réseau ne ferme pas le service plus longtemps) ; un bon remet
 * à zéro. Pour tout le service (toutes ses connexions) */
static long rp_fails;
static u64 rp_until;

int rp_locked(void)
{
	return (u64)korin_sys0(SYS_TIME_MS) < rp_until;
}

static void rp_result(int ok)
{
	if (ok) {
		rp_fails = 0;
		rp_until = 0;
		return;
	}
	long f = ++rp_fails;
	if (f >= 5) {
		u64 wait = 30000ull << (f - 5 < 7 ? f - 5 : 7);
		if (wait > 600000ull)
			wait = 600000ull;
		rp_until = (u64)korin_sys0(SYS_TIME_MS) + wait;
	}
}

void rp_note(int ok)
{
	rp_result(ok);
}

/* saisie, sa longueur -> 1 si c'est le mot de passe de rp_pw (rp_read avant) ;
 * même durée quel que soit le premier octet faux (rp_egal) */
int rp_equal(const void *in, u64 len)
{
	const u8 *s = in;
	u64 l = 0;
	while (rp_pw[l])
		l++;
	if (!l || rp_locked())
		return 0; /* aucun mot de passe, ou trop d'échecs : refusé */
	u64 diff = l ^ len; /* longueurs différentes -> non nul */
	for (u64 i = 0; i < l; i++)
		diff |= (u8)rp_pw[i] ^ (i < len ? s[i] : 0);
	rp_result(diff == 0);
	return diff == 0;
}

/* ---- les comptes (authd) */

#define RP_AUTH_VERIFY 9
#define RP_AUTH_HASPW 10
static u8 rp_am[512] __attribute__((aligned(16)));

/* authd : op, données (deux chaînes au plus) -> résultat */
static long rp_auth(long op, const char *a, const char *b)
{
	long ch = korin_sys2(SYS_SVC_OPEN, "auth", 2000);
	if (ch < 0)
		return ch;
	memset(rp_am, 0, sizeof rp_am);
	*(u64 *)rp_am = (u64)op;
	u64 k = 48;
	for (const char *s = a; *s && k < 48 + 64; s++)
		rp_am[k++] = (u8)*s;
	rp_am[k++] = 0;
	for (const char *s = b; s && *s && k < 500; s++)
		rp_am[k++] = (u8)*s;
	rp_am[k++] = 0;
	*(u32 *)(rp_am + 40) = (u32)(k - 48);
	long r = korin_sys2(SYS_SVC_CALL, ch, rp_am);
	memset(rp_am, 0, sizeof rp_am);
	return r; /* (un numéro de service, pas une poignée : rien à fermer) */
}

/* /etc/users.ini : pour chaque compte (section, uid, home), fn -> 1 : arrêter */
static long rp_users(int (*fn)(const char *name, long uid, const char *home, void *arg), void *arg)
{
	long fd = sys_open("/etc/users.ini", O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, rp_buf, RP_BUF - 1);
	sys_close(fd);
	if (n <= 0)
		return 0;
	rp_buf[n] = 0;
	char name[64] = "", home[160] = "";
	long uid = -1;
	const char *p = rp_buf;
	for (;;) {
		const char *e = p;
		while (*e && *e != 10)
			e++;
		int sect = *p == '[' || !*p;
		if (sect && name[0] && uid >= 0 && fn(name, uid, home, arg))
			return 1;
		if (*p == '[') {
			u64 k = 0;
			for (const char *c = p + 1; c < e && *c != ']' && k < sizeof name - 1; c++)
				name[k++] = *c;
			name[k] = 0;
			uid = -1;
			home[0] = 0;
		} else {
			const char *v = rp_key(p, "uid");
			if (v) {
				uid = 0;
				while (*v >= '0' && *v <= '9')
					uid = uid * 10 + (*v++ - '0');
			} else if ((v = rp_key(p, "home"))) {
				u64 k = 0;
				while (v + k < e && v[k] != 13 && k < sizeof home - 1) {
					home[k] = v[k];
					k++;
				}
				home[k] = 0;
			}
		}
		if (!*e) /* la fin : la dernière section */
			return name[0] && uid >= 0 && fn(name, uid, home, arg);
		p = e + 1;
	}
}

static int rp_has_pw(const char *name, long uid, const char *home, void *arg)
{
	(void)home;
	(void)arg;
	return uid >= 1000 && rp_auth(RP_AUTH_HASPW, name, 0) == 1;
}

int rp_accounts(void)
{
	long fd = sys_open(RP_FILE, O_READ);
	if (fd >= 0) { /* « shared = 1 » : le secret commun, même avec des comptes */
		long n = sys_read(fd, rp_buf, RP_BUF - 1);
		sys_close(fd);
		rp_buf[n > 0 ? n : 0] = 0;
		for (const char *p = rp_buf; p && *p; p = rp_next(p)) {
			const char *v = rp_key(p, "shared");
			if (v && *v == '1')
				return 0;
		}
	}
	return rp_users(rp_has_pw, 0) == 1;
}

struct rp_find {
	const char *name;
	long uid;
	char *home;
	u64 cap;
};

static int rp_find_fn(const char *name, long uid, const char *home, void *arg)
{
	struct rp_find *f = arg;
	if (strcmp(name, f->name))
		return 0;
	f->uid = uid;
	u64 k = 0;
	while (home[k] && k < f->cap - 1) {
		f->home[k] = home[k];
		k++;
	}
	f->home[k] = 0;
	return 1;
}

long rp_account(const char *name, const char *pw, char *home, u64 cap)
{
	if (rp_locked())
		return -1;
	struct rp_find f = {name, -1, home, cap};
	home[0] = 0;
	/* un compte de personne (uid >= 1000) qui a un mot de passe : jamais system, jamais sans mot de passe */
	int ok = name[0] && rp_users(rp_find_fn, &f) == 1 && f.uid >= 1000 && home[0] == '/' &&
		 rp_auth(RP_AUTH_HASPW, name, 0) == 1 && rp_auth(RP_AUTH_VERIFY, name, pw) == 0;
	rp_result(ok);
	return ok ? f.uid : -1;
}

/* ---- VNC (S4) */

long rp_read_vnc(void)
{
	rp_pw[0] = 0;
	if (rp_vault()) {
		long r = kv_sys_get(RP_VAULT_VNC, rp_pw, RP_MAX);
		if (r > 0) {
			rp_pw[r] = 0;
			return r;
		}
	}
	if (rp_accounts())
		return 0; /* (mode comptes sans mot de passe VNC : refusé) */
	return rp_read();
}
