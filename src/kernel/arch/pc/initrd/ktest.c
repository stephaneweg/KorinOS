/* ktest : essais des appels système du noyau x86-64 (mémoire, threads,
 * futex, services, système), en C sans bibliothèque. Écrit une ligne par
 * essai (ok / ÉCHEC) puis le bilan ; code de sortie = nombre d'échecs. */
#include <stdint.h>

#include <korin/syscall_nr.h>

typedef uint64_t u64;
typedef int64_t i64;

static inline i64 sc(u64 n, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f)
{
	register u64 r10 __asm__("r10") = d;
	register u64 r8 __asm__("r8") = e;
	register u64 r9 __asm__("r9") = f;
	i64 ret;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
			 : "rcx", "r11", "memory");
	return ret;
}
#define S0(n) sc(n, 0, 0, 0, 0, 0, 0)
#define S1(n, a) sc(n, (u64)(a), 0, 0, 0, 0, 0)
#define S2(n, a, b) sc(n, (u64)(a), (u64)(b), 0, 0, 0, 0)
#define S3(n, a, b, c) sc(n, (u64)(a), (u64)(b), (u64)(c), 0, 0, 0)
#define S4(n, a, b, c, d) sc(n, (u64)(a), (u64)(b), (u64)(c), (u64)(d), 0, 0)
#define S6(n, a, b, c, d, e, f) sc(n, (u64)(a), (u64)(b), (u64)(c), (u64)(d), (u64)(e), (u64)(f))

static u64 slen(const char *s)
{
	u64 n = 0;
	while (s[n])
		n++;
	return n;
}

