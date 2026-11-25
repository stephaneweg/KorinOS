/* path.c : chemins des programmes et dossiers courants par pid, traduction de
 * path.inc (docs/FICHIERS.md, étape 1, ops 30-36).
 *  * Un seul arbre, à la Unix (15/11/2026, docs/etude/MULTI-UTILISATEUR.md) :
 *    « / » est le volume système, les autres volumes sont montés dans un dossier
 *    (« /tmp », « /media/usb0 », « /net/nas », « /boot »). Chemin d'un programme :
 *    absolu (« /a/b »), relatif (dossier courant du pid appelant), ou
 *    « HOME:/a » : le dossier personnel de l'utilisateur en cours (« /home »
 *    tant qu'il n'y a pas de comptes, « /home/<nom> » ensuite). Deux alias fixes
 *    (24/11/2026, alias_fixes) : « PUBLIC:/a » = « /home/public/a » (le dossier
 *    partagé entre les comptes), « APPS:/a » = « /apps/a ». Aucun autre
 *    « VOLUME: » n'est accepté. (À terme : un compte n'atteindrait /, /tmp et
 *    /home que par élévation, ses alias restant ouverts : une sorte d'unveil.)
 *  * Table des points de montage (chemins_v) : un chemin externe (« /media/usb0 »),
 *    sa forme interne (« /usb0 », ou « /ram/sub » pour un dossier monté) ; le plus
 *    long préfixe gagne, « / » couvre tout.
 *  * Forme interne : « /<volume>/a/b » (nom interne du volume en minuscules, sans
 *    « . », « .. », ni « / » final) ; le volume système s'appelle « root ». Elle ne
 *    sort jamais de vfs : getcwd, V_EXEC et les tubes nommés rendent la forme
 *    externe (chemin_externe).
 *  * Les ops passent au service du volume (SVC_FORWARD) quand vfs n'a rien à
 *    composer ; V_EXEC, V_STATX, V_CHMOD, V_CHDIR et l'index en appellent un.
 *  * Le rootpath d'un volume n'apparaît qu'en envoyant au service (vol_chemin). */
#include "vfs.h"

#define MAP_MAX 32    /* points de montage et dossiers montés */
#define MAP_PHYS 64   /* une entrée : externe, puis interne */
#define MAP_TAILLE 192
#define E_PERM_X -1   /* E_PERM : pas exécutable */

const char dossier_defaut[] = "/root";
const char chemin_sys[] = "/root/sys/";
static const char chemin_lib[] = "/root/lib/";
const char sp_bin[] = "/root/bin/";
const char sp_sbin[] = "/root/sbin/"; /* les commandes d'administration (étude MULTI-UTILISATEUR §2.9) */
const char sp_apps[] = "/root/apps/";
const char sp_main[] = ".app/main";

static char chemins_v[MAP_MAX][MAP_TAILLE]; /* points de montage (à zéro : place libre) */

/* ajoute le chemin p à [out, *end[ -> 1 si ok */
static long walk(const char *p, char *out, char **endp)
{
	char *e = *endp;
	for (;;) {
		char c = *p;
		if (!c)
			break;
		if (c == '/') {
			p++;
			continue;
		}
		if (c == '.' && (!p[1] || p[1] == '/')) { /* « . » */
			p++;
			continue;
		}
		if (c == '.' && p[1] == '.' && (!p[2] || p[2] == '/')) { /* « .. » : retirer le dernier composant */
			p += 2;
			while (e > out) {
				char x = *--e;
				*e = 0;
				if (x == '/')
					break;
			}
			continue;
		}
		if (e >= out + PATH_MAX - 2)
			return 0;
		*e++ = '/';
		for (;;) {
			c = *p;
			if (!c || c == '/')
				break;
			if (e >= out + PATH_MAX - 1)
				return 0;
			*e++ = c;
			p++;
		}
		*e = 0;
	}
	*e = 0;
	*endp = e;
	return 1;
}

/* dossier courant (absolu), chemin, sortie (PATH_MAX) -> 1 si ok ; absolu, sans
 * « . » ni « .. », sans « / » final */
