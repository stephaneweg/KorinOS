/* GDT et TSS du cœur 0. Les sélecteurs utilisateur sont déjà placés pour
 * sysret (données à 0x18, code à 0x20) ; aucun process ne tourne encore. */
#include <stdint.h>

#include "cpu.h"

#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_TSS 0x28

struct __attribute__((packed)) tss {
	uint32_t reserved0;
	uint64_t rsp[3];
	uint64_t reserved1;
	uint64_t ist[7];
	uint64_t reserved2;
	uint16_t reserved3;
	uint16_t iomap_base;
};

struct __attribute__((packed)) gdtr {
	uint16_t limit;
	uint64_t base;
};

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct tss tss0 __attribute__((aligned(16)));
static uint8_t ist_double_fault[16384] __attribute__((aligned(16)));
static uint8_t ist_nmi[16384] __attribute__((aligned(16)));

void gdt_load(const struct gdtr *g, uint64_t code, uint64_t data);

void gdt_init(void)
{
	gdt[0] = 0;
	gdt[1] = 0x00AF9A000000FFFFULL; /* noyau, code 64 bits */
	gdt[2] = 0x00CF92000000FFFFULL; /* noyau, données */
	gdt[3] = 0x00CFF2000000FFFFULL; /* utilisateur, données */
	gdt[4] = 0x00AFFA000000FFFFULL; /* utilisateur, code 64 bits */

	tss0.ist[0] = (uint64_t)(ist_double_fault + sizeof ist_double_fault);
	tss0.ist[1] = (uint64_t)(ist_nmi + sizeof ist_nmi);
	tss0.iomap_base = sizeof tss0;
	uint64_t base = (uint64_t)&tss0, limit = sizeof tss0 - 1;
	gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ULL << 40) |
		 (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
	gdt[6] = base >> 32;

	struct gdtr g = {sizeof gdt - 1, (uint64_t)gdt};
	gdt_load(&g, SEL_KCODE, SEL_KDATA);
	__asm__ volatile("ltr %w0" ::"r"(SEL_TSS));
}
