/* emtest : essai différentiel d'EmuKit (src/kits/emukit), sans bibliothèque,
 * pour AArch64 (Korin sur le Pi) et x86-64 (Korin sur PC).
 *
 * Appelle les fonctions du kit par sa table et n'écrit que ce qui ne dépend ni
 * des adresses ni de l'heure : valeurs rendues, ce que le kit passe aux
 * fonctions de l'émulateur (un faux émulateur : sommes FNV-1a de la ROM, des
 * boutons, de la sauvegarde), et la vignette (--thumb) qu'il écrit. Le kit en
 * assembleur (build/lib/emukit.so) et le kit en C
 * (build/kits/<arch>/emukit.so) doivent donner des sorties identiques :
 *     SD0:/bin/emtest [-f]   (par telnet : tools/kits/run_pi.sh)
 *
 * Partie en mémoire (Pi et PC) : les petites fonctions de la table
 * (emu_pad_axes, emu_command, emu_rom_path, emu_frame_size, emu_frame_same,
 * emu_game, emu_pointer, emu_gl_hook) et emu_thread (un thread, son argument,
 * sa fin). emtest -f : en plus, après « === fichiers (RAM0:) », emu_run
 * --thumb (sans fenêtre : 900 images, la dernière réduite à 160 x 120) sur une
 * fausse ROM de RAM0:/emtest, avec sa sauvegarde, emu_rom_by_path, et des
 * erreurs (ROM absente, refusée, vignette impossible). emu_run ouvre AudioKit :
 * le PC ne l'a pas forcément, ni service de fichiers : ne comparer que ce qui
 * précède cette ligne. La fenêtre (emu_run sans --thumb) : par captures, avec
 * un vrai émulateur.
 *
 * Contrôles propres : « ECHEC ... », code de sortie = nombre d'échecs.
 * Construction : src/kits/emukit/test/build.sh (dans WSL). */
#include <stddef.h>
#include <stdint.h>

/* src/inc/syscalls.inc */
#define SYS_EXIT 0x10
#define SYS_YIELD 0x18
#define SYS_THREAD_JOIN 0x25
#define SYS_PRINT 0x60
#define SYS_LIB_OPEN 0x64

#define KIT_MAGIC 0x54494B41
/* AppKit (rangs de appkit.abi) */
#define AK_F_OPEN 14
#define AK_F_READ 15
#define AK_F_WRITE 16
#define AK_F_CLOSE 18
#define AK_F_MKDIR 21
#define AK_F_UNLINK 22
#define AK_F_PRINT 25    /* (version 26) */
#define AK_F_KIT_LOAD 31 /* (version 32) */

#define PAD_START (1 << 13)

/* rangs de emukit.abi */
enum {
	E_RUN, E_FRAME_SIZE, E_COMMAND, E_ROM_PATH, E_THREAD, E_PAD_AXES, E_FRAME_SAME, E_GAME, E_POINTER,
	E_ROM_BY_PATH, E_GL_HOOK, E_COUNT_FN
};

static const char *const fn_names[E_COUNT_FN] = {
	"emu_run", "emu_frame_size", "emu_command", "emu_rom_path", "emu_thread", "emu_pad_axes",
	"emu_frame_same", "emu_game", "emu_pointer", "emu_rom_by_path", "emu_gl_hook",
};

/* ---- appels système */

#if defined(__aarch64__)
static inline long sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
	return x0;
}
#elif defined(__x86_64__)
static inline long sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	long ret;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
			 : "rcx", "r11", "memory");
	return ret;
}
#else
#error "architecture non prise en charge"
#endif
#define sys1(n, a) sys6((n), (long)(a), 0, 0, 0, 0, 0)
#define sys2(n, a, b) sys6((n), (long)(a), (long)(b), 0, 0, 0, 0)

void *memset(void *d, int c, size_t n)
{
	volatile unsigned char *a = d;
	while (n--)
		*a++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	volatile unsigned char *a = d;
	const unsigned char *b = s;
	while (n--)
		*a++ = *b++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	if (a < b)
		while (n--)
			*a++ = *b++;
	else
		while (n--)
			a[n] = b[n];
	return d;
}

int memcmp(const void *x, const void *y, size_t n)
{
	const unsigned char *a = x, *b = y;
	for (; n; n--, a++, b++)
		if (*a != *b)
			return *a - *b;
	return 0;
}

