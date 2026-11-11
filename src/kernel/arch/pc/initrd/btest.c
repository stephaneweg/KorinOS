/* btest : essais du démarrage (modules, fichiers de démarrage, verrou),
 * de SVC_MAP_CALLER et de PROC_STATS. Sans bibliothèque ; à lancer par
 * « run btest » (droits de SD0:/sys). */
#include <stdint.h>

#include <korin/syscall_nr.h>

typedef uint64_t u64;
typedef int64_t i64;

static inline i64 sc(u64 n, u64 a, u64 b, u64 c, u64 d)
{
	register u64 r10 __asm__("r10") = d;
	i64 ret;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
	return ret;
}
#define S0(n) sc(n, 0, 0, 0, 0)
#define S1(n, a) sc(n, (u64)(a), 0, 0, 0)
#define S2(n, a, b) sc(n, (u64)(a), (u64)(b), 0, 0)
#define S3(n, a, b, c) sc(n, (u64)(a), (u64)(b), (u64)(c), 0)
#define S4(n, a, b, c, d) sc(n, (u64)(a), (u64)(b), (u64)(c), (u64)(d))

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
	put("btest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

/* service : lit 8 octets de la mémoire de l'appelant, y écrit leur double */
static volatile i64 chan = -1;

static void server(u64 arg)
{
	(void)arg;
	static u64 msg[64];
	chan = S2(SYS_SVC_REGISTER, "btest", 0);
	for (;;) {
		i64 n = S3(SYS_SVC_NEXT, chan, msg, 1000);
		if (n <= 0 || msg[0] >= 0xFFF0)
			continue;
		if (msg[0] == 99) {
			msg[0] = 0;
			S2(SYS_SVC_REPLY, n, msg);
			S1(SYS_THREAD_EXIT, 0);
		}
		i64 a = S4(SYS_SVC_MAP_CALLER, n, msg[1], 8, 1);
		u64 ph[2] = {0};
		i64 np = a > 0 ? S3(SYS_SVC_CALLER_PHYS, a, ph, 2) : 0;
		if (a > 0)
			*(volatile u64 *)a *= 2;
		msg[0] = a > 0 && np == 1 && ph[0] ? 1 : (u64)a;
		S2(SYS_SVC_REPLY, n, msg); /* rend la vue */
	}
}

static u64 stack[2048] __attribute__((aligned(16)));

__attribute__((force_align_arg_pointer, noreturn)) void _start(void)
{
	put("btest : démarrage, mémoire de l'appelant, compteurs\n");

	/* modules de l'initrd */
	char buf[64];
	check("mod_lire", S3(SYS_MOD_LIRE, "testkit.so", buf, 4) == 4 && buf[1] == 'E', 0);
	check("mod_lire absent", S3(SYS_MOD_LIRE, "absent", buf, 4) == -2, 0);
	i64 pid = S2(SYS_MOD_LANCER, "hello", 0);
	check("mod_lancer", pid > 0 && S3(SYS_WAIT, pid, 0, 0) == 0, pid);

	/* fichiers de démarrage */
	i64 n = S3(SYS_BOOT_FILE, 0, buf, sizeof buf);
	check("boot_file : liste", n == 22 && buf[0] == 'S' && buf[3] == ':', n);
	const char opts[] = "# options d'essai\nno_preload\nautre\n";
	check("boot_file : donner", S3(SYS_BOOT_FILE, "SD0:/etc/options.txt", opts, sizeof opts - 1) == 0, 0);
	check("boot_file : nom inattendu", S3(SYS_BOOT_FILE, "SD0:/x", opts, 4) == -22, 0);
	check("demarrage_pret", S0(SYS_DEMARRAGE_PRET) == 0, 0);
	check("option no_preload appliquée", S3(SYS_IMAGE_CREATE, "SD0:/bin/x", 4096, 0) == -38, 0);

	/* SVC_MAP_CALLER */
	S3(SYS_THREAD_CREATE, server, &stack[2048], 0);
	i64 ch = S2(SYS_SVC_OPEN, "btest", 2000);
	static volatile u64 value = 21;
	static u64 msg[64];
	msg[0] = 1;
	msg[1] = (u64)&value;
	i64 r = S2(SYS_SVC_CALL, ch, msg);
	check("svc_map_caller : le service écrit chez l'appelant", r == 1 && value == 42, r);
	msg[0] = 99;
	S2(SYS_SVC_CALL, ch, msg);

	/* PROC_STATS */
	u64 st[0x62];
	i64 w = S3(SYS_PROC_STATS, 0, st, 0x62);
	check("proc_stats", w == 0x62 && st[0] > 10 && st[1 + SYS_SVC_CALL] >= 2, w);

	put("btest : ");
	putn(passes);
	put(" ok, ");
	putn(fails);
	put(" échec(s)\n");
	S1(SYS_EXIT, fails);
	for (;;)
		;
}
