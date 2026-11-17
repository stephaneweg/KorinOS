/* osl.c - la couche système d'ACPICA (AcpiOs*) sur Korin, dans le service acpid du PC.
 *
 * Un seul fil : verrous et sémaphores sont des compteurs (personne d'autre ne peut les rendre pendant
 * une attente : une attente qui ne peut pas aboutir rend AE_TIME) ; AcpiOsExecute met le travail dans une
 * file que la boucle du service vide (acpi_run_deferred). Mémoire physique : DEV_MAP (registres, et
 * mémoire du micrologiciel en cache : tables, GNVS), gardée mappée (ACPICA mappe et démappe souvent ;
 * le noyau n'a pas de démappage) ; ports : IO_PORT (8, 16, 32 bits) ; configuration PCI : ECAM de la MCFG ;
 * temps : TSC étalonné au démarrage. */
#include "korin_kit.h"

#include "acpi.h"
#include "accommon.h"

#include "acpid.h"

/* (utprint.c d'ACPICA, renommés par ackorin.h) */
int AcpiKvsnprintf(char *s, ACPI_SIZE n, const char *fmt, va_list ap);
int AcpiKsnprintf(char *s, ACPI_SIZE n, const char *fmt, ...);

void *malloc(unsigned long n);
void free(void *p);

#define E_NOENT_ -2

/* ---- temps */
static unsigned long tsc_per_us = 1000;

static inline unsigned long rdtsc(void)
{
	unsigned lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long)hi << 32) | lo;
}

void osl_init(void)
{
	/* TSC étalonné sur 20 ms de l'horloge du noyau */
	long t0 = korin_sys0(SYS_TIME_MS);
	while (korin_sys0(SYS_TIME_MS) == t0)
		;
	unsigned long c0 = rdtsc();
	long t1 = korin_sys0(SYS_TIME_MS);
	while (korin_sys0(SYS_TIME_MS) < t1 + 20)
		;
	unsigned long c1 = rdtsc();
	unsigned long us = (unsigned long)(korin_sys0(SYS_TIME_MS) - t1) * 1000;
	if (us && c1 > c0)
		tsc_per_us = (c1 - c0) / us;
	if (!tsc_per_us)
		tsc_per_us = 1000;
}

void AcpiOsStall(UINT32 us)
{
	if (us >= 2000) {
		korin_sys1(SYS_SLEEP, us / 1000);
		us %= 1000;
	}
	unsigned long end = rdtsc() + us * tsc_per_us;
	while (rdtsc() < end)
		__asm__ volatile("pause");
}

void AcpiOsSleep(UINT64 ms)
{
	korin_sys1(SYS_SLEEP, ms ? ms : 1);
}

UINT64 AcpiOsGetTimer(void)
{
	return rdtsc() / tsc_per_us * 10; /* en 100 ns */
}

/* ---- démarrage, tables */
ACPI_STATUS AcpiOsInitialize(void)
{
	return AE_OK;
}

ACPI_STATUS AcpiOsTerminate(void)
{
	return AE_OK;
}

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer(void)
{
	UINT64 a = 0;
	if (korin_sys4(SYS_ACPI_TABLE, "RPTR", 0, &a, 8) != 8)
		return 0;
	return a;
}

ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *Init, ACPI_STRING *New)
{
	(void)Init;
	*New = NULL;
	return AE_OK;
}

ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER *Existing, ACPI_TABLE_HEADER **New)
{
	(void)Existing;
	*New = NULL;
	return AE_OK;
}

ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *Existing, ACPI_PHYSICAL_ADDRESS *Addr,
					UINT32 *Len)
{
	(void)Existing;
	*Addr = 0;
	*Len = 0;
	return AE_OK;
}

/* ---- mémoire */
void *AcpiOsAllocate(ACPI_SIZE n)
{
	return malloc(n ? n : 1);
}

void AcpiOsFree(void *p)
{
	free(p);
}

#define MAPS 512
static struct map {
	UINT64 phys, len;
	UINT8 *va;
} maps[MAPS];
static int nmaps;

