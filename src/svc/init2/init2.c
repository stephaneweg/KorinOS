/* init2 - étapes 2 et 3 du démarrage (docs/FEUILLE-DE-ROUTE.md, section 1),
 * repris de init2.asm ; lancé par init une fois / servi. Commun au Pi et au
 * PC ; le redémarrage matériel est dans la couche de chaque machine
 * (init2_aarch64.c : bloc PM du Pi 4 ; init2_x86_64.c).
 *  * étape 2 : busd (le bus d'événements, docs/BUS.md), hwmanager (découverte
 *    du matériel), hidmanager (les entrées), les bus de la machine
 *    (plat_buses : usbd sur le Pi, annonces de pcid sur le PC), le shell
 *    (Elegant : /sys/elegant, qui lit shell= de /etc/system.ini), puis
 *    les services de /etc/services.txt ;
 *  * étape 3 : quand Elegant répond (le service « ws »), les programmes de
 *    /etc/startup.txt (sans ce fichier : Bienvenue) ;
 *  * sans interface graphique (plat_text_console : sur le PC, shell=console ou
 *    pas d'Elegant ; jamais sur le Pi, où Elegant décide lui-même), ni Elegant
 *    ni startup.txt : la console texte (/sys/svc/console, un shell sur un
 *    pseudo-terminal), lancée après services.txt et relancée si elle tombe ;
 *  * avant tout cela, s'il y a des paquets étagés (/var/pkg/stage,
 *    docs/PAQUETS.md) : /bin/pkg commit, qu'il attend en servant ses
 *    demandes (ses kits, le redémarrage si le noyau a changé) ;
 *  * ensuite il veille : un service qu'il a lancé et qui s'arrête sur une
 *    erreur (code de sortie non nul) est relancé, RELANCES_MAX fois au plus ;
 *    toutes les 30 min, l'heure est gardée dans /etc/lastclock.
 * Il s'enregistre sous « init2 » (prévenu des fins de process : SVF_ENDS).
 *  * INIT_OP_LAUNCH : lance un programme de /sys pour un process sans
 *    droits ; données : chemin résolu par vfs (« /sys/... »), 0, arguments,
 *    0 -> pid. Refus : chemin hors de /sys, ou un service de /sys/svc/ demandé par un compte (E_PERM, E33). Chaque demande est
 *    notée au journal ;
 *  * INIT_OP_KIT_LOAD : inscrit un kit pour un process sans droits ; données :
 *    le nom (« netkit ») -> 0 ou erreur (f_kit_load d'AppKit) ;
 *  * INIT_OP_REBOOT : a bit 0 = tryboot, bit 1 = arrêt ;
 *  * INIT_OP_SVC_STOP, INIT_OP_SVC_START (appelant en uid 0 : Réglages élevé,
 *    volet Démarrage, docs/DEMARRAGE.md) : données = une ligne de
 *    services.txt ; arrête le service de cette ligne sans le relancer, ou le
 *    lance (surveillé) ;
 *  * le type de démarrage (docs/DEMARRAGE.md) : une ligne de services.txt
 *    écrite aussi dans /etc/services.off (désactivé) ou /etc/services.manual
 *    (manuel) n'est pas lancée au démarrage ; un service manuel l'est quand
 *    un service qui en a besoin démarre (/etc/services.deps), jamais un
 *    désactivé ; startup.txt : session.off et session.manual (personnels,
 *    conf_path) ;
 *  * après startup.txt : init2 se relance avec l'argument « preload », qui
 *    précharge les exécutables de /etc/preload.txt et s'arrête ;
 *  * les tâches planifiées (docs/TACHES.md) : /sys/svc/taskd lancé après les
 *    services (surveillé) ; à l'ouverture de la session, « taskd phase
 *    login » ; avant de fermer la session ou d'éteindre, « taskd phase
 *    logout » ou « taskd phase shutdown », attendu (TASKS_PHASE_MAX ms au
 *    plus) en servant les demandes. */
#include "init2.h"
#include "caller.h"

#define BLK_DMA 5 /* (src/inc/blk.inc) */
#define BLK_UHS 8
#define VFS_PATHMAP 23 /* (src/inc/fs.inc) */

#define SURV_MAX 24 /* services surveillés */
#define RELANCES_MAX 3
#define LISTE_MAX 4096
#define ATTENTE_WS 20000 /* ms : le shell doit répondre */
#define HEURE_TXT 48
#define HEURE_MINUTES 30 /* sauvegarde de l'heure */
#define COMMIT_MAX 300000 /* ms : pkg commit, au démarrage */
#define PRELOAD_NOTIFY_MS 300 /* (fasmarm, hello... : pas de notification) */
#define TASKD "/sys/svc/taskd"
#define TASKS_PHASE_MAX 65000 /* ms : les tâches de l'extinction (taskd en arrête au bout de 60 s) */

struct surv {
	u64 pid;
	u64 relances;
	char ligne[128]; /* « chemin [arguments] » */
};

static long canal = -1;
static struct surv surv[SURV_MAX];
static char ligne[128];
u8 msg[512] __attribute__((aligned(16)));
static char heure_txt[HEURE_TXT];
static u64 veilles;
static char liste[LISTE_MAX];
static char launch_path[256];
static char launch_args[256];
static u8 plist[64];

