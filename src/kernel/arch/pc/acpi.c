/* Tables ACPI statiques : RSDP, XSDT (ou RSDT), MADT, FADT.
 * L'AML (extinction, batterie, I2C…) viendra d'ACPICA dans un service. */
#include <korin/arch.h>
#include <korin/lib.h>

#include "acpi.h"

struct __attribute__((packed)) rsdp {
	char sig[8];
	uint8_t checksum;
	char oem[6];
	uint8_t revision;
	uint32_t rsdt;
	uint32_t length;
	uint64_t xsdt;
	uint8_t xchecksum;
	uint8_t reserved[3];
};

struct __attribute__((packed)) sdt {
	char sig[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char oem[6];
	char oem_table[8];
	uint32_t oem_rev;
	uint32_t creator;
	uint32_t creator_rev;
};

struct acpi_info acpi;

static int sum_ok(const void *p, uint32_t n)
{
	uint8_t s = 0;
	for (uint32_t i = 0; i < n; i++)
		s += ((const uint8_t *)p)[i];
	return s == 0;
}

static const struct sdt *find_table(const struct rsdp *r, const char sig[4])
{
	if (r->revision >= 2 && r->xsdt) {
		const struct sdt *x = arch_phys_to_virt(r->xsdt);
		uint32_t n = (x->length - sizeof *x) / 8;
		const uint8_t *e = (const uint8_t *)(x + 1);
		for (uint32_t i = 0; i < n; i++) {
			uint64_t a;
			memcpy(&a, e + i * 8, 8);
			const struct sdt *t = arch_phys_to_virt(a);
			if (!memcmp(t->sig, sig, 4))
				return t;
		}
		return 0;
	}
	const struct sdt *x = arch_phys_to_virt(r->rsdt);
	uint32_t n = (x->length - sizeof *x) / 4;
	const uint32_t *e = (const uint32_t *)(x + 1);
	for (uint32_t i = 0; i < n; i++) {
		const struct sdt *t = arch_phys_to_virt(e[i]);
		if (!memcmp(t->sig, sig, 4))
			return t;
	}
	return 0;
}

static void parse_madt(const struct sdt *t)
{
	const uint8_t *p = (const uint8_t *)t;
	uint32_t lapic;
	memcpy(&lapic, p + 36, 4);
	acpi.lapic_base = lapic;
	for (int i = 0; i < 16; i++) {
		acpi.isa_gsi[i] = (uint32_t)i;
		acpi.isa_flags[i] = 0;
	}
	for (uint32_t off = 44; off + 2 <= t->length;) {
		uint8_t type = p[off], len = p[off + 1];
		if (len < 2)
			break;
		const uint8_t *e = p + off;
		switch (type) {
		case 0: /* APIC local */
		{
			uint32_t flags;
			memcpy(&flags, e + 4, 4);
			if ((flags & 1) && acpi.cpu_count < ACPI_MAX_CPUS) /* actif (« online capable » seul : non) */
				acpi.cpu_apic_id[acpi.cpu_count++] = e[3];
			break;
		}
		case 1: /* IOAPIC */
			if (!acpi.ioapic_base) {
				uint32_t addr, gsi;
				memcpy(&addr, e + 4, 4);
				memcpy(&gsi, e + 8, 4);
				acpi.ioapic_base = addr;
				acpi.ioapic_gsi_base = gsi;
			}
			acpi.ioapic_count++;
			break;
		case 2: /* remplacement d'une IRQ ISA */
		{
			uint8_t src = e[3];
			uint32_t gsi;
			uint16_t flags;
			memcpy(&gsi, e + 4, 4);
			memcpy(&flags, e + 8, 2);
			if (src < 16) {
				acpi.isa_gsi[src] = gsi;
				acpi.isa_flags[src] = flags;
			}
			break;
		}
		case 5: /* adresse 64 bits de l'APIC local */
			memcpy(&acpi.lapic_base, e + 4, 8);
			break;
		case 9: /* x2APIC local */
		{
			uint32_t id, flags;
			memcpy(&id, e + 4, 4);
			memcpy(&flags, e + 8, 4);
			if ((flags & 1) && acpi.cpu_count < ACPI_MAX_CPUS) /* actif (« online capable » seul : non) */
				acpi.cpu_apic_id[acpi.cpu_count++] = id;
			break;
		}
		}
		off += len;
	}
}

static void parse_fadt(const struct sdt *t)
{
	const uint8_t *p = (const uint8_t *)t;
	uint32_t pm, flags = 0;
	memcpy(&pm, p + 76, 4);
	if (t->length >= 116)
		memcpy(&flags, p + 112, 4);
	if (t->length >= 111) {
		uint16_t boot_arch;
		memcpy(&boot_arch, p + 109, 2);
		acpi.has_8042 = (boot_arch >> 1) & 1;
		acpi.boot_arch_valid = t->revision >= 2;
	}
	acpi.pm_timer_port = pm;
	acpi.pm_timer_32 = (flags >> 8) & 1;
	if (t->length >= 220 && p[208] == 1) { /* X_PM_TMR_BLK dans l'espace des ports */
		uint64_t a;
		memcpy(&a, p + 212, 8);
		if (a)
			acpi.pm_timer_port = (uint32_t)a;
	}
	if ((flags >> 10) & 1 && t->length >= 129) { /* RESET_REG pris en charge */
		acpi.reset_space = p[116];
		memcpy(&acpi.reset_addr, p + 120, 8);
		acpi.reset_value = p[128];
		acpi.has_reset = 1;
	}
	acpi.hw_reduced = (flags >> 20) & 1;
}

int acpi_init(uint64_t rsdp_phys)
{
	if (!rsdp_phys)
		return -1;
	const struct rsdp *r = arch_phys_to_virt(rsdp_phys);
	if (memcmp(r->sig, "RSD PTR ", 8) || !sum_ok(r, 20))
		return -1;
	memcpy(acpi.oem, r->oem, 6);
	acpi.oem[6] = 0;
	for (int i = 5; i >= 0 && acpi.oem[i] == ' '; i--)
		acpi.oem[i] = 0;
	acpi.revision = r->revision;
	const struct sdt *madt = find_table(r, "APIC");
	if (madt && sum_ok(madt, madt->length))
		parse_madt(madt);
	const struct sdt *fadt = find_table(r, "FACP");
	if (fadt)
		parse_fadt(fadt);
	acpi.has_hpet = find_table(r, "HPET") != 0;
	acpi.has_mcfg = find_table(r, "MCFG") != 0;
	return 0;
}