static long slen(const char *s)
{
	long n = 0;
	while (s[n])
		n++;
	return n;
}

/* ---- AppKit */

static long ak = -1;
typedef long (*fn6)(long, long, long, long, long, long);

static long ak_call(int i, long a, long b, long c)
{
	if (ak < 0)
		return -38;
	const uint32_t *t = (const uint32_t *)ak;
	fn6 fn = (fn6)(ak + t[2 + i]);
	return fn(a, b, c, 0, 0, 0);
}

/* ---- sortie */

static char pb[4096];
static long pn;

static void flush(void)
{
	if (!pn)
		return;
	if (ak_call(AK_F_PRINT, (long)pb, pn, 0) < 0)
		sys2(SYS_PRINT, pb, pn);
	pn = 0;
}

static void pc(char c)
{
	if (pn == (long)sizeof pb)
		flush();
	pb[pn++] = c;
}

static void ps(const char *s)
{
	while (*s)
		pc(*s++);
}

static void pu(uint64_t v)
{
	char b[24];
	int i = 0;
	do
		b[i++] = (char)('0' + v % 10);
	while (v /= 10);
	while (i)
		pc(b[--i]);
}

static void pd(long v)
{
	if (v < 0) {
		pc('-');
		pu(-(uint64_t)v);
	} else {
		pu((uint64_t)v);
	}
}

static void px(uint64_t v, int digits)
{
	for (int i = digits - 1; i >= 0; i--)
		pc("0123456789abcdef"[(v >> (i * 4)) & 15]);
}

static void pq(const char *s, long max)
{
	pc('"');
	for (long i = 0; i < max && s[i]; i++) {
		unsigned c = (unsigned char)s[i];
		if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
			pc((char)c);
		} else {
			pc('\\');
			pc('x');
			px(c, 2);
		}
	}
	pc('"');
}

static void nl(void)
{
	pc('\n');
}

static void kv(const char *k, long v)
{
	pc(' ');
	ps(k);
	pc('=');
	pd(v);
}

static void ks(const char *k, const char *s, long max)
{
	pc(' ');
	ps(k);
	pc('=');
	pq(s, max);
}

static uint32_t fnv_add(uint32_t h, const void *p, long n)
{
	const unsigned char *s = p;
	for (long i = 0; i < n; i++) {
		h ^= s[i];
		h *= 16777619u;
	}
	return h;
}

static uint32_t fnv(const void *p, long n)
{
	return fnv_add(2166136261u, p, n);
}

static uint32_t fnv_mix(uint32_t h, uint64_t v)
{
	return fnv_add(h, &v, 8);
}

static void kh(const char *k, uint32_t h)
{
	pc(' ');
	ps(k);
	pc('=');
	px(h, 8);
}

static long checks, fails;

static void ck(int ok, const char *what)
{
	checks++;
	if (!ok) {
		fails++;
		ps("ECHEC ");
		ps(what);
		nl();
	}
}

/* ---- le kit */

static long kit;
static long called[E_COUNT_FN];

static long call(int i, long a, long b, long c, long d)
{
	const uint32_t *t = (const uint32_t *)kit;
	fn6 fn = (fn6)(kit + t[2 + i]);
	called[i]++;
	return fn(a, b, c, d, 0, 0);
}
#define L(x) ((long)(x))
#define C0(i) call(i, 0, 0, 0, 0)
#define C1(i, a) call(i, L(a), 0, 0, 0)
#define C2(i, a, b) call(i, L(a), L(b), 0, 0)
#define C4(i, a, b, c, d) call(i, L(a), L(b), L(c), L(d))

static long open_kit(const char *name, long min)
{
	long t = sys2(SYS_LIB_OPEN, name, min);
	if (t == -2) {
		long a = sys2(SYS_LIB_OPEN, "appkit", 32);
		if (a >= 0) {
			const uint32_t *at = (const uint32_t *)a;
			long (*load)(const char *) = (long (*)(const char *))(a + at[2 + AK_F_KIT_LOAD]);
			if (load(name) == 0)
				t = sys2(SYS_LIB_OPEN, name, min);
		}
	}
	return t;
}

/* ---- 1. en mémoire */

static volatile long th_seen;

static long th_fn(long arg)
{
	th_seen = arg * 3 + 1;
	return 77; /* (le trampolin du kit finit le thread avec 0) */
}

