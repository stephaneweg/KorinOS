/* exec.c : le bit d'exécution, traduction de exec.inc (docs/FICHIERS.md, « Le
 * bit d'exécution »). FAT32 et exFAT n'ont pas de bit x :
 *  * un fichier est exécutable par son chemin : sous /bin, /sys, un
 *    /apps/<nom>.app/main, ou sous un dossier de /etc/exec.txt (absent :
 *    exd_defaut) ; lu une fois, à la première question ;
 *  * sinon par l'index de son volume : <VOL>:/.exec (sous son rootpath ; deux
 *    volumes d'un même service et d'une même racine en partagent un : vol_index),
 *    un chemin relatif par
 *    ligne, lu à la première question sur ce volume puis tenu en mémoire
 *    (IDX_CAP octets, jamais rendus) ; /tmp (ramfs) en mémoire seulement ;
 *    casse ignorée, sauf sur ramfs ;
 *  * V_EXEC : un script (ni ELF ni MZ) est lancé par l'interprète de sa ligne
 *    #! (avec son argument), sinon /bin/sh (path.c).
 * Tout passe par msg2 : msg tient la requête en cours (et la réponse de stat). */
#include "vfs.h"

#define IDX_CAP 32768 /* octets de l'index d'un volume */
#define IDX_RETRY_MS 30000 /* un index illisible (service lent, réseau absent) : pas relu avant 30 s */
#define EXD_MAX 16    /* dossiers de exec.txt */
#define EXD_TAMPON 4096

static u64 exd_lu;
static char exd_v[EXD_MAX][PATH_MAX]; /* dossiers d'exécutables (internes, minuscules) */
char exd_ligne[PATH_MAX];
static char exd_tampon[EXD_TAMPON];

/* dossiers d'exécutables sans /etc/exec.txt (le même que celui de la carte) */
static const char exd_defaut[] = "/devtools/gcc/bin\n/gcc/libexec\n/gcc/aarch64-onyx-elf/bin\n"
				 "/dotnet\n/apps/koton.app/plugins\n/devtools/rust/bin\n";

static long appel2(struct volume *v)
{
	return korin_sys2(SYS_SVC_CALL, v->chan, &msg2);
}

/* libre = 1 : le verrou global relâché pendant l'attente (vol_appel_libre) */
static long appel2l(struct volume *v, int libre)
{
	return libre ? vol_appel_libre(v, &msg2) : appel2(v);
}

/* msg2 préparé (d = 0, sans données) */
static void msg2_op(long op, long a, long b, long c)
{
	msg2.op = op;
	msg2.a = a;
	msg2.b = b;
	msg2.c = c;
	msg2.d = 0;
	msg2.len = 0;
}

/* volume, chemin relatif à sa racine -> msg2.data : le chemin pour son service
 * (rootpath devant, vol_chemin ; coupé s'il ne tient pas) */
static void msg2_chemin(struct volume *v, const char *rel)
{
	u64 n = vol_chemin(v, rel, msg2.data, FS_DATA_MAX);
	if (!n) {
		msg2.data[FS_DATA_MAX - 1] = 0;
		n = FS_DATA_MAX;
	}
	msg2.len = (u32)n;
}

/* volume, chemin relatif, tampon, octets au plus -> octets lus, ou erreur */
static long vol_lire_fichier(struct volume *v, const char *rel, void *buf, u64 max, int libre)
{
	msg2_op(FS_OPEN, O_READ, 0, 0);
	msg2_chemin(v, rel);
	long r = appel2l(v, libre);
	if (r < 0)
		return r;
	long num = msg2.b;
	u64 pos = 0;
	while (max - pos) {
		u64 n = max - pos > FS_DATA_MAX ? FS_DATA_MAX : max - pos;
		msg2_op(FS_READ, num, pos, n);
		r = appel2l(v, libre);
		if (r <= 0)
			break;
		if ((u64)r > max - pos) /* (jamais plus que demandé) */
			r = max - pos;
		memcpy((u8 *)buf + pos, msg2.data, r);
		pos += r;
	}
	msg2_op(FS_CLOSE, num, 0, 0);
	appel2l(v, libre);
	return pos;
}