long path_normalize(const char *cwd, const char *path, char *out)
{
	char *e = out;
	out[0] = 0;
	if (path[0] != '/' && !walk(cwd, out, &e)) /* absolu : on ignore le dossier courant */
		return 0;
	if (!walk(path, out, &e))
		return 0;
	if (e == out) { /* résultat vide : « / » */
		out[0] = '/';
		out[1] = 0;
	}
	return 1;
}

/* ---- points de montage : chemins externes <-> internes */

/* p (« /a/b ») commence-t-il par le composant entier e (« /a ») ? -> longueur de e, sinon -1 ;
 * « / » couvre tout (longueur 0 : le reste garde son « / ») */
static long prefixe_composant(const char *p, const char *e)
{
	if (e[0] == '/' && !e[1])
		return p[0] == '/' ? 0 : -1;
	u64 k = 0;
	while (e[k] && e[k] == p[k])
		k++;
	if (e[k] || (p[k] && p[k] != '/'))
		return -1;
	return (long)k;
}

/* chemin externe absolu normalisé -> son entrée (0 : aucune), *rest la suite (« /b » ou vide) :
 * le point de montage au plus long préfixe */
const char *chemin_virtuel(const char *path, const char **rest)
{
	const char *best = 0;
	long bk = -1;
	for (int i = 0; i < MAP_MAX; i++) {
		const char *e = chemins_v[i];
		if (!e[0])
			continue;
		long k = prefixe_composant(path, e);
		if (k > bk) {
			bk = k;
			best = e;
		}
	}
	if (best)
		*rest = path + bk;
	return best;
}

/* chemin interne normalisé (« /usb0/a ») -> sa forme externe (« /media/usb0/a ») dans o ; rend la
 * fin (après le 0). L'entrée au plus long préfixe interne gagne, à égalité le chemin externe le plus
 * court ; sans entrée (volume sans point de montage) : « / » */
char *chemin_externe(const char *s, char *o)
{
	const char *best = 0;
	long bk = -1;
	u64 bl = 0;
	for (int i = 0; i < MAP_MAX; i++) {
		const char *e = chemins_v[i];
		if (!e[0])
			continue;
		long k = prefixe_composant(s, e + MAP_PHYS);
		if (k < 0)
			continue;
		u64 l = strlen(e);
		if (k > bk || (k == bk && l < bl)) {
			bk = k;
			bl = l;
			best = e;
		}
	}
	const char *rest = "";
	if (best) {
		rest = s + bk;
		if (!(best[0] == '/' && !best[1])) /* (« / » : le reste a déjà son « / ») */
			o = str_cat(o, best);
	}
	if (!*rest && !(best && !(best[0] == '/' && !best[1])))
		rest = "/";
	o = str_cat(o, rest);
	return o + 1;
}

/* le dossier personnel de l'utilisateur de pid, forme interne, dans out (PATH_MAX) -> 1, 0 sans
 * volume système : celui de son compte dans /etc/users.ini (homes.c, d'après l'uid que le noyau donne
 * pour ce pid) ; « /home » sans users.ini. Plus tard, un home sur le réseau (« /net/<serveur>/<nom> »)
 * passera par le même chemin. */
static long home_interne(long pid, char *out)
{
	const char *rest;
	const char *e = chemin_virtuel(home_externe(pid_uid(pid)), &rest);
	if (!e)
		return 0;
	out[0] = 0;
	str_cat(str_cat(out, e + MAP_PHYS), rest);
	return 1;
}

/* « HOME: » au début du chemin (sans casse) ? -> longueur du préfixe (5), sinon 0 */
static u64 prefixe_home(const char *p)
{
	if ((p[0] | 0x20) == 'h' && (p[1] | 0x20) == 'o' && (p[2] | 0x20) == 'm' && (p[3] | 0x20) == 'e' &&
	    p[4] == ':')
		return 5;
	return 0;
}

