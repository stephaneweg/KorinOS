/* threadtest - essai des threads (appels 0x20-0x2A, docs/ABI.md) et des verrous d'AppKit
 * (mtx_*, cnd_* : docs/KITS.md), comme threadtest.asm :
 *  * TPIDR_EL0 : 0 au départ du thread principal ; valeur donnée par thread_create_ex, gardée sous
 *    préemption (8 threads sur 4 cœurs qui le relisent et le changent sans cesse pendant 1,5 s), sur
 *    plusieurs cœurs (thread_list : cœur de chacun) ; celui du thread principal intact ;
 *  * thread_self, thread_set_name et thread_list (noms), thread_join (code de sortie de
 *    thread_exit, E_SRCH, E_DEADLK), thread_kill (thread bloqué dans un futex, thread qui tourne
 *    sans appel système) ;
 *  * priorités : 16 threads qui tournent 2 s, moitié en priorité haute : ces derniers ont plus de
 *    temps processeur sur un même cœur, les normaux en ont quand même (pas de famine) ; temps réel
 *    sans droits : E_PERM ;
 *  * mutex (4 threads, compteur protégé), condition (producteur et consommateur), attentes avec
 *    délai (cnd_timedwait, futex_wait).
 * Sortie : « threadtest : N PASS, M FAIL », code de sortie = M.
 *
 * Le registre de thread est lu et écrit en asm en ligne : TPIDR_EL0 sur AArch64. Sur x86-64, son
 * équivalent est la base de FS, que le noyau du PC pose (arch_set_tls) mais qu'un process ne peut ni
 * lire ni écrire (CR4.FSGSBASE éteint : rdfsbase serait une instruction interdite). Sur PC donc :
 * chaque thread reçoit pour « TPIDR » l'adresse d'un mot qui contient sa propre adresse (comme un
 * TCB) et la relit par « mov %fs:0 » — la valeur donnée par thread_create_ex et sa garde sous
 * préemption sont vraiment vérifiées — mais la boucle ne la change pas (pas de wrfsbase) ; le
 * thread principal (base 0, illisible) garde sa valeur dans une variable : ses deux essais n'y
 * vérifient rien. */
#include "korinrt.h"

#define STK 16384 /* pile d'un thread */
#define NSTK 16   /* piles (threads en même temps au plus) */
#define N_TLS 8
#define N_PRIO 16
#define N_MTX 4
#define MTX_ROUNDS 20000
#define N_PROD 1000
#define E_SRCH -3
#define E_DEADLK -35
#define E_BUSY -16
#define E_TIMEDOUT -110
#define THREAD_PRIO_NORMAL 0
#define THREAD_PRIO_HIGH 1
#define THREAD_PRIO_RT 2
#define THL_TID 0
#define THL_PRIO 16
#define THL_CPU 24
#define THL_RUNTIME 32
#define THL_NAME 40
#define THL_SIZE 64

/* fiche d'un thread (64 o) */
struct rec {
	u64 tls;   /* TPIDR_EL0 attendu */
	u64 tid;   /* tid (thread_self) */
	u64 bad;   /* lectures fausses de TPIDR_EL0 */
	u64 iter;  /* tours de boucle */
	u64 index;
	u64 prio;
	u64 pad[2];
};

static long n_pass, n_fail, tid_main;
static volatile u64 counter, value, full;
static long list_n;
static long tids[N_PRIO];
static struct rec recs[N_PRIO];
static u64 per_cpu[4][8];
static volatile u32 stop, kw;
static u32 mtx, c_full, c_empty, c_none;
static u8 list_buf[64 * THL_SIZE] __attribute__((aligned(64)));
static u8 stacks[STK * NSTK] __attribute__((aligned(16)));

/* ---- le registre de thread */