static void part_memory(void)
{
	ps("=== en memoire\n");
	ps("rom_path");
	ks("p", (const char *)C0(E_ROM_PATH), 600);
	nl();
	long ax = 0x1111;
	for (long i = 0; i < 6; i++) {
		ax = 0x1111;
		long r = C2(E_PAD_AXES, i, &ax);
		ps("pad_axes");
		kv("i", i);
		kv("r", r);
		kv("ax", ax);
		nl();
	}
	ps("petites");
	kv("command", C1(E_COMMAND, 5));
	kv("command0", C1(E_COMMAND, 0));
	kv("frame_size", C2(E_FRAME_SIZE, 320, 240));
	kv("frame_same", C0(E_FRAME_SAME));
	kv("game", C2(E_GAME, 0, 0));
	kv("pointer", C1(E_POINTER, 0));
	kv("gl_hook", C2(E_GL_HOOK, 0, 0));
	nl();
	for (long k = 0; k < 3; k++) {
		th_seen = 0;
		long tid = C4(E_THREAD, th_fn, 10 + k, 65536 << k, "emtest-t");
		long code = tid >= 0 ? sys1(SYS_THREAD_JOIN, tid) : tid;
		ps("thread");
		kv("k", k);
		kv("tid>0", tid > 0);
		kv("code", code);
		kv("vu", th_seen);
		nl();
		ck(th_seen == (10 + k) * 3 + 1, "emu_thread : argument");
	}
	ps("thread trop grand");
	kv("r", C4(E_THREAD, th_fn, 1, 1L << 46, "x"));
	nl();
}

/* ---- 2. fichiers : le faux émulateur */

struct emu_desc {
	const char *name, *ext;
	long w, h, maxw, maxh;
	long fps_milli, rate;
	void *ctx;
	long (*load)(void *ctx, const unsigned char *rom, long size);
	void (*frame)(void *ctx, unsigned *pixels, long pitch, long buttons, long flags);
	long (*audio)(void *ctx, short *lr, long frames);
	void (*reset)(void *ctx);
	long (*save)(void *ctx, const void **data);
	void (*restore)(void *ctx, const void *data, long size);
	const char *menu;
	void (*cmd)(void *ctx, long id);
	void (*mouse)(void *ctx, long kind, long x, long y, long buttons);
	void **bufs;
	long skip;
	const char *error;
};

static struct {
	long loads, load_size, load_ok, by_path;
	uint32_t load_h;
	long frames, last_pitch;
	uint32_t btn_h, flags_h;
	long audio_calls, audio_left;
	long restores, restore_size;
	uint32_t restore_h;
	long saves;
	long resize_at;
} st;

static long f_load(void *ctx, const unsigned char *rom, long size)
{
	(void)ctx;
	st.loads++;
	st.load_size = size;
	if (st.by_path)
		st.load_h = rom ? fnv(rom, slen((const char *)rom)) : 0;
	else
		st.load_h = rom ? fnv(rom, size) : 0;
	return st.load_ok;
}

static void f_frame(void *ctx, unsigned *px, long pitch, long buttons, long flags)
{
	(void)ctx;
	long n = st.frames++;
	st.last_pitch = pitch;
	st.btn_h = fnv_mix(st.btn_h, (uint64_t)buttons);
	st.flags_h = fnv_mix(st.flags_h, (uint64_t)flags);
	long w = 256, h = 224;
	if (st.resize_at && n >= st.resize_at) {
		w = 320;
		h = 200;
		if (n == st.resize_at)
			C2(E_FRAME_SIZE, w, h);
	}
	unsigned *row = px;
	for (long y = 0; y < h; y++) {
		for (long x = 0; x < w; x++)
			row[x] = (unsigned)((x * 3 + n) & 255) << 16 | (unsigned)((y * 5 + n * 2) & 255) << 8 |
				 (unsigned)((x ^ y) & 255);
		row = (unsigned *)((char *)row + pitch);
	}
	if (n == 10)
		C0(E_FRAME_SAME);
	if (n == 11) {
		long ax = -1;
		C2(E_PAD_AXES, 1, &ax);
		st.btn_h = fnv_mix(st.btn_h, (uint64_t)ax);
	}
}

static long f_audio(void *ctx, short *lr, long frames)
{
	(void)ctx;
	st.audio_calls++;
	for (long i = 0; i < 8; i++)
		lr[i] = (short)i;
	if (st.audio_left > 0) {
		st.audio_left--;
		return frames;
	}
	st.audio_left = 2;
	return 735;
}

