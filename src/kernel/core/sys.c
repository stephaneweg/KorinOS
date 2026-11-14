/* Système (docs/ABI.md, 0x60 et 0x1B-0x1C) : horloge, SYSINFO, aléa
 * (ChaCha20 remêlé par l'aléa que voit le noyau), journal du noyau. Portable. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/proc.h>
#include <korin/sched.h>
#include <korin/spinlock.h>

/* ---- horloge : secondes UTC = base + temps depuis le démarrage */

static int64_t clock_base = -1; /* secondes UTC au démarrage */
static int64_t clock_offset;    /* décalage de l'heure locale */
static spinlock_t clock_lock = SPINLOCK_INIT;
/* CLOCK_SET, troisième argument : une heure sûre (NTP de netd), qui peut faire reculer l'horloge. L'horloge CMOS
 * d'un PC qui a aussi Windows est à l'heure LOCALE (Paris : UTC+1 ou +2), lue ici comme UTC : sans cela, Korin
 * garderait cette avance pour toujours (et SD0:/etc/lastclock la reporterait au démarrage suivant). Une valeur
 * remarquable plutôt qu'un 1 : un appelant à deux arguments laisse n'importe quoi dans ce registre. */
#define CLOCK_SET_EXACT 0x4E545021 /* « NTP! » */

static int64_t now_utc(void)
{
	if (clock_base < 0)
		clock_base = (int64_t)arch_rtc_seconds() - (int64_t)(arch_ticks_ms() / 1000);
	return clock_base + (int64_t)(arch_ticks_ms() / 1000);
}

int64_t sys_clock(int set, uint64_t a, uint64_t b, uint64_t c)
{
	uint64_t f = spin_lock_irqsave(&clock_lock);
	int64_t r;
	if (!set) {
		r = a == 0 ? now_utc() : a == 1 ? clock_offset : a == 2 ? now_utc() + clock_offset
		  : a == 3 ? (int64_t)arch_counter_hz() : E_INVAL;
	} else if (!sched_current()->proc->rights)
		r = E_PERM;
	else {
		int64_t want = (int64_t)a;
		/* elle ne recule jamais, sauf devant une heure sûre (NTP) */
		if (want > now_utc() || (c == CLOCK_SET_EXACT && want > 1000000000))
			clock_base += want - now_utc();
		clock_offset = (int64_t)b; /* le décalage est toujours pris */
		r = 0;
	}
	spin_unlock_irqrestore(&clock_lock, f);
	return r;
}

/* ---- SYSINFO : 80 octets */

int64_t sys_sysinfo(uint64_t out)
{
	uint64_t o[10] = {0};
	o[0] = arch_cpu_count();
	o[1] = pmm_free_pages() * PAGE_SIZE >> 10;
	o[2] = pmm_total_pages() * PAGE_SIZE >> 10;
	o[3] = arch_ticks_ms();
	uint64_t f = sched_lock_take();
	for (struct process *p = *proc_list_head(); p; p = p->next)
		o[4]++;
	sched_lock_give(f);
	for (unsigned c = 0; c < 4 && c < o[0]; c++) { /* les 4 premiers cœurs ; les autres : CPU_LOAD */
		uint64_t busy;
		sched_cpu_load(c, &o[5 + c], &busy);
	}
	o[9] = sched_switches();
	return copy_to_user(out, o, 80) ? E_FAULT : 0;
}

/* ---- CPU_LOAD : pour chaque cœur, 16 octets {% de la dernière seconde, ms occupées
 * depuis le démarrage} ; rend le nombre de cœurs (places 0 : le nombre seul). places | CPU_LOAD_FREQ :
 * 32 octets, en plus {MHz effectifs de la dernière seconde (APERF/MPERF ; 0 : inconnus), MHz les
 * plus hauts demandés (HWP, PERF_CTL ; 0 : inconnus)} */

#define CPU_LOAD_FREQ (1ULL << 32)

int64_t sys_cpu_load(uint64_t out, uint64_t places)
{
	uint64_t n = arch_cpu_count();
	uint64_t sz = (places & CPU_LOAD_FREQ) ? 32 : 16;
	places &= 0xFFFFFFFF;
	for (uint64_t c = 0; c < n && c < places; c++) {
		uint64_t e[4];
		sched_cpu_load((unsigned)c, &e[0], &e[1]);
		uint32_t mx;
		e[2] = arch_cpu_mhz((unsigned)c, &mx);
		e[3] = mx;
		if (copy_to_user(out + c * sz, e, sz))
			return E_FAULT;
	}
	return (int64_t)n;
}

/* ---- aléa : ChaCha20, clé remêlée à chaque appel */

