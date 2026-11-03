/*
 * harness.c -- Quake sans fenetre, sous qemu-aarch64 : le temps est compte (1/60 s par image), rien ne depend de
 * l'horloge, et chaque image donne une ligne de sommes. Les deux constructions de ce fichier doivent ecrire les memes
 * lignes : le C d'id (ref_quake) et les routines A64 (asm_quake). Voir tools/quake/check.sh.
 *     harness [t] basedir images largeur hauteur commande...      commande : playdemo demo1, map start...
 *     t : sans sommes, le temps par image a la fin (tools/quake/time.sh)
 * Reprend les pilotes nuls d'id (sys_null.c, vid_null.c, in_null.c, snd_null.c), GPL : third_party/quake/gnu.txt.
 */
#include "quakedef.h"
#include "d_local.h"
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#define MAXW 800
#define MAXH 600
#define MEM (96 * 1024 * 1024)

static byte mem[MEM];
static byte vid_buffer[MAXW * MAXH];
static short zbuffer[MAXW * MAXH];
static byte surfcache[8 * 1024 * 1024];
static double fake_time;
qboolean isDedicated;
unsigned short d_8to16table[256];
unsigned d_8to24table[256];
static unsigned pal[256];
static int W = 320, H = 200;

/* --- fichiers --- */
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
	handles[i] = f; *hndl = i;
	fseek (f, 0, SEEK_END); end = ftell (f); fseek (f, 0, SEEK_SET);
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
	FILE *f = fopen (path, "rb");
	if (f) { fclose (f); return 1; }
	return -1;
}
void Sys_mkdir (char *path) { mkdir (path, 0777); }

/* --- systeme --- */
void Sys_MakeCodeWriteable (unsigned long a, unsigned long n) {}
void Sys_Error (char *error, ...)
{
	va_list ap;
	fprintf (stderr, "Sys_Error: ");
	va_start (ap, error); vfprintf (stderr, error, ap); va_end (ap);
	fprintf (stderr, "\n");
	exit (1);
}
void Sys_Printf (char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt); vfprintf (stderr, fmt, ap); va_end (ap);
}
void Sys_Quit (void) { exit (0); }
double Sys_FloatTime (void) { return fake_time; }
char *Sys_ConsoleInput (void) { return NULL; }
void Sys_Sleep (void) {}
void Sys_SendKeyEvents (void) {}
void Sys_HighFPPrecision (void) {}
void Sys_LowFPPrecision (void) {}

/* --- image --- */
void VID_SetPalette (unsigned char *p)
{
	int i;
	for (i = 0; i < 256; i++, p += 3) pal[i] = p[0] << 16 | p[1] << 8 | p[2];
}
void VID_ShiftPalette (unsigned char *p) { VID_SetPalette (p); }
void VID_Init (unsigned char *palette)
{
	vid.width = vid.conwidth = W;
	vid.height = vid.conheight = H;
	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.aspect = 1.0;
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *) vid.colormap + 2048));
	vid.buffer = vid.conbuffer = vid_buffer;
	vid.rowbytes = vid.conrowbytes = W;
	d_pzbuffer = zbuffer;
	D_InitCaches (surfcache, sizeof surfcache);
	VID_SetPalette (palette);
}
void VID_Shutdown (void) {}
void VID_Update (vrect_t *rects) {}
int VID_SetMode (int m, unsigned char *p) { return 1; }
void VID_HandlePause (qboolean p) {}
void D_BeginDirectRect (int x, int y, byte *b, int w, int h) {}
void D_EndDirectRect (int x, int y, int w, int h) {}

/* --- entrees --- */
void IN_Init (void) {}
void IN_Shutdown (void) {}
void IN_Commands (void) {}
void IN_Move (usercmd_t *cmd) {}

/* --- son : un tampon lu au rythme du temps compte --- */
#define DMA_SAMPLES (1 << 16)
static short dma[DMA_SAMPLES];
extern volatile dma_t *shm;
extern volatile dma_t sn;
qboolean SNDDMA_Init (void)
{
	shm = &sn;
	shm->splitbuffer = 0;
	shm->samplebits = 16;
	shm->speed = 44100;
	shm->channels = 2;
	shm->samples = DMA_SAMPLES;
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = (unsigned char *) dma;
	return true;
}
int SNDDMA_GetDMAPos (void)
{
	long n = (long) (fake_time * 44100.0) * 2;
	return shm->samplepos = n & (DMA_SAMPLES - 1);
}
void SNDDMA_Shutdown (void) {}
void SNDDMA_Submit (void) {}

/* --- la boucle --- */
static unsigned fnv (const void *p, long n, unsigned h)
{
	const byte *d = p;
	long i;
	for (i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
	return h;
}
int main (int argc, char **argv)
{
	static quakeparms_t parms;
	static char *av[64];
	static char cmd[256];
	char *base;
	int i, ac, frames, silent = 0;
	struct timespec t0, t1;

	if (argc > 1 && !strcmp (argv[1], "t")) { silent = 1; argc--; argv++; }
	if (argc < 6) { fprintf (stderr, "harness [t] basedir images largeur hauteur commande...\n"); return 2; }
	base = argv[1];
	frames = atoi (argv[2]);
	W = atoi (argv[3]);
	H = atoi (argv[4]);
	for (i = 5; i < argc; i++) { strcat (cmd, argv[i]); strcat (cmd, i + 1 < argc ? " " : "\n"); }

	parms.memsize = MEM;
	parms.membase = mem;
	parms.basedir = base;
	ac = 0;
	av[ac++] = "quake";
	if (getenv ("QUAKE_ARGS"))		/* (comme SD0:/quake/args.txt) */
	{
		char *p;
		for (p = strtok (getenv ("QUAKE_ARGS"), " "); p && ac < 64; p = strtok (NULL, " ")) av[ac++] = p;
	}
	COM_InitArgv (ac, av);
	parms.argc = com_argc;
	parms.argv = com_argv;
	Host_Init (&parms);
	Cvar_Set ("r_maxsurfs", "4000");	/* (comme src/apps/quake/main.c) */
	Cvar_Set ("r_maxedges", "16000");
	Cbuf_AddText (cmd);
	clock_gettime (CLOCK_MONOTONIC, &t0);
	for (i = 0; i < frames; i++)
	{
		unsigned a, c;
		static unsigned b;
		fake_time += 1.0 / 60.0;
		Host_Frame (1.0 / 60.0);
		if (silent) continue;
		if (getenv ("QUAKE_DUMP") && i == atoi (getenv ("QUAKE_DUMP")))
		{
			/* l'image i en .ppm, dans le fichier QUAKE_DUMPF */
			FILE *f = fopen (getenv ("QUAKE_DUMPF"), "wb");
			int k;
			fprintf (f, "P6 %d %d 255\n", W, H);
			for (k = 0; k < W * H; k++)
			{
				unsigned c = pal[vid.buffer[k]];
				fputc (c >> 16, f); fputc (c >> 8, f); fputc (c, f);
			}
			fclose (f);
		}
		a = fnv (vid.buffer, W * H, 0x811C9DC5u);
		if (!(i & 15)) b = fnv (dma, sizeof dma, 0x811C9DC5u);	/* (l'anneau dure 44 images) */
		c = fnv (pal, sizeof pal, b);
		printf ("%d %08x %08x\n", i, a, b);
	}
	clock_gettime (CLOCK_MONOTONIC, &t1);
	if (silent)
		printf ("%.2f ms/image\n", ((t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / frames);
	return 0;
}