/* les alias fixes : « NOM: » -> son dossier de l'arbre (HOME:, propre à chaque compte, est à part) */
static const char *const alias_fixes[][2] = {
	{"PUBLIC:", "/home/public"},
	{"APPS:", "/apps"},
};

/* un alias fixe au début du chemin (sans casse) ? -> longueur du préfixe, son dossier dans *cible ; sinon 0 */
static u64 prefixe_alias(const char *p, const char **cible)
{
	for (u64 k = 0; k < sizeof alias_fixes / sizeof alias_fixes[0]; k++) {
		const char *a = alias_fixes[k][0];
		u64 i = 0;
		while (a[i] && (((u8)p[i] | 0x20) == ((u8)a[i] | 0x20) || (a[i] == ':' && p[i] == ':')))
			i++;
		if (!a[i]) {
			*cible = alias_fixes[k][1];
			return i;
		}
	}
	return 0;
}

/* « NOM: » (lettres et chiffres) au début ? -> 1 : un ancien chemin à volume, refusé */
static int prefixe_volume(const char *p)
{
	for (u64 n = 0; n < 16; n++) {
		u8 c = (u8)p[n];
		if (c == ':')
			return n > 0;
		if ((u32)(c - '0') > 9 && (u32)((c | 0x20) - 'a') > 25)
			return 0;
	}
	return 0;
}

/* chemin interne : « .../<nom>.app » -> « .../<nom>.app/main » */
static void app_main_fix(char *p)
{
	u64 n = strlen(p);
	if (n < 5 || memcmp(p + n - 4, ".app", 4) || n >= PATH_MAX - 6)
		return;
	memcpy(p + n, "/main", 6);
}

/* chemin, son dernier composant, tampon de 32 o -> nom du process :
 * « /root/apps/paint.app/main » donne « paint », sinon le dernier composant */
static const char *spawn_name(const char *path, const char *last, char *buf)
{
	if (memcmp(last, "main", 4) || last[4])
		return last;
	const char *sl = last - 1; /* sur le « / » */
	if (sl - path < 5 || memcmp(sl - 4, ".app", 4))
		return last;
	const char *end = sl - 4, *s = end; /* son début : après le « / » précédent */
	while (s > path && s[-1] != '/')
		s--;
	u64 n = 0;
	while (s < end && n < 31)
		buf[n++] = *s++;
	buf[n] = 0;
	return buf;
}

/* ---- dossiers courants */

static char *dossier_entree(long pid)
{
	struct pr *r = pid_get(pid);
	return r ? r->dir : 0;
}

static const char *dossier_de(long pid)
{
	struct pr *r = pid_find(pid);
	if (r && r->dir[0])
		return r->dir;
	return dossier_defaut;
}

/* avis de naissance : le fils reçoit le dossier du père (par défaut s'il n'en a pas) */
void naissance(long child, long parent)
{
	struct pr *c = pid_get(child);
	if (!c)
		return;
	c->dir[0] = 0;
	struct pr *p = pid_find(parent); /* (père 0 : le noyau, sans fiche) */
	if (!p)
		return;
	for (u64 i = 0; i < PATH_MAX; i++) {
		c->dir[i] = p->dir[i];
		if (!c->dir[i])
			return;
	}
	c->dir[PATH_MAX - 1] = 0;
}

void oublier_dossier(long pid)
{
	struct pr *r = pid_find(pid);
	if (r)
		r->dir[0] = 0;
}

/* pid, chemin du programme, sortie (PATH_MAX) : chemin interne absolu normalisé
 * -> 0 ou erreur (E_NOENT : « VOLUME: » ou rien de monté là, E_INVAL : trop long) */
