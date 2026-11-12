#ifndef KORIN_PC_ACPI_H
#define KORIN_PC_ACPI_H

#include <stdint.h>

#define ACPI_MAX_CPUS 256

struct acpi_info {
	char oem[7];
	uint8_t revision;
	uint64_t lapic_base;
	uint32_t cpu_count;
	uint32_t cpu_apic_id[ACPI_MAX_CPUS];
	uint64_t ioapic_base;
	uint32_t ioapic_gsi_base;
	uint32_t ioapic_count;
	uint32_t isa_gsi[16];   /* IRQ ISA -> GSI (remplacements de la MADT) */
	uint16_t isa_flags[16]; /* polarité (bits 0-1), déclenchement (bits 2-3) */
	uint32_t pm_timer_port; /* 0 : absent */
	int pm_timer_32;
	int has_reset;
	uint8_t reset_space, reset_value;
	uint64_t reset_addr;
	int has_8042, boot_arch_valid;
	int hw_reduced;
	int has_hpet, has_mcfg;
};

extern struct acpi_info acpi;

int acpi_init(uint64_t rsdp_phys);
/* table ACPI (en-tête compris) de signature sig et de rang idx, 0 si absente */
const void *acpi_table(const char sig[4], unsigned idx, uint32_t *len);

#endif