static long spawn(const char *path, const char *args)
{
	return ksys(SYS_SPAWN, (long)path, (long)args, 0, 0, 0, 0); /* (file_sys d'AppKit, comme sys) */
}

static int session_uid; /* 1 : lancer_ligne lance avec l'uid du compte de la session (startup.txt) */

/* « chemin [arguments] », watch = 1 : à surveiller -> pid ou erreur */
long lancer_ligne(const char *line, int watch)
{
	str_copy_n(ligne, line, 128); /* copie : la coupure ne touche pas l'original (relances) */
	char *args = 0;
	for (char *c = ligne; *c; c++)
		if (*c == ' ') {
			*c = 0;
			args = c + 1;
			break;
		}
	long pid = session_uid ? spawn_session(ligne, args) : spawn(ligne, args);
	say("init2 : "); /* « init2 : /sys/elegant -> 7 » */
	say(line);
	say(" -> ");
	say_dec(pid);
	say("\n");
	if (pid < 0 || !watch)
		return pid;
	for (int i = 0; i < SURV_MAX; i++) /* à surveiller : une place libre */
		if (!surv[i].pid) {
			surv[i].pid = (u64)pid;
			surv[i].relances = 0;
			str_copy_n(surv[i].ligne, line, 128);
			break;
		}
	return pid;
}

/* le type de démarrage (docs/DEMARRAGE.md ; list_load, list_has de korinrt) : les services désactivés
 * (/etc/services.off), manuels (/etc/services.manual), leurs dépendances (/etc/services.deps : « chemin = ce
 * qu'il lui faut ») et services.txt tel quel (pour trouver la ligne d'une dépendance) ; pour startup.txt,
 * session.off et session.manual à la suite */
#define DEP_PROF 4 /* dépendances de dépendances */
static char off_list[2048];
static char man_list[2048];
static char deps_txt[2048];
static char svc_txt[LISTE_MAX];
static char sess_list[4096];

static void services_lists(void)
{
	list_load("/etc/services.off", off_list, sizeof off_list);
	list_load("/etc/services.manual", man_list, sizeof man_list);
	list_load("/etc/services.deps", deps_txt, sizeof deps_txt);
	list_load("/etc/services.txt", svc_txt, sizeof svc_txt);
}

/* deux lignes égales, sans les blancs autour -> 1 */
static int same_line(const char *a, const char *b);

/* la ligne (sans blancs autour) qui tourne, surveillée -> 1 */
static int line_running(const char *line)
{
	for (int i = 0; i < SURV_MAX; i++)
		if (surv[i].pid && same_line(surv[i].ligne, line))
			return 1;
	return 0;
}

/* [p, fin de mot) == path (jusqu'au blanc, au « = » ou à la fin de ligne) -> 1 */
static int word_is(const char *p, const char *path)
{
	u64 n = str_len(path);
	if (memcmp(p, path, n))
		return 0;
	char c = p[n];
	return !c || c == ' ' || c == '\t' || c == '=' || c == '\r' || c == '\n';
}

/* path (« /sys/svc/netd ») -> sa ligne de services.txt dans out (128), 1 si trouvée (pas en commentaire) */
static int service_line(const char *path, char *out)
{
	for (const char *p = svc_txt; *p;) {
		while (*p == ' ' || *p == '\t')
			p++;
		const char *e = p;
		while (*e && *e != '\n' && *e != '\r')
			e++;
		if (*p != '#' && word_is(p, path)) {
			u64 n = (u64)(e - p) < 127 ? (u64)(e - p) : 127;
			memcpy(out, p, n);
			out[n] = 0;
			return 1;
		}
		while (*e == '\n' || *e == '\r')
			e++;
		p = e;
	}
	return 0;
}

/* line : ses dépendances de services.deps lancées d'abord (sauf désactivées, ou qui tournent déjà) */
static void lancer_deps(const char *line, int prof)
{
	if (prof >= DEP_PROF)
		return;
	char path[128];
	u64 n = 0;
	while (line[n] && line[n] != ' ' && n < 127)
		n++;
	memcpy(path, line, n);
	path[n] = 0;
	for (const char *p = deps_txt; *p;) { /* la ligne « path = a b » */
		while (*p == ' ' || *p == '\t')
			p++;
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		if (*p != '#' && word_is(p, path)) {
			const char *q = p + n;
			while (q < e && *q != '=')
				q++;
			while (q < e) { /* chaque dépendance */
				while (q < e && (*q == '=' || *q == ' ' || *q == '\t' || *q == '\r'))
					q++;
				char dep[128];
				u64 k = 0;
				while (q < e && *q != ' ' && *q != '\t' && *q != '\r' && k < 127)
					dep[k++] = *q++;
				dep[k] = 0;
				char dl[128];
				if (!k || !service_line(dep, dl) || line_running(dl))
					continue;
				if (list_has(off_list, dl)) {
					say("init2 : dependance desactivee : ");
					say(dl);
					say("\n");
					continue;
				}
				lancer_deps(dl, prof + 1);
				lancer_ligne(dl, 1);
			}
			return;
		}
		p = *e ? e + 1 : e;
	}
}

/* fichier (une ligne par programme), watch, off = lignes sautées (0 : aucune), man = lignes manuelles (0 :
 * aucune ; avec man, les dépendances de chaque ligne sont lancées avant elle) -> 1 si le fichier existe */