long resoudre(long pid, const char *path, char *out)
{
	char tmp[PATH_MAX], ext[PATH_MAX];
	u64 h = prefixe_home(path);
	if (h) { /* « HOME:/a » : le dossier personnel, forme interne */
		if (!home_interne(pid, tmp))
			return E_NOENT;
		u64 n = strlen(tmp), r = strlen(path + h);
		if (n + r + 2 >= PATH_MAX)
			return E_INVAL;
		tmp[n++] = '/';
		memcpy(tmp + n, path + h, r + 1);
		if (!path_normalize(dossier_defaut, tmp, out))
			return E_INVAL;
		return out[1] ? 0 : E_NOENT; /* (« .. » jusqu'au-dessus des volumes) */
	}
	const char *cible;
	u64 a = prefixe_alias(path, &cible);
	if (a) { /* « PUBLIC:/a », « APPS:/a » : leur dossier de l'arbre, puis comme un chemin absolu */
		u64 n = strlen(cible), r = strlen(path + a);
		if (n + r + 2 >= PATH_MAX)
			return E_INVAL;
		memcpy(tmp, cible, n);
		tmp[n++] = '/';
		memcpy(tmp + n, path + a, r + 1);
		return resoudre(pid, tmp, out);
	}
	if (prefixe_volume(path)) /* (plus de « / », « /media/usb0 »... : l'arbre seulement) */
		return E_NOENT;
	u64 n = 0;
	if (path[0] != '/') { /* relatif : le dossier courant, forme externe */
		n = (u64)(chemin_externe(dossier_de(pid), tmp) - tmp) - 1;
		if (n + 1 >= PATH_MAX)
			return E_INVAL;
		tmp[n++] = '/';
	}
	u64 r = strlen(path);
	if (n + r + 1 >= PATH_MAX)
		return E_INVAL;
	memcpy(tmp + n, path, r + 1);
	if (!path_normalize("/", tmp, ext))
		return E_INVAL;
	const char *rest;
	const char *e = chemin_virtuel(ext, &rest);
	if (!e) /* (rien monté sur « / » : avant le volume système) */
		return E_NOENT;
	const char *in = e + MAP_PHYS;
	u64 li = strlen(in), lr = strlen(rest);
	if (li + lr + 1 >= PATH_MAX)
		return E_INVAL;
	memcpy(out, in, li);
	memcpy(out + li, rest, lr + 1);
	return 0;
}

/* ---- la table des points de montage */

/* chemin externe (« /media/usb0 », normalisé), interne (« /usb0 ») -> 0, E_INVAL, E_NOSPC ; un
 * point déjà connu est remplacé */
long point_ajouter(const char *ext, const char *in)
{
	if (ext[0] != '/' || strlen(ext) >= MAP_PHYS || in[0] != '/' || strlen(in) >= MAP_TAILLE - MAP_PHYS)
		return E_INVAL;
	char *e = 0; /* le sien, ou la première place libre */
	for (int i = 0; i < MAP_MAX && !e; i++)
		if (chemins_v[i][0] && !strcmp(chemins_v[i], ext))
			e = chemins_v[i];
	for (int i = 0; i < MAP_MAX && !e; i++)
		if (!chemins_v[i][0])
			e = chemins_v[i];
	if (!e)
		return E_NOSPC;
	strcpy(e + MAP_PHYS, in);
	strcpy(e, ext);
	return 0;
}

/* volume (nom interne) : ses entrées quittent la table (démonté) */
void points_retirer(const char *vol)
{
	for (int i = 0; i < MAP_MAX; i++) {
		char *e = chemins_v[i];
		if (!e[0])
			continue;
		const char *in = e + MAP_PHYS + 1;
		u64 k = 0;
		while (vol[k] && vol[k] == in[k])
			k++;
		if (!vol[k] && (!in[k] || in[k] == '/'))
			e[0] = 0;
	}
}

/* chemin externe normalisé -> 1 s'il est exactement un point de montage */
int point_pris(const char *ext)
{
	for (int i = 0; i < MAP_MAX; i++)
		if (chemins_v[i][0] && !strcmp(chemins_v[i], ext))
			return 1;
	return 0;
}

/* volume (nom interne) -> son point de montage (0 : aucun) */
const char *point_de(const char *vol)
{
	const char *best = 0;
	for (int i = 0; i < MAP_MAX; i++) {
		const char *e = chemins_v[i];
		if (e[0] && e[MAP_PHYS + 1 + strlen(vol)] == 0 && !strncmp(e + MAP_PHYS + 1, vol, strlen(vol)) &&
		    (!best || strlen(e) < strlen(best)))
			best = e;
	}
	return best;
}