void *AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS phys, ACPI_SIZE len)
{
	UINT64 base = phys & ~0xFFFULL, end = (phys + (len ? len : 1) + 0xFFF) & ~0xFFFULL;
	for (int i = 0; i < nmaps; i++)
		if (base >= maps[i].phys && end <= maps[i].phys + maps[i].len)
			return maps[i].va + (phys - maps[i].phys);
	long va = korin_sys2(SYS_DEV_MAP, base, end - base);
	if (va < 0) {
		char b[96];
		AcpiKsnprintf(b, sizeof b, "DEV_MAP %lx (%lu octets) refusé (%ld)", (unsigned long)phys,
			      (unsigned long)len, va);
		acpid_log(b);
		return NULL;
	}
	if (nmaps < MAPS) {
		maps[nmaps].phys = base;
		maps[nmaps].len = end - base;
		maps[nmaps].va = (UINT8 *)va;
		nmaps++;
	}
	return (UINT8 *)va + (phys - base);
}

void AcpiOsUnmapMemory(void *va, ACPI_SIZE len)
{
	(void)va; /* gardé : le prochain appel le retrouve */
	(void)len;
}

ACPI_STATUS AcpiOsGetPhysicalAddress(void *va, ACPI_PHYSICAL_ADDRESS *phys)
{
	(void)va;
	(void)phys;
	return AE_SUPPORT;
}

BOOLEAN AcpiOsReadable(void *p, ACPI_SIZE n)
{
	(void)p;
	(void)n;
	return TRUE;
}

BOOLEAN AcpiOsWritable(void *p, ACPI_SIZE n)
{
	(void)p;
	(void)n;
	return TRUE;
}

ACPI_STATUS AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS a, UINT64 *v, UINT32 width)
{
	volatile void *p = AcpiOsMapMemory(a, width / 8);
	if (!p)
		return AE_NO_MEMORY;
	switch (width) {
	case 8: *v = *(volatile UINT8 *)p; break;
	case 16: *v = *(volatile UINT16 *)p; break;
	case 32: *v = *(volatile UINT32 *)p; break;
	case 64: *v = *(volatile UINT64 *)p; break;
	default: return AE_BAD_PARAMETER;
	}
	return AE_OK;
}

ACPI_STATUS AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS a, UINT64 v, UINT32 width)
{
	volatile void *p = AcpiOsMapMemory(a, width / 8);
	if (!p)
		return AE_NO_MEMORY;
	switch (width) {
	case 8: *(volatile UINT8 *)p = (UINT8)v; break;
	case 16: *(volatile UINT16 *)p = (UINT16)v; break;
	case 32: *(volatile UINT32 *)p = (UINT32)v; break;
	case 64: *(volatile UINT64 *)p = v; break;
	default: return AE_BAD_PARAMETER;
	}
	return AE_OK;
}

/* ---- ports d'E/S */
static int port_refused;

static UINT32 port_op(ACPI_IO_ADDRESS port, unsigned op, UINT32 v, int *ok)
{
	long r = korin_sys3(SYS_IO_PORT, port, op, v);
	if (r < 0) {
		*ok = 0;
		if (port_refused++ < 8) {
			char b[64];
			AcpiKsnprintf(b, sizeof b, "port %x refusé (%ld)", (unsigned)port, r);
			acpid_log(b);
		}
		return 0xFFFFFFFF;
	}
	return (UINT32)r;
}

ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS port, UINT32 *v, UINT32 width)
{
	int ok = 1;
	unsigned bytes = width / 8;
	if (bytes != 1 && bytes != 2 && bytes != 4)
		return AE_BAD_PARAMETER;
	if (port & (bytes - 1)) { /* pas aligné : octet par octet */
		UINT32 x = 0;
		for (unsigned i = 0; i < bytes; i++)
			x |= (port_op(port + i, 0, 0, &ok) & 0xFF) << (8 * i);
		*v = x;
	} else
		*v = port_op(port, bytes == 1 ? 0 : bytes == 2 ? 2 : 4, 0, &ok);
	if (!ok) /* (comme un port sans périphérique) */
		*v = bytes == 4 ? 0xFFFFFFFF : (1u << width) - 1;
	return AE_OK;
}

ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS port, UINT32 v, UINT32 width)
{
	int ok = 1;
	unsigned bytes = width / 8;
	if (bytes != 1 && bytes != 2 && bytes != 4)
		return AE_BAD_PARAMETER;
	if (port & (bytes - 1)) {
		for (unsigned i = 0; i < bytes; i++)
			port_op(port + i, 1, (v >> (8 * i)) & 0xFF, &ok);
	} else
		port_op(port, bytes == 1 ? 1 : bytes == 2 ? 3 : 5, v, &ok);
	return AE_OK;
}

