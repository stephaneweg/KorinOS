/* Extinction et redémarrage du PC (SYS_POWER, docs/etude/PC-PILOTES.md, « Extinction et
 * redémarrage du PC »), d'après la spécification ACPI 6 (chapitres 4.8, 5.2.9, 7.4.2, 16) :
 *  * extinction : l'état S5 (« soft off »). SLP_TYPa / SLP_TYPb viennent du paquet \_S5_
 *    du DSDT, lu sans interprète d'AML (le nom, puis Package (n) { a, b, … }) ; si le
 *    micrologiciel n'a pas encore passé la machine en mode ACPI (SCI_EN à 0), ACPI_ENABLE est
 *    écrit dans SMI_CMD ; puis SLP_TYP, et SLP_TYP | SLP_EN dans PM1a_CNT (et PM1b_CNT) ; une
 *    machine « à matériel réduit » : SLEEP_CONTROL_REG. Pas d'AML : ni _PTS ni _GTS (la plupart
 *    des machines s'éteignent sans) ; si la machine tourne encore 3 s après, un message à
 *    l'écran dit qu'on peut l'éteindre au bouton ;
 *  * redémarrage : RESET_REG de la FADT (si RESET_REG_SUP), puis le registre 0xCF9 du chipset
 *    (0x02 puis 0x06 : remise à zéro complète), puis le contrôleur clavier (0xFE dans 0x64),
 *    puis une triple faute ;
 *  * avant les deux : les autres cœurs arrêtés (une NMI à chacun : arrêté même interruptions
 *    coupées). */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/lib.h>

#include "../x86_64/cpu.h"
#include "acpi.h"
#include "apic.h"

#define SLP_EN (1u << 13)
#define SLP_TYP_SHIFT 10
#define SLP_TYP_MASK (7u << SLP_TYP_SHIFT)
#define SCI_EN 1u
#define WAK_STS (1u << 15)
#define HWR_SLP_EN (1u << 5) /* SLEEP_CONTROL_REG du matériel réduit */
#define HWR_SLP_TYP_SHIFT 2

static inline void outw16(uint16_t port, uint16_t v)
{
	__asm__ volatile("outw %0, %1" ::"a"(v), "Nd"(port));
}

