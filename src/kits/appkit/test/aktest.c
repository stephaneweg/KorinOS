/* aktest : essai différentiel d'AppKit (src/kits/appkit), sans bibliothèque,
 * pour AArch64 (Korin sur le Pi) et x86-64 (Korin sur PC).
 *
 * Appelle les fonctions du kit par sa table (comme un programme en
 * assembleur : sur le Pi, par les relais du kit en C) et n'écrit que ce qui ne
 * dépend ni des adresses, ni de l'heure, ni des pids : résultats, codes
 * d'erreur, tailles, sommes FNV-1a. Le kit en assembleur et le kit en C
 * doivent donner des sorties identiques :
 *     aktest        partie sans fichiers (chaînes, nombres, UTF-8, chemins, tas,
 *                   verrous et conditions avec des threads, fork, file_sys
 *                   sur la sortie standard) : la même sur le Pi et sur le PC
 *     aktest -f     en plus, après « === Pi », ce qui demande vfs et les
 *                   services : langue, fichiers et répertoires de /tmp,
 *                   tubes, spawn (/bin/echo, /bin/cat...), argv et
 *                   environnement, scripts, kits, fork avec fichiers, exec ;
 *                   et sur AArch64 les registres gardés par file_sys et
 *                   f_exec_resume (x1-x15, x17, x18, NZCV)
 * Modes internes (lancés par aktest -f lui-même) : -a (argv et environnement
 * reçus, f_start_blob), -x N (après un exec : le descripteur N repris).
 * Contrôles propres : « ECHEC ... » ; code de sortie = nombre d'échecs.
 *
 * Construction : src/kits/appkit/test/build.sh (dans WSL). */
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

/* src/inc/syscalls.inc */
#define SYS_SBRK 0x00
#define SYS_EXIT 0x10
#define SYS_WAIT 0x13
#define SYS_SLEEP 0x17
#define SYS_YIELD 0x18
#define SYS_THREAD_CREATE 0x20
#define SYS_THREAD_EXIT 0x21
#define SYS_SVC_OPEN 0x31
#define SYS_SVC_CALL 0x32
#define SYS_PRINT 0x60
#define SYS_LIB_OPEN 0x64
#define SYS_FORK 0x6A
#define SYS_OPEN 0x80
#define SYS_READ 0x81
#define SYS_WRITE 0x82
#define SYS_SEEK 0x83
#define SYS_CLOSE 0x84
#define SYS_STAT 0x85
#define SYS_READDIR 0x86
#define SYS_MKDIR 0x87
#define SYS_UNLINK 0x88
#define SYS_CHDIR 0x89
#define SYS_GETCWD 0x8A
#define SYS_SPAWN 0x8B
#define SYS_SPAWN_PIPE 0x8C
#define SYS_PRELOAD 0x8D
#define SYS_UNLOAD 0x8E

#define KIT_MAGIC 0x54494B41
#define O_READ 1
#define O_WRITE 2
#define O_CREATE 4
#define O_TRUNC 8
#define O_APPEND 16
#define O_NONBLOCK 32
#define V_EXEC 36
#define E_AGAIN -11

/* rangs de appkit.abi */
enum {
	PUTS, FMT_DEC, PRINT_DEC, STR_CAT, MALLOC, STR_LEN, STR_EQ, MEM_MOVE, UTF8_PUT, PATH_JOIN,
	PATH_PARENT, PARSE_DEC, STR_ENDS, FICHIER_SYS, F_OUVRIR, F_LIRE, F_ECRIRE, F_POSITION, F_FERMER,
	F_STAT, F_LIRE_REP, F_MKDIR, F_UNLINK, F_CHDIR, F_GETCWD, F_PRINT, F_SPAWN, F_SPAWN_PIPE,
	F_SPAWN_STDIO, F_SPAWN_FILE, F_PIPE, F_KIT_LOAD, MTX_LOCK, MTX_TRYLOCK, MTX_UNLOCK, CND_WAIT,
	CND_TIMEDWAIT, CND_SIGNAL, CND_BROADCAST, F_REDIR, F_SPAWN_ENV, F_START_BLOB, LANG_TR, LANG_PUTS,
	LANG_GET, LANG_LOAD, F_FORK, F_FORK_CHILD, F_LIRE_A, F_ECRIRE_A, F_EXEC, F_EXEC_FILE,
	F_EXEC_REPRISE, AK_N
};

static const char *const fn_names[AK_N] = {
	"puts", "fmt_dec", "print_dec", "str_cat", "malloc", "str_len", "str_eq", "mem_move", "utf8_put",
	"path_join", "path_parent", "parse_dec", "str_ends", "file_sys", "f_open", "f_read",
	"f_write", "f_seek", "f_close", "f_stat", "f_readdir", "f_mkdir", "f_unlink", "f_chdir",
	"f_getcwd", "f_print", "f_spawn", "f_spawn_pipe", "f_spawn_stdio", "f_spawn_file", "f_pipe",
	"f_kit_load", "mtx_lock", "mtx_trylock", "mtx_unlock", "cnd_wait", "cnd_timedwait", "cnd_signal",
	"cnd_broadcast", "f_redir", "f_spawn_env", "f_start_blob", "lang_tr", "lang_puts", "lang_get",
	"lang_load", "f_fork", "f_fork_child", "f_pread", "f_pwrite", "f_exec", "f_exec_file",
	"f_exec_resume",
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
#define sys0(n) sys6((n), 0, 0, 0, 0, 0, 0)
#define sys1(n, a) sys6((n), (long)(a), 0, 0, 0, 0, 0)
#define sys2(n, a, b) sys6((n), (long)(a), (long)(b), 0, 0, 0, 0)
#define sys3(n, a, b, c) sys6((n), (long)(a), (long)(b), (long)(c), 0, 0, 0)

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

static void scpy(char *d, const char *s)
{
	while ((*d++ = *s++))
		;
}

/* ---- le kit */

static long ak;
static long called[AK_N];

static long fn(int i)
{
	called[i]++;
	return ak + ((const u32 *)ak)[2 + i];
}

typedef long (*fn4)(long, long, long, long);
static long C(int i, long a, long b, long c, long d)
{
	return ((fn4)fn(i))(a, b, c, d);
}
#define L(x) ((long)(x))
#define C0(i) C(i, 0, 0, 0, 0)
#define C1(i, a) C(i, L(a), 0, 0, 0)
#define C2(i, a, b) C(i, L(a), L(b), 0, 0)
#define C3(i, a, b, c) C(i, L(a), L(b), L(c), 0)
#define C4(i, a, b, c, d) C(i, L(a), L(b), L(c), L(d))

/* ---- sortie : f_print d'AppKit (sortie standard), sinon la console */

static char pb[4096];
static long pn;

static void flush(void)
{
	if (!pn)
		return;
	if (!ak || ((long (*)(const char *, long))(ak + ((const u32 *)ak)[2 + F_PRINT]))(pb, pn) < 0)
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

static void pu(u64 v)
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
		pu(-(u64)v);
	} else {
		pu((u64)v);
	}
}

static void px(u64 v, int digits)
{
	for (int i = digits - 1; i >= 0; i--)
		pc("0123456789abcdef"[(v >> (i * 4)) & 15]);
}

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