/* ---- configuration PCI : ECAM (MCFG) */
static struct ecam {
	UINT64 base;
	UINT16 seg;
	UINT8 bus0, bus1;
} ecams[8];
static int necam = -1;

static void ecam_load(void)
{
	static UINT8 t[256];
	necam = 0;
	long n = korin_sys4(SYS_ACPI_TABLE, "MCFG", 0, t, sizeof t);
	if (n < 44)
		return;
	if (n > (long)sizeof t)
		n = sizeof t;
	for (long o = 44; o + 16 <= n && necam < 8; o += 16) {
		struct ecam *e = &ecams[necam++];
		memcpy(&e->base, t + o, 8);
		memcpy(&e->seg, t + o + 8, 2);
		e->bus0 = t[o + 10];
		e->bus1 = t[o + 11];
	}
}

static volatile UINT8 *pci_cfg(ACPI_PCI_ID *id, UINT32 reg)
{
	if (necam < 0)
		ecam_load();
	for (int i = 0; i < necam; i++) {
		struct ecam *e = &ecams[i];
		if (e->seg != id->Segment || id->Bus < e->bus0 || id->Bus > e->bus1)
			continue;
		UINT64 a = e->base + ((UINT64)(id->Bus - e->bus0) << 20) + ((UINT64)id->Device << 15) +
			   ((UINT64)id->Function << 12);
		volatile UINT8 *p = AcpiOsMapMemory(a, 4096);
		return p ? p + (reg & 0xFFF) : NULL;
	}
	return NULL;
}

ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID *id, UINT32 reg, UINT64 *v, UINT32 width)
{
	volatile UINT8 *p = pci_cfg(id, reg);
	if (!p)
		return AE_SUPPORT;
	switch (width) {
	case 8: *v = *p; break;
	case 16: *v = *(volatile UINT16 *)p; break;
	case 32: *v = *(volatile UINT32 *)p; break;
	case 64: *v = *(volatile UINT32 *)p | ((UINT64) * (volatile UINT32 *)(p + 4) << 32); break;
	default: return AE_BAD_PARAMETER;
	}
	return AE_OK;
}

ACPI_STATUS AcpiOsWritePciConfiguration(ACPI_PCI_ID *id, UINT32 reg, UINT64 v, UINT32 width)
{
	volatile UINT8 *p = pci_cfg(id, reg);
	if (!p)
		return AE_SUPPORT;
	switch (width) {
	case 8: *p = (UINT8)v; break;
	case 16: *(volatile UINT16 *)p = (UINT16)v; break;
	case 32: *(volatile UINT32 *)p = (UINT32)v; break;
	case 64:
		*(volatile UINT32 *)p = (UINT32)v;
		*(volatile UINT32 *)(p + 4) = (UINT32)(v >> 32);
		break;
	default: return AE_BAD_PARAMETER;
	}
	return AE_OK;
}

/* ---- verrous et sémaphores (un seul fil) */
struct sem {
	UINT32 count, max;
};

ACPI_STATUS AcpiOsCreateSemaphore(UINT32 max, UINT32 initial, ACPI_SEMAPHORE *out)
{
	struct sem *s = malloc(sizeof *s);
	if (!s)
		return AE_NO_MEMORY;
	s->count = initial;
	s->max = max;
	*out = s;
	return AE_OK;
}

ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE h)
{
	free(h);
	return AE_OK;
}

ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE h, UINT32 units, UINT16 timeout)
{
	struct sem *s = h;
	(void)timeout;
	if (!s)
		return AE_BAD_PARAMETER;
	if (s->count < units)
		return AE_TIME; /* personne ne peut la rendre pendant l'attente */
	s->count -= units;
	return AE_OK;
}

ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE h, UINT32 units)
{
	struct sem *s = h;
	if (!s)
		return AE_BAD_PARAMETER;
	s->count += units;
	if (s->max && s->count > s->max)
		s->count = s->max;
	return AE_OK;
}

ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *out)
{
	*out = malloc(1);
	return *out ? AE_OK : AE_NO_MEMORY;
}