/* (msg) VFS_PATHMAP : externe, 0, cible, 0 (un dossier de l'arbre : « /media/ram0/x ») -> 0,
 * E_INVAL, E_NOENT, E_NOSPC : monte ce dossier aussi sous ce chemin */
long chemin_ajouter(void)
{
	((u8 *)&msg)[MSG_SIZE - 1] = 0;
	char *v = (char *)msg.data;
	if (v[0] != '/' || !v[1]) /* (« / » : seulement le volume système) */
		return E_INVAL;
	char ext[PATH_MAX];
	if (strlen(v) >= MAP_PHYS || !path_normalize("/", v, ext))
		return E_INVAL;
	long r = resoudre(0, v + strlen(v) + 1, resolu);
	if (r)
		return r;
	return point_ajouter(ext, resolu);
}

/* (msg) VFS_PATHLIST : a = première entrée -> nombre d'entrées rendues, b =
 * l'entrée suivante (MAP_MAX : fini), données = « externe », 0, « interne »
 * (« /usb0 »), 0, autant qu'il en tient : les points de montage */
long chemins_lister(void)
{
	u64 i = (u64)msg.a, o = 0;
	long n = 0;
	for (; i < MAP_MAX; i++) {
		const char *v = chemins_v[i];
		if (!v[0])
			continue;
		const char *p = v + MAP_PHYS;
		u64 lv = strlen(v), lp = strlen(p);
		if (o + lv + lp + 2 > sizeof msg.data)
			break; /* (la suite au prochain appel) */
		memcpy(msg.data + o, v, lv + 1);
		memcpy(msg.data + o + lv + 1, p, lp + 1);
		o += lv + lp + 2;
		n++;
	}
	msg.b = (i64)i;
	msg.len = (u32)o;
	return n;
}

/* chemin interne -> attributs qu'il donne : sous /root/sys/ ATTR_SYS, sous
 * /root/lib/ ATTR_LIB (le service les ajoute à la réponse d'ouverture) */
static long attributs_de(const char *p)
{
	if (!strncmp(p, chemin_sys, sizeof chemin_sys - 1))
		return ATTR_SYS;
	if (!strncmp(p, chemin_lib, sizeof chemin_lib - 1))
		return ATTR_LIB;
	return 0;
}

/* volume, msg prêt : la requête en cours passe à son service -> 0 (answered =
 * 1), sinon l'erreur (service parti) */
static long transmettre(struct volume *v)
{
	long r = korin_sys3(SYS_SVC_FORWARD, current_req, v->chan, &msg);
	if (!r)
		answered = 1;
	return r;
}

/* op FS_*, chemin interne (hors de msg), d (pid pour qui l'on ouvre, V_EXEC ; 0
 * sinon), fwd = 1 : la requête en cours passe au service (SVC_FORWARD) ; msg :
 * a déjà rempli -> résultat (réponse du service dans msg ; ouverture : a = son
 * canal). Ouverture : c = les attributs que le service ajoute */