static void pz(const char *s)
{
	if (!s)
		ps("(nul)");
	else
		pq(s, slen(s));
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

static void sec(const char *s)
{
	ps("-- ");
	ps(s);
	nl();
}

static u32 fnv(const void *p, long n)
{
	const unsigned char *s = p;
	u32 h = 2166136261u;
	for (long i = 0; i < n; i++) {
		h ^= s[i];
		h *= 16777619u;
	}
	return h;
}

static void kh(const char *k, u32 h)
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

/* ---- file_sys : x8 = numéro (sur le Pi, comme la macro sys), 7e argument
 * sur PC ; sur le Pi, les registres d'après l'appel sont comparés à ceux
 * d'avant (x1-x15, x17, x18, NZCV ; x16 : le relais) */

static u64 regbad __attribute__((unused)); /* bits 1..18 : registres changés, bit 31 : NZCV */
static long regcalls __attribute__((unused));

#if defined(__aarch64__)
/* regcall(fonction, in[9] = x0..x8, out[20] = x0..x18, NZCV) : x9-x15 et x17
 * à des motifs, NZCV à 0x60000000, x16 = la fonction */
__asm__(".text\n"
	".p2align 2\n"
	"regcall:\n"
	"\tstp x29, x30, [sp, -32]!\n"
	"\tmov x29, sp\n"
	"\tstr x2, [sp, 16]\n"
	"\tmov x16, x0\n"
	"\tmov x17, #0x60000000\n"
	"\tmsr nzcv, x17\n"
	"\tldr x9, =0x0909090909090909\n"
	"\tldr x10, =0x1010101010101010\n"
	"\tldr x11, =0x1111111111111111\n"
	"\tldr x12, =0x1212121212121212\n"
	"\tldr x13, =0x1313131313131313\n"
	"\tldr x14, =0x1414141414141414\n"
	"\tldr x15, =0x1515151515151515\n"
	"\tldr x17, =0x1717171717171717\n"
	"\tldp x2, x3, [x1, 16]\n"
	"\tldp x4, x5, [x1, 32]\n"
	"\tldp x6, x7, [x1, 48]\n"
	"\tldr x8, [x1, 64]\n"
	"\tldr x0, [x1]\n"
	"\tldr x1, [x1, 8]\n"
	"\tblr x16\n"
	"\tldr x30, [sp, 16]\n"
	"\tstp x0, x1, [x30]\n"
	"\tstp x2, x3, [x30, 16]\n"
	"\tstp x4, x5, [x30, 32]\n"
	"\tstp x6, x7, [x30, 48]\n"
	"\tstp x8, x9, [x30, 64]\n"
	"\tstp x10, x11, [x30, 80]\n"
	"\tstp x12, x13, [x30, 96]\n"
	"\tstp x14, x15, [x30, 112]\n"
	"\tstp x16, x17, [x30, 128]\n"
	"\tstr x18, [x30, 144]\n"
	"\tmrs x0, nzcv\n"
	"\tstr x0, [x30, 152]\n"
	"\tldp x29, x30, [sp], 32\n"
	"\tret\n"
	".ltorg\n");
long regcall(long f, const long *in, long *out);

/* -> x0 ; garde : bits des registres qu'on veut retrouver (1..18, 31) */
static long rcall(long f, const long *in, u64 garde)
{
	long out[20];
	long x18;
	__asm__ volatile("mov %0, x18" : "=r"(x18));
	regcall(f, in, out);
	long r = out[0];
	static const u64 pat[19] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0909090909090909ULL, 0x1010101010101010ULL,
				    0x1111111111111111ULL, 0x1212121212121212ULL, 0x1313131313131313ULL,
				    0x1414141414141414ULL, 0x1515151515151515ULL, 0, 0x1717171717171717ULL, 0};
	for (int i = 1; i <= 18; i++) {
		u64 want = i <= 8 ? (u64)in[i] : i == 16 ? (u64)f : i == 18 ? (u64)x18 : pat[i];
		if (((garde >> i) & 1) && (u64)out[i] != want)
			regbad |= 1ULL << i;
	}
	if (((garde >> 31) & 1) && (out[19] & 0xF0000000) != 0x60000000)
		regbad |= 1ULL << 31;
	regcalls++;
	return r;
}
#define GARDE_SYS 0x8006FFFEULL /* x1-x15, x17, x18, NZCV */
#endif

static long fsys(long nr, long a0, long a1, long a2)
{
#if defined(__aarch64__)
	long in[9] = {a0, a1, a2, 0x33, 0x44, 0x55, 0x66, 0x77, nr};
	return rcall(fn(FICHIER_SYS), in, GARDE_SYS);
#else
	return ((long (*)(long, long, long, long, long, long, long))fn(FICHIER_SYS))(a0, a1, a2, 0, 0, 0, nr);
#endif
}

static long ak_open(void)
{
	return sys2(SYS_LIB_OPEN, "appkit", 1);
}

static void sexit(long code)
{
	flush();
	sys1(SYS_EXIT, code);
	for (;;)
		;
}

/* =========================================================================
 *  partie sans fichiers
 * ========================================================================= */

static void t_strings(void)
{
	static char b[256];
	sec("chaines");
	static const char *const ls[] = {"", "a", "bonjour", "\xc3\xa9t\xc3\xa9"};
	for (unsigned i = 0; i < 4; i++) {
		ps("str_len");
		pz(ls[i]);
		kv("n", C1(STR_LEN, ls[i]));
		nl();
	}
	static const char *const eq[][2] = {{"", ""}, {"a", "a"}, {"a", "b"}, {"ab", "a"}, {"a", "ab"}, {"Abc", "abc"}};
	for (unsigned i = 0; i < 6; i++) {
		ps("str_eq ");
		pz(eq[i][0]);
		pc(' ');
		pz(eq[i][1]);
		kv("r", C2(STR_EQ, eq[i][0], eq[i][1]));
		nl();
	}
	b[0] = 0;
	long e = C2(STR_CAT, b, "/x:");
	kv("str_cat1", e - L(b));
	e = C2(STR_CAT, e, "/apps");
	kv("str_cat2", e - L(b));
	e = C2(STR_CAT, e, "");
	kv("str_cat3", e - L(b));
	pc(' ');
	pz(b);
	nl();
	static const char *const en[][2] = {{"photo.PNG", ".png"}, {"a.txt", ".TXT"}, {"x", "xy"}, {"", ""},
					    {"abc", ""}, {"a@", "`"}, {"A[", "a{"}, {"file.so", "so"}, {"so", ".so"}};
	for (unsigned i = 0; i < 9; i++) {
		ps("str_ends ");
		pz(en[i][0]);
		pc(' ');
		pz(en[i][1]);
		kv("r", C2(STR_ENDS, en[i][0], en[i][1]));
		nl();
	}
	/* mem_move : recouvrements dans les deux sens, petits et grands blocs */
	static const long mv[][3] = {{0, 10, 50}, {10, 0, 50}, {0, 3, 40}, {3, 0, 40}, {5, 5, 20},
				     {0, 100, 0}, {1, 0, 1}, {0, 17, 100}, {17, 0, 100}, {0, 16, 33}};
	for (unsigned i = 0; i < 10; i++) {
		for (int k = 0; k < 256; k++)
			b[k] = (char)(k * 7 + 1);
		long r = C3(MEM_MOVE, b + mv[i][0], b + mv[i][1], mv[i][2]);
		ps("mem_move");
		kv("d", mv[i][0]);
		kv("s", mv[i][1]);
		kv("n", mv[i][2]);
		kv("ret", r - L(b));
		kh("h", fnv(b, 256));
		nl();
	}
}

static void t_numbers(void)
{
	static char b[64];
	sec("nombres");
	static const u64 vs[] = {0, 1, 9, 10, 99, 100, 12345, 4294967295ULL, 4294967296ULL, 9223372036854775807ULL,
				 9223372036854775808ULL, 18446744073709551615ULL};
	for (unsigned i = 0; i < sizeof vs / sizeof vs[0]; i++) {
		memset(b, 'x', sizeof b);
		long e = C2(FMT_DEC, vs[i], b);
		ps("fmt_dec");
		kv("len", e - L(b));
		pc(' ');
		pz(b);
		nl();
	}
	static const char *const ds[] = {"", "0", "42", "123abc", " 12", "007", "18446744073709551615",
					 "18446744073709551616", "99999999999999999999999", "-5", "9/"};
	for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
		ps("parse_dec ");
		pz(ds[i]);
		ps(" v=");
		pu((u64)C1(PARSE_DEC, ds[i]));
		nl();
	}
	static const long ps_[] = {0, 7, -7, 1234567890, -9223372036854775807L - 1, 9223372036854775807L};
	ps("print_dec :");
	flush();
	for (unsigned i = 0; i < sizeof ps_ / sizeof ps_[0]; i++) {
		C1(PUTS, " ");
		C1(PRINT_DEC, ps_[i]);
	}
	C1(PUTS, "\n");
	ps("puts vide :");
	flush();
	C1(PUTS, "");
	C1(PUTS, "[fin]\n");
	flush();
	C2(F_PRINT, "f_print 12 octets\n", 18);
	C2(F_PRINT, "rien", 0);
}

