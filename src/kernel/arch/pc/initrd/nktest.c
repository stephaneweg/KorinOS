/* nktest : NetKit écrit en C (src/kits/netkit/netkit.c), chargé par LIB_OPEN
 * sur le noyau x86-64 ; le test joue un petit netd (service « net ») avec un
 * tampon partagé pour les gros envois et réceptions. « run nktest » (droits). */
#include <stdint.h>

#include <korin/syscall_nr.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;
typedef uint8_t u8;

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
	put("nktest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

/* ---- faux netd */

static volatile int ready;
static i64 bufno;
static u8 *buf;      /* le tampon partagé, vu par le faux netd */
static u64 received; /* octets reçus par SEND */
static u64 sum;      /* somme des octets reçus */

static void fake_netd(u64 arg)
{
	(void)arg;
	static u64 m[64];
	u8 *data = (u8 *)m + 48;
	i64 ch = S2(SYS_SVC_REGISTER, "net", 0);
	bufno = S1(SYS_BUF_CREATE, 128 * 1024);
	buf = (u8 *)(0x6800000000ULL + (u64)bufno * (64ULL << 20));
	ready = ch >= 0 && bufno >= 0 ? 1 : -1;
	for (;;) {
		i64 n = S3(SYS_SVC_NEXT, ch, m, 1000);
		if (n <= 0 || m[0] >= 0xFFF0)
			continue;
		switch (m[0]) {
		case 1: /* OPEN : le numéro du tampon en b */
			m[2] = (u64)bufno;
			m[0] = 0;
			break;
		case 2: /* SOCKET */
			m[0] = 5;
			break;
		case 3: /* CONNECT */
			m[0] = m[1] == 5 && m[2] == 0x0A000202 && m[3] == 80 ? 0 : (u64)-22;
			break;
		case 7: /* SEND : données dans le message (b = -1) ou à b dans le tampon */
		{
			const u8 *src = m[2] == (u64)-1 ? data : buf + m[2];
			for (u64 i = 0; i < m[3]; i++)
				sum += src[i];
			received += m[3];
			m[0] = m[3];
			break;
		}
		case 8: /* RECV : max en c */
		{
			u8 *dst = m[2] == (u64)-1 ? data : buf + m[2];
			u64 k = m[3] < 6000 ? m[3] : 6000;
			for (u64 i = 0; i < k; i++)
				dst[i] = (u8)(i * 3);
			m[0] = k;
			break;
		}
		case 12: /* RESOLVE */
			m[0] = data[0] == 'k' ? 0x0A000202 : (u64)-2;
			break;
		case 14: /* INFO : 48 octets */
			for (int i = 0; i < 48; i++)
				data[i] = (u8)(i + 1);
			m[0] = 0;
			break;
		case 99:
			m[0] = 0;
			S2(SYS_SVC_REPLY, n, m);
			S1(SYS_THREAD_EXIT, 0);
			break;
		default:
			m[0] = (u64)-38;
		}
		S2(SYS_SVC_REPLY, n, m);
	}
}

static u64 stack[4096] __attribute__((aligned(16)));
static u8 big[20000], in[9000];

typedef i64 (*fn)(u64, u64, u64, u64, u64);

static int eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

__attribute__((force_align_arg_pointer, noreturn)) void _start(void)
{
	put("nktest : NetKit en C sur x86-64\n");
	i64 t = S2(SYS_LIB_OPEN, "netkit", 33);
	check("lib_open netkit (version 33)", t >= 0x5000000000LL, t);
	const u32 *tab = (const u32 *)t;
#define F(i) ((fn)(t + tab[2 + (i)]))
	enum { FMT_IP, PRINT_IP, WAIT_DHCP, SOCKET, CONNECT, BIND, LISTEN, ACCEPT, SEND, RECV, CLOSE, SENDTO,
	       RECVFROM, RESOLVE, INFO, PING, IFACES, TIME, NTP, WSCAN, WJOIN, WSTATUS };
	char ip[20];
	F(FMT_IP)(0xC0A80102, (u64)ip, 0, 0, 0);
	check("fmt_ip", eq(ip, "192.168.1.2"), 0);
	u8 ni[32];
	check("sans netd : net_info rend l'erreur", F(INFO)((u64)ni, 0, 0, 0, 0) < 0 && ni[0] == 0, 0);
	check("sans wifi : E_NODEV", F(WSTATUS)((u64)in, 0, 0, 0, 0) == -19, 0);

	S3(SYS_THREAD_CREATE, fake_netd, &stack[4096], 0);
	while (!ready)
		S0(SYS_YIELD);
	check("net_socket", F(SOCKET)(1, 0, 0, 0, 0) == 5, 0);
	check("net_connect", F(CONNECT)(5, 0x0A000202, 80, 0, 0) == 0, 0);
	check("net_resolve", F(RESOLVE)((u64) "korin.test", 0, 0, 0, 0) == 0x0A000202, 0);
	check("net_info", F(INFO)((u64)ni, 0, 0, 0, 0) == 0 && ni[0] == 1 && ni[31] == 32, ni[31]);
	check("send court (dans le message)", F(SEND)(5, (u64) "bonjour", 7, 0, 0) == 7 && received == 7, 0);
	u64 want = sum;
	for (u64 i = 0; i < sizeof big; i++) {
		big[i] = (u8)(i * 7);
		want += big[i];
	}
	i64 r = F(SEND)(5, (u64)big, sizeof big, 0, 0);
	check("send de 20 000 octets (zones de 8 Ko)", r == (i64)sizeof big && sum == want, r);
	r = F(RECV)(5, (u64)in, 100, 50, 0);
	check("recv court", r == 100 && in[99] == (u8)(99 * 3), r);
	r = F(RECV)(5, (u64)in, sizeof in, 50, 0);
	check("recv par zone (8 Ko au plus)", r == 6000 && in[5999] == (u8)(5999 * 3), r);

	u64 m[64] = {99};
	S2(SYS_SVC_CALL, S2(SYS_SVC_OPEN, "net", 0), m);
	put("nktest : ");
	putn(passes);
	put(" ok, ");
	putn(fails);
	put(" échec(s)\n");
	S1(SYS_EXIT, fails);
	for (;;)
		;
}
