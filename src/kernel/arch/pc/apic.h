#ifndef KORIN_PC_APIC_H
#define KORIN_PC_APIC_H

#include <stdint.h>

#define VEC_TIMER 0x30
#define VEC_KEYBOARD 0x31
#define VEC_LINE0 0x40 /* lignes des pilotes : 0x40 + GSI (GSI < 0x78) */
#define VEC_MSI0 0xB8  /* MSI/MSI-X : 0xB8-0xEF de chaque cœur (msi.c) */
#define VEC_MSI_END 0xF0
#define VEC_TLB 0xF0   /* invalidation des TLB entre cœurs */
#define VEC_SPURIOUS 0xFF

void apic_init(void);
void ioapic_route_isa(uint8_t irq, uint8_t vector);
uint32_t lapic_id(void);
void apic_print_info(void);
void apic_init_ap(void);
void lapic_send_ipi(uint32_t apic_id, uint32_t low);
void udelay(uint64_t us);
void msi_init(void);
int msi_line_valid(unsigned line);
void msi_line_enable(unsigned line, int on);
void msi_print_info(void);

#endif