static void t_utf8(void)
{
	static u8 b[8];
	sec("utf8_put");
	static const u32 cps[] = {0, 0x41, 0x7F, 0x80, 0xE9, 0x7FF, 0x800, 0x20AC, 0xFFFF, 0x10000, 0x1F600,
				  0x10FFFF, 0xFFFFFFFF};
	for (unsigned i = 0; i < sizeof cps / sizeof cps[0]; i++) {
		memset(b, 0xAA, sizeof b);
		long n = C2(UTF8_PUT, cps[i], b);
		ps("U+");
		px(cps[i], 8);
		kv("n", n);
		pc(' ');
		for (int k = 0; k < 4; k++)
			px(b[k], 2);
		nl();
	}
}

static void t_paths(void)
{
	static char b[300];
	sec("chemins");
	static const char *const pj[][2] = {{"/a", "b"}, {"/", "b"}, {"/", "x"}, {"/tmp/", "f.txt"},
					    {"a/b", ""}, {"x", "y/z"}};
	for (unsigned i = 0; i < 6; i++) {
		b[0] = 'Q';
		long e = C3(PATH_JOIN, b + 1, pj[i][0], pj[i][1]);
		ps("path_join");
		kv("len", e - L(b + 1));
		pc(' ');
		pz(b + 1);
		nl();
	}
	/* répertoire vide : l'octet d'avant la destination est lu (comme l'asm) */
	for (int k = 0; k < 2; k++) {
		b[0] = k ? '/' : 'Q';
		long e = C3(PATH_JOIN, b + 1, "", "nom");
		ps("path_join vide avant=");
		pc(b[0]);
		kv("len", e - L(b + 1));
		pc(' ');
		pz(b + 1);
		nl();
	}
	static const char *const pp[] = {"/a/b", "/a", "/", "/sd", "/a/b", "abc", "a/b", "/a/b/",
					 "X:/y/", "/tmp/aktest/f", "", "//", "a:b/c"};
	for (unsigned i = 0; i < sizeof pp / sizeof pp[0]; i++) {
		scpy(b, pp[i]);
		long r = C1(PATH_PARENT, b);
		ps("path_parent ");
		pz(pp[i]);
		ps(" -> ");
		pz(b);
		kv("ret", r - L(b));
		nl();
	}
}

static void t_malloc(void)
{
	sec("malloc");
	static const u64 sz[] = {0, 1, 15, 16, 17, 100, 4096};
	long prev = C1(MALLOC, 1);
	ck(prev != 0, "malloc(1)");
	for (unsigned i = 0; i < sizeof sz / sizeof sz[0]; i++) {
		long p = C1(MALLOC, sz[i]);
		ps("malloc");
		kv("n", (long)sz[i]);
		kv("nul", p == 0);
		kv("align16", (p & 15) == 0);
		kv("ecart", p - prev);
		nl();
		if (p) {
			memset((void *)p, 0x5A, sz[i]);
			prev = p;
		}
	}
	long p = C1(MALLOC, 1ULL << 62);
	ps("malloc 2^62");
	kv("nul", p == 0);
	nl();
}

/* ---- verrous et conditions, avec des threads */

#define NTH 4
#define NINC 20000
static u32 mtx, cnd, cnd2;
static volatile long counter, flag, done_cnt;
static volatile long exited;
static u64 stacks[NTH + 4][2048] __attribute__((aligned(16)));

__attribute__((noreturn)) static void th_exit(void)
{
	__atomic_add_fetch(&exited, 1, __ATOMIC_SEQ_CST);
	sys1(SYS_THREAD_EXIT, 0);
	for (;;)
		;
}

#if defined(__x86_64__)
#define THREAD_ATTR __attribute__((force_align_arg_pointer, noreturn))
#else
#define THREAD_ATTR __attribute__((noreturn))
#endif

THREAD_ATTR static void th_inc(long arg)
{
	(void)arg;
	for (int i = 0; i < NINC; i++) {
		C1(MTX_LOCK, &mtx);
		long v = counter;
		if ((i & 255) == 0)
			sys0(SYS_YIELD);
		counter = v + 1;
		C1(MTX_UNLOCK, &mtx);
	}
	th_exit();
}

/* attend flag == 1, le passe à 2, prévient */
THREAD_ATTR static void th_pong(long arg)
{
	(void)arg;
	C1(MTX_LOCK, &mtx);
	while (flag != 1)
		C2(CND_WAIT, &cnd, &mtx);
	flag = 2;
	C1(CND_SIGNAL, &cnd);
	C1(MTX_UNLOCK, &mtx);
	th_exit();
}

/* attend flag == 3 (diffusé), compte */
THREAD_ATTR static void th_bcast(long arg)
{
	(void)arg;
	C1(MTX_LOCK, &mtx);
	while (flag != 3)
		C2(CND_WAIT, &cnd, &mtx);
	done_cnt++;
	C1(CND_SIGNAL, &cnd2);
	C1(MTX_UNLOCK, &mtx);
	th_exit();
}

static int nstack;
static long start_thread(void (*f)(long), long arg)
{
	u64 *top = &stacks[nstack++][2048];
	return sys3(SYS_THREAD_CREATE, f, top, arg);
}

static void wait_exited(long n)
{
	for (int i = 0; i < 20000 && __atomic_load_n(&exited, __ATOMIC_SEQ_CST) < n; i++)
		sys1(SYS_SLEEP, 1);
}

static void t_sync(void)
{
	sec("verrous");
	C1(MTX_LOCK, &mtx);
	long a = C1(MTX_TRYLOCK, &mtx);
	C1(MTX_UNLOCK, &mtx);
	long b = C1(MTX_TRYLOCK, &mtx);
	long v = mtx;
	C1(MTX_UNLOCK, &mtx);
	ps("trylock");
	kv("pris", a);
	kv("libre", b);
	kv("mot", v);
	kv("apres", (long)mtx);
	nl();
	nstack = 0;
	exited = 0;
	counter = 0;
	long ok = 0;
	for (int i = 0; i < NTH; i++)
		ok += start_thread(th_inc, i) > 0;
	wait_exited(NTH);
	ps("mutex");
	kv("threads", ok);
	kv("compte", counter);
	kv("attendu", (long)NTH * NINC);
	kv("mot", (long)mtx);
	nl();
	ck(counter == (long)NTH * NINC, "mutex : compte");
	/* condition : ping-pong */
	flag = 0;
	start_thread(th_pong, 0);
	sys1(SYS_SLEEP, 5);
	C1(MTX_LOCK, &mtx);
	flag = 1;
	C1(CND_SIGNAL, &cnd);
	long tours = 0;
	while (flag != 2 && tours < 100000) {
		C2(CND_WAIT, &cnd, &mtx);
		tours++;
	}
	C1(MTX_UNLOCK, &mtx);
	wait_exited(NTH + 1);
	ps("cnd_wait ping-pong");
	kv("flag", flag);
	nl();
	/* diffusion */
	done_cnt = 0;
	for (int i = 0; i < 3; i++)
		start_thread(th_bcast, i);
	sys1(SYS_SLEEP, 10);
	C1(MTX_LOCK, &mtx);
	flag = 3;
	C1(CND_BROADCAST, &cnd);
	tours = 0;
	while (done_cnt < 3 && tours < 100000) {
		C3(CND_TIMEDWAIT, &cnd2, &mtx, 50);
		tours++;
	}
	C1(MTX_UNLOCK, &mtx);
	wait_exited(NTH + 4);
	ps("cnd_broadcast");
	kv("reveilles", done_cnt);
	kv("fini", exited);
	nl();
	/* attente échue (sans signal) */
	C1(MTX_LOCK, &mtx);
	long r = 0;
	for (int i = 0; i < 10; i++) {
		r = C3(CND_TIMEDWAIT, &cnd2, &mtx, 20);
		if (r)
			break;
	}
	long held = C1(MTX_TRYLOCK, &mtx);
	C1(MTX_UNLOCK, &mtx);
	ps("cnd_timedwait");
	kv("r", r);
	kv("repris", held);
	nl();
}

