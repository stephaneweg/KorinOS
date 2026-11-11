/* Démarrage du noyau sur PC x86-64 (UEFI) : premier jalon de
 * docs/etude/ETUDE-X86.md. Ordre : copie du bloc de démarrage, GDT, IDT,
 * tables de pages, console GOP, pages physiques, ACPI, APIC, clavier, moniteur. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/lib.h>
#include <korin/pmm.h>

#include "../x86_64/cpu.h"
#include "../x86_64/paging.h"
#include "acpi.h"
#include "apic.h"
#include "bootinfo.h"
#include "ps2.h"

#define MMAP_MAX 65536
#define REGIONS_MAX 256

uint8_t boot_stack[65536] __attribute__((aligned(16)));

static struct bootinfo bi;
static uint8_t mmap_copy[MMAP_MAX] __attribute__((aligned(8)));
static struct mem_region regions[REGIONS_MAX];
static int region_count;
static int hypervisor;
static char cpu_brand[49];

void monitor_run(void);

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

void arch_reboot(void)
{
	cli();
	if (acpi.has_reset && acpi.reset_space == 1)
		outb((uint16_t)acpi.reset_addr, acpi.reset_value);
	else if (acpi.has_reset && acpi.reset_space == 0)
		*(volatile uint8_t *)arch_phys_to_virt(acpi.reset_addr) = acpi.reset_value;
	outb(0xCF9, 0x02); /* registre de remise à zéro du chipset */
	outb(0xCF9, 0x06);
	for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
		;
	outb(0x64, 0xFE); /* contrôleur clavier */
	arch_halt_forever();
}

void arch_print_info(void)
{
	kprintf("  processeur : %s\n", cpu_brand);
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
	if (end <= base || region_count >= REGIONS_MAX)
		return;
	regions[region_count].base = base;
	regions[region_count].size = end - base;
	region_count++;
}

/* mémoire libre de l'UEFI moins le bloc du noyau et le premier Mo (gardé
 * pour le trampoline du démarrage des autres cœurs) */
static uint64_t build_regions(void)
{
	uint64_t top = 0;
	uint64_t kstart = bi.kernel_phys, kend = bi.kernel_phys + bi.kernel_size;
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
		if (base < 0x100000)
			base = 0x100000;
		if (base < kend && end > kstart) {
			add_region(base, kstart);
			add_region(kend, end);
		} else
			add_region(base, end);
	}
	return top;
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
	gdt_init();
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
	kprintf("Korin OS pour PC x86-64 : premier jalon (micronoyau)\n");
	console_color(CON_DIM);
	kprintf("%s, écran %ux%u\n\n", cpu_brand, bi.fb_width, bi.fb_height);
	console_color(CON_NORMAL);

	pmm_init(regions, region_count);
	kprintf("Mémoire : %lu Mo libres en %d régions\n", pmm_free_pages() * PAGE_SIZE >> 20,
		region_count);

	if (acpi_init(bi.rsdp))
		kpanic("pas de tables ACPI");
	kprintf("ACPI : %s, %u processeur(s), IOAPIC à %lx\n", acpi.oem, acpi.cpu_count, acpi.ioapic_base);

	apic_init();
	sti();
	kprintf("Interruptions : APIC local et minuterie à 1000 Hz\n");

	if (ps2_init())
		kprintf("Clavier PS/2 : absent (USB pas encore pris en charge)\n");
	else
		kprintf("Clavier PS/2 : prêt (AZERTY)\n");

	console_color(CON_OK);
	kprintf("\nNoyau prêt.\n");
	console_color(CON_NORMAL);
	monitor_run();
}
