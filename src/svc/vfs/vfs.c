/* vfs - le service des volumes (docs/MICRO-NOYAU.md, section 4 ; docs/FICHIERS.md),
 * traduction de vfs.asm. Il tient la table des volumes montés : pour chacun, le
 * service de fichiers qui le sert.
 *  * Les programmes (AppKit) lui envoient leurs chemins (ops 30-36) ; il les
 *    transmet au service du volume avec le chemin relatif (« a/b ») ; à
 *    l'ouverture, le service ajoute son canal à la réponse.
 *  * Monter : un volume, une source (« ram:16M »), un service (« ramfs ») ; vfs
 *    lance SD0:/sys/svc/<service> « <volume> <source> » (un module de
 *    l'initramfs d'abord), qui s'enregistre sous « fs.<volume> ». Au démarrage :
 *    SD0:/etc/mounts.txt (dans un thread) ; ensuite : la requête 20 (monter).
 *  * Un service de fichiers qui s'arrête : son volume disparaît.
 *  * Chaque volume est publié sur le bus : « korin.storage.volume.<volume> »
 *    (gardée, « <source> <service> »), retiré avec lui.
 *  * Mode services (lancé par init) : un volume en arguments (« SD0 blk.sd0p1
 *    fatfs ») est monté d'abord.
 *  * rootpath (12/11/2026, docs/FICHIERS.md, « Le dossier racine d'un volume ») :
 *    un quatrième mot facultatif (« SD0 blk.sd0p1 fatfs /korinARM », la requête
 *    20, « SD1 = blk.sd0p2, fatfs, /data » de mounts.txt) : ce dossier de la
 *    source est la racine du volume. vfs le met devant le chemin relatif en
 *    l'envoyant au service (vol_chemin) ; les chemins internes (« /sd0/a »), donc
 *    getcwd, les listes et les messages, ne le montrent jamais, et un « .. » ne
 *    peut pas en sortir (normalisé avant). Omis ou « / » : comme avant.
 *  * Deux volumes d'une même source « blk.* » et d'un même service partagent
 *    toujours ce service (canal, pid, cache) : jamais deux fatfs sur la même
 *    partition. Un tel volume démonté ne fait que quitter la table ; le service
 *    s'arrête avec le dernier.
 *  * BOOT: à la demande (décision du 12/11/2026 ; sur le PC, la partition EFI de
 *    Windows ne reste pas ouverte) : un programme qui écrit un fichier de
 *    démarrage (pkg, mkinitramfs) demande VFS_BOOT_BEGIN : vfs monte BOOT:
 *    d'après sa ligne « BOOT = source, service, rootpath, ondemand » de
 *    SD0:/etc/mounts.txt (jamais montée au démarrage) et rend « BOOT: » ; sans
 *    cette ligne il rend « SD0: » (les fichiers de démarrage à la racine du volume
 *    système, comme avant). VFS_BOOT_END, ou la fin du programme, le rend ; au
 *    dernier, BOOT: est démonté (FS_SYNC, puis son service arrêté s'il ne sert
 *    que lui).
 *  * Chemins (path.c), bit d'exécution (exec.c), tubes et entrées / sorties
 *    (pipes.c), fiches des process (pids.c).
 *  * Lancement depuis le thread principal (monter : un service hors de
 *    l'initramfs) : résolution locale puis f_spawn_file d'AppKit : vfs ne
 *    s'envoie jamais de requête à lui-même. */
#include "vfs.h"

#define INI_MAX 4096
#define BUS_OP_PUBLISH 1
#define BUS_RETAIN 1
#define BUS_DELETE 2
#define BUS_NAME_MAX 64
#define BUS_VALUE_MAX 256
#define BUS_EINVAL -22
#define BUS_ENODEV -19
#define VE_TAILLE 56 /* VFS_VOLUMES : nom 16, source 24, service 16 */