/* ---- fork, f_fork_child, f_exec_resume, file_sys sur la sortie */

static volatile long shared_v;

static void t_fork(void)
{
	sec("fork");
	shared_v = 7;
	flush();
	long pid = C0(F_FORK);
	if (pid == 0) {
		long r = shared_v == 7 ? 40 : 1;
		shared_v = 99; /* (le père ne le voit pas) */
		sexit(r + 2);
	}
	long code = sys1(SYS_WAIT, pid);
	ps("f_fork");
	kv("pere", pid > 0);
	kv("code", code);
	kv("v", shared_v);
	nl();
	flush();
	pid = fsys(SYS_FORK, 0, 0, 0);
	if (pid == 0)
		sexit(21);
	code = sys1(SYS_WAIT, pid);
	ps("file_sys fork");
	kv("pere", pid > 0);
	kv("code", code);
	nl();
	flush();
	called[F_FORK_CHILD]++; /* (appelée dans le fils) */
	pid = sys0(SYS_FORK); /* fork sans AppKit, puis f_fork_child dans le fils */
	if (pid == 0)
		sexit(50 + C0(F_FORK_CHILD));
	code = sys1(SYS_WAIT, pid);
	ps("f_fork_child");
	kv("pere", pid > 0);
	kv("code", code);
	nl();
	C0(F_EXEC_REPRISE);
	C0(F_EXEC_REPRISE);
	ps("f_exec_resume : ok\n");
	ps("file_sys sortie :");
	flush();
	long w1 = fsys(SYS_WRITE, 1, L(" stdout"), 7);
	long w2 = fsys(SYS_WRITE, 2, L(" stderr\n"), 8);
	long w3 = fsys(SYS_PRINT, L("print\n"), 6, 0);
	(void)w3;
	ps("ecrits");
	kv("1", w1);
	kv("2", w2);
	kv("seek1", fsys(SYS_SEEK, 1, 0, 0));
	kv("read5", fsys(SYS_READ, 5, 0, 0));
	kv("read70", fsys(SYS_READ, 70, 0, 0));
	kv("read200", fsys(SYS_READ, 200, 0, 0));
	kv("close3", fsys(SYS_CLOSE, 3, 0, 0));
	kv("seek-1", fsys(SYS_SEEK, -1, 0, 0));
	nl();
}

/* =========================================================================
 *  partie du Pi : vfs et services
 * ========================================================================= */

static char big[200000];
static char rbuf[200064] __attribute__((aligned(64)));

static void gen(char *p, long n, u64 seed)
{
	for (long i = 0; i < n; i++) {
		seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
		p[i] = (char)(seed >> 56);
	}
}

static long wr_file(const char *path, const void *p, long n)
{
	long fd = C2(F_OUVRIR, path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long r = C3(F_ECRIRE, fd, p, n);
	C1(F_FERMER, fd);
	return r;
}

/* fichier entier -> octets lus (dans rbuf) */
static long rd_file(const char *path)
{
	long fd = C2(F_OUVRIR, path, O_READ);
	if (fd < 0)
		return fd;
	long n = 0, r = 0;
	while (n < (long)sizeof rbuf && (r = C3(F_LIRE, fd, rbuf + n, (long)sizeof rbuf - n < 4096 ? (long)sizeof rbuf - n : 4096)) > 0)
		n += r;
	C1(F_FERMER, fd);
	return r < 0 ? r : n;
}

static void t_lang(void)
{
	sec("langue");
	ps("lang_get ");
	pz((const char *)C0(LANG_GET));
	nl();
	static const char *const sys_t[] = {"File", "Edit", "texte sans traduction"};
	for (int i = 0; i < 3; i++) {
		ps("lang_tr ");
		pz(sys_t[i]);
		ps(" -> ");
		pz((const char *)C1(LANG_TR, sys_t[i]));
		nl();
	}
	ps("lang_tr 0");
	kv("r", C1(LANG_TR, 0));
	nl();
	static const char dict[] = "# commentaire = rien\n"
				   "bonjour = salut\n"
				   "  espace avant   =   apres  \n"
				   "cle\\=egale = valeur\\=egale\n"
				   "retour = ligne1\\nligne2\n"
				   "crlf = fin\r\n"
				   "=sans cle\n"
				   "sans egal\n"
				   "\n"
				   "bonjour = dernier gagne\n"
				   "anti\\\\slash = a\\\\b\\x\n"
				   "coupe\\\n"
				   "vide =\n"
				   "dernier = sans fin de ligne";
	long w = wr_file("/tmp/aktest/dict.txt", dict, sizeof dict - 1);
	ps("dict");
	kv("ecrits", w);
	kv("lang_load", C1(LANG_LOAD, "/tmp/aktest/dict.txt"));
	kv("absent", C1(LANG_LOAD, "/tmp/aktest/absent.txt"));
	nl();
	static const char *const keys[] = {"bonjour", "  espace avant", "espace avant", "cle=egale", "retour",
					   "crlf", "sans cle", "", "sans egal", "anti\\slash", "coupe",
					   "vide", "dernier", "# commentaire"};
	for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
		ps("lang_tr ");
		pz(keys[i]);
		ps(" -> ");
		pz((const char *)C1(LANG_TR, keys[i]));
		nl();
	}
	ps("lang_puts :");
	flush();
	C1(LANG_PUTS, " bonjour\n");
	C1(LANG_PUTS, "bonjour");
	C1(PUTS, "\n");
}

