/* cpu.h -- le processeur émulé de win16 (cpu.cpp : le cœur i486 de MAME) */
#ifndef W16_CPU_H
#define W16_CPU_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define W16_MEM_SIZE (32u << 20)	/* la mémoire émulée : 32 Mo, adresses linéaires 0..W16_MEM_SIZE-1 */
extern uint8_t *w16_mem;
extern int cpu_trace;

/* les registres, rangés comme dans le cœur de MAME (REG32) */
enum { R_EAX, R_ECX, R_EDX, R_EBX, R_ESP, R_EBP, R_ESI, R_EDI };
/* les registres de segment */
enum { S_ES, S_CS, S_SS, S_DS, S_FS, S_GS };

int cpu_init (void);
void cpu_set_tables (uint32_t ldt, uint32_t ldt_limit, uint32_t idt, uint32_t idt_limit);
uint32_t cpu_reg (int r);
void cpu_set_reg (int r, uint32_t v);
uint16_t cpu_sreg (int s);
uint32_t cpu_sreg_base (int s);
void cpu_set_sreg (int s, uint16_t sel);
uint32_t cpu_eip (void);
void cpu_jump (uint16_t cs, uint32_t eip);
uint32_t cpu_flags (void);
void cpu_set_flags (uint32_t f);
void cpu_step (void);
void cpu_reload_segments (void);
int cpu_disasm (char *buf, uint16_t cs, uint32_t eip);

#ifdef __cplusplus
}
#endif
#endif