static long canal;
static long own_pid;
static long bp_canal; /* canal de busd + 1 (0 : pas ouvert) */
struct volume volumes[VOL_MAX];
static char nom_vol[16], nom_svc[32], chemin[64], args[96];
struct msg msg __attribute__((aligned(16)));
static struct msg msg_save __attribute__((aligned(16)));
static char ini_buf[INI_MAX];

/* ---- bus (busd), sans BusKit comme l'asm (src/svc/inc/buspub.inc) */

/* attente en ms -> 1 si le canal de busd est ouvert */
static long bp_open(long wait)
{
	if (bp_canal)
		return 1;
	long c = korin_sys2(SYS_SVC_OPEN, "bus", wait);
	if (c < 0)
		return 0;
	bp_canal = c + 1;
	return 1;
}

/* sujet, valeur (255 octets au plus ; 0 : vide), BUS_RETAIN / BUS_DELETE -> réponse
 * de busd, < 0 : erreur ou pas de canal (busd relancé : le canal est oublié) */
static long bp_publish(const char *topic, const char *val, long flags)
{
	struct msg m __attribute__((aligned(16)));
	m.op = BUS_OP_PUBLISH;
	m.a = flags;
	m.c = m.d = 0;
	m.len = 0;
	u8 *o = m.data;
	for (long left = BUS_NAME_MAX;;) { /* le nom */
		u8 c = (u8)*topic++;
		*o++ = c;
		if (!c)
			break;
		if (!--left)
			return BUS_EINVAL;
	}
	u8 *v0 = o; /* la valeur */
	if (val)
		for (long left = BUS_VALUE_MAX - 1; left; left--) {
			u8 c = (u8)*val++;
			if (!c)
				break;
			*o++ = c;
		}
	*o = 0;
	m.b = o - v0;
	if (!bp_canal)
		return BUS_ENODEV;
	long r = korin_sys2(SYS_SVC_CALL, bp_canal - 1, &m);
	if (r >= 0 || r == m.op) /* (réponse de busd) */
		return r;
	bp_canal = 0; /* non : le canal est mort */
	return BUS_ENODEV;
}

static char *bp_cat(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	return d;
}

/* volume, remove = 0 : publié (gardé, « <source> <service> », puis « /<rootpath> »
 * s'il en a un) ; 1 : retiré. Rien tant que le bus n'est pas ouvert (le thread des
 * montages publiera) */
void vol_publier(struct volume *v, int remove)
{
	char topic[64], val[128];
	if (!bp_canal)
		return;
	*bp_cat(bp_cat(topic, "korin.storage.volume."), v->name) = 0;
	char *o = bp_cat(val, v->src);
	*o++ = ' ';
	o = bp_cat(o, v->fs);
	if (v->root[0]) {
		o = bp_cat(o, " /");
		o = bp_cat(o, v->root);
	}
	*o = 0;
	if (remove)
		bp_publish(topic, 0, BUS_DELETE);
	else
		bp_publish(topic, val, BUS_RETAIN);
}

/* « vol/reste » ou « vol » -> entrée du volume (0 : aucun), *rest le reste
 * (sans « / » initial) */
struct volume *volume_de(const char *p, const char **rest)
{
	for (int i = 0; i < VOL_MAX; i++) {
		struct volume *v = &volumes[i];
		if (v->chan < 0 || !v->name[0])
			continue;
		u64 k = 0;
		while (v->name[k] && v->name[k] == p[k])
			k++;
		if (v->name[k])
			continue;
		if (p[k] == '/') {
			if (rest)
				*rest = p + k + 1;
			return v;
		}
		if (!p[k]) { /* « vol » seul : racine */
			if (rest)
				*rest = p + k;
			return v;
		}
	}
	return 0;
}

/* destination, source, octets au plus (sans le 0) */
static void copie_bornee(char *d, const char *s, u64 n)
{
	while (n && *s) {
		*d++ = *s++;
		n--;
	}
	*d = 0;
}

/* volume, chemin relatif à sa racine (« a/b », vide : la racine), sortie, octets au
 * plus -> le chemin pour son service (rootpath devant : « korinARM/a/b »), octets
 * écrits avec le 0 ; 0 s'il ne tient pas */
