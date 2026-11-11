/* wtest : essais du canal du serveur graphique (le test joue le serveur et
 * le programme). Sans bibliothèque ; à lancer par « run wtest » (droits). */
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
	put("wtest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

static volatile int registered;
static volatile u64 sig_bits;

static void server(u64 arg)
{
	(void)arg;
	static u64 msg[64];
	registered = S0(SYS_WS_REGISTER) == 0 ? 1 : -1;
	for (;;) {
		i64 n = S2(SYS_WS_NEXT, msg, 1000);
		if (n <= 0)
			continue;
		if (msg[0] == 0xFFFD) {
			sig_bits |= msg[1];
			continue;
		}
		if (msg[0] >= 0xFFF0)
			continue;
		u64 op = msg[0];
		if (op == 98) { /* un événement pour l'appelant */
			u64 ev[4] = {7, 1234, 0, 0};
			S2(SYS_WS_POST, (u32)(msg[5] >> 32), ev);
		}
		msg[0] = op == 99 ? 0 : msg[1] * 3;
		S2(SYS_WS_REPLY, n, msg);
		if (op == 99)
			S1(SYS_THREAD_EXIT, 0);
	}
}


static u64 stack[2048] __attribute__((aligned(16)));

__attribute__((force_align_arg_pointer, noreturn)) void _start(void)
{
	put("wtest : canal du serveur graphique\n");
	static u64 msg[64];
	u64 si[3];
	check("screen_info", S1(SYS_SCREEN_INFO, si) == 0 && si[0] >= 640 && si[1] >= 400, (i64)si[0]);
	check("pas de serveur : E_NODEV", S1(SYS_WS_CALL, msg) == -19, 0);

	S3(SYS_THREAD_CREATE, server, &stack[2048], 0);
	while (!registered)
		S0(SYS_YIELD);
	check("ws_register", registered == 1, registered);
	msg[0] = 1;
	msg[1] = 14;
	check("ws_call", S1(SYS_WS_CALL, msg) == 42, (i64)msg[0]);
	msg[0] = 98;
	msg[1] = 1;
	S1(SYS_WS_CALL, msg);
	u64 ev[4];
	check("ws_post puis ev_wait", S2(SYS_EV_WAIT, ev, 1000) == 7 && ev[1] == 1234, (i64)ev[1]);
	check("ev_wait : délai", S2(SYS_EV_WAIT, ev, 20) == 0, 0);
	S1(SYS_WS_SIGNAL, 4);
	for (int i = 0; i < 50 && !sig_bits; i++)
		S1(SYS_SLEEP, 2);
	check("ws_signal", sig_bits == 4, (i64)sig_bits);

	/* presse-papier */
	char clip[16];
	check("clip_set", S2(SYS_CLIP_SET, "texte", 5) == 0, 0);
	check("clip_get", S2(SYS_CLIP_GET, clip, sizeof clip) == 5 && clip[0] == 't' && clip[4] == 'e', 0);

	/* écran : le serveur prend le framebuffer */
	u64 info[5];
	check("ws_display", S1(SYS_WS_DISPLAY, info) == 0 && info[0] == 0x6D00000000ULL && info[1] == si[0], (i64)info[1]);
	volatile u32 *fb = (volatile u32 *)info[0];
	for (u64 y = 0; y < 64; y++)
		for (u64 x = 0; x < 64; x++)
			fb[y * (info[3] / 4) + x] = 0x00FF8800;
	check("écran écrit", fb[10] == 0x00FF8800, 0);
	check("ws_mode : mode fixé par l'UEFI (E_IO)", S3(SYS_WS_MODE, 800, 600, info) == -5, 0);

	msg[0] = 99;
	S1(SYS_WS_CALL, msg);
	put("wtest : ");
	putn(passes);
	put(" ok, ");
	putn(fails);
	put(" échec(s)\n");
	S1(SYS_EXIT, fails);
	for (;;)
		;
}
