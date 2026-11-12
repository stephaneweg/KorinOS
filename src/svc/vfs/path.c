/* path.c : chemins des programmes et dossiers courants par pid, traduction de
 * path.inc (docs/FICHIERS.md, étape 1, ops 30-36).
 *  * Chemin d'un programme : « SD0:/a/b », « sd0:a/b », « SD:/a » (volume 0),
 *    relatif (dossier courant du pid appelant), ou « /a » : racine du volume
 *    courant ; un chemin virtuel (SD0:/etc/paths.txt) passe d'abord.
 *  * Forme interne : « /sd0/a/b » (minuscules pour le volume, sans « . »,
 *    « .. », ni « / » final). Une entrée vide vaut « /sd0 ».
 *  * Les ops passent au service du volume (SVC_FORWARD) quand vfs n'a rien à
 *    composer ; V_EXEC, V_STATX, V_CHMOD, V_CHDIR et l'index en appellent un.
 *  * Le rootpath d'un volume n'apparaît qu'en envoyant au service (vol_chemin) :
 *    la forme interne reste « /sd0/a », un « .. » s'arrête à « / ».
 *  * Un nom sans numéro (« SD: ») désigne le volume 0 (« sd0 »), sauf s'il est
 *    lui-même monté sous ce nom : « BOOT: » est « boot ». */
#include "vfs.h"

#define MAP_MAX 16    /* chemins virtuels */
#define MAP_PHYS 64   /* une entrée : virtuel, puis physique interne */
#define MAP_TAILLE 192
#define E_PERM_X -1   /* E_PERM : pas exécutable */

const char dossier_defaut[] = "/sd0";
const char chemin_sys[] = "/sd0/sys/";
static const char chemin_lib[] = "/sd0/lib/";
const char sp_bin[] = "/sd0/bin/";
const char sp_apps[] = "/sd0/apps/";
const char sp_main[] = ".app/main";

static char chemins_v[MAP_MAX][MAP_TAILLE]; /* chemins virtuels (à zéro : aucun) */
long answered;
char resolu[PATH_MAX]; /* chemin résolu de la requête */
char nom_exec[PATH_MAX];
static char nom_proc[32];

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

/* chemin d'un programme (PATH_MAX, modifié en place) : « SD0:/a/b », « sd0:a/b »,
 * « SD:/a » -> « /sd0/a/b » ; relatif (sans « : ») : inchangé -> 0 s'il commence
 * par « / » ou si le volume est vide */
long path_from_volume(char *p)
{
	if (p[0] == '/')
		return 0;
	u64 n = 0; /* « NOM: » ? (lettres et chiffres, 8 au plus) */
	for (;;) {
		u8 c = (u8)p[n];
		if (c == ':')
			break;
		if ((u32)(c - '0') > 9 && (u32)((c | 0x20) - 'a') > 25)
			return 1; /* relatif : inchangé */
		if (++n > 8)
			return 1;
	}
	if (!n)
		return 0;
	u64 zero = (u32)((u8)p[n - 1] - '0') > 9; /* sans numéro : volume 0... */
	if (zero) { /* ... sauf un volume monté sous ce nom (BOOT:) */
		char nm[16];
		for (u64 k = 0; k < n; k++) {
			char c = p[k];
			nm[k] = c >= 'A' && c <= 'Z' ? c + 32 : c;
		}
		nm[n] = 0;
		if (volume_de(nm, 0))
			zero = 0;
	}
	char *rest = p + n + 1; /* après « : » (et « / ») */
	if (*rest == '/')
		rest++;
	u64 rlen = strlen(rest);
	u64 head = n + zero + 2; /* « / » + nom (+ « 0 ») + « / » */
	if (head + rlen >= PATH_MAX - 1)
		return 0;
	memmove(p + head, rest, rlen + 1); /* (de la fin vers le début : la tête peut grandir) */
	for (u64 k = n; k; k--) { /* nom en minuscules, décalé d'un cran */
		char c = p[k - 1];
		if (c >= 'A' && c <= 'Z')
			c += 32;
		p[k] = c;
	}
	p[0] = '/';
	u64 k = n + 1;
	if (zero)
		p[k++] = '0';
	p[k] = '/';
	return 1;
}

