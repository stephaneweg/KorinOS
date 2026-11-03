// ref_n64 : le coeur C++ du R4300, la reference de l'assembleur (tools/emu/n64_harness.asm), sous qemu-aarch64 :
//     ref_n64 rom images [mode]    (j : le recompilateur ; s : le rendu logiciel et l'ecriture des images en RDRAM, comme l'application ; t : sans sommes ni sorties)
// Une ligne par image : numero, cycles, sommes des registres, de la RDRAM (toutes les 32 images), de l'image, pc,
// sommes du son, des sommets, des lots, du rendu logiciel. Toutes les sorties doivent etre egales.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include "n64/n64.h"
#include "bas3d.h"

static unsigned fnv (const void *p, size_t n, unsigned h = 0x811C9DC5)
{
	const unsigned char *d = (const unsigned char *) p;
	for (size_t i = 0; i < n; i += 4) h = (h ^ d[i] ^ (d[i + 1] << 8) ^ (d[i + 2] << 16) ^ ((unsigned) d[i + 3] << 24)) * 16777619u;
	return h;
}
static void *code_alloc (unsigned long n)
{
	void *p = mmap (0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
}
// comme l'application
static bool sw_frame (n64::Machine *g_m, unsigned *px, int w, int h, int stride)
{
	if (g_m->gfxReady < 0) return false;
	const n64::GFrame &F = g_m->gfxFrame[g_m->gfxReady];
	int W = F.width, H = F.height;
	if (W <= 0 || H <= 0 || W > n64::FB_MAX_W || H > n64::FB_MAX_H) return false;
	static unsigned *pic; static float *zb;
	if (!pic) { pic = new unsigned[n64::FB_MAX_W * n64::FB_MAX_H]; zb = new float[n64::FB_MAX_W * n64::FB_MAX_H]; }
	static bas::G3Batch bt[n64::GFrame::MAXB];
	static bas::G3Texture tx[n64::Machine::MAX_TEX];
	for (int i = 0; i < F.nb; i++)
	{
		const n64::GBatch &B = F.b[i];
		bt[i].first = B.first; bt[i].count = B.count; bt[i].flags = B.flags;
		bt[i].texture = B.tex >= 0 ? B.tex + 1 : 0;
		for (int k = 0; k < 16; k++) bt[i].m[k] = B.m[k];
	}
	for (int i = 0; i < n64::Machine::MAX_TEX; i++)
	{
		int tw = g_m->tex[i].w, th = g_m->tex[i].h;
		if (tw <= 0 || th <= 0 || tw * th > (int) n64::Machine::TEX_PIXELS) tw = th = 1;
		tx[i].px = g_m->tex[i].px; tx[i].w = tw; tx[i].h = th;
	}
	bas::swRender (pic, W, H, W, zb, (const bas::G3Vertex *) F.v, F.nv, bt, F.nb, F.clear, false,
		       [] (int t) -> const bas::G3Texture * { return &tx[t - 1]; });
	for (int y = 0; y < h; y++)
	{
		const unsigned *r = pic + (y * H / h) * W;
		unsigned *d = px + (long) y * stride;
		for (int x = 0; x < w; x++) d[x] = r[x * W / w];
	}
	return true;
}
int main (int argc, char **argv)
{
	FILE *f = fopen (argv[1], "rb");
	if (!f) return 1;
	static unsigned char rom[1 << 26];
	unsigned n = (unsigned) fread (rom, 1, sizeof rom, f);
	fclose (f);
	int frames = atoi (argv[2]);
	n64::Machine *m = new n64::Machine;
	m->setAudioRate (44100);
	if (!m->load (rom, n)) { puts ("load failed"); return 1; }
	const char *mode = argc > 3 ? argv[3] : "";
	if (strchr (mode, 'j') && !m->jitEnable (code_alloc)) { puts ("pas de jit"); return 1; }
	bool sw = strchr (mode, 's'), quiet = strchr (mode, 't');
	static unsigned px[640 * 480];
	unsigned lastSerial = 0, age = 1000, drawn = ~0u;
	static short pcm[8192 * 2];
	struct timespec t0, t1;
	clock_gettime (CLOCK_MONOTONIC, &t0);
	for (int i = 0; i < frames; i++)
	{
		unsigned b = ((unsigned) i >> 5) * 2654435761u >> 16;
		m->setPad (0, (i & 0x100) ? b & 0xFFFF : 0, 0, 0);
		m->runFrame ();
		unsigned sh = 0;
		if (sw)
		{
			if (m->gfxSerial != lastSerial) { lastSerial = m->gfxSerial; age = 0; }
			else if (age < 1000) age++;
			if (age < 30 && drawn != m->gfxSerial && sw_frame (m, px, 640, 480, 640))
			{
				drawn = m->gfxSerial;
				m->fbSnapshot (px, 640, 480, 640);
				sh = fnv (px, sizeof px);
			}
		}
		unsigned ah = 0x811C9DC5;
		int got;
		while ((got = m->audioRead (pcm, 8192)) > 0) ah = fnv (pcm, got * 4, ah);
		if (quiet) continue;
		unsigned vh = 0x811C9DC5, bh = 0x811C9DC5;
		if (m->gfxReady >= 0)
		{
			n64::GFrame &g = m->gfxFrame[m->gfxReady];
			vh = fnv (g.v, g.nv * sizeof (n64::GVertex));
			bh = fnv (g.b, g.nb * sizeof (n64::GBatch));
		}
		printf ("%d %llu %08x %08x %08x %u %08x %08x %08x %08x\n", i, m->cycles, fnv (m->r, sizeof m->r, fnv (m->fpr, sizeof m->fpr)),
			(i & 31) ? 0 : fnv (m->rdram, n64::RDRAM_SIZE), fnv (m->fb, (size_t) m->fbW * m->fbH * 4), m->pc, ah, vh, bh, sh);
	}
	clock_gettime (CLOCK_MONOTONIC, &t1);
	unsigned long fl = 0, c = m->jitStats (&fl);
	fprintf (stderr, "%.2f s, %lu blocs, %lu vidages, %llu cycles\n", (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9, c, fl, m->cycles);
	return 0;
}