static uint32_t key[8];
static uint64_t counter;
static volatile uint64_t pool;
static spinlock_t rnd_lock = SPINLOCK_INIT;

#define ROTL(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define QR(a, b, c, d) (a += b, d ^= a, d = ROTL(d, 16), c += d, b ^= c, b = ROTL(b, 12), \
			a += b, d ^= a, d = ROTL(d, 8), c += d, b ^= c, b = ROTL(b, 7))

static void chacha_block(const uint32_t k[8], uint64_t ctr, uint32_t out[16])
{
	uint32_t s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
			  k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7],
			  (uint32_t)ctr, (uint32_t)(ctr >> 32), 0, 0};
	uint32_t x[16];
	memcpy(x, s, sizeof x);
	for (int i = 0; i < 10; i++) {
		QR(x[0], x[4], x[8], x[12]);
		QR(x[1], x[5], x[9], x[13]);
		QR(x[2], x[6], x[10], x[14]);
		QR(x[3], x[7], x[11], x[15]);
		QR(x[0], x[5], x[10], x[15]);
		QR(x[1], x[6], x[11], x[12]);
		QR(x[2], x[7], x[8], x[13]);
		QR(x[3], x[4], x[9], x[14]);
	}
	for (int i = 0; i < 16; i++)
		out[i] = x[i] + s[i];
}

void random_mix(uint64_t v)
{
	pool = (pool ^ v) * 0x9E3779B97F4A7C15ULL + (pool >> 29);
}

int64_t sys_getrandom(uint64_t buf, uint64_t n)
{
	if (n > 65536)
		n = 65536;
	uint8_t chunk[64];
	uint64_t done = 0;
	uint64_t f = spin_lock_irqsave(&rnd_lock);
	/* remêlage : compteur du processeur, aléa du matériel, interruptions */
	uint64_t hw = 0;
	arch_hw_random(&hw);
	random_mix(hw);
	random_mix(arch_ticks_ms());
	key[counter & 7] ^= (uint32_t)pool;
	key[(counter + 3) & 7] ^= (uint32_t)(pool >> 32);
	uint32_t k[8];
	memcpy(k, key, sizeof k);
	uint64_t ctr = counter;
	counter += (n + 63) / 64 + 1;
	/* clé suivante tirée du flux : l'aléa déjà donné ne se retrouve pas */
	uint32_t nk[16];
	chacha_block(k, ctr++, nk);
	memcpy(key, nk, sizeof key);
	spin_unlock_irqrestore(&rnd_lock, f);
	while (done < n) {
		chacha_block(k, ctr++, (uint32_t *)chunk);
		uint64_t m = n - done < 64 ? n - done : 64;
		if (copy_to_user(buf + done, chunk, m))
			return E_FAULT;
		done += m;
	}
	memset(k, 0, sizeof k);
	return (int64_t)n;
}

/* ---- journal du noyau : 64 Ko, chaque ligne précédée de son heure */

#define KLOG_SIZE 65536
static char klog[KLOG_SIZE];
static uint64_t klog_len; /* octets écrits depuis le démarrage */
static int line_start = 1;
static spinlock_t klog_lock = SPINLOCK_INIT;

static void klog_putc(char c)
{
	klog[klog_len % KLOG_SIZE] = c;
	klog_len++;
}

void klog_write(const char *s)
{
	uint64_t f = spin_lock_irqsave(&klog_lock);
	for (; *s; s++) {
		if (line_start) {
			char stamp[20];
			uint64_t ms = arch_ticks_ms();
			ksnprintf(stamp, sizeof stamp, "[%5lu.%03lu] ", ms / 1000, ms % 1000);
			for (char *p = stamp; *p; p++)
				klog_putc(*p);
			line_start = 0;
		}
		klog_putc(*s);
		if (*s == '\n')
			line_start = 1;
	}
	spin_unlock_irqrestore(&klog_lock, f);
}

int64_t sys_klog(uint64_t buf, uint64_t max)
{
	uint64_t f = spin_lock_irqsave(&klog_lock);
	uint64_t n = klog_len < KLOG_SIZE ? klog_len : KLOG_SIZE;
	uint64_t first = klog_len - n;
	spin_unlock_irqrestore(&klog_lock, f);
	if (n > max)
		n = max;
	char tmp[256];
	for (uint64_t done = 0; done < n;) {
		uint64_t m = n - done < sizeof tmp ? n - done : sizeof tmp;
		for (uint64_t i = 0; i < m; i++)
			tmp[i] = klog[(first + done + i) % KLOG_SIZE];
		if (copy_to_user(buf + done, tmp, m))
			return E_FAULT;
		done += m;
	}
	return (int64_t)n;
}
