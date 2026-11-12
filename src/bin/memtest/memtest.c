/* memtest - essai de la mémoire des process (docs/ABI.md, appels 0x06-0x0C,
 * 0x1A, 0x65 ; docs/ARCHITECTURE.md, section Mémoire), comme memtest.asm :
 *  * réservation et peuplement à la demande : 256 Mo réservés, une page sur
 *    64 touchée, pages réellement prises (vm_query, vm_stats, pages libres),
 *    page jamais écrite lue à zéro ; munmap (moitié, trou au milieu, tout :
 *    pages rendues) ;
 *  * réservation de 8 Go PROT_NONE (plus que la RAM), mprotect d'un morceau
 *    (lecture / écriture, puis lecture seule), tampon PROT_NONE refusé par le
 *    noyau (E_FAULT), munmap hors de l'arène refusé ;
 *  * le noyau écrit dans une page pas encore peuplée (getrandom) ;
 *  * madvise DONTNEED (pages rendues, relues à zéro), WILLNEED (peuplées) ;
 *  * VM_FIXED_NOREPLACE sur une place prise (E_EXIST), VM_FIXED (remplacée) ;
 *  * mprotect : un fils qui écrit après PROT_READ, un fils qui lit une page
 *    PROT_NONE : tués proprement (code -11), le système continue ;
 *  * image préchargée (SD0:/etc/preload.txt doit lister SD0:/bin/memtest) :
 *    pages de code partagées, page de données en copie sur écriture ; deux
 *    fils écrivent chacun dans la même variable : chacun ne voit que sa
 *    valeur, le père garde l'originale ; références de l'image (+2 pendant,
 *    rendues après) ;
 *  * charge sur plusieurs cœurs : 4 threads qui font fauter les mêmes pages
 *    en même temps (contenu juste, pas de page en double), et 50 cycles
 *    map / touche / vérifie / unmap chacun ; rien ne fuit ;
 *  * fichier changé : SD0:/bin/hello (préchargé) ouvert en écriture -> son
 *    image est oubliée (vfs, IMAGE_FORGET), il se lance quand même.
 * Sortie : « memtest : N PASS, M FAIL », code de sortie = M.
 *   memtest bench [n]  : temps de lancement de SD0:/bin/fasmarm préchargé et
 *                        d'une copie non préchargée (SD0:/tmp), n fois (10)
 *   memtest child ...  : (fils) cow, protect, none, stats
 * Les arguments viennent de la ligne brute du noyau (krt_line), comme x0 dans
 * l'asm. Les pages de code comptées par « pages partagées » vont du début du
 * code (_start) à la fin du premier segment arrondie à la page (fin_code de
 * l'asm) : la valeur montrée dépend donc de la taille du binaire. */
#include "korinrt.h"

#define MO 0x100000UL
#define GO 0x40000000UL
#define LAZY_SIZE (256 * MO)
#define STEP (64 * 4096UL) /* une page sur 64 */
#define N_TOUCH (LAZY_SIZE / STEP)
#define COW_INIT 0x777
#define N_TH 4
#define TH_PAGES 4096 /* région commune : 16 Mo */
#define TH_CYCLES 50
#define STK 16384
#define E_FAULT -14
#define COPY_BUF 65536

#define PROT_NONE 0
#define VM_FIXED 0x10
#define VM_FIXED_NOREPLACE 0x100000
#define VM_ARENA_BASE 0x5800000000UL
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#define PI_PRESENT 1
#define PI_SHARED 2
#define PI_COW 4
#define PI_WRITE 8
#define VS_OWN 0
#define VS_SHARED 8
#define VS_FAULTS 24
#define VS_COWS 32
#define VS_FREE_LOW 48
#define VS_FREE_HIGH 56
#define IML_FSIZE 0
#define IML_REFS 24
#define IML_PATH 48
#define IML_SIZE 128
#define THREAD_PRIO_NORMAL 0

#if defined(__x86_64__)
#define THREAD_ATTR __attribute__((force_align_arg_pointer, noreturn))
#else
#define THREAD_ATTR __attribute__((noreturn))
#endif

static const char p_self[] = "SD0:/bin/memtest";
static const char p_hello[] = "SD0:/bin/hello";
static const char p_fasm[] = "SD0:/bin/fasmarm";
static const char p_copy[] = "SD0:/tmp/fasmarm.copy";

