/* Banc différentiel du port d'AudioKit en C (Linux, WSL) : les mêmes appels
 * sur les codecs en asm (build/audiokit_cmp/akref.bin, sous qemu-aarch64) et
 * sur le C ; chaque sortie est comparée octet par octet.
 *
 *   sh src/kits/audiokit/test/cmp/build.sh NOM...   construit cmp_NOM (aarch64 et x86-64)
 *   sh src/kits/audiokit/test/cmp/run.sh NOM...     lance : asm contre C (aarch64), puis C
 *                                                   x86-64 contre C aarch64
 *
 * Un essai (cmp_NOM.c) : des fonctions void t(int ref) enregistrées par
 * RUN(t) dans main ; dans t, AKCALL(ref, fonction, args...) appelle l'asm
 * (ref = 1) ou le C (ref = 0) ; OUT("nom", données, octets) range une sortie.
 * Les deux passes doivent produire les mêmes sorties dans le même ordre ; run.sh
 * compare aussi les lignes imprimées (rien qui n'existe que sur aarch64). Un
 * symbole absent devient un bouchon faible (stubs.c) ; un essai peut définir sa
 * propre doublure faible. Fichiers fabriqués : mkflac.py, mkmp3.py, mkcelt.py ;
 * KCC_A64=... (build.sh) : codecs compilés par un autre GCC (ex. celui de Korin).
 * Les codecs en asm sont assemblés par mkref.py (Windows : FASMARM) avec la table
 * de toutes leurs étiquettes ; une fonction asm qui ne suit pas l'AAPCS ne
 * s'appelle pas par AKCALL. */
#ifndef CMP_H
#define CMP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "akref_syms.h"

/* adresse d'une étiquette de la référence en asm (0 sur x86-64) */
void *refaddr(int idx);
/* appel : ref = 1 asm (registres gardés par un relais), 0 C */
long akcall(void *fn, int ref, long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7);

#define AK_NARG_(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define AK_NARG(...) AK_NARG_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define AK_CAT_(a, b) a##b
#define AK_CAT(a, b) AK_CAT_(a, b)
#define AK_A0() 0, 0, 0, 0, 0, 0, 0, 0
#define AK_A1(a) (long)(a), 0, 0, 0, 0, 0, 0, 0
#define AK_A2(a, b) (long)(a), (long)(b), 0, 0, 0, 0, 0, 0
#define AK_A3(a, b, c) (long)(a), (long)(b), (long)(c), 0, 0, 0, 0, 0
#define AK_A4(a, b, c, d) (long)(a), (long)(b), (long)(c), (long)(d), 0, 0, 0, 0
#define AK_A5(a, b, c, d, e) (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), 0, 0, 0
#define AK_A6(a, b, c, d, e, f) (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f), 0, 0
#define AK_A7(a, b, c, d, e, f, g) (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f), (long)(g), 0
#define AK_A8(a, b, c, d, e, f, g, h) \
	(long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f), (long)(g), (long)(h)
#define AK_ARGS(...) AK_CAT(AK_A, AK_NARG(__VA_ARGS__))(__VA_ARGS__)

/* fonction C déclarée ailleurs (prototype quelconque) : appelée par son adresse */
#define AKCALL(ref, fn, ...) akcall((ref) ? refaddr(REF_##fn) : (void *)(fn), (ref), AK_ARGS(__VA_ARGS__))
#define AKCALL0(ref, fn) akcall((ref) ? refaddr(REF_##fn) : (void *)(fn), (ref), AK_A0())

/* rappel C donné à l'asm : relais qui garde x1-x18 et v0-v31 (comme AppKit) ;
 * slot 0..7 ; rend fn tel quel pour le C */
void *refcb(int ref, int slot, void *fn);

/* sorties */
void OUT(const char *name, const void *p, size_t n);
void OUTV(const char *name, long v); /* une valeur */
uint32_t crc32(const void *p, size_t n);
/* fichier entier en mémoire (malloc) ; 0 si absent */
uint8_t *load(const char *path, size_t *n);

void run_test(const char *name, void (*t)(int ref));
#define RUN(t) run_test(#t, t)
int cmp_end(void); /* code de sortie : 0 si tout est égal */

extern int cmp_has_ref; /* 1 sur aarch64 (référence chargée) */

#endif