u64 vol_chemin(const struct volume *v, const char *rest, u8 *out, u64 cap)
{
	u64 n = 0;
	for (const char *s = v->root; *s; s++) {
		if (n + 1 >= cap)
			return 0;
		out[n++] = (u8)*s;
	}
	if (n && *rest) {
		if (n + 1 >= cap)
			return 0;
		out[n++] = '/';
	}
	for (const char *s = rest; *s; s++) {
		if (n + 1 >= cap)
			return 0;
		out[n++] = (u8)*s;
	}
	out[n++] = 0;
	return n;
}

/* volume -> celui dont l'index des exécutables (.exec) vaut pour lui : le premier
 * volume du même service et de la même racine (BOOT: et SD0: d'une carte sans
 * rootpath : un seul fichier, un seul index en mémoire) */
struct volume *vol_index(struct volume *v)
{
	for (int i = 0; i < VOL_MAX; i++) {
		struct volume *e = &volumes[i];
		if (e->name[0] && e->chan == v->chan && !strcmp(e->root, v->root))
			return e;
	}
	return v;
}

/* rootpath donné (« /korinARM », « korinARM/ », « / », 0) -> root (« korinARM »,
 * vide : la racine) ; 0, E_INVAL (« .. », trop long) */
static long racine_normaliser(const char *p, char *root)
{
	u64 n = 0;
	root[0] = 0;
	if (!p)
		return 0;
	while (*p) {
		while (*p == '/')
			p++;
		if (!*p)
			break;
		const char *e = p;
		while (*e && *e != '/')
			e++;
		u64 k = (u64)(e - p);
		if (k == 1 && p[0] == '.') { /* « . » : rien */
			p = e;
			continue;
		}
		if (k == 2 && p[0] == '.' && p[1] == '.') /* (jamais au-dessus de la source) */
			return E_INVAL;
		if (n + k + 2 > ROOT_MAX)
			return E_INVAL;
		if (n)
			root[n++] = '/';
		memcpy(root + n, p, k);
		n += k;
		root[n] = 0;
		p = e;
	}
	return 0;
}

/* canal, rootpath (non vide) -> 0 si c'est un répertoire de la source, sinon
 * l'erreur (message local : le thread des montages appelle aussi) */
static long racine_verifier(long ch, const char *root)
{
	struct msg m __attribute__((aligned(16)));
	m.op = FS_STAT;
	m.a = m.b = m.c = m.d = 0;
	u64 n = strlen(root) + 1;
	memcpy(m.data, root, n);
	m.len = (u32)n;
	long r = korin_sys2(SYS_SVC_CALL, ch, &m);
	if (r < 0)
		return r;
	return (m.c & ATTR_DIR) ? 0 : E_NOTDIR;
}

/* (thread principal) chemin, arguments : lance le programme sans requête à vfs
 * lui-même : V_EXEC résolu ici (au nom de vfs ; msg sauvé), puis f_spawn_file
 * d'AppKit -> pid ou erreur */
static long spawn_local(const char *path, const char *arg)
{
	memmove(&msg_save, &msg, MSG_SIZE);
	msg.op = V_EXEC;
	msg.pid = (u32)own_pid;
	msg.data[0] = 0;
	str_cat((char *)msg.data, path);
	long r = ops_chemins();
	if (r >= 0)
		r = f_spawn_file((long *)&msg, arg, 0); /* (entrées / sorties de vfs) */
	memmove(&msg, &msg_save, MSG_SIZE);
	return r;
}

/* volume (« RAM0 » ou « ram0 », « : » final permis), source, service, rootpath (0 ou
 * « / » : la racine de la source), share (0, ou le volume dont il prend le service :
 * BOOT:) : lance SD0:/sys/svc/<service> « <volume> <source> » et l'inscrit ; une
 * source « blk.* » déjà montée par ce service le partage (jamais deux services sur
 * une partition) ; main = 1 : appelé par le thread principal (lancement local) -> 0
 * ou erreur (rootpath absent : E_NOENT, pas un dossier : E_NOTDIR, « .. » : E_INVAL) */
