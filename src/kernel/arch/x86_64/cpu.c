/* Cœurs x86-64 : structure de chaque cœur (GS), syscall/sysret, PAT, NX, FPU,
 * interruptions, piles et bascule des threads.
 *
 * État FPU des threads : XSAVE si le processeur l'a (x87, SSE et AVX : XCR0 = 7 ; AVX-512 avec
 * l'option « avx512 » de boot.cfg), sinon FXSAVE (512 octets). Une zone par thread, de la taille
 * que donne CPUID 0xD, alignée sur 64 ; XSAVEOPT aux bascules quand il est là (seuls les registres
 * changés depuis le dernier XRSTOR de cette zone sont écrits). AVX-512 reste éteint par défaut :
 * aucun programme de Korin ne s'en sert encore (JSC, Skia, libvpx, dav1d n'ont pas de chemin
 * AVX-512 construit), et il porterait la zone à 2,7 Ko par thread et la trame d'un signal au-delà
 * de MINSIGSTKSZ (2048 : une pile de secours de cette taille déborderait). */
#include <korin/arch.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/sched.h>

#include "cpu.h"

struct cpu cpus[MAX_CPUS];
static volatile unsigned online_count;
static uint64_t bsp_efer;

#define XCR0_X87 1ULL
#define XCR0_SSE 2ULL
#define XCR0_AVX 4ULL
#define XCR0_AVX512 0xE0ULL /* opmask, ZMM_Hi256, Hi16_ZMM */

/* état d'architecture d'un thread : zone FXSAVE / XSAVE (alignée sur 64) et base de FS */
struct arch_thread {
	uint8_t *fpu;
	void *fpu_mem; /* (kzalloc : fpu en est l'adresse alignée) */
	uint64_t fs_base;
};

uint64_t fpu_xcr0;          /* composantes sauvées par XSAVE ; 0 : FXSAVE seulement */
uint32_t fpu_size = 512;    /* taille de la zone */
static int fpu_opt;         /* XSAVEOPT */
int fpu_want_avx512;        /* option « avx512 » (pc_main) */
int fpu_no_xsave;           /* option « noxsave » : FXSAVE seulement, sans AVX (pc_main) */
int cpu_no_smep, cpu_no_umip; /* options « nosmep », « noumip » (pc_main : essais, retour en arrière) */
static uint64_t protect_cr4;  /* SMEP, UMIP : les bits de CR4 trouvés sur le cœur 0 (cpu_protect) */
static uint8_t *fpu_init_area; /* état initial (XSTATE_BV = 0) : threads neufs, exec */

