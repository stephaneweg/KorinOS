/* kbtest : BusKit écrit en C (src/kits/buskit/buskit.c), chargé par LIB_OPEN
 * sur le noyau x86-64 ; le test joue lui-même busd (service « bus »).
 * Sans bibliothèque ; à lancer par « run kbtest » (droits : service). */
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
	put("kbtest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

static int eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

/* faux busd : un sujet gardé, « essai » */
static volatile int ready;
static char value[256];
static u64 vlen;

static void fake_busd(u64 arg)
{
	(void)arg;
	static u64 msg[64];
	i64 ch = S2(SYS_SVC_REGISTER, "bus", 0);
	ready = ch >= 0 ? 1 : -1;
	char *data = (char *)msg + 48;
	for (;;) {
		i64 n = S3(SYS_SVC_NEXT, ch, msg, 1000);
		if (n <= 0 || msg[0] >= 0xFFF0)
			continue;
		switch (msg[0]) {
		case 1: { /* PUBLISH : nom, 0, valeur (b octets) */
			u64 l = 0;
			while (data[l])
				l++;
			vlen = msg[2];
			for (u64 i = 0; i < vlen; i++)
				value[i] = data[l + 1 + i];
			msg[0] = 3; /* 3 abonnés prévenus */
			break;
		}
		case 2:
			msg[0] = 7; /* abonnement 7 */
			break;
		case 4: /* GET */
			if (msg[1] == 0 && !eq(data, "essai")) {
				msg[0] = (u64)-2;
				break;
			}
			for (u64 i = 0; i < vlen; i++)
				data[i] = value[i];
			msg[0] = vlen;
			break;
		case 5: /* LIST : un seul sujet, le 1 */
			if (msg[1] == 0) {
				const char *nm = "essai";
				for (int i = 0; i < 6; i++)
					data[i] = nm[i];
				msg[0] = 1;
			} else
				msg[0] = 0;
			break;
		case 99:
			msg[0] = 0;
			S2(SYS_SVC_REPLY, n, msg);
			S1(SYS_THREAD_EXIT, 0);
			break;
		default:
			msg[0] = 0;
		}
		S2(SYS_SVC_REPLY, n, msg);
	}
}

static u64 stack[2048] __attribute__((aligned(16)));

typedef i64 (*f0)(void);
typedef i64 (*f2)(u64, u64);
typedef i64 (*f3)(u64, u64, u64);
typedef i64 (*f4)(u64, u64, u64, u64);

__attribute__((force_align_arg_pointer, noreturn)) void _start(void)
{
	put("kbtest : BusKit en C sur x86-64\n");
	i64 t = S2(SYS_LIB_OPEN, "buskit", 9);
	check("lib_open buskit (9 fonctions)", t >= 0x5000000000LL, t);
	const u32 *tab = (const u32 *)t;
	check("table AKIT", tab[0] == 0x54494B41 && tab[1] == 9, (i64)tab[1]);
#define FN(i) (t + tab[2 + (i)])
	f0 bus_available = (f0)FN(0);
	f4 bus_publish = (f4)FN(1);
	f2 bus_subscribe = (f2)FN(2);
	f3 bus_get = (f3)FN(4);
	f2 bus_list = (f2)FN(7);
	f2 bus_name = (f2)FN(6);

	check("busd absent : bus_available 0", bus_available() == 0, 0);
	char buf[64];
	check("busd absent : BUS_ENODEV", bus_get((u64) "x", (u64)buf, 8) == -19, 0);

	S3(SYS_THREAD_CREATE, fake_busd, &stack[2048], 0);
	while (!ready)
		S0(SYS_YIELD);
	/* le kit a gardé « absent » ? non : il rouvre le canal à chaque appel tant qu'il n'est pas ouvert */
	check("busd là : bus_available 1", bus_available() == 1, 0);
	check("bus_publish (chaîne)", bus_publish((u64) "essai", (u64) "bonjour", (u64)-1, 1) == 3, 0);
	i64 n = bus_get((u64) "essai", (u64)buf, sizeof buf);
	check("bus_get", n == 7 && buf[0] == 'b' && buf[6] == 'r', n);
	check("bus_get coupé à la taille", bus_get((u64) "essai", (u64)buf, 3) == 7, 0);
	check("bus_get inconnu", bus_get((u64) "autre", (u64)buf, 8) == -2, 0);
	check("nom vide refusé", bus_get((u64) "", (u64)buf, 8) == -22, 0);
	check("bus_subscribe", bus_subscribe((u64) "essai.*", 1) == 7, 0);
	check("bus_list", bus_list(0, (u64)buf) == 1 && eq(buf, "essai"), 0);
	check("bus_name", bus_name(1, (u64)buf) == 0 && eq(buf, "essai"), 0);
	check("bus_name inconnu", bus_name(5, (u64)buf) == -2 && buf[0] == 0, 0);

	u64 m[64] = {99};
	S2(SYS_SVC_CALL, S2(SYS_SVC_OPEN, "bus", 0), m);
	put("kbtest : ");
	putn(passes);
	put(" ok, ");
	putn(fails);
	put(" échec(s)\n");
	S1(SYS_EXIT, fails);
	for (;;)
		;
}