static long vers_volume(long op, const char *path, long d, int fwd)
{
	if (path[0] != '/') {
		msg.op = E_NOENT;
		return E_NOENT;
	}
	if (op == FS_OPEN)
		msg.c = attributs_de(path);
	const char *rest;
	struct volume *v = volume_de(path + 1, &rest);
	if (!v) {
		msg.op = E_NOENT;
		return E_NOENT;
	}
	if (v->root[0] && !*rest && op == FS_UNLINK) { /* la racine du volume (le service : son
							  rootpath, un dossier comme un autre) */
		msg.op = E_INVAL;
		return E_INVAL;
	}
	u64 n = vol_chemin(v, rest, msg.data, FS_DATA_MAX); /* (rootpath devant) */
	if (!n) {
		msg.op = E_NAMETOOLONG;
		return E_NAMETOOLONG;
	}
	msg.len = (u32)n;
	msg.op = op;
	msg.d = d;
	/* le cache des noms (ncache.c) : un absent connu est répondu ici ; un open en lecture d'un
	   inconnu est fait par un appel au nom du programme, pour en apprendre le résultat */
	int lecture = op == FS_OPEN && !(msg.a & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND));
	int apprendre = 0, garder_a = 0;
	if (op == FS_STAT || lecture) {
		long st = nc_lookup(v, (char *)msg.data);
		if (st < 0) {
			msg.op = st;
			msg.len = 0;
			return st;
		}
		if (!st) {
			apprendre = 1;
			if (fwd && lecture) { /* (le service voit vfs : d = le programme, propriétaire) */
				fwd = 0;
				msg.d = msg.pid;
				garder_a = 1; /* a : le canal que le service rend, comme transmise */
			}
		}
	} else if (op == FS_OPEN || op == FS_MKDIR || op == FS_UNLINK) {
		nc_forget(v, (char *)msg.data); /* (le chemin va être créé ou effacé) */
	}
	if (fwd)
		return transmettre(v);
	char cle[NC_KEY_MAX];
	u64 seq = nc_seq();
	if (apprendre)
		memcpy(cle, msg.data, n);
	long ch = v->chan;
	long r = vol_appel_libre(v, &msg); /* (verrou global relâché : un service lent ne bloque pas vfs) */
	msg.op = r;
	if (apprendre && v->chan == ch && v->name[0]) /* (démonté entre-temps : nc_seq a bougé) */
		nc_learn(v, cle, r, seq);
	if (r >= 0 && op == FS_OPEN && !garder_a)
		msg.a = ch;
	return r;
}

/* resolu va changer : le noyau oublie l'image préchargée de ce chemin */
static void forget_chemin(const char *p)
{
	char tmp[PATH_MAX + 16];
	chemin_externe(p, tmp);
	korin_sys1(SYS_IMAGE_FORGET, tmp);
}

static void forget_resolu(void)
{
	forget_chemin(resolu);
}

/* -> 0 si resolu est un fichier (E_ISDIR : répertoire), ou erreur */
static long stat_resolu(void)
{
	long r = vers_volume(FS_STAT, resolu, 0, 0);
	if (r < 0)
		return r;
	return (msg.c & ATTR_DIR) ? E_ISDIR : 0;
}

/* ajoute à resolu -> 1, 0 si trop long */
static long ajouter(const char *s)
{
	u64 n = strlen(resolu);
	for (;;) {
		resolu[n] = *s++;
		if (!resolu[n])
			return 1;
		if (++n >= PATH_MAX - 1) {
			resolu[n] = 0;
			return 0;
		}
	}
}

/* (V_EXEC) pid ; msg : nom ou chemin -> resolu = l'exécutable, 0 ou erreur.
 * Nom seul : /bin/<nom>, sinon /apps/<nom>.app/main ; chemin :
 * « .../<nom>.app » -> « .../main ». Un répertoire : E_ISDIR */
static long exec_resoudre(long pid)
{
	const char *d = (char *)msg.data;
	u64 i = 0;
	for (;;) {
		nom_exec[i] = d[i];
		if (!d[i++])
			break;
		if (i >= PATH_MAX - 1) {
			nom_exec[i] = 0;
			break;
		}
	}
	int chemin = 0;
	for (const char *s = nom_exec; *s; s++)
		if (*s == '/' || *s == ':')
			chemin = 1;
	if (chemin) {
		long r = resoudre(pid, nom_exec, resolu);
		if (r)
			return r;
		app_main_fix(resolu);
		return stat_resolu();
	}
	if (!nom_exec[0])
		return E_NOENT;
	resolu[0] = 0;
	if (!ajouter(sp_bin) || !ajouter(nom_exec))
		return E_INVAL;
	long r = stat_resolu();
	if (r != E_NOENT) /* absente : « /root/apps/<nom>.app/main » */
		return r;
	resolu[0] = 0;
	if (!ajouter(sp_apps) || !ajouter(nom_exec) || !ajouter(sp_main))
		return E_INVAL;
	return stat_resolu();
}