static int lancer_fichier(const char *path, int watch, const char *off, const char *man)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, liste, LISTE_MAX - 1);
	if (n < 0)
		n = 0;
	sys_close(fd);
	liste[n] = 0;
	char *l = liste;
	while (*l) {
		char *e = l; /* fin de ligne -> 0 */
		while (*e && *e != 10 && *e != 13)
			e++;
		if (*e)
			*e++ = 0;
		if (*l && *l != '#') { /* vide, ou commentaire */
			if (off && list_has(off, l)) {
				say("init2 : desactive : ");
				say(l);
				say("\n");
			} else if (man && list_has(man, l)) {
				say("init2 : manuel : ");
				say(l);
				say("\n");
			} else if (!man || !line_running(l)) { /* (déjà lancé comme dépendance) */
				if (man)
					lancer_deps(l, 0);
				lancer_ligne(l, watch);
			}
		}
		l = e;
	}
	return 1;
}

/* pid terminé, son code de sortie : un service surveillé qui s'arrête sur une
 * erreur est relancé (RELANCES_MAX fois) */
static void fin_de(u64 pid, u64 code)
{
	for (int i = 0; i < SURV_MAX; i++) {
		struct surv *s = &surv[i];
		if (s->pid != pid) /* (pid 0 : une place libre, comme l'asm) */
			continue;
		s->pid = 0; /* il ne tourne plus */
		if (!code)
			return; /* arrêt normal (code 0) : rien à faire */
		if (s->relances >= RELANCES_MAX) {
			say("init2 : abandon (trop de relances) : ");
			say(s->ligne);
			say("\n");
			if (!strcmp(s->ligne, "/sys/elegant"))
				plat_shell_lost(); /* (PC : la console texte en secours) */
			return;
		}
		u64 r = s->relances + 1;
		say("init2 : relance d'un service arrete sur une erreur\n");
		long np = lancer_ligne(s->ligne, 0); /* (relance : copiée, sans nouvelle place) */
		if (np < 0)
			return;
		s->pid = (u64)np;
		s->relances = r;
		return;
	}
}

/* ---- l'heure : /etc/lastclock « secondes_UTC décalage » */

/* texte -> nombre (signé) ; *ps après lui */
static long lire_dec(const char **ps)
{
	const char *s = *ps;
	long v = 0;
	int neg = 0;
	while (*s == ' ')
		s++;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	while ((u8)(*s - '0') <= 9)
		v = v * 10 + (*s++ - '0');
	*ps = s;
	return neg ? -v : v;
}

static char *ecrire_dec(long v, char *d)
{
	u64 u = (u64)v;
	if (v < 0) {
		*d++ = '-';
		u = (u64)-v;
	}
	char *b = d;
	do {
		*d++ = (char)('0' + u % 10);
		u /= 10;
	} while (u);
	for (char *e = d - 1; b < e; b++, e--) { /* les chiffres sont à l'envers */
		char t = *b;
		*b = *e;
		*e = t;
	}
	return d;
}

/* sans réseau, l'heure repart de la dernière sauvegarde : jamais en arrière
 * (docs/FICHIERS.md, dates) */
static void charger_heure(void)
{
	long fd = sys_open("/etc/lastclock", O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, heure_txt, HEURE_TXT - 1);
	heure_txt[n > 0 ? n : 0] = 0;
	sys_close(fd);
	const char *s = heure_txt;
	long secs = lire_dec(&s);
	long off = lire_dec(&s);
	korin_sys2(SYS_CLOCK_SET, secs, off);
}