static void t_files(void)
{
	static char b[512];
	long st[2];
	sec("repertoires");
	kv("mkdir", C1(F_MKDIR, "/tmp/aktest/d"));
	kv("mkdir2", C1(F_MKDIR, "/tmp/aktest/d"));
	kv("getcwd", C2(F_GETCWD, b, 256));
	nl();
	kv("chdir", C1(F_CHDIR, "/tmp/aktest"));
	kv("getcwd", C2(F_GETCWD, b, 256));
	pc(' ');
	pz(b);
	kv("chdir_absent", C1(F_CHDIR, "/tmp/aktest/zz"));
	kv("getcwd100", C2(F_GETCWD, b, 100));
	kv("getcwd0", C2(F_GETCWD, 0, 256));
	nl();
	/* chemins relatifs au dossier courant */
	kv("rel_ecrit", wr_file("d/rel.txt", "relatif", 7));
	kv("rel_stat", C2(F_STAT, "d/rel.txt", st));
	kv("taille", st[0]);
	nl();
	static char longp[400];
	for (int i = 0; i < 300; i++)
		longp[i] = 'a';
	longp[0] = '/';
	kv("long_open", C2(F_OUVRIR, longp, O_READ));
	kv("long_stat", C2(F_STAT, longp, st));
	longp[254] = 0;
	kv("open254", C2(F_OUVRIR, longp, O_READ));
	longp[254] = 'a';
	longp[255] = 0;
	kv("open255", C2(F_OUVRIR, longp, O_READ));
	kv("stat0", C2(F_STAT, "d", 0));
	kv("mkdir_long", C1(F_MKDIR, longp));
	nl();
	for (int i = 0; i < 5; i++) {
		char nm[40];
		scpy(nm, "d/f0.txt");
		nm[3] = (char)('0' + i);
		wr_file(nm, big, 100 * i);
	}
	C1(F_MKDIR, "d/sous");
	long fd = C2(F_OUVRIR, "d", O_READ);
	ps("ouvrir d");
	kv("fd_ok", fd >= 64);
	nl();
	/* entrées : triées par nom (l'ordre du service n'est pas garanti) */
	static char names[16][64];
	static long sizes[16], attrs[16];
	int n = 0;
	long r;
	while (n < 16 && (r = C2(F_LIRE_REP, fd, b)) == 1) {
		scpy(names[n], b);
		sizes[n] = *(long *)(b + 256);
		attrs[n] = *(long *)(b + 264);
		n++;
	}
	kv("readdir_fin", r);
	kv("readdir_encore", C2(F_LIRE_REP, fd, b));
	kv("lire_dir", C3(F_LIRE, fd, b, 10));
	kv("seek_dir", C3(F_POSITION, fd, 0, 0));
	kv("readdir_apres_seek", C2(F_LIRE_REP, fd, b));
	nl();
	for (int i = 0; i < n; i++)
		for (int j = i + 1; j < n; j++) {
			const char *x = names[i], *y = names[j];
			while (*x && *x == *y)
				x++, y++;
			if ((u8)*x > (u8)*y) {
				char t[64];
				scpy(t, names[i]);
				scpy(names[i], names[j]);
				scpy(names[j], t);
				long s = sizes[i];
				sizes[i] = sizes[j];
				sizes[j] = s;
				s = attrs[i];
				attrs[i] = attrs[j];
				attrs[j] = s;
			}
		}
	for (int i = 0; i < n; i++) {
		ps("  ");
		pz(names[i]);
		kv("taille", (attrs[i] & 0x10) ? 0 : sizes[i]);
		kv("dir", (attrs[i] & 0x10) != 0);
		nl();
	}
	kv("fermer", C1(F_FERMER, fd));
	kv("fermer2", C1(F_FERMER, fd));
	kv("readdir_ferme", C2(F_LIRE_REP, fd, b));
	nl();

	sec("fichiers");
	gen(big, sizeof big, 12345);
	static const long wsz[] = {0, 1, 463, 464, 465, 1000, 65535, 65536, 70000, 200000};
	for (unsigned i = 0; i < sizeof wsz / sizeof wsz[0]; i++) {
		long w = wr_file("/tmp/aktest/w.bin", big, wsz[i]);
		long s = C2(F_STAT, "/tmp/aktest/w.bin", st);
		long rr = rd_file("/tmp/aktest/w.bin");
		ps("ecrire");
		kv("n", wsz[i]);
		kv("w", w);
		kv("stat", s);
		kv("taille", st[0]);
		kv("relu", rr);
		kv("egal", rr == wsz[i] && !memcmp(rbuf, big, rr));
		nl();
	}
	/* lectures par morceaux de tailles variées : direct (aligné), tampon, messages */
	fd = C2(F_OUVRIR, "/tmp/aktest/w.bin", O_READ);
	static const long rsz[] = {100, 70000, 1, 464, 465, 65536, 70000, 100000};
	long off = 0;
	for (unsigned i = 0; i < sizeof rsz / sizeof rsz[0]; i++) {
		char *dst = (i & 1) ? rbuf + 1 : rbuf; /* aligné ou non */
		long got = C3(F_LIRE, fd, dst, rsz[i]);
		long pos = C3(F_POSITION, fd, 0, 1);
		ps("lire");
		kv("n", rsz[i]);
		kv("r", got);
		kv("pos", pos);
		kv("egal", got >= 0 && !memcmp(dst, big + off, got));
		nl();
		if (got > 0)
			off += got;
	}
	kv("seek0", C3(F_POSITION, fd, 0, 0));
	kv("seek2", C3(F_POSITION, fd, -10, 2));
	kv("seek1", C3(F_POSITION, fd, 5, 1));
	kv("seekneg", C3(F_POSITION, fd, -1, 0));
	kv("seek3", C3(F_POSITION, fd, 0, 3));
	kv("seekloin", C3(F_POSITION, fd, 1000000, 0));
	kv("lireloin", C3(F_LIRE, fd, rbuf, 10));
	kv("ecrire_ro", C3(F_ECRIRE, fd, "x", 1));
	nl();
	kv("pread", C4(F_LIRE_A, fd, rbuf, 8, 100));
	kh("h", fnv(rbuf, 8));
	kv("egal", !memcmp(rbuf, big + 100, 8));
	kv("pos", C3(F_POSITION, fd, 0, 1));
	kv("pread-1", C4(F_LIRE_A, fd, rbuf, 8, -1));
	kv("pread_fin", C4(F_LIRE_A, fd, rbuf, 8, 199996));
	kv("pwrite_ro", C4(F_ECRIRE_A, fd, "x", 1, 0));
	nl();
	C1(F_FERMER, fd);
	fd = C2(F_OUVRIR, "/tmp/aktest/w.bin", O_READ | O_WRITE);
	kv("rw", fd >= 64);
	kv("pwrite", C4(F_ECRIRE_A, fd, "ABCD", 4, 10));
	kv("pos", C3(F_POSITION, fd, 0, 1));
	kv("ecrire", C3(F_ECRIRE, fd, "zz", 2));
	kv("pos", C3(F_POSITION, fd, 0, 1));
	kv("pread", C4(F_LIRE_A, fd, rbuf, 16, 0));
	nl();
	pq(rbuf + 8, 8);
	kh("h", fnv(rbuf, 16));
	kv("pwrite-1", C4(F_ECRIRE_A, fd, "x", 1, -1));
	kv("pwrite_loin", C4(F_ECRIRE_A, fd, "fin", 3, 200010));
	kv("stat", C2(F_STAT, "/tmp/aktest/w.bin", st));
	kv("taille", st[0]);
	nl();
	C1(F_FERMER, fd);
	fd = C2(F_OUVRIR, "/tmp/aktest/w.bin", O_WRITE | O_APPEND);
	kv("append_pos", C3(F_POSITION, fd, 0, 1));
	kv("append", C3(F_ECRIRE, fd, "<fin>", 5));
	kv("lire_wo", C3(F_LIRE, fd, rbuf, 5));
	C1(F_FERMER, fd);
	kv("taille", (C2(F_STAT, "/tmp/aktest/w.bin", st), st[0]));
	nl();
	/* erreurs */
	kv("absent", C2(F_OUVRIR, "/tmp/aktest/absent", O_READ));
	kv("stat_absent", C2(F_STAT, "/tmp/aktest/absent", st));
	kv("lire3", C3(F_LIRE, 3, rbuf, 1));
	kv("lire63", C3(F_LIRE, 63, rbuf, 1));
	kv("lire127", C3(F_LIRE, 127, rbuf, 1));
	kv("lire128", C3(F_LIRE, 128, rbuf, 1));
	kv("fermer100", C1(F_FERMER, 100));
	kv("seek100", C3(F_POSITION, 100, 0, 0));
	kv("readdir100", C2(F_LIRE_REP, 100, b));
	nl();
	fd = C2(F_OUVRIR, "/tmp/aktest/d", O_READ);
	kv("lire_dir", C3(F_LIRE, fd, rbuf, 1));
	C1(F_FERMER, fd);
	fd = C2(F_OUVRIR, "/tmp/aktest/w.bin", O_READ);
	kv("readdir_fichier", C2(F_LIRE_REP, fd, b));
	C1(F_FERMER, fd);
	kv("unlink", C1(F_UNLINK, "/tmp/aktest/w.bin"));
	kv("unlink2", C1(F_UNLINK, "/tmp/aktest/w.bin"));
	kv("rmdir_plein", C1(F_UNLINK, "/tmp/aktest/d"));
	nl();
	/* table pleine : 64 descripteurs au plus */
	static long fds[70];
	int k;
	for (k = 0; k < 70; k++) {
		fds[k] = C2(F_OUVRIR, "/tmp/aktest/dict.txt", O_READ);
		if (fds[k] < 0)
			break;
	}
	ps("table");
	kv("ouverts", k);
	kv("erreur", k < 70 ? fds[k] : 0);
	for (int i = 0; i < k; i++)
		C1(F_FERMER, fds[i]);
	long ro = C2(F_OUVRIR, "/tmp/aktest/dict.txt", O_READ);
	kv("rouvert", ro >= 64);
	C1(F_FERMER, ro);
	nl();
}

