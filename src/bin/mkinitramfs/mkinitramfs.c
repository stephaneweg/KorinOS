/* mkinitramfs - les modules de démarrage (docs/MICRO-NOYAU.md, section 4), comme
 * mkinitramfs.asm : « mkinitramfs [liste] » (la ligne d'arguments brute du noyau, krt_line, est
 * le chemin de la liste, espaces comprises ; par défaut SD0:/etc/initramfs.txt : un fichier par
 * ligne, # : commentaire, blancs autour retirés). Pour chaque fichier : l'enveloppe de module
 * (64 octets : « ARMPIMOD », taille en-tête compris, le nom après le dernier « / » sur 48 octets)
 * puis le contenu (4 Mo au plus, le reste ignoré), écrite à la racine de SD0: (<nom>.mod : le nom
 * coupé au premier point, 40 caractères au plus ; s'il est déjà pris, les points deviennent des
 * « _ », sans autre essai). 16 modules au plus : le 17e est une erreur. La première erreur arrête
 * tout (code 1, config.txt pas touché). Puis la ligne « initramfs » de SD0:/config.txt est refaite
 * (l'adresse gardee si son dernier mot commence par 0 ; sans ligne : ajoutée à la fin avec
 * 0x02000000, collée à une dernière ligne sans fin, comme l'asm). Sortie sans tampon (puts). */
#include "korinrt.h"

#define MOD_MAX 16
#define NOM_MAX 48 /* nom dans l'en-tête (0 compris) */
#define FICHIER_MAX (4 * 1024 * 1024)
#define LISTE_MAX 4096
#define CONFIG_MAX 16384

static u8 *buf;
static long mods_n;
static char mods[MOD_MAX][64]; /* noms des fichiers écrits */
static char file_name[64];
static char addr_read[64]; /* (32 dans l'asm) */
static char list_buf[LISTE_MAX];
static char config_in[CONFIG_MAX];
static char config_out[CONFIG_MAX + 1024];

/* chemin, tampon, taille -> octets lus, < 0 si erreur */
static long read_file(const char *path, void *b, long size)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return fd;
	long n = 0;
	while (n < size) {
		long r = sys_read(fd, (u8 *)b + n, (u64)(size - n));
		if (r <= 0)
			break;
		n += r;
	}
	sys_close(fd);
	return n;
}

/* chaîne : sans espaces ni tabulations devant, sans blancs (<= ' ') derrière */
static char *trim(char *s)
{
	while (*s == ' ' || *s == 9)
		s++;
	char *e = s;
	while (*e)
		e++;
	while (e > s && (u8)e[-1] <= ' ')
		*--e = 0;
	return s;
}

/* -> 1 si « file_name.mod » est déjà le fichier d'un module précédent */
static int taken(void)
{
	char t[64];
	str_cat(str_cat(t, file_name), ".mod");
	for (long i = 0; i < mods_n; i++)
		if (str_eq(mods[i], t))
			return 1;
	return 0;
}

/* nom du module -> file_name = « <nom>.mod » */
static void mod_name(const char *name)
{
	char *o = file_name;
	const char *s = name;
	for (int n = 40; n; n--) {
		char c = *s++;
		if (!c || c == '.')
			break;
		*o++ = c;
	}
	*o = 0;
	if (taken()) { /* pris : le nom entier, « . » -> « _ » */
		o = file_name;
		s = name;
		for (int n = 40; n; n--) {
			char c = *s++;
			if (!c)
				break;
			*o++ = c == '.' ? '_' : c;
		}
		*o = 0;
	}
	str_cat(file_name + str_len(file_name), ".mod");
}

static long fail(const char *msg, const char *path)
{
	lang_puts(msg);
	ak_puts(path);
	ak_puts("\n");
	return -1;
}