/* (vfs.c : /etc/mounts.txt pour /boot) */
long vol_lire(struct volume *v, const char *rel, void *buf, u64 max)
{
	return vol_lire_fichier(v, rel, buf, max, 0);
}

/* le fichier est récrit -> 0 ou erreur */
static long vol_ecrire_fichier(struct volume *v, const char *rel, const void *buf, u64 len)
{
	msg2_op(FS_OPEN, O_WRITE | O_CREATE | O_TRUNC, 0, 0);
	msg2_chemin(v, rel);
	nc_forget(v, (char *)msg2.data); /* (le cache des noms) */
	long r = appel2(v);
	if (r < 0)
		return r;
	long num = msg2.b;
	u64 pos = 0;
	long res = 0;
	while (len - pos) {
		u64 n = len - pos > FS_DATA_MAX ? FS_DATA_MAX : len - pos;
		msg2_op(FS_WRITE, num, pos, n);
		msg2.len = (u32)n;
		memcpy(msg2.data, (const u8 *)buf + pos, n);
		r = appel2(v);
		if (r <= 0) {
			res = E_NOSPC;
			break;
		}
		pos += r;
	}
	msg2_op(FS_CLOSE, num, 0, 0);
	appel2(v);
	return res;
}

/* chemin, préfixe (minuscules) -> la suite si le chemin commence par le préfixe
 * (casse ignorée), 0 sinon */
static const char *prefixe_ci(const char *p, const char *pre)
{
	for (; *pre; pre++) {
		char c = *p++;
		if (c >= 'A' && c <= 'Z')
			c += 32;
		if (c != *pre)
			return 0;
	}
	return p;
}

static void exd_charger(void);

/* chemin interne -> 1 s'il est exécutable par son chemin (règles) */
long exec_regle(const char *path)
{
	const char *r = prefixe_ci(path, sp_bin); /* /bin/... */
	if (!r)
		r = prefixe_ci(path, sp_sbin); /* /sbin/... */
	if (!r)
		r = prefixe_ci(path, chemin_sys); /* /sys/... */
	if (r) {
		if (*r) /* (un fichier dedans, pas le dossier) */
			return 1;
	} else if ((r = prefixe_ci(path, sp_apps))) { /* /apps/<nom>.app/main */
		const char *s = r;
		while (*s && *s != '/')
			s++;
		if (*s && s - r >= 5) {
			const char *t = prefixe_ci(s - 4, sp_main);
			if (t && !*t)
				return 1;
		}
	}
	{ /* HOME:/apps/<nom>.app/main : les apps que l'utilisateur crée (qbasic, qbstudio), dans « /home/apps » ou
	   * « /home/<compte>/apps » (pas /home/public : écrit par tous) ; les règles d'accès gardent le dossier des
	   * autres comptes (décision du 25/11) */
		char ext[PATH_MAX];
		chemin_externe(path, ext);
		const char *h = prefixe_ci(ext, "/home/");
		if (h) {
			const char *a = prefixe_ci(h, "apps/");
			if (!a && !prefixe_ci(h, "public/")) {
				while (*h && *h != '/')
					h++;
				if (*h == '/')
					a = prefixe_ci(h + 1, "apps/");
			}
			if (a) {
				const char *s = a;
				while (*s && *s != '/')
					s++;
				if (*s && s - a >= 5) {
					const char *t = prefixe_ci(s - 4, sp_main);
					if (t && !*t)
						return 1;
				}
			}
		}
	}
	exd_charger(); /* les dossiers de exec.txt */
	for (int i = 0; i < EXD_MAX && exd_v[i][0]; i++) {
		const char *t = prefixe_ci(path, exd_v[i]);
		if (t && t[0] == '/' && t[1])
			return 1;
	}
	return 0;
}

/* texte de exec.txt (« # » : commentaire ; un dossier par ligne, « /a » ou
 * « /a » : chemin virtuel) -> exd_v (chemins internes, minuscules) */
