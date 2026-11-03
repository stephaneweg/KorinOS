// ref_gc -- le coeur GameCube (src/apps/gcemu/gc) sous qemu-aarch64, sans fenetre : la reference et le
// harnais de tools/emu/check_gc.sh, tools/emu/time_gc.sh.
//     ref_gc <iso | dol> <trames> [toutes]
// Une ligne par `toutes` trames (defaut 1) : numero, somme de l'image du VI, somme de MEM1, cycles, pc.
//   GC_JIT=0    l'interprete ; GC_SNAP=n : une image PPM toutes les n trames (snap_<n>.ppm)
//   GC_PAD="f0-f1:hex;..." : boutons (Machine::PAD_*) tenus de la trame f0 a f1 ; 0x10000 / 0x20000 /
//   0x40000 / 0x80000 : le stick a gauche / droite / haut / bas (comme le gcrun d'Onyx)
//   GC_CARD=fichier : carte memoire en A, lue, jamais reecrite ; GC_MEM=0 : pas de somme de MEM1
//   GC_REC=1 : l'enregistreur du GX (gxes.h), sans GL : la somme de chaque image enregistree (sommets, lots,
//   uniformes) et des textures decodees, le moyen de juger le code asm du GX contre le C++
#include "gc/gc.h"
#include "gxes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
using namespace gc;