static unsigned char save_data[700];

static long f_save(void *ctx, const void **data)
{
	(void)ctx;
	st.saves++;
	*data = save_data;
	return sizeof save_data;
}

static void f_restore(void *ctx, const void *data, long size)
{
	(void)ctx;
	st.restores++;
	st.restore_size = size;
	st.restore_h = fnv(data, size);
}

static unsigned char rom[100000];
static unsigned char filebuf[256 * 1024];

static long write_file(const char *path, const void *p, long n)
{
	long fd = ak_call(AK_F_OPEN, (long)path, 2 | 4 | 8, 0);
	if (fd < 0)
		return fd;
	long r = ak_call(AK_F_WRITE, fd, (long)p, n);
	ak_call(AK_F_CLOSE, fd, 0, 0);
	return r;
}

static long read_file(const char *path, unsigned char *buf, long cap)
{
	long fd = ak_call(AK_F_OPEN, (long)path, 1, 0);
	if (fd < 0)
		return fd;
	long n = 0;
	for (;;) {
		long r = ak_call(AK_F_READ, fd, (long)(buf + n), cap - n > 65536 ? 65536 : cap - n);
		if (r <= 0)
			break;
		n += r;
		if (n >= cap)
			break;
	}
	ak_call(AK_F_CLOSE, fd, 0, 0);
	return n;
}

static struct emu_desc desc;

static void run_thumb(const char *what, const char *args, const char *out)
{
	memset(&st, 0, sizeof st);
	st.load_ok = 1;
	st.resize_at = 600;
	ak_call(AK_F_UNLINK, (long)out, 0, 0);
	long r = C2(E_RUN, &desc, args);
	ps("thumb ");
	ps(what);
	kv("r", r);
	nl();
	ps(" ");
	kv("loads", st.loads);
	kv("size", st.load_size);
	kh("rom", st.load_h);
	kv("frames", st.frames);
	kv("pitch", st.last_pitch);
	kh("btn", st.btn_h);
	kh("flags", st.flags_h);
	kv("audio", st.audio_calls);
	kv("restores", st.restores);
	kv("rsize", st.restore_size);
	kh("rsav", st.restore_h);
	kv("saves", st.saves);
	nl();
	long n = read_file(out, filebuf, sizeof filebuf);
	ps(" vignette");
	kv("octets", n);
	if (n > 0)
		kh("somme", fnv(filebuf, n));
	ks("rom_path", (const char *)C0(E_ROM_PATH), 600);
	nl();
}