static void t_pipes(void)
{
	static char b[2048];
	long fds[2] = {0, 0};
	sec("tubes");
	long r = C1(F_PIPE, fds);
	ps("f_pipe");
	kv("r", r);
	kv("fds_ok", fds[0] >= 64 && fds[1] >= 64 && fds[0] != fds[1]);
	kv("ecrire", C3(F_ECRIRE, fds[1], "bonjour", 7));
	kv("lire", C3(F_LIRE, fds[0], b, 100));
	pc(' ');
	pq(b, 7);
	nl();
	gen(big, 1500, 777);
	kv("ecrire1500", C3(F_ECRIRE, fds[1], big, 1500));
	long tot = 0, n = 0;
	while (tot < 1500 && n < 50) {
		r = C3(F_LIRE, fds[0], b + tot, 2048 - tot);
		if (r <= 0)
			break;
		ps(" lu=");
		pd(r);
		tot += r;
		n++;
	}
	kv("egal", tot == 1500 && !memcmp(b, big, 1500));
	nl();
	kv("seek", C3(F_POSITION, fds[0], 0, 0));
	kv("pread", C4(F_LIRE_A, fds[0], b, 1, 0));
	kv("pwrite", C4(F_ECRIRE_A, fds[1], b, 1, 0));
	kv("lire_bout_ecriture", C3(F_LIRE, fds[1], b, 1));
	kv("ecrire_bout_lecture", C3(F_ECRIRE, fds[0], b, 1));
	kv("lire0", C3(F_LIRE, fds[0], b, 0));
	kv("fermer_ecriture", C1(F_FERMER, fds[1]));
	kv("lire_fin", C3(F_LIRE, fds[0], b, 10));
	kv("fermer_lecture", C1(F_FERMER, fds[0]));
	nl();
	/* lecteur parti : l'écriture échoue */
	C1(F_PIPE, fds);
	C1(F_FERMER, fds[0]);
	kv("ecrire_sans_lecteur", C3(F_ECRIRE, fds[1], "x", 1));
	C1(F_FERMER, fds[1]);
	nl();
}

/* lit un tube jusqu'à la fin (E_AGAIN : on attend) -> octets dans b */
static long drain(long fd, char *b, long max)
{
	long n = 0;
	for (int i = 0; i < 4000 && n < max; i++) {
		long r = C3(F_LIRE, fd, b + n, max - n);
		if (r == E_AGAIN) {
			sys1(SYS_SLEEP, 5);
			continue;
		}
		if (r <= 0)
			break;
		n += r;
	}
	return n;
}

static void show_out(const char *what, long pid, long fd)
{
	static char b[4096];
	long n = fd >= 0 ? drain(fd, b, sizeof b) : 0;
	long code = pid > 0 ? sys1(SYS_WAIT, pid) : 0;
	ps(what);
	kv("pid_ok", pid > 0);
	if (pid <= 0)
		kv("err", pid);
	kv("code", code);
	pc(' ');
	pq(b, n);
	nl();
}

/* une requête V_EXEC à vfs : la réponse dans m (a canal, b numéro, c
 * attributs, d taille ; données : nom, chemin résolu) */
static long vexec(long *m, const char *path)
{
	memset(m, 0, 512);
	m[0] = V_EXEC;
	long n = slen(path) + 1;
	memcpy((char *)m + 48, path, n);
	*(u32 *)((char *)m + 40) = (u32)n;
	long ch = sys2(SYS_SVC_OPEN, "vfs", 0);
	if (ch < 0)
		return ch;
	return sys2(SYS_SVC_CALL, ch, m);
}

/* V_CHMOD (vfs) : le fichier entre dans l'index des exécutables (mode 0755) */
static long vchmod(const char *path)
{
	static long m[64];
	memset(m, 0, sizeof m);
	m[0] = 47;
	m[1] = 0755;
	long n = slen(path) + 1;
	memcpy((char *)m + 48, path, n);
	*(u32 *)((char *)m + 40) = (u32)n;
	long ch = sys2(SYS_SVC_OPEN, "vfs", 0);
	if (ch < 0)
		return ch;
	return sys2(SYS_SVC_CALL, ch, m);
}