/* l'horloge -> /etc/lastclock (rien tant que l'heure n'est pas connue) */
static void sauver_heure(void)
{
	u64 secs = (u64)korin_sys1(SYS_CLOCK_GET, 0);
	if (secs < 1000000000)
		return;
	long off = korin_sys1(SYS_CLOCK_GET, 1);
	char *p = ecrire_dec((long)secs, heure_txt);
	*p++ = ' ';
	p = ecrire_dec(off, p);
	*p++ = 10;
	long fd = sys_open("/etc/lastclock", O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return;
	sys_write(fd, heure_txt, (u64)(p - heure_txt));
	sys_close(fd);
}

/* ---- /etc/paths.txt : « /devtools/gcc = /devtools/gcc » -> VFS_PATHMAP à vfs */

static const char *cp; /* (x19 de l'asm) */
static char *cw;       /* (x20 : dans msg) */

static void chemins_blancs(void)
{
	while (*cp == ' ' || *cp == 9)
		cp++;
}

/* un mot de cp vers cw (dans msg, terminé par 0) */
static void chemins_mot(void)
{
	char *lim = (char *)msg + 512 - 2;
	for (;;) {
		char c = *cp;
		if (!c || c == ' ' || c == 9 || c == '=' || c == 13 || c == 10)
			break;
		cp++;
		if (cw < lim)
			*cw++ = c;
	}
	*cw++ = 0;
}

static void charger_chemins(void)
{
	long vfs = korin_sys2(SYS_SVC_OPEN, "vfs", 5000);
	if (vfs < 0)
		return;
	long fd = sys_open("/etc/paths.txt", O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, liste, LISTE_MAX - 1);
	if (n < 0)
		n = 0;
	sys_close(fd);
	liste[n] = 0;
	cp = liste;
	for (;;) {
		chemins_blancs();
		char c = *cp;
		if (!c)
			return;
		if (c != '#' && c != 13 && c != 10) {
			cw = (char *)msg + MSG_DATA;
			chemins_mot();
			chemins_blancs();
			if (*cp == '=') {
				cp++;
				chemins_blancs();
				chemins_mot();
				M64(msg, 0) = VFS_PATHMAP;
				M32(msg, MSG_LEN) = (u32)(cw - ((char *)msg + MSG_DATA));
				if (korin_sys2(SYS_SVC_CALL, vfs, msg) < 0)
					say("init2 : ligne de paths.txt refusee\n");
			}
		}
		for (;;) { /* ligne suivante */
			c = *cp;
			if (!c)
				return;
			cp++;
			if (c == 10)
				break;
		}
	}
}

/* la carte passe en DMA : maintenant seulement, le noyau a lu les options
 * (no_sd_dma) ; le démarrage lui-même reste en PIO */
static void sd_dma_request(void)
{
	static u8 m[MSG_SIZE] __attribute__((aligned(16))); /* (l'asm : sur la pile, sans mise à zéro) */
	long blk = korin_sys2(SYS_SVC_OPEN, "blk.sd0", 0);
	if (blk < 0)
		return;
	long fd = sys_open("/etc/no_sd_uhs", O_READ); /* le 1,8 V, avant le DMA, sauf si ce fichier
							     existe (essayé sur le PocketTerm : DDR50) */
	if (fd >= 0)
		sys_close(fd);
	else {
		M64(m, 0) = BLK_UHS;
		korin_sys2(SYS_SVC_CALL, blk, m);
	}
	M64(m, 0) = BLK_DMA;
	korin_sys2(SYS_SVC_CALL, blk, m);
}

/* ---- requêtes */

/* pid -> son nom (PROC_INFO : jusqu'à l'appel suivant), « ? » s'il est absent */
static const char *name_of_pid(u64 pid)
{
	if (korin_sys2(SYS_PROC_INFO, pid, plist))
		return "?";
	plist[24 + 31] = 0;
	return (const char *)plist + 24;
}

/* 1 si le chemin commence par « /sys/ » sans composant « .. » */
static int sys_path_ok(const char *p)
{
	const char *s = "/sys/";
	while (*s)
		if (*s++ != *p++)
			return 0;
	for (; *p; p++)
		if (p[-1] == '/' && p[0] == '.' && p[1] == '.')
			return 0;
	return 1;
}

/* « init2 : launch par 12 terminal : /sys/svc/echo -> 14 » */
static long launch_request(void)
{
	msg[511] = 0; /* (données terminées) */
	const char *path = (const char *)msg + 48;
	const char *args = path + str_len(path) + 1; /* arguments : après son 0 */
	if (!*args)
		args = 0;
	str_copy_n(launch_path, path, 256); /* (msg sert à la réponse) */
	if (args) {
		str_copy_n(launch_args, args, 256);
		args = launch_args;
	}
	u64 who = M32(msg, 44);
	const char *name = name_of_pid(who);
	long r = E_PERM;
	long uid = korin_sys1(SYS_PROC_UID, (long)who); /* (le Pi : E_NOSYS, uid 0) */
	/* l'uid voulu par un demandeur d'uid 0 (SYS_SPAWN_AS d'AppKit : c = INIT_LAUNCH_UID_TAG | uid) : pkg élevé qui relance
	 * le panneau sous le compte de la session (refresh_desktop) ; le noyau a la même règle pour SPAWN_AS */
	u64 want = M64(msg, 24);
	if (uid == 0 && (want >> 32) == (INIT_LAUNCH_UID_TAG >> 32))
		uid = (long)(u32)want;
	long real = korin_sys1(SYS_PROC_UID, (long)who);
	if (uid < 0 && uid != E_NOSYS)
		r = -3; /* E_SRCH (demandeur disparu, ou erreur : jamais lancé en uid 0 à sa place, E18) */
	else if (real > 0 && (real & 0x40000000))
		r = E_PERM; /* un bac à sable (process web de Jet) : rien de /sys (ni oauthd, ni le reste), étape 2 */
	else if (real > 0 && !memcmp(launch_path, "/sys/svc/", 9) && !str_eq(launch_path, "/sys/svc/oauthd"))
		r = E_PERM; /* un service (avec droits) : pour l'uid 0 seulement, plus pour une app (E33 ; svctest : sudo) ;
			     * sauf oauthd, le service d'un compte (oauth.<uid>), que NetKit lance à la demande sous l'uid
			     * de l'app (sans lui, Fichiers ne se connectait plus à Google Drive : 26/11/2026) */
	else if (sys_path_ok(launch_path)) { /* AppKit : droits (vfs : ATTR_SYS ; le noyau : init2 les a) */
		r = uid > 0 ? ksys(SYS_SPAWN_AS, (long)launch_path, (long)args, uid, 0, 0, 0) : E_NOSYS;
		if (r == E_NOSYS) /* (uid 0, ou AppKit sans SPAWN_AS) */
			r = spawn(launch_path, args);
	}
	say("init2 : launch par ");
	say_dec((long)who);
	say(" ");
	say(name);
	say(" : ");
	say(launch_path);
	say(" -> ");
	say_dec(r);
	say("\n");
	return r;
}

/* « init2 : kit_load par 12 monitor : netkit -> 0 » */
static long kit_request(void)
{
	msg[511] = 0;
	str_copy_n(launch_path, (const char *)msg + 48, 32); /* le nom (msg sert à la réponse) */
	u64 who = M32(msg, 44);
	const char *name = name_of_pid(who);
	long r = f_kit_load(launch_path); /* lu par vfs, inscrit par le noyau (nom vérifié par AppKit) */
	say("init2 : kit_load par ");
	say_dec((long)who);
	say(" ");
	say(name);
	say(" : ");
	say(launch_path);
	say(" -> ");
	say_dec(r);
	say("\n");
	return r;
}

static void requete(long req);

/* les tâches planifiées d'une phase (« logout », « shutdown » ; docs/TACHES.md) : taskd les lance et les attend ;
 * init2 sert les demandes pendant ce temps (msg est réutilisé : l'appelant a déjà lu le sien), TASKS_PHASE_MAX ms
 * au plus. Une demande d'extinction qui arrive pendant une phase ne relance pas de phase. */
static int in_phase;

static void tasks_phase(const char *which)
{
	long st[2];
	if (in_phase || canal < 0 || sys_stat(TASKD, st) < 0)
		return;
	char a[32];
	str_cat(str_cat(a, "phase "), which);
	long pid = spawn(TASKD, a);
	if (pid < 0)
		return;
	in_phase = 1;
	long t0 = sys_time_ms();
	while (sys_time_ms() - t0 < TASKS_PHASE_MAX) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, 500);
		if (req < 0)
			break;
		if (!req)
			continue;
		if (M64(msg, 0) == SVC_OP_END && M64(msg, 8) == (u64)pid) {
			fin_de(pid, M64(msg, 16));
			break;
		}
		requete(req);
	}
	in_phase = 0;
}