/* (msg) V_RENAME au nom du pid : les deux chemins résolus, un seul volume
 * -> résultat (E_XDEV : deux volumes) */
static long chemin_renommer(long pid)
{
	char *d = (char *)msg.data;
	long r = resoudre(pid, d, resolu);
	if (r)
		return r;
	while (*d++)
		;
	r = resoudre(pid, d, nom_exec);
	if (r)
		return r;
	if ((r = acces_verifier(pid, resolu, 1)) || (r = acces_verifier(pid, nom_exec, 1)))
		return r; /* (règles d'accès, access.c) */
	forget_resolu();
	forget_chemin(nom_exec);
	nc_flush(); /* (un dossier renommé : tout ce qu'il contient change de nom) */
	watch_touch(resolu); /* les guets des deux dossiers (watch.c) */
	watch_touch(nom_exec);
	const char *target, *old;
	struct volume *v = volume_de(nom_exec + 1, &target);
	if (!v)
		return E_NOENT;
	struct volume *v2 = volume_de(resolu + 1, &old);
	if (!v2)
		return E_NOENT;
	if (v2->chan != v->chan) /* (deux volumes d'un même service : /boot et /, permis) */
		return E_XDEV;
	if ((v2->root[0] && !*old) || (v->root[0] && !*target)) /* la racine d'un volume à rootpath */
		return E_INVAL;
	struct volume *io = vol_index(v2), *in = vol_index(v);
	int index = 0; /* l'index est-il concerné (l'ancien ou ce qu'il contient, ou la cible) ? */
	if (!idx_charger(io) && !idx_charger(in)) {
		u64 len;
		if (idx_cherche(io, old, 1, &len) >= 0 || idx_cherche(in, target, 0, &len) >= 0)
			index = 1;
	}
	u64 n1 = vol_chemin(v2, old, msg.data, FS_DATA_MAX); /* ancien, 0, nouveau, 0 (rootpath devant) */
	u64 n2 = n1 ? vol_chemin(v, target, msg.data + n1, FS_DATA_MAX - n1) : 0;
	if (!n2)
		return E_NAMETOOLONG;
	msg.len = (u32)(n1 + n2);
	msg.op = FS_RENAME;
	if (!index) /* le service répond au programme */
		return transmettre(v);
	r = korin_sys2(SYS_SVC_CALL, v->chan, &msg); /* appel direct, puis l'index suit */
	if (r < 0)
		return r;
	idx_renommer(io, old, in, target);
	idx_ecrire(in);
	if (io != in)
		idx_ecrire(io);
	return r;
}

static u64 longueur(const char *s)
{
	return strlen(s);
}

/* (msg) ops 30-36, 42, 43, 46, 47 au nom de MSG_PID (V_EXEC : données = nom du
 * process, 0, chemin résolu « /a/b », 0) -> résultat (msg prêt pour la réponse) */
