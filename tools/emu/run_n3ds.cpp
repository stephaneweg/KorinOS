// run_n3ds.cpp -- le banc commun du coeur 3DS : charge une ROM, un .3dsx ou un .elf, joue des images sans ecran et
// ecrit pour chacune le CRC des deux ecrans et le temps de la machine. Le meme source, relie au coeur d'Onyx et a
// Dynarmic pas a pas (ref_n3ds_cpu.cpp) ou au port (src/apps/n3dsemu), donne deux journaux a comparer
// (check_n3ds.sh). Sous qemu-aarch64, avec les appels systeme de Linux (shim d'Onyx).
//     run_n3ds <rom> <images> [touches] [dossier des ppm] [une image sur n]
//     touches : "60:8,70:0,300:1" : a partir de l'image 60 Start (les bits BTN_* de n3ds.h)
// Sous ARMPI (N3DS_PI, tools/emu/build.sh) : --gl, Mesa ; --dry, a blanc ; --soft, le rendu logiciel. Le temps par image,
// sans cadence ni fenetre, toutes les 60 images. Aucun fichier n'est ecrit hors du dossier des ppm : pas de .sav.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "n3ds/n3ds.h"
#ifdef N3DS_PI
#include "n3ds/n3ds_gl.h"
void n3ds_gl_say (FILE *o);
#endif

using namespace n3ds;
#ifdef N3DS_PORT
namespace n3ds { void arm11Say (Cpu *, FILE *); }
#endif

static unsigned crc32 (const void *d, size_t n)
{
	static unsigned t[256];
	if (!t[1]) for (unsigned i = 0; i < 256; i++) { unsigned c = i; for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1))); t[i] = c; }
	unsigned c = 0xFFFFFFFFu;
	const unsigned char *p = (const unsigned char *) d;
	for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ c >> 8;
	return ~c;
}
static bool fileRead (void *user, u64 off, void *dst, u32 n) { FILE *f = (FILE *) user; return fseek (f, (long) off, SEEK_SET) == 0 && fread (dst, 1, n, f) == n; }
static void debugOut (void *, const char *text, u32 len) { fwrite (text, 1, len, stderr); }
static void ppm (const char *dir, int frame, const u32 *top, const u32 *bot)
{
	char name[512]; snprintf (name, sizeof name, "%s/f%05d.ppm", dir, frame);
	FILE *o = fopen (name, "wb");
	if (!o) return;
	fprintf (o, "P6\n%d %d\n255\n", TOP_W, SCREEN_H * 2);
	static unsigned char row[TOP_W * 3];
	for (int y = 0; y < SCREEN_H * 2; y++)
	{
		memset (row, 0, sizeof row);
		for (int x = 0; x < TOP_W; x++)
		{
			u32 v = 0;
			if (y < SCREEN_H) v = top[y * TOP_W + x];
			else if (x >= (TOP_W - BOTTOM_W) / 2 && x < (TOP_W + BOTTOM_W) / 2) v = bot[(y - SCREEN_H) * BOTTOM_W + x - (TOP_W - BOTTOM_W) / 2];
			row[x * 3] = (unsigned char) (v >> 16); row[x * 3 + 1] = (unsigned char) (v >> 8); row[x * 3 + 2] = (unsigned char) v;
		}
		fwrite (row, 1, sizeof row, o);
	}
	fclose (o);
}