/* req = INIT_OP_REBOOT (a bit 0 : tryboot, bit 1 : arrêt) : répond, laisse 500 ms
 * aux dernières écritures (journal, fichiers fermés), puis la couche de la
 * machine redémarre ; revient si le redémarrage n'a pas eu lieu */
static void reboot_request(long req)
{
	u64 flags = M64(msg, 8);
	say("init2 : redémarrage demandé par le pid ");
	say_dec((long)M32(msg, 44));
	if (flags & 1)
		say(" (tryboot : tryboot.txt une fois)");
	say("\n");
	M64(msg, 0) = 0; /* la réponse d'abord */
	M32(msg, 40) = 0;
	korin_sys2(SYS_SVC_REPLY, req, msg);
	tasks_phase("shutdown"); /* les tâches de l'extinction (docs/TACHES.md) */
	sys_sleep(500);
	plat_reboot(flags);
	say("init2 : le redémarrage n'a pas eu lieu\n");
}

/* deux lignes égales, sans les blancs autour -> 1 */
static int same_line(const char *a, const char *b)
{
	while (*a == ' ' || *a == '\t')
		a++;
	while (*b == ' ' || *b == '\t')
		b++;
	u64 na = str_len(a), nb = str_len(b);
	while (na && (a[na - 1] == ' ' || a[na - 1] == '\t' || a[na - 1] == '\r' || a[na - 1] == '\n'))
		na--;
	while (nb && (b[nb - 1] == ' ' || b[nb - 1] == '\t' || b[nb - 1] == '\r' || b[nb - 1] == '\n'))
		nb--;
	return na == nb && !memcmp(a, b, na);
}

/* INIT_OP_LOGOUT : fermer la session (le dialogue d'extinction, docs/DESIGN-CONNEXION.md §3.4) ; l'appelant est du
 * compte de la session, ou d'uid 0. La réponse d'abord (l'appelant est de ceux qu'on arrête), puis ses process
 * (plat_logout), /tmp/session.ini effacé, /tmp/login.manual posé (l'écran de connexion se montre, même avec la
 * connexion automatique), startup.txt réarmé, et Elegant prévenu (WS_OP_SESSION_END) : il relance /sys/login */
#define WS_OP_SESSION_END 41 /* (src/inc/ws_ext.inc) */
static int startup_attendu;

static void logout_request(long req)
{
	long uid = user_session_uid();
	long who = korin_sys1(SYS_PROC_UID, (long)M32(msg, 44));
	long r = uid <= 0 ? E_NOENT : who != 0 && who != uid ? E_PERM : 0;
	say("init2 : fermeture de la session demandée par le pid ");
	say_dec((long)M32(msg, 44));
	say(" -> ");
	say_dec(r);
	say("\n");
	M64(msg, 0) = (u64)r;
	M32(msg, 40) = 0;
	korin_sys2(SYS_SVC_REPLY, req, msg);
	if (r)
		return;
	tasks_phase("logout"); /* les tâches de la fermeture de la session (docs/TACHES.md) */
	if (plat_logout(uid) < 0)
		return;
	sys_unlink("/tmp/session.ini");
	long fd = sys_open("/tmp/login.manual", O_WRITE | O_CREATE | O_TRUNC);
	if (fd >= 0)
		sys_close(fd);
	startup_attendu = 1;
	long ws = korin_sys2(SYS_SVC_OPEN, "ws", 2000);
	if (ws < 0) {
		say("init2 : Elegant ne répond pas : pas d'écran de connexion\n");
		return;
	}
	memset(msg, 0, 64);
	M64(msg, 0) = WS_OP_SESSION_END;
	M32(msg, 40) = 0;
	korin_sys2(SYS_SVC_CALL, ws, msg);
}