#if defined(__aarch64__)
static inline u64 tls_get(void)
{
	u64 v;
	__asm__ volatile("mrs %0, tpidr_el0" : "=r"(v));
	return v;
}
static inline void tls_set(u64 v)
{
	__asm__ volatile("msr tpidr_el0, %0" ::"r"(v));
}
#define tls_main_get tls_get
#define tls_main_set tls_set
#define TLS_STEP 0x10 /* la boucle le change tous les 65 536 tours */
#else
static u64 tls_slot[N_TLS]; /* mot n : sa propre adresse (base de FS du thread n) */
static u64 tls_main;
static inline u64 tls_get(void)
{
	u64 v;
	__asm__ volatile("mov %%fs:0, %0" : "=r"(v));
	return v;
}
static inline void tls_set(u64 v)
{
	(void)v; /* (pas de wrfsbase : voir en tête) */
}
static u64 tls_main_get(void) { return tls_main; }
static void tls_main_set(u64 v) { tls_main = v; }
#define TLS_STEP 0
#endif

/* 1 si réussi, libellé, valeur : « threadtest : PASS libellé (valeur) » */
static void verdict(int ok, const char *label, long v)
{
	if (ok)
		n_pass++;
	else
		n_fail++;
	ak_puts("threadtest : ");
	ak_puts(ok ? "PASS " : "FAIL ");
	ak_puts(label);
	ak_puts(" (");
	print_dec(v);
	ak_puts(")\n");
}

/* numéro de pile -> son sommet */
static u8 *stack_top(long i)
{
	return stacks + (i + 1) * STK;
}

/* thread_list de ce process dans list_buf */
static void list(void)
{
	list_n = korin_sys3(SYS_THREAD_LIST, 0, list_buf, 64);
}

/* tid -> son entrée de list_buf, 0 si absent */
static u8 *find(long tid)
{
	u8 *e = list_buf;
	for (long n = list_n; n > 0; n--, e += THL_SIZE)
		if (*(long *)(e + THL_TID) == tid)
			return e;
	return 0;
}

#define FIELD(e, off) (*(u64 *)((e) + (off)))

NORETURN static void thread_exit(long code)
{
	korin_sys1(SYS_THREAD_EXIT, code);
	__builtin_unreachable();
}

/* ---- les threads */

/* fiche : relit TPIDR_EL0 pendant 1,5 s (et le change tous les 65 536 tours) ;
 * code de sortie = index x 7 + erreurs x 1000 */
static void w_tls(struct rec *r)
{
	u64 want = r->tls, bad = 0, it = 0;
	r->tid = korin_sys0(SYS_THREAD_SELF);
	if (tls_get() != want) /* valeur de thread_create_ex */
		bad++;
	u64 end = korin_sys0(SYS_TIME_MS) + 1500;
	for (;;) {
		if (tls_get() != want)
			bad++;
		it++;
		if (!(it & 0xFFFF) && TLS_STEP) {
			want += TLS_STEP;
			tls_set(want);
		}
		if (it & 0xFFF)
			continue;
		if ((u64)korin_sys0(SYS_TIME_MS) >= end)
			break;
	}
	r->bad = bad;
	r->iter = it;
	thread_exit(r->index * 7 + bad * 1000);
}

/* fiche : tourne jusqu'à stop */
static void w_spin(struct rec *r)
{
	u64 n = 0;
	for (;;) {
		n++;
		if (n & 0xFFF)
			continue;
		if (stop)
			break;
	}
	r->iter = n;
	thread_exit(0);
}

/* bloqué pour toujours dans un futex (thread_kill le réveille et le tue) */
static void w_blocked(void)
{
	do
		korin_sys3(SYS_FUTEX_WAIT, &kw, 0, 0);
	while (!kw);
	thread_exit(5);
}

/* tourne pour toujours sans appel système (tué à un retour d'interruption) */
static void w_running(void)
{
	u64 n = 0;
	for (;;) {
		n++;
		__asm__ volatile("" : "+r"(n)); /* (la boucle reste : pas d'appel système) */
	}
}

