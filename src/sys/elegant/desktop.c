/* desktop.c : le mode « desktop » (shell=desktop), le bureau Elegant (desktop.inc). Comme dans
 * Onyx, le bureau est fait de programmes ordinaires, lancés au démarrage de la session
 * (/etc/session, une ligne « chemin [arguments] » par programme) : le fond d'écran (voronoy),
 * la barre de menus (menubar), le panel. Le serveur ne fait que le fond uni d'attente. Le mode
 * « poche » a la sienne (pocket.c). */
#include "elegant.h"

const char str_mode_desktop[] = "desktop";
static const char path_session[] = "/etc/session";
/* sans /etc/session (chaînes à la suite, puis une vide) */
static const char session_def[] = "/sys/voronoy\0"
				  "/sys/menubar\0"
				  "/sys/panel\0";

/* (bu_none, bu_no de l'asm : une seule fonction pour toutes les entrées « rien » de la table ;
 * en C, une par forme d'appel) */
void dk_none(void)
{
}

long dk_no(long x, long y)
{
	(void)x;
	(void)y;
	return 0;
}

static void dk_none_w(struct win *w)
{
	(void)w;
}

/* Super seul (appuyé puis relâché sans autre touche) : la vue d'ensemble (overview.c), comme
 * GNOME ; Super est avalé, les autres touches passent */
#define HID_LGUI 0xE3
#define HID_RGUI 0xE7
static long dk_super; /* Super appuyé, seul jusqu'ici */
static long login_pid; /* > 0 : /sys/login tourne */

static long dk_key(const u8 *in)
{
	u64 code = M64(in, 8);
	if (!ov_enabled || login_pid > 0) /* (le bureau classique : Super va aux applications, comme avant ; pas de vue
					      d'ensemble à l'écran de connexion) */
		return 0;
	if (code == HID_LGUI || code == HID_RGUI) {
		if (M64(in, 16)) {
			dk_super = 1;
		} else {
			if (dk_super)
				ov_toggle();
			dk_super = 0;
		}
		return 1;
	}
	if (M64(in, 16))
		dk_super = 0; /* une autre touche : Super n'est plus seul */
	return 0;
}

const struct policy mode_desktop = {
	.name = str_mode_desktop,
	.bar_h = 0,
	.init = dk_init,
	.back = dk_back,
	.front = dk_none,
	.click = dk_no,
	.hover = dk_no,
	.tick = dk_tick,
	.wins = dk_none,
	.flags = 0,
	.place = dk_none_w,
	.key = dk_key,
	.gone = dk_none_w,
	.raise = dk_none_w,
};

/* événement du bus (WEV_BUS) : korin.wallpaper.drawn, le fond d'écran de la session est dessiné */
static void dk_bus_ev(u8 *ev)
{
	if (sh_sub_wall > 0 && M32(ev, BEV_SUB) == (u64)sh_sub_wall)
		ov_wall_drawn();
}

/* à chaque tour : le bus (korin.shell.mode, korin.wallpaper.drawn : shell.c) */
void dk_tick(void)
{
	sh_tick(dk_bus_ev);
	ov_tick(); /* la vue d'ensemble : animations, vignettes */
}

/* fond uni dans le clip */
void dk_back(void)
{
	bb_fill(clip_r.x0, clip_r.y0, clip_r.x1 - clip_r.x0, clip_r.y1 - clip_r.y0, COL_DESK);
}

/* lance la session : chaque ligne de /etc/session (# : commentaire) */
void dk_init(void)
{
	session_start(path_session, session_def);
}

/* ---- l'écran de connexion (docs/DESIGN-CONNEXION.md, étape 6 du multi-utilisateur) : la session ne part qu'une
 * fois /sys/login fini (il écrit /tmp/session.ini : le compte ouvert ; connexion automatique : tout de suite,
 * sans rien montrer). Elegant reste en uid 0 et continue de servir les fenêtres (la carte de login) : la fin de
 * login est surveillée à chaque tour (session_poll, depuis sh_tick). */