static long n_pass, n_fail;
static u8 *common;
static long pipe_fd;
static long tids[N_TH];
static u64 stats_buf[8];
static u64 query_buf[4];
static u8 image_list[16 * IML_SIZE];
static u8 pipe_buf[256];
/* une page à elle seule, jamais écrite par le père (copie sur écriture) */
static volatile u64 cow_page[512] __attribute__((aligned(4096))) = { COW_INIT };
#define cow_var (cow_page[0])
static u8 stacks[STK * (N_TH + 1)] __attribute__((aligned(16)));

extern void _start(const char *line);
extern const u8 __ehdr_start[];

static long sys(long nr, long a, long b, long c, long d)
{
	return ksys(nr, a, b, c, d, 0, 0);
}

static long vm_map(u64 addr, u64 size, long prot, long flags)
{
	return sys(SYS_VM_MAP, (long)addr, (long)size, prot, flags);
}

static long vm_unmap(u64 addr, u64 size)
{
	return sys(SYS_VM_UNMAP, (long)addr, (long)size, 0, 0);
}

static long pageinfo(const volatile void *p)
{
	return sys(SYS_VM_PAGEINFO, (long)p, 0, 0, 0);
}

static u64 *stats(void)
{
	sys(SYS_VM_STATS, 0, (long)stats_buf, 0, 0);
	return stats_buf;
}

static u64 free_pages(void)
{
	u64 *s = stats();
	return s[VS_FREE_LOW / 8] + s[VS_FREE_HIGH / 8];
}

static u64 *query(u64 addr)
{
	sys(SYS_VM_QUERY, (long)addr, (long)query_buf, 0, 0);
	return query_buf;
}

static u32 q_pages(const u64 *q)
{
	return *(const u32 *)((const u8 *)q + 24);
}

/* 1 si s commence par p */
static int prefix(const char *s, const char *p)
{
	while (*p)
		if (*s++ != *p++)
			return 0;
	return 1;
}

