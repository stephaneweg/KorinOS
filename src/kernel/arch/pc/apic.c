/* Interruptions du PC : 8259 masqués, APIC local (xAPIC ou x2APIC, selon ce
 * que le micrologiciel a laissé), IOAPIC, minuterie de l'APIC à 1000 Hz
 * étalonnée sur la minuterie ACPI (PM timer), sinon sur le PIT. */
#include <korin/arch.h>
#include <korin/lib.h>

#include "../x86_64/cpu.h"
#include "acpi.h"
#include "apic.h"

#define LAPIC_ID 0x20
#define LAPIC_TPR 0x80
#define LAPIC_EOI 0xB0
#define LAPIC_SVR 0xF0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR 0x390
#define LAPIC_TIMER_DIV 0x3E0

#define HZ 1000

static volatile uint32_t *lapic_mmio;
static int x2apic;
static volatile uint32_t *ioapic;
static volatile uint64_t ticks;
static uint32_t lapic_per_ms;
static uint64_t tsc_hz;
static const char *calib_source = "?";

static uint32_t lapic_read(uint32_t reg)
{
	if (x2apic)
		return (uint32_t)rdmsr(0x800 + (reg >> 4));
	return lapic_mmio[reg / 4];
}

static void lapic_write(uint32_t reg, uint32_t v)
{
	if (x2apic)
		wrmsr(0x800 + (reg >> 4), v);
	else
		lapic_mmio[reg / 4] = v;
}

void irq_eoi(void)
{
	lapic_write(LAPIC_EOI, 0);
}

uint32_t lapic_id(void)
{
	uint32_t id = lapic_read(LAPIC_ID);
	return x2apic ? id : id >> 24;
}

static void pic_disable(void)
{
	/* reprogrammés sur 0x20-0x2F puis tous masqués : une IRQ parasite ne
	 * tombe pas sur un vecteur d'exception */
	outb(0x20, 0x11);
	io_wait();
	outb(0xA0, 0x11);
	io_wait();
	outb(0x21, 0x20);
	io_wait();
	outb(0xA1, 0x28);
	io_wait();
	outb(0x21, 4);
	io_wait();
	outb(0xA1, 2);
	io_wait();
	outb(0x21, 1);
	io_wait();
	outb(0xA1, 1);
	io_wait();
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);
}

/* ---- étalonnage : 10 ms mesurées sur le PM timer ou le PIT */

static uint32_t pm_read(void)
{
	return inl((uint16_t)acpi.pm_timer_port) & (acpi.pm_timer_32 ? 0xFFFFFFFFu : 0xFFFFFFu);
}

static void wait_10ms_pm(void)
{
	uint32_t mask = acpi.pm_timer_32 ? 0xFFFFFFFFu : 0xFFFFFFu;
	uint32_t start = pm_read();
	while (((pm_read() - start) & mask) < 35795) /* 3,579545 MHz */
		pause();
}

static void wait_10ms_pit(void)
{
	/* canal 2, porte par le port 0x61, mode 0, 11932 coups = 10 ms */
	outb(0x61, (inb(0x61) & 0xFD) | 1);
	outb(0x43, 0xB0);
	outb(0x42, 11932 & 0xFF);
	outb(0x42, 11932 >> 8);
	uint8_t v = inb(0x61) & 0xFE;
	outb(0x61, v);
	outb(0x61, v | 1);
	while (!(inb(0x61) & 0x20))
		pause();
}

static void timer_irq(struct regs *r)
{
	(void)r;
	ticks++;
}

static void ioapic_write(uint32_t reg, uint32_t v)
{
	ioapic[0] = reg;
	ioapic[4] = v;
}

static uint32_t ioapic_read(uint32_t reg)
{
	ioapic[0] = reg;
	return ioapic[4];
}

void apic_init(void)
{
	pic_disable();

	uint64_t base = rdmsr(MSR_APIC_BASE);
	x2apic = (base >> 10) & 1;
	if (!(base & (1 << 11))) /* APIC éteint : le rallumer en xAPIC */
		wrmsr(MSR_APIC_BASE, base | (1 << 11));
	uint64_t phys = acpi.lapic_base ? acpi.lapic_base : (base & ~0xFFFULL);
	lapic_mmio = arch_phys_to_virt(phys & ~0xFFFULL);

	lapic_write(LAPIC_TPR, 0);
	lapic_write(LAPIC_SVR, 0x100 | 0xFF); /* allumé, parasite au vecteur 0xFF */

	/* IOAPIC : toutes les entrées masquées */
	if (acpi.ioapic_base) {
		ioapic = arch_phys_to_virt(acpi.ioapic_base);
		uint32_t n = ((ioapic_read(1) >> 16) & 0xFF) + 1;
		for (uint32_t i = 0; i < n; i++) {
			ioapic_write(0x10 + 2 * i, 1 << 16);
			ioapic_write(0x11 + 2 * i, 0);
		}
	}

	/* minuterie : compte à rebours libre, mesuré sur 10 ms */
	lapic_write(LAPIC_TIMER_DIV, 0x3); /* divisé par 16 */
	lapic_write(LAPIC_LVT_TIMER, 1 << 16);
	lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);
	uint64_t t0 = rdtsc();
	if (acpi.pm_timer_port) {
		wait_10ms_pm();
		calib_source = "minuterie ACPI";
	} else {
		wait_10ms_pit();
		calib_source = "PIT";
	}
	uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CUR);
	tsc_hz = (rdtsc() - t0) * 100;
	lapic_per_ms = elapsed / 10;
	if (!lapic_per_ms)
		lapic_per_ms = 1;

	idt_set_handler(VEC_TIMER, timer_irq);
	idt_set_handler(VEC_SPURIOUS, 0);
	lapic_write(LAPIC_LVT_TIMER, VEC_TIMER | (1 << 17)); /* périodique */
	lapic_write(LAPIC_TIMER_INIT, lapic_per_ms * (1000 / HZ));
}

void ioapic_route_isa(uint8_t irq, uint8_t vector)
{
	if (!ioapic || irq >= 16)
		return;
	uint32_t gsi = acpi.isa_gsi[irq] - acpi.ioapic_gsi_base;
	uint16_t f = acpi.isa_flags[irq];
	uint32_t low = vector;
	if ((f & 3) == 3)
		low |= 1 << 13; /* actif bas */
	if (((f >> 2) & 3) == 3)
		low |= 1 << 15; /* sur niveau */
	ioapic_write(0x11 + 2 * gsi, lapic_id() << 24);
	ioapic_write(0x10 + 2 * gsi, low);
}

uint64_t arch_ticks_ms(void)
{
	return ticks * (1000 / HZ);
}

void apic_print_info(void)
{
	kprintf("  APIC local : %s, id %u ; minuterie %u coups/ms (étalonnée sur %s)\n",
		x2apic ? "x2APIC" : "xAPIC", lapic_id(), lapic_per_ms, calib_source);
	kprintf("  TSC : %lu MHz\n", tsc_hz / 1000000);
}
