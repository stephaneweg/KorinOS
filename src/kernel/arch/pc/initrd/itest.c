/* itest : essais de l'IPC entre process du noyau x86-64 (canaux, poignées
 * portées, attente, mémoire partagée). Sans bibliothèque ; à lancer par
 * « urun itest » (il fait un fork). */
#include <stdint.h>

#include <korin/syscall_nr.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;

static inline i64 sc(u64 n, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f)
{
	register u64 r10 __asm__("r10") = d;
	register u64 r8 __asm__("r8") = e;
	register u64 r9 __asm__("r9") = f;
	i64 ret;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
			 : "rcx", "r11", "memory");
	return ret;
}
#define S0(n) sc(n, 0, 0, 0, 0, 0, 0)
#define S1(n, a) sc(n, (u64)(a), 0, 0, 0, 0, 0)
#define S2(n, a, b) sc(n, (u64)(a), (u64)(b), 0, 0, 0, 0)
#define S3(n, a, b, c) sc(n, (u64)(a), (u64)(b), (u64)(c), 0, 0, 0)
#define S4(n, a, b, c, d) sc(n, (u64)(a), (u64)(b), (u64)(c), (u64)(d), 0, 0)
#define S6(n, a, b, c, d, e, f) sc(n, (u64)(a), (u64)(b), (u64)(c), (u64)(d), (u64)(e), (u64)(f))

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
	put("itest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

struct msg {
	u64 iov;
	u32 iovcnt, nh;
	u64 handles;
	u32 flags, pad;
};

static i64 send(u64 h, const void *p, u64 n, u32 *hs, u32 nh, u64 fl)
{
	u64 iov[2] = {(u64)p, n};
	struct msg m = {(u64)iov, 1, nh, (u64)hs, 0, 0};
	return S4(SYS_CHAN_SEND, h, &m, fl, 0);
}

static i64 recv(u64 h, void *p, u64 n, u32 *hs, u32 *nh, u32 *flags, u64 fl)
{
	u64 iov[2] = {(u64)p, n};
	struct msg m = {(u64)iov, 1, nh ? *nh : 0, (u64)hs, 0, 0};
	i64 r = S4(SYS_CHAN_RECV, h, &m, fl, 100);
	if (nh)
		*nh = m.nh;
	if (flags)
		*flags = m.flags;
	return r;
}

static int eq(const char *a, const char *b, u64 n)
{
	for (u64 i = 0; i < n; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

__attribute__((force_align_arg_pointer, noreturn)) void _start(void)
{
	put("itest : essais de l'IPC entre process\n");
	char buf[256];
	u32 h[2], g[2], fl = 0;

	/* paquets */
	check("chan_create seqpacket", S2(SYS_CHAN_CREATE, 5, h) == 0 && h[0] && h[1] && h[0] != h[1], 0);
	check("send", send(h[0], "bonjour", 7, 0, 0, 0) == 7, 0);
	i64 r = recv(h[1], buf, sizeof buf, 0, 0, &fl, 0);
	check("recv : le message", r == 7 && eq(buf, "bonjour", 7) && fl == 0, r);
	send(h[0], "0123456789", 10, 0, 0, 0);
	r = recv(h[1], buf, 4, 0, 0, &fl, 0);
	check("paquet coupé (IPC_TRUNC)", r == 4 && (fl & 1) && eq(buf, "0123", 4), r);
	check("vide + NOWAIT : E_AGAIN", recv(h[1], buf, 8, 0, 0, 0, 1) == -11, 0);

	/* attente */
	u32 w[2] = {h[1], 1};
	check("handle_wait : rien", S3(SYS_HANDLE_WAIT, w, 1, 0) == 0, 0);
	send(h[0], "x", 1, 0, 0, 0);
	check("handle_wait : POLLIN", S3(SYS_HANDLE_WAIT, w, 1, 50) == 1 && ((w[1] >> 16) & 1), (i64)w[1]);
	recv(h[1], buf, 8, 0, 0, 0, 0);

	/* flot */
	u32 s[2];
	S2(SYS_CHAN_CREATE, 1, s);
	send(s[0], "abcdef", 6, 0, 0, 0);
	send(s[0], "ghij", 4, 0, 0, 0);
	r = recv(s[1], buf, 8, 0, 0, 0, 0);
	check("flot : deux messages d'un coup", r == 8 && eq(buf, "abcdefgh", 8), r);
	r = recv(s[1], buf, 8, 0, 0, 0, 0);
	check("flot : la suite", r == 2 && eq(buf, "ij", 2), r);

	/* poignée portée par un message */
	S2(SYS_CHAN_CREATE, 5, g);
	u32 carry[2] = {g[0], 0};
	check("send avec une poignée", send(h[0], "p", 1, carry, 1, 0) == 1, 0);
	u32 got[4], nh = 2;
	r = recv(h[1], buf, 8, got, &nh, &fl, 0);
	check("poignée reçue", r == 1 && nh == 1 && got[0] && got[0] != g[0] && (got[1] & 0xFF) == 1, (i64)nh);
	send(got[0], "via", 3, 0, 0, 0);
	r = recv(g[1], buf, 8, 0, 0, 0, 0);
	check("la poignée reçue marche", r == 3 && eq(buf, "via", 3), r);
	u32 self[2] = {h[1], 0};
	check("bout receveur par son canal : E_INVAL", send(h[0], "q", 1, self, 1, 0) == -22, 0);

	/* fin d'un canal */
	S2(SYS_CHAN_SHUTDOWN, s[0], 2);
	check("SHUT_WR : le pair lit la fin", recv(s[1], buf, 8, 0, 0, 0, 0) == 0, 0);
	S1(SYS_HANDLE_CLOSE, g[1]);
	check("pair fermé : E_PIPE", send(g[0], "z", 1, 0, 0, 0) == -32, 0);
	u64 info[8];
	check("handle_info", S2(SYS_HANDLE_INFO, g[0], info) == 0 && (info[2] & 1), (i64)info[2]);
	check("poignée fermée : E_BADF", S1(SYS_HANDLE_CLOSE, g[1]) == -9, 0);

	/* mémoire partagée entre père et fils */
	i64 m = S2(SYS_SHM_CREATE, 8192, 0);
	check("shm_create", m > 0, m);
	i64 a = S6(SYS_SHM_MAP, m, 0, 8192, 3, 0, 0);
	check("shm_map", a >= 0x5800000000LL, a);
	volatile u64 *sh = (volatile u64 *)a;
	sh[0] = 11;
	sh[600] = 22;
	i64 pid = S0(SYS_FORK);
	if (pid == 0) {
		sh[0] = 77; /* partagée : le père le voit */
		u64 sv[2] = {0};
		S2(SYS_SHM_CTL, m, 0); /* (rien) */
		S1(SYS_EXIT, sh[600] == 22 && sv[0] == 0 ? 5 : 6);
	}
	check("fork : fils", S3(SYS_WAIT, pid, 0, 0) == 5, 0);
	check("partagée après fork", sh[0] == 77, (i64)sh[0]);
	i64 c = S6(SYS_SHM_MAP, m, 0, 8192, 3, 0x200000, 0); /* copie sur écriture */
	volatile u64 *cw = (volatile u64 *)c;
	cw[0] = 1234;
	check("VM_COW : copie privée", cw[0] == 1234 && sh[0] == 77, (i64)sh[0]);
	check("vm_query : région partagée", ({ u64 q[4]; S2(SYS_VM_QUERY, a, q) == 0 && (q[2] >> 32) == 4; }), 0);
	check("vm_unmap d'une projection", S2(SYS_VM_UNMAP, c, 8192) == 0, 0);

	/* objets nommés */
	i64 n1 = S2(SYS_SHM_OPEN, "/essai", 1);
	i64 n2 = S2(SYS_SHM_OPEN, "/essai", 0);
	u64 i1[8], i2[8];
	S2(SYS_HANDLE_INFO, n1, i1);
	S2(SYS_HANDLE_INFO, n2, i2);
	check("shm_open : même objet", n1 > 0 && n2 > 0 && n1 != n2 && i1[3] == i2[3], n2);
	check("shm_open EXCL : E_EXIST", S2(SYS_SHM_OPEN, "/essai", 3) == -17, 0);
	check("shm_ctl taille", S3(SYS_SHM_CTL, n1, 1, 4096) == 0 && S2(SYS_HANDLE_INFO, n2, i2) == 0 && i2[1] == 4096,
	      (i64)i2[1]);
	check("shm_unlink", S1(SYS_SHM_UNLINK, "/essai") == 0 && S2(SYS_SHM_OPEN, "/essai", 0) == -2, 0);
	u64 list[64];
	check("handle_list", S3(SYS_HANDLE_LIST, list, 16, 0) >= 6, 0);

	put("itest : ");
	putn(passes);
	put(" ok, ");
	putn(fails);
	put(" échec(s)\n");
	S1(SYS_EXIT, fails);
	for (;;)
		;
}
