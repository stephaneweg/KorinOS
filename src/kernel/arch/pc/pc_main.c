/* Démarrage du noyau sur PC x86-64 (UEFI), voir docs/etude/ETUDE-X86.md.
 * Ordre : copie du bloc de démarrage, GDT, cœur 0, IDT, tables de pages,
 * console GOP, pages physiques, ACPI, APIC, initrd, autres cœurs, puis init
 * (ou, avec l'option « monitor », le clavier PS/2 et le moniteur). */
#include <korin/arch.h>
#include <korin/boot.h>
#include <korin/console.h>
#include <korin/lib.h>
#include <korin/initrd.h>
#include <korin/pmm.h>
#include <korin/sched.h>
#include <korin/signal.h>

#include "../x86_64/cpu.h"
#include "../x86_64/paging.h"
#include "acpi.h"
#include "apic.h"
#include "bootinfo.h"
#include "ps2.h"
#include "smp.h"
#include <korin/ws.h>

#define MMAP_MAX 65536
#define REGIONS_MAX 1024

uint8_t boot_stack[65536] __attribute__((aligned(16)));
static uint8_t ist_df0[8192] __attribute__((aligned(16)));
static uint8_t ist_nmi0[8192] __attribute__((aligned(16)));

static struct bootinfo bi;
static uint8_t mmap_copy[MMAP_MAX] __attribute__((aligned(8)));
static struct mem_region regions[REGIONS_MAX];
static int region_count;
static int hypervisor;
static char cpu_brand[49];

void monitor_run(void *arg);

/* ---- interface arch.h */

void arch_wait(void)
{
	__asm__ volatile("sti; hlt" ::: "memory");
}

void arch_irq_enable(void)
{
	sti();
}

void arch_irq_disable(void)
{
	cli();
}

void arch_debug_putc(char c)
{
	if (hypervisor)
		outb(0xE9, (uint8_t)c); /* debugcon de QEMU */
}

void arch_halt_forever(void)
{
	for (;;) {
		cli();
		hlt();
	}
}

/* (arch_reboot, arch_power_off : power.c) */

void arch_print_info(void)
{
	kprintf("  processeur : %s, %u cœur(s) logique(s) en marche\n", cpu_brand, arch_cpu_count());
	kprintf("  ACPI : rév. %u, OEM %s, %u processeur(s) dans la MADT, %u IOAPIC%s%s%s\n",
		acpi.revision, acpi.oem, acpi.cpu_count, acpi.ioapic_count,
		acpi.has_hpet ? ", HPET" : "", acpi.has_mcfg ? ", MCFG (PCIe)" : "",
		acpi.hw_reduced ? ", matériel réduit" : "");
	if (acpi.pm_timer_port)
		kprintf("  minuterie ACPI au port %x (%s bits)\n", acpi.pm_timer_port,
			acpi.pm_timer_32 ? "32" : "24");
	apic_print_info();
	kprintf("  écran : %ux%u, %s ; noyau à %lx (%lu Ko), %d tables de pages\n",
		bi.fb_width, bi.fb_height, bi.fb_format ? "BGR" : "RGB", bi.kernel_phys,
		bi.kernel_size >> 10, paging_pool_used());
}

void arch_fb_info(struct fb_desc *d)
{
	d->phys = bi.fb_base;
	d->width = bi.fb_width;
	d->height = bi.fb_height;
	d->pitch_bytes = (uint64_t)bi.fb_pitch * 4;
}

int arch_phys_is_ram(uint64_t phys, uint64_t size)
{
	for (uint64_t off = 0; off + bi.mmap_desc_size <= bi.mmap_size; off += bi.mmap_desc_size) {
		const struct efi_mdesc *d = (const struct efi_mdesc *)(mmap_copy + off);
		uint64_t s = d->phys, e = d->phys + d->pages * PAGE_SIZE;
		int ram = d->type >= 1 && d->type <= 10 && d->type != 11 && d->type != 12;
		if (ram && phys < e && phys + size > s)
			return 1;
	}
	return 0; /* (le framebuffer GOP n'est pas dans la carte : il se mappe) */
}

/* horloge CMOS : on la lit comme du temps universel (Windows la règle à
 * l'heure locale : décalage à corriger par CLOCK_SET) */
static uint8_t cmos(uint8_t reg)
{
	outb(0x70, reg);
	return inb(0x71);
}

uint64_t arch_rtc_seconds(void)
{
	for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++)
		;
	uint8_t sec = cmos(0), min = cmos(2), hour = cmos(4), day = cmos(7), mon = cmos(8), year = cmos(9);
	uint8_t b = cmos(0x0B);
	if (!(b & 4)) { /* BCD */
#define BCD(x) (uint8_t)(((x) & 15) + ((x) >> 4) * 10)
		sec = BCD(sec);
		min = BCD(min);
		hour = (uint8_t)(BCD(hour & 0x7F) | (hour & 0x80));
		day = BCD(day);
		mon = BCD(mon);
		year = BCD(year);
	}
	if (!(b & 2) && (hour & 0x80))
		hour = (uint8_t)(((hour & 0x7F) + 12) % 24);
	uint64_t y = 2000 + year;
	if (mon < 1 || mon > 12 || day < 1)
		return 0;
	/* jours depuis 1970 (algorithme des jours civils) */
	int64_t yy = (int64_t)y - (mon <= 2);
	int64_t era = yy / 400;
	int64_t yoe = yy - era * 400;
	int64_t doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	int64_t days = era * 146097 + doe - 719468;
	return (uint64_t)(days * 86400 + hour * 3600 + min * 60 + sec);
}

