/* svclib.h : ce que partagent les services du système écrits en C (busd,
 * hwmanager, hidmanager, init2...), repris des inclusions de l'asm :
 * ini_read et str_copy_n (src/inc/applist.inc), les constantes des
 * services (src/inc/armpi.inc). Compilé avec chaque service qui le demande
 * (svc.conf : « sources = ../svclib/svclib.c »). */
#ifndef KORIN_SVCLIB_H
#define KORIN_SVCLIB_H

#include "korinrt.h"

/* src/inc/armpi.inc */
#define SVF_ENDS 1
#define SVF_BIRTHS 2
#define SVF_MEMORY 4
#define SVF_POST 8
#define SVC_OP_END 0xFFFF
#define SVC_OP_SIGNAL 0xFFFD
#define INIT_OP_LAUNCH 1
#define INIT_OP_KIT_LOAD 2
#define INIT_OP_REBOOT 3
#define BUF_VA_BASE 0x6800000000ull /* tampon n : BUF_VA_BASE + n * BUF_SLOT */
#define BUF_SLOT 0x4000000ull

/* le message d'une requête (512 o) : +0 op, +8 a, +16 b, +24 c, +32 d, +40
 * longueur, +44 pid de l'appelant (32 bits), +48 données */
#define M64(m, off) (*(u64 *)((u8 *)(m) + (off)))
#define M32(m, off) (*(u32 *)((u8 *)(m) + (off)))

/* « cle = valeur » : fn(clé en minuscules, valeur), les deux sans blancs autour,
 * pour chaque ligne ; « # » commente la fin d'une ligne ; 4095 octets lus au plus
 * (ini_read d'applist.inc) -> 0 si le fichier manque */
typedef void (*ini_fn)(char *key, char *val);
int ini_read(const char *path, ini_fn fn);
void ini_parse(char *text, ini_fn fn); /* la même lecture, sur un texte terminé par 0 (modifié) */
char *ini_trim(char *s); /* sans espaces ni tabulations devant, sans blancs (<= ' ') derrière */

/* copie bornée : size octets au plus, 0 compris (str_copy_n d'applist.inc) */
void str_copy_n(char *d, const char *s, long size);

/* la sortie des services : puts et print_dec d'AppKit, comme l'asm (bl puts) */
static inline void say(const char *s) { ak_puts(s); }
static inline void say_dec(long v) { print_dec(v); }

#endif
