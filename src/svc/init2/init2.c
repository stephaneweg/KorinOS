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
 *    0 -> pid. Refus : chemin hors de /sys (E_PERM). Chaque demande est
 *    notée au journal ;
 *  * INIT_OP_KIT_LOAD : inscrit un kit pour un process sans droits ; données :
 *    le nom (« netkit ») -> 0 ou erreur (f_kit_load d'AppKit) ;
 *  * INIT_OP_REBOOT : a bit 0 = tryboot, bit 1 = arrêt ;
 *  * après startup.txt : init2 se relance avec l'argument « preload », qui
 *    précharge les exécutables de /etc/preload.txt et s'arrête. */
#include "init2.h"

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

/* fichier (une ligne par programme), watch -> 1 si le fichier existe */
static int lancer_fichier(const char *path, int watch)
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
		if (*l && *l != '#') /* vide, ou commentaire */
			lancer_ligne(l, watch);
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
	if (sys_path_ok(launch_path)) { /* AppKit : droits (vfs : ATTR_SYS ; le noyau : init2 les a) */
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
	sys_sleep(500);
	plat_reboot(flags);
	say("init2 : le redémarrage n'a pas eu lieu\n");
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
		sauver_heure();
		reboot_request(req); /* (ne revient pas si tout va bien ; la réponse est déjà partie) */
		return;
	} else if (op == INIT_OP_LAUNCH)
		r = launch_request();
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
static int startup_attendu;

static void startup_si_ouverte(void)
{
	long st[2];
	if (sys_stat("/tmp/session.ini", st) < 0 && sys_stat("/sys/login", st) >= 0)
		return;
	startup_attendu = 0;
	session_uid = 1;
	lancer_fichier(conf_path("startup.txt", 0), 0);
	session_uid = 0;
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
	lancer_fichier("/etc/services.txt", 1); /* les services de services.txt */
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