static void exd_analyser(const char *p)
{
	for (int i = 0; i < EXD_MAX; i++)
		exd_v[i][0] = 0; /* table vide */
	long n = 0;
	for (;;) {
		char c = *p;
		if (!c)
			return;
		if (c == ' ' || c == 9 || c == 10 || c == 13) {
			p++;
			continue;
		}
		if (c == '#') {
			while (*p && *p++ != 10)
				;
			continue;
		}
		u64 k = 0; /* le mot, jusqu'à la fin de la ligne */
		while (*p && *p != 10 && *p != 13) {
			if (k < PATH_MAX - 1)
				exd_ligne[k++] = *p;
			p++;
		}
		while (k && (exd_ligne[k - 1] == ' ' || exd_ligne[k - 1] == 9)) /* (blancs de fin retirés) */
			k--;
		if (!k)
			continue;
		exd_ligne[k] = 0;
		if (n >= EXD_MAX)
			continue;
		char *e = exd_v[n]; /* résolu comme un chemin de programme (pid 0 : dossier /root) */
		if (resoudre(0, exd_ligne, e)) {
			e[0] = 0;
			continue;
		}
		for (; *e; e++) /* en minuscules */
			if (*e >= 'A' && *e <= 'Z')
				*e += 32;
		n++;
	}
}

/* /etc/exec.txt dans exd_v (une fois ; / absent : les valeurs par défaut,
 * le fichier sera lu plus tard) */
static void exd_charger(void)
{
	if (exd_lu)
		return;
	struct volume *v = volume_de("root", 0);
	if (!v) {
		exd_analyser(exd_defaut);
		return;
	}
	long r = vol_lire_fichier(v, "etc/exec.txt", exd_tampon, EXD_TAMPON - 1, 0);
	exd_lu = 1;
	if (r < 0) { /* (absent : les valeurs par défaut) */
		exd_analyser(exd_defaut);
		return;
	}
	exd_tampon[r] = 0;
	exd_analyser(exd_tampon);
}

/* -> 1 si son service est ramfs (mémoire) */
static long vol_ramfs(struct volume *v)
{
	return !strcmp(v->fs, "ramfs");
}

/* -> 0 : son index est en mémoire, ou erreur */
long idx_charger(struct volume *v)
{
	if (v->idxok)
		return 0;
	if (v->idxbusy) /* un autre thread le lit : vu comme vide pour cette question */
		return E_BUSY;
	if (v->idxfail && korin_sys0(SYS_TIME_MS) - v->idxfail < IDX_RETRY_MS)
		return E_AGAIN; /* (illisible il y a peu : pas d'autre attente du service) */
	if (!v->idx) {
		v->idx = ak_malloc(IDX_CAP);
		if (!v->idx)
			return E_NOMEM;
	}
	v->idxn = 0;
	if (!vol_ramfs(v)) {
		long ch = v->chan, pid = v->pid;
		v->idxbusy = 1; /* lu verrou relâché : un service lent ne bloque pas vfs */
		long r = vol_lire_fichier(v, ".exec", v->idx, IDX_CAP - 2, 1);
		v->idxbusy = 0;
		if (v->chan != ch || v->pid != pid || !v->name[0]) /* démonté pendant la lecture */
			return E_NOENT;
		if (r != E_NOENT) { /* E_NOENT : vide */
			if (r < 0) {
				v->idxfail = korin_sys0(SYS_TIME_MS) | 1;
				return r;
			}
			/* nettoyé : sans \r, ni ligne vide, ni « / » en tête ; \n final */
			char *b = v->idx;
			u64 w = 0;
			int bol = 1; /* début de ligne */
			for (long i = 0; i < r; i++) {
				char c = b[i];
				if (c == 13)
					continue;
				if (c == 10) {
					if (bol) /* (ligne vide) */
						continue;
					b[w++] = c;
					bol = 1;
					continue;
				}
				if (bol && (c == '/' || c == ' '))
					continue;
				bol = 0;
				b[w++] = c;
			}
			if (!bol)
				b[w++] = 10;
			v->idxn = w;
		}
	}
	v->idxfail = 0;
	v->idxok = 1;
	return 0;
}