static void *hostCode (u32 n)
{
	void *p = mmap (0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
}
static FILE *g_f;
static bool discRead (void *, u32 off, u32 len, u8 *dst)
{
	return !fseeko (g_f, off, SEEK_SET) && fread (dst, 1, len, g_f) == len;
}
static u64 hash (const void *p, size_t n)
{
	const u32 *w = (const u32 *) p;
	u64 h = 0xCBF29CE484222325ull;
	for (size_t i = 0; i < n / 4; i++) { h ^= w[i]; h *= 0x100000001B3ull; }
	return h;
}
struct PadStep { int f0, f1; u32 b; };
static PadStep g_pad[256]; static int g_npad;
static void padParse (const char *s)
{
	while (s && *s && g_npad < 256)
	{
		PadStep &p = g_pad[g_npad];
		if (sscanf (s, "%d-%d:%x", &p.f0, &p.f1, &p.b) == 3) g_npad++;
		s = strchr (s, ';');
		if (s) s++;
	}
}
static void padAt (Machine &m, int f)
{
	u32 b = 0;
	for (int i = 0; i < g_npad; i++) if (f >= g_pad[i].f0 && f <= g_pad[i].f1) b |= g_pad[i].b;
	int sx = (b & 0x10000) ? -100 : (b & 0x20000) ? 100 : 0, sy = (b & 0x80000) ? -100 : (b & 0x40000) ? 100 : 0;
	m.setPad (0, b & 0xFFFF, sx, sy, 0, 0, 0, 0);
}
static void snap (Machine &m, int f)
{
	char n[64]; snprintf (n, sizeof n, "snap_%d.ppm", f);
	FILE *o = fopen (n, "wb");
	if (!o) return;
	fprintf (o, "P6\n%d %d\n255\n", m.fbW, m.fbH);
	for (int i = 0; i < m.fbW * m.fbH; i++) { u32 c = m.fb[i]; u8 t[3] = { (u8) (c >> 16), (u8) (c >> 8), (u8) c }; fwrite (t, 1, 3, o); }
	fclose (o);
}
static u64 hashB (const void *p, size_t n, u64 h = 0xCBF29CE484222325ull)
{
	const u8 *b = (const u8 *) p;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001B3ull; }
	return h;
}
// chaque image finie est sommee, puis oubliee
static gxes::Rec g_rec;
static u64 g_recHash = 0xCBF29CE484222325ull, g_recVerts; static u32 g_recFrames;
struct RecTee : GxGpu
{
	void draw (Machine &m, const GxState &s, const GxVertex *v, int nv, const u32 *idx, int ni, int prim) override { g_rec.draw (m, s, v, nv, idx, ni, prim); }
	void copy (Machine &m, const GxCopy &c) override
	{
		bool end = c.toXfb;
		g_rec.copy (m, c);
		if (!end) return;
		gxes::Frame &F = g_rec.frame[g_rec.rd % gxes::NFRAMES];
		g_recHash = hashB (F.v, (size_t) F.nf * 4, g_recHash);
		for (u32 i = 0; i < F.nb; i++)
		{
			gxes::Batch b = F.b[i];
			b.lp = 0;
			g_recHash = hashB (&b, sizeof b, g_recHash);
		}
		g_recHash = hashB (F.u, (size_t) F.nu * sizeof (gxes::Uni), g_recHash);
		for (int k = 0; k < Machine::MAX_TEX; k++)
			if (m.tex[k].dirty && m.tex[k].px)
			{
				g_recHash = hashB (m.tex[k].px, (size_t) m.tex[k].w * m.tex[k].h * 4, g_recHash);
				m.tex[k].dirty = false;
			}
		g_recVerts += F.nf; g_recFrames++;
		g_rec.rd = g_rec.rd + 1;
	}
};
static RecTee g_recTee;
static double now () { timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main (int argc, char **argv)
{
	if (argc < 3) return 2;
	const char *path = argv[1];
	int fields = atoi (argv[2]), every = argc > 3 ? atoi (argv[3]) : 1;
	static Machine *mp = new Machine;
	Machine &m = *mp;
	padParse (getenv ("GC_PAD"));
	g_f = fopen (path, "rb");
	if (!g_f) { fprintf (stderr, "pas de %s\n", path); return 1; }
	fseeko (g_f, 0, SEEK_END);
	long sz = ftello (g_f);
	fseeko (g_f, 0, SEEK_SET);
	static u8 card[Machine::CARD_SIZE];
	memset (card, 0xFF, sizeof card);
	if (const char *c = getenv ("GC_CARD")) { FILE *cf = fopen (c, "rb"); if (cf) { fread (card, 1, sizeof card, cf); fclose (cf); } }
	m.cardInsert (0, card, sizeof card);
	m.setAudioRate (48000);
	bool ok;
	size_t n = strlen (path);
	if (n > 4 && !strcmp (path + n - 4, ".dol"))
	{
		u8 *d = new u8[sz];
		ok = fread (d, 1, sz, g_f) == (size_t) sz && m.loadDol (d, (u32) sz);
	}
	else { m.discRead = discRead; ok = m.loadDiscImage ((u32) sz); }
	if (!ok) { fprintf (stderr, "chargement refuse\n"); return 1; }
	codeAlloc = hostCode;
	const char *j = getenv ("GC_JIT");
	if (!j || *j != '0') { if (!m.jitEnable ()) fprintf (stderr, "pas de JIT\n"); }
	if (const char *r = getenv ("GC_REC")) if (*r == '1') { g_rec.init (); m.gpu = &g_recTee; }
	const char *sn = getenv ("GC_SNAP"), *me = getenv ("GC_MEM");
	int snapN = sn ? atoi (sn) : 0;
	bool hashMem = !me || *me != '0';
	static s16 pcm[8192];
	u64 audio = 0xCBF29CE484222325ull;
	double t0 = now ();
	for (int f = 1; f <= fields; f++)
	{
		padAt (m, f);
		m.runFrame ();
		int k;
		while ((k = m.audioRead (pcm, 4096)) > 0) for (int i = 0; i < k * 2; i++) { audio ^= (u16) pcm[i]; audio *= 0x100000001B3ull; }
		if (snapN && f % snapN == 0) snap (m, f);
		if (f % every == 0)
			printf ("%d fb %dx%d %016llx mem %016llx son %016llx cyc %llu pc %08x cmd %u prim %u vtx %u copies %u tex %u\n", f, m.fbW, m.fbH,
				(unsigned long long) hash (m.fb, (size_t) m.fbW * m.fbH * 4), hashMem ? (unsigned long long) hash (m.mem1, MEM1_SIZE) : 0ull,
				(unsigned long long) audio, (unsigned long long) m.cycles, m.pc, m.gxCmds, m.gxPrims, m.gxVerts, m.gxCopies, m.texDecodes);
		if (f % every == 0 && m.gpu)
			printf ("  rec %016llx images %u flottants %llu programmes %d\n", (unsigned long long) g_recHash, g_recFrames, (unsigned long long) g_recVerts, g_rec.nProg);
		if (m.halted) { printf ("arret : %s\n", m.haltMsg); break; }
	}
	char st[256]; m.status (st, sizeof st);
	fprintf (stderr, "%s\n%d trames en %.1f s\n", st, fields, now () - t0);
	return 0;
}
