/* main.c -- win16 : lance un programme de Windows 3.x (exécutable NE) sur Korin OS.
 *
 *   win16 [--trace | --trace-cpu] [--drive X=/dossier]... PROGRAMME.EXE [arguments]
 *
 * Lecteurs : C: = HOME:/win16/c, D: = le dossier du programme, E: = « / » ; HOME:/win16/drives.ini les règle
 * (section [drives], « X=/dossier »), --drive aussi, pour ce lancement (kernel.c).
 *
 * Programme sous GPL-2, repris de winevdm (docs/etude/WIN16.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include "w16.h"
#include "be.h"
#include "dos.h"

void kernel_set_cmdline (const char *args);

void w16_terminate (int code)
{
	w16_exit = 1;
	w16_exit_code = code;
}

/* le chemin vu par le programme : D:\<NOM>.EXE (D: = son dossier, kernel.c) */
static void dos_path (const char *path, char *out, size_t cap)
{
	const char *b = strrchr (path, '/');
	b = b ? b + 1 : path;
	snprintf (out, cap, "D:\\%s", b);
	for (char *p = out; *p; p++)
		*p = (char) toupper ((unsigned char) *p);
}

/* un exécutable de Windows (MZ suivi d'un en-tête NE) ; sinon, un programme DOS */
/* n octets du fichier à la position off (lseek, read en boucle, comme ne_load) -> le nombre lu */
static size_t read_at (int fd, uint32_t off, uint8_t *buf, size_t n)
{
	if (lseek (fd, (off_t) off, SEEK_SET) != (off_t) off)
		return 0;
	size_t got = 0;
	while (got < n)
	{
		ssize_t r = read (fd, buf + got, n - got);
		if (r <= 0)
			break;
		got += (size_t) r;
	}
	return got;
}

/* un exécutable de Windows : MZ, et un en-tête NE à l'adresse de 3Ch (le champ 18h n'est pas regardé : des éditeurs de
 * liens de Windows y laissent moins de 40h) ; sinon, un programme DOS. Le choix est écrit sur stderr. */
static int is_windows (const char *path)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0)
		return 1;	/* (ne_load dira qu'il ne peut pas le lire) */
	uint8_t h[0x40], ne[2] = { 0, 0 };
	size_t n = read_at (fd, 0, h, sizeof h);
	int win = 0;
	uint32_t off = 0;
	if (n == sizeof h && ((h[0] == 'M' && h[1] == 'Z') || (h[0] == 'Z' && h[1] == 'M')))
	{
		off = (uint32_t) (h[0x3c] | h[0x3d] << 8 | h[0x3e] << 16 | (uint32_t) h[0x3f] << 24);
		if (off >= 0x40 && read_at (fd, off, ne, 2) == 2)
			win = ne[0] == 'N' && ne[1] == 'E';
	}
	close (fd);
	fprintf (stderr, "win16 : %s : %s (en-tête lu : %zu octets, %c%c en %x)\n", path, win ? "Windows 3.x" : "DOS", n,
		ne[0] >= 32 ? ne[0] : '.', ne[1] >= 32 ? ne[1] : '.', off);
	return win;
}

char w16_dosdir[512];

static int is_batch (const char *path)
{
	size_t l = strlen (path);
	return l > 4 && !strcasecmp (path + l - 4, ".bat");
}