static void part_files(void)
{
	ps("=== fichiers (RAM0:)\n");
	ak_call(AK_F_MKDIR, (long)"RAM0:/emtest", 0, 0);
	for (long i = 0; i < (long)sizeof rom; i++)
		rom[i] = (unsigned char)(i * 13 + (i >> 8));
	for (long i = 0; i < (long)sizeof save_data; i++)
		save_data[i] = (unsigned char)(i ^ 0x5A);
	ps("ecrit");
	kv("rom", write_file("RAM0:/emtest/jeu.rom", rom, sizeof rom));
	kv("sav", write_file("RAM0:/emtest/jeu.sav", save_data, 333));
	kv("rom2", write_file("RAM0:/emtest/sanspoint", rom, 5000));
	kv("sav2", write_file("RAM0:/emtest/sanspoint.sav", save_data + 5, 44));
	nl();
	desc.name = "Essai";
	desc.ext = "*.rom";
	desc.w = 256;
	desc.h = 224;
	desc.maxw = 512;
	desc.maxh = 448;
	desc.fps_milli = 60099;
	desc.rate = 44100;
	desc.load = f_load;
	desc.frame = f_frame;
	desc.audio = f_audio;
	desc.save = f_save;
	desc.restore = f_restore;
	run_thumb("jeu", "--thumb RAM0:/emtest/th1.raw RAM0:/emtest/jeu.rom", "RAM0:/emtest/th1.raw");
	run_thumb("espaces", "   --thumb    RAM0:/emtest/th2.raw RAM0:/emtest/jeu.rom", "RAM0:/emtest/th2.raw");
	run_thumb("full", "--full --thumb RAM0:/emtest/th3.raw RAM0:/emtest/sanspoint", "RAM0:/emtest/th3.raw");
	ps("sav ecrit");
	long n = read_file("RAM0:/emtest/sanspoint.sav", filebuf, sizeof filebuf);
	kv("octets", n);
	nl();
	desc.maxw = 0;
	desc.maxh = 0;
	desc.audio = 0;
	desc.restore = 0;
	run_thumb("sans max", "--thumb RAM0:/emtest/th4.raw RAM0:/emtest/jeu.rom", "RAM0:/emtest/th4.raw");
	run_thumb("absente", "--thumb RAM0:/emtest/th5.raw RAM0:/emtest/absente.rom", "RAM0:/emtest/th5.raw");
	run_thumb("vignette impossible", "--thumb RAM0:/emtest/non/th6.raw RAM0:/emtest/jeu.rom",
		  "RAM0:/emtest/non/th6.raw");
	desc.load = f_load;
	desc.frame = f_frame;
	st.load_ok = 0;
	{
		memset(&st, 0, sizeof st);
		long r = C2(E_RUN, &desc, "--thumb RAM0:/emtest/th7.raw RAM0:/emtest/jeu.rom");
		ps("thumb refusee");
		kv("r", r);
		kv("loads", st.loads);
		kv("frames", st.frames);
		nl();
	}
	/* EMD_BUFS : les images de l'application */
	static unsigned bufs[3][320 * 240];
	void *bp[3] = {bufs[0], 0, bufs[2]};
	desc.bufs = bp;
	desc.maxw = 320;
	desc.maxh = 240;
	run_thumb("bufs", "--thumb RAM0:/emtest/th8.raw RAM0:/emtest/jeu.rom", "RAM0:/emtest/th8.raw");
	ps(" bufs");
	kh("b0", fnv(bufs[0], sizeof bufs[0]));
	kh("b2", fnv(bufs[2], sizeof bufs[2]));
	nl();
	/* la ROM par son chemin (en dernier : le kit le garde) */
	desc.bufs = 0;
	kv("rom_by_path", C0(E_ROM_BY_PATH));
	nl();
	memset(&st, 0, sizeof st);
	st.by_path = 1;
	st.load_ok = 1;
	long r = C2(E_RUN, &desc, "--thumb RAM0:/emtest/th9.raw RAM0:/emtest/jeu.rom");
	ps("thumb par chemin");
	kv("r", r);
	kv("loads", st.loads);
	kv("size", st.load_size);
	kh("chemin", st.load_h);
	kv("frames", st.frames);
	nl();
	n = read_file("RAM0:/emtest/th9.raw", filebuf, sizeof filebuf);
	ps(" vignette");
	kv("octets", n);
	if (n > 0)
		kh("somme", fnv(filebuf, n));
	nl();
}

static int has_flag(const char *line, char f)
{
	if (!line)
		return 0;
	for (long i = 0; line[i]; i++)
		if (line[i] == '-' && line[i + 1] == f)
			return 1;
	return 0;
}

static void run(const char *line)
{
	ak = sys2(SYS_LIB_OPEN, "appkit", 32);
	ps("emtest : EmuKit\n");
	kit = open_kit("emukit", 1);
	if (kit < 0) {
		ps("emukit absent : ");
		pd(kit);
		nl();
		flush();
		sys1(SYS_EXIT, 127);
	}
	const uint32_t *t = (const uint32_t *)kit;
	ps("table magic=");
	px(t[0], 8);
	ps(" count=");
	pd(t[1]);
	nl();
	if (t[0] != KIT_MAGIC || t[1] < E_COUNT_FN) {
		ps("table invalide\n");
		flush();
		sys1(SYS_EXIT, 126);
	}
	part_memory();
	ps("fin : ");
	pd(checks);
	ps(" controles, ");
	pd(fails);
	ps(" echecs\n");
	flush();
	if (has_flag(line, 'f')) {
		part_files();
		for (int i = 0; i < E_COUNT_FN; i++)
			if (!called[i]) {
				ps("jamais appelee : ");
				ps(fn_names[i]);
				nl();
			}
	}
	flush();
	sys1(SYS_EXIT, fails > 125 ? 125 : fails);
}

#if defined(__x86_64__)
__attribute__((force_align_arg_pointer, noreturn))
#else
__attribute__((noreturn))
#endif
void _start(const char *line)
{
	run(line);
	for (;;)
		;
}