/* chemin interne normalisé (« /sd0/a ») -> « SD0:/a » ; rend la fin (après le 0) */
char *path_to_volume(const char *s, char *o)
{
	s++; /* « / » */
	for (;;) { /* nom du volume, en majuscules */
		char c = *s++;
		if (!c) {
			s--; /* (fin : « VOL:/ ») */
			break;
		}
		if (c == '/')
			break;
		if (c >= 'a' && c <= 'z')
			c -= 32;
		*o++ = c;
	}
	*o++ = ':';
	*o++ = '/';
	while ((*o++ = *s++))
		;
	return o;
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
 * « /sd0/apps/paint.app/main » donne « paint », sinon le dernier composant */
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
 * -> 0 ou erreur (E_NOENT : volume mal écrit, E_INVAL : trop long) */
long resoudre(long pid, const char *path, char *out)
{
	char tmp[PATH_MAX];
	const char *cwd = dossier_de(pid);
	u64 pre = 0; /* longueur du préfixe */
	if (path[0] == '/') {
		const char *rest;
		const char *e = chemin_virtuel(path, &rest);
		if (e) { /* son chemin physique, puis le reste */
			for (const char *s = e + MAP_PHYS; *s;)
				tmp[pre++] = *s++;
			path = rest;
		} else { /* « /a » : racine du volume courant, préfixe « /vol » du dossier */
			tmp[0] = '/';
			pre = 1;
			while (cwd[pre] && cwd[pre] != '/') {
				tmp[pre] = cwd[pre];
				pre++;
			}
		}
	}
	for (u64 i = 0;; i++) {
		if (pre + i >= PATH_MAX - 1)
			return E_INVAL;
		tmp[pre + i] = path[i];
		if (!path[i])
			break;
	}
	if (!pre && !path_from_volume(tmp)) /* « VOL:... » -> « /vol/... » */
		return E_NOENT;
	if (!path_normalize(cwd, tmp, out))
		return E_INVAL;
	return 0;
}

/* ---- chemins virtuels : « /gcc » = « SD0:/gcc », quel que soit le volume du
 * dossier courant (init2 les donne : SD0:/etc/paths.txt) */

/* chemin « /a/b » -> son entrée (0 : aucune), *rest la suite (« /b » ou vide) */
const char *chemin_virtuel(const char *path, const char **rest)
{
	for (int i = 0; i < MAP_MAX; i++) {
		const char *e = chemins_v[i];
		if (!e[0])
			return 0;
		u64 k = 0;
		while (e[k] && e[k] == path[k])
			k++;
		if (e[k])
			continue;
		if (!path[k] || path[k] == '/') { /* composant entier : pas « /gccx » */
			*rest = path + k;
			return e;
		}
	}
	return 0;
}

/* (msg) VFS_PATHMAP : virtuel, 0, physique, 0 -> 0, E_INVAL, E_NOSPC ; un
 * chemin virtuel déjà connu est remplacé */
long chemin_ajouter(void)
{
	((u8 *)&msg)[MSG_SIZE - 1] = 0;
	char *v = (char *)msg.data;
	if (v[0] != '/' || !v[1]) /* (« / » : pas une entrée) */
		return E_INVAL;
	u64 n = 0;
	while (v[n])
		if (++n >= MAP_PHYS)
			return E_INVAL;
	const char *ph = v + n + 1; /* physique -> forme interne */
	for (u64 i = 0;; i++) {
		nom_exec[i] = ph[i];
		if (!ph[i])
			break;
		if (i + 1 >= PATH_MAX - 1)
			return E_INVAL;
	}
	if (!path_from_volume(nom_exec))
		return E_INVAL;
	if (!path_normalize(dossier_defaut, nom_exec, resolu))
		return E_INVAL;
	if (strlen(resolu) >= MAP_TAILLE - MAP_PHYS)
		return E_INVAL;
	char *e = 0; /* la sienne, ou la première vide */
	for (int i = 0; i < MAP_MAX && !e; i++)
		if (!chemins_v[i][0] || !strcmp(chemins_v[i], v))
			e = chemins_v[i];
	if (!e)
		return E_NOSPC;
	strcpy(e, v);
	strcpy(e + MAP_PHYS, resolu);
	return 0;
}

/* chemin interne -> attributs qu'il donne : sous /sd0/sys/ ATTR_SYS, sous
 * /sd0/lib/ ATTR_LIB (le service les ajoute à la réponse d'ouverture) */
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
	if (fwd)
		return transmettre(v);
	long r = korin_sys2(SYS_SVC_CALL, v->chan, &msg);
	msg.op = r;
	if (r >= 0 && op == FS_OPEN)
		msg.a = v->chan;
	return r;
}

/* resolu va changer : le noyau oublie l'image préchargée de ce chemin */
static void forget_chemin(const char *p)
{
	char tmp[PATH_MAX + 16];
	path_to_volume(p, tmp);
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
 * Nom seul : SD0:/bin/<nom>, sinon SD0:/apps/<nom>.app/main ; chemin :
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
	if (r != E_NOENT) /* absente : « /sd0/apps/<nom>.app/main » */
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
	forget_resolu();
	forget_chemin(nom_exec);
	const char *target, *old;
	struct volume *v = volume_de(nom_exec + 1, &target);
	if (!v)
		return E_NOENT;
	struct volume *v2 = volume_de(resolu + 1, &old);
	if (!v2)
		return E_NOENT;
	if (v2->chan != v->chan) /* (deux volumes d'un même service : BOOT: et SD0:, permis) */
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
 * process, 0, chemin résolu « SD0:/... », 0) -> résultat (msg prêt pour la réponse) */
long ops_chemins(void)
{
	answered = 0; /* (transmise au service : sans réponse) */
	u64 op = msg.op;
	long pid = msg.pid;
	((u8 *)&msg)[MSG_SIZE - 1] = 0; /* (données terminées) */
	if (op == V_GETCWD) { /* « /sd0/a » -> « SD0:/a » */
		u8 *e = (u8 *)path_to_volume(dossier_de(pid), (char *)msg.data);
		msg.len = (u32)(e - msg.data);
		return 0;
	}
	if (op == V_EXEC) {
		long r = exec_resoudre(pid);
		if (r)
			return r;
		int script = 0;
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
		o = path_to_volume(resolu, o + n); /* puis le chemin résolu, « SD0:/a/b » */
		if (script) { /* un script : l'argument de #!, puis le script résolu */
			const char *a = iarg;
			while ((*o++ = *a++))
				;
			o = path_to_volume(script_res, o);
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
	switch (op) {
	case V_OPEN:
		/* écriture, création, troncature : le fichier change (image préchargée) */
		if (msg.a & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND))
			forget_resolu();
		return vers_volume(FS_OPEN, resolu, 0, 1); /* (propriétaire : MSG_PID, le programme) */
	case V_CHDIR: {
		char *dir = dossier_entree(pid);
		if (!dir)
			return E_INVAL;
		r = vers_volume(FS_STAT, resolu, 0, 0); /* un répertoire ? */
		if (r < 0)
			return r;
		if (!(msg.c & ATTR_DIR))
			return E_NOTDIR;
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
