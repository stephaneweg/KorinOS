/* artest : essai différentiel d'ArchiveKit (src/kits/archivekit), sans
 * bibliothèque, pour AArch64 (Korin sur le Pi) et x86-64 (Korin sur PC).
 *
 * Appelle les fonctions du kit avec des entrées variées et n'écrit que ce qui
 * ne dépend ni des adresses ni de l'heure : résultats, codes d'erreur, tailles
 * et sommes FNV-1a des octets produits. Le kit en assembleur et le kit en C
 * doivent donner des sorties identiques, octet pour octet :
 *     artest > SD1:/ar-asm.txt   (puis avec le kit en C, puis sur le PC)
 * Contrôles propres (allers-retours deflate -> inflate, contenu relu des
 * archives) : « ECHEC ... » et code de sortie = nombre d'échecs.
 *
 * artest -f : en plus, à la fin et après une ligne « === fichiers (RAM0:) »,
 * des essais sur des fichiers de RAM0:/artest (ar_create, ar_add_file,
 * ar_open, ar_probe, ar_extract, ar_extract_all). Le PC n'a pas encore de
 * service de fichiers : ne comparer que ce qui précède cette ligne.
 *
 * Construction : src/kits/archivekit/test/build.sh (dans WSL). */
#include <stddef.h>
#include <stdint.h>

/* src/inc/syscalls.inc */
#define SYS_EXIT 0x10
#define SYS_PRINT 0x60
#define SYS_LIB_OPEN 0x64

#define KIT_MAGIC 0x54494B41
/* AppKit (rangs de appkit.abi) */
#define AK_F_OUVRIR 14
#define AK_F_ECRIRE 16
#define AK_F_FERMER 18
#define AK_F_MKDIR 21
#define AK_F_UNLINK 22
#define AK_F_PRINT 25    /* (version 26) */
#define AK_F_KIT_LOAD 31 /* (version 32) */

/* archivekit.inc */
#define AR_RAW 0
#define AR_ZLIB 1
#define AR_GZIP 2
#define AR_AUTO 3
#define AR_FMT_ZIP 1
#define AR_FMT_TAR 2
#define AR_FMT_TGZ 3
#define AR_FMT_GZIP 4

/* rangs de archivekit.abi */
enum {
	AR_CRC32,
	AR_ADLER32,
	AR_DEFLATE_BOUND,
	AR_DEFLATE,
	AR_INFLATE,
	AR_INFLATE_SIZE,
	AR_DEFLATE_NEW,
	AR_INFLATE_NEW,
	AR_DATA,
	AR_BYTES,
	AR_FREE,
	AR_OPEN,
	AR_OPEN_MEM,
	AR_CLOSE,
	AR_FORMAT,
	AR_COUNT,
	AR_ENTRY,
	AR_NAME,
	AR_ENTRY_SIZE,
	AR_FIND,
	AR_READ,
	AR_LOAD,
	AR_EXTRACT,
	AR_EXTRACT_ALL,
	AR_TEST,
	AR_CREATE,
	AR_CREATE_MEM,
	AR_ADD_MEM,
	AR_ADD_FILE,
	AR_ADD_DIR,
	AR_SET_TIME,
	AR_FINISH,
	AR_FORMATS,
	AR_FORMAT_NAME,
	AR_PROBE,
	AR_STRERROR,
	AR_COUNT_FN
};

static const char *const fn_names[AR_COUNT_FN] = {
	"ar_crc32",   "ar_adler32",   "ar_deflate_bound", "ar_deflate",     "ar_inflate",  "ar_inflate_size",
	"ar_deflate_new", "ar_inflate_new", "ar_data",    "ar_bytes",       "ar_free",     "ar_open",
	"ar_open_mem", "ar_close",    "ar_format",        "ar_count",       "ar_entry",    "ar_name",
	"ar_entry_size", "ar_find",   "ar_read",          "ar_load",        "ar_extract",  "ar_extract_all",
	"ar_test",    "ar_create",    "ar_create_mem",    "ar_add_mem",     "ar_add_file", "ar_add_dir",
	"ar_set_time", "ar_finish",   "ar_formats",       "ar_format_name", "ar_probe",    "ar_strerror",
};

/* ---- appels système (comme src/kits/common/korin_kit.h) */

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
#define sys2(n, a, b) sys6((n), (long)(a), (long)(b), 0, 0, 0, 0)

/* le compilateur peut les appeler lui-même */
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

/* ---- sortie : AppKit (sortie standard, se redirige) s'il est là, sinon la
 * console du noyau */

typedef long (*fn_print)(const char *, long);
static fn_print f_print;
static char pb[4096];
static long pn;