static inline void xsetbv0(uint64_t v)
{
	__asm__ volatile("xsetbv" ::"c"(0), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

/* le registre FPU courant -> zone a ; full : XSAVE même si XSAVEOPT est là (zone d'un autre
 * thread, trame d'un signal : XSAVEOPT n'y vaut que pour la zone du dernier XRSTOR) */
void fpu_save(uint8_t *a, int full)
{
	uint32_t lo = (uint32_t)fpu_xcr0, hi = (uint32_t)(fpu_xcr0 >> 32);
	if (!fpu_xcr0)
		__asm__ volatile("fxsave64 (%0)" ::"r"(a) : "memory");
	else if (fpu_opt && !full)
		__asm__ volatile("xsaveopt64 (%0)" ::"r"(a), "a"(lo), "d"(hi) : "memory");
	else
		__asm__ volatile("xsave64 (%0)" ::"r"(a), "a"(lo), "d"(hi) : "memory");
}

void fpu_restore(const uint8_t *a)
{
	uint32_t lo = (uint32_t)fpu_xcr0, hi = (uint32_t)(fpu_xcr0 >> 32);
	if (!fpu_xcr0)
		__asm__ volatile("fxrstor64 (%0)" ::"r"(a) : "memory");
	else
		__asm__ volatile("xrstor64 (%0)" ::"r"(a), "a"(lo), "d"(hi) : "memory");
}

/* (cœur 0) ce que XSAVE sauvera, et la taille de la zone */
static void fpu_detect(void)
{
	uint32_t r[4], d[4];
	cpuid(0, 0, r);
	if (r[0] < 0xD || fpu_no_xsave)
		return;
	cpuid(1, 0, r);
	if (!((r[2] >> 26) & 1)) /* XSAVE */
		return;
	int avx = (r[2] >> 28) & 1;
	cpuid(0xD, 0, d);
	uint64_t sup = d[0] | ((uint64_t)d[3] << 32);
	uint64_t want = XCR0_X87 | XCR0_SSE;
	if (avx && (sup & XCR0_AVX))
		want |= XCR0_AVX;
	cpuid(7, 0, r);
	if ((want & XCR0_AVX) && fpu_want_avx512 && ((r[1] >> 16) & 1) && (sup & XCR0_AVX512) == XCR0_AVX512)
		want |= XCR0_AVX512;
	uint32_t size = 576; /* zone FXSAVE + en-tête */
	for (int i = 2; i < 64; i++)
		if (want & (1ULL << i)) {
			cpuid(0xD, (uint32_t)i, d); /* eax : taille, ebx : décalage */
			if (d[1] + d[0] > size)
				size = d[1] + d[0];
		}
	cpuid(0xD, 1, d);
	fpu_opt = d[0] & 1;
	fpu_xcr0 = want;
	fpu_size = (size + 63) & ~63U;
}

static void fpu_set_initial(uint8_t *a)
{
	memset(a, 0, fpu_size);
	*(uint16_t *)(a + 0) = 0x037F;  /* FCW : exceptions masquées */
	*(uint32_t *)(a + 24) = 0x1F80; /* MXCSR : idem (XRSTOR le lit même si XSTATE_BV = 0) */
}

void arch_fpu_info(uint64_t *xcr0, uint32_t *size, int *opt)
{
	*xcr0 = fpu_xcr0;
	*size = fpu_size;
	*opt = fpu_opt;
}

void syscall_entry(void);
void thread_trampoline(void);
void context_switch(uint64_t *save_sp, uint64_t new_sp);

unsigned arch_cpu_id(void)
{
	uint64_t id;
	__asm__ volatile("mov %%gs:16, %0" : "=r"(id));
	return (unsigned)id;
}

unsigned arch_cpu_count(void)
{
	return online_count;
}

void smp_tlb_poll(void); /* (arch/pc/smp.c) */

/* attente active (verrous tournants…) : répond aussi aux invalidations de TLB demandées à ce
 * cœur, qui attend peut-être les interruptions coupées (sinon : étreinte fatale avec celui qui
 * invalide en tenant un verrou, mm_fork) */
void arch_cpu_relax(void)
{
	pause();
	smp_tlb_poll();
}

struct cpu *cpu_get(unsigned idx)
{
	return &cpus[idx];
}

void cpu_init(unsigned idx, uint32_t apic_id)
{
	struct cpu *c = &cpus[idx];
	c->index = idx;
	c->apic_id = apic_id;
	wrmsr(MSR_GS_BASE, (uint64_t)c);
	wrmsr(MSR_KERNEL_GS_BASE, 0);

	if (idx == 0)
		bsp_efer = rdmsr(MSR_EFER);
	/* SCE : syscall ; NXE comme le cœur 0 (réglé par paging_init) */
	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1 | (bsp_efer & (1ULL << 11)));
	/* PAT : la même que le cœur 0 (paging_init) ; sans elle, l'entrée 1 d'un autre cœur reste
	 * write-through et chaque écriture à l'écran y est lente (0,25 s par défilement sur le ZBook) */
	wrmsr(MSR_PAT, KORIN_PAT);
	/* sysret : CS = 0x10 + 16, SS = 0x10 + 8 (| 3) ; syscall : CS 0x08, SS 0x10 */
	wrmsr(MSR_STAR, (0x10ULL << 48) | (0x08ULL << 32));
	wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
	wrmsr(MSR_FMASK, 0x44700); /* IF, TF, DF, NT, AC coupés à l'entrée (NT : iretq ferait #GP) */

	/* FPU et SSE pour les process : CR0.EM éteint, MP et NE ; CR4.OSFXSR, OSXMMEXCPT */
	uint64_t cr0, cr4;
	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
	/* CD (30) et NW (29) éteints : le trampoline des cœurs secondaires part de 0x60000010 et n'ajoute
	 * que PG et PE, d'où des caches coupés sur ces cœurs ; WP (16) : le noyau respecte la lecture seule
	 * des pages des programmes (ses écritures voulues passent par la carte physique, ou par
	 * copy_to_user qui casse d'abord la copie sur écriture) */
	cr0 = (cr0 & ~((1ULL << 30) | (1ULL << 29) | (1ULL << 2))) | (1ULL << 16) | (1ULL << 1) | (1ULL << 5);
	__asm__ volatile("mov %0, %%cr0" ::"r"(cr0));
	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= (1ULL << 9) | (1ULL << 10);
	if (idx == 0)
		fpu_detect();
	if (fpu_xcr0)
		cr4 |= 1ULL << 18; /* OSXSAVE : XSAVE, XGETBV et AVX permis */
	__asm__ volatile("mov %0, %%cr4" ::"r"(cr4));
	if (fpu_xcr0)
		xsetbv0(fpu_xcr0);
	__asm__ volatile("fninit");
	if (idx)
		cpu_protect(idx); /* (le cœur 0 : après paging_init, pc_main) */
}

/* SMEP (CR4.20) : le noyau ne peut plus exécuter une page d'un process ; UMIP (CR4.11) : sgdt, sidt, sldt, smsw,
 * str interdits aux process (ils donnaient les adresses des tables du noyau). Étape 0 de
 * docs/etude/DURCISSEMENT-N3-N5.md (N4). Le cœur 0 : après paging_init (les tables de l'UEFI peuvent avoir le bit U) ;
 * les autres : à la fin de cpu_init. La faute de page d'une violation : idt.c (sans elle, une boucle sans fin). */
void cpu_protect(unsigned idx)
{
	if (idx == 0) {
		uint32_t r[4];
		cpuid(0, 0, r);
		if (r[0] >= 7) {
			cpuid(7, 0, r);
			if ((r[1] & (1u << 7)) && !cpu_no_smep)
				protect_cr4 |= 1ULL << 20;
			if ((r[2] & (1u << 2)) && !cpu_no_umip)
				protect_cr4 |= 1ULL << 11;
		}
		kprintf("cpu : SMEP %s, UMIP %s\n", (protect_cr4 >> 20) & 1 ? "oui" : "non",
			(protect_cr4 >> 11) & 1 ? "oui" : "non");
	}
	if (protect_cr4) {
		uint64_t cr4;
		__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
		__asm__ volatile("mov %0, %%cr4" ::"r"(cr4 | protect_cr4));
	}
}

void cpu_set_online(unsigned idx)
{
	cpus[idx].online = 1;
	__atomic_add_fetch(&online_count, 1, __ATOMIC_SEQ_CST);
}

uint64_t arch_irq_save(void)
{
	uint64_t f;
	__asm__ volatile("pushfq; pop %0; cli" : "=r"(f)::"memory");
	return f;
}

void arch_irq_restore(uint64_t f)
{
	if (f & 0x200)
		sti();
}

void arch_set_tls(uint64_t v)
{
	struct thread *t = sched_current();
	((struct arch_thread *)t->arch_state)->fs_base = v;
	wrmsr(MSR_FS_BASE, v);
}

uint64_t arch_get_tls(void)
{
	return rdmsr(MSR_FS_BASE);
}

/* aléa du processeur : RDSEED (entropie directe) s'il existe, sinon RDRAND ; chacun réessayé (le drapeau
 * CF à 0 veut dire « pas encore prêt », pas « absent ») -> 1, 0 : rien (N14, E44) */
int arch_hw_random(uint64_t *v)
{
	uint32_t r[4];
	unsigned char ok = 0;
	cpuid(0, 0, r);
	if (r[0] >= 7) {
		cpuid(7, 0, r);
		if ((r[1] >> 18) & 1) /* RDSEED */
			for (int i = 0; i < 16 && !ok; i++)
				__asm__ volatile("rdseed %0; setc %1" : "=r"(*v), "=qm"(ok));
		if (ok)
			return 1;
	}
	cpuid(1, 0, r);
	if (!((r[2] >> 30) & 1))
		return 0;
	for (int i = 0; i < 10 && !ok; i++)
		__asm__ volatile("rdrand %0; setc %1" : "=r"(*v), "=qm"(ok));
	return ok;
}

uint64_t arch_cycles(void)
{
	return rdtsc();
}

int arch_elf_machine(void)
{
	return 0x3E; /* EM_X86_64 */
}

/* zone de fpu_size octets alignée sur 64 (le tas aligne sur 16) -> adresse ; *mem : à rendre */
uint8_t *fpu_area_alloc(void **mem)
{
	uint8_t *m = kzalloc(fpu_size + 64);
	*mem = m;
	return m ? (uint8_t *)(((uint64_t)m + 63) & ~63ULL) : 0;
}

/* état FPU d'un thread : l'état initial */
int arch_thread_init(struct thread *t)
{
	struct arch_thread *a = kzalloc(sizeof *a);
	if (!a)
		return -1;
	a->fpu = fpu_area_alloc(&a->fpu_mem);
	if (!a->fpu) {
		kfree(a);
		return -1;
	}
	if (!fpu_init_area) { /* (le premier thread : le cœur 0, après fpu_detect) */
		void *mem;
		uint8_t *z = fpu_area_alloc(&mem);
		if (z)
			fpu_set_initial(z);
		fpu_init_area = z;
	}
	fpu_set_initial(a->fpu);
	t->arch_state = a;
	return 0;
}

void arch_thread_free(struct thread *t)
{
	struct arch_thread *a = t->arch_state;
	if (a)
		kfree(a->fpu_mem);
	kfree(a);
	t->arch_state = 0;
}

void arch_thread_setup(struct thread *t, void (*fn)(void *), void *arg)
{
	uint64_t *sp = (uint64_t *)t->kstack_top;
	*--sp = (uint64_t)thread_trampoline;
	*--sp = 0;              /* rbx */
	*--sp = 0;              /* rbp */
	*--sp = (uint64_t)fn;   /* r12 */
	*--sp = (uint64_t)arg;  /* r13 */
	*--sp = 0;              /* r14 */
	*--sp = 0;              /* r15 */
	t->arch_sp = (uint64_t)sp;
}

void fork_trampoline(void);

/* fils de fork : il repart de la trame de l'appel du père, rax = 0, avec la
 * même FPU et le même registre de thread */
void arch_fork_setup(struct thread *child, const void *parent_frame)
{
	struct regs *r = (struct regs *)(child->kstack_top - sizeof(struct regs));
	memcpy(r, parent_frame, sizeof *r);
	r->rax = 0;
	struct arch_thread *ca = child->arch_state;
	fpu_save(ca->fpu, 1);
	ca->fs_base = rdmsr(MSR_FS_BASE);
	uint64_t *sp = (uint64_t *)r;
	*--sp = (uint64_t)fork_trampoline;
	*--sp = 0;            /* rbx */
	*--sp = 0;            /* rbp */
	*--sp = (uint64_t)r;  /* r12 : la trame */
	*--sp = 0;            /* r13 */
	*--sp = 0;            /* r14 */
	*--sp = 0;            /* r15 */
	child->arch_sp = (uint64_t)sp;
}

/* exec : registres du programme neuf (entrée, pile, argument), FPU neuve */
void arch_exec_frame(void *frame, uint64_t entry, uint64_t sp, uint64_t arg)
{
	struct regs *r = frame;
	uint64_t vec = r->vector;
	memset(r, 0, sizeof *r);
	r->vector = vec;
	r->rip = entry;
	r->rsp = sp;
	r->rdi = arg;
	r->rflags = 0x202;
	r->cs = 0x23;
	r->ss = 0x1B;
	if (fpu_xcr0 && fpu_init_area) {
		fpu_restore(fpu_init_area); /* tout à l'état initial, YMM compris */
	} else {
		__asm__ volatile("fninit");
		uint32_t mxcsr = 0x1F80;
		__asm__ volatile("ldmxcsr %0" ::"m"(mxcsr));
	}
	arch_set_tls(0);
}

void arch_context_switch(struct thread *prev, struct thread *next)
{
	/* le noyau n'utilise pas la FPU (-mgeneral-regs-only) : on sauve celle du
	 * process à chaque bascule */
	struct arch_thread *pa = prev->arch_state, *na = next->arch_state;
	fpu_save(pa->fpu, 0);
	fpu_restore(na->fpu);
	pa->fs_base = rdmsr(MSR_FS_BASE);
	if (na->fs_base != pa->fs_base)
		wrmsr(MSR_FS_BASE, na->fs_base);
	cpus[arch_cpu_id()].kstack_top = next->kstack_top;
	tss_set_rsp0(next->kstack_top);
	context_switch(&prev->arch_sp, next->arch_sp);
}
