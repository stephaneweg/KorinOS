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
 *                (nouveau vide : plus de mot de passe) */
#include "korinrt.h"

#define M64(m, off) (*(u64 *)((u8 *)(m) + (off)))
#define M32(m, off) (*(u32 *)((u8 *)(m) + (off)))

#define AUTH_CAN 1
#define AUTH_SUDO 2
#define AUTH_CHECK 3
#define AUTH_PASSWD 4

#define E_ACCES -13
#define ITERATIONS 20000
#define FILE_MAX 8192

static u8 msg[MSG_SIZE];

/* ---------------------------------------------------------------- SHA-256, HMAC, PBKDF2 (FIPS 180-4, RFC 8018) */

struct sha {
	u32 h[8];
	u8 buf[64];
	u64 len;
	u32 n;
};

static const u32 K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static u32 ror(u32 x, int n)
{
	return (x >> n) | (x << (32 - n));
}

static void sha_block(struct sha *s, const u8 *p)
{
	u32 w[64], a, b, c, d, e, f, g, h;
	for (int i = 0; i < 16; i++)
		w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 64; i++) {
		u32 s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
		u32 s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
	for (int i = 0; i < 64; i++) {
		u32 t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		u32 t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
	}
	s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e, s->h[5] += f, s->h[6] += g, s->h[7] += h;
}

static void sha_init(struct sha *s)
{
	static const u32 iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
				  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	for (int i = 0; i < 8; i++)
		s->h[i] = iv[i];
	s->len = 0;
	s->n = 0;
}

static void sha_add(struct sha *s, const void *data, u64 len)
{
	const u8 *p = data;
	s->len += len;
	while (len--) {
		s->buf[s->n++] = *p++;
		if (s->n == 64) {
			sha_block(s, s->buf);
			s->n = 0;
		}
	}
}

static void sha_end(struct sha *s, u8 out[32])
{
	u64 bits = s->len * 8;
	u8 one = 0x80, zero = 0;
	sha_add(s, &one, 1);
	while (s->n != 56)
		sha_add(s, &zero, 1);
	u8 l[8];
	for (int i = 0; i < 8; i++)
		l[i] = (u8)(bits >> (56 - 8 * i));
	sha_add(s, l, 8);
	for (int i = 0; i < 8; i++) {
		out[4 * i] = (u8)(s->h[i] >> 24);
		out[4 * i + 1] = (u8)(s->h[i] >> 16);
		out[4 * i + 2] = (u8)(s->h[i] >> 8);
		out[4 * i + 3] = (u8)s->h[i];
	}
}

/* HMAC-SHA-256 d'un message en deux morceaux (le sel et le numéro de bloc, pour PBKDF2) */
static void hmac(const u8 *key, u64 klen, const u8 *m1, u64 n1, const u8 *m2, u64 n2, u8 out[32])
{
	u8 k[64], pad[64], t[32];
	struct sha s;
	memset(k, 0, 64);
	if (klen > 64) {
		sha_init(&s);
		sha_add(&s, key, klen);
		sha_end(&s, k);
	} else
		memcpy(k, key, klen);
	for (int i = 0; i < 64; i++)
		pad[i] = k[i] ^ 0x36;
	sha_init(&s);
	sha_add(&s, pad, 64);
	sha_add(&s, m1, n1);
	sha_add(&s, m2, n2);
	sha_end(&s, t);
	for (int i = 0; i < 64; i++)
		pad[i] = k[i] ^ 0x5c;
	sha_init(&s);
	sha_add(&s, pad, 64);
	sha_add(&s, t, 32);
	sha_end(&s, out);
}

/* PBKDF2-HMAC-SHA-256, un bloc de 32 octets */
static void pbkdf2(const char *pw, const u8 *salt, u64 slen, u32 iter, u8 out[32])
{
	u8 u[32], blk[4] = {0, 0, 0, 1};
	u64 plen = (u64)str_len(pw);
	hmac((const u8 *)pw, plen, salt, slen, blk, 4, u);
	memcpy(out, u, 32);
	for (u32 i = 1; i < iter; i++) {
		hmac((const u8 *)pw, plen, u, 32, 0, 0, u);
		for (int j = 0; j < 32; j++)
			out[j] ^= u[j];
	}
}

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

/* l'empreinte du compte -> 1 (dans empreinte ; vide : pas de mot de passe) */
static int empreinte_de(const char *name)
{
	char *p = lire("/etc/shadow.ini"), *l;
	empreinte[0] = 0;
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
	if (!empreinte_de(name) || !empreinte[0])
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

/* le mot de passe du compte devient pw (vide : aucun) : shadow.ini récrit, les autres lignes gardées */
static long ecrire_mdp(const char *name, const char *pw)
{
	char ligne_cpt[200];
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

static long traiter(void)
{
	u64 op = M64(msg, 0);
	long pid = (long)M32(msg, 44);
	msg[MSG_SIZE - 1] = 0;
	const char *data = (const char *)msg + MSG_DATA;
	long uid = korin_sys1(SYS_PROC_UID, pid);
	struct compte c;
	memset(&c, 0, sizeof c);
	if (uid < 0)
		return E_PERM;
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
		return empreinte_de(c.name) && empreinte[0] ? 2 : 1;
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
		if (!verifier(c.name, data)) {
			journal("sudo", &c, pid, "mauvais mot de passe");
			korin_sys1(SYS_SLEEP, 1000);
			return E_ACCES;
		}
		journal("sudo", &c, pid, "accorde");
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
		long r = ecrire_mdp(c.name, nw);
		journal("passwd", &c, pid, r ? "echec de l'ecriture de /etc/shadow.ini" : "change");
		return r;
	}
	}
	return E_INVAL;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long canal = korin_sys2(SYS_SVC_REGISTER, "auth", 0);
	if (canal < 0) {
		ak_puts("authd : service auth deja pris\n");
		return 1;
	}
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, 60000);
		if (req < 0)
			return 0;
		if (!req)
			continue;
		long r = traiter();
		memset(msg + MSG_DATA, 0, MSG_SIZE - MSG_DATA); /* (ni mot de passe ni empreinte dans la réponse) */
		M64(msg, 0) = (u64)r;
		M32(msg, 40) = 0;
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
}