/* ---- démarrage */

static void read_cpu(void)
{
	uint32_t r[4];
	cpuid(1, 0, r);
	hypervisor = (r[2] >> 31) & 1;
	cpuid(0x80000000, 0, r);
	if (r[0] >= 0x80000004) {
		for (uint32_t i = 0; i < 3; i++) {
			cpuid(0x80000002 + i, 0, r);
			memcpy(cpu_brand + i * 16, r, 16);
		}
		cpu_brand[48] = 0;
	}
	char *p = cpu_brand;
	while (*p == ' ')
		p++;
	memmove(cpu_brand, p, strlen(p) + 1);
}

static void add_region(uint64_t base, uint64_t end)
{
	if (end <= base)
		return;
	/* la carte de l'UEFI d'un vrai PC a des centaines de descripteurs, souvent contigus (ZBook : plus de
	   256, et le noyau ne voyait que 900 Mo sur 32 Go) : une région qui prolonge la précédente l'agrandit */
	if (region_count && regions[region_count - 1].base + regions[region_count - 1].size == base) {
		regions[region_count - 1].size += end - base;
		return;
	}
	if (region_count >= REGIONS_MAX) {
		kprintf("mémoire : plus de %d régions, le reste est ignoré\n", REGIONS_MAX);
		return;
	}
	regions[region_count].base = base;
	regions[region_count].size = end - base;
	region_count++;
}

/* une région libre, moins les plages occupées (noyau, initrd) */
static void add_free(uint64_t base, uint64_t end, int skip)
{
	static const int n = 2;
	uint64_t starts[2] = {bi.kernel_phys, bi.initrd};
	uint64_t ends[2] = {bi.kernel_phys + bi.kernel_size, bi.initrd + ALIGN_UP(bi.initrd_size, PAGE_SIZE)};
	for (int i = skip; i < n; i++) {
		if (starts[i] == ends[i] || base >= ends[i] || end <= starts[i])
			continue;
		add_free(base, starts[i], i + 1);
		add_free(ends[i], end, i + 1);
		return;
	}
	add_region(base, end);
}

/* mémoire libre de l'UEFI moins le bloc du noyau, l'initrd et le premier Mo
 * (gardé pour le trampoline du démarrage des autres cœurs) */
static uint64_t build_regions(void)
{
	int tramp_found = 0;
	uint64_t top = 0;
	for (uint64_t off = 0; off + bi.mmap_desc_size <= bi.mmap_size; off += bi.mmap_desc_size) {
		const struct efi_mdesc *d = (const struct efi_mdesc *)(mmap_copy + off);
		uint64_t base = d->phys, end = d->phys + d->pages * PAGE_SIZE;
		if (end > top)
			top = end;
		switch (d->type) {
		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
		case EFI_BOOT_CODE:
		case EFI_BOOT_DATA:
		case EFI_CONVENTIONAL:
			break;
		default:
			continue;
		}
		/* 4 pages libres sous 1 Mo pour le trampoline des autres cœurs */
		if (!tramp_found && base < 0x100000) {
			uint64_t t = ALIGN_UP(base < 0x1000 ? 0x1000 : base, PAGE_SIZE);
			uint64_t lim = end < 0x100000 ? end : 0x100000;
			if (t + 0x4000 <= lim) {
				smp_set_trampoline(t);
				tramp_found = 1;
			}
		}
		if (base < 0x100000)
			base = 0x100000;
		if (end > base)
			add_free(base, end, 0);
	}
	return top;
}

/* un mot des options du noyau (\korin\boot.cfg, « cmdline = keyb=BE monitor ») */
static int cmdline_has(const char *word)
{
	uint64_t n = strlen(word);
	for (const char *p = bi.cmdline; *p; p++)
		if ((p == bi.cmdline || p[-1] == ' ') && !strncmp(p, word, n) && (p[n] == ' ' || !p[n]))
			return 1;
	return 0;
}

static void keyboard_start(void)
{
	const char *layout = ps2_layout(bi.cmdline); /* « keyb=BE » : AZERTY belge */
	if (ps2_init())
		kprintf("Clavier PS/2 : absent\n");
	else
		kprintf("Clavier PS/2 : prêt pour le moniteur (%s)\n", layout);
}

/* sans le moniteur : si SD0: ne vient pas (init n'a pas trouvé la partition KORIN,
 * init2 absent...), le moniteur prend l'écran et le clavier pour qu'on voie et
 * qu'on puisse essayer à la main (run pcid -n, run nvme -r...) ; ps2kbd n'est pas
 * encore lancé à ce moment (init2 le lance après SD0:) */