/* INIT_OP_SVC_STOP, INIT_OP_SVC_START (le volet Démarrage de Réglages) : données = la ligne de services.txt ;
 * l'appelant doit être en uid 0 (Réglages élevé ; le Pi, sans comptes : E_NOSYS, permis) ;
 * « init2 : svc_stop par 12 control : /sys/svc/telnetd -> 0 » */
static long service_request(u64 op)
{
	msg[511] = 0;
	str_copy_n(launch_path, (const char *)msg + 48, 128); /* (msg sert à la réponse) */
	u64 who = M32(msg, 44);
	long uid = korin_sys1(SYS_PROC_UID, (long)who);
	long r = E_PERM;
	if (uid == 0 || uid == E_NOSYS) { /* (uid 0, ou le Pi ; E_SRCH : refusé) */
		struct surv *found = 0; /* la place de cette ligne qui tourne */
		for (int i = 0; i < SURV_MAX; i++)
			if (surv[i].pid && same_line(surv[i].ligne, launch_path)) {
				found = &surv[i];
				break;
			}
		if (op == INIT_OP_SVC_STOP) {
			r = E_NOENT;
			if (found) {
				u64 pid = found->pid;
				found->pid = 0; /* plus surveillé : sa fin n'est pas une panne */
				korin_sys1(SYS_KILL, (long)pid);
				r = 0;
			}
		} else if (found)
			r = E_EXIST;
		else {
			services_lists(); /* (relues : le volet a pu les changer) */
			if (!sys_path_ok(launch_path) || list_has(off_list, launch_path))
				r = E_PERM; /* (désactivé : jamais lancé) */
			else {
				lancer_deps(launch_path, 0); /* ses dépendances d'abord (manuelles comprises) */
				r = lancer_ligne(launch_path, 1);
			}
		}
	}
	say(op == INIT_OP_SVC_STOP ? "init2 : svc_stop par " : "init2 : svc_start par ");
	say_dec((long)who);
	say(" ");
	say(name_of_pid(who));
	say(" : ");
	say(launch_path);
	say(" -> ");
	say_dec(r);
	say("\n");
	return r;
}

/* ---- INIT_OP_SETTING (décision du 25/11/2026) : Réglages change, pour le compte de la session et sans élévation,
 * quelques clés de /etc/options.txt et /etc/system.ini ; init2 (uid 0) les écrit, les valeurs vérifiées. Le reste
 * (autologin, access, comptes, accès à distance, fuseau, disposition par défaut, résolution) garde l'élévation */
#define INIT_OP_SETTING 7

static int set_ok(const char *v, const char *allowed, long max)
{
	long n = 0;
	for (; v[n]; n++) {
		const char *a = allowed;
		while (*a && *a != v[n])
			a++;
		if (!*a)
			return 0;
	}
	return n > 0 && n <= max;
}

static int set_word(const char *v, const char *a, const char *b)
{
	return str_eq(v, a) || str_eq(v, b);
}

/* « clé=valeur » à la place des lignes de la clé (à la fin), le reste gardé (comme ini_set de Réglages) */
static long set_line(const char *path, const char *key, const char *val)
{
	static char in[8192], out[8192 + 160];
	long kl = str_len(key), n = 0, fd = sys_open(path, O_READ);
	if (fd >= 0) {
		n = sys_read(fd, in, sizeof in - 1);
		sys_close(fd);
	}
	if (n < 0)
		n = 0;
	in[n] = 0;
	char *o = out, *p = in;
	while (*p) {
		char *e = p;
		while (*e && *e != '\n')
			e++;
		if (*e)
			e++;
		char *k = p;
		while (*k == ' ')
			k++;
		int same = 1;
		for (long i = 0; i < kl && same; i++)
			same = k[i] == key[i];
		if (same && (k[kl] == '=' || k[kl] == ' ')) {
			p = e;
			continue;
		}
		while (p < e)
			*o++ = *p++;
	}
	if (o != out && o[-1] != '\n') {
		*o++ = '\r';
		*o++ = '\n';
	}
	o = str_cat(str_cat(str_cat(o, key), "="), val);
	*o++ = '\r';
	*o++ = '\n';
	fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	sys_write(fd, out, (u64)(o - out));
	sys_close(fd);
	return 0;
}