static long monter(const char *vol, const char *src, const char *svc, const char *rootp, struct volume *share,
		   int main_thread)
{
	char root[ROOT_MAX];
	u64 n = 0; /* nom en minuscules, sans « : » */
	for (; vol[n] && vol[n] != ':' && n < 14; n++) {
		char c = vol[n];
		if (c >= 'A' && c <= 'Z')
			c += 32;
		nom_vol[n] = c;
	}
	nom_vol[n] = 0;
	if (!n)
		return E_INVAL;
	if (volume_de(nom_vol, 0)) /* déjà monté ? */
		return E_EXIST;
	if (racine_normaliser(rootp, root))
		return E_INVAL;
	char bsrc[24], bsvc[16]; /* (comme gardés dans la table) */
	copie_bornee(bsrc, src, 23);
	copie_bornee(bsvc, svc, 15);
	if (!share && bsrc[0] == 'b' && bsrc[1] == 'l' && bsrc[2] == 'k' && bsrc[3] == '.')
		for (int i = 0; i < VOL_MAX && !share; i++) { /* la partition est-elle déjà servie ? */
			struct volume *e = &volumes[i];
			if (e->name[0] && e->chan >= 0 && !strcmp(e->src, bsrc) && !strcmp(e->fs, bsvc))
				share = e;
		}
	long ch, pid;
	int launched = 0;
	if (share) { /* le même service, le même cache */
		ch = share->chan;
		pid = share->pid;
	} else {
		str_cat(str_cat(chemin, "SD0:/sys/svc/"), svc);
		str_cat(str_cat(str_cat(args, nom_vol), " "), src);
		/* un module de l'initramfs (services de démarrage), sinon le disque */
		pid = korin_sys2(SYS_MOD_LAUNCH, svc, args);
		if (pid < 0) {
			if (main_thread) /* (le thread principal sert les requêtes : pas de requête à soi) */
				pid = spawn_local(chemin, args);
			else
				pid = ksys(SYS_SPAWN, (long)chemin, (long)args, 0, 0, 0, 0);
		}
		chemin[0] = 0; /* (tampons rendus vides) */
		args[0] = 0;
		if (pid < 0)
			return E_NOENT;
		str_cat(str_cat(nom_svc, "fs."), nom_vol); /* « fs.<volume> » */
		ch = korin_sys2(SYS_SVC_OPEN, nom_svc, 3000);
		nom_svc[0] = 0;
		if (ch < 0)
			return E_NOENT;
		launched = 1;
	}
	if (root[0]) { /* le dossier racine doit exister */
		long r = racine_verifier(ch, root);
		if (r) {
			if (launched) /* (son service ne sert à rien d'autre) */
				korin_sys1(SYS_KILL, pid);
			return r;
		}
	}
	struct volume *v = 0; /* place libre */
	for (int i = 0; i < VOL_MAX && !v; i++)
		if (!volumes[i].name[0])
			v = &volumes[i];
	if (!v)
		return E_NOMEM;
	if (share) /* le nom de son service : celui du volume qui l'a lancé */
		strcpy(v->svcvol, share->svcvol);
	else
		strcpy(v->svcvol, nom_vol);
	v->chan = ch;
	v->pid = pid;
	v->idxok = 0; /* index : lu à la première question (tampon gardé) */
	v->idxn = 0;
	strcpy(v->root, root);
	strcpy(v->name, nom_vol);
	strcpy(v->src, bsrc); /* source et service (pour VFS_VOLUMES) */
	strcpy(v->fs, bsvc);
	vol_publier(v, 0); /* sur le bus */
	return 0;
}

/* mot -> coupé au premier espace ; le mot suivant (0 : aucun) */
static char *mot_suivant(char *p)
{
	while (*p && *p != ' ')
		p++;
	if (!*p)
		return 0;
	*p++ = 0;
	while (*p == ' ')
		p++;
	return *p ? p : 0;
}

