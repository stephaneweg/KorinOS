/*
 * fractal.h -- ce que partagent l'interface de Fractales (main.cpp) et ses calculs (kernels.cpp) : les types, les
 * facons de colorer, la vue et ce que les calculs lisent. Les noms suivent mandelbrot.asm (g_*).
 */
#ifndef FRACTAL_H
#define FRACTAL_H

#include <stdint.h>

/* les types (ordre de la liste deroulante) */
enum { T_MANDEL, T_JULIA, T_SHIP, T_TRICORN, T_M3, T_M4, T_M5, T_NEWTON, T_PHOENIX, T_LYAP, T_CLIFFORD, T_DEJONG,
	T_BUDDHA, T_COUNT };

/* les facons de colorer */
enum { C_SMOOTH, C_NEWTON, C_LYAP, C_HIST };

#define LUT_N		4096
#define NTH		16			/* threads au plus (l'asm : 4, les coeurs du Pi) */
#define ROW_MAX		8192			/* points d'une ligne */
#define ROW_PAD		8			/* (les calculs vont par 4 points : de quoi deborder) */
#define ORB_MAX		20000			/* points d'une orbite (Buddhabrot) */
#define ATTR_N		8192			/* pas d'un lot d'attracteur (deux orbites) */
#define BUD_N		256			/* points c d'un lot de Buddhabrot */

/* l'etat d'un thread pour les comptes : deux orbites d'attracteur, le hasard, l'orbite du Buddhabrot */
struct orbit_state
{
	double x[2], y[2];
	uint64_t rng;
	double *orb;				/* ORB_MAX points (re, im) */
};

/* ce que les calculs lisent (ecrit par l'interface, threads arretes) */
extern long g_vw, g_vh, g_type, g_mode, g_maxit, g_seqn;
extern unsigned long g_offset;
extern double g_x0, g_y0, g_dx, g_cr, g_ci, g_ild, g_pa[4];
extern float g_scale;
extern uint32_t g_lut[LUT_N];
extern unsigned char g_seq[64];
extern void *g_mu;				/* vw * vh valeurs : flottants (temps de fuite...) ou comptes */
extern struct orbit_state g_orb[NTH];

/* une ligne de points : out = valeurs (flottants), n points, x du premier, y, pas */
void row_kernel (long type, float *out, long n, long maxit, double x0, double y, double d);
/* un lot de comptes (attracteurs, Buddhabrot) par le thread me */
void hist_kernel (long type, long me);
/* (Clifford, De Jong) min, max de x puis de y d'une orbite */
void attr_bounds (long type, double *b);
/* log2 approche (a 2e-5 pres), comme log2d de l'asm */
double log2d (double x);
/* les couleurs : valeurs -> pixels ; comptes -> image entiere */
void color_line (const float *mu, long n, uint32_t *px);
void color_hist (uint32_t *px, long pitch);
void set_lut (const uint32_t *stops);

#endif