int main (int argc, char **argv)
{
	int i = 1, dos_prompt = 0;
	for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
	{
		if (!strcmp (argv[i], "--dos"))
			dos_prompt = 1;
		else if (!strcmp (argv[i], "--trace"))
			w16_trace = 1;
		else if (!strcmp (argv[i], "--trace-cpu"))
			w16_trace = 2;
		else if (!strcmp (argv[i], "--drive") && i + 1 < argc)
		{
			extern void kernel_drive_option (const char *);
			kernel_drive_option (argv[++i]);
		}
	}
	if (getenv ("W16_TRACE"))
		w16_trace = atoi (getenv ("W16_TRACE"));
	/* (WinExec relance ce programme ; sous Korin, argv[0] est le nom du process, pas son chemin : lancé par
	   Fichiers ou le lanceur, « main » ; le programme est alors celui du paquet) */
	snprintf (w16_self, sizeof w16_self, "%s", strchr (argv[0], '/') ? argv[0] : "/apps/win16.app/main");
	int ui = be_init ();
	/* Z: : le dossier dos/ à côté du programme (COMMAND.COM), ou W16_DOSDIR */
	{
		const char *e = getenv ("W16_DOSDIR");
		if (e && *e)
			snprintf (w16_dosdir, sizeof w16_dosdir, "%s", e);
		else
		{
			snprintf (w16_dosdir, sizeof w16_dosdir, "%s", w16_self);
			char *sl = strrchr (w16_dosdir, '/');
			if (sl)
				snprintf (sl, sizeof w16_dosdir - (size_t) (sl - w16_dosdir), "/dos");
			else
				snprintf (w16_dosdir, sizeof w16_dosdir, "dos");
		}
	}
	/* (lancé sans fichier, depuis le lanceur du bureau : l'invite du DOS, comme --dos) */
	if (i >= argc && ui)
		dos_prompt = 1;
	if (dos_prompt || (i < argc && is_batch (argv[i])))
	{
		/* l'invite du DOS (--dos), ou un .BAT : COMMAND.COM (FreeCOM) sur Z:, D: = le dossier courant ou celui du .BAT */
		char comspec[600], args[300] = "";
		snprintf (comspec, sizeof comspec, "%s/COMMAND.COM", w16_dosdir);
		if (i < argc && is_batch (argv[i]))
		{
			snprintf (task.dir, sizeof task.dir, "%s", argv[i]);
			char *sl = strrchr (task.dir, '/');
			if (sl)
				*sl = 0;
			else
				snprintf (task.dir, sizeof task.dir, ".");
			const char *b = strrchr (argv[i], '/');
			snprintf (args, sizeof args, "/C %s", b ? b + 1 : argv[i]);
			for (int k = i + 1; k < argc; k++)
			{
				strncat (args, " ", sizeof args - strlen (args) - 1);
				strncat (args, argv[k], sizeof args - strlen (args) - 1);
			}
		}
		else
		{
			extern const char *kernel_user_home (void);
			snprintf (task.dir, sizeof task.dir, "%s", kernel_user_home ());
		}
		dos_hold = !dos_prompt;
		return dos_main (comspec, "Z:\\COMMAND.COM", args);
	}
	if (i >= argc)
	{
		if (ui)
			be_message ("Windows 3.x", "Ouvrez un programme Windows 3.x (fichier .exe) avec Windows 3.x.", 0);
		fprintf (stderr, "usage : win16 [--trace] PROGRAMME.EXE [arguments]\n");
		return 1;
	}
	if (!ui)
		fprintf (stderr, "win16 : pas d'affichage\n");
	if (!is_windows (argv[i]))
	{
		/* un programme DOS (MZ sans en-tête NE, .COM) : la machine DOS (dos.c) */
		char args[256] = "";
		for (int k = i + 1; k < argc; k++)
		{
			if (k > i + 1)
				strncat (args, " ", sizeof args - strlen (args) - 1);
			strncat (args, argv[k], sizeof args - strlen (args) - 1);
		}
		snprintf (task.dir, sizeof task.dir, "%s", argv[i]);
		char *sl = strrchr (task.dir, '/');
		if (sl)
			*sl = 0;
		else
			snprintf (task.dir, sizeof task.dir, ".");
		return dos_main (argv[i], 0, args);
	}
	if (!cpu_init ())
	{
		fprintf (stderr, "win16 : pas assez de mémoire\n");
		return 1;
	}
	mem_init ();
	run_init ();
	memset (&task, 0, sizeof task);
	task.cmdshow = 1;	/* SW_SHOWNORMAL */
	/* le dossier du programme (D:) */
	snprintf (task.dir, sizeof task.dir, "%s", argv[i]);
	char *sl = strrchr (task.dir, '/');
	if (sl)
		*sl = 0;
	else
		snprintf (task.dir, sizeof task.dir, ".");
	dos_path (argv[i], task.dospath, sizeof task.dospath);
	kernel_init ();
	user_init ();
	gdi_init ();
	/* une pile pour les LibMain des DLL, chargées avant le programme */
	uint16_t lstack = global_alloc (0x4000, 0);
	cpu_set_sreg (S_SS, lstack);
	cpu_set_reg (R_ESP, 0x3ffe);
	if (!ne_load (argv[i]))
	{
		char msg[600];
		snprintf (msg, sizeof msg, "Impossible de lire %s.", argv[i]);
		w16_fatal ("%s", msg);
	}
	snprintf (task.modname, sizeof task.modname, "%s", task.main->name);
	char args[256] = "";
	for (int k = i + 1; k < argc; k++)
	{
		if (k > i + 1)
			strncat (args, " ", sizeof args - strlen (args) - 1);
		strncat (args, argv[k], sizeof args - strlen (args) - 1);
	}
	kernel_set_cmdline (args);
	/* les registres du démarrage d'une tâche (TASK_CallToStart de Wine) */
	cpu_set_sreg (S_SS, task.ss);
	cpu_set_reg (R_ESP, task.sp);
	cpu_set_sreg (S_DS, task.hinstance);
	cpu_set_sreg (S_ES, task.psp);
	cpu_set_reg (R_EAX, 0);
	cpu_set_reg (R_EBX, task.stack_size);
	cpu_set_reg (R_ECX, task.heap_size);
	cpu_set_reg (R_EDX, 0);
	cpu_set_reg (R_ESI, 0);
	cpu_set_reg (R_EDI, task.hinstance);
	cpu_set_reg (R_EBP, 0);
	cpu_jump (task.cs, task.ip);
	cpu_set_flags (0x202);
	w16_log ("%s : départ en %04x:%04x, pile %04x:%04x\n", task.modname, task.cs, task.ip, task.ss, task.sp);
	run_until_return (0xffff);
	w16_log ("fin : code %d\n", w16_exit_code);
	return w16_exit_code;
}