static void put(const char *s)
{
	S2(SYS_PRINT, s, slen(s));
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
	put("ktest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

/* ---- mutex sur futex */

static volatile uint32_t lock_word;

static void mtx_lock(void)
{
	while (__atomic_exchange_n(&lock_word, 1, __ATOMIC_ACQUIRE))
		S3(SYS_FUTEX_WAIT, &lock_word, 1, 0);
}

static void mtx_unlock(void)
{
	__atomic_store_n(&lock_word, 0, __ATOMIC_RELEASE);
	S2(SYS_FUTEX_WAKE, &lock_word, 1);
}

static volatile u64 counter;

static void worker(u64 n)
{
	for (u64 i = 0; i < n; i++) {
		mtx_lock();
		counter++;
		mtx_unlock();
	}
	S1(SYS_THREAD_EXIT, 7);
}

static void sleeper(u64 arg)
{
	(void)arg;
	S1(SYS_SLEEP, 100000);
	S1(SYS_THREAD_EXIT, 1);
}

/* ---- service d'essai : rend a + b, et compte les signaux */

static volatile i64 svc_chan = -1;
static volatile u64 sig_seen;

static void server(u64 arg)
{
	(void)arg;
	static uint64_t msg[64];
	svc_chan = S2(SYS_SVC_REGISTER, "ktest", 0);
	for (;;) {
		i64 n = S3(SYS_SVC_NEXT, svc_chan, msg, 1000);
		if (n <= 0)
			continue;
		if (msg[0] == 0xFFFD) {
			sig_seen |= msg[1];
			continue;
		}
		if (msg[0] == 99) {
			msg[0] = 0;
			S2(SYS_SVC_REPLY, n, msg);
			S1(SYS_THREAD_EXIT, 0);
		}
		msg[0] = msg[1] + msg[2];
		S2(SYS_SVC_REPLY, n, msg);
	}
}

static char big_bss[4 << 20]; /* .bss paresseux */

static u64 *thread_stack(void)
{
	i64 a = S4(SYS_VM_MAP, 0, 65536, 3, 0);
	return (u64 *)(a + 65536);
}

__attribute__((force_align_arg_pointer, noreturn)) void _start(void)
{
	put("ktest : essais du noyau x86-64\n");

	/* tas */
	i64 b0 = S1(SYS_SBRK, 0);
	i64 b1 = S1(SYS_SBRK, 3 * 4096 + 100);
	char *heap = (char *)b1;
	for (int i = 0; i < 3 * 4096 + 100; i++)
		heap[i] = (char)i;
	check("sbrk", b0 == b1 && b0 > 0 && heap[5000] == (char)5000, b1);
	check("sbrk rend", S1(SYS_SBRK, -(3 * 4096 + 100)) == b1 + 3 * 4096 + 100 && S1(SYS_SBRK, 0) == b0, 0);

	/* régions paresseuses */
	i64 r = S4(SYS_VM_MAP, 0, 1 << 20, 3, 0);
	check("vm_map dans l'arène", r >= 0x5800000000LL && r < 0x6800000000LL, r);
	u64 q[4];
	S2(SYS_VM_QUERY, r, q);
	check("vm_query neuve : 0 page", q[0] == (u64)r && q[1] == (u64)r + (1 << 20) && (q[3] & 0xFFFFFFFF) == 0, (i64)q[3]);
	char *m = (char *)r;
	for (int i = 0; i < 10; i++)
		m[i * 4096] = (char)(i + 1);
	S2(SYS_VM_QUERY, r, q);
	check("vm_query : 10 pages amenées", (q[3] & 0xFFFFFFFF) == 10 && (q[2] >> 32) == 1, (i64)q[3]);
	check("vm_pageinfo", (S1(SYS_VM_PAGEINFO, r) & 0x29) == 0x29 && S1(SYS_VM_PAGEINFO, r + 20 * 4096) == 32,
	      S1(SYS_VM_PAGEINFO, r));
	check("vm_map fixe sans remplacer", S4(SYS_VM_MAP, r, 4096, 3, 0x100000) == -17, 0);
	check("vm_advise dontneed", S3(SYS_VM_ADVISE, r, 4096, 4) == 0 && m[0] == 0 && m[4096] == 2, m[4096]);
	check("vm_protect", S3(SYS_VM_PROTECT, r + 65536, 65536, 1) == 0, 0);
	S2(SYS_VM_QUERY, r + 65536, q);
	check("vm_query coupe", q[0] == (u64)r + 65536 && q[1] == (u64)r + 131072 && (q[2] & 0xFF) == 1, (i64)q[1]);
	check("vm_unmap", S2(SYS_VM_UNMAP, r, 1 << 20) == 0 && S2(SYS_VM_QUERY, r, q) != 0, 0);
	check("vm_map hors arène refusé", S4(SYS_VM_MAP, 0x1000, 4096, 3, 0x10) == -22, 0);

	/* .bss paresseux de 4 Mo */
	for (int i = 0; i < (4 << 20); i += 65536)
		big_bss[i] = 1;
	u64 st[8];
	S2(SYS_VM_STATS, 0, st);
	check(".bss paresseux", big_bss[65536] == 1 && big_bss[4096] == 0 && st[3] >= 64, (i64)st[3]);

	/* tampons partagés */
	i64 bn = S1(SYS_BUF_CREATE, 8192);
	u64 bi[4];
	check("buf_create", bn >= 0 && S2(SYS_BUF_INFO, bn, bi) == 0 && bi[1] == 8192 && bi[0] != 0, bn);
	char *bp = (char *)(0x6800000000ULL + (u64)bn * (64ULL << 20));
	bp[100] = 42;
	check("buf_map (déjà là)", S1(SYS_BUF_MAP, bn) == (i64)bp && bp[100] == 42, 0);
	check("buf_free", S1(SYS_BUF_FREE, bn) == 0, 0);

	/* threads et futex : 4 threads, 20000 passages chacun sous un mutex */
	i64 tids[4];
	for (int i = 0; i < 4; i++)
		tids[i] = S3(SYS_THREAD_CREATE, worker, thread_stack(), 20000);
	int joined = 0;
	for (int i = 0; i < 4; i++)
		joined += S1(SYS_THREAD_JOIN, tids[i]) == 7;
	check("4 threads, futex", joined == 4 && counter == 80000, (i64)counter);
	check("join d'un thread fini (gardé)", S1(SYS_THREAD_JOIN, tids[0]) == 7, 0);
	check("join de soi", S1(SYS_THREAD_JOIN, S0(SYS_THREAD_SELF)) == -35, 0);
	i64 sl = S6(SYS_THREAD_CREATE_EX, sleeper, thread_stack(), 0, 0x1234, 1, "dormeur");
	S1(SYS_SLEEP, 50);
	u64 tl[64 * 8 / 8];
	i64 nt = S3(SYS_THREAD_LIST, 0, tl, 8);
	check("thread_list", nt >= 2, nt);
	check("thread_kill d'un dormeur", S2(SYS_THREAD_KILL, sl, 55) == 0 && S1(SYS_THREAD_JOIN, sl) == 55, 0);
	check("futex : valeur changée", S3(SYS_FUTEX_WAIT, &lock_word, 1, 0) == -11, 0);
	check("futex : délai", S3(SYS_FUTEX_WAIT, &lock_word, 0, 30) == -110, 0);

	/* services */
	i64 st_tid = S3(SYS_THREAD_CREATE, server, thread_stack(), 0);
	i64 ch = S2(SYS_SVC_OPEN, "ktest", 2000);
	check("svc_open", ch >= 0, ch);
	static uint64_t msg[64];
	int good = 0;
	u64 t0 = (u64)S0(SYS_TIME_MS);
	for (int i = 0; i < 200; i++) {
		msg[0] = 1;
		msg[1] = (u64)i;
		msg[2] = 1000;
		i64 res = S2(SYS_SVC_CALL, ch, msg);
		good += res == i + 1000 && msg[0] == (u64)(i + 1000) && (uint32_t)(msg[5] >> 32) == (uint32_t)S0(SYS_GETPID);
	}
	u64 t1 = (u64)S0(SYS_TIME_MS);
	check("200 svc_call", good == 200, good);
	S2(SYS_SVC_SIGNAL, ch, 5);
	S1(SYS_SLEEP, 50);
	check("svc_signal", sig_seen == 5, (i64)sig_seen);
	check("svc_open absent", S2(SYS_SVC_OPEN, "absent", 10) == -2, 0);
	msg[0] = 99;
	S2(SYS_SVC_CALL, ch, msg);
	S1(SYS_THREAD_JOIN, st_tid);

	/* système */
	uint8_t rnd[64] = {0};
	int nz = 0;
	check("getrandom", S2(SYS_GETRANDOM, rnd, 64) == 64, 0);
	for (int i = 0; i < 64; i++)
		nz += rnd[i] != 0;
	check("getrandom pas nul", nz > 32, nz);
	u64 si[10];
	check("sysinfo", S1(SYS_SYSINFO, si) == 0 && si[0] >= 1 && si[1] > 0 && si[4] >= 2, (i64)si[0]);
	check("clock_get", S1(SYS_CLOCK_GET, 0) > 1700000000, S1(SYS_CLOCK_GET, 0));
	char klog[256];
	check("klog", S2(SYS_KLOG, klog, sizeof klog) > 10 && klog[0] == '[', 0);
	u64 pi[8];
	check("proc_info", S2(SYS_PROC_INFO, 0, pi) == 0 && pi[0] == (u64)S0(SYS_GETPID), 0);
	u64 pl[16 * 8];
	check("proclist", S2(SYS_PROCLIST, pl, 16) >= 1, 0);
	check("droits", S1(SYS_PROC_RIGHTS, S0(SYS_GETPID)) == 1 && (S0(SYS_PROC_FLAGS) & 0x100), 0);
	check("appel inconnu", S0(0x7F) == -38, 0);
	check("adresse fausse", S2(SYS_PRINT, 0x10, 5) == -14, 0);

	/* matériel : ACPI_TABLE, IRQ_MSI (docs/etude/PC-PILOTES.md) */
	uint8_t tab[64];
	i64 tn = S4(SYS_ACPI_TABLE, "APIC", 0, tab, sizeof tab);
	check("acpi_table APIC", tn > 44 && tab[0] == 'A' && tab[3] == 'C', tn);
	check("acpi_table DSDT", S4(SYS_ACPI_TABLE, "DSDT", 0, tab, 8) > 36 && tab[0] == 'D', 0);
	check("acpi_table absente", S4(SYS_ACPI_TABLE, "XXXX", 0, tab, 8) == -2, 0);
	u64 ad[4] = {0};
	i64 ml = S2(SYS_IRQ_MSI, 2, ad);
	check("irq_msi", ml >= 0x100 && (ad[0] >> 20) == 0xFEE && ad[1] >= 0xB8 && ad[1] < 0xF0 &&
			     (ad[2] >> 20) == 0xFEE && ad[3] >= 0xB8 && ad[3] < 0xF0, ml);
	check("irq_msi deux vecteurs", ad[0] != ad[2] || ad[1] != ad[3], 0);
	check("irq_bind msi", S3(SYS_IRQ_BIND, ml, -1, 0) == 0, 0);
	check("irq_wait msi (délai)", S2(SYS_IRQ_WAIT, ml, 5) == 0, 0);
	check("irq_unbind msi", S1(SYS_IRQ_UNBIND, ml) == 0, 0);
	check("irq_msi rendue", S3(SYS_IRQ_BIND, ml, -1, 0) == -22, 0);
	check("irq_msi 0", S2(SYS_IRQ_MSI, 0, ad) == -22, 0);

	put("ktest : ");
	putn(passes);
	put(" ok, ");
	putn(fails);
	put(" échec(s) ; 200 requêtes de service en ");
	putn((i64)(t1 - t0));
	put(" ms\n");
	S1(SYS_EXIT, fails);
	for (;;)
		;
}