/* « volume source service [rootpath] » (modifiée) -> 0 ou erreur */
static long monter_ligne(char *line)
{
	char *src = mot_suivant(line);
	if (!src)
		return E_INVAL;
	char *svc = mot_suivant(src);
	if (!svc)
		return E_INVAL;
	char *root = mot_suivant(svc);
	if (root)
		mot_suivant(root); /* (un cinquième mot : ignoré) */
	return monter(line, src, svc, root, 0, 1); /* (thread principal) */
}

static void journal(const char *what, long r)
{
	ak_puts("vfs : montage ");
	ak_puts(what);
	ak_puts(" -> ");
	print_dec(r);
	ak_puts("\n");
}

/* (msg) VFS_VOLINFO : données « vol » (minuscules) -> données : le volume dont le
 * service porte le nom (« fs.<vol> »), 0, son rootpath (« / », « /korinARM »), 0 */
static long info_volume(void)
{
	((u8 *)&msg)[MSG_SIZE - 1] = 0;
	struct volume *v = volume_de((char *)msg.data, 0);
	if (!v)
		return E_NOENT;
	u8 *o = msg.data;
	o = (u8 *)str_cat((char *)o, v->svcvol) + 1;
	*o++ = '/';
	o = (u8 *)str_cat((char *)o, v->root) + 1;
	msg.len = (u32)(o - msg.data);
	msg.a = v->chan;
	return 0;
}

/* ---- SD0:/etc/mounts.txt (src/inc/applist.inc : ini_read, ini_trim, ini_field) */

static char *ini_trim(char *s)
{
	while (*s == ' ' || *s == 9)
		s++;
	char *e = s + strlen(s);
	while (e > s && (u8)e[-1] <= ' ')
		*--e = 0;
	return s;
}

/* « a, b, c » -> premier champ (sans espaces), *next la suite (0 : aucune) */
static char *ini_field(char *s, char **next)
{
	char *p = s;
	while (*p && *p != ',')
		p++;
	if (*p) {
		*p = 0;
		*next = p + 1;
	} else {
		*next = 0;
	}
	return ini_trim(s);
}

/* chemin, fonction (clé en minuscules, valeur) pour chaque ligne « clé = valeur »
 * -> 0 si le fichier manque */
static long ini_read(const char *path, void (*fn)(char *key, char *val))
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, ini_buf, INI_MAX - 1);
	sys_close(fd);
	if (n < 0)
		n = 0;
	ini_buf[n] = 0;
	char *l = ini_buf;
	while (*l) {
		char *e = l; /* fin de ligne -> 0 ; « # » : commentaire */
		while (*e && *e != 10) {
			if (*e == '#')
				*e = 0;
			e++;
		}
		char last = *e;
		*e = 0;
		char *eq = l; /* clé = valeur ? */
		while (*eq && *eq != '=')
			eq++;
		if (*eq) {
			*eq = 0;
			char *key = ini_trim(l);
			for (char *k = key; *k; k++)
				if (*k >= 'A' && *k <= 'Z')
					*k += 32;
			fn(key, ini_trim(eq + 1));
		}
		if (!last)
			break;
		l = e + 1;
	}
	return 1;
}

/* volume (minuscules), « source, service [, rootpath] » */
static void montage_ligne(char *vol, char *val)
{
	char *rest;
	char *src = ini_field(val, &rest);
	if (!rest)
		return;
	char *svc = ini_field(rest, &rest);
	char *root = rest ? ini_field(rest, &rest) : 0;
	char *flag = rest ? ini_field(rest, &rest) : 0;
	if (flag && !strcmp(flag, "ondemand")) /* (BOOT: : monté par VFS_BOOT_BEGIN seulement) */
		return;
	long r = monter(vol, src, svc, root, 0, 0); /* (thread des montages) */
	journal(vol, r); /* au journal : « vfs : montage ram0 -> 0 » */
}

