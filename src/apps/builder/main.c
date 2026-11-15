/*
 * builder -- PiBuild, la fenetre de pibuild (docs/GCC.md).
 *
 * Le dossier des sources (choisi par ui_folder_choose), le nom du programme, son type (devine du dossier :
 * .../bin/<nom>, .../sys/, .../svc/ ; sinon app), le bouton Construire : /bin/pibuild <type> <dossier> <nom>,
 * sa sortie dans un tube, lu sans attendre depuis la boucle : le journal se remplit pendant la construction.
 * Argument : un dossier, ou un fichier de ce dossier (l'editeur, Ctrl+B).
 *
 * Port en C de builder.asm (memes widgets, memes textes), un seul source pour le Pi et le PC ; l'asm reste
 * dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <uikit.h>

#define WIN_W		680
#define WIN_H		480
#define M		12
#define LBL_W		72
#define BTN_W		116
#define ROW_H		28
#define FLD_X		(M + LBL_W)
#define FLD_W		(WIN_W - FLD_X - M - BTN_W - 8 - FRAME_CAP_W)	/* (a gauche des boutons de la fenetre) */
#define TYP_W		76
#define NAM_W		(FLD_W - TYP_W - 8)
#define BTN_X		(WIN_W - M - BTN_W - FRAME_CAP_W)
#define Y1		M
#define Y2		(Y1 + ROW_H + 8)
#define LOG_Y		(Y2 + ROW_H + 10)
#define LOG_MAX		65536			/* journal garde (la fin) */

extern char **environ;
static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static long w_dir, w_name, w_type, w_choose, w_build, w_log;
static pid_t run_pid;
static int run_fd = -1;
static long log_len;
static char dir[256];
static char logbuf[LOG_MAX + 8];
static const char *const type_args[] = { "app", "bin", "sys", "svc", "lib" };

static void put_label (long y, const char *s) { ui_label (M, y + 3, LBL_W - 6, 22, (long) s, 0); }

static void build (void)
{
	put_label (Y1, "Sources");
	put_label (Y2, "Nom");
	w_dir = ui_textbox (FLD_X, Y1, FLD_W, ROW_H, UTF_READONLY);
	w_name = ui_textbox (FLD_X, Y2, NAM_W, ROW_H, 0);
	w_type = ui_dropdown (FLD_X + NAM_W + 8, Y2, TYP_W, ROW_H, (long) "app|bin|sys|svc|lib");
	ui_w_set_value (w_type, 0);
	w_choose = ui_pushbutton (BTN_X, Y1, BTN_W, ROW_H, (long) "Choisir...", 0);
	w_build = ui_pushbutton (BTN_X, Y2, BTN_W, ROW_H, (long) "Construire", 0);
	w_log = ui_textarea (M, LOG_Y, WIN_W - 2 * M, WIN_H - LOG_Y - M, UTF_READONLY | UTF_MONO | UTA_VSCROLL);
}

/* le dossier -> le type : le dernier /bin/, /sys/ ou /svc/ qu'il contient (sinon app) */
static long type_guess (const char *p)
{
	long t = 0;
	for (; *p; p++)
	{
		if (*p != '/' || !p[1] || !p[2] || !p[3] || p[4] != '/')
			continue;
		if (!strncmp (p + 1, "bin", 3))
			t = 1;
		else if (!strncmp (p + 1, "sys", 3))
			t = 2;
		else if (!strncmp (p + 1, "svc", 3))
			t = 3;
	}
	return t;
}

/* dir montre, sans son / final (le dialogue rend « .../ ») ; le nom : son dernier element */
static void dir_show (void)
{
	size_t n = strlen (dir);
	if (n >= 1 && dir[n - 1] == '/')
		dir[n - 1] = 0;
	ui_w_set_text (w_dir, (long) dir);
	const char *name = dir;
	for (const char *p = dir; *p; p++)
		if (*p == '/' || *p == ':')
			name = p + 1;
	ui_w_set_text (w_name, (long) name);
	ui_w_set_value (w_type, type_guess (dir));
}

static void do_choose (void)
{
	if (ui_folder_choose ((long) "/src/apps", (long) dir, 240))
		dir_show ();
}

/* l'argument : un dossier, ou un fichier (un point dans son dernier element) : son dossier */
static void set_arg (const char *a)
{
	size_t i = 0;
	for (; a[i] && i < 240; i++)
		dir[i] = a[i];
	dir[i] = 0;
	char *slash = 0;
	int dot = 0;
	for (char *p = dir; *p; p++)
	{
		if (*p == '/')
		{
			slash = p;
			dot = 0;
		}
		if (*p == '.')
			dot = 1;
	}
	if (dot && slash)
		*slash = 0;
	dir_show ();
}