/* volume (index chargé), chemin relatif, sub = 1 : aussi les lignes sous ce
 * chemin (« chemin/... ») -> début de la première ligne trouvée (-1 : aucune),
 * *len sa longueur (sans \n) */
long idx_cherche(struct volume *v, const char *rel, int sub, u64 *len)
{
	int ci = !vol_ramfs(v); /* casse ignorée */
	const char *b = v->idx;
	u64 n = v->idxn, l = 0;
	while (l < n) {
		u64 k = 0;
		for (;;) {
			char c1 = rel[k];
			char c2 = l + k < n ? b[l + k] : 10;
			if (!c1) {
				if (c2 == 10 || (sub && c2 == '/' && k)) { /* ligne = chemin, ou chemin/... */
					u64 e = l + k;
					while (e < n && b[e] != 10)
						e++;
					*len = e - l;
					return l;
				}
				break;
			}
			if (c2 == 10)
				break;
			if (ci) {
				if (c1 >= 'A' && c1 <= 'Z')
					c1 += 32;
				if (c2 >= 'A' && c2 <= 'Z')
					c2 += 32;
			}
			if (c1 != c2)
				break;
			k++;
		}
		u64 x = l + k; /* ligne suivante */
		for (;;) {
			if (x >= n) {
				*len = 0;
				return -1;
			}
			if (b[x++] == 10)
				break;
		}
		l = x;
	}
	*len = 0;
	return -1;
}

/* [début, début + longueur + 1[ retiré de l'index */
static void idx_cut(struct volume *v, u64 s, u64 len)
{
	u64 n = v->idxn;
	u64 e = s + len + 1;
	if (e > n)
		e = n;
	u64 tail = n - e;
	v->idxn = n - (e - s);
	if (tail)
		memmove(v->idx + s, v->idx + e, tail);
}

/* -> lignes retirées */
static long idx_retirer(struct volume *v, const char *rel, int sub)
{
	long c = 0;
	for (;;) {
		u64 len;
		long s = idx_cherche(v, rel, sub, &len);
		if (s < 0)
			return c;
		idx_cut(v, s, len);
		c++;
	}
}

/* -> 1 ajouté, 0 déjà là, E_NOSPC (index plein) */
static long idx_ajouter(struct volume *v, const char *rel)
{
	u64 len;
	if (idx_cherche(v, rel, 0, &len) >= 0)
		return 0;
	u64 n = strlen(rel);
	if (!n)
		return 0;
	u64 t = v->idxn + n + 1;
	if (t > IDX_CAP)
		return E_NOSPC;
	memcpy(v->idx + v->idxn, rel, n);
	v->idx[v->idxn + n] = 10;
	v->idxn = t;
	return 1;
}

/* index de l'ancien chemin relatif, ancien, index du nouveau, nouveau -> entrées
 * déplacées (l'ancien et ce qu'il contient ; une entrée du nouveau chemin est
 * d'abord retirée). Deux index : /boot et / à rootpath, un même service */
long idx_renommer(struct volume *vo, const char *old, struct volume *v, const char *new)
{
	long c = 0;
	if (vo == v && !vol_ramfs(v)) { /* même chemin, à la casse près : rien (comme l'asm : bit 0x20 ignoré) */
		const u8 *a = (const u8 *)old, *b = (const u8 *)new;
		for (;;) {
			u8 x = *a++, y = *b++;
			if ((x | 0x20) != (y | 0x20))
				break;
			if (!x)
				return 0;
		}
	}
	idx_retirer(v, new, 0); /* la cible remplacée */
	for (u64 bound = vo->idxn; bound; bound--) { /* (borne : une ligne fait au moins 2 o) */
		u64 len;
		long s = idx_cherche(vo, old, 1, &len);
		if (s < 0)
			break;
		u64 lo = strlen(old), k = 0; /* nouveau + la suite de la ligne après l'ancien */
		for (const char *p = new; *p; p++) {
			exd_ligne[k++] = *p;
			if (k >= PATH_MAX - 1)
				return c;
		}
		for (u64 i = lo; i < len; i++) {
			exd_ligne[k++] = vo->idx[s + i];
			if (k >= PATH_MAX - 1)
				return c;
		}
		exd_ligne[k] = 0;
		idx_cut(vo, s, len); /* la ligne retirée... */
		idx_ajouter(v, exd_ligne); /* ... et la nouvelle ajoutée */
		c++;
	}
	return c;
}

