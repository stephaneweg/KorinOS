/*
 * cpu.cpp -- le processeur de win16 : le cœur i486 de MAME (mame/emu/cpu/i386, BSD-3-Clause, tel que winevdm et
 * MS-DOS Player le pilotent : une instruction par CPU_EXECUTE), sur une mémoire émulée à part (w16_mem : adresse
 * linéaire = décalage dans le tableau ; winevdm, lui, prend les pointeurs de son hôte Win32 32 bits).
 *
 * Ce fichier reprend la colle de vm86/msdos.cpp de winevdm (GPL-2) : types et constantes de MAME, accès mémoire.
 * Il ne montre au reste de win16 que les fonctions C de cpu.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include "cpu.h"

typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int8_t INT8;
typedef int16_t INT16;
typedef int32_t INT32;
typedef int64_t INT64;
typedef unsigned int UINT;
typedef uint16_t WORD;	/* (x87ops.c : les fonctions du x87 que winevdm exporte à ses DLL) */
typedef uint32_t DWORD;
#include <assert.h>

/* (i386op16.c, i386op32.c : l'IRET dans la zone des appels de MS-DOS Player ; aucune ici) */
#define IRET_TOP 0
#define IRET_SIZE 0

#define fatalerror(...) { fprintf (stderr, __VA_ARGS__); abort (); }
#define error(...) fprintf (stderr, "error: " __VA_ARGS__)

/* (comme msdos.cpp : un i486 avec son x87) */
#define SUPPORT_DISASSEMBLER
#define CPU_MODEL i486
#define SUPPORT_FPU
#define HAS_I386
#define LSB_FIRST
#ifndef INLINE
#define INLINE inline
#endif
#define U64(v) UINT64(v)

int ignore_illegal_insn;
int cpu_trace;

static void logerror (const char *format, ...)
{
	if (!cpu_trace)
		return;
	va_list arg;
	va_start (arg, format);
	vfprintf (stderr, format, arg);
	va_end (arg);
}
#define popmessage(...)

/* ---- src/emu/devcpu.h ---- */
#define CPU_INIT_NAME(name)			cpu_init_##name
#define CPU_INIT(name)				void CPU_INIT_NAME(name)()
#define CPU_INIT_CALL(name)			CPU_INIT_NAME(name)()
#define CPU_RESET_NAME(name)			cpu_reset_##name
#define CPU_RESET(name)				void CPU_RESET_NAME(name)()
#define CPU_RESET_CALL(name)			CPU_RESET_NAME(name)()
#define CPU_EXECUTE_NAME(name)			cpu_execute_##name
#define CPU_EXECUTE(name)			void CPU_EXECUTE_NAME(name)()
#define CPU_EXECUTE_CALL(name)			CPU_EXECUTE_NAME(name)()
#define CPU_TRANSLATE_NAME(name)		cpu_translate_##name
#define CPU_TRANSLATE(name)			int CPU_TRANSLATE_NAME(name)(address_spacenum space, int intention, offs_t *address)
#define CPU_TRANSLATE_CALL(name)		CPU_TRANSLATE_NAME(name)(space, intention, address)
#define CPU_DISASSEMBLE_NAME(name)		cpu_disassemble_##name
#define CPU_DISASSEMBLE(name)			int CPU_DISASSEMBLE_NAME(name)(char *buffer, offs_t eip, const UINT8 *oprom)
#define CPU_DISASSEMBLE_CALL(name)		CPU_DISASSEMBLE_NAME(name)(buffer, eip, oprom)

/* ---- src/emu/didisasm.h ---- */
const UINT32 DASMFLAG_SUPPORTED     = 0x80000000;
const UINT32 DASMFLAG_STEP_OUT      = 0x40000000;
const UINT32 DASMFLAG_STEP_OVER     = 0x20000000;
const UINT32 DASMFLAG_OVERINSTMASK  = 0x18000000;
const UINT32 DASMFLAG_OVERINSTSHIFT = 27;
const UINT32 DASMFLAG_LENGTHMASK    = 0x0000ffff;

