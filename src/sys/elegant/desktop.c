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

static long dk_no_key(const u8 *in)
{
	(void)in;
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
	.key = dk_no_key,
	.gone = dk_none_w,
	.raise = dk_none_w,
};

/* à chaque tour : le bus (korin.shell.mode, shell.c) */
void dk_tick(void)
{
	sh_tick(0);
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

/* fichier de la session, programmes sans lui (chaînes, puis une vide) */
void session_start(const char *file, const char *defaults)
{
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
	char *args = 0; /* arguments après le premier espace */
	for (char *s = line; *s; s++)
		if (*s == ' ') {
			*s = 0;
			args = s + 1;
			break;
		}
	ksys(SYS_SPAWN, (long)line, (long)args, 0, 0, 0, 0); /* (file_sys d'AppKit, comme sys) */
}