/* égales aux majuscules près (A-Z seulement) */
static int str_ieq(const char *a, const char *b)
{
	for (;;) {
		u32 x = (u8)*a++, y = (u8)*b++;
		if (x - 'A' <= 25)
			x += 32;
		if (y - 'A' <= 25)
			y += 32;
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

/* nombre décimal en tête (espaces sautés ; 0 sinon) */
static u64 number(const char *s)
{
	u64 v = 0;
	while (*s == ' ')
		s++;
	for (;;) {
		u32 d = (u8)*s++ - '0';
		if (d > 9)
			return v;
		v = v * 10 + d;
	}
}

/* ok, libellé, valeur montrée */
static void verdict(int ok, const char *label, long v)
{
	const char *w;
	if (ok) {
		n_pass++;
		w = "PASS ";
	} else {
		n_fail++;
		w = "FAIL ";
	}
	ak_puts("memtest : ");
	ak_puts(w);
	ak_puts(label);
	ak_puts(" (");
	print_dec(v);
	ak_puts(")\n");
}

/* --- réservation et peuplement à la demande, munmap */
static void t_lazy(void)
{
	u64 free0 = free_pages();
	u64 faults0 = stats()[VS_FAULTS / 8];
	u64 r = (u64)vm_map(0, LAZY_SIZE, PROT_READ | PROT_WRITE, 0);
	verdict(r >= VM_ARENA_BASE, "vm_map 256 Mo : adresse dans l'arene", (long)r);
	u64 *q = query(r);
	u32 pg = q_pages(q);
	verdict(q[0] == r && q[1] == r + LAZY_SIZE && pg == 0,
		"vm_query : la region entiere, aucune page presente", pg);
	for (u64 i = 0; i < N_TOUCH; i++)
		*(volatile u64 *)(r + i * STEP) = (i + 1) | 0x5A5A0000;
	pg = q_pages(query(r));
	verdict(pg == N_TOUCH, "une page sur 64 touchee : pages presentes", pg);
	u64 d = stats()[VS_FAULTS / 8] - faults0;
	verdict(d == N_TOUCH, "fautes a la demande (vm_stats)", (long)d);
	d = free0 - free_pages(); /* (+ tables : une L3 par 2 Mo touchés) */
	verdict(d <= N_TOUCH + (LAZY_SIZE >> 21) + 16, "pages reellement prises (pages libres)", (long)d);
	u64 bad = 0;
	for (u64 i = 0; i < N_TOUCH; i++)
		if (*(volatile u64 *)(r + i * STEP) != ((i + 1) | 0x5A5A0000))
			bad++;
	verdict(bad == 0, "contenu relu (erreurs)", (long)bad);
	u64 z = *(volatile u64 *)(r + 4096); /* jamais écrite */
	verdict(z == 0, "page jamais ecrite : lue a zero", (long)z);
	vm_unmap(r + LAZY_SIZE / 2, LAZY_SIZE / 2); /* la seconde moitié */
	q = query(r);
	pg = q_pages(q);
	verdict(q[1] == r + LAZY_SIZE / 2 && pg == N_TOUCH / 2 + 1, /* (+ la page lue à zéro) */
		"munmap de la moitie : region raccourcie, pages restantes", pg);
	vm_unmap(r + 32 * MO, 32 * MO); /* un trou au milieu */
	long res = sys(SYS_VM_QUERY, (long)(r + 32 * MO), (long)query_buf, 0, 0);
	verdict(res == 1 && query_buf[0] == r + 64 * MO, /* 1 : la suivante, au-dessus */
		"munmap au milieu : region coupee en deux (vm_query : la suivante)", res);
	res = vm_unmap(r, LAZY_SIZE / 2); /* tout */
	verdict(res == 0, "munmap du reste", res);
	long dd = (long)(free0 - free_pages()); /* (les tables L3 restent) */
	verdict(dd <= (long)(LAZY_SIZE >> 21) + 16, "pages rendues (ecart des pages libres)", dd);
}

/* --- grande réservation PROT_NONE, mprotect */
static void t_reserve(void)
{
	long r = vm_map(0, 8 * GO, PROT_NONE, 0);
	verdict(r > 0, "reservation de 8 Go PROT_NONE (plus que la RAM)", r);
	u64 m = (u64)r + 4 * GO; /* 1 Mo au milieu : lecture / écriture */
	long x = sys(SYS_VM_PROTECT, (long)m, MO, PROT_READ | PROT_WRITE, 0);
	verdict(x == 0, "mprotect lecture / ecriture d'1 Mo au milieu", x);
	for (u64 i = 0; i < 256; i++)
		*(volatile u64 *)(m + (i << 12) + 8) = i;
	u64 bad = 0;
	for (u64 i = 0; i < 256; i++)
		if (*(volatile u64 *)(m + (i << 12) + 8) != i)
			bad++;
	verdict(bad == 0, "ecriture et relecture dans la partie ouverte (erreurs)", (long)bad);
	u64 *q = query(m); /* coupée en trois : la sienne */
	verdict(q[0] == m && q[1] == m + MO && (u32)q[2] == (PROT_READ | PROT_WRITE),
		"region coupee en trois, droits de la partie ouverte", q_pages(q));
	sys(SYS_VM_PROTECT, (long)m, MO, PROT_READ, 0); /* lecture seule : la lecture marche */
	u64 v = *(volatile u64 *)(m + 8 + 5 * 4096);
	verdict(v == 5, "mprotect lecture seule : la lecture marche", (long)v);
	x = sys(SYS_GETRANDOM, r, 32, 0, 0); /* tampon PROT_NONE : refusé */
	verdict(x == E_FAULT, "tampon PROT_NONE donne au noyau : E_FAULT", x);
	x = sys(SYS_VM_UNMAP, (long)_start, 4096, 0, 0); /* hors de l'arène : E_INVAL */
	verdict(x == -22, "munmap hors de l'arene : E_INVAL", x);
	x = vm_unmap((u64)r, 8 * GO);
	verdict(x == 0, "munmap des 8 Go", x);
}

/* --- le noyau dans une page pas encore peuplée */
static void t_kernel(void)
{
	u64 m = (u64)vm_map(0, 65536, PROT_READ | PROT_WRITE, 0);
	long n = sys(SYS_GETRANDOM, (long)(m + 4 * 4096 - 128), 256, 0, 0); /* à cheval sur deux pages */
	long pi = pageinfo((void *)(m + 4 * 4096));
	verdict(n == 256 && (pi & PI_PRESENT) == PI_PRESENT,
		"le noyau ecrit dans des pages pas encore peuplees (getrandom)", n);
	vm_unmap(m, 65536);
}

/* --- madvise */
static void t_advise(void)
{
	u64 m = (u64)vm_map(0, MO, PROT_READ | PROT_WRITE, 0);
	for (u64 i = 0; i < 256; i++)
		*(volatile u64 *)(m + (i << 12)) = 0x55;
	sys(SYS_VM_ADVISE, (long)m, MO, MADV_DONTNEED, 0);
	u32 pg = q_pages(query(m));
	u64 v = *(volatile u64 *)(m + 4096);
	verdict(pg == 0 && v == 0, "MADV_DONTNEED : pages rendues, relues a zero", pg);
	sys(SYS_VM_ADVISE, (long)m, MO, MADV_WILLNEED, 0);
	pg = q_pages(query(m));
	verdict(pg == 256, "MADV_WILLNEED : pages peuplees", pg);
	vm_unmap(m, MO);
}

/* --- VM_FIXED, VM_FIXED_NOREPLACE */
static void t_fixed(void)
{
	long m = vm_map(0, 65536, PROT_READ | PROT_WRITE, 0);
	*(volatile u64 *)m = 7;
	long x = vm_map((u64)m, 65536, PROT_READ | PROT_WRITE, VM_FIXED_NOREPLACE);
	verdict(x == E_EXIST, "VM_FIXED_NOREPLACE sur une place prise : E_EXIST", x);
	x = vm_map((u64)m, 65536, PROT_READ | PROT_WRITE, VM_FIXED);
	u64 v = *(volatile u64 *)m;
	verdict(x == m && v == 0, "VM_FIXED : remplacee, a zero", (long)v);
	vm_unmap((u64)m, 65536);
}

/* arguments : lance « SD0:/bin/memtest <arguments> » -> son code */
static long child_code(const char *args)
{
	long pid = sys(SYS_SPAWN, (long)p_self, (long)args, 0, 0);
	if (pid < 0)
		return pid;
	return sys(SYS_WAIT, pid, 0, 0, 0);
}

/* --- mprotect : fils tués proprement */
static void t_protect(void)
{
	long c = child_code("child protect");
	verdict(c == -11, "fils qui ecrit apres PROT_READ : tue (code)", c);
	c = child_code("child none");
	verdict(c == -11, "fils qui lit une page PROT_NONE : tue (code)", c);
}

/* chemin -> son entrée dans la liste des images, 0 : absente */
static u8 *image_of(const char *path)
{
	long n = sys(SYS_IMAGE_LIST, (long)image_list, 16, 0, 0);
	for (u8 *e = image_list; n > 0; n--, e += IML_SIZE)
		if (str_ieq(path, (const char *)e + IML_PATH))
			return e;
	return 0;
}

/* références de l'image de SD0:/bin/memtest (0 : absente) */
static long refs_self(void)
{
	u8 *e = image_of(p_self);
	return e ? *(long *)(e + IML_REFS) : 0;
}

/* pages de code : de _start à la fin du premier segment, arrondie à la page
 * (start..fin_code de l'asm) */
static u64 code_pages(void)
{
	const u8 *ph = __ehdr_start + *(const u64 *)(__ehdr_start + 32); /* e_phoff */
	u64 end = *(const u64 *)(ph + 16) + *(const u64 *)(ph + 40);     /* p_vaddr + p_memsz */
	end = (end + 4095) & ~4095UL;
	return (end - (u64)_start) >> 12;
}

/* --- image préchargée : pages partagées, copie sur écriture */
static void t_cow(void)
{
	long pi = pageinfo((void *)_start); /* lancé depuis une image préchargée ? */
	verdict(pi & PI_SHARED, "lance depuis une image prechargee (code partage)", pi);
	if (!(pageinfo((void *)_start) & PI_SHARED)) {
		ak_puts("memtest : pas lance depuis une image prechargee (SD0:/etc/preload.txt) : copie sur ecriture non essayee\n");
		return;
	}
	pi = pageinfo(&cow_var); /* page de données jamais écrite */
	verdict((pi & (PI_COW | PI_SHARED)) == (PI_COW | PI_SHARED),
		"page de donnees jamais ecrite : partagee, en copie sur ecriture", pi);
	sys(SYS_SLEEP, 100, 0, 0, 0); /* (les fils de t_protect : le faucheur rend leur référence après SYS_WAIT) */
	long refs0 = refs_self();
	long a = sys(SYS_SPAWN, (long)p_self, (long)"child cow", 0, 0); /* deux fils en même temps */
	long b = sys(SYS_SPAWN, (long)p_self, (long)"child cow", 0, 0);
	sys(SYS_SLEEP, 150, 0, 0, 0);
	long d = refs_self() - refs0;
	verdict(d == 2, "references de l'image : +2 avec deux fils", d);
	long c = sys(SYS_WAIT, a, 0, 0, 0);
	verdict(c == 0, "fils 1 : sa valeur a lui seul (code : bits des echecs)", c);
	c = sys(SYS_WAIT, b, 0, 0, 0);
	verdict(c == 0, "fils 2 : sa valeur a lui seul (code : bits des echecs)", c);
	long r = refs_self();
	verdict(r == refs0, "references rendues a la fin des fils", r);
	u64 v = cow_var; /* le père : la valeur d'origine */
	verdict(v == COW_INIT, "le pere voit toujours la valeur d'origine", (long)v);
	u64 sh = (u64)child_code("child stats") >> 20; /* (pages partagées << 20 | à lui) */
	verdict(sh >= code_pages(), "fils : pages partagees, pas copiees (au moins le code)", (long)sh);
}

/* (thread) rang : écrit son mot dans chaque page de la région commune (tous
 * en même temps : fautes sur les mêmes pages), puis TH_CYCLES cycles map
 * 256 Ko / touche / vérifie / unmap -> code = erreurs */
THREAD_ATTR static void w_core(long rank)
{
	u8 *c = common;
	for (u64 p = 0; p < TH_PAGES; p++)
		*(volatile u64 *)(c + (p << 12) + rank * 8) = ((u64)(rank + 1) << 32) | p;
	long bad = 0;
	for (int cy = 0; cy < TH_CYCLES; cy++) {
		long m = vm_map(0, 256 * 1024, PROT_READ | PROT_WRITE, 0);
		if (m < 0) {
			bad++;
			continue;
		}
		for (u64 i = 0; i < 64; i++)
			*(volatile u64 *)(m + (i << 12) + 16) = i + rank;
		for (u64 i = 0; i < 64; i++)
			if (*(volatile u64 *)(m + (i << 12) + 16) != i + rank)
				bad++;
		if (vm_unmap((u64)m, 256 * 1024))
			bad++;
	}
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, bad);
}

/* --- plusieurs cœurs : fautes concurrentes, map / unmap */
static void t_cores(void)
{
	u64 free0 = free_pages();
	common = (u8 *)vm_map(0, TH_PAGES * 4096UL, PROT_READ | PROT_WRITE, 0);
	for (long i = 0; i < N_TH; i++)
		tids[i] = korin_sys6(SYS_THREAD_CREATE_EX, (long)w_core, (long)(stacks + (i + 1) * STK), i, 0,
				     THREAD_PRIO_NORMAL, (long)"memtest-coeur");
	long errs = 0; /* erreurs des cycles privés */
	for (long i = 0; i < N_TH; i++)
		errs += sys(SYS_THREAD_JOIN, tids[i], 0, 0, 0);
	u8 *c = common; /* chaque mot de chaque page */
	long bad = 0;
	for (u64 p = 0; p < TH_PAGES; p++)
		for (u64 t = 0; t < N_TH; t++)
			if (*(volatile u64 *)(c + (p << 12) + t * 8) != (((t + 1) << 32) | p))
				bad++;
	verdict(bad == 0, "4 threads : fautes en meme temps sur les memes pages, contenu juste", bad);
	u32 pg = q_pages(query((u64)c));
	verdict(pg == TH_PAGES, "pas de page en double (pages presentes de la region commune)", pg);
	verdict(errs == 0, "4 x 50 cycles map / touche / verifie / unmap (erreurs)", errs);
	vm_unmap((u64)c, TH_PAGES * 4096UL);
	long d = (long)(free0 - free_pages());
	verdict(d <= 64, "rien ne fuit (ecart des pages libres)", d);
}

/* --- fichier changé : image oubliée */
static void t_forget(void)
{
	if (!image_of(p_hello)) {
		ak_puts("memtest : SD0:/bin/hello pas precharge (deja oublie ?) : essai du fichier change saute\n");
		return;
	}
	long fd = sys(SYS_OPEN, (long)p_hello, O_WRITE, 0, 0); /* ouvert en écriture, rien d'écrit */
	if (fd >= 0)
		sys(SYS_CLOSE, fd, 0, 0, 0);
	verdict(image_of(p_hello) == 0, "SD0:/bin/hello ouvert en ecriture : image oubliee (vfs)", fd);
	long c = sys(SYS_SPAWN, (long)p_hello, 0, 0, 0); /* lancé quand même (lu sur la carte) */
	if (c >= 0)
		c = sys(SYS_WAIT, c, 0, 0, 0);
	verdict(c == 0, "hello se lance quand meme (lu sur la carte)", c);
}

NORETURN static void sys_exit(long code)
{
	for (;;)
		korin_sys1(SYS_EXIT, code);
}

/* --- fils */

/* écrit dans une page lecture / écriture devenue lecture seule : doit être tué */
NORETURN static void child_protect(void)
{
	volatile u64 *p = (volatile u64 *)vm_map(0, 4096, PROT_READ | PROT_WRITE, 0);
	*p = 1;
	sys(SYS_VM_PROTECT, (long)p, 4096, PROT_READ, 0);
	*p = *p + 1; /* faute ici */
	sys_exit(0); /* (pas tué : 0, compte comme un échec) */
}

/* lit une page PROT_NONE : doit être tué */
NORETURN static void child_none(void)
{
	volatile u64 *p = (volatile u64 *)vm_map(0, 4096, PROT_NONE, 0);
	(void)*p; /* faute ici */
	sys_exit(0);
}

/* copie sur écriture : code = bits des échecs (0 : tout va bien) */
NORETURN static void child_cow(void)
{
	long bits = 0;
	if ((pageinfo(&cow_var) & (PI_COW | PI_SHARED)) != (PI_COW | PI_SHARED))
		bits |= 1; /* partagée, en copie sur écriture */
	if (cow_var != COW_INIT)
		bits |= 2; /* valeur d'origine */
	long pid = sys(SYS_GETPID, 0, 0, 0, 0); /* son pid dans la variable */
	cow_var = (u64)pid;
	long pi = pageinfo(&cow_var); /* à lui, inscriptible */
	if (pi & (PI_COW | PI_SHARED))
		bits |= 4;
	if (!(pi & PI_WRITE))
		bits |= 4;
	sys(SYS_SLEEP, 300, 0, 0, 0); /* l'autre fils écrit pendant ce temps */
	if (cow_var != (u64)pid)
		bits |= 8;
	if (!(pageinfo((void *)_start) & PI_SHARED))
		bits |= 16; /* code partagé */
	if (!stats()[VS_COWS / 8])
		bits |= 32; /* au moins une copie */
	sys_exit(bits);
}

/* code = pages partagées << 20 | pages à lui */
NORETURN static void child_stats(void)
{
	u64 *s = stats();
	sys_exit((long)(s[VS_OWN / 8] | (s[VS_SHARED / 8] << 20)));
}

NORETURN static void child(const char *a)
{
	a += 6; /* après « child » */
	if (prefix(a, "cow"))
		child_cow();
	if (prefix(a, "protect"))
		child_protect();
	if (prefix(a, "none"))
		child_none();
	if (prefix(a, "stats"))
		child_stats();
	sys_exit(99);
}

/* --- bench : temps de lancement */

/* chemin : lancé sans argument, sa sortie lue jusqu'à la fin, puis attendu */
static void run_read(const char *path)
{
	long pid = sys(SYS_SPAWN_PIPE, (long)path, 0, (long)&pipe_fd, 0);
	if (pid < 0)
		return;
	for (;;) {
		long n = sys(SYS_READ, pipe_fd, (long)pipe_buf, 256, 0);
		if (n == -11) {
			sys(SYS_SLEEP, 1, 0, 0, 0);
			continue;
		}
		if (n > 0)
			continue;
		sys(SYS_CLOSE, pipe_fd, 0, 0, 0);
		sys(SYS_WAIT, pid, 0, 0, 0);
		return;
	}
}

/* p_fasm -> p_copy (tampon de VM_MAP) -> octets copiés (ou l'erreur) */
static long copy(void)
{
	long buf = vm_map(0, COPY_BUF, PROT_READ | PROT_WRITE, 0);
	long in = sys(SYS_OPEN, (long)p_fasm, O_READ, 0, 0);
	if (in < 0)
		return in;
	long res;
	long out = sys(SYS_OPEN, (long)p_copy, O_WRITE | O_CREATE | O_TRUNC, 0, 0);
	if (out < 0) {
		res = out;
	} else {
		res = 0;
		for (;;) {
			long n = sys(SYS_READ, in, buf, COPY_BUF, 0);
			if (n < 0) {
				res = n;
				break;
			}
			if (n == 0)
				break;
			long w = sys(SYS_WRITE, out, buf, n, 0);
			if (w <= 0) {
				res = w; /* (erreur d'écriture : montrée) */
				break;
			}
			res += w;
		}
		sys(SYS_CLOSE, out, 0, 0, 0);
	}
	sys(SYS_CLOSE, in, 0, 0, 0);
	vm_unmap((u64)buf, COPY_BUF);
	return res;
}

/* dixièmes : « 12,3 » */
static void tenths(u64 v)
{
	print_dec((long)(v / 10));
	ak_puts(",");
	print_dec((long)(v % 10));
}

NORETURN static void bench(const char *a)
{
	u64 n = number(a + 5); /* nombre de lancements (10) */
	if (!n)
		n = 10;
	u8 *e = image_of(p_fasm); /* préchargé ? */
	ak_puts("bench : SD0:/bin/fasmarm ");
	if (e) {
		print_dec((long)(*(u64 *)(e + IML_FSIZE) >> 10));
		ak_puts(" Ko, precharge\n");
	} else {
		ak_puts("PAS precharge (preload.txt)\n");
	}
	sys(SYS_MKDIR, (long)"SD0:/tmp", 0, 0, 0); /* la copie, non préchargée */
	long c = copy();
	ak_puts("bench : copie non prechargee SD0:/tmp/fasmarm.copy, octets ");
	print_dec(c);
	ak_puts("\n");
	run_read(p_fasm); /* une fois chacun (caches chauds) */
	run_read(p_copy);
	u64 t1 = (u64)sys(SYS_TIME_MS, 0, 0, 0, 0);
	for (u64 i = 0; i < n; i++)
		run_read(p_fasm);
	t1 = (u64)sys(SYS_TIME_MS, 0, 0, 0, 0) - t1;
	u64 t2 = (u64)sys(SYS_TIME_MS, 0, 0, 0, 0);
	for (u64 i = 0; i < n; i++)
		run_read(p_copy);
	t2 = (u64)sys(SYS_TIME_MS, 0, 0, 0, 0) - t2;
	ak_puts("bench : precharge ");
	print_dec((long)t1);
	ak_puts(" ms, copie lue sur la carte ");
	print_dec((long)t2);
	ak_puts(" ms, pour ");
	print_dec((long)n);
	ak_puts(" lancements (spawn + sortie + wait) ; par lancement : ");
	tenths(t1 * 10 / n);
	ak_puts(" ms contre ");
	tenths(t2 * 10 / n);
	ak_puts(" ms\n");
	sys(SYS_UNLINK, (long)p_copy, 0, 0, 0);
	sys_exit(t1 >= t2); /* code 0 : le préchargé va plus vite */
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *a = krt_line;
	if (a) {
		if (prefix(a, "child "))
			child(a);
		if (prefix(a, "bench"))
			bench(a);
	}
	ak_puts("memtest : reservations, pagination a la demande, munmap, mprotect, copie sur ecriture, 4 coeurs\n");
	t_lazy();
	t_reserve();
	t_kernel();
	t_advise();
	t_fixed();
	t_protect();
	t_cow();
	t_cores();
	t_forget();
	ak_puts("memtest : ");
	print_dec(n_pass);
	ak_puts(" PASS, ");
	print_dec(n_fail);
	ak_puts(" FAIL\n");
	sys_exit(n_fail);
}
