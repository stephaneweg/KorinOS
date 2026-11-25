/* homes.c : le dossier personnel de chaque uid, pour « HOME:/ » (docs/etude/MULTI-UTILISATEUR.md §2.4,
 * étape 3). vfs lit /etc/users.ini (« home = » de chaque compte, relu au plus toutes les 5 s) ; l'uid de
 * l'appelant vient du noyau (pid_uid). L'uid 0 (system : services, init2, Elegant) et un uid sans compte
 * prennent le dossier du compte de la session (autologin de /etc/system.ini, sinon le premier compte d'uid
 * 1000 ou plus) : un service garde ainsi le HOME: qu'il avait. Sans users.ini : « /home », comme avant. */
#include "vfs.h"

#define HOMES_MAX 16
#define HOME_LEN 128
#define RELOAD_MS 5000

static struct {
	long uid;
	char name[32];
	char home[HOME_LEN];
} homes[HOMES_MAX];
static int nhomes;
static long session_uid = -1;
static int mode_acces = ACCESS_LOG;
static u64 loaded_ms;
static int loaded;
static char hbuf[4096];

static int sp(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

/* ligne suivante de *p, coupée en place, sans les espaces des bouts ; 0 à la fin */
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

/* « clé = valeur » -> la clé (coupée en place), *val ; 0 sinon */
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
	u64 n = strlen(s);
	if (n >= cap)
		n = cap - 1;
	memcpy(d, s, n);
	d[n] = 0;
}

static long dec(const char *s)
{
	long n = 0;
	while (*s >= '0' && *s <= '9')
		n = n * 10 + (*s++ - '0');
	return n;
}

static void charger(void)
{
	struct volume *root = volume_de("root", 0);
	nhomes = 0;
	session_uid = -1;
	if (!root)
		return;
	long n = vol_lire(root, "etc/users.ini", hbuf, sizeof hbuf - 1);
	if (n > 0) {
		hbuf[n] = 0;
		char *p = hbuf, *l;
		int cur = -1;
		while ((l = ligne(&p))) {
			if (*l == '[') {
				cur = nhomes < HOMES_MAX ? nhomes++ : -1;
				if (cur >= 0) {
					char *e = l + 1;
					while (*e && *e != ']')
						e++;
					*e = 0;
					homes[cur].uid = -1;
					homes[cur].home[0] = 0;
					copie(homes[cur].name, sizeof homes[cur].name, l + 1);
				}
				continue;
			}
			char *v, *k = cle(l, &v);
			if (!k || cur < 0)
				continue;
			if (!strcmp(k, "uid"))
				homes[cur].uid = dec(v);
			else if (!strcmp(k, "home"))
				copie(homes[cur].home, HOME_LEN, v);
		}
	}
	char autologin[32] = "";
	mode_acces = ACCESS_LOG;
	n = vol_lire(root, "etc/system.ini", hbuf, sizeof hbuf - 1);
	if (n > 0) {
		hbuf[n] = 0;
		char *p = hbuf, *l;
		while ((l = ligne(&p))) {
			char *v, *k = cle(l, &v);
			if (k && !strcmp(k, "autologin"))
				copie(autologin, sizeof autologin, v);
			else if (k && !strcmp(k, "access"))
				mode_acces = !strcmp(v, "enforce") ? ACCESS_ENFORCE : !strcmp(v, "off") ? ACCESS_OFF : ACCESS_LOG;
		}
	}
	for (int i = 0; i < nhomes && session_uid < 0; i++)
		if (autologin[0] && !strcmp(homes[i].name, autologin))
			session_uid = homes[i].uid;
	for (int i = 0; i < nhomes && session_uid < 0; i++)
		if (homes[i].uid >= 1000)
			session_uid = homes[i].uid;
}

/* a et b égaux sur n octets, sans la casse ASCII (FAT32, exFAT) */
int nom_egal_n(const char *a, const char *b, u64 n)
{
	for (u64 i = 0; i < n; i++) {
		char x = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i];
		char y = b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i];
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
	return 1;
}

static const char *home_de(long uid)
{
	for (int i = 0; i < nhomes; i++)
		if (homes[i].uid == uid && homes[i].home[0] == '/')
			return homes[i].home;
	return 0;
}

static void a_jour(void)
{
	u64 now = korin_sys0(SYS_TIME_MS);
	if (!loaded || now - loaded_ms > RELOAD_MS) {
		charger();
		loaded = 1;
		loaded_ms = now;
	}
}

/* « access = » de /etc/system.ini : ACCESS_LOG (défaut), ACCESS_ENFORCE, ACCESS_OFF */
int acces_mode(void)
{
	a_jour();
	return mode_acces;
}

/* le chemin externe est-il dans le dossier personnel d'un autre compte que uid ? */
int home_autre(const char *p, long uid)
{
	a_jour();
	for (int i = 0; i < nhomes; i++) {
		const char *h = homes[i].home;
		u64 n = strlen(h);
		if (homes[i].uid == uid || homes[i].uid <= 0 || h[0] != '/' || n < 2)
			continue; /* (system, et un « / » qui couvrirait tout) */
		if (nom_egal_n(p, h, n) && (p[n] == 0 || p[n] == '/'))
			return 1;
	}
	return 0;
}

/* le compte dont le dossier personnel contient p (le plus long), 0 : aucun */
long home_proprio(const char *p)
{
	a_jour();
	long best = 0;
	u64 bn = 0;
	for (int i = 0; i < nhomes; i++) {
		const char *h = homes[i].home;
		u64 n = strlen(h);
		if (homes[i].uid <= 0 || h[0] != '/' || n < 2 || n <= bn)
			continue;
		if (nom_egal_n(p, h, n) && (p[n] == 0 || p[n] == '/')) {
			best = homes[i].uid;
			bn = n;
		}
	}
	return best;
}

/* l'uid du compte de la session (autologin, sinon le premier), 0 : aucun */
long session_uid_de(void)
{
	a_jour();
	return session_uid > 0 ? session_uid : 0;
}

/* uid -> le dossier de son compte (règles d'accès), 0 : pas de compte (ni l'uid 0, ni un uid inconnu) */
const char *home_compte(long uid)
{
	a_jour();
	return uid > 0 ? home_de(uid) : 0;
}

/* uid -> son dossier personnel, chemin externe (« /home », « /home/stephane ») */
const char *home_externe(long uid)
{
	a_jour();
	const char *h = uid > 0 ? home_de(uid) : 0;
	if (!h && session_uid >= 0)
		h = home_de(session_uid);
	return h ? h : "/home";
}