#ifndef WAIT_EX_TAG
#define WAIT_EX_TAG (0x57414954L << 32) /* (syscalls.inc : SYS_WAIT, forme étendue) */
#define WEX_NOHANG 1
#define WR_NONE 0
#endif
#ifndef WR_EXITED
#define WR_EXITED 1 /* (sortie : le code de sortie, < 0 : -signal) */
#endif
static int voronoy_early;       /* le fond à facettes lancé avant login (pas relancé par la session) */
static long voronoy_pid;        /* (son pid : arrêté si la session ne le veut pas, session.off) */
static int login_done;          /* 1 : la session est ouverte (ou il n'y a pas d'écran de connexion) */
static const char *sess_file, *sess_def;

static void session_run_file(const char *file, const char *defaults);

/* login fini sans écrire /tmp/session.ini (il est tombé) : le compte par défaut, pour qu'init2 continue */
static void session_default(void)
{
	long st[2];
	if (sys_stat("/tmp/session.ini", st) >= 0)
		return;
	long uid = user_session_uid();
	char line[40] = "uid = ";
	char *o = line + 6;
	char t[24];
	int k = 0;
	u64 v = uid > 0 ? (u64)uid : 0;
	do
		t[k++] = (char)('0' + v % 10);
	while ((v /= 10));
	while (k)
		*o++ = t[--k];
	*o++ = '\n';
	long fd = sys_open("/tmp/session.ini", O_WRITE | O_CREATE | O_TRUNC);
	if (fd >= 0) {
		sys_write(fd, line, (u64)(o - line));
		sys_close(fd);
	}
}

void session_poll(void)
{
	if (login_pid <= 0)
		return;
	long out = 0;
	long r = korin_sys3(SYS_WAIT, login_pid, WAIT_EX_TAG | WEX_NOHANG, (long)&out);
	if (r == WR_NONE)
		return;
	login_pid = 0;
	login_done = 1;
	/* login arrêté par SIGTERM ou SIGKILL (sortie -15, -9) : l'extinction ou le redémarrage en cours (init2
	   prévient tous les programmes, Éteindre de l'écran de connexion compris) ; pas de session. Tombé
	   autrement : le compte par défaut, comme avant. */
	if (r == WR_EXITED && (out == -15 || out == -9))
		return;
	long st[2];
	if (sys_stat("/tmp/login.halt", st) >= 0) /* (Éteindre ou Redémarrer choisi dans login : marque posée) */
		return;
	/* tombé sur une faute (sortie < 0) sans avoir ouvert de session : l'écran de connexion revient, pas le
	   bureau sans mot de passe ; le code de sortie dans /tmp/login.exit (pour chercher la faute) */
	if (r == WR_EXITED && out < 0 && sys_stat("/tmp/session.ini", st) < 0) {
		static int relaunched;
		char line[24], *o = line + sizeof line;
		u64 v = (u64)-out;
		*--o = '\n';
		do
			*--o = (char)('0' + v % 10);
		while ((v /= 10));
		*--o = '-';
		long fd = sys_open("/tmp/login.exit", O_WRITE | O_CREATE | O_TRUNC);
		if (fd >= 0) {
			sys_write(fd, o, (u64)(line + sizeof line - o));
			sys_close(fd);
		}
		if (relaunched++ < 5) {
			login_done = 0;
			session_start(sess_file, sess_def);
		}
		return;
	}
	session_default();
	session_run_file(sess_file, sess_def);
}

/* fichier de la session, programmes sans lui (chaînes, puis une vide) */
void session_start(const char *file, const char *defaults)
{
	long st[2];
	if (!login_done && !login_pid && sys_stat("/sys/login", st) >= 0) {
		sess_file = file;
		sess_def = defaults;
		u64 pe[8]; /* le fond à facettes sous la carte (DESIGN-CONNEXION §4), s'il ne tourne pas déjà (une
			      session fermée : il est d'uid 0, il est resté) */
		int fond = voronoy_pid > 0 && korin_sys2(SYS_PROC_INFO, voronoy_pid, (long)pe) == 0 && pe[7] != 3;
		if (!fond && sys_stat("/sys/voronoy", st) >= 0)
			voronoy_early = (voronoy_pid = ksys(SYS_SPAWN, (long)"/sys/voronoy", 0, 0, 0, 0, 0)) > 0;
		login_pid = ksys(SYS_SPAWN, (long)"/sys/login", 0, 0, 0, 0, 0);
		if (login_pid > 0)
			return; /* (la session part à la fin de login : session_poll) */
		login_pid = 0;
	}
	if (login_pid > 0) { /* (un changement de mode pendant la connexion : le fichier du nouveau mode) */
		sess_file = file;
		sess_def = defaults;
		return;
	}
	sess_file = file;
	sess_def = defaults;
	login_done = 1;
	session_run_file(file, defaults);
}