/* ---- src/emu/diexec.h ---- */
enum line_state { CLEAR_LINE = 0, ASSERT_LINE, HOLD_LINE, PULSE_LINE };
enum { INPUT_LINE_IRQ = 0, INPUT_LINE_NMI };

/* ---- src/emu/dimemory.h ---- */
const int TRANSLATE_TYPE_MASK       = 0x03;
const int TRANSLATE_USER_MASK       = 0x04;
const int TRANSLATE_DEBUG_MASK      = 0x08;
const int TRANSLATE_READ            = 0;
const int TRANSLATE_WRITE           = 1;
const int TRANSLATE_FETCH           = 2;
const int TRANSLATE_READ_USER       = (TRANSLATE_READ | TRANSLATE_USER_MASK);
const int TRANSLATE_WRITE_USER      = (TRANSLATE_WRITE | TRANSLATE_USER_MASK);
const int TRANSLATE_FETCH_USER      = (TRANSLATE_FETCH | TRANSLATE_USER_MASK);
const int TRANSLATE_READ_DEBUG      = (TRANSLATE_READ | TRANSLATE_DEBUG_MASK);
const int TRANSLATE_WRITE_DEBUG     = (TRANSLATE_WRITE | TRANSLATE_DEBUG_MASK);
const int TRANSLATE_FETCH_DEBUG     = (TRANSLATE_FETCH | TRANSLATE_DEBUG_MASK);

/* ---- src/emu/emucore.h ---- */
enum endianness_t { ENDIANNESS_LITTLE, ENDIANNESS_BIG };
const endianness_t ENDIANNESS_NATIVE = ENDIANNESS_LITTLE;
#define ENDIAN_VALUE_LE_BE(endian,leval,beval)	(((endian) == ENDIANNESS_LITTLE) ? (leval) : (beval))
#define NATIVE_ENDIAN_VALUE_LE_BE(leval,beval)	ENDIAN_VALUE_LE_BE(ENDIANNESS_NATIVE, leval, beval)

/* ---- src/emu/memory.h ---- */
enum address_spacenum { AS_0, AS_1, AS_2, AS_3, ADDRESS_SPACES, AS_PROGRAM = AS_0, AS_DATA = AS_1, AS_IO = AS_2 };
typedef UINT32 offs_t;

/* la mémoire émulée : W16_MEM_SIZE octets (une puissance de deux), plus 4 pour les lectures en bord */
uint8_t *w16_mem;
static UINT8 *mem;
#define MEMMASK (W16_MEM_SIZE - 1)

static void *read_ptr (offs_t a) { return mem + (a & MEMMASK); }
static inline UINT8 read_byte (offs_t a) { return mem[a & MEMMASK]; }
static inline UINT16 read_word (offs_t a) { UINT16 v; memcpy (&v, mem + (a & MEMMASK), 2); return v; }
static inline UINT32 read_dword (offs_t a) { UINT32 v; memcpy (&v, mem + (a & MEMMASK), 4); return v; }
static inline void write_byte (offs_t a, UINT8 v) { mem[a & MEMMASK] = v; }
static inline void write_word (offs_t a, UINT16 v) { memcpy (mem + (a & MEMMASK), &v, 2); }
static inline void write_dword (offs_t a, UINT32 v) { memcpy (mem + (a & MEMMASK), &v, 4); }

#define read_decrypted_byte read_byte
#define read_decrypted_word read_word
#define read_decrypted_dword read_dword
#define read_raw_byte read_byte
#define write_raw_byte write_byte
#define read_word_unaligned read_word
#define write_word_unaligned write_word
#define read_io_word_unaligned read_io_word
#define write_io_word_unaligned write_io_word