/* MTX_ROUNDS incréments du compteur sous le mutex (yield parfois, verrou tenu) */
static void w_mtx(void)
{
	for (u64 i = 0; i < MTX_ROUNDS; i++) {
		mtx_lock(&mtx);
		u64 c = counter;
		if (!(i & 0xFF))
			korin_sys0(SYS_YIELD);
		counter = c + 1;
		mtx_unlock(&mtx);
	}
	thread_exit(0);
}

/* consommateur : N_PROD valeurs -> code de sortie = leur somme */
static void w_consumer(void)
{
	u64 sum = 0;
	for (int got = 0; got < N_PROD; got++) {
		mtx_lock(&mtx);
		while (!full)
			cnd_wait(&c_full, &mtx);
		sum += value;
		full = 0;
		cnd_signal(&c_empty);
		mtx_unlock(&mtx);
	}
	thread_exit(sum);
}

static long thread_create(void *entry, long stack, long arg)
{
	return korin_sys3(SYS_THREAD_CREATE, entry, stack_top(stack), arg);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ak_puts("threadtest : TPIDR_EL0, join, priorites, mutex et conditions (environ 6 s)\n");

	/* --- TPIDR_EL0 du thread principal */
	u64 v = tls_main_get();
	verdict(v == 0, "TPIDR_EL0 du thread principal a 0 au depart", v);
	tls_main_set(0xABCDEF);

	/* --- thread_self, nom du thread principal */
	long r = korin_sys0(SYS_THREAD_SELF);
	tid_main = r;
	verdict(r > 0, "thread_self (tid)", r);
	r = korin_sys2(SYS_THREAD_SET_NAME, 0, "tt-main");
	verdict(r == 0, "thread_set_name", r);

	/* --- 8 threads TPIDR_EL0 */
	long i;
	for (i = 0; i < N_TLS; i++) {
		struct rec *e = &recs[i];
#if defined(__aarch64__)
		e->tls = ((u64)(i + 1) << 32) + 0x5000;
#else
		tls_slot[i] = (u64)&tls_slot[i];
		e->tls = tls_slot[i];
#endif
		e->index = i;
		e->tid = 0;
		tids[i] = korin_sys6(SYS_THREAD_CREATE_EX, (long)w_tls, (long)stack_top(i), (long)e, e->tls,
				     THREAD_PRIO_NORMAL, (long)"tls-worker");
		if (tids[i] < 0)
			break;
	}
	verdict(i == N_TLS, "thread_create_ex x 8 (TPIDR_EL0, nom)", i);
	korin_sys1(SYS_SLEEP, 500); /* pendant qu'ils tournent : cœurs et noms */
	list();
	u64 cpus = 0, names = 0;
	for (i = 0; i < N_TLS; i++) {
		u8 *e = find(tids[i]);
		if (!e)
			continue;
		cpus |= 1UL << FIELD(e, THL_CPU);
		names += str_eq((char *)e + THL_NAME, "tls-worker");
	}
	long ncpu = 0;
	for (u64 m = cpus; m; m >>= 1)
		ncpu += m & 1;
	verdict((u64)ncpu >= 2, "threads sur plusieurs coeurs (thread_list)", ncpu);
	verdict(names == N_TLS, "noms des threads (thread_list)", names);
	{
		u8 *e = find(tid_main); /* nom du thread principal dans la liste */
		long ok = e ? str_eq((char *)e + THL_NAME, "tt-main") : 0;
		verdict(ok != 0, "nom du thread principal (thread_list)", ok);
	}
	u64 codes = 0, bad = 0, iters = 0, selves = 0; /* join : codes, erreurs de TPIDR_EL0 */
	for (i = 0; i < N_TLS; i++) {
		u64 c = korin_sys1(SYS_THREAD_JOIN, tids[i]);
		if (c % 1000 == (u64)i * 7) /* code mod 1000 = index x 7 */
			codes++;
		bad += recs[i].bad;
		iters += recs[i].iter;
		if (recs[i].tid == (u64)tids[i])
			selves++;
	}
	verdict(codes == N_TLS, "thread_join : codes de thread_exit", codes);
	verdict(selves == N_TLS, "thread_self dans chaque thread", selves);
	verdict(bad == 0, "TPIDR_EL0 garde sous preemption (lectures fausses)", bad);
	verdict(iters >= N_TLS * 0x10000, "tours de boucle (milliers)", iters / 1000);
	v = tls_main_get();
	verdict(v == 0xABCDEF, "TPIDR_EL0 du thread principal intact", v);

	/* --- join : erreurs */
	r = korin_sys1(SYS_THREAD_JOIN, tids[1]); /* déjà fini (dans l'anneau) : son code */
	verdict(r == 7, "thread_join d'un thread deja rejoint : son code", r);
	r = korin_sys1(SYS_THREAD_JOIN, 0x7FFFFFF0);
	verdict(r == E_SRCH, "thread_join d'un tid inconnu : E_SRCH", r);
	r = korin_sys1(SYS_THREAD_JOIN, tid_main);
	verdict(r == E_DEADLK, "thread_join de soi : E_DEADLK", r);

	/* --- priorités */
	long old = korin_sys2(SYS_THREAD_SET_PRIORITY, 0, THREAD_PRIO_HIGH); /* soi : haute puis normale */
	r = korin_sys2(SYS_THREAD_SET_PRIORITY, 0, THREAD_PRIO_NORMAL);
	verdict(old == THREAD_PRIO_NORMAL && r == THREAD_PRIO_HIGH, "thread_set_priority rend l'ancienne", r);
	r = korin_sys2(SYS_THREAD_SET_PRIORITY, 0, THREAD_PRIO_RT);
	verdict(r == E_PERM, "temps reel sans droits : E_PERM", r);
	stop = 0;
	for (i = 0; i < N_PRIO; i++) {
		struct rec *e = &recs[i];
		e->iter = 0;
		e->prio = (i >> 2) & 1; /* blocs de 4 : normal, haut, normal, haut (tourniquet des cœurs :
					   chacun a les deux sortes) */
		tids[i] = korin_sys6(SYS_THREAD_CREATE_EX, (long)w_spin, (long)stack_top(i), (long)e, 0,
				     e->prio, (long)"prio-worker");
		if (tids[i] < 0)
			break;
	}
	verdict(i == N_PRIO, "thread_create_ex x 16 (8 normaux, 8 hauts)", i);
	korin_sys1(SYS_SLEEP, 2000);
	list(); /* temps de chacun (ticks = ms) */
	stop = 1;
	memset(per_cpu, 0, sizeof per_cpu); /* par cœur : haut (somme, nombre), normal (somme, nombre, minimum) */
	for (i = 0; i < N_PRIO; i++) {
		u8 *e = find(tids[i]);
		if (!e)
			continue;
		u64 *pc = per_cpu[FIELD(e, THL_CPU) & 3];
		u64 rt = FIELD(e, THL_RUNTIME);
		if (FIELD(e, THL_PRIO)) {
			pc[0] += rt;
			pc[1]++;
		} else {
			pc[2] += rt;
			pc[3]++;
			if (pc[3] == 1 || rt < pc[4])
				pc[4] = rt;
		}
	}
	u64 hi = 0, lo = 0, minn = 0x7FFFFFFF, mixed = 0; /* cœurs où les deux sortes tournaient */
	for (i = 0; i < 4; i++) {
		u64 *pc = per_cpu[i];
		if (!pc[1] || !pc[3])
			continue;
		hi += pc[0] / pc[1];
		lo += pc[2] / pc[3];
		if (pc[4] < minn)
			minn = pc[4];
		mixed++;
	}
	verdict(mixed >= 1, "coeurs avec les deux priorites", mixed);
	u64 adv = lo ? hi * 100 / lo : 0; /* avance des hauts : 100 x haut / normal */
	verdict(adv >= 150, "priorite haute : plus de temps processeur (% du normal)", adv);
	u64 mn = mixed ? minn : 0; /* pas de famine : chaque normal a eu au moins 2 % des 2 s */
	verdict(mn >= 40, "pas de famine : temps minimal d'un thread normal (ms)", mn);
	for (i = 0; i < N_PRIO; i++)
		korin_sys1(SYS_THREAD_JOIN, tids[i]);

	/* --- thread_kill */
	kw = 0;
	long tb = thread_create(w_blocked, 0, 0);
	long tr = thread_create(w_running, 1, 0);
	korin_sys1(SYS_SLEEP, 200);
	long k = korin_sys2(SYS_THREAD_KILL, tb, 77);
	k |= korin_sys2(SYS_THREAD_KILL, tr, 78);
	r = korin_sys1(SYS_THREAD_JOIN, tb);
	verdict(r == 77 && k == 0, "thread_kill d'un thread bloque (code)", r);
	r = korin_sys1(SYS_THREAD_JOIN, tr);
	verdict(r == 78, "thread_kill d'un thread qui tourne (code)", r);
	r = korin_sys2(SYS_THREAD_KILL, 0x7FFFFFF0, 1);
	verdict(r == E_SRCH, "thread_kill d'un tid inconnu : E_SRCH", r);

	/* --- mutex */
	counter = 0;
	for (i = 0; i < N_MTX; i++)
		tids[i] = thread_create(w_mtx, i, 0);
	for (i = 0; i < N_MTX; i++)
		korin_sys1(SYS_THREAD_JOIN, tids[i]);
	verdict(counter == N_MTX * MTX_ROUNDS, "mutex : compteur de 4 threads", counter);
	mtx_lock(&mtx); /* trylock : pris -> E_BUSY, libre -> 0 */
	long t1 = mtx_trylock(&mtx);
	mtx_unlock(&mtx);
	long t2 = mtx_trylock(&mtx);
	mtx_unlock(&mtx);
	verdict(t1 == E_BUSY && t2 == 0, "mtx_trylock : E_BUSY puis 0", t1);

	/* --- condition : producteur (ici) et consommateur */
	full = 0;
	long tc = thread_create(w_consumer, 0, 0);
	for (u64 n = 1; n <= N_PROD; n++) {
		mtx_lock(&mtx);
		while (full)
			cnd_wait(&c_empty, &mtx);
		value = n;
		full = 1;
		cnd_signal(&c_full);
		mtx_unlock(&mtx);
	}
	r = korin_sys1(SYS_THREAD_JOIN, tc);
	verdict(r == N_PROD * (N_PROD + 1) / 2, "condition : somme du consommateur", r);

	/* --- attentes avec délai */
	long t0 = korin_sys0(SYS_TIME_MS);
	mtx_lock(&mtx);
	r = cnd_timedwait(&c_none, &mtx, 60);
	mtx_unlock(&mtx);
	u64 dt = korin_sys0(SYS_TIME_MS) - t0;
	verdict(r == E_TIMEDOUT && dt >= 50, "cnd_timedwait : E_TIMEDOUT apres le delai (ms)", dt);
	t0 = korin_sys0(SYS_TIME_MS);
	kw = 0;
	r = korin_sys3(SYS_FUTEX_WAIT, &kw, 0, 100);
	dt = korin_sys0(SYS_TIME_MS) - t0;
	verdict(r == E_TIMEDOUT && dt >= 90, "futex_wait : E_TIMEDOUT apres le delai (ms)", dt);
	r = korin_sys3(SYS_FUTEX_WAIT, &kw, 5, 0); /* valeur différente : tout de suite */
	verdict(r == E_AGAIN, "futex_wait d'une autre valeur : E_AGAIN", r);

	/* --- bilan */
	ak_puts("threadtest : ");
	print_dec(n_pass);
	ak_puts(" PASS, ");
	print_dec(n_fail);
	ak_puts(" FAIL\n");
	_exit(n_fail);
}