/* ajoute n octets au journal, et le montre */
static void log_add (const char *s, long n)
{
	if (log_len + n >= LOG_MAX - 1)
	{					/* plein : la seconde moitie reste */
		memmove (logbuf, logbuf + LOG_MAX / 2, LOG_MAX / 2);
		log_len = LOG_MAX / 2;
	}
	memcpy (logbuf + log_len, s, (size_t) n);
	log_len += n;
	logbuf[log_len] = 0;
	ui_w_set_text (w_log, (long) logbuf);
	ui_textarea_goto (w_log, log_len);
}
static void log_str (const char *s) { log_add (s, (long) strlen (s)); }

static void do_build (void)
{
	if (run_pid)
		return;				/* (une a la fois) */
	const char *name = (const char *) ui_w_get_text (w_name);
	if (!dir[0] || !name[0])
	{
		log_str (TR ("Choisissez un dossier et donnez un nom.\n"));
		return;
	}
	long t = ui_w_get_value (w_type);
	if (t < 0 || t > 4)
		t = 0;
	char nm[256];
	size_t i = 0;
	for (; name[i] && i < 240; i++)
		nm[i] = name[i];
	nm[i] = 0;
	log_len = 0;
	log_str ("pibuild ");
	log_str (type_args[t]);
	log_str (" ");
	log_str (dir);
	log_str (" ");
	log_str (nm);
	log_str ("\n");
	/* ses sorties dans un tube, lu sans attendre (O_NONBLOCK) */
	int fds[2];
	if (pipe (fds) == 0)
	{
		posix_spawn_file_actions_t fa;
		posix_spawn_file_actions_init (&fa);
		posix_spawn_file_actions_adddup2 (&fa, fds[1], 1);
		posix_spawn_file_actions_adddup2 (&fa, fds[1], 2);	/* (comme SYS_SPAWN_PIPE : la sortie d'erreur aussi) */
		posix_spawn_file_actions_addclose (&fa, fds[0]);
		posix_spawn_file_actions_addclose (&fa, fds[1]);
		char *argv[] = { "/bin/pibuild", (char *) type_args[t], dir, nm, 0 };
		pid_t pid;
		int e = posix_spawn (&pid, argv[0], &fa, 0, argv, environ);
		posix_spawn_file_actions_destroy (&fa);
		close (fds[1]);
		if (e == 0)
		{
			fcntl (fds[0], F_SETFL, fcntl (fds[0], F_GETFL) | O_NONBLOCK);
			run_fd = fds[0];
			run_pid = pid;
			return;
		}
		close (fds[0]);
	}
	log_str (TR ("Impossible de lancer pibuild.\n"));
}

/* ce qui est arrive dans le tube, sans attendre ; a la fin : le code de sortie */
static void poll_child (void)
{
	char buf[520];
	for (int k = 0; k < 16; k++)
	{
		long n = read (run_fd, buf, 512);
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;			/* rien pour l'instant */
		if (n > 0)
		{
			log_add (buf, n);
			continue;
		}
		close (run_fd);
		run_fd = -1;
		int st = 0;
		waitpid (run_pid, &st, 0);
		run_pid = 0;
		int ok = WIFEXITED (st) && WEXITSTATUS (st) == 0;
		log_str (TR (ok ? "\nConstruction reussie.\n" : "\nEchec de la construction.\n"));
		return;
	}
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui))
		return 1;
	if (ui_window (WIN_W, WIN_H, (long) "PiBuild") < 0)
		return 0;
	build ();
	if (argc > 1 && argv[1][0])
	{					/* (la ligne d'arguments entiere, comme l'asm) */
		char a[256] = "";
		for (int i = 1; i < argc; i++)
		{
			if (i > 1)
				strncat (a, " ", sizeof a - strlen (a) - 1);
			strncat (a, argv[i], sizeof a - strlen (a) - 1);
		}
		set_arg (a);
	}
	ui_w_draw (0);
	ui_update_all ();
	for (;;)
	{
		long t = ui_event (run_pid ? 15 : 200);	/* en cours : lire le tube souvent */
		if (run_pid)
			poll_child ();
		if (t == 0)
		{
			ui_w_idle ();
			continue;
		}
		if (t == WEV_CLOSE)
			break;
		long code = 0, taken = 0;
		long w = uikit_w_event (&code, &taken);
		if (code != UN_ACTIVATE)
			continue;
		if (w == w_choose)
			do_choose ();
		else if (w == w_build)
			do_build ();
	}
	return 0;
}
