/* AudioKit : MP3 (ak_mp3.c, d'après ak_mp3.inc) et ses tables (ak_mp3tab.c,
 * engendré par tools/audiotest/mp3tab2c.py d'après ak_mp3tab.inc). Les points
 * d'entrée du décodeur (mp3_open, mp3_init_pkt) sont dans ak.h. Les tables
 * de l'encodeur (mp3e_*) servent à ak_mp3enc.c. Flottants : leurs bits (u32,
 * lus par ak_u2f). */
#ifndef AK_MP3_H
#define AK_MP3_H

#include "ak.h"

#define MP3_QUADA_BITS 6

/* ---- décodeur */
/* répertoire : par table 0..31 : début (entrées de 16 bits), bits du premier
 * niveau (0 : table 0, tout à zéro), linbits, (0) */
extern const u16 mp3_huffdir[128];
/* feuille = longueur << 8 | x << 4 | y ; nœud = 0x8000 | bits << 11 | décalage */
extern const u16 mp3_huff[3498];
/* quadruplets, table A (count1table_select = 0), MP3_QUADA_BITS au premier niveau */
extern const u16 mp3_quadA[64];
/* débuts des bandes longues (23 + 1 par fréquence) et courtes (14 + 2) */
extern const u16 mp3_sfb_long[216];
extern const u16 mp3_sfb_short[144];
extern const u8 mp3_pretab[24];
extern const u8 mp3_slen[32];
/* nombre de bandes LSF (13818-3) [6][3][4] */
extern const u8 mp3_nsf[72];
/* fenêtre de synthèse D, matrice N (colonne par colonne : k = 0..31, i = 0..63) */
extern const u32 mp3_D[512];
extern const u32 mp3_N[2048];
/* IMDCT longue (par k (18) puis i (36)), courte (k (6) puis i (12)) */
extern const u32 mp3_imdct36[648];
extern const u32 mp3_imdct12[72];
/* fenêtres des types de bloc 0, 1, 2 (les 12 premières), 3 : 36 chacune */
extern const u32 mp3_win[144];
/* anticrénelage */
extern const u32 mp3_cs[8];
extern const u32 mp3_ca[8];
/* intensité MPEG-1 (is_pos 0..6) : gauche, droite ; MPEG-2 : io^n, 2 x 32 */
extern const u32 mp3_is_l[7];
extern const u32 mp3_is_r[7];
extern const u32 mp3_is_lsf[64];
/* 2^(q / 4), q = 0..3 */
extern const u32 mp3_pow2q[4];

/* ---- encodeur (ak_mp3enc.inc) ; mp3e_hlist, mp3e_hdir : enregistrements
 * (dh, db, db) rangés en octets, petit boutiste */
extern const u8 mp3e_hlist[60];
extern const u8 mp3e_hdir[128];
extern const u8 mp3e_hlen[1378];
extern const u32 mp3e_hcode[1378];
extern const u8 mp3e_quad[32];
extern const u8 mp3e_lbsel[56];

#endif