static long setting_request(long req)
{
	if (!caller_session_or_admin(M32(msg, 44), req))
		return E_PERM;
	const char *f = (const char *)msg + 48, *end = (const char *)msg + MSG_SIZE;
	const char *k = f + str_len(f) + 1;
	if (k >= end)
		return E_INVAL;
	const char *v = k + str_len(k) + 1;
	if (v >= end || v + str_len(v) >= end)
		return E_INVAL;
	int ok = 0;
	const char *path = 0;
	if (str_eq(f, "options")) {
		path = "/etc/options.txt";
		/* (le pays du Wi-Fi ; le fuseau, la disposition par défaut et la résolution : par élévation, décision
		 * du 25/11/2026) */
		if (str_eq(k, "pays"))
			ok = set_ok(v, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 2) && str_len(v) == 2;
	} else if (str_eq(f, "system")) {
		path = "/etc/system.ini";
		if (str_eq(k, "lang"))
			ok = set_word(v, "fr", "en");
		else if (str_eq(k, "desktop"))
			ok = set_word(v, "classic", "overview");
		else if (str_eq(k, "shell"))
			ok = set_word(v, "desktop", "poche");
	}
	if (!ok)
		return E_INVAL;
	say("init2 : reglage ");
	say(k);
	say(" = ");
	say(v);
	say("\n");
	return set_line(path, k, v);
}

static void requete(long req)
{
	u64 op = M64(msg, 0);
	long r;
	if (op == SVC_OP_END) {
		fin_de(M64(msg, 8), M64(msg, 16)); /* pid, code de sortie */
		return;
	}
	if (op == INIT_OP_KIT_LOAD)
		r = kit_request();
	else if (op == INIT_OP_REBOOT) {
		if (!caller_session_or_admin(M32(msg, 44), req)) { /* (le compte de la session, ou l'uid 0 : E32) */
			M64(msg, 0) = (u64)E_PERM;
			M32(msg, 40) = 0;
			korin_sys2(SYS_SVC_REPLY, req, msg);
			return;
		}
		sauver_heure();
		reboot_request(req); /* (ne revient pas si tout va bien ; la réponse est déjà partie) */
		return;
	} else if (op == INIT_OP_LOGOUT) {
		logout_request(req); /* (la réponse est déjà partie) */
		return;
	} else if (op == INIT_OP_LAUNCH)
		r = launch_request();
	else if (op == INIT_OP_SVC_STOP || op == INIT_OP_SVC_START)
		r = service_request(op);
	else if (op == INIT_OP_SETTING)
		r = setting_request(req);
	else
		r = E_INVAL;
	M64(msg, 0) = (u64)r;
	M32(msg, 40) = 0;
	korin_sys2(SYS_SVC_REPLY, req, msg);
}

/* des paquets étagés (docs/PAQUETS.md) : /bin/pkg commit, avant tout le
 * reste ; init2 le sert pendant ce temps, COMMIT_MAX ms au plus. pkg absent ou
 * qui échoue : le démarrage continue, les paquets restent étagés. */
static void commit_boot(void)
{
	long st[4];
	if (sys_stat("/var/pkg/stage", st) < 0)
		return;
	long pid = spawn("/bin/pkg", "commit");
	if (pid < 0)
		return;
	long t0 = sys_time_ms();
	do {
		if (canal < 0)
			return;
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, 1000);
		if (req < 0)
			return;
		if (req) {
			if (M64(msg, 0) == SVC_OP_END && M64(msg, 8) == (u64)pid) /* la fin de pkg */
				return;
			requete(req);
		}
	} while ((u64)(sys_time_ms() - t0) < COMMIT_MAX);
}

/* ---- (init2 preload) /etc/preload.txt : chaque chemin est préchargé
 * (SYS_PRELOAD) ; au journal : un échec par ligne, puis le total et la durée */
static void precharger(void)
{
	if (ksys(SYS_PRELOAD, 0, 0, 0, 0, 0, 0) < 0) /* le noyau précharge-t-il ? (no_preload) */
		return;
	long fd = sys_open("/etc/preload.txt", O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, liste, LISTE_MAX - 1);
	if (n < 0)
		n = 0;
	sys_close(fd);
	liste[n] = 0;
	long t0 = sys_time_ms(), count = 0;
	char *l = liste;
	while (*l) {
		char *e = l; /* fin de ligne -> 0 */
		while (*e && *e != 10 && *e != 13)
			e++;
		char *next = e;
		if (*e)
			*next++ = 0;
		for (char *b = e - 1; b >= l && (*b == ' ' || *b == 9 || !*b); b--) /* espaces de fin */
			*b = 0;
		while (*l == ' ' || *l == 9) /* espaces de début */
			l++;
		if (*l && *l != '#') { /* vide, ou commentaire */
			long t1 = sys_time_ms();
			long r = ksys(SYS_PRELOAD, (long)l, 0, 0, 0, 0, 0);
			if (r < 0) { /* « init2 : preload /x -> -2 » */
				say("init2 : preload ");
				say(l);
				say(" -> ");
				say_dec(r);
				say("\n");
			} else {
				count++;
				if ((u64)(sys_time_ms() - t1) >= PRELOAD_NOTIFY_MS) /* un chargement qui se voit */
					preload_notify(l);
			}
		}
		l = next;
	}
	say("init2 : preload : "); /* « init2 : preload : 2 image(s) en 85 ms » */
	say_dec(count);
	say(" image(s) en ");
	say_dec(sys_time_ms() - t0);
	say(" ms\n");
}

/* startup.txt (personnel, sinon /etc ; sans : rien, Bienvenue n'est plus lancé d'office, 16/11), sous le
 * compte de la session, une fois qu'elle est ouverte : /tmp/session.ini écrit par l'écran de connexion
 * (/sys/login, lancé par Elegant ; docs/DESIGN-CONNEXION.md) ; sans /sys/login (le Pi) : tout de suite */