void AcpiOsDeleteLock(ACPI_SPINLOCK h)
{
	free(h);
}

ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK h)
{
	(void)h;
	return 0;
}

void AcpiOsReleaseLock(ACPI_SPINLOCK h, ACPI_CPU_FLAGS f)
{
	(void)h;
	(void)f;
}

/* verrou global de la FACS (ACPI 5.2.10.1) : bit 0 attente, bit 1 pris */
int AcpiKorinAcquireGlobalLock(void *facs)
{
	volatile UINT32 *g = &((ACPI_TABLE_FACS *)facs)->GlobalLock;
	UINT32 o, n;
	do {
		o = *g;
		n = ((o & ~1u) | 2u) | ((o >> 1) & 1u); /* pris ; en attente s'il l'était déjà */
	} while (!__sync_bool_compare_and_swap(g, o, n));
	return n < 3;
}

int AcpiKorinReleaseGlobalLock(void *facs)
{
	volatile UINT32 *g = &((ACPI_TABLE_FACS *)facs)->GlobalLock;
	UINT32 o, n;
	do {
		o = *g;
		n = o & ~3u;
	} while (!__sync_bool_compare_and_swap(g, o, n));
	return o & 1;
}

/* ---- fils, interruptions, travaux différés */
ACPI_THREAD_ID AcpiOsGetThreadId(void)
{
	return 1;
}

#define WORKS 64
static struct work {
	ACPI_OSD_EXEC_CALLBACK fn;
	void *ctx;
} works[WORKS];
static int whead, wtail;

ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE type, ACPI_OSD_EXEC_CALLBACK fn, void *ctx)
{
	(void)type;
	int next = (wtail + 1) % WORKS;
	if (next == whead)
		return AE_NO_MEMORY;
	works[wtail].fn = fn;
	works[wtail].ctx = ctx;
	wtail = next;
	return AE_OK;
}

void acpi_run_deferred(void)
{
	for (int guard = 0; whead != wtail && guard < 256; guard++) {
		struct work w = works[whead];
		whead = (whead + 1) % WORKS;
		w.fn(w.ctx);
	}
}

void AcpiOsWaitEventsComplete(void)
{
	acpi_run_deferred();
}

ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 line, ACPI_OSD_HANDLER fn, void *ctx)
{
	(void)line; /* (pas encore de SCI : la batterie se lit en sondant) */
	(void)fn;
	(void)ctx;
	return AE_OK;
}

ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 line, ACPI_OSD_HANDLER fn)
{
	(void)line;
	(void)fn;
	return AE_OK;
}

ACPI_STATUS AcpiOsSignal(UINT32 fn, void *info)
{
	if (fn == ACPI_SIGNAL_FATAL)
		acpid_log("l'AML signale une erreur fatale (Fatal)");
	(void)info;
	return AE_OK;
}

ACPI_STATUS AcpiOsEnterSleep(UINT8 state, UINT32 a, UINT32 b)
{
	(void)state;
	(void)a;
	(void)b;
	return AE_OK;
}

/* ---- sortie : au journal, ligne par ligne */
static char line[256];
static int llen;

void AcpiOsVprintf(const char *fmt, va_list ap)
{
	char b[256];
	int n = AcpiKvsnprintf(b, sizeof b, fmt, ap);
	if (n > (int)sizeof b - 1)
		n = sizeof b - 1;
	for (int i = 0; i < n; i++) {
		if (b[i] == '\n' || llen == (int)sizeof line - 1) {
			line[llen] = 0;
			if (llen)
				acpid_log(line);
			llen = 0;
			if (b[i] == '\n')
				continue;
		}
		line[llen++] = b[i];
	}
}

void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	AcpiOsVprintf(fmt, ap);
	va_end(ap);
}

void AcpiOsRedirectOutput(void *dest)
{
	(void)dest;
}

ACPI_STATUS AcpiOsGetLine(char *buf, UINT32 len, UINT32 *read)
{
	(void)buf;
	(void)len;
	(void)read;
	return AE_SUPPORT;
}