#define RESCUE_MS 45000
static void rescue_run(void *arg)
{
	if (boot_wait_ready(RESCUE_MS))
		return; /* SD0: est servi : le clavier ira à ps2kbd */
	console_color(CON_WARN);
	kprintf("\nSD0: n'est pas venu en %u s : moniteur de secours (journal ci-dessus).\n", RESCUE_MS / 1000);
	console_color(CON_NORMAL);
	keyboard_start();
	monitor_run(arg);
}

void kmain(uint64_t bootinfo_phys)
{
	const struct bootinfo *src = (const struct bootinfo *)bootinfo_phys;
	if (src->magic != BOOTINFO_MAGIC)
		arch_halt_forever();
	bi = *src;
	if (bi.mmap_size > MMAP_MAX)
		bi.mmap_size = MMAP_MAX;
	memcpy(mmap_copy, (const void *)bi.mmap, bi.mmap_size);

	read_cpu();
	cpu_init(0, 0);
	gdt_init(0, (uint64_t)(ist_df0 + sizeof ist_df0), (uint64_t)(ist_nmi0 + sizeof ist_nmi0));
	idt_init();

	uint64_t top = build_regions();
	uint64_t cover = top;
	if (bi.fb_base + bi.fb_size > cover)
		cover = bi.fb_base + bi.fb_size;
	if (cover < (4ULL << 30))
		cover = 4ULL << 30; /* APIC, IOAPIC, MMIO sous 4 Go */
	paging_init(bi.kernel_phys, bi.kernel_virt, bi.kernel_size, cover, bi.fb_base, bi.fb_size);

	struct fb_info fb = {
		.base = arch_phys_to_virt(bi.fb_base),
		.width = bi.fb_width,
		.height = bi.fb_height,
		.pitch = bi.fb_pitch,
		.bgr = bi.fb_format == 1,
	};
	console_init(&fb);
	console_color(CON_ACCENT);
	kprintf("Korin OS pour PC x86-64 : jalon 3 (micronoyau, process, SMP)\n");
	console_color(CON_DIM);
	kprintf("%s, écran %ux%u\n", cpu_brand, bi.fb_width, bi.fb_height);
	bi.cmdline[sizeof bi.cmdline - 1] = 0;
	if (bi.cmdline[0]) /* \korin\boot.cfg, « cmdline = » (keyb=BE : ps2_layout) */
		kprintf("Options : %s\n", bi.cmdline);
	kprintf("\n");
	console_color(CON_NORMAL);

	pmm_init(regions, region_count);
	kprintf("Mémoire : %lu Mo libres en %d régions\n", pmm_free_pages() * PAGE_SIZE >> 20,
		region_count);

	if (acpi_init(bi.rsdp))
		kpanic("pas de tables ACPI");
	kprintf("ACPI : %s, %u processeur(s), IOAPIC à %lx\n", acpi.oem, acpi.cpu_count, acpi.ioapic_base);
	acpi_power_init();

	sched_init_cpu(0, (uint64_t)(boot_stack + sizeof boot_stack));
	cpu_set_online(0);
	apic_init();
	cpu_get(0)->apic_id = lapic_id();
	sti();
	kprintf("Interruptions : APIC local et minuterie à 1000 Hz\n");

	if (bi.initrd) {
		initrd_init(arch_phys_to_virt(bi.initrd), bi.initrd_size);
		kprintf("Initrd : %lu Ko\n", bi.initrd_size >> 10);
	}
	/* le moniteur (option « monitor » de \korin\boot.cfg, ou pas d'init dans
	 * l'initrd) : le noyau prend le clavier PS/2 ; sinon le clavier est au service
	 * ps2kbd, lancé par init2, et le noyau ne touche pas l'i8042 */
	const void *img;
	uint64_t size;
	int monitor = cmdline_has("monitor") || initrd_find("init", &img, &size) != 0;
	if (monitor)
		keyboard_start();

	console_color(CON_OK);
	kprintf("\nNoyau prêt.\n");
	console_color(CON_NORMAL);

	smp_init_ipis();
	smp_start();

	struct thread *alarms = thread_create(&kernel_process, "alarmes", signal_alarm_thread, 0);
	if (alarms)
		thread_start(alarms);
	struct thread *mon = thread_create(&kernel_process, monitor ? "moniteur" : "secours",
					   monitor ? monitor_run : rescue_run, 0);
	if (mon)
		thread_start(mon);
	/* comme le Pi : init (module de démarrage, droits de SD0:/sys), sauf avec le
	 * moniteur : alors le programme d'essai hello (init se lance à la main : run init) */
	const char *first = monitor ? "hello" : "init";
	if (initrd_find(first, &img, &size) == 0) {
		int pid = proc_spawn(first, img, size);
		if (pid < 0)
			kprintf("%s : erreur %d\n", first, pid);
	}

	/* thread inactif : libère ce qui est fini, attend la prochaine interruption */
	for (;;) {
		sched_reap();
		arch_wait();
	}
}