// N3DS_GLDUMP=<prefixe> N3DS_GLFRAMES=<a>-<b> : chaque image de ces trames, telle que le GPU la recevrait, dans un
// fichier, suivie de ce qu'en fait le rendu logiciel (tools/emu/n3ds_replay.cpp). Le rendu logiciel dessine toujours.
static int g_frameNow, g_dumpA = -1, g_dumpB, g_dumpN;
static FILE *g_dumpF;
static bool g_trust;
static u32 g_dumpW;
static bool dumpDraw (void *, const GpuFrame *f, u32 *pixels)
{
	if (g_frameNow < g_dumpA || g_frameNow > g_dumpB) return g_trust;
	char name[512]; snprintf (name, sizeof name, "%s_%05d_%03d.gld", getenv ("N3DS_GLDUMP"), g_frameNow, g_dumpN++);
	g_dumpF = fopen (name, "wb");
	if (!g_dumpF) return false;
	const u32 head[9] = { 0x31444C47, (u32) f->w, (u32) f->h, f->nPrograms, f->nTextures, f->nb, f->nu, f->nFloats, f->color };
	fwrite (head, 4, 9, g_dumpF);
	for (u32 i = 0; i < f->nPrograms; i++) { fwrite (&f->programs[i].nWords, 4, 1, g_dumpF); fwrite (&f->programs[i].nVary, 4, 1, g_dumpF); fwrite (f->programs[i].words, 8, f->programs[i].nWords, g_dumpF); }
	for (u32 i = 0; i < f->nTextures; i++)
	{
		bool used = false;
		for (u32 b = 0; b < f->nb; b++) for (int k = 0; k < 3; k++) if (f->b[b].tex[k] == (int) i) used = true;
		const u32 wh[3] = { used && f->textures[i].px ? f->textures[i].w : 0, f->textures[i].h, f->textures[i].serial };
		fwrite (wh, 4, 3, g_dumpF);
		if (wh[0]) fwrite (f->textures[i].px, 4, (size_t) wh[0] * wh[1], g_dumpF);
	}
	fwrite (&f->nProcs, 4, 1, g_dumpF);
	for (u32 i = 0; i < f->nProcs; i++) { fwrite (&f->procs[i].serial, 4, 1, g_dumpF); if (f->procs[i].map) { fwrite (f->procs[i].map, 4, 4 * 128, g_dumpF); fwrite (f->procs[i].colors, 4, 256, g_dumpF); } else { static float z[4 * 128 + 256]; fwrite (z, 4, 4 * 128 + 256, g_dumpF); } }
	fwrite (&f->nVPrograms, 4, 1, g_dumpF);
	for (u32 i = 0; i < f->nVPrograms; i++) { fwrite (&f->vprograms[i].nvs, 4, 1, g_dumpF); fwrite (&f->vprograms[i].inputs, 4, 1, g_dumpF); fwrite (f->vprograms[i].vs, 8, f->vprograms[i].nvs, g_dumpF); }
	fwrite (f->b, sizeof (GpuBatch), f->nb, g_dumpF);
	fwrite (f->u, 4, f->nu, g_dumpF);
	fwrite (f->v, 4, f->nFloats, g_dumpF);
	fwrite (pixels, 4, (size_t) f->w * (size_t) f->h, g_dumpF);
	if (g_trust)								// (N3DS_GLTRUST : le GPU est cense avoir dessine, rien n'est compare au rendu logiciel)
	{
		fwrite (pixels, 4, (size_t) f->w * (size_t) f->h, g_dumpF);
		fclose (g_dumpF); g_dumpF = 0;
		return true;
	}
	return false;
}
static void dumpSoft (void *, const u32 *px, int w, int h)
{
	if (!g_dumpF) return;
	fwrite (px, 4, (size_t) w * (size_t) h, g_dumpF);
	fclose (g_dumpF); g_dumpF = 0;
}