/* chemin d'un fichier : son module à la racine de SD0: -> 0, < 0 */
static long module(const char *path)
{
	if (mods_n >= MOD_MAX)
		return fail("mkinitramfs : trop de modules, ignore : ", path);
	const char *name = path; /* le nom : après le dernier « / » */
	for (const char *p = path; *p;)
		if (*p++ == '/')
			name = p;
	memset(buf, 0, 64); /* en-tête : « ARMPIMOD », taille, nom */
	memcpy(buf, "ARMPIMOD", 8);
	char *o = (char *)buf + 16;
	const char *s = name;
	for (int n = NOM_MAX - 1; n; n--) {
		char c = *s++;
		if (!c)
			break;
		*o++ = c;
	}
	long n = read_file(path, buf + 64, FICHIER_MAX);
	if (n < 0)
		return fail("mkinitramfs : fichier illisible : ", path);
	u64 total = (u64)n + 64;
	*(u64 *)(buf + 8) = total;
	mod_name(name);
	char where[128];
	str_cat(str_cat(where, "SD0:/"), file_name);
	long fd = sys_open(where, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fail("mkinitramfs : ecriture impossible : ", path);
	long w = sys_write(fd, buf, total);
	sys_close(fd);
	if (w != (long)total)
		return fail("mkinitramfs : ecriture impossible : ", path);
	lang_puts("mkinitramfs : "); /* « mkinitramfs : SD0:/sys/svc/sd -> sd.mod (4 Ko) » */
	ak_puts(path);
	ak_puts(" -> ");
	ak_puts(file_name);
	ak_puts(" (");
	print_dec((long)((total + 1023) >> 10));
	lang_puts(" Ko)\n");
	char *d = mods[mods_n++]; /* retenu pour config.txt (63 caractères au plus) */
	s = file_name;
	for (int k = 63; k; k--)
		if (!(*d++ = *s++))
			return 0;
	*d = 0;
	return 0;
}

/* destination, adresse : « initramfs a.mod,b.mod <adresse> » CR LF -> fin */
static char *initramfs_line(char *o, const char *addr)
{
	o = str_cat(o, "initramfs");
	*o++ = ' ';
	for (long i = 0;;) {
		o = str_cat(o, mods[i]);
		if (++i == mods_n)
			break;
		*o++ = ',';
	}
	*o++ = ' ';
	o = str_cat(o, addr);
	*o++ = 13;
	*o++ = 10;
	return o;
}

/* début, fin de ligne : le dernier mot, s'il commence par 0 (« 0x... »), sinon 0x02000000 */
static const char *address(const char *l, const char *end)
{
	const char *e = end - 1; /* sans CR LF ni espaces de fin */
	for (;;) {
		if (e < l)
			return "0x02000000";
		if ((u8)*e > ' ')
			break;
		e--;
	}
	const char *b = e;
	while (b > l && (u8)b[-1] > ' ')
		b--;
	if (*b != '0')
		return "0x02000000";
	char *o = addr_read;
	while (b <= e)
		*o++ = *b++;
	*o = 0;
	return addr_read;
}

/* texte, mot -> 1 si le texte commence par le mot suivi d'une espace */
static int starts(const char *s, const char *w)
{
	while (*w)
		if (*w++ != *s++)
			return 0;
	return *s == ' ';
}

/* SD0:/config.txt : la ligne « initramfs » refaite -> 0, < 0 */
static long config(void)
{
	long n = read_file("SD0:/config.txt", config_in, CONFIG_MAX - 1);
	if (n < 0)
		return n;
	config_in[n] = 0;
	char *o = config_out;
	const char *l = config_in;
	int found = 0;
	while (*l) {
		const char *e = l; /* fin de ligne (CR LF compris) */
		while (*e)
			if (*e++ == 10)
				break;
		if (starts(l, "initramfs")) {
			if (!found) { /* (une seule) */
				found = 1;
				o = initramfs_line(o, address(l, e));
			}
		} else {
			do
				*o++ = *l++;
			while (l < e);
		}
		l = e;
	}
	if (!found) /* pas de ligne : à la fin */
		o = initramfs_line(o, "0x02000000");
	long fd = sys_open("SD0:/config.txt", O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long w = sys_write(fd, config_out, (u64)(o - config_out));
	sys_close(fd);
	if (w < 0)
		return w;
	lang_puts("mkinitramfs : SD0:/config.txt a jour (ligne initramfs)\n");
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *lp = "SD0:/etc/initramfs.txt"; /* la liste */
	if (krt_line && *krt_line)
		lp = krt_line;
	long n = read_file(lp, list_buf, LISTE_MAX - 1);
	if (n < 0) {
		lang_puts("mkinitramfs : liste illisible : ");
		ak_puts(lp);
		ak_puts("\n");
		_exit(1);
	}
	list_buf[n] = 0;
	buf = ak_malloc(FICHIER_MAX + 64); /* tampon d'un module */
	if (!buf)
		_exit(1);
	char *l = list_buf; /* une ligne par fichier */
	while (*l) {
		char *e = l; /* fin de ligne -> 0 */
		while (*e && *e != 10 && *e != 13)
			e++;
		if (*e)
			*e++ = 0;
		char *s = trim(l);
		if (*s && *s != '#' && module(s) < 0)
			_exit(1);
		l = e;
	}
	if (!mods_n) {
		lang_puts("mkinitramfs : liste vide, config.txt inchange\n");
		_exit(1);
	}
	if (config() < 0) {
		lang_puts("mkinitramfs : SD0:/config.txt illisible ou non ecrit\n");
		_exit(1);
	}
	lang_puts("mkinitramfs : pret pour le prochain demarrage\n");
	_exit(0);
}
