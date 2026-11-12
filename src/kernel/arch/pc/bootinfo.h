/* Bloc de démarrage passé par BOOTX64.EFI au noyau x86-64.
 * Même disposition que boot/bootinfo.inc : changer les deux ensemble. */
#ifndef KORIN_PC_BOOTINFO_H
#define KORIN_PC_BOOTINFO_H

#include <stdint.h>

#define BOOTINFO_MAGIC 0x31544F424E49524BULL /* "KRINBOT1" */

struct bootinfo {
	uint64_t magic;
	uint64_t fb_base;     /* physique */
	uint64_t fb_size;
	uint32_t fb_width;
	uint32_t fb_height;
	uint32_t fb_pitch;    /* pixels par ligne */
	uint32_t fb_format;   /* 0 = RGB, 1 = BGR */
	uint64_t mmap;        /* physique : descripteurs EFI_MEMORY_DESCRIPTOR */
	uint64_t mmap_size;
	uint64_t mmap_desc_size;
	uint64_t rsdp;        /* physique, 0 sinon */
	uint64_t kernel_phys; /* bloc du noyau : image + tables du chargeur */
	uint64_t kernel_size;
	uint64_t kernel_virt;
	uint64_t initrd;      /* physique : archive ustar, 0 sinon */
	uint64_t initrd_size;
	char cmdline[128];    /* options du noyau (boot.cfg du chargeur, « cmdline = »), terminées par 0 */
};

_Static_assert(sizeof(struct bootinfo) == 240, "bootinfo.inc");

/* EFI_MEMORY_DESCRIPTOR */
struct efi_mdesc {
	uint32_t type;
	uint32_t pad;
	uint64_t phys;
	uint64_t virt;
	uint64_t pages;
	uint64_t attr;
};

enum {
	EFI_LOADER_CODE = 1,
	EFI_LOADER_DATA = 2,
	EFI_BOOT_CODE = 3,
	EFI_BOOT_DATA = 4,
	EFI_CONVENTIONAL = 7,
	EFI_ACPI_RECLAIM = 9,
};

#endif