int main (int argc, char **argv)
{
#ifdef N3DS_PI
	bool useGl = false, novs = false;
	{
		int n = 1;
		for (int i = 1; i < argc; i++)
			if (!strcmp (argv[i], "--gl")) useGl = true;
			else if (!strcmp (argv[i], "--dry")) { useGl = true; n3ds_gl_dry = 1; }
			else if (!strcmp (argv[i], "--novs")) novs = true;
			else if (strcmp (argv[i], "--soft")) argv[n++] = argv[i];
		argc = n;
	}
#endif
	if (argc < 3) { fprintf (stderr, "run_n3ds rom images [touches] [dossier] [pas]\n"); return 2; }
	static Machine *m = new Machine;
	if (!m->init ()) { fprintf (stderr, "init\n"); return 1; }
	m->debugOut = debugOut;
#ifdef N3DS_PORT
	m->texCheck = getenv ("N3DS_TEXCHECK") != 0;
#endif
	if (getenv ("N3DS_GLDUMP"))
	{
		sscanf (getenv ("N3DS_GLFRAMES"), "%d-%d", &g_dumpA, &g_dumpB);
		m->gpuDraw = dumpDraw; m->gpuSoft = dumpSoft;
		g_trust = getenv ("N3DS_GLTRUST") != 0;
		if (getenv ("N3DS_GLVS")) { m->gpuVS = true; m->gpuIndexed = true; m->parallelBegin = [] (void *, void (*) (void *, int), void *) { return false; }; }
	}
	FILE *ff = fopen (getenv ("N3DS_FONT") ? getenv ("N3DS_FONT") : "SD0:/n3dstest/sysfont.bcfnt", "rb");
	if (ff)
	{
		static u8 font[1 << 20];
		const size_t k = fread (font, 1, sizeof font, ff);
		fclose (ff);
		if (k) m->setSharedFont (font, (u32) k);
	}
	FILE *f = fopen (argv[1], "rb");
	if (!f) { fprintf (stderr, "%s ?\n", argv[1]); return 1; }
	fseek (f, 0, SEEK_END);
	Source src = { f, (u64) ftell (f), fileRead };
	if (!m->loadFrom (src)) { fprintf (stderr, "chargement : %s\n", m->lastError); return 1; }
	const int frames = atoi (argv[2]);
	const char *keys = argc > 3 ? argv[3] : "", *dir = argc > 4 ? argv[4] : 0;
	const int every = argc > 5 ? atoi (argv[5]) : 60;
#ifdef N3DS_PI
	m->monoOnly = true;
	if (useGl)
	{
		const int gl = n3ds_gl_setup (m);
		if (novs) m->gpuVS = false;
		printf ("GPU : %d\n", gl);
		fflush (stdout);
	}
	struct timeval tv; gettimeofday (&tv, 0);
	unsigned long long t0 = (unsigned long long) tv.tv_sec * 1000000ull + (unsigned long long) tv.tv_usec, lists0 = 0, trans0 = 0, fills0 = 0, gpu0 = 0, soft0 = 0, usGpu0 = 0, vs0 = 0, same0 = 0;
#endif
	static u32 top[TOP_W * SCREEN_H], bot[BOTTOM_W * SCREEN_H];
	u32 held = 0;
	unsigned allCrc = 0;
	for (int n = 0; n < frames && !m->exited; n++)
	{
		for (const char *e = keys; *e; )
		{
			const int at = atoi (e);
			const char *c = strchr (e, ':');
			if (!c) break;
			if (at == n) held = (u32) strtoul (c + 1, 0, 0);
			const char *nx = strchr (c, ',');
			if (!nx) break;
			e = nx + 1;
		}
		g_frameNow = n;
		m->setInput (held, 0, 0, false, 0, 0);
		m->runFrame ();
		m->gpuSync ();
		m->screenImage (SCREEN_TOP, top); m->screenImage (SCREEN_BOTTOM, bot);
#ifdef N3DS_PI
		if ((n % 60) == 59)
		{
			gettimeofday (&tv, 0);
			const unsigned long long t1 = (unsigned long long) tv.tv_sec * 1000000ull + (unsigned long long) tv.tv_usec;
			printf ("f %d : %.2f ms par image, listes %.2f, transferts %.2f, remplissages %.2f, GPU %.2f ; images GPU %llu, logiciel %llu, memes %llu ; sommets GLSL %llu\n", n + 1, (double) (t1 - t0) / 60000.0,
				(double) (m->gsp.usLists - lists0) / 60000.0, (double) (m->gsp.usTransfers - trans0) / 60000.0, (double) (m->gsp.usFills - fills0) / 60000.0, (double) (m->gsp.usGpu - usGpu0) / 60000.0,
				(unsigned long long) (m->gsp.gpuFrames - gpu0), (unsigned long long) (m->gsp.softFrames - soft0), (unsigned long long) (m->gsp.sameFrames - same0), (unsigned long long) (m->gsp.vsVertices - vs0));
			fflush (stdout);
			lists0 = m->gsp.usLists; trans0 = m->gsp.usTransfers; fills0 = m->gsp.usFills; gpu0 = m->gsp.gpuFrames; soft0 = m->gsp.softFrames; usGpu0 = m->gsp.usGpu; vs0 = m->gsp.vsVertices; same0 = m->gsp.sameFrames;
			gettimeofday (&tv, 0);
			t0 = (unsigned long long) tv.tv_sec * 1000000ull + (unsigned long long) tv.tv_usec;
		}
#else
		const unsigned a = crc32 (top, sizeof top), b = crc32 (bot, sizeof bot);
		allCrc = crc32 (&a, 4) ^ allCrc * 31 ^ b;
		printf ("f %d %08x %08x now %llu svc %llu lists %u\n", n, a, b, (unsigned long long) m->now, (unsigned long long) m->svcCount, (unsigned) m->gsp.cmdLists);
#endif
		if (dir && (n % every) == every - 1) ppm (dir, n, top, bot);
#ifndef N3DS_PI
		if (!(n & 63)) fflush (stdout);
#endif
	}
	printf ("fin %08x now %llu svc %llu\n", allCrc, (unsigned long long) m->now, (unsigned long long) m->svcCount);
	fprintf (stderr, "fin : %s %s\n", m->lastError, m->notes);
#ifdef N3DS_PI
	n3ds_gl_say (stdout);
#endif
#ifdef N3DS_PORT
	if (m->texCheck) fprintf (stderr, "textures : %llu texels, %llu faux\n", (unsigned long long) m->texChecked, (unsigned long long) m->texWrong);
	if (getenv ("N3DS_SAY")) arm11Say (m->cpu, stderr);
#endif
	fflush (0);
	_Exit (0);
}
