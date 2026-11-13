/*
 * term.h -- le Terminal en C (port de terminal.asm, vt.inc, vt_draw.inc, vt_keys.inc) : ce que partagent
 * main.c (fenetre, pseudo-terminal), vt.c (grille, analyseur), vt_draw.c (dessin) et vt_keys.c (touches).
 * Un seul source pour le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#ifndef TERM_H
#define TERM_H

#include <stdint.h>
#include <uikit.h>

/* ---- la fenetre ---- */
#define WIN_W		760
#define WIN_H		480
#define PAD		6
#define LH		18			/* hauteur de ligne */
#define FONT_PX		15

/* les couleurs du terminal : la palette par defaut d'un terminal (contenu, pas le chrome : comme l'asm,
 * elles ne suivent pas le theme) */
#define COL_BG		0x001C2026u
#define COL_TXT		0x00D8DEE6u
#define COL_CURSOR	0x00F0C674u
#define MIN_W		320			/* taille minimale de la fenetre */
#define MIN_H		160
#define RD_MAX		32			/* lectures du maitre par passage */

/* ---- la grille (vt.c) ---- */
#define VT_MAXCOLS	256
#define VT_MAXROWS	128
#define VT_SBMAX	1000			/* lignes d'historique */
#define VT_NPAR		16			/* parametres CSI gardes */

/* une cellule : point de code (0 : vide, dessine comme une espace), attributs VA_*, couleurs (0 par
 * defaut, VC_IDX | n : palette de 256, VC_RGB | 0xRRGGBB). Une cellule effacee avec le stylo par
 * defaut vaut 0. */
struct cell
{
	uint32_t cp, attr, fg, bg;
};

#define VA_BOLD		1
#define VA_DIM		2
#define VA_ITALIC	4			/* garde, dessine droit (pas de mono italique) */
#define VA_UNDER	8
#define VA_BLINK	16			/* garde, sans clignotement */
#define VA_REV		32
#define VA_HIDDEN	64
#define VA_STRIKE	128
#define VC_RGB		0x01000000u
#define VC_IDX		0x02000000u

/* modes */
#define VM_CURSOR	1			/* ?25 curseur visible */
#define VM_WRAP		2			/* ?7 retour a la ligne automatique */
#define VM_PASTE	4			/* ?2004 collage encadre (note seulement) */
#define VM_LFCR		8			/* LF fait aussi CR (LNM ; ONLCR de vterm le fait d'habitude) */
#define VM_APPCK	16			/* ?1 fleches en mode application (ESC O A) */
#define VM_DEFAULT	(VM_CURSOR | VM_WRAP)

/* curseur sauve (ESC 7, CSI s, ?1048 ; ?1049) */
struct saved
{
	long x, y;
	struct cell pen;
	long wrap, ok;
};

/* l'etat du terminal (le bloc vt de l'asm) */
struct vt
{
	long rows, cols;
	long cx, cy;
	long wrap;				/* 1 : derniere colonne ecrite, retour en attente */
	long top, bot;				/* zone de defilement (incluse) */
	struct cell pen;			/* stylo : comme une cellule (point de code 0) */
	struct cell **tbl;			/* table des lignes de l'ecran montre */
	long alt;				/* 1 : ecran alternatif */
	long state;
	long uneed;				/* UTF-8 : octets attendus */
	uint32_t ucp;
	long np;				/* parametres CSI lus */
	long prefix;				/* ? > < = */
	long inter;				/* intermediaire (ESC, CSI) */
	uint32_t par[VT_NPAR];
	long modes;
	long sbn;				/* lignes d'historique */
	long sbhead;				/* prochaine place dans l'anneau */
	long scrolls;				/* lignes sorties par le haut (repere de la saisie) */
	uint32_t lastcp;			/* dernier caractere (REP) */
	struct saved sc, sc2;
};

extern struct ui_vars ui;
extern struct vt vt;
extern struct cell *rowp_main[VT_MAXROWS], *rowp_alt[VT_MAXROWS];
extern struct cell *vt_shadow;			/* ce qui est a l'ecran (vt_draw.c) */
extern struct cell *vt_sb;			/* l'historique : VT_SBMAX lignes de VT_MAXCOLS cellules */
extern long scroll;				/* vue remontee de tant de lignes (0 : en bas) */
extern long adv100;				/* avance de la police mono (x100) */
extern long font_bold;
extern long full_redraw;

/* vt.c */
int vt_init (void);
void vt_resize (long cols, long rows);
void term_putb (unsigned b);
/* vt_draw.c */
void pal_init (void);
void term_draw (void);
/* vt_keys.c */
#define K_MOD_CTRL	0x11
#define K_MOD_SHIFT	0x22
#define K_MOD_ALT	0x44
#define K_MOD_LALT	0x04			/* Alt gauche seul : ESC devant un caractere ; Alt droite est AltGr */
int key_encode (unsigned code, unsigned ch, unsigned mods, unsigned char *buf);
/* main.c */
void term_reply_s (const char *s);

#endif
