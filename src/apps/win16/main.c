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
#include <ctype.h>
#include "w16.h"
#include "be.h"

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

int main (int argc, char **argv)
{
	int i = 1;
	for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
	{
		if (!strcmp (argv[i], "--trace"))
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
	int ui = be_init ();
	if (i >= argc)
	{
		if (ui)
			be_message ("Windows 3.x", "Ouvrez un programme Windows 3.x (fichier .exe) avec Windows 3.x.", 0);
		fprintf (stderr, "usage : win16 [--trace] PROGRAMME.EXE [arguments]\n");
		return 1;
	}
	if (!ui)
		fprintf (stderr, "win16 : pas d'affichage\n");
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