static inline uint16_t inw16(uint16_t port)
{
	uint16_t v;
	__asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

static uint16_t reg16_read(uint8_t space, uint64_t addr)
{
	if (space == 1)
		return inw16((uint16_t)addr);
	return *(volatile uint16_t *)arch_phys_to_virt(addr);
}

static void reg16_write(uint8_t space, uint64_t addr, uint16_t v)
{
	if (space == 1)
		outw16((uint16_t)addr, v);
	else
		*(volatile uint16_t *)arch_phys_to_virt(addr) = v;
}

static void reg8_write(uint8_t space, uint64_t addr, uint8_t v)
{
	if (space == 1)
		outb((uint16_t)addr, v);
	else
		*(volatile uint8_t *)arch_phys_to_virt(addr) = v;
}

/* ---- \_S5_ dans l'AML : NameOp (0x08), « \ » facultatif, « _S5_ », PackageOp (0x12),
 * PkgLength (1 à 4 octets), NumElements, puis les éléments (ZeroOp, OneOp, BytePrefix 0x0A
 * n, WordPrefix 0x0B, DWordPrefix 0x0C) */

static const uint8_t *aml_int(const uint8_t *p, const uint8_t *end, uint8_t *out)
{
	if (p >= end)
		return 0;
	switch (*p) {
	case 0x00: /* ZeroOp */
		*out = 0;
		return p + 1;
	case 0x01: /* OneOp */
		*out = 1;
		return p + 1;
	case 0x0A: /* BytePrefix */
		if (p + 2 > end)
			return 0;
		*out = p[1];
		return p + 2;
	case 0x0B: /* WordPrefix */
		if (p + 3 > end)
			return 0;
		*out = p[1];
		return p + 3;
	case 0x0C: /* DWordPrefix */
		if (p + 5 > end)
			return 0;
		*out = p[1];
		return p + 5;
	}
	return 0;
}

static int find_s5(const uint8_t *t, uint32_t len)
{
	if (len <= 36)
		return 0;
	const uint8_t *end = t + len;
	for (const uint8_t *p = t + 36; p + 8 < end; p++) {
		if (p[0] != '_' || p[1] != 'S' || p[2] != '5' || p[3] != '_')
			continue;
		if (!(p[-1] == 0x08 || (p[-1] == '\\' && p[-2] == 0x08)))
			continue;
		if (p[4] != 0x12) /* PackageOp */
			continue;
		const uint8_t *q = p + 5;
		q += ((*q >> 6) & 3) + 1; /* PkgLength */
		if (q >= end || *q < 1) /* NumElements */
			continue;
		uint8_t n = *q++;
		uint8_t a = 0, b = 0;
		q = aml_int(q, end, &a);
		if (!q)
			continue;
		if (n >= 2 && !aml_int(q, end, &b))
			b = 0;
		acpi.s5a = a & 7;
		acpi.s5b = b & 7;
		return 1;
	}
	return 0;
}

void acpi_power_init(void)
{
	uint32_t len = 0;
	const uint8_t *dsdt = acpi_table("DSDT", 0, &len);
	acpi.s5_found = dsdt && find_s5(dsdt, len);
	for (unsigned i = 0; !acpi.s5_found && i < 16; i++) { /* (rarement : dans une SSDT) */
		const uint8_t *s = acpi_table("SSDT", i, &len);
		if (!s)
			break;
		acpi.s5_found = find_s5(s, len);
	}
	int sci = 0;
	if (acpi.pm1a_cnt && !acpi.hw_reduced)
		sci = reg16_read(acpi.pm1_cnt_space, acpi.pm1a_cnt) & SCI_EN;
	kprintf("ACPI : extinction %s (SLP_TYP %u/%u), PM1a_CNT %s %lx, PM1b_CNT %lx, SMI_CMD %x "
		"ACPI_ENABLE %x, mode ACPI %s ; redémarrage %s %lx = %x\n",
		acpi.s5_found ? "\\_S5_" : "sans \\_S5_ (SLP_TYP 0 essayé)", acpi.s5a, acpi.s5b,
		acpi.pm1_cnt_space == 1 ? "port" : "mémoire", acpi.pm1a_cnt, acpi.pm1b_cnt, acpi.smi_cmd,
		acpi.acpi_enable, acpi.hw_reduced ? "(matériel réduit)" : sci ? "actif" : "inactif",
		acpi.has_reset ? (acpi.reset_space == 1 ? "port" : acpi.reset_space == 2 ? "PCI" : "mémoire") : "(pas de RESET_REG)",
		acpi.reset_addr, acpi.reset_value);
}

/* ---- les autres cœurs : une NMI à chacun (raccourci « tous sauf moi ») */

static void nmi_stop(struct regs *r)
{
	(void)r;
	arch_halt_forever();
}

static void stop_other_cpus(void)
{
	static int done;
	if (done || arch_cpu_count() <= 1)
		return;
	done = 1;
	idt_set_handler(2, nmi_stop);
	lapic_send_ipi(0, 0xC0400); /* tous sauf soi (bits 18-19), mode NMI (bits 8-10) */
	udelay(20000);
}

/* le mode ACPI : SCI_EN à 1, par SMI_CMD / ACPI_ENABLE si le micrologiciel ne l'a pas fait */
static void acpi_mode_on(void)
{
	if (acpi.hw_reduced || !acpi.pm1a_cnt)
		return;
	if (reg16_read(acpi.pm1_cnt_space, acpi.pm1a_cnt) & SCI_EN)
		return;
	if (!acpi.smi_cmd || !acpi.acpi_enable) {
		kprintf("ACPI : mode hérité, sans SMI_CMD pour en sortir\n");
		return;
	}
	outb((uint16_t)acpi.smi_cmd, acpi.acpi_enable);
	for (int i = 0; i < 300; i++) { /* 3 s au plus */
		if (reg16_read(acpi.pm1_cnt_space, acpi.pm1a_cnt) & SCI_EN)
			return;
		udelay(10000);
	}
	kprintf("ACPI : SCI_EN toujours à 0 après ACPI_ENABLE\n");
}

static void slp_write(uint64_t cnt, uint8_t typ)
{
	uint16_t v = (uint16_t)((reg16_read(acpi.pm1_cnt_space, cnt) & ~SLP_TYP_MASK) | ((unsigned)typ << SLP_TYP_SHIFT));
	reg16_write(acpi.pm1_cnt_space, cnt, v);
	reg16_write(acpi.pm1_cnt_space, cnt, (uint16_t)(v | SLP_EN));
}

__attribute__((noreturn)) void arch_power_off(void)
{
	stop_other_cpus();
	cli();
	if (acpi.hw_reduced && acpi.sleep_ctl) {
		reg8_write(acpi.sleep_ctl_space, acpi.sleep_ctl,
			   (uint8_t)(((acpi.s5a & 7) << HWR_SLP_TYP_SHIFT) | HWR_SLP_EN));
	} else if (acpi.pm1a_cnt) {
		acpi_mode_on();
		if (acpi.pm1a_evt) /* WAK_STS effacé (écrire 1) */
			reg16_write(acpi.pm1_evt_space, acpi.pm1a_evt, WAK_STS);
		__asm__ volatile("wbinvd" ::: "memory");
		slp_write(acpi.pm1a_cnt, acpi.s5a);
		if (acpi.pm1b_cnt)
			slp_write(acpi.pm1b_cnt, acpi.s5b);
	}
	udelay(3000000);
	/* toujours là : le message, à la place du bureau */
	console_silence(0);
	console_clear();
	console_color(CON_WARN);
	kprintf("\n\n  Korin OS est arrêté : les fichiers sont enregistrés et les disques arrêtés.\n\n"
		"  L'extinction par l'ACPI n'a pas eu lieu : vous pouvez éteindre l'ordinateur\n"
		"  (bouton d'alimentation tenu quelques secondes).\n");
	console_color(CON_NORMAL);
	arch_halt_forever();
}

static void pci_cfg_write8(uint64_t a, uint8_t v)
{
	/* adresse de RESET_REG dans l'espace de configuration du bus 0 : périphérique bits
	 * 32-47, fonction bits 16-31, registre bits 0-15 */
	uint32_t dev = (uint32_t)(a >> 32) & 31, fn = (uint32_t)(a >> 16) & 7, off = (uint32_t)a & 0xFF;
	outl(0xCF8, 0x80000000u | (dev << 11) | (fn << 8) | (off & 0xFC));
	outb((uint16_t)(0xCFC + (off & 3)), v);
}

void arch_reboot(void)
{
	stop_other_cpus();
	cli();
	if (acpi.has_reset) {
		if (acpi.reset_space == 1)
			outb((uint16_t)acpi.reset_addr, acpi.reset_value);
		else if (acpi.reset_space == 0)
			*(volatile uint8_t *)arch_phys_to_virt(acpi.reset_addr) = acpi.reset_value;
		else if (acpi.reset_space == 2)
			pci_cfg_write8(acpi.reset_addr, acpi.reset_value);
		udelay(500000);
	}
	outb(0xCF9, 0x02); /* registre de remise à zéro du chipset : préparer, puis complète */
	udelay(10);
	outb(0xCF9, 0x06);
	udelay(500000);
	for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
		;
	outb(0x64, 0xFE); /* contrôleur clavier */
	udelay(500000);
	/* triple faute : une IDT vide, puis une exception */
	static const struct __attribute__((packed)) {
		uint16_t limit;
		uint64_t base;
	} none = {0, 0};
	__asm__ volatile("lidt %0; int3" ::"m"(none));
	arch_halt_forever();
}
