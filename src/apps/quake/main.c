/*
 * quake -- Quake d'id Software (third_party/quake, GPL) en rendu logiciel, sur EmuKit (docs/QUAKE.md).
 * Ce fichier : le systeme du moteur (fichiers, horloge, erreurs) et la boucle. vid.c, snd.c, in.c : le reste.
 *     quake [parametres de Quake]        SD0:/quake/args.txt : d'autres parametres, sur une ligne
 * Donnees : SD0:/quake/id1, pak0.pak compris (LibreQuake ou l'original) ; sauvegardes et config.cfg au meme endroit.
 * Ce programme est sous GPL version 2, comme le moteur : voir third_party/quake/gnu.txt.
 */
#include "quakedef.h"
#include <emukit.h>
#include <errno.h>
#include <sys/stat.h>
#include "armpi.h"

#define BASEDIR "SD0:/quake"
#define MEM (96 * 1024 * 1024)
#define MAX_ARGS 32

static byte g_mem[MEM];		/* (dans le programme : le moteur range des pointeurs en decalages sur 32 bits) */
static char *g_argv[MAX_ARGS];
static int g_argc;
static double g_last;
qboolean isDedicated;
extern byte *r_stack_start;

/* --- fichiers : comme sys_null.c d'id --- */
#define MAX_HANDLES 32
static FILE *handles[MAX_HANDLES];

static int findhandle (void)
{
	int i;
	for (i = 1; i < MAX_HANDLES; i++) if (!handles[i]) return i;
	Sys_Error ("out of handles");
	return -1;
}
int Sys_FileOpenRead (char *path, int *hndl)
{
	int i = findhandle (), end;
	FILE *f = fopen (path, "rb");
	if (!f) { *hndl = -1; return -1; }
	handles[i] = f;
	*hndl = i;
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, 0, SEEK_SET);
	return end;
}
int Sys_FileOpenWrite (char *path)
{
	int i = findhandle ();
	FILE *f = fopen (path, "wb");
	if (!f) Sys_Error ("Error opening %s: %s", path, strerror (errno));
	handles[i] = f;
	return i;
}
void Sys_FileClose (int h) { fclose (handles[h]); handles[h] = NULL; }
void Sys_FileSeek (int h, int pos) { fseek (handles[h], pos, SEEK_SET); }
int Sys_FileRead (int h, void *d, int n) { return fread (d, 1, n, handles[h]); }
int Sys_FileWrite (int h, void *d, int n) { return fwrite (d, 1, n, handles[h]); }
int Sys_FileTime (char *path)
{
	struct stat st;
	return stat (path, &st) ? -1 : (int) st.st_mtime;
}
void Sys_mkdir (char *path) { mkdir (path, 0777); }

/* --- systeme --- */
void Sys_MakeCodeWriteable (unsigned long a, unsigned long n) {}
void Sys_Sleep (void) {}
void Sys_HighFPPrecision (void) {}
void Sys_LowFPPrecision (void) {}
char *Sys_ConsoleInput (void) { return NULL; }

/* une erreur fatale tombe sur n'importe quel thread : le texte va dans SD0:/quake/error.txt, et le programme s'arrete */
void Sys_Error (char *error, ...)
{
	static char text[1024];
	va_list ap;
	FILE *f;
	va_start (ap, error);
	vsnprintf (text, sizeof text, error, ap);
	va_end (ap);
	fprintf (stderr, "Sys_Error: %s\n", text);
	f = fopen (BASEDIR "/error.txt", "w");
	if (f) { fprintf (f, "%s\n", text); fclose (f); }
	exit (1);
}
void Sys_Printf (char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vfprintf (stderr, fmt, ap);
	va_end (ap);
}
void Sys_Quit (void)
{
	Host_Shutdown ();
	exit (0);
}
double Sys_FloatTime (void)
{
	unsigned long t, hz;
	static unsigned long t0;
	__asm__ volatile ("isb\n mrs %0, cntpct_el0\n mrs %1, cntfrq_el0" : "=r" (t), "=r" (hz));
	if (!t0) t0 = t;
	return (double) (t - t0) / hz;
}