/* son index récrit dans <VOL>:/.exec (ramfs : rien) -> 0 ou erreur */
long idx_ecrire(struct volume *v)
{
	if (vol_ramfs(v))
		return 0;
	return vol_ecrire_fichier(v, ".exec", v->idx, v->idxn);
}

/* chemin interne -> 1 s'il est exécutable (règles, puis index) */
long est_executable(const char *path)
{
	if (exec_regle(path))
		return 1;
	const char *rest;
	struct volume *v = volume_de(path + 1, &rest);
	if (!v)
		return 0;
	v = vol_index(v);
	if (idx_charger(v))
		return 0;
	u64 len;
	return idx_cherche(v, rest, 0, &len) >= 0;
}

/* resolu va être effacé (V_UNLINK) : ses entrées, et celles de ce qu'il
 * contient, quittent l'index */
void idx_oublier(void)
{
	const char *rest;
	struct volume *v = volume_de(resolu + 1, &rest);
	if (!v)
		return;
	v = vol_index(v);
	if (idx_charger(v))
		return;
	if (idx_retirer(v, rest, 1))
		idx_ecrire(v);
}

/* (V_CHMOD) resolu, mode : un bit x l'ajoute à l'index, aucun l'en retire
 * (chemin des règles : rien) -> 0 ou erreur */
long idx_chmod(u64 mode)
{
	if (exec_regle(resolu))
		return 0;
	const char *rest;
	struct volume *v = volume_de(resolu + 1, &rest);
	if (!v)
		return E_NOENT;
	v = vol_index(v);
	long r = idx_charger(v);
	if (r)
		return r;
	if (mode & 0111) {
		r = idx_ajouter(v, rest);
		if (r <= 0)
			return r;
	} else if (!idx_retirer(v, rest, 0)) {
		return 0;
	}
	return idx_ecrire(v);
}

/* resolu (existe, fichier) : ses premiers octets -> 0 : un programme (ELF, MZ) ;
 * 1 : un script, interp et iarg remplis (ligne #!, sinon « /bin/sh ») ; < 0 : erreur */
long script_voir(void)
{
	const char *rest;
	struct volume *v = volume_de(resolu + 1, &rest);
	if (!v)
		return E_NOENT;
	long n = vol_lire_fichier(v, rest, tete, TETE, 1);
	if (n < 0)
		return n;
	const u8 *t = tete;
	if (n >= 4 && t[0] == 0x7F && t[1] == 'E' && t[2] == 'L' && t[3] == 'F')
		return 0;
	if (n >= 2 && t[0] == 'M' && t[1] == 'Z')
		return 0;
	if (n >= 2 && t[0] == '#' && t[1] == '!') {
		long i = 2;
		while (i < n && (t[i] == ' ' || t[i] == 9))
			i++;
		u64 k = 0;
		while (i < n && t[i] > ' ' && k < PATH_MAX - 1) /* (blanc, \t, \r, \n, 0) */
			interp[k++] = (char)t[i++];
		interp[k] = 0;
		if (k) {
			while (i < n && (t[i] == ' ' || t[i] == 9)) /* l'argument : le reste de la ligne */
				i++;
			k = 0;
			while (i < n && t[i] != 10 && t[i] != 13 && t[i] && k < IARG_MAX - 1)
				iarg[k++] = (char)t[i++];
			while (k && (iarg[k - 1] == ' ' || iarg[k - 1] == 9))
				k--;
			iarg[k] = 0;
			return 1;
		}
	}
	strcpy(interp, "/bin/sh"); /* sans #! : /bin/sh */
	iarg[0] = 0;
	return 1;
}
