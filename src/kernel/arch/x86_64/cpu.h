/* Instructions du processeur x86-64 utilisées depuis le C. */
#ifndef KORIN_X86_CPU_H
#define KORIN_X86_CPU_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t v) { __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(port)); }
static inline void outw(uint16_t port, uint16_t v) { __asm__ volatile("outw %0, %1" ::"a"(v), "Nd"(port)); }
static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile("outl %0, %1" ::"a"(v), "Nd"(port)); }
static inline uint8_t inb(uint16_t port)
{
	uint8_t v;
	__asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}
static inline uint32_t inl(uint16_t port)
{
	uint32_t v;
	__asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}
static inline void io_wait(void) { outb(0x80, 0); }

static inline uint64_t rdmsr(uint32_t msr)
{
	uint32_t lo, hi;
	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
	return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v)
{
	__asm__ volatile("wrmsr" ::"c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

static inline void cpuid(uint32_t leaf, uint32_t sub, uint32_t r[4])
{
	__asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}

static inline uint64_t rdtsc(void)
{
	uint32_t lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t read_cr2(void)
{
	uint64_t v;
	__asm__ volatile("mov %%cr2, %0" : "=r"(v));
	return v;
}
static inline uint64_t read_cr3(void)
{
	uint64_t v;
	__asm__ volatile("mov %%cr3, %0" : "=r"(v));
	return v;
}
static inline void write_cr3(uint64_t v) { __asm__ volatile("mov %0, %%cr3" ::"r"(v) : "memory"); }

static inline void cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void sti(void) { __asm__ volatile("sti" ::: "memory"); }
static inline void hlt(void) { __asm__ volatile("hlt" ::: "memory"); }
static inline void pause(void) { __asm__ volatile("pause"); }

#define MSR_APIC_BASE 0x1B
#define MSR_PAT 0x277

/* pile d'une interruption, dans l'ordre de isr.asm */
struct regs {
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
	uint64_t vector, error;
	uint64_t rip, cs, rflags, rsp, ss;
};

typedef void (*irq_handler)(struct regs *r);

void gdt_init(void);
void idt_init(void);
void idt_set_handler(uint8_t vector, irq_handler h);
/* fin d'interruption, fournie par la carte (APIC local) */
void irq_eoi(void);

#endif