static void flush(void)
{
	if (!pn)
		return;
	if (!f_print || f_print(pb, pn) < 0)
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

/* octets entre guillemets, échappés */
static void pq(const void *p, long n)
{
	const unsigned char *s = p;
	pc('"');
	for (long i = 0; i < n; i++) {
		unsigned c = s[i];
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

static uint32_t fnv(const void *p, long n)
{
	const unsigned char *s = p;
	uint32_t h = 2166136261u;
	for (long i = 0; i < n; i++) {
		h ^= s[i];
		h *= 16777619u;
	}
	return h;
}

static uint32_t fnv_mix(uint32_t h, uint64_t v)
{
	for (int i = 0; i < 8; i++) {
		h ^= (uint8_t)(v >> (8 * i));
		h *= 16777619u;
	}
	return h;
}

static void kh(const char *k, uint32_t h)
{
	pc(' ');
	ps(k);
	pc('=');
	px(h, 8);
}

/* ---- contrôles */

static long checks, fails, calls;

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
static long kit_count;
static long called[AR_COUNT_FN];
static long ak; /* table d'AppKit (fichiers, -f) */

typedef long (*fn6)(long, long, long, long, long, long);

static long call(int i, long a, long b, long c, long d, long e, long f)
{
	const uint32_t *t = (const uint32_t *)kit;
	fn6 fn = (fn6)(kit + t[2 + i]);
	calls++;
	called[i]++;
	return fn(a, b, c, d, e, f);
}
#define L(x) ((long)(x))
#define C1(i, a) call(i, L(a), 0, 0, 0, 0, 0)
#define C2(i, a, b) call(i, L(a), L(b), 0, 0, 0, 0)
#define C3(i, a, b, c) call(i, L(a), L(b), L(c), 0, 0, 0)
#define C4(i, a, b, c, d) call(i, L(a), L(b), L(c), L(d), 0, 0)
#define C5(i, a, b, c, d, e) call(i, L(a), L(b), L(c), L(d), L(e), 0)
#define C6(i, a, b, c, d, e, f) call(i, L(a), L(b), L(c), L(d), L(e), L(f))
static long hres(long h);

static long ak_call(int i, long a, long b, long c)
{
	const uint32_t *t = (const uint32_t *)ak;
	fn6 fn = (fn6)(ak + t[2 + i]);
	return fn(a, b, c, 0, 0, 0);
}

static long open_kit(const char *name, long min)
{
	long t = sys2(SYS_LIB_OPEN, name, min);
	if (t == -2) { /* pas encore en mémoire : AppKit le lit (comme kits_open) */
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

/* ---- données d'essai */

static uint64_t lcg;

static uint32_t rnd(void)
{
	lcg = lcg * 6364136223846793005ULL + 1442695040888963407ULL;
	return (uint32_t)(lcg >> 33);
}

static const char *const words[] = {
	"le",     "la",       "les",     "des",     "une",      "un",      "archive", "fichier", "dossier", "carte",
	"donnees", "Korin",   "compression", "deflate", "bloc",  "fenetre", "octets",  "de",      "et",      "pour",
	"dans",   "avec",     "sur",     "est",     "sont",     "que",     "qui",     "pas",     "plus",    "tout",
	"systeme", "noyau",   "service", "memoire", "page",     "table",   "code",    "Huffman", "longueur", "distance",
	"zlib",   "gzip",     "tar",     "zip",     "entree",   "sortie",  "puits",   "tampon",  "erreur",  "somme",
};
#define NWORDS ((uint32_t)(sizeof words / sizeof words[0]))

/* texte : des mots tirés au hasard, une ponctuation de temps en temps */
static long gen_text(unsigned char *o, long n, uint64_t seed)
{
	lcg = seed;
	long k = 0;
	while (k < n) {
		const char *w = words[rnd() % NWORDS];
		while (*w && k < n)
			o[k++] = (unsigned char)*w++;
		if (k >= n)
			break;
		uint32_t r = rnd() % 16;
		if (r == 0 && k + 2 <= n) {
			o[k++] = '.';
			o[k++] = '\n';
		} else if (r == 1) {
			o[k++] = ',';
		} else if (r == 2) {
			long v = rnd() % 10000;
			char b[8];
			int i = 0;
			do
				b[i++] = (char)('0' + v % 10);
			while ((v /= 10) && i < 8);
			o[k++] = ' ';
			while (i && k < n)
				o[k++] = (unsigned char)b[--i];
			if (k < n)
				o[k++] = ' ';
		} else {
			o[k++] = ' ';
		}
	}
	return n;
}

static void gen_rand(unsigned char *o, long n, uint64_t seed)
{
	lcg = seed;
	for (long i = 0; i < n; i++)
		o[i] = (unsigned char)(rnd() >> 7);
}

#define BIG 200000
#define CAPB (BIG + BIG / 4096 + 256)

static unsigned char in_one[1] = {'A'};
static unsigned char in_text[3000];
static unsigned char in_rep[40000];
static unsigned char in_rand[6000];
static unsigned char in_big[BIG];
static unsigned char in_short[] = "hello hello hello hello, archivekit !\n";

static unsigned char out[CAPB];
static unsigned char back[CAPB];
static unsigned char tmp[CAPB];
static unsigned char dmg[CAPB];

struct input {
	const char *name;
	const unsigned char *p;
	long n;
};
static struct input inputs[7];

static void make_inputs(void)
{
	gen_text(in_text, sizeof in_text, 1);
	/* répétitif : un motif, des zéros, un motif plus long */
	for (long i = 0; i < (long)sizeof in_rep; i++) {
		if (i < 12000)
			in_rep[i] = (unsigned char)("abcdefgh"[i % 8]);
		else if (i < 30000)
			in_rep[i] = 0;
		else
			in_rep[i] = (unsigned char)("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-xyz"[(i * 7) % 40]);
	}
	gen_rand(in_rand, sizeof in_rand, 7);
	/* gros : du texte, avec des morceaux incompressibles au milieu */
	gen_text(in_big, BIG, 2);
	for (long at = 40000; at + 3000 < BIG; at += 50000)
		gen_rand(in_big + at, 3000, (uint64_t)at);
	inputs[0] = (struct input){"vide", in_one, 0};
	inputs[1] = (struct input){"un", in_one, 1};
	inputs[2] = (struct input){"court", in_short, (long)sizeof in_short - 1};
	inputs[3] = (struct input){"texte", in_text, (long)sizeof in_text};
	inputs[4] = (struct input){"repetitif", in_rep, (long)sizeof in_rep};
	inputs[5] = (struct input){"hasard", in_rand, (long)sizeof in_rand};
	inputs[6] = (struct input){"gros", in_big, BIG};
}

/* ---- rangées de résultats (beaucoup de petits essais : 16 par ligne) */

static int row_n;

static void row_begin(const char *what)
{
	ps(what);
	pc(':');
	row_n = 0;
}

static void row_add(long v)
{
	if (row_n && !(row_n % 16)) {
		nl();
		pc(' ');
	}
	pc(' ');
	pd(v);
	row_n++;
}

static void row_end(void)
{
	nl();
}

/* ======================================================================== */

static void test_sums(void)
{
	ps("== sommes\n");
	static const long lens[] = {0, 1, 3, 7, 8, 9, 15, 16, 17, 100, 3000};
	for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
		long n = lens[i];
		ps("crc32 n=");
		pd(n);
		pc(' ');
		px((uint64_t)C3(AR_CRC32, 0, in_text, n), 8);
		ps(" adler32 ");
		px((uint64_t)C3(AR_ADLER32, 1, in_text, n), 8);
		nl();
	}
	/* par morceaux = d'un coup */
	long c = 0, a = 1;
	for (long at = 0; at < BIG;) {
		long k = 1 + (at * 37) % 9001;
		if (k > BIG - at)
			k = BIG - at;
		c = C3(AR_CRC32, c, in_big + at, k);
		a = C3(AR_ADLER32, a, in_big + at, k);
		at += k;
	}
	long c1 = C3(AR_CRC32, 0, in_big, BIG), a1 = C3(AR_ADLER32, 1, in_big, BIG);
	ps("gros crc32 ");
	px((uint64_t)c1, 8);
	ps(" adler32 ");
	px((uint64_t)a1, 8);
	nl();
	ck(c == c1 && a == a1, "sommes par morceaux");
	/* valeurs de départ quelconques */
	ps("depart crc32 ");
	px((uint64_t)C3(AR_CRC32, 0x12345678, in_rand, 1000), 8);
	pc(' ');
	px((uint64_t)C3(AR_CRC32, -1, in_rand, 0), 16);
	ps(" adler32 ");
	px((uint64_t)C3(AR_ADLER32, 0xFFF0FFF0L, in_rand, 1000), 8);
	pc(' ');
	px((uint64_t)C3(AR_ADLER32, 0x7FFFFFFFFFL, in_rand, 0), 16);
	nl();
	/* le standard : "123456789" */
	ps("check crc32 ");
	px((uint64_t)C3(AR_CRC32, 0, "123456789", 9), 8);
	ps(" adler32 ");
	px((uint64_t)C3(AR_ADLER32, 1, "123456789", 9), 8);
	nl();
}

static void test_deflate(void)
{
	ps("== deflate, inflate\n");
	static const long bn[] = {0, 1, 4095, 4096, 100000, 1L << 32};
	for (unsigned i = 0; i < sizeof bn / sizeof bn[0]; i++) {
		ps("bound ");
		pd(bn[i]);
		pc(' ');
		pd(C2(AR_DEFLATE_BOUND, bn[i], i % 3));
		nl();
	}
	for (int k = 0; k < 7; k++) {
		const struct input *in = &inputs[k];
		for (int w = 0; w < 3; w++)
			for (int lv = 0; lv <= 9; lv++) {
				long cap = C2(AR_DEFLATE_BOUND, in->n, w);
				long r = C6(AR_DEFLATE, in->p, in->n, out, cap, w, lv);
				ps("deflate ");
				ps(in->name);
				kv("env", w);
				kv("niv", lv);
				kv("->", r);
				if (r >= 0)
					kh("fnv", fnv(out, r));
				ck(r >= 0, "deflate");
				if (r < 0) {
					nl();
					continue;
				}
				/* inflate dans un tampon juste assez grand, puis trop petit */
				long r2 = C5(AR_INFLATE, out, r, back, in->n, w);
				ck(r2 == in->n && !memcmp(back, in->p, (size_t)in->n), "aller-retour");
				kv("inf", r2);
				if (w) {
					long r3 = C5(AR_INFLATE, out, r, back, in->n, AR_AUTO);
					ck(r3 == in->n, "aller-retour AR_AUTO");
					kv("auto", r3);
				}
				kv("size", C3(AR_INFLATE_SIZE, out, r, w));
				if (in->n)
					kv("petit", C5(AR_INFLATE, out, r, back, in->n - 1, w));
				nl();
			}
	}
	/* niveaux et enveloppes invalides, -1 = 6 */
	ps("args");
	kv("niv-1", C6(AR_DEFLATE, in_text, 3000, out, CAPB, AR_ZLIB, -1));
	kh("fnv", fnv(out, 0 + (long)C6(AR_DEFLATE, in_text, 3000, out, CAPB, AR_ZLIB, -1)));
	kv("niv10", C6(AR_DEFLATE, in_text, 3000, out, CAPB, AR_ZLIB, 10));
	kv("niv-2", C6(AR_DEFLATE, in_text, 3000, out, CAPB, AR_ZLIB, -2));
	kv("env3", C6(AR_DEFLATE, in_text, 3000, out, CAPB, AR_AUTO, 6));
	kv("env-1", C6(AR_DEFLATE, in_text, 3000, out, CAPB, -1, 6));
	kv("inf-env4", C5(AR_INFLATE, out, 10, back, CAPB, 4));
	kv("size-env9", C3(AR_INFLATE_SIZE, out, 10, 9));
	nl();
	/* sortie trop petite */
	long full = C6(AR_DEFLATE, in_text, 3000, out, CAPB, AR_GZIP, 6);
	row_begin("deflate place");
	static const long caps[] = {0, 1, 2, 9, 10, 11, 100, 500};
	for (unsigned i = 0; i < sizeof caps / sizeof caps[0]; i++)
		row_add(C6(AR_DEFLATE, in_text, 3000, tmp, caps[i], AR_GZIP, 6));
	row_add(C6(AR_DEFLATE, in_text, 3000, tmp, full - 1, AR_GZIP, 6));
	row_add(C6(AR_DEFLATE, in_text, 3000, tmp, full, AR_GZIP, 6));
	row_add(C6(AR_DEFLATE, in_rand, 6000, tmp, 6000, AR_RAW, 9));
	row_add(C6(AR_DEFLATE, in_rand, 6000, tmp, 6005, AR_RAW, 9));
	row_add(C6(AR_DEFLATE, in_rand, 6000, tmp, 6010, AR_RAW, 0));
	row_end();
	row_begin("inflate place");
	long gz = C6(AR_DEFLATE, in_text, 3000, out, CAPB, AR_GZIP, 6);
	for (long c = 0; c <= 3000; c += 250)
		row_add(C5(AR_INFLATE, out, gz, back, c, AR_GZIP));
	row_add(C5(AR_INFLATE, out, gz, back, 2999, AR_GZIP));
	row_add(C5(AR_INFLATE, out, gz, back, 3000, AR_GZIP));
	row_add(C5(AR_INFLATE, out, gz, 0, 0, AR_GZIP));
	row_end();
}

static void show_buf(const char *what, long b, const void *ref, long refn)
{
	ps(what);
	if (b < 0) {
		kv("->", b);
		nl();
		return;
	}
	long n = C1(AR_BYTES, b);
	const unsigned char *d = (const unsigned char *)C1(AR_DATA, b);
	kv("octets", n);
	kh("fnv", fnv(d, n));
	kv("zero", d[n]);
	if (ref) {
		kv("egal", n == refn && !memcmp(d, ref, (size_t)n));
		ck(n == refn && !memcmp(d, ref, (size_t)n), what);
	}
	kv("free", C1(AR_FREE, b));
	nl();
}

static void test_new(void)
{
	ps("== deflate_new, inflate_new\n");
	for (int k = 0; k < 7; k++) {
		const struct input *in = &inputs[k];
		for (int w = 0; w < 3; w++) {
			long lv = (k + w) % 11 - 1; /* -1..9 */
			long r = C6(AR_DEFLATE, in->p, in->n, out, CAPB, w, lv);
			long b = C4(AR_DEFLATE_NEW, in->p, in->n, w, lv);
			ps("deflate_new ");
			ps(in->name);
			kv("env", w);
			kv("niv", lv);
			nl();
			show_buf(" =deflate", b, out, r);
			static const long exp[] = {0, -1, 5};
			for (int e = 0; e < 4; e++) {
				long x = e < 3 ? exp[e] : in->n;
				long ib = C4(AR_INFLATE_NEW, out, r, w ? AR_AUTO : AR_RAW, x);
				ps(" inflate_new");
				kv("prevu", x);
				show_buf("", ib, in->p, in->n);
			}
		}
	}
	ps("args");
	kv("niv11", C4(AR_DEFLATE_NEW, in_text, 10, 0, 11));
	kv("env5", C4(AR_DEFLATE_NEW, in_text, 10, 5, 6));
	kv("env5niv11", C4(AR_DEFLATE_NEW, in_text, 10, 5, 11));
	kv("inf-env7", C4(AR_INFLATE_NEW, out, 10, 7, 0));
	kv("inf-abime", hres(C4(AR_INFLATE_NEW, in_rand, 100, AR_ZLIB, 0)));
	nl();
}

/* une rangée : le flux s (n octets) tronqué, puis abîmé bit par bit */
static void damage(const char *what, const unsigned char *s, long n, int w, long orig)
{
	long step = n <= 400 ? 1 : n / 400;
	ps(what);
	kv("octets", n);
	nl();
	row_begin(" tronque");
	for (long l = 0; l < n; l += step)
		row_add(C5(AR_INFLATE, s, l, back, orig, w));
	row_end();
	row_begin(" tronque size");
	for (long l = 0; l < n; l += step)
		row_add(C3(AR_INFLATE_SIZE, s, l, w));
	row_end();
	uint32_t h = 2166136261u;
	row_begin(" bit");
	for (long i = 0; i < n; i += step) {
		memcpy(dmg, s, (size_t)n);
		dmg[i] ^= (unsigned char)(1 << ((i * 5 + 3) & 7));
		long r = C5(AR_INFLATE, dmg, n, back, orig + 64, w);
		row_add(r);
		if (r > 0)
			h = fnv_mix(h, fnv(back, r));
	}
	row_end();
	ps(" bit fnv");
	kh("", h);
	nl();
	h = 2166136261u;
	row_begin(" bit new");
	for (long i = 0; i < n; i += step) {
		memcpy(dmg, s, (size_t)n);
		dmg[i] ^= (unsigned char)(1 << ((i * 3 + 1) & 7));
		long b = C4(AR_INFLATE_NEW, dmg, n, w, 0);
		if (b < 0) {
			row_add(b);
		} else {
			long bn = C1(AR_BYTES, b);
			row_add(bn);
			h = fnv_mix(h, fnv((const void *)C1(AR_DATA, b), bn));
			C1(AR_FREE, b);
		}
	}
	row_end();
	ps(" bit new fnv");
	kh("", h);
	nl();
	row_begin(" octet size");
	for (long i = 0; i < n; i += step) {
		memcpy(dmg, s, (size_t)n);
		dmg[i] = (unsigned char)(dmg[i] + 0x55);
		row_add(C3(AR_INFLATE_SIZE, dmg, n, w ? AR_AUTO : AR_RAW));
	}
	row_end();
}

static void test_damage(void)
{
	ps("== donnees abimees\n");
	static const struct {
		int in, w, lv;
	} cases[] = {
		{3, AR_RAW, 6}, {3, AR_ZLIB, 6}, {3, AR_GZIP, 6}, {3, AR_GZIP, 1}, {2, AR_ZLIB, 6},
		{5, AR_RAW, 6}, {4, AR_GZIP, 9}, {2, AR_RAW, 0}, {6, AR_ZLIB, 4},
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		const struct input *in = &inputs[cases[i].in];
		long r = C6(AR_DEFLATE, in->p, in->n, tmp, CAPB, cases[i].w, cases[i].lv);
		ps("flux ");
		ps(in->name);
		kv("env", cases[i].w);
		kv("niv", cases[i].lv);
		nl();
		static unsigned char s[CAPB];
		memcpy(s, tmp, (size_t)r);
		damage("", s, r, cases[i].w, in->n);
	}
	/* du hasard dans chaque enveloppe */
	for (int w = 0; w < 4; w++) {
		row_begin("hasard");
		pd(w);
		for (int k = 0; k < 40; k++) {
			long n = 1 + (k * 53) % 1500;
			gen_rand(dmg, n, (uint64_t)(k * 11 + w));
			if (w == AR_ZLIB && k % 2) {
				dmg[0] = 0x78;
				dmg[1] = 0x9C;
			}
			if (w == AR_GZIP && k % 2) {
				dmg[0] = 0x1F;
				dmg[1] = 0x8B;
				dmg[2] = 8;
				dmg[3] = (unsigned char)(k & 0x1F);
			}
			row_add(C5(AR_INFLATE, dmg, n, back, CAPB, w));
		}
		row_end();
	}
}

/* des flux faits à la main */
static long put_gzhdr(unsigned char *o, int flags)
{
	long k = 0;
	o[k++] = 0x1F;
	o[k++] = 0x8B;
	o[k++] = 8;
	o[k++] = (unsigned char)flags;
	o[k++] = 0x10; /* date */
	o[k++] = 0x20;
	o[k++] = 0x30;
	o[k++] = 0x40;
	o[k++] = 0;
	o[k++] = 3;
	if (flags & 4) {
		o[k++] = 5;
		o[k++] = 0;
		for (int i = 0; i < 5; i++)
			o[k++] = (unsigned char)('x' + i);
	}
	if (flags & 8) {
		const char *n = "nom.txt";
		while (*n)
			o[k++] = (unsigned char)*n++;
		o[k++] = 0;
	}
	if (flags & 16) {
		const char *n = "un commentaire";
		while (*n)
			o[k++] = (unsigned char)*n++;
		o[k++] = 0;
	}
	if (flags & 2) {
		o[k++] = 0xAB;
		o[k++] = 0xCD;
	}
	return k;
}

static long gz_member(unsigned char *o, int flags, const unsigned char *p, long n, int lv)
{
	long k = put_gzhdr(o, flags);
	long r = C6(AR_DEFLATE, p, n, o + k, CAPB / 2, AR_RAW, lv);
	if (r < 0)
		return r;
	k += r;
	long c = C3(AR_CRC32, 0, p, n);
	for (int i = 0; i < 4; i++)
		o[k++] = (unsigned char)(c >> (8 * i));
	for (int i = 0; i < 4; i++)
		o[k++] = (unsigned char)(n >> (8 * i));
	return k;
}

static void test_crafted(void)
{
	ps("== flux faits a la main\n");
	row_begin("gzip drapeaux");
	for (int f = 0; f < 32; f++) {
		long k = gz_member(tmp, f, in_text, 1000, 6);
		row_add(C3(AR_INFLATE_SIZE, tmp, k, AR_GZIP));
	}
	for (int f = 0x20; f <= 0xE0; f += 0x20) {
		long k = gz_member(tmp, f, in_text, 1000, 6);
		row_add(C3(AR_INFLATE_SIZE, tmp, k, AR_AUTO));
	}
	row_end();
	/* plusieurs membres, puis du bourrage */
	long k = gz_member(tmp, 8, in_text, 1000, 6);
	k += gz_member(tmp + k, 0, in_rep, 20000, 1);
	k += gz_member(tmp + k, 4, in_one, 0, 0);
	ps("gzip membres");
	kv("octets", k);
	kv("size", C3(AR_INFLATE_SIZE, tmp, k, AR_GZIP));
	long r = C5(AR_INFLATE, tmp, k, back, CAPB, AR_AUTO);
	kv("inf", r);
	if (r > 0)
		kh("fnv", fnv(back, r));
	tmp[k] = 0;
	tmp[k + 1] = 0;
	tmp[k + 2] = 0;
	kv("bourrage", C3(AR_INFLATE_SIZE, tmp, k + 3, AR_GZIP));
	tmp[k] = 0x1F;
	kv("1f", C3(AR_INFLATE_SIZE, tmp, k + 1, AR_GZIP));
	tmp[k + 1] = 0x8B;
	kv("1f8b", C3(AR_INFLATE_SIZE, tmp, k + 2, AR_GZIP));
	kv("1f8b00", C3(AR_INFLATE_SIZE, tmp, k + 3, AR_GZIP));
	nl();
	/* zlib : en-têtes */
	row_begin("zlib entetes");
	for (int a = 0; a < 256; a += 7)
		for (int b = 0; b < 256; b += 29) {
			long n = C6(AR_DEFLATE, in_short, 20, tmp, CAPB, AR_ZLIB, 6);
			tmp[0] = (unsigned char)a;
			tmp[1] = (unsigned char)b;
			row_add(C3(AR_INFLATE_SIZE, tmp, n, AR_ZLIB));
		}
	row_end();
	/* blocs bruts : types, stockés */
	static const unsigned char raw[][12] = {
		{0x07},                                     /* type 3 */
		{0x01, 0x00, 0x00, 0xFF, 0xFF},             /* stocké vide, dernier */
		{0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c'},
		{0x01, 0x03, 0x00, 0xFC, 0xFE, 'a', 'b', 'c'}, /* NLEN faux */
		{0x01, 0x05, 0x00, 0xFA, 0xFF, 'a', 'b'},      /* tronqué */
		{0x03, 0x00},                               /* fixe vide */
		{0x4B, 0x04, 0x02, 0x00},                   /* fixe : a, puis une distance trop loin ? */
		{0x05, 0xC0, 0x81, 0x00},                   /* dynamique abîmé */
		{0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},       /* dynamique, HLIT trop grand */
		{0x00, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00}, /* stocké vide, puis fixe vide */
	};
	static const long rawn[] = {1, 5, 8, 8, 7, 2, 4, 4, 6, 7};
	row_begin("bruts");
	for (unsigned i = 0; i < sizeof rawn / sizeof rawn[0]; i++) {
		row_add(C5(AR_INFLATE, raw[i], rawn[i], back, 100, AR_RAW));
		row_add(C3(AR_INFLATE_SIZE, raw[i], rawn[i], AR_RAW));
	}
	row_end();
	/* une distance juste au bord : "ab" puis copie de 258 à distance 2, dans
	 * des tampons de toutes les tailles (la copie par 8 octets, les bords) */
	long n = C6(AR_DEFLATE, in_rep, 2000, tmp, CAPB, AR_RAW, 9);
	row_begin("bords");
	for (long c = 1990; c <= 2010; c++) {
		memset(back, 0xEE, 2100);
		long r2 = C5(AR_INFLATE, tmp, n, back, c, AR_RAW);
		row_add(r2);
		row_add(r2 > 0 ? (long)fnv(back, r2) : 0);
	}
	row_end();
}

/* ---- archives en mémoire */

#define T0 1700000000L

static long name_long(char *o, int len, char c)
{
	int k = 0;
	while (k < len) {
		for (int j = 0; j < 9 && k < len; j++)
			o[k++] = c;
		if (k < len)
			o[k++] = '/';
	}
	if (o[k - 1] == '/')
		o[k - 1] = 'z';
	o[k] = 0;
	return k;
}

static char nm120[200], nm300[400], nm160[200];

/* une poignée -> 1 (rendue), ou l'erreur : rien qui dépende d'une adresse */
static long hres(long h)
{
	if (h < 0)
		return h;
	C1(AR_FREE, h);
	return 1;
}

/* une archive en mémoire -> écrivain fini (ou erreur) */
static long build(int fmt, int variant)
{
	long w = C1(AR_CREATE_MEM, fmt);
	ps(" create_mem");
	kv("fmt", fmt);
	if (w < 0) {
		kv("->", w);
		nl();
		return w;
	}
	kv("temps", C2(AR_SET_TIME, w, variant == 2 ? 1000 : T0));
	nl();
	row_begin(" ajouts");
	if (fmt == AR_FMT_GZIP) {
		row_add(C5(AR_ADD_MEM, w, "./doc/texte.txt", in_text, 3000, variant ? 9 : -1));
		row_add(C5(AR_ADD_MEM, w, "encore", in_text, 10, 6));
		row_add(C2(AR_ADD_DIR, w, "dossier"));
	} else {
		row_add(C5(AR_ADD_MEM, w, "mimetype", "application/vnd.oasis.opendocument.text", 39, 0));
		row_add(C2(AR_ADD_DIR, w, "docs"));
		row_add(C5(AR_ADD_MEM, w, "docs/lisez-moi.txt", in_text, 3000, 6));
		row_add(C5(AR_ADD_MEM, w, "/donnees\\hasard.bin", in_rand, 6000, 9));
		row_add(C5(AR_ADD_MEM, w, "./rep.bin", in_rep, 40000, 1));
		row_add(C5(AR_ADD_MEM, w, "vide.txt", in_one, 0, 6));
		row_add(C5(AR_ADD_MEM, w, "caf\xc3\xa9/\xc3\xa9t\xc3\xa9.txt", in_short, 10, 6));
		row_add(C5(AR_ADD_MEM, w, nm120, in_short, 20, 3));
		row_add(C5(AR_ADD_MEM, w, nm300, in_short, 30, 4));
		row_add(C5(AR_ADD_MEM, w, nm160, in_short, 5, 5));
		row_add(C2(AR_ADD_DIR, w, nm300));
		if (variant == 1)
			row_add(C5(AR_ADD_MEM, w, "gros.txt", in_big, BIG, 7));
		row_add(C2(AR_SET_TIME, w, 4102444800L + 86400L * 40000)); /* au-delà de 2107 */
		row_add(C5(AR_ADD_MEM, w, "futur.txt", in_short, 3, 2));
		row_add(C2(AR_SET_TIME, w, 951782400)); /* 29/02/2000 */
		row_add(C5(AR_ADD_MEM, w, "bissextile.txt", in_short, 3, 8));
		/* erreurs */
		row_add(C5(AR_ADD_MEM, w, "", in_short, 3, 6));
		row_add(C5(AR_ADD_MEM, w, "///", in_short, 3, 6));
		row_add(C5(AR_ADD_MEM, w, 0, in_short, 3, 6));
		row_add(C5(AR_ADD_MEM, w, "x", in_short, 3, 10));
		row_add(C2(AR_ADD_DIR, w, ""));
	}
	row_end();
	long r = C1(AR_FINISH, w);
	ps(" finish");
	kv("->", r);
	kv("octets", C1(AR_BYTES, w));
	if (r >= 0) {
		const unsigned char *d = (const unsigned char *)C1(AR_DATA, w);
		kh("fnv", fnv(d, r));
		kv("zero", d[r]);
	}
	kv("refini", C1(AR_FINISH, w));
	kv("ajout", C5(AR_ADD_MEM, w, "x", in_short, 3, 6));
	kv("temps", C2(AR_SET_TIME, w, 5));
	nl();
	return w;
}

/* une archive en mémoire ouverte : tout ce qu'on peut en lire */
static void inspect(const unsigned char *d, long n, int verbose)
{
	long a = C2(AR_OPEN_MEM, d, n);
	if (a < 0) {
		ps(" open_mem");
		kv("->", a);
		nl();
		return;
	}
	long cnt = C1(AR_COUNT, a);
	ps(" open_mem");
	kv("fmt", C1(AR_FORMAT, a));
	kv("n", cnt);
	nl();
	uint32_t h = 2166136261u;
	for (long i = 0; i < cnt; i++) {
		long e[8];
		long r = C3(AR_ENTRY, a, i, e);
		const char *nm = (const char *)C2(AR_NAME, a, i);
		if (verbose) {
			ps("  ");
			pd(i);
			pc(' ');
			pq(nm, slen(nm));
			kv("r", r);
			kv("taille", e[1]);
			kv("paquet", e[2]);
			kv("date", e[3]);
			kv("dossier", e[4]);
			kv("methode", e[5]);
			ps(" crc=");
			px((uint64_t)e[6], 8);
			kv("drap", e[7]);
			ck((const char *)e[0] == nm, "ar_entry nom");
			ck(C2(AR_ENTRY_SIZE, a, i) == e[1], "ar_entry_size");
		}
		long rr = C4(AR_READ, a, i, back, CAPB);
		long rt = C2(AR_TEST, a, i);
		long b = C2(AR_LOAD, a, i);
		long bn = b < 0 ? b : C1(AR_BYTES, b);
		uint32_t fh = rr > 0 ? fnv(back, rr) : 0;
		if (b >= 0) {
			ck(bn == rr && !memcmp((const void *)C1(AR_DATA, b), back, (size_t)(rr > 0 ? rr : 0)),
			   "ar_load = ar_read");
			C1(AR_FREE, b);
		}
		long small = e[1] > 0 ? C4(AR_READ, a, i, tmp, e[1] - 1) : 0;
		long fi = C2(AR_FIND, a, nm);
		if (verbose) {
			ps("   ");
			kv("read", rr);
			kh("fnv", fh);
			kv("test", rt);
			kv("load", bn);
			kv("petit", small);
			kv("find", fi);
			nl();
		} else {
			h = fnv_mix(h, (uint64_t)rr);
			h = fnv_mix(h, fh);
			h = fnv_mix(h, (uint64_t)rt);
			h = fnv_mix(h, (uint64_t)bn);
			h = fnv_mix(h, (uint64_t)small);
			h = fnv_mix(h, (uint64_t)fi);
			h = fnv_mix(h, (uint64_t)e[1]);
			h = fnv_mix(h, (uint64_t)e[3]);
			h = fnv_mix(h, fnv(nm, slen(nm)));
		}
	}
	if (!verbose) {
		ps("  entrees");
		kh("fnv", h);
		nl();
	}
	ps("  find");
	kv("DOCS/LISEZ-MOI.TXT", C2(AR_FIND, a, "DOCS/LISEZ-MOI.TXT"));
	kv("absent", C2(AR_FIND, a, "absent"));
	kv("nul", C2(AR_FIND, a, 0));
	kv("hors", C3(AR_ENTRY, a, cnt, tmp));
	kv("hors-1", C2(AR_NAME, a, -1));
	kv("hors-size", C2(AR_ENTRY_SIZE, a, cnt));
	kv("read-nul", C4(AR_READ, a, 0, 0, 100));
	kv("read-hors", C4(AR_READ, a, cnt, back, 100));
	kv("test-hors", C2(AR_TEST, a, cnt));
	kv("load-hors", C2(AR_LOAD, a, cnt));
	kv("entry-nul", cnt ? C3(AR_ENTRY, a, 0, 0) : 0);
	kv("data", C1(AR_DATA, a));
	nl();
	ps("  close");
	kv("->", C1(AR_CLOSE, a));
	nl();
}

/* une archive abîmée : ouverture et lecture de chaque entrée */
static void damaged_archive(const unsigned char *d, long n)
{
	long step = n <= 300 ? 1 : n / 300;
	uint32_t h = 2166136261u;
	row_begin("  tronquee");
	for (long l = 0; l < n; l += step) {
		memcpy(dmg, d, (size_t)l);
		long a = C2(AR_OPEN_MEM, dmg, l);
		if (a < 0) {
			row_add(a);
			continue;
		}
		long cnt = C1(AR_COUNT, a);
		row_add(cnt);
		for (long i = 0; i < cnt; i++)
			h = fnv_mix(h, (uint64_t)C2(AR_TEST, a, i));
		C1(AR_FREE, a);
	}
	row_end();
	ps("  tronquee tests");
	kh("fnv", h);
	nl();
	h = 2166136261u;
	row_begin("  abimee");
	for (long i = 0; i < n; i += step) {
		memcpy(dmg, d, (size_t)n);
		dmg[i] ^= (unsigned char)(0x10 << (i % 4));
		long a = C2(AR_OPEN_MEM, dmg, n);
		if (a < 0) {
			row_add(a);
			continue;
		}
		long cnt = C1(AR_COUNT, a);
		row_add(cnt);
		for (long j = 0; j < cnt; j++) {
			long r = C4(AR_READ, a, j, back, CAPB);
			h = fnv_mix(h, (uint64_t)r);
			if (r > 0)
				h = fnv_mix(h, fnv(back, r));
			const char *nm = (const char *)C2(AR_NAME, a, j);
			h = fnv_mix(h, fnv(nm, slen(nm)));
		}
		C1(AR_CLOSE, a);
	}
	row_end();
	ps("  abimee lectures");
	kh("fnv", h);
	nl();
}

static unsigned char arch[CAPB + 400000];

static void test_archives(void)
{
	ps("== archives en memoire\n");
	name_long(nm120, 120, 'a');
	name_long(nm300, 300, 'b');
	/* 160 : un seul / après 150 octets -> le préfixe ne va pas, nom long */
	for (int i = 0; i < 160; i++)
		nm160[i] = (char)('c' + i % 20);
	nm160[150] = '/';
	nm160[160] = 0;
	for (int fmt = AR_FMT_ZIP; fmt <= AR_FMT_GZIP; fmt++)
		for (int v = 0; v < 3; v++) {
			ps("archive");
			kv("fmt", fmt);
			kv("variante", v);
			nl();
			long w = build(fmt, v);
			if (w < 0)
				continue;
			long n = C1(AR_BYTES, w);
			if (n < 0) {
				C1(AR_FREE, w);
				continue;
			}
			memcpy(arch, (const void *)C1(AR_DATA, w), (size_t)n);
			kv("free", C1(AR_FREE, w));
			nl();
			inspect(arch, n, v != 1);
			if (v == 0)
				damaged_archive(arch, n);
		}
	/* archives vides */
	for (int fmt = AR_FMT_ZIP; fmt <= AR_FMT_GZIP; fmt++) {
		long w = C1(AR_CREATE_MEM, fmt);
		long r = C1(AR_FINISH, w);
		ps("vide");
		kv("fmt", fmt);
		kv("->", r);
		if (r >= 0)
			kh("fnv", fmt == AR_FMT_GZIP || fmt == AR_FMT_TGZ ? 0 : fnv((const void *)C1(AR_DATA, w), r));
		nl();
		if (r >= 0)
			inspect((const unsigned char *)C1(AR_DATA, w), r, 1);
		C1(AR_FREE, w);
	}
	/* un .gz sans nom (deflate_new) */
	long b = C4(AR_DEFLATE_NEW, in_text, 3000, AR_GZIP, 6);
	ps("gz sans nom");
	nl();
	inspect((const unsigned char *)C1(AR_DATA, b), C1(AR_BYTES, b), 1);
	C1(AR_FREE, b);
	/* pas des archives */
	row_begin("pas archive");
	row_add(hres(C2(AR_OPEN_MEM, in_rand, 6000)));
	row_add(hres(C2(AR_OPEN_MEM, in_text, 3000)));
	row_add(hres(C2(AR_OPEN_MEM, in_text, 5)));
	row_add(hres(C2(AR_OPEN_MEM, in_text, 0)));
	row_add(hres(C2(AR_OPEN_MEM, 0, 10)));
	memset(tmp, 0, 2048);
	row_add(hres(C2(AR_OPEN_MEM, tmp, 512)));
	long a = C2(AR_OPEN_MEM, tmp, 1024);
	row_add(a < 0 ? a : C1(AR_FORMAT, a));
	row_add(a < 0 ? a : C1(AR_COUNT, a));
	C1(AR_CLOSE, a);
	a = C2(AR_OPEN_MEM, tmp, 1000);
	row_add(a < 0 ? a : C1(AR_FORMAT, a));
	C1(AR_CLOSE, a);
	row_end();
}

/* ---- archives faites à la main : ZIP64, CP437, .., pax */

static void w16(unsigned char *p, long v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static void w32(unsigned char *p, long v)
{
	w16(p, v);
	w16(p + 2, v >> 16);
}

static void w64(unsigned char *p, long v)
{
	w32(p, v);
	w32(p + 4, v >> 32);
}

static long zip64(unsigned char *o, const char *name, const unsigned char *p, long n, int locsize)
{
	long nl_ = slen(name), k = 0;
	long crc = C3(AR_CRC32, 0, p, n);
	/* en-tête local */
	w32(o + k, 0x04034B50);
	w16(o + k + 4, 45);
	w16(o + k + 6, 0);
	w16(o + k + 8, 0);
	w32(o + k + 10, 0x5A210000);
	w32(o + k + 14, crc);
	w32(o + k + 18, -1);
	w32(o + k + 22, -1);
	w16(o + k + 26, nl_);
	w16(o + k + 28, 20);
	k += 30;
	memcpy(o + k, name, (size_t)nl_);
	k += nl_;
	w16(o + k, 1);
	w16(o + k + 2, 16);
	w64(o + k + 4, n);
	w64(o + k + 12, n);
	k += 20;
	memcpy(o + k, p, (size_t)n);
	k += n;
	long cd = k;
	/* répertoire central : tailles et position dans l'extra */
	w32(o + k, 0x02014B50);
	w16(o + k + 4, 0x031E);
	w16(o + k + 6, 45);
	w16(o + k + 8, 0);
	w16(o + k + 10, 0);
	w32(o + k + 12, 0x5A210000);
	w32(o + k + 16, crc);
	w32(o + k + 20, -1);
	w32(o + k + 24, -1);
	w16(o + k + 28, nl_);
	w16(o + k + 30, 4 + 8 + 8 + 8 + 4 + 2);
	w16(o + k + 32, 0);
	memset(o + k + 34, 0, 8);
	w32(o + k + 42, -1);
	k += 46;
	memcpy(o + k, name, (size_t)nl_);
	k += nl_;
	w16(o + k, 9); /* un extra inconnu d'abord */
	w16(o + k + 2, 2);
	w16(o + k + 4, 0x1234);
	k += 6;
	w16(o + k, 1);
	w16(o + k + 2, locsize);
	w64(o + k + 4, n);
	w64(o + k + 12, n);
	w64(o + k + 20, 0);
	k += 28;
	long cds = k - cd;
	/* fin ZIP64, localisateur, fin */
	long e64 = k;
	w32(o + k, 0x06064B50);
	w64(o + k + 4, 44);
	w16(o + k + 12, 45);
	w16(o + k + 14, 45);
	w32(o + k + 16, 0);
	w32(o + k + 20, 0);
	w64(o + k + 24, 1);
	w64(o + k + 32, 1);
	w64(o + k + 40, cds);
	w64(o + k + 48, cd);
	k += 56;
	w32(o + k, 0x07064B50);
	w32(o + k + 4, 0);
	w64(o + k + 8, e64);
	w32(o + k + 16, 1);
	k += 20;
	w32(o + k, 0x06054B50);
	w32(o + k + 4, 0);
	w16(o + k + 8, 0xFFFF);
	w16(o + k + 10, 0xFFFF);
	w32(o + k + 12, -1);
	w32(o + k + 16, -1);
	w16(o + k + 20, 0);
	k += 22;
	return k;
}

/* en-tête tar : nom, taille, type, date ; somme juste */
static void tar_hdr(unsigned char *h, const char *name, long size, char type, long t, int magic)
{
	memset(h, 0, 512);
	memcpy(h, name, (size_t)slen(name));
	memcpy(h + 100, "0000644", 8);
	memcpy(h + 108, "0000000", 8);
	memcpy(h + 116, "0000000", 8);
	for (int i = 10; i >= 0; i--) {
		h[124 + i] = (unsigned char)('0' + (size & 7));
		size >>= 3;
		h[136 + i] = (unsigned char)('0' + (t & 7));
		t >>= 3;
	}
	h[156] = (unsigned char)type;
	if (magic == 1)
		memcpy(h + 257, "ustar\0" "00", 8);
	if (magic == 2)
		memcpy(h + 257, "ustar  ", 8);
	memset(h + 148, ' ', 8);
	long s = 0;
	for (int i = 0; i < 512; i++)
		s += h[i];
	for (int i = 5; i >= 0; i--) {
		h[148 + i] = (unsigned char)('0' + (s & 7));
		s >>= 3;
	}
	h[154] = 0;
}

static long tar_add(unsigned char *o, long k, const char *name, const void *p, long n, char type, long t, int magic)
{
	tar_hdr(o + k, name, n, type, t, magic);
	k += 512;
	memcpy(o + k, p, (size_t)n);
	k += n;
	while (k % 512)
		o[k++] = 0;
	return k;
}

/* une fiche pax : « longueur clé=valeur\n », la longueur se comptant elle-même */
static long pax_rec(char *o, long k, const char *s)
{
	long l = slen(s) + 2, d = 1;
	while (l + d >= (d == 1 ? 10 : d == 2 ? 100 : 1000))
		d++;
	l += d;
	char b[8];
	int i = 0;
	for (long v = l; v; v /= 10)
		b[i++] = (char)('0' + v % 10);
	while (i)
		o[k++] = b[--i];
	o[k++] = ' ';
	while (*s)
		o[k++] = *s++;
	o[k++] = '\n';
	return k;
}

static void test_crafted_archives(void)
{
	ps("== archives faites a la main\n");
	/* ZIP64, avec des données devant (auto-extractible) */
	memset(arch, 'S', 100);
	long n = zip64(arch + 100, "z64/donnees.txt", in_text, 1000, 24);
	ps("zip64");
	nl();
	inspect(arch + 100, n, 1);
	ps("zip64 devant");
	nl();
	inspect(arch, n + 100, 1);
	n = zip64(arch, "z64/court.txt", in_text, 1000, 8); /* extra trop court */
	ps("zip64 extra court");
	nl();
	inspect(arch, n, 1);
	damaged_archive(arch, n);
	/* CP437 : un nom pas UTF-8 sans le bit 11 ; un .. ; une entrée chiffrée ;
	 * une méthode inconnue */
	long w = C1(AR_CREATE_MEM, AR_FMT_ZIP);
	C2(AR_SET_TIME, w, T0);
	C5(AR_ADD_MEM, w, "caf\x82\x90\xe9.txt", in_short, 10, 0);
	C5(AR_ADD_MEM, w, "../evil.txt", in_short, 10, 6);
	C5(AR_ADD_MEM, w, "a/../../b.txt", in_short, 10, 6);
	C5(AR_ADD_MEM, w, "chiffre.txt", in_short, 10, 6);
	C5(AR_ADD_MEM, w, "methode.txt", in_short, 10, 0);
	C5(AR_ADD_MEM, w, "crc.txt", in_short, 30, 6);
	n = C1(AR_FINISH, w);
	memcpy(arch, (const void *)C1(AR_DATA, w), (size_t)n);
	C1(AR_FREE, w);
	/* le répertoire central : drapeaux et méthodes changés */
	for (long i = 0; i + 46 <= n; i++)
		if (arch[i] == 0x50 && arch[i + 1] == 0x4B && arch[i + 2] == 1 && arch[i + 3] == 2) {
			const char *nm = (const char *)arch + i + 46;
			if (nm[0] == 'c' && nm[1] == 'a')
				w16(arch + i + 8, 0);
			if (nm[0] == 'c' && nm[1] == 'h')
				w16(arch + i + 8, 1);
			if (nm[0] == 'm')
				w16(arch + i + 10, 12);
			if (nm[0] == 'c' && nm[1] == 'r')
				arch[i + 16] ^= 1;
		}
	ps("zip retouche");
	nl();
	inspect(arch, n, 1);
	/* tar : pax (path, size, mtime), GNU L, vieux tar sans ustar, liens */
	long k = 0;
	char pax[256];
	long pl = 0;
	pl = pax_rec(pax, pl, "path=pax/un/nom/long.txt");
	pl = pax_rec(pax, pl, "mtime=1234567890");
	pl = pax_rec(pax, pl, "size=0000012");
	pl = pax_rec(pax, pl, "aaa=bbbb");
	memcpy(pax + pl, "9 x=1\n", 6); /* (trop long : la fin) */
	pl += 6;
	k = tar_add(arch, k, "PaxHeaders/x", pax, pl, 'x', T0, 1);
	k = tar_add(arch, k, "court", in_text, 12, '0', T0, 1);
	k = tar_add(arch, k, "././@LongLink", "gnu/nom/tres/long/vraiment.txt", 31, 'L', 0, 2);
	k = tar_add(arch, k, "tronque", in_text, 100, '0', T0, 2);
	k = tar_add(arch, k, "vieux.txt", in_text, 50, 0, T0, 0);
	k = tar_add(arch, k, "lien", "", 0, '2', T0, 1);
	k = tar_add(arch, k, "global", "9 x=1\n", 6, 'g', T0, 1);
	k = tar_add(arch, k, "dossier", "", 0, '5', T0, 1);
	k = tar_add(arch, k, "pax2", "13 path=\n8 ab\n", 14, 'x', T0, 1);
	k = tar_add(arch, k, "apres-pax2.txt", in_text, 7, '7', T0, 1);
	memset(arch + k, 0, 1024);
	k += 1024;
	ps("tar fait main");
	nl();
	inspect(arch, k, 1);
	damaged_archive(arch, k);
	/* ce tar dans un gzip : un tar.gz */
	long b = C4(AR_DEFLATE_NEW, arch, k, AR_GZIP, 6);
	ps("tgz fait main");
	nl();
	inspect((const unsigned char *)C1(AR_DATA, b), C1(AR_BYTES, b), 1);
	C1(AR_FREE, b);
}

static void test_misc(void)
{
	ps("== questions\n");
	static unsigned char fi[4 * 128 + 8];
	memset(fi, 0xEE, sizeof fi);
	ps("formats");
	kv("compter", C2(AR_FORMATS, 0, 0));
	kv("deux", C2(AR_FORMATS, fi, 2));
	kv("tous", C2(AR_FORMATS, fi, 9));
	nl();
	for (int i = 0; i < 4; i++) {
		const long *q = (const long *)(fi + i * 128);
		ps(" ");
		pd(q[0]);
		pc(' ');
		pd(q[1]);
		pc(' ');
		pd(q[2]);
		pc(' ');
		pq(fi + i * 128 + 24, slen((const char *)fi + i * 128 + 24));
		pc(' ');
		pq(fi + i * 128 + 40, slen((const char *)fi + i * 128 + 40));
		nl();
	}
	ps("format_name");
	for (int i = -1; i <= 5; i++) {
		long s = C1(AR_FORMAT_NAME, i);
		pc(' ');
		if (s)
			pq((const char *)s, slen((const char *)s));
		else
			ps("0");
	}
	nl();
	static const long errs[] = {-2, -5, -12, -13, -22, -27, -28, -38, -74, -84, -95, 0, -1, -3, -258, 5, -256 - 2};
	for (unsigned i = 0; i < sizeof errs / sizeof errs[0]; i++) {
		const char *s = (const char *)C1(AR_STRERROR, errs[i]);
		ps("strerror ");
		pd(errs[i]);
		pc(' ');
		pq(s, slen(s));
		nl();
	}
	ps("poignees");
	static long zeros[16];
	kv("data0", C1(AR_DATA, 0));
	kv("data-5", C1(AR_DATA, -5));
	kv("datazero", C1(AR_DATA, zeros));
	kv("bytes0", C1(AR_BYTES, 0));
	kv("free0", C1(AR_FREE, 0));
	kv("free-1", C1(AR_FREE, -1));
	kv("freezero", C1(AR_FREE, zeros));
	kv("close0", C1(AR_CLOSE, 0));
	kv("close-1", C1(AR_CLOSE, -1));
	kv("closezero", C1(AR_CLOSE, zeros));
	kv("format0", C1(AR_FORMAT, 0));
	kv("count-1", C1(AR_COUNT, -1));
	kv("countzero", C1(AR_COUNT, zeros));
	kv("entry0", C3(AR_ENTRY, 0, 0, tmp));
	kv("name0", C2(AR_NAME, 0, 0));
	kv("size0", C2(AR_ENTRY_SIZE, 0, 0));
	kv("find0", C2(AR_FIND, 0, "x"));
	kv("read0", C4(AR_READ, 0, 0, tmp, 10));
	kv("load0", C2(AR_LOAD, 0, 0));
	kv("test0", C2(AR_TEST, 0, 0));
	kv("extract0", C3(AR_EXTRACT, 0, 0, "RAM0:/x"));
	kv("extractall0", C2(AR_EXTRACT_ALL, 0, "RAM0:/x"));
	kv("create_mem0", hres(C1(AR_CREATE_MEM, 0)));
	kv("create_mem5", hres(C1(AR_CREATE_MEM, 5)));
	kv("create9", hres(C2(AR_CREATE, "RAM0:/x", 9)));
	kv("add0", C5(AR_ADD_MEM, 0, "x", tmp, 1, 6));
	kv("addzero", C5(AR_ADD_MEM, zeros, "x", tmp, 1, 6));
	kv("dir0", C2(AR_ADD_DIR, 0, "x"));
	kv("file0", C4(AR_ADD_FILE, 0, 0, "RAM0:/x", 6));
	kv("time0", C2(AR_SET_TIME, 0, 1));
	kv("finish0", C1(AR_FINISH, 0));
	nl();
	/* poignées croisées : un tampon n'est pas une archive, ni un écrivain */
	long b = C4(AR_DEFLATE_NEW, in_short, 10, AR_RAW, 6);
	long w = C1(AR_CREATE_MEM, AR_FMT_TAR);
	ps("croisees");
	kv("count-tampon", C1(AR_COUNT, b));
	kv("add-tampon", C5(AR_ADD_MEM, b, "x", tmp, 1, 6));
	kv("data-ecrivain", C1(AR_DATA, w));
	kv("bytes-ecrivain", C1(AR_BYTES, w));
	kv("close-ecrivain", C1(AR_CLOSE, w));
	kv("free-ecrivain", C1(AR_FREE, w));
	kv("free-tampon", C1(AR_FREE, b));
	nl();
}

/* ---- fichiers (RAM0:), avec -f */

static long ak_write(const char *path, const void *p, long n)
{
	long fd = ak_call(AK_F_OUVRIR, (long)path, 2 | 4 | 8, 0);
	if (fd < 0)
		return fd;
	long r = ak_call(AK_F_ECRIRE, fd, (long)p, n);
	ak_call(AK_F_FERMER, fd, 0, 0);
	return r;
}

static void file_archive(const char *path, int fmt)
{
	long w = C2(AR_CREATE, path, fmt);
	ps("create ");
	ps(path);
	if (w < 0) {
		kv("->", w);
		nl();
		return;
	}
	kv("temps", C2(AR_SET_TIME, w, T0));
	if (fmt == AR_FMT_GZIP) {
		kv("fichier", C4(AR_ADD_FILE, w, 0, "RAM0:/artest/src/texte.txt", 6));
	} else {
		kv("mem", C5(AR_ADD_MEM, w, "mem.txt", in_text, 3000, 6));
		kv("hasard", C5(AR_ADD_MEM, w, "hasard.bin", in_rand, 6000, 6));
		kv("fichier", C4(AR_ADD_FILE, w, "copie/texte.txt", "RAM0:/artest/src/texte.txt", 9));
		kv("absent", C4(AR_ADD_FILE, w, 0, "RAM0:/artest/absent", 6));
		kv("nul", C4(AR_ADD_FILE, w, 0, 0, 6));
	}
	long r = C1(AR_FINISH, w);
	/* (tar.gz : la date du fichier, d'aujourd'hui, change la taille compressée) */
	kv("finish", fmt == AR_FMT_TGZ && r > 0 ? 1 : r);
	kv("data", C1(AR_DATA, w));
	kv("free", C1(AR_FREE, w));
	nl();
	ps(" probe");
	kv("->", C1(AR_PROBE, path));
	nl();
	long a = C1(AR_OPEN, path);
	ps(" open");
	if (a < 0) {
		kv("->", a);
		nl();
		return;
	}
	long cnt = C1(AR_COUNT, a);
	kv("fmt", C1(AR_FORMAT, a));
	kv("n", cnt);
	nl();
	for (long i = 0; i < cnt; i++) {
		const char *nm = (const char *)C2(AR_NAME, a, i);
		long rr = C4(AR_READ, a, i, back, CAPB);
		ps("  ");
		pq(nm, slen(nm));
		kv("taille", C2(AR_ENTRY_SIZE, a, i));
		kv("read", rr);
		if (rr > 0)
			kh("fnv", fnv(back, rr));
		kv("test", C2(AR_TEST, a, i));
		nl();
	}
	ps(" extract");
	kv("un", C3(AR_EXTRACT, a, 0, "RAM0:/artest/un/deux/fichier.bin"));
	kv("tout", C2(AR_EXTRACT_ALL, a, "RAM0:/artest/x"));
	kv("hors", C3(AR_EXTRACT, a, cnt, "RAM0:/artest/hors"));
	kv("close", C1(AR_CLOSE, a));
	nl();
	/* relu par une archive en mémoire */
	w = C1(AR_CREATE_MEM, AR_FMT_TAR);
	C2(AR_SET_TIME, w, T0);
	ps(" relu");
	kv("fichier", C4(AR_ADD_FILE, w, "f", "RAM0:/artest/un/deux/fichier.bin", 6));
	r = C1(AR_FINISH, w);
	a = C2(AR_OPEN_MEM, C1(AR_DATA, w), r);
	long b = C2(AR_LOAD, a, 0);
	kv("octets", C1(AR_BYTES, b));
	kh("fnv", fnv((const void *)C1(AR_DATA, b), C1(AR_BYTES, b)));
	C1(AR_FREE, b);
	C1(AR_CLOSE, a);
	C1(AR_FREE, w);
	nl();
}

static void test_files(void)
{
	ps("=== fichiers (RAM0:)\n");
	ak = sys2(SYS_LIB_OPEN, "appkit", 32);
	if (ak < 0) {
		ps("appkit absent\n");
		return;
	}
	ak_call(AK_F_MKDIR, (long)"RAM0:/artest", 0, 0);
	ak_call(AK_F_MKDIR, (long)"RAM0:/artest/src", 0, 0);
	ps("ecrit");
	kv("texte", ak_write("RAM0:/artest/src/texte.txt", in_text, 3000));
	kv("gros", ak_write("RAM0:/artest/src/gros.bin", in_big, BIG));
	nl();
	file_archive("RAM0:/artest/a.zip", AR_FMT_ZIP);
	file_archive("RAM0:/artest/a.tar", AR_FMT_TAR);
	file_archive("RAM0:/artest/a.tgz", AR_FMT_TGZ);
	file_archive("RAM0:/artest/texte.txt.gz", AR_FMT_GZIP);
	ps("probe");
	kv("absent", C1(AR_PROBE, "RAM0:/artest/absent.zip"));
	kv("texte", C1(AR_PROBE, "RAM0:/artest/src/texte.txt"));
	kv("open-absent", C1(AR_OPEN, "RAM0:/artest/absent.zip"));
	kv("create-nul", hres(C2(AR_CREATE, "RAM0:/artest/pas/de/dossier/a.zip", AR_FMT_ZIP)));
	nl();
	/* un dossier entier (l'ordre et les dates viennent du disque : seulement
	 * le nombre d'entrées et le contenu) */
	long w = C1(AR_CREATE_MEM, AR_FMT_ZIP);
	ps("dossier");
	kv("ajout", C4(AR_ADD_FILE, w, 0, "RAM0:/artest/src", 6));
	long r = C1(AR_FINISH, w);
	long a = C2(AR_OPEN_MEM, C1(AR_DATA, w), r);
	long cnt = C1(AR_COUNT, a);
	kv("n", cnt);
	uint32_t h = 0;
	for (long i = 0; i < cnt; i++) {
		const char *nm = (const char *)C2(AR_NAME, a, i);
		long rr = C4(AR_READ, a, i, back, CAPB);
		h += fnv(nm, slen(nm)) ^ (rr > 0 ? fnv(back, rr) : (uint32_t)rr); /* (sans l'ordre) */
	}
	kh("fnv", h);
	C1(AR_CLOSE, a);
	C1(AR_FREE, w);
	nl();
	ak_call(AK_F_UNLINK, (long)"RAM0:/artest/src/gros.bin", 0, 0);
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
	ps("artest : ArchiveKit\n");
	long a = sys2(SYS_LIB_OPEN, "appkit", 26);
	if (a >= 0) {
		const uint32_t *at = (const uint32_t *)a;
		f_print = (fn_print)(a + at[2 + AK_F_PRINT]);
	}
	kit = open_kit("archivekit", 1);
	if (kit < 0) {
		ps("archivekit absent : ");
		pd(kit);
		nl();
		flush();
		sys6(SYS_EXIT, 127, 0, 0, 0, 0, 0);
	}
	const uint32_t *t = (const uint32_t *)kit;
	kit_count = t[1];
	ps("table magic=");
	px(t[0], 8);
	ps(" count=");
	pd(kit_count);
	nl();
	if (t[0] != KIT_MAGIC || kit_count < AR_COUNT_FN) {
		ps("table invalide\n");
		flush();
		sys6(SYS_EXIT, 126, 0, 0, 0, 0, 0);
	}
	make_inputs();
	test_sums();
	test_deflate();
	test_new();
	test_damage();
	test_crafted();
	test_archives();
	test_crafted_archives();
	test_misc();
	int files = has_flag(line, 'f');
	for (int i = 0; i < AR_COUNT_FN; i++)
		if (!called[i] && i != AR_OPEN && i != AR_PROBE) { /* (ces deux-là : -f) */
			ps("jamais appelee : ");
			ps(fn_names[i]);
			nl();
		}
	ps("fin : ");
	pd(checks);
	ps(" controles, ");
	pd(fails);
	ps(" echecs\n");
	if (files)
		test_files();
	flush();
	sys6(SYS_EXIT, fails > 125 ? 125 : fails, 0, 0, 0, 0, 0);
}

#if defined(__x86_64__)
__attribute__((force_align_arg_pointer, noreturn))
#else
__attribute__((noreturn))
#endif
void _start(const char *line) /* x0, rdi : la ligne d'arguments (0 : aucune) */
{
	run(line);
	for (;;)
		;
}