/* les ports d'E/S : rien derrière (un programme Win16 n'y touche pas ; les rares le font par les VxD) */
static UINT8 read_io_byte (offs_t a) { (void) a; return 0xff; }
static UINT16 read_io_word (offs_t a) { (void) a; return 0xffff; }
static UINT32 read_io_dword (offs_t a) { (void) a; return 0xffffffff; }
static void write_io_byte (offs_t a, UINT8 v) { (void) a; (void) v; }
static void write_io_word (offs_t a, UINT16 v) { (void) a; (void) v; }
static void write_io_dword (offs_t a, UINT32 v) { (void) a; (void) v; }

#define ARRAY_LENGTH(x)     (sizeof(x) / sizeof(x[0]))

static int pic_ack (void) { return 0; }
void msdos_syscall (unsigned int a) { (void) a; }

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#include "mame/lib/softfloat/softfloat.c"
#include "mame/lib/softfloat/fsincos.c"
#include "mame/lib/softfloat/f2xm1.c"
#include "mame/lib/softfloat/fpatan.c"
#include "mame/lib/softfloat/fyl2x.c"
#include "mame/emu/cpu/i386/i386.c"
#include "mame/emu/cpu/vtlb.c"
#include "mame/emu/cpu/i386/i386dasm.c"

/* ======================================================================== l'interface C (cpu.h) */

extern "C" {

int cpu_init (void)
{
	w16_mem = (uint8_t *) calloc (1, W16_MEM_SIZE + 4);
	if (!w16_mem)
		return 0;
	mem = w16_mem;
	CPU_INIT_CALL (CPU_MODEL);
	CPU_RESET_CALL (CPU_MODEL);
	m_cr[0] |= 0x20;	/* (NE : erreurs du x87 par exception 16, pas par l'IRQ 13 ; comme winevdm) */
	return 1;
}

/* le mode protégé à CPL 3, la LDT et l'IDT à ces adresses linéaires (la GDT est la LDT, comme winevdm) */
void cpu_set_tables (uint32_t ldt, uint32_t ldt_limit, uint32_t idt, uint32_t idt_limit)
{
	m_cr[0] |= 1;
	m_ldtr.base = ldt;
	m_ldtr.limit = ldt_limit;
	m_gdtr.base = ldt;
	m_gdtr.limit = ldt_limit;
	m_idtr.base = idt;
	m_idtr.limit = idt_limit;
	m_CPL = 3;
	m_IOP1 = 1;
	m_IOP2 = 1;
	m_eflags |= 0x3000;
}

uint32_t cpu_reg (int r) { return REG32 (r); }
void cpu_set_reg (int r, uint32_t v) { REG32 (r) = v; }
uint16_t cpu_sreg (int s) { return m_sreg[s].selector; }
uint32_t cpu_sreg_base (int s) { return m_sreg[s].base; }

/* charge un registre de segment (pas CS : cpu_jump) ; 0 est permis pour ES, FS, GS (et DS) */
void cpu_set_sreg (int s, uint16_t sel)
{
	m_sreg[s].selector = sel;
	i386_load_segment_descriptor (s);
}

uint32_t cpu_eip (void) { return m_eip; }

void cpu_jump (uint16_t cs, uint32_t eip)
{
	i386_protected_mode_jump (cs, eip, 1, 0);
	m_eip = eip;
	CHANGE_PC (m_eip);
}

uint32_t cpu_flags (void) { return get_flags (); }
void cpu_set_flags (uint32_t f) { set_flags (f); }

void cpu_step (void)
{
	CPU_EXECUTE_CALL (i386);
}

/* recharge les descripteurs déjà chargés (après un changement de la LDT) */
void cpu_reload_segments (void)
{
	for (int s = ES; s <= GS; s++)
		if (s != CS && m_sreg[s].selector)
			i386_load_segment_descriptor (s);
}

int cpu_disasm (char *buf, uint16_t cs, uint32_t eip)
{
	uint32_t base = 0;
	(void) cs;
	base = m_sreg[CS].base;
	return i386_dasm_one_ex (buf, eip, mem + ((base + eip) & MEMMASK), 16) & 0xff;
}

}