static void t_spawn(void)
{
	static long m[64];
	long fd = -5, fds[2], pin[2], pout[2];
	sec("spawn");
	flush();
	long pid = C3(F_SPAWN_PIPE, "/bin/echo", "un deux  trois", &fd);
	show_out("spawn_pipe echo", pid, fd);
	C1(F_FERMER, fd);
	fd = -5;
	pid = C3(F_SPAWN_PIPE, "/bin/echo", 0, &fd);
	show_out("spawn_pipe echo sans args", pid, fd);
	C1(F_FERMER, fd);
	fd = -5;
	pid = C3(F_SPAWN_PIPE, "/tmp/aktest/absent", "x", &fd);
	ps("spawn_pipe absent");
	kv("r", pid);
	kv("fd", fd);
	kv("fault", C3(F_SPAWN_PIPE, "/bin/echo", "x", 0));
	nl();
	pid = C2(F_SPAWN, "/bin/true", 0);
	kv("spawn_true", pid > 0);
	kv("code", sys1(SYS_WAIT, pid));
	pid = C2(F_SPAWN, "/bin/false", 0);
	kv("spawn_false", pid > 0);
	kv("code", sys1(SYS_WAIT, pid));
	kv("spawn_absent", C2(F_SPAWN, "/bin/absent_ak", 0));
	nl();
	/* spawn_stdio : entrée et sortie par des tubes */
	C1(F_PIPE, pin);
	C1(F_PIPE, pout);
	long sfd[3] = {pin[0], pout[1], pout[1]};
	pid = C3(F_SPAWN_STDIO, "/bin/cat", 0, sfd);
	C1(F_FERMER, pin[0]);
	C1(F_FERMER, pout[1]);
	C3(F_ECRIRE, pin[1], "abc\ndef\n", 8);
	C1(F_FERMER, pin[1]);
	show_out("spawn_stdio cat", pid, pout[0]);
	C1(F_FERMER, pout[0]);
	/* redirection vers un fichier (f_redir) */
	wr_file("/tmp/aktest/redir.txt", "", 0);
	long rfd = C2(F_REDIR, "/tmp/aktest/redir.txt", O_WRITE);
	ps("f_redir");
	kv("ok", rfd >= 64);
	nl();
	long rf[3] = {0, rfd, rfd};
	pid = C3(F_SPAWN_STDIO, "/bin/echo", "vers un fichier", rf);
	kv("echo_redir", pid > 0);
	kv("code", sys1(SYS_WAIT, pid));
	C1(F_FERMER, rfd);
	pid = C3(F_SPAWN_STDIO, "/bin/echo", "deux fois", rf);
	kv("redir_ferme", pid);
	long n = rd_file("/tmp/aktest/redir.txt");
	kv("lus", n);
	pc(' ');
	pq(rbuf, n > 0 ? n : 0);
	nl();
	long ra = C2(F_REDIR, "/tmp/aktest/zz/x.txt", O_READ);
	kv("redir_absent", ra < 0 ? ra : 64);
	if (ra >= 0)
		C1(F_FERMER, ra);
	long bad[3] = {0, 120, 0};
	kv("stdio_badf", C3(F_SPAWN_STDIO, "/bin/true", 0, bad));
	long fdir = C2(F_OUVRIR, "/tmp/aktest/d", O_READ);
	long bd2[3] = {0, fdir, 0};
	long sd = C3(F_SPAWN_STDIO, "/bin/true", 0, bd2);
	kv("stdio_dir", sd > 0 ? 1 : sd);
	if (sd > 0)
		sys1(SYS_WAIT, sd);
	C1(F_FERMER, fdir);
	nl();
	/* stdio : -1 (console), 0-2 (ceux du père), 0 pour tous */
	flush();
	long con[3] = {-1, 1, 2};
	pid = C3(F_SPAWN_STDIO, "/bin/echo", "[echo sur la sortie du pere]", con);
	kv("stdio_pere", pid > 0);
	kv("code", sys1(SYS_WAIT, pid));
	nl();
	flush();
	pid = C3(F_SPAWN_STDIO, "/bin/echo", "[echo par spawn_stdio(0)]", 0);
	kv("stdio_0", pid > 0);
	kv("code", sys1(SYS_WAIT, pid));
	nl();
	/* argv et environnement */
	C1(F_PIPE, fds);
	long ef[3] = {0, fds[1], fds[1]};
	static char *const av[] = {"aktest", "-a", "deux mots", "x\"y", "", "fin\\", 0};
	static char *const ev[] = {"AK=1", "B=deux mots", 0};
	pid = C4(F_SPAWN_ENV, "/bin/aktest", av, ev, ef);
	C1(F_FERMER, fds[1]);
	show_out("spawn_env aktest -a", pid, fds[0]);
	C1(F_FERMER, fds[0]);
	C1(F_PIPE, fds);
	ef[1] = ef[2] = fds[1];
	static char *const av2[] = {"echo", "a b", "c", 0};
	pid = C4(F_SPAWN_ENV, "/bin/echo", av2, 0, ef);
	C1(F_FERMER, fds[1]);
	show_out("spawn_env echo", pid, fds[0]);
	C1(F_FERMER, fds[0]);
	kv("spawn_env_absent", C4(F_SPAWN_ENV, "/bin/absent_ak", av2, 0, 0));
	nl();
	/* un script (#!) : son interprète */
	static const char script[] = "#!/bin/echo coucou\nrien\n";
	wr_file("/tmp/aktest/s.sh", script, sizeof script - 1);
	ps("chmod s.sh");
	kv("r", vchmod("/tmp/aktest/s.sh"));
	nl();
	C1(F_PIPE, fds);
	ef[1] = ef[2] = fds[1];
	static char *const av3[] = {"s.sh", "arg1", 0};
	pid = C4(F_SPAWN_ENV, "/tmp/aktest/s.sh", av3, 0, ef);
	C1(F_FERMER, fds[1]);
	show_out("spawn_env script", pid, fds[0]);
	C1(F_FERMER, fds[0]);
	fd = -5;
	pid = C3(F_SPAWN_PIPE, "/tmp/aktest/s.sh", "l1 l2", &fd);
	show_out("spawn_pipe script", pid, fd);
	C1(F_FERMER, fd);
	/* f_spawn_file : la réponse de V_EXEC faite ici */
	long r = vexec(m, "/bin/echo");
	ps("vexec");
	kv("r", r);
	kv("taille_ok", m[4] > 64);
	nl();
	flush();
	if (r >= 0) {
		pid = C3(F_SPAWN_FILE, m, "[echo par spawn_file]", 0);
		kv("spawn_file", pid > 0);
		kv("code", sys1(SYS_WAIT, pid));
		nl();
	}
	/* file_sys : spawn, spawn_pipe, preload, unload */
	flush();
	pid = fsys(SYS_SPAWN, L("/bin/true"), 0, 0);
	kv("sys_spawn", pid > 0);
	kv("code", sys1(SYS_WAIT, pid));
	fd = -5;
	pid = fsys(SYS_SPAWN_PIPE, L("/bin/echo"), L("par sys"), L(&fd));
	nl();
	show_out("sys_spawn_pipe", pid, fd);
	C1(F_FERMER, fd);
	long q = fsys(SYS_PRELOAD, 0, 0, 0);
	kv("preload?", q);
	kv("preload", fsys(SYS_PRELOAD, L("/bin/true"), 0, 0));
	kv("unload", fsys(SYS_UNLOAD, L("/bin/true"), 0, 0));
	kv("preload_absent", fsys(SYS_PRELOAD, L("/bin/absent_ak"), 0, 0));
	kv("unload_absent", fsys(SYS_UNLOAD, L("/bin/absent_ak"), 0, 0));
	nl();
}

static void t_kits(void)
{
	sec("kits");
	static const char *const ks[] = {"", "Abc", "a-b", "abcdefghijklmnopqrstuvw", "abcdefghijklmnopqrstuvwx",
					 "zzz_absent_9", "documentkit"};
	for (unsigned i = 0; i < sizeof ks / sizeof ks[0]; i++) {
		ps("f_kit_load ");
		pz(ks[i]);
		kv("r", C1(F_KIT_LOAD, ks[i]));
		nl();
	}
	ps("documentkit ouvert");
	kv("ok", sys2(SYS_LIB_OPEN, "documentkit", 1) >= 0);
	nl();
}

static void t_fork_files(void)
{
	static char b[16];
	sec("fork et fichiers");
	wr_file("/tmp/aktest/pos.txt", "0123456789abcdef", 16);
	long fd = C2(F_OUVRIR, "/tmp/aktest/pos.txt", O_READ);
	C3(F_POSITION, fd, 2, 0);
	flush();
	long pid = C0(F_FORK);
	if (pid == 0) {
		long r = C3(F_LIRE, fd, b, 3);
		sexit(r == 3 && b[0] == '2' && b[2] == '4' ? 33 : 34);
	}
	long code = sys1(SYS_WAIT, pid);
	long r = C3(F_LIRE, fd, b, 3);
	ps("position partagee");
	kv("code", code);
	kv("r", r);
	pc(' ');
	pq(b, r > 0 ? r : 0);
	kv("pos", C3(F_POSITION, fd, 0, 1));
	kv("seek", C3(F_POSITION, fd, 1, 1));
	kv("pread", C4(F_LIRE_A, fd, b, 2, 0));
	pc(' ');
	pq(b, 2);
	nl();
	C1(F_FERMER, fd);
}

static void t_exec(void)
{
	static long m[64];
	static long P[9];
	long fds[2];
	sec("exec");
	kv("exec_absent", C4(F_EXEC, "/bin/absent_ak", 0, 0, 0));
	long bad[3] = {0, 120, 0};
	kv("exec_badf", C4(F_EXEC, "/bin/true", 0, 0, bad));
	nl();
	/* un descripteur ouvert passe au programme suivant */
	wr_file("/tmp/aktest/x.txt", "contenu herite par exec", 23);
	long fd = C2(F_OUVRIR, "/tmp/aktest/x.txt", O_READ);
	C3(F_POSITION, fd, 8, 0);
	C1(F_PIPE, fds);
	char arg[24];
	scpy(arg, "-x ");
	long e = C2(FMT_DEC, fd, arg + 3);
	(void)e;
	static char *av[] = {"aktest", "-x", 0, 0};
	av[2] = arg + 3;
	static char *const ev[] = {"EXEC=1", 0};
	flush();
	long pid = C0(F_FORK);
	if (pid == 0) {
		long sf[3] = {0, fds[1], fds[1]};
		long r = C4(F_EXEC, "/bin/aktest", av, ev, sf);
		sexit(100 - r);
	}
	C1(F_FERMER, fds[1]);
	show_out("f_exec aktest -x", pid, fds[0]);
	C1(F_FERMER, fds[0]);
	kv("pos_pere", C3(F_POSITION, fd, 0, 1));
	C1(F_FERMER, fd);
	nl();
	/* un script */
	C1(F_PIPE, fds);
	flush();
	pid = C0(F_FORK);
	if (pid == 0) {
		static char *const av3[] = {"s.sh", "e1", 0};
		long sf[3] = {0, fds[1], fds[1]};
		long r = C4(F_EXEC, "/tmp/aktest/s.sh", av3, 0, sf);
		sexit(100 - r);
	}
	C1(F_FERMER, fds[1]);
	show_out("f_exec script", pid, fds[0]);
	C1(F_FERMER, fds[0]);
	/* pas un ELF (dans un fils : s'il passait, il n'y reviendrait pas) */
	static char txt[200];
	for (int i = 0; i < 199; i++)
		txt[i] = (char)('a' + i % 26);
	wr_file("/tmp/aktest/pas_elf", txt, 199);
	vchmod("/tmp/aktest/pas_elf");
	flush();
	pid = C0(F_FORK);
	if (pid == 0)
		sexit(100 - C4(F_EXEC, "/tmp/aktest/pas_elf", 0, 0, 0));
	kv("exec_pas_elf", sys1(SYS_WAIT, pid));
	wr_file("/tmp/aktest/mz", "MZ", 2);
	vchmod("/tmp/aktest/mz");
	flush();
	pid = C0(F_FORK);
	if (pid == 0)
		sexit(100 - C4(F_EXEC, "/tmp/aktest/mz", 0, 0, 0));
	kv("exec_court", sys1(SYS_WAIT, pid));
	nl();
	/* f_exec_file : /sys (E_PERM), puis /bin/echo dans un fils */
	long r = vexec(m, "/sys/svc/vfs");
	kv("vexec_sys", r);
	if (r >= 0) {
		memset(P, 0, sizeof P);
		kv("exec_file_sys", C2(F_EXEC_FILE, m, P));
		kv("params_intacts", !P[0] && !P[6] && !P[7] && !P[8]);
	}
	nl();
	flush();
	pid = C0(F_FORK);
	if (pid == 0) {
		r = vexec(m, "/bin/echo");
		if (r < 0)
			sexit(99);
		memset(P, 0, sizeof P);
		P[1] = L("[echo par exec_file]");
		sexit(100 - C2(F_EXEC_FILE, m, P));
	}
	kv("exec_file_echo", sys1(SYS_WAIT, pid));
	/* (V_UNLINK les retire aussi de l'index des exécutables) */
	kv("unlink", C1(F_UNLINK, "/tmp/aktest/s.sh") | C1(F_UNLINK, "/tmp/aktest/pas_elf") |
			     C1(F_UNLINK, "/tmp/aktest/mz"));
	nl();
}