/* WS_OP_SESSION_END (init2 seul : INIT_OP_LOGOUT, la session fermée, ses process arrêtés) : l'écran de
 * connexion revient (/tmp/login.manual : sans la connexion automatique) -> 0, E_PERM, E_INVAL (pas de login) */
long req_session_end(void)
{
	long st[2];
	if (korin_sys1(SYS_PROC_UID, (long)M32(msg, WM_PID)) != 0)
		return E_PERM;
	if (!sess_file || login_pid > 0 || sys_stat("/sys/login", st) < 0)
		return E_INVAL;
	login_done = 0;
	session_start(sess_file, sess_def);
	return 0;
}

/* les programmes à ne pas lancer (volet Démarrage de Réglages, docs/DEMARRAGE.md) : les désactivés
 * (session.off) puis les manuels (session.manual), personnels (HOME:/etc/, sinon /etc/), une ligne du
 * fichier de la session par entrée */
static char sess_off[4096];

static void session_run_file(const char *file, const char *defaults)
{
	long su = user_session_uid(); /* (le compte ouvert, pas le HOME: d'Elegant, E30) */
	long k = list_load(conf_path_uid(su, "session.off"), sess_off, sizeof sess_off / 2);
	sess_off[k] = 10;
	list_load(conf_path_uid(su, "session.manual"), sess_off + k + 1, (long)sizeof sess_off - k - 1);
	if (voronoy_early && voronoy_pid > 0 && (su > 0 || list_has(sess_off, "/sys/voronoy"))) {
		/* (lancé sous la connexion, en uid 0 : arrêté ; la session le relance sous le compte, qui lit son propre
		 * wallpaper.ini, E33 étape 1) */
		ksys(SYS_KILL, voronoy_pid, 0, 0, 0, 0, 0);
		voronoy_pid = 0;
		voronoy_early = 0;
	}
	long fd = sys_open(file, O_READ);
	long n = -1;
	if (fd >= 0) {
		n = sys_read(fd, ini_buf, INI_MAX - 1);
		sys_close(fd);
	}
	if (n <= 0) {
		/* Écart voulu : l'asm passait la chaîne en place à session_run, qui la coupe au premier
		   espace (« /sys/menubar --poche » du mode poche) ; la suivante devenait alors
		   « --poche », lancée pour rien. Ici, une copie. */
		char line[256];
		for (const char *s = defaults; *s; s += str_len(s) + 1) {
			str_copy_n(line, s, sizeof line);
			session_run(line);
		}
		return;
	}
	ini_buf[n] = 0;
	char *p = ini_buf;
	while (*p) { /* début de ligne */
		char *e = p; /* fin de ligne -> 0 */
		while (*e && *e != 10 && *e != 13)
			e++;
		char sep = *e; /* (le séparateur, pour continuer) */
		*e = 0;
		session_run(p);
		if (!sep)
			return;
		p = e + 1;
	}
}

/* ligne « chemin [arguments] » (modifiée) : lance le programme */
void session_run(char *line)
{
	if (!*line || *line == '#') /* vide, commentaire */
		return;
	if (list_has(sess_off, line)) /* désactivé (Réglages > Démarrage) */
		return;
	if (voronoy_early && !memcmp(line, "/sys/voronoy", 12) && (!line[12] || line[12] == ' '))
		return; /* (déjà lancé sous l'écran de connexion) */
	char *args = 0; /* arguments après le premier espace */
	for (char *s = line; *s; s++)
		if (*s == ' ') {
			*s = 0;
			args = s + 1;
			break;
		}
	if (!memcmp(line, "/sys/voronoy", 13))
		ov_wall_expect(); /* (la vue d'ensemble de la session attend qu'il ait dessiné le fond) */
	spawn_session(line, args); /* avec l'uid du compte (korinrt, users.c), Elegant restant en uid 0 */
}
