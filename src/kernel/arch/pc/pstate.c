/* Fréquence des cœurs du PC : P-states et fréquence effective (docs/etude/PC-PILOTES.md,
 * « Fréquence du processeur »).
 *
 * Sans pilote, le micrologiciel laisse souvent le processeur à une fréquence basse : sur le ZBook
 * (i7-11800H, Tiger Lake), ni turbo (4,6 GHz) ni montée rapide. Sur chaque cœur, à son démarrage :
 *   * HWP (CPUID 6 : EAX bit 7) : IA32_PM_ENABLE (0x770) = 1, puis IA32_HWP_REQUEST (0x774) :
 *     minimum et maximum = Lowest et Highest de IA32_HWP_CAPABILITIES (0x771), Desired = 0 (le
 *     processeur choisit), EPP = 0 (performance) s'il est annoncé (EAX bit 10), sinon
 *     IA32_ENERGY_PERF_BIAS (0x1B0) = 0 (ECX bit 3) ;
 *   * sans HWP, avec EIST (CPUID 1 : ECX bit 7) : EIST et turbo permis dans IA32_MISC_ENABLE (0x1A0,
 *     s'il n'est pas verrouillé), puis IA32_PERF_CTL (0x199) au rapport le plus haut (turbo d'un cœur
 *     de MSR_TURBO_RATIO_LIMIT 0x1AD, sinon le maximum garanti de MSR_PLATFORM_INFO 0xCE) ;
 *   * sinon rien. Option « pstate=balanced » de boot.cfg : EPP 0x80 (équilibré) ; « pstate=off » :
 *     rien n'est touché.
 * Seulement chez Intel (famille 6), et chaque MSR est lu ou écrit « sûr » : une #GP (QEMU, un MSR
 * absent) est rattrapée (idt.c), le MSR est alors laissé.
 * Fréquence effective : APERF / MPERF (0xE8 / 0xE7, CPUID 6 : ECX bit 0), chaque seconde sur
 * chaque cœur (minuterie) : MHz = TSC × ΔAPERF / ΔMPERF (la fréquence moyenne quand le cœur
 * travaille) ; CPU_LOAD la rend (32 octets par cœur). Au démarrage, le cœur 0 tourne 50 ms et le
 * journal dit la fréquence obtenue. */
#include <korin/arch.h>
#include <korin/lib.h>

#include "../x86_64/cpu.h"
#include "apic.h"

#define MSR_MPERF 0xE7
#define MSR_APERF 0xE8
#define MSR_PLATFORM_INFO 0xCE
#define MSR_PERF_CTL 0x199
#define MSR_MISC_ENABLE 0x1A0
#define MSR_TURBO_RATIO 0x1AD
#define MSR_ENERGY_PERF_BIAS 0x1B0
#define MSR_PM_ENABLE 0x770
#define MSR_HWP_CAPS 0x771
#define MSR_HWP_REQUEST 0x774

int pstate_mode; /* 0 : performance, 1 : équilibré, 2 : rien (pc_main, option pstate=) */

static struct pcpu {
	int aperf;            /* APERF/MPERF lisibles */
	uint64_t a, m, t;     /* dernière mesure */
	uint32_t tick;        /* ms depuis la dernière mesure */
	volatile uint32_t mhz; /* fréquence effective de la dernière seconde (0 : inconnue) */
} pc[MAX_CPUS];

static int intel;
static uint32_t max_mhz; /* plus haut demandé (HWP Highest ou rapport de PERF_CTL) × 100 */

static volatile int probing[MAX_CPUS], faulted[MAX_CPUS];

/* (idt.c) une #GP du noyau sur rdmsr / wrmsr pendant une lecture « sûre » : sautée -> 1 */
int msr_fault_fixup(struct regs *r)
{
	unsigned c = arch_cpu_id();
	const uint8_t *ip = (const uint8_t *)r->rip;
	if (!probing[c] || ip[0] != 0x0F || (ip[1] != 0x32 && ip[1] != 0x30))
		return 0;
	faulted[c] = 1;
	r->rip += 2;
	return 1;
}

static int rdmsr_safe(uint32_t msr, uint64_t *v)
{
	uint64_t f = arch_irq_save();
	unsigned c = arch_cpu_id();
	faulted[c] = 0;
	probing[c] = 1;
	uint64_t x = rdmsr(msr);
	probing[c] = 0;
	int bad = faulted[c];
	arch_irq_restore(f);
	if (!bad)
		*v = x;
	return bad ? -1 : 0;
}

static int wrmsr_safe(uint32_t msr, uint64_t v)
{
	uint64_t f = arch_irq_save();
	unsigned c = arch_cpu_id();
	faulted[c] = 0;
	probing[c] = 1;
	wrmsr(msr, v);
	probing[c] = 0;
	int bad = faulted[c];
	arch_irq_restore(f);
	return bad ? -1 : 0;
}