long ops_chemins(void)
{
	answered = 0; /* (transmise au service : sans réponse) */
	u64 op = msg.op;
	long pid = msg.pid;
	((u8 *)&msg)[MSG_SIZE - 1] = 0; /* (données terminées) */
	if (op == V_GETCWD) { /* forme interne -> externe */
		u8 *e = (u8 *)chemin_externe(dossier_de(pid), (char *)msg.data);
		msg.len = (u32)(e - msg.data);
		return 0;
	}
	if (op == V_EXEC) {
		long r = exec_resoudre(pid);
		if (r)
			return r;
		int script = 0;
		if ((r = acces_verifier(pid, resolu, 0)) || (r = acces_sbin(pid, resolu)))
			return r;
		if (!est_executable(resolu)) /* exécutable ? (exec.c) */
			return E_PERM_X;
		r = script_voir(); /* un script : son interprète */
		if (r < 0)
			return r;
		if (r) {
			strcpy(script_res, resolu);
			r = resoudre(pid, interp, resolu);
			if (r)
				return r;
			r = stat_resolu();
			if (r)
				return r;
			if (!est_executable(resolu))
				return E_PERM_X;
			script = 1;
			/* noms et argument : tiennent-ils ? */
			if (longueur(resolu) + longueur(script_res) + longueur(iarg) >= FS_DATA_MAX - 48)
				return E_INVAL;
		}
		msg.a = O_READ; /* ouvert pour l'appelant (attributs : ajoutés par le service) */
		r = vers_volume(FS_OPEN, resolu, pid, 0);
		if (r < 0)
			return r;
		const char *p = script ? script_res : resolu; /* nom du process (un script : le sien) */
		const char *last = p;
		for (const char *s = p; *s;)
			if (*s++ == '/')
				last = s;
		const char *name = spawn_name(p, last, nom_proc);
		char *o = (char *)msg.data;
		u64 n = 0;
		do
			o[n] = name[n];
		while (name[n++]);
		o = chemin_externe(resolu, o + n); /* puis le chemin résolu, « /a/b » */
		if (script) { /* un script : l'argument de #!, puis le script résolu */
			const char *a = iarg;
			while ((*o++ = *a++))
				;
			o = chemin_externe(script_res, o);
			msg.c |= ATTR_SCRIPT;
		}
		msg.len = (u32)((u8 *)o - msg.data);
		return r;
	}
	if (op == V_RENAME)
		return chemin_renommer(pid);
	long r = resoudre(pid, (char *)msg.data, resolu);
	if (r)
		return r;
	if (op == V_OPEN || op == V_MKDIR || op == V_UNLINK || op == V_UTIME || op == V_CHMOD) {
		int ecrire = op != V_OPEN || (msg.a & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND));
		r = acces_verifier(pid, resolu, ecrire); /* règles d'accès (access.c ; étape 3 : journal) */
		if (r)
			return r;
		if (ecrire)
			watch_touch(resolu); /* un nom change dans son dossier : les guets (watch.c) */
	}
	switch (op) {
	case V_OPEN:
		/* écriture, création, troncature : le fichier change (image préchargée) */
		if (msg.a & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND))
			forget_resolu();
		return vers_volume(FS_OPEN, resolu, 0, 1); /* (propriétaire : MSG_PID, le programme) */
	case V_CHDIR: {
		if (!dossier_entree(pid))
			return E_INVAL;
		r = vers_volume(FS_STAT, resolu, 0, 0); /* un répertoire ? (verrou relâché pendant l'attente) */
		if (r < 0)
			return r;
		if (!(msg.c & ATTR_DIR))
			return E_NOTDIR;
		char *dir = dossier_entree(pid); /* (relue : la table a pu changer pendant l'attente) */
		if (!dir)
			return E_INVAL;
		strcpy(dir, resolu);
		msg.len = 0;
		return 0;
	}
	case V_UTIME: /* (b : la date, déjà dans msg) */
		return vers_volume(FS_UTIME, resolu, 0, 1);
	case V_STATX: { /* stat, puis le bit d'exécution */
		r = vers_volume(FS_STAT, resolu, 0, 0);
		if (r < 0)
			return r;
		if (!(msg.c & ATTR_DIR) && est_executable(resolu))
			msg.c |= ATTR_EXEC;
		msg.c |= acces_bits(resolu); /* propriétaire et genre (access.c) : st_uid, st_mode de libposix */
		msg.len = 0;
		return 0;
	}
	case V_CHMOD: {
		u64 mode = msg.a; /* le mode (msg sert au stat) */
		r = vers_volume(FS_STAT, resolu, 0, 0);
		if (r < 0)
			return r;
		r = (msg.c & ATTR_DIR) ? 0 : idx_chmod(mode); /* (un répertoire : rien) */
		msg.len = 0;
		return r;
	}
	case V_UNLINK: /* un fichier supprimé : son image préchargée n'est plus prise, ni
			  son entrée de l'index (exec.c) */
		forget_resolu();
		idx_oublier();
		break;
	}
	/* 31-33 -> FS_STAT, FS_MKDIR, FS_UNLINK ; le service répond au programme */
	return vers_volume(op - (V_STAT - FS_STAT), resolu, 0, 1);
}
