#ifndef KORIN_PC_APIC_H
#define KORIN_PC_APIC_H

#include <stdint.h>

#define VEC_TIMER 0x30
#define VEC_KEYBOARD 0x31
#define VEC_SPURIOUS 0xFF

void apic_init(void);
void ioapic_route_isa(uint8_t irq, uint8_t vector);
uint32_t lapic_id(void);
void apic_print_info(void);

#endif