static void t_blob(void)
{
	sec("f_start_blob");
	long b = C0(F_START_BLOB);
	ps("blob");
	kv("present", b != 0);
	if (b) {
		const u32 *h = (const u32 *)b;
		kv("argc", h[1]);
		kv("env", h[2] > 0);
		const char *s = (const char *)b + 16;
		for (u32 i = 0; i < h[1]; i++) {
			pc(' ');
			pz(s);
			s += slen(s) + 1;
		}
	}
	kv("encore", C0(F_START_BLOB) != 0);
	nl();
}

#if defined(__aarch64__)
static void t_regs(void)
{
	sec("registres (AArch64)");
	ps("file_sys");
	kv("appels", regcalls);
	ps(" ecarts=");
	px(regbad, 8);
	nl();
	ck(!regbad, "registres de file_sys");
	regbad = 0;
	static char b[300];
	long in[9] = {L(b), 256, 3, 4, 5, 6, 7, 8, SYS_GETCWD};
	rcall(fn(FICHIER_SYS), in, GARDE_SYS);
	long in2[9] = {L("/tmp/aktest/absent"), 1, 3, 4, 5, 6, 7, 8, SYS_OPEN};
	rcall(fn(FICHIER_SYS), in2, GARDE_SYS);
	long in3[9] = {L("/tmp/aktest/x.txt"), L(b), 3, 4, 5, 6, 7, 8, SYS_STAT};
	rcall(fn(FICHIER_SYS), in3, GARDE_SYS);
	/* f_exec_resume garde tout (x0 et x16 compris) */
	long in4[9] = {0x1234, 1, 2, 3, 4, 5, 6, 7, 8};
	long r = rcall(fn(F_EXEC_REPRISE), in4, 0x8007FFFEULL);
	ps("f_exec_resume");
	kv("x0", r);
	/* ce que l'asm ne touchait pas (des appelants en dépendent) */
	long in5[9] = {12345, L(b), 2, 3, 4, 5, 6, 7, 8};
	rcall(fn(FMT_DEC), in5, 0x2FF80ULL); /* x7-x15, x17 */
	long in6[9] = {L("123"), 1, 2, 3, 4, 5, 6, 7, 8};
	rcall(fn(PARSE_DEC), in6, 0x2FFF0ULL); /* x4-x15, x17 */
	static u32 mx;
	long in7[9] = {L(&mx), 1, 2, 3, 4, 5, 6, 7, 8};
	rcall(fn(MTX_UNLOCK), in7, 0x2E5FEULL); /* x1-x8, x10, x13-x15, x17 */
	long in8[9] = {L("abcdef"), 1, 2, 3, 4, 5, 6, 7, 8};
	rcall(fn(STR_LEN), in8, 0x2FFF8ULL); /* x3-x15, x17 */
	ps(" ecarts=");
	px(regbad, 8);
	nl();
	ck(!regbad, "registres gardés");
}
#endif

/* ---- modes internes */

static void mode_a(const char *line)
{
	ps("[aktest -a] ligne=");
	pz(line);
	long b = C0(F_START_BLOB);
	kv("blob", b != 0);
	if (b) {
		const u32 *h = (const u32 *)b;
		kv("argc", h[1]);
		kv("envc", h[2]);
		const char *s = (const char *)b + 16;
		for (u32 i = 0; i < h[1] + h[2]; i++) {
			pc(' ');
			pz(s);
			s += slen(s) + 1;
		}
	}
	nl();
	sexit(3);
}

static void mode_x(const char *line)
{
	static char b[64];
	long fd = (long)C1(PARSE_DEC, line + 3);
	long r = C3(F_LIRE, fd, b, 20);
	ps("[aktest -x] fd_ok=");
	pd(fd >= 64);
	kv("r", r);
	pc(' ');
	pq(b, r > 0 ? r : 0);
	kv("pos", C3(F_POSITION, fd, 0, 1));
	long bl = C0(F_START_BLOB);
	kv("blob", bl != 0);
	if (bl) {
		const u32 *h = (const u32 *)bl;
		kv("argc", h[1]);
		kv("envc", h[2]);
	}
	nl();
	C3(F_POSITION, fd, 4, 0);
	sexit(7);
}

static int has_flag(const char *line, char f)
{
	if (!line)
		return 0;
	for (long i = 0; line[i]; i++)
		if (line[i] == '-' && line[i + 1] == f && (line[i + 2] == 0 || line[i + 2] == ' '))
			return 1;
	return 0;
}

static void run(const char *line)
{
	ak = ak_open();
	if (ak < 0) {
		ak = 0;
		ps("appkit absent : ");
		pd(ak);
		nl();
		sexit(127);
	}
	const u32 *t = (const u32 *)ak;
	if (has_flag(line, 'a'))
		mode_a(line);
	if (has_flag(line, 'x'))
		mode_x(line);
	ps("aktest : AppKit\n");
	ps("table magic=");
	px(t[0], 8);
	ps(" count=");
	pd(t[1]);
	nl();
	if (t[0] != KIT_MAGIC || t[1] < AK_N) {
		ps("table invalide\n");
		sexit(126);
	}
	t_strings();
	t_numbers();
	t_utf8();
	t_paths();
	t_malloc();
	t_sync();
	t_fork();
	int files = has_flag(line, 'f');
	if (files) {
		ps("=== Pi (fichiers, services, registres)\n");
		C1(F_MKDIR, "/tmp"); /* (absent sur certaines cartes ; existe sinon) */
		ps("mkdir /tmp/aktest");
		kv("r", C1(F_MKDIR, "/tmp/aktest"));
		nl();
		t_blob();
		t_lang();
		t_files();
		t_pipes();
		t_spawn();
		t_kits();
		t_fork_files();
		t_exec();
#if defined(__aarch64__)
		t_regs();
#endif
		C1(F_CHDIR, "/tmp");
		for (int i = 0; i < AK_N; i++)
			if (!called[i]) {
				ps("jamais appelee : ");
				ps(fn_names[i]);
				nl();
			}
	}
	ps("fin : ");
	pd(checks);
	ps(" controles, ");
	pd(fails);
	ps(" echecs\n");
	sexit(fails > 125 ? 125 : fails);
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
