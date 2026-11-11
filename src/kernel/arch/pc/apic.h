#ifndef KORIN_PC_APIC_H
#define KORIN_PC_APIC_H

#include <stdint.h>

#define VEC_TIMER 0x30
#define VEC_KEYBOARD 0x31
#define VEC_LINE0 0x40 /* lignes des pilotes : 0x40 + GSI */
#define VEC_TLB 0xF0   /* invalidation des TLB entre cœurs */
#define VEC_SPURIOUS 0xFF

void apic_init(void);
void ioapic_route_isa(uint8_t irq, uint8_t vector);
uint32_t lapic_id(void);
void apic_print_info(void);
void apic_init_ap(void);
void lapic_send_ipi(uint32_t apic_id, uint32_t low);
void udelay(uint64_t us);

#endif
