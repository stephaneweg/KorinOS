/* w16.h -- win16 : programmes de Windows 3.x (NE, Win16) sur Korin OS (docs/etude/WIN16.md).
 *
 * Programme sous GPL-2 (repris de winevdm, github.com/otya128/winevdm) ; le cœur du processeur (mame/) garde sa
 * licence BSD-3-Clause, les .spec (spec/) viennent de Wine (LGPL-2.1+). */
#ifndef W16_H
#define W16_H
#include <stdint.h>
#include <stddef.h>
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- la carte de la mémoire émulée (adresses linéaires) ---- */
#define LDT_LIN		0x10000		/* la LDT : 8192 descripteurs */
#define LDT_ENTRIES	8192
#define IDT_LIN		0x20000		/* l'IDT : 256 portes 16 bits */
#define THUNK_LIN	0x21000		/* 64 Ko de zéros : le corps des segments de points d'entrée */
#define HEAP_LIN	0x40000		/* le tas global, jusqu'à W16_MEM_SIZE */

/* ---- sélecteurs (mem.c) ---- */
#define SEL(i)		((uint16_t) (((i) << 3) | 7))	/* LDT, RPL 3 */
#define SEL_INDEX(s)	((s) >> 3)
uint16_t sel_alloc (uint32_t base, uint32_t size, int code);
void sel_set (uint16_t sel, uint32_t base, uint32_t size, int code);
void sel_free (uint16_t sel);
uint32_t sel_base (uint16_t sel);
uint32_t sel_limit (uint16_t sel);
int sel_valid (uint16_t sel);

/* le tas global (GlobalAlloc) : un bloc a un sélecteur, poignée = sélecteur */
uint16_t global_alloc (uint32_t size, int code);
void global_free (uint16_t sel);
uint32_t global_size (uint16_t sel);
uint32_t lin_alloc (uint32_t size);	/* mémoire linéaire sans sélecteur */
void lin_free (uint32_t lin);

void mem_init (void);

/* accès à la mémoire émulée */
static inline uint8_t *lin_ptr (uint32_t lin) { return w16_mem + (lin & (W16_MEM_SIZE - 1)); }
static inline uint32_t far_lin (uint32_t segptr) { return sel_base ((uint16_t) (segptr >> 16)) + (segptr & 0xffff); }
static inline void *far_ptr (uint32_t segptr) { return segptr ? lin_ptr (far_lin (segptr)) : 0; }
static inline uint16_t rd16 (uint32_t lin) { uint8_t *p = lin_ptr (lin); return (uint16_t) (p[0] | p[1] << 8); }
static inline uint32_t rd32 (uint32_t lin) { return rd16 (lin) | (uint32_t) rd16 (lin + 2) << 16; }
static inline void wr16 (uint32_t lin, uint16_t v) { uint8_t *p = lin_ptr (lin); p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); }
static inline void wr32 (uint32_t lin, uint32_t v) { wr16 (lin, (uint16_t) v); wr16 (lin + 2, (uint16_t) (v >> 16)); }
#define MAKESEGPTR(s, o) (((uint32_t) (s) << 16) | (uint16_t) (o))
#define SELECTOROF(p) ((uint16_t) ((p) >> 16))
#define OFFSETOF(p) ((uint16_t) (p))

/* ---- les DLL intégrées (api_table.c, généré par tools/win16/spec2c.py) ---- */
struct w16call
{
	uint32_t a[16];		/* les arguments, dans l'ordre de la déclaration (pointeurs : seg:off) */
	int n;
	uint32_t ret;		/* le résultat : AX (et DX si la fonction rend un long) */
	int mod, ord;
};
typedef void (*w16_fn) (struct w16call *c);
struct w16_api
{
	uint16_t ord;
	uint8_t conv;		/* 0 pascal, 1 register (registres du processeur), 2 cdecl, 3 stub */
	uint8_t ret16;
	const char *name;
	const char *args;	/* w word, s s_word, l long, p ptr, t str, P segptr, T segstr, d double */
	w16_fn fn;
};
struct w16_module
{
	const char *name;
	const struct w16_api *api;
	int count;
};
extern const struct w16_module w16_modules[];
enum { MOD_KERNEL, MOD_USER, MOD_GDI };

int api_find_module (const char *name);
int api_find_name (int mod, const char *name);
uint32_t api_entry (int mod, int ord);	/* seg:off du point d'entrée de cet ordinal */

/* ---- l'exécution (run.c) ---- */
void run_init (void);
int run_until_return (uint16_t ret_off);	/* fait tourner le processeur jusqu'au retour d'un rappel */
uint32_t call16 (uint32_t target, const uint16_t *args, int nwords, uint16_t ds);	/* appel Pascal vers le 16 bits */
void push16 (uint16_t v);
uint16_t pop16 (void);
extern int w16_exit, w16_exit_code;
extern int w16_trace;
void w16_fatal (const char *fmt, ...) __attribute__ ((noreturn, format (printf, 1, 2)));
void w16_log (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

/* ---- le programme chargé (ne.c, kernel.c) ---- */
struct w16_task
{
	uint16_t hinstance;	/* = le sélecteur des données automatiques (DGROUP) */
	uint16_t hmodule;
	uint16_t psp;		/* le PSP : ligne de commande en 0x80 */
	uint16_t env;
	uint16_t cs, ip, ss, sp;
	uint16_t heap_size, stack_size;
	uint16_t cmdshow;
	uint16_t segs[256];	/* sélecteur de chaque segment du NE (1..n) */
	int nsegs;
	char path[512];		/* le chemin Korin du .exe */
	char dospath[128];	/* le même, vu par le programme (C:\...) */
	char modname[16];
	uint32_t rsrc_off;	/* table des ressources (dans le fichier) */
	uint8_t *file;
	size_t file_size;
};
extern struct w16_task task;
int ne_load (const char *path);
const char *ne_resource_find (int type, uint32_t name, uint32_t *size);

/* ---- les modules ---- */
void dos_int21 (void);
void kernel_init (void);
void user_init (void);
int user_window_count (void);
void gdi_init (void);
const char *w16_str (uint32_t segptr);	/* chaîne de l'app (CP1252) -> UTF-8 (tampon tournant) */

#ifdef __cplusplus
}
#endif
#endif