/* volume : démonté. Un volume dont le service en sert un autre (BOOT: et SD0:)
 * quitte seulement la table ; sinon son service s'arrête (sync = 1 : FS_SYNC
 * d'abord, et le volume quitte la table tout de suite) */
static void demonter(struct volume *v, int sync)
{
	int shared = 0;
	for (int i = 0; i < VOL_MAX; i++)
		if (&volumes[i] != v && volumes[i].name[0] && volumes[i].pid == v->pid)
			shared = 1;
	if (shared || sync) { /* il quitte la table */
		vol_publier(v, 1);
		v->idxok = 0;
		v->name[0] = 0;
	}
	if (shared)
		return;
	if (sync) { /* (comme eject : les entrées des fichiers ouverts réécrites) */
		struct msg m __attribute__((aligned(16)));
		memset(&m, 0, sizeof m);
		m.op = FS_SYNC;
		korin_sys2(SYS_SVC_CALL, v->chan, &m);
	}
	korin_sys1(SYS_KILL, v->pid); /* son service s'arrête : le volume part à l'avis de fin */
}

/* ---- BOOT: à la demande (VFS_BOOT_BEGIN, VFS_BOOT_END) */

#define BOOT_USERS 8
static struct {
	long pid, n;
} boot_users[BOOT_USERS];
static int boot_auto; /* 1 : BOOT: monté ici, à la demande */
static long boot_end(long pid, int all);
static char boot_line[INI_MAX];
static char boot_src[24], boot_svc[16], boot_root[ROOT_MAX];

/* SD0:/etc/mounts.txt (lu par le service de SD0: : le thread principal ne
 * s'envoie pas de requête) : la ligne « BOOT = source, service, rootpath,
 * ondemand » -> 1, boot_src, boot_svc, boot_root ; 0 : aucune */
static int boot_decrit(void)
{
	struct volume *sd = volume_de("sd0", 0);
	if (!sd)
		return 0;
	long n = vol_lire(sd, "etc/mounts.txt", boot_line, INI_MAX - 1);
	if (n <= 0)
		return 0;
	boot_line[n] = 0;
	for (char *l = boot_line; *l;) {
		char *e = l;
		while (*e && *e != 10)
			e++;
		char last = *e;
		*e = 0;
		for (char *c = l; *c; c++)
			if (*c == '#')
				*c = 0;
		char *eq = l;
		while (*eq && *eq != '=')
			eq++;
		if (*eq) {
			*eq = 0;
			char *key = ini_trim(l);
			if ((key[0] | 0x20) == 'b' && (key[1] | 0x20) == 'o' && (key[2] | 0x20) == 'o' &&
			    (key[3] | 0x20) == 't' && !key[4]) {
				char *rest;
				char *src = ini_field(eq + 1, &rest);
				char *svc = rest ? ini_field(rest, &rest) : 0;
				char *root = rest ? ini_field(rest, &rest) : 0;
				char *flag = rest ? ini_field(rest, &rest) : 0;
				if (*src && svc && *svc && flag && !strcmp(flag, "ondemand")) {
					copie_bornee(boot_src, src, 23);
					copie_bornee(boot_svc, svc, 15);
					copie_bornee(boot_root, root ? root : "", ROOT_MAX - 1);
					return 1;
				}
			}
		}
		if (!last)
			break;
		l = e + 1;
	}
	return 0;
}

/* (msg) pid : une demande de plus -> 0, ou E_NOMEM */
static long boot_prendre(long pid)
{
	for (int i = 0; i < BOOT_USERS; i++)
		if (boot_users[i].pid == pid) {
			boot_users[i].n++;
			return 0;
		}
	for (int i = 0; i < BOOT_USERS; i++)
		if (!boot_users[i].pid) {
			boot_users[i].pid = pid;
			boot_users[i].n = 1;
			return 0;
		}
	return E_NOMEM;
}