/* --- EmuKit --- */
static void read_args (char *prog)
{
	static char line[512];
	FILE *f;
	char *p;
	if (g_argc == 0) g_argv[g_argc++] = prog;
	f = fopen (BASEDIR "/args.txt", "r");
	if (!f) return;
	if (!fgets (line, sizeof line, f)) line[0] = 0;
	fclose (f);
	for (p = strtok (line, " \t\r\n"); p && g_argc < MAX_ARGS; p = strtok (NULL, " \t\r\n")) g_argv[g_argc++] = p;
}

static long load (void *ctx, const unsigned char *rom, long n)
{
	static quakeparms_t parms;
	struct stat st;

	mkdir (BASEDIR, 0777);
	mkdir (BASEDIR "/id1", 0777);
	if (stat (BASEDIR "/id1/pak0.pak", &st)) return 0;
	parms.memsize = MEM;
	parms.membase = g_mem;
	parms.basedir = BASEDIR;
	COM_InitArgv (g_argc, g_argv);
	parms.argc = com_argc;
	parms.argv = com_argv;
	Host_Init (&parms);
	/* (les cartes de LibreQuake sont plus chargees que celles de 1996) */
	Cvar_Set ("r_maxsurfs", "4000");
	Cvar_Set ("r_maxedges", "16000");
	g_last = Sys_FloatTime ();
	return 1;
}

static void frame (void *ctx, unsigned *px, long pitch, long pad, long flags)
{
	double now = Sys_FloatTime ();
	int before = host_framecount;
	double dt = now - g_last;

	r_stack_start = (byte *) &now;		/* (Host_Init a tourne sur le thread de la fenetre) */
	armpi_pad (pad);
	g_last = now;
	Host_Frame (dt);
	armpi_video_apply ();
	if (host_framecount == before) { emu_frame_same (); return; }	/* (trop tot : Host_FilterTime n'a rien fait) */
	emu_frame_size (vid.width, vid.height);
	armpi_present (px, pitch);
}

static void cmd (void *ctx, long id)
{
	if (id >= ID_RES && id < ID_RES + ARMPI_NRES) armpi_video_set ((int) id - ID_RES);
}

static const char menu[] =
	"MVideo\n"
	"I100\t320 x 200\n"
	"I101\t640 x 480\n"
	"I102\t800 x 600\n";

static struct emu_desc d;

/* (lang/fr.txt, dans le dictionnaire du systeme : ni UIKit ici, lang_tr d'AppKit par son rang) */
void *__armpi_appkit (unsigned rank);
static const char *lang_text (const char *s)
{
	const char *(*f) (const char *) = (const char *(*) (const char *)) __armpi_appkit (42);
	return f ? f (s) : s;
}

int main (int argc, char **argv)
{
	int i;
	long r;

	for (i = 0; i < argc && i < MAX_ARGS; i++) g_argv[g_argc++] = argv[i];
	read_args ("quake");
	emukit_kit_open ();
	d.name = "Quake";
	d.ext = "*.pak";
	d.w = 320;
	d.h = 200;
	d.maxw = ARMPI_MAXW;
	d.maxh = ARMPI_MAXH;
	d.fps_milli = 72000;
	d.rate = ARMPI_RATE;
	d.load = load;
	d.frame = frame;
	d.audio = armpi_audio;
	d.mouse = armpi_mouse;
	d.menu = menu;
	d.cmd = cmd;
	d.error = lang_text ("Quake needs its data: copy pak0.pak to SD0:/quake/id1 (see docs/QUAKE.md).");
	emu_game ((long) armpi_key, EMG_NOROM);
	r = emu_run ((long) &d, 0);
	if (!r) Host_Shutdown ();
	return (int) r;
}