/* sur chaque cœur (le cœur 0 après la minuterie, les autres à leur démarrage) */
void pstate_init_cpu(unsigned idx)
{
	uint32_t r[4];
	cpuid(0, 0, r);
	intel = r[1] == 0x756E6547 && r[3] == 0x49656E69 && r[2] == 0x6C65746E; /* GenuineIntel */
	uint32_t max_leaf = r[0];
	if (max_leaf < 6)
		return;
	cpuid(6, 0, r);
	uint32_t eax6 = r[0], ecx6 = r[2];
	uint64_t v;
	if ((ecx6 & 1) && rdmsr_safe(MSR_APERF, &v) == 0 && rdmsr_safe(MSR_MPERF, &v) == 0)
		pc[idx].aperf = 1;
	if (!intel || pstate_mode == 2)
		return;
	uint32_t epp = pstate_mode == 1 ? 0x80 : 0;
	if ((eax6 >> 7) & 1) { /* HWP */
		uint64_t caps;
		if (wrmsr_safe(MSR_PM_ENABLE, 1) || rdmsr_safe(MSR_HWP_CAPS, &caps))
			return;
		uint32_t highest = caps & 0xFF, lowest = (caps >> 24) & 0xFF;
		uint64_t req = lowest | (highest << 8); /* Desired 0 : le processeur choisit */
		if ((eax6 >> 10) & 1)
			req |= (uint64_t)epp << 24;
		else if ((ecx6 >> 3) & 1)
			wrmsr_safe(MSR_ENERGY_PERF_BIAS, pstate_mode == 1 ? 6 : 0);
		if (wrmsr_safe(MSR_HWP_REQUEST, req) == 0 && idx == 0) {
			max_mhz = highest * 100;
			kprintf("CPU : HWP actif, performances %u à %u (garantie %lu, efficace %lu), EPP %s\n", lowest,
				highest, (caps >> 8) & 0xFF, (caps >> 16) & 0xFF,
				(eax6 >> 10) & 1 ? (epp ? "0x80 (équilibré)" : "0 (performance)") : "absent (EPB)");
		}
		return;
	}
	cpuid(1, 0, r);
	if (!((r[2] >> 7) & 1)) { /* pas d'EIST */
		if (idx == 0)
			kprintf("CPU : ni HWP ni EIST : fréquence laissée au micrologiciel\n");
		return;
	}
	uint64_t misc;
	if (rdmsr_safe(MSR_MISC_ENABLE, &misc) == 0) {
		uint64_t want = (misc | (1ULL << 16)) & ~(1ULL << 38); /* EIST oui, turbo permis */
		if (want != misc)
			wrmsr_safe(MSR_MISC_ENABLE, want); /* (verrouillé : refusé, tant pis) */
	}
	uint64_t plat, turbo;
	uint32_t ratio = 0;
	if (rdmsr_safe(MSR_PLATFORM_INFO, &plat) == 0)
		ratio = (plat >> 8) & 0xFF;
	if (pstate_mode == 0 && rdmsr_safe(MSR_TURBO_RATIO, &turbo) == 0 && (turbo & 0xFF) > ratio)
		ratio = turbo & 0xFF;
	if (ratio && wrmsr_safe(MSR_PERF_CTL, (uint64_t)ratio << 8) == 0 && idx == 0) {
		max_mhz = ratio * 100;
		kprintf("CPU : P-states par IA32_PERF_CTL, rapport %u (%u MHz)\n", ratio, ratio * 100);
	} else if (idx == 0)
		kprintf("CPU : EIST sans rapport lisible : fréquence laissée au micrologiciel\n");
}

static void sample(struct pcpu *p, int update)
{
	uint64_t a, m;
	if (rdmsr_safe(MSR_APERF, &a) || rdmsr_safe(MSR_MPERF, &m)) {
		p->aperf = 0;
		return;
	}
	uint64_t t = rdtsc();
	uint64_t da = a - p->a, dm = m - p->m;
	uint64_t hz = arch_counter_hz();
	if (update && dm && hz)
		p->mhz = (uint32_t)((hz / 1000) * da / dm / 1000);
	p->a = a;
	p->m = m;
	p->t = t;
}

/* le cœur 0, après pstate_init_cpu (0) : 50 ms de travail, la fréquence obtenue au journal */
void pstate_boot_report(void)
{
	struct pcpu *p = &pc[0];
	if (!p->aperf) {
		kprintf("CPU : pas d'APERF/MPERF : fréquence effective inconnue\n");
		return;
	}
	sample(p, 0);
	udelay(50000);
	sample(p, 1);
	kprintf("CPU : fréquence effective du cœur 0 sous charge : %u MHz (TSC %lu MHz, plus haut demandé %u MHz)\n",
		p->mhz, arch_counter_hz() / 1000000, max_mhz);
}

/* chaque milliseconde, sur chaque cœur (minuterie) */
void pstate_tick(void)
{
	struct pcpu *p = &pc[arch_cpu_id()];
	if (!p->aperf || ++p->tick < 1000)
		return;
	p->tick = 0;
	sample(p, p->t != 0);
}

/* (CPU_LOAD) fréquence effective du cœur (0 : inconnue), *max : le plus haut demandé (0 : inconnu) */
uint32_t arch_cpu_mhz(unsigned cpu, uint32_t *max)
{
	*max = max_mhz;
	return cpu < MAX_CPUS ? pc[cpu].mhz : 0;
}