/* (msg) VFS_BOOT_BEGIN au nom de pid -> 0, données « BOOT: » (a = 1 : monté à la
 * demande, à rendre) ou « SD0: » (a = 0 : pas de BOOT: décrit) ; ou l'erreur */
static long boot_begin(long pid)
{
	struct volume *v = volume_de("boot", 0);
	msg.a = 0;
	if (v && !boot_auto) { /* monté par l'utilisateur (mount BOOT:) : tel quel */
		str_cat((char *)msg.data, "BOOT:");
		msg.len = 6;
		return 0;
	}
	if (!v) {
		if (!boot_decrit()) { /* pas de BOOT: : la racine du volume système */
			str_cat((char *)msg.data, "SD0:");
			msg.len = 5;
			return 0;
		}
		long r = monter("BOOT", boot_src, boot_svc, boot_root, 0, 1);
		journal("BOOT", r); /* « vfs : montage BOOT -> 0 » */
		if (r)
			return r;
		boot_auto = 1;
	}
	long r = boot_prendre(pid);
	if (r) {
		boot_end(pid, 0); /* (personne d'autre : démonté) */
		return r;
	}
	str_cat((char *)msg.data, "BOOT:");
	msg.len = 6;
	msg.a = 1;
	return 0;
}

/* pid, all = 1 : toutes ses demandes (fin du process) ; la dernière rendue :
 * BOOT: démonté -> 0 */
static long boot_end(long pid, int all)
{
	int busy = 0;
	for (int i = 0; i < BOOT_USERS; i++) {
		if (boot_users[i].pid == pid && (all || --boot_users[i].n <= 0)) {
			boot_users[i].pid = 0;
			boot_users[i].n = 0;
		}
		if (boot_users[i].pid)
			busy = 1;
	}
	if (busy || !boot_auto)
		return 0;
	boot_auto = 0;
	struct volume *v = volume_de("boot", 0);
	if (v) {
		demonter(v, 1);
		journal("BOOT (fin)", 0);
	}
	return 0;
}

/* (thread) les montages permanents de SD0:/etc/mounts.txt, puis le bus */
static THREAD_ENTRY void montages_thread(void *arg)
{
	(void)arg;
	ini_read("SD0:/etc/mounts.txt", montage_ligne);
	while (!bp_open(60000)) /* le bus : lancé par init2 */
		;
	for (int i = 0; i < VOL_MAX; i++) /* les volumes déjà montés */
		if (volumes[i].name[0])
			vol_publier(&volumes[i], 0);
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}

/* (msg) VFS_VOLUMES : les volumes montés -> b = nombre ; données : par volume,
 * nom (16 o), source (24 o), service (16 o) */
static long lister_volumes(void)
{
	u8 *o = msg.data;
	long n = 0;
	for (int i = 0; i < VOL_MAX; i++) {
		struct volume *v = &volumes[i];
		if (!v->name[0])
			continue;
		memcpy(o, v->name, 16);
		memcpy(o + 16, v->src, 24);
		memcpy(o + 40, v->fs, 16);
		o += VE_TAILLE;
		n++;
	}
	msg.b = n;
	return 0;
}

