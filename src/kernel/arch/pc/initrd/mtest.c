/* mtest : essais des kits et des images préchargées du noyau x86-64. Sans
 * bibliothèque ; à lancer par « urun mtest » (il fait un fork). */
#include <stdint.h>

#include <korin/syscall_nr.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;

static inline i64 sc(u64 n, u64 a, u64 b, u64 c)
{
	i64 ret;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
	return ret;
}
#define S0(n) sc(n, 0, 0, 0)
#define S1(n, a) sc(n, (u64)(a), 0, 0)
#define S2(n, a, b) sc(n, (u64)(a), (u64)(b), 0)
#define S3(n, a, b, c) sc(n, (u64)(a), (u64)(b), (u64)(c))

static void put(const char *s)
{
	u64 n = 0;
	while (s[n])
		n++;
	S2(SYS_PRINT, s, n);
}

static void putn(i64 v)
{
	char b[24];
	int i = 23, neg = v < 0;
	u64 u = neg ? (u64)-v : (u64)v;
	b[i] = 0;
	do
		b[--i] = (char)('0' + u % 10);
	while (u /= 10);
	if (neg)
		b[--i] = '-';
	put(&b[i]);
}

static int fails, passes;

static void check(const char *what, int ok, i64 got)
{
	if (ok) {
		passes++;
		return;
	}
	fails++;
	put("mtest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

/* un exécutable de 3 pages : en-têtes + code (lecture, exécution : pages
 * partagées), une page de données (copie sur écriture), un .bss paresseux */
static uint8_t elf[12288] __attribute__((aligned(4096)));

static void w16(u64 o, u32 v) { elf[o] = (uint8_t)v, elf[o + 1] = (uint8_t)(v >> 8); }
static void w32(u64 o, u32 v) { w16(o, v), w16(o + 2, v >> 16); }
static void w64(u64 o, u64 v) { w32(o, (u32)v), w32(o + 4, (u32)(v >> 32)); }

static void phdr(int i, u32 flags, u64 off, u64 va, u64 fsz, u64 msz)
{
	u64 p = 64 + (u64)i * 56;
	w32(p, 1), w32(p + 4, flags), w64(p + 8, off), w64(p + 16, va), w64(p + 24, va);
	w64(p + 32, fsz), w64(p + 40, msz), w64(p + 48, 4096);
}

static void build_elf(void)
{
	elf[0] = 0x7f, elf[1] = 'E', elf[2] = 'L', elf[3] = 'F', elf[4] = 2, elf[5] = 1, elf[6] = 1;
	w16(16, 2), w16(18, 0x3E), w32(20, 1), w64(24, 0x401000), w64(32, 64);
	w16(52, 64), w16(54, 56), w16(56, 3);
	phdr(0, 5, 0, 0x400000, 8192, 8192);         /* en-têtes + code */
	phdr(1, 6, 8192, 0x602000, 4096, 4096);      /* données */
	phdr(2, 6, 0, 0x700000, 0, 8192);            /* .bss seul : paresseux */
	/* code : rax = [0x602000] + [0x700000] (0) ; exit(rax) */
	static const uint8_t code[] = {
		0x48, 0x8B, 0x04, 0x25, 0x00, 0x20, 0x60, 0x00,  /* mov rax, [0x602000] */
		0x48, 0x03, 0x04, 0x25, 0x00, 0x00, 0x70, 0x00,  /* add rax, [0x700000] */
		0x48, 0x89, 0xC7,                                /* mov rdi, rax */
		0xB8, 0x10, 0x00, 0x00, 0x00,                    /* mov eax, SYS_EXIT */
		0x0F, 0x05,                                      /* syscall */
	};
	for (u64 i = 0; i < sizeof code; i++)
		elf[4096 + i] = code[i];
	w64(8192, 42); /* la donnée */
}

#define PATH "/bin/essai"

__attribute__((force_align_arg_pointer, noreturn)) void _start(void)
{
	put("mtest : essais des kits et des images\n");

	/* kit de l'initrd */
	i64 t = S2(SYS_LIB_OPEN, "testkit", 1);
	check("lib_open", t >= 0x5000000000LL && t < 0x5400000000LL, t);
	const u32 *tab = (const u32 *)t;
	check("table : AKIT, 2 fonctions", tab[0] == 0x54494B41 && tab[1] == 2, (i64)tab[1]);
	const u32 *offs = (const u32 *)(t + 8);
	u64 (*add2)(u64, u64) = (u64 (*)(u64, u64))(t + offs[0]);
	u64 (*bump)(void) = (u64 (*)(void))(t + offs[1]);
	check("fonction du kit", add2(40, 2) == 42, (i64)add2(40, 2));
	check("variable du kit", bump() == 101 && bump() == 102, 0);
	check("lib_open de nouveau : même table", S2(SYS_LIB_OPEN, "testkit", 0) == t, 0);
	check("version trop ancienne", S2(SYS_LIB_OPEN, "testkit", 9) == -22, 0);
	check("version suffisante", S2(SYS_LIB_OPEN, "testkit", 2) == t, 0);
	check("kit absent", S2(SYS_LIB_OPEN, "absent", 0) == -2, 0);
	i64 pid = S0(SYS_FORK);
	if (pid == 0)
		S1(SYS_EXIT, (i64)bump()); /* copie du fils : 103 */
	check("variables par process (fork)", S3(SYS_WAIT, pid, 0, 0) == 103 && bump() == 103, 0);

	/* image : fenêtre, lecture, scellement */
	build_elf();
	i64 win = S3(SYS_IMAGE_CREATE, PATH, sizeof elf, 1); /* épinglée : gardée après ses process */
	check("image_create : fenêtre", win >= 0x6C00000000LL, win);
	uint8_t *w = (uint8_t *)win;
	for (u64 i = 0; i < sizeof elf; i++)
		w[i] = elf[i];
	check("image_seal", S2(SYS_IMAGE_SEAL, win, 0) == 0, 0);
	u64 list[16 * 16];
	i64 n = S2(SYS_IMAGE_LIST, list, 16);
	check("image_list", n == 1 && list[0] == sizeof elf && list[1] == 3, n);
	u64 params[6] = {0};
	i64 p1 = S3(SYS_SPAWN_PRELOADED, "/BIN/essai", sizeof elf, params);
	i64 p2 = S3(SYS_SPAWN_PRELOADED, PATH, sizeof elf, params);
	check("spawn_preloaded (majuscules près), deux fois", p1 > 0 && p2 > 0, p1);
	check("le programme de l'image tourne", S3(SYS_WAIT, p1, 0, 0) == 42 && S3(SYS_WAIT, p2, 0, 0) == 42, 0);
	check("autre taille : E_NOENT", S3(SYS_SPAWN_PRELOADED, PATH, 100, params) == -2, 0);

	/* exec par chemin */
	pid = S0(SYS_FORK);
	if (pid == 0) {
		u64 ep[9] = {0};
		S3(SYS_EXEC, PATH, sizeof elf, ep);
		S1(SYS_EXIT, 1);
	}
	check("exec par chemin", S3(SYS_WAIT, pid, 0, 0) == 42, 0);

	check("image_forget", S1(SYS_IMAGE_FORGET, PATH) == 1, 0);
	check("oubliée : E_NOENT", S3(SYS_SPAWN_PRELOADED, PATH, sizeof elf, params) == -2, 0);
	check("fenêtre inconnue", S2(SYS_IMAGE_SEAL, 0x6C00000000ULL, 0) == -22, 0);

	put("mtest : ");
	putn(passes);
	put(" ok, ");
	putn(fails);
	put(" échec(s)\n");
	S1(SYS_EXIT, fails);
	for (;;)
		;
}