static void startup_si_ouverte(void)
{
	long st[2];
	if (sys_stat("/tmp/session.ini", st) < 0 && sys_stat("/sys/login", st) >= 0)
		return;
	startup_attendu = 0;
	static int boot_ok;
	if (!boot_ok) { /* A/B (docs/etude/MISES-A-JOUR-AB.md) : le démarrage a réussi, la version en essai garde ses essais */
		boot_ok = 1;
		if (sys_stat("/bin/pkg", st) >= 0)
			spawn("/bin/pkg", "bootok");
	}
	session_uid = 1;
	long su = user_session_uid(); /* (les fichiers du compte ouvert, pas ceux du HOME: d'init2, E30) */
	long n = list_load(conf_path_uid(su, "session.off"), sess_list, sizeof sess_list / 2); /* désactivés, puis manuels */
	sess_list[n] = 10;
	list_load(conf_path_uid(su, "session.manual"), sess_list + n + 1, (long)sizeof sess_list - n - 1);
	lancer_fichier(conf_path_uid(su, "startup.txt"), 0, sess_list, 0);
	session_uid = 0;
	if (sys_stat(TASKD, st) >= 0)
		spawn(TASKD, "phase login"); /* les tâches de l'ouverture de la session (docs/TACHES.md) */
}

int main(int argc, char **argv)
{
	if ((krt_line && !strcmp(krt_line, "preload")) || (argc == 2 && !strcmp(argv[1], "preload"))) {
		precharger(); /* « preload » : le préchargement */
		return 0;
	}
	canal = korin_sys2(SYS_SVC_REGISTER, "init2", SVF_ENDS); /* prévenu des fins de process */
	charger_heure(); /* l'horloge, puis les chemins virtuels, avant tout le reste */
	charger_chemins();
	sd_dma_request();
	commit_boot(); /* les paquets étagés, avant tout le reste */
	lancer_ligne("/sys/svc/busd", 1); /* le bus d'événements, avant ceux qui y publient */
	{ /* l'élévation (authd, docs/etude/MULTI-UTILISATEUR.md §2.6) : lancée ici, pas par services.txt, qu'une
	   * mise à jour ne change pas quand l'utilisateur l'a modifié (Réglages : serveur web, partage) */
		long st[2];
		if (sys_stat("/sys/svc/authd", st) >= 0 && korin_sys2(SYS_SVC_OPEN, "auth", 0) < 0)
			lancer_ligne("/sys/svc/authd", 1);
		/* le coffre à secrets (docs/KEYVAULT.md) : authd lui passe le mot de passe à la connexion */
		if (sys_stat("/sys/svc/keyvault", st) >= 0 && korin_sys2(SYS_SVC_OPEN, "keyvault", 0) < 0)
			lancer_ligne("/sys/svc/keyvault", 1);
	}
	if (korin_sys2(SYS_SVC_OPEN, "hwmanager", 0) < 0) /* (la clé d'installation : init l'a déjà lancé) */
		lancer_ligne("/sys/svc/hwmanager", 1); /* étape 2 : hwmanager, le shell */
	else
		say("init2 : hwmanager deja lance (par init)\n");
	lancer_ligne("/sys/svc/hidmanager", 1);
	plat_buses(); /* le bus USB (ses pilotes : hwmanager) ; PC : le PCIe annoncé à hwmanager */
	int console = plat_text_console();
	if (!console)
		lancer_ligne("/sys/elegant", 1);
	services_lists(); /* le type de démarrage (volet Démarrage de Réglages) */
	lancer_fichier("/etc/services.txt", 1, off_list, man_list); /* les services de services.txt */
	{ /* les tâches planifiées (docs/TACHES.md) : ici, pas par services.txt (un fichier modifié n'est plus mis à jour) */
		long st[2];
		if (sys_stat(TASKD, st) >= 0 && korin_sys2(SYS_SVC_OPEN, "taskd", 0) < 0)
			lancer_ligne(TASKD, 1);
	}
	if (console)
		lancer_ligne("/sys/svc/console", 1); /* (sans Elegant : ni « ws », ni startup.txt) */
	else {
		if (korin_sys2(SYS_SVC_OPEN, "ws", ATTENTE_WS) < 0) { /* étape 3 : le shell répond ? */
			say("init2 : le shell ne repond pas, startup.txt quand meme\n");
			plat_shell_lost(); /* (PC : la console texte en secours) */
		}
		startup_attendu = 1; /* startup.txt : quand la session est ouverte (écran de connexion), plus bas */
	}
	spawn("/sys/svc/init2", "preload"); /* exécutables préchargés : dans un process à part */
	for (;;) { /* relancer ce qui tombe */
		if (canal < 0)
			return 0;
		if (startup_attendu)
			startup_si_ouverte();
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, startup_attendu ? 500 : 60000);
		if (req < 0)
			return 0;
		if (req) {
			requete(req);
			continue;
		}
		if (startup_attendu)
			continue; /* (les tours d'une demi-seconde ne comptent pas pour l'heure) */
		if (++veilles >= HEURE_MINUTES) { /* toutes les 30 min : l'heure sur la carte */
			veilles = 0;
			sauver_heure();
		}
	}
}