/* pid terminé : retire le volume qu'il servait */
static void oublier_pid(long pid)
{
	for (int i = 0; i < VOL_MAX; i++) {
		struct volume *v = &volumes[i];
		if (!v->name[0] || v->pid != pid)
			continue;
		vol_publier(v, 1); /* retiré du bus */
		v->idxok = 0; /* (son index sera relu : tampon gardé) */
		v->name[0] = 0;
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	canal = korin_sys2(SYS_SVC_REGISTER, "vfs", SVF_ENDS | SVF_BIRTHS);
	if (canal < 0)
		SVC_QUIT();
	own_pid = korin_sys0(SYS_GETPID);
	if (krt_line) { /* d'abord le volume donné (SD0:) */
		u64 n = strlen(krt_line) + 1;
		char *line = ak_malloc(n);
		memcpy(line, krt_line, n);
		long r = monter_ligne(line);
		journal(line, r); /* (la ligne coupée : son premier mot, comme l'asm) */
	}
	u8 *stack = ak_malloc(16384); /* mounts.txt : dans un thread (ses services se
					 chargent depuis un volume que vfs doit servir) */
	if (stack)
		korin_sys3(SYS_THREAD_CREATE, montages_thread, stack + 16384, 0);
	for (;;) {
		waits_tick(); /* lectures de tubes trop longues */
		long wait = waits_any() ? 500 : 60000;
		long req = korin_sys3(SYS_SVC_NEXT, canal, &msg, wait);
		if (req < 0)
			SVC_QUIT();
		if (!req)
			continue;
		u64 op = msg.op;
		long r;
		if (op == SVC_OP_END) { /* un process terminé */
			boot_end(msg.a, 1); /* un écrivain de BOOT: : sa demande est rendue */
			oublier_pid(msg.a); /* un service de fichiers ? son volume disparaît */
			oublier_dossier(msg.a); /* son dossier courant est oublié */
			ends_pid_gone(msg.a); /* ses bouts de tubes sont fermés */
			pid_forget(msg.a); /* sa fiche est rendue */
			continue;
		}
		if (op == SVC_OP_BIRTH) { /* le fils reçoit le dossier du père, ses entrées / sorties */
			naissance(msg.a, msg.b);
			stdio_birth(msg.a, msg.b, msg.c, msg.d, *(i64 *)msg.data);
			continue;
		}
		if (op == SVC_OP_FORK) { /* le dossier du père, ses bouts de tubes, ses entrées / sorties */
			naissance(msg.a, msg.b);
			if (ends_fork(msg.a, msg.b))
				ak_puts("vfs : fork, table des bouts pleine : le fils n'a pas tous les tubes du pere\n"); /* (texte de l'asm) */
			continue;
		}
		if (op == SVC_OP_EXEC) { /* exec : ses entrées / sorties */
			stdio_exec(msg.a, msg.c, msg.d, *(i64 *)msg.data);
			continue;
		}
		if (op == V_PIPE_ROOM || (op >= V_PIPE && op <= V_STDIO)) { /* tubes, entrées / sorties */
			current_req = req;
			int deferred;
			r = ops_pipes(&deferred);
			if (deferred) /* réponse différée */
				continue;
		} else if ((op >= V_OPEN && op <= V_EXEC) || op == V_UTIME || op == V_RENAME || op == V_STATX ||
			   op == V_CHMOD) { /* chemins des programmes */
			current_req = req; /* (SVC_FORWARD : vers_volume) */
			r = ops_chemins();
			if (answered) /* transmise : le service répond */
				continue;
		} else if (op == V_FILE_END || op == V_END_FILE) {
			r = ops_bouts();
		} else if (op == VFS_MOUNT) { /* « volume source service » */
			((u8 *)&msg)[MSG_SIZE - 1] = 0;
			r = monter_ligne((char *)msg.data);
		} else if (op == VFS_UNMOUNT) { /* données : « vol » (minuscules) */
			struct volume *v = volume_de((char *)msg.data, 0);
			r = E_NOENT;
			if (v) {
				demonter(v, 0);
				r = 0;
			}
		} else if (op == VFS_BOOT_BEGIN) {
			r = boot_begin(msg.pid);
		} else if (op == VFS_BOOT_END) {
			r = boot_end(msg.pid, 0);
		} else if (op == VFS_VOLINFO) {
			r = info_volume();
		} else if (op == VFS_PATHMAP) {
			r = chemin_ajouter();
		} else if (op == VFS_PATHQ) {
			((u8 *)&msg)[MSG_SIZE - 1] = 0;
			const char *rest;
			r = chemin_virtuel((char *)msg.data, &rest) != 0;
		} else if (op == VFS_VOLUMES) {
			r = lister_volumes();
		} else {
			r = E_INVAL;
		}
		msg.op = r;
		korin_sys2(SYS_SVC_REPLY, req, &msg);
	}
}
