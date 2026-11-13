/* Tables ACPI statiques : RSDP, XSDT (ou RSDT), MADT, FADT.
 * L'AML (extinction, batterie, I2C…) viendra d'ACPICA dans un service. */
#include <korin/arch.h>
#include <korin/hw.h>
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
	/* extinction : SMI_CMD, ACPI_ENABLE, PM1a/b_EVT_BLK, PM1a/b_CNT_BLK (ports) ; les X_ de
	 * l'ACPI 2+ (structures d'adresse génériques) les remplacent s'ils sont donnés */
	uint32_t v32;
	memcpy(&acpi.smi_cmd, p + 48, 4);
	acpi.acpi_enable = p[52];
	memcpy(&v32, p + 56, 4);
	acpi.pm1a_evt = v32;
	memcpy(&v32, p + 60, 4);
	acpi.pm1b_evt = v32;
	memcpy(&v32, p + 64, 4);
	acpi.pm1a_cnt = v32;
	memcpy(&v32, p + 68, 4);
	acpi.pm1b_cnt = v32;
	acpi.pm1_cnt_space = acpi.pm1_evt_space = 1;
	static const struct {
		uint32_t off;
		uint64_t *addr;
		uint8_t *space;
	} gas[] = {
		{148, &acpi.pm1a_evt, &acpi.pm1_evt_space}, {160, &acpi.pm1b_evt, 0},
		{172, &acpi.pm1a_cnt, &acpi.pm1_cnt_space}, {184, &acpi.pm1b_cnt, 0},
		{244, &acpi.sleep_ctl, &acpi.sleep_ctl_space}, {256, &acpi.sleep_sts, &acpi.sleep_sts_space},
	};
	for (unsigned i = 0; i < sizeof gas / sizeof gas[0]; i++) {
		if (t->length < gas[i].off + 12)
			continue;
		uint64_t a;
		memcpy(&a, p + gas[i].off + 4, 8);
		if (!a || p[gas[i].off] > 1) /* (seulement mémoire et ports) */
			continue;
		*gas[i].addr = a;
		if (gas[i].space)
			*gas[i].space = p[gas[i].off];
	}
}

static const struct rsdp *root;

/* la table de signature sig, de rang idx (plusieurs SSDT), pour ACPI_TABLE ;
 * DSDT et FACS ne sont pas dans la XSDT : la FADT les donne */
const void *acpi_table(const char sig[4], unsigned idx, uint32_t *len)
{
	if (!root)
		return 0;
	const struct sdt *t = 0;
	if (!memcmp(sig, "DSDT", 4) || !memcmp(sig, "FACS", 4)) {
		const struct sdt *f = idx ? 0 : find_table(root, "FACP");
		if (!f)
			return 0;
		const uint8_t *p = (const uint8_t *)f;
		int dsdt = sig[0] == 'D';
		uint32_t a32;
		uint64_t a64 = 0;
		memcpy(&a32, p + (dsdt ? 40 : 36), 4);
		if (f->length >= (dsdt ? 148u : 140u))
			memcpy(&a64, p + (dsdt ? 140 : 132), 8);
		uint64_t a = a64 ? a64 : a32;
		if (!a)
			return 0;
		t = arch_phys_to_virt(a);
		if (!dsdt) { /* la FACS n'a pas l'en-tête commun : longueur en +4 */
			memcpy(len, (const uint8_t *)t + 4, 4);
			return t;
		}
	} else {
		int xs = root->revision >= 2 && root->xsdt;
		const struct sdt *x = arch_phys_to_virt(xs ? root->xsdt : root->rsdt);
		uint32_t w = xs ? 8 : 4, n = (x->length - sizeof *x) / w;
		const uint8_t *e = (const uint8_t *)(x + 1);
		for (uint32_t i = 0; i < n && !t; i++) {
			uint64_t a = 0;
			memcpy(&a, e + i * w, w);
			const struct sdt *c = arch_phys_to_virt(a);
			if (!memcmp(c->sig, sig, 4) && idx-- == 0)
				t = c;
		}
	}
	if (!t)
		return 0;
	*len = t->length;
	return t;
}

int acpi_init(uint64_t rsdp_phys)
{
	if (!rsdp_phys)
		return -1;
	const struct rsdp *r = arch_phys_to_virt(rsdp_phys);
	if (memcmp(r->sig, "RSD PTR ", 8) || !sum_ok(r, 20))
		return -1;
	root = r;
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

/* ACPI_TABLE (core/hw.c) */
const void *arch_firmware_table(const char sig[4], unsigned idx, uint32_t *len)
{
	return acpi_table(sig, idx, len);
}
