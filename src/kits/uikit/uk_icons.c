/* uk_icons.c -- UIKit : icônes des applications (docs/DESIGN-ELEGANT.md 9).
 * Port ligne à ligne de uikit_icons.inc (src/kits/uikit/PORT.md).
 *
 * D'abord une image pré-rendue (tools/icons) : SD0:/apps/<nom>.app/icon.rgba
 * (elle part avec le paquet), sinon SD0:/res/icons/<nom>.rgba (applets sans
 * paquet propre). Chargée à la première demande, gardée en mémoire pour ce
 * programme, à chaque taille demandée ; une application sans image n'est
 * recherchée qu'une fois toutes les IC_RETRY ms.
 *
 * Repli, pour les applications tierces : le dessin par le code, une tuile
 * arrondie de la couleur de l'application, puis un symbole, sur une grille de
 * 40 mise à l'échelle. Une application inconnue : son initiale, couleur tirée
 * de son nom.
 *
 * Description d'un symbole : opérations de 16 octets
 *	+0 op (IC_*), +1 x, +2 y, +3 largeur (texte : taille), +4 hauteur
 *	(texte : police 0 Sans, 1 Bold, 2 Mono), +5 rayon, +8 couleur,
 *	+12 texte : dans l'asm, décalage de la chaîne depuis l'opération ; ici
 *	(aucun pointeur dans les données) un numéro de chaîne (ic_text_str).
 * Couleurs spéciales : IC_C (la tuile), IC_CL (la tuile éclaircie). */
#include "uk.h"

/* opération d'un symbole (16 octets, comme la macro ic) */
struct ic_op {
	u8 op, x, y, w, h, r, pad0, pad1;
	u32 c;
	u32 s; /* chaîne : 0 aucune, 1 losange, 2 engrenage, 3 invite */
};
/* entrée : nom (16 o), couleur, symbole (UK_ICONE_SIZE = 24) ; dans l'asm, le
 * décalage du symbole depuis l'entrée ; ici son rang dans uk_ic_ops */
struct ic_ent {
	char name[16];
	u32 c;
	u32 sym;
};

/* symboles (ics_*), dans l'ordre de l'asm */
static const struct ic_op uk_ic_ops[209] = {
	/* ics_bienvenue */
	{IC_TEXT, 0, 0, 26, 0, 0, 0, 0, 0x00FFFFFF, 1},
	{IC_BOX, 17, 17, 6, 6, 3, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_calc */
	{IC_BOX, 9, 6, 22, 8, 2, 0, 0, 0x00DCEFE2, 0},
	{IC_BOX, 9, 17, 6, 5, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 17, 17, 6, 5, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 25, 17, 6, 5, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 9, 23, 6, 5, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 17, 23, 6, 5, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 25, 23, 6, 5, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 9, 29, 6, 5, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 17, 29, 6, 5, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 25, 29, 6, 5, 1, 0, 0, 0x00F59E0B, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_cibles */
	{IC_BOX, 6, 6, 28, 28, 14, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 10, 10, 20, 20, 10, 0, 0, IC_C, 0},
	{IC_BOX, 14, 14, 12, 12, 6, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 17, 17, 6, 6, 3, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_letters */
	{IC_BOX, 10, 7, 20, 26, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 14, 12, 12, 2, 0, 0, 0, IC_C, 0},
	{IC_FILL, 14, 17, 12, 2, 0, 0, 0, IC_C, 0},
	{IC_FILL, 14, 22, 12, 2, 0, 0, 0, IC_C, 0},
	{IC_FILL, 14, 27, 7, 2, 0, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_sheet */
	{IC_BOX, 8, 9, 24, 22, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 8, 15, 24, 1, 0, 0, 0, IC_C, 0},
	{IC_FILL, 8, 22, 24, 1, 0, 0, 0, IC_C, 0},
	{IC_FILL, 16, 9, 1, 22, 0, 0, 0, IC_C, 0},
	{IC_FILL, 24, 9, 1, 22, 0, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_slides */
	{IC_BOX, 7, 9, 26, 18, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 11, 13, 12, 3, 0, 0, 0, IC_C, 0},
	{IC_FILL, 11, 19, 18, 2, 0, 0, 0, IC_CL, 0},
	{IC_FILL, 19, 27, 2, 5, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 13, 32, 14, 2, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_cardfile */
	{IC_BOX, 11, 8, 18, 8, 2, 0, 0, IC_CL, 0},
	{IC_BOX, 7, 13, 26, 19, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 11, 18, 11, 2, 0, 0, 0, IC_C, 0},
	{IC_FILL, 11, 23, 18, 2, 0, 0, 0, IC_CL, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_calendar */
	{IC_BOX, 8, 9, 24, 23, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 8, 15, 24, 2, 0, 0, 0, IC_C, 0},
	{IC_FILL, 13, 6, 2, 6, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 25, 6, 2, 6, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 13, 20, 4, 4, 1, 0, 0, IC_C, 0},
	{IC_BOX, 19, 20, 4, 4, 1, 0, 0, IC_C, 0},
	{IC_BOX, 25, 20, 4, 4, 1, 0, 0, IC_CL, 0},
	{IC_BOX, 13, 26, 4, 3, 1, 0, 0, IC_CL, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_notes */
	{IC_BOX, 9, 8, 22, 25, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 13, 14, 14, 2, 0, 0, 0, IC_C, 0},
	{IC_FILL, 13, 19, 14, 2, 0, 0, 0, IC_C, 0},
	{IC_FILL, 13, 24, 9, 2, 0, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_circuits */
	{IC_FILL, 6, 14, 10, 2, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 6, 24, 10, 2, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 13, 10, 16, 20, 8, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 13, 10, 8, 20, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 29, 19, 6, 2, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_dessin */
	{IC_BOX, 6, 10, 28, 20, 10, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 11, 14, 5, 5, 2, 0, 0, 0x00E5484D, 0},
	{IC_BOX, 18, 12, 5, 5, 2, 0, 0, 0x002F74E0, 0},
	{IC_BOX, 25, 14, 5, 5, 2, 0, 0, 0x0030A46C, 0},
	{IC_BOX, 11, 21, 5, 5, 2, 0, 0, 0x006E56CF, 0},
	{IC_BOX, 22, 21, 6, 6, 3, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_editeur */
	{IC_BOX, 11, 7, 18, 26, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 14, 12, 12, 2, 0, 0, 0, IC_CL, 0},
	{IC_FILL, 14, 16, 12, 2, 0, 0, 0, IC_CL, 0},
	{IC_FILL, 14, 20, 12, 2, 0, 0, 0, IC_CL, 0},
	{IC_FILL, 14, 24, 8, 2, 0, 0, 0, IC_CL, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_fichiers */
	{IC_BOX, 8, 11, 11, 6, 2, 0, 0, 0x00D7F0F4, 0},
	{IC_BOX, 8, 14, 24, 17, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_horloge */
	{IC_BOX, 8, 8, 24, 24, 12, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 19, 12, 2, 9, 0, 0, 0, 0x001B1F24, 0},
	{IC_FILL, 19, 19, 7, 2, 0, 0, 0, 0x001B1F24, 0},
	{IC_BOX, 18, 18, 4, 4, 2, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_magneto */
	{IC_BOX, 7, 17, 2, 6, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 11, 14, 2, 12, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 15, 10, 2, 20, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 19, 13, 2, 14, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 23, 11, 2, 18, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 27, 16, 2, 8, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 31, 18, 2, 4, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_archiver */
	{IC_BOX, 8, 12, 24, 20, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 6, 9, 28, 7, 2, 0, 0, 0x00F1E2BE, 0},
	{IC_FILL, 16, 19, 8, 3, 0, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_iconedit */
	{IC_FILL, 8, 8, 7, 7, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 16, 8, 7, 7, 0, 0, 0, 0x00FFD43B, 0},
	{IC_FILL, 24, 8, 7, 7, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 8, 16, 7, 7, 0, 0, 0, 0x0074C0FC, 0},
	{IC_FILL, 16, 16, 7, 7, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 24, 16, 7, 7, 0, 0, 0, 0x0074C0FC, 0},
	{IC_FILL, 8, 24, 7, 7, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 16, 24, 7, 7, 0, 0, 0, 0x00FFD43B, 0},
	{IC_FILL, 24, 24, 7, 7, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_fractale */
	{IC_BOX, 15, 9, 20, 22, 10, 0, 0, 0x0000000F, 0},
	{IC_BOX, 7, 15, 10, 10, 5, 0, 0, 0x0000000F, 0},
	{IC_BOX, 4, 19, 4, 2, 1, 0, 0, 0x00FFB000, 0},
	{IC_BOX, 13, 8, 5, 5, 2, 0, 0, 0x00FFB000, 0},
	{IC_BOX, 13, 27, 5, 5, 2, 0, 0, 0x0074C0FC, 0},
	{IC_BOX, 28, 19, 5, 3, 1, 0, 0, 0x001E2A5A, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_disques */
	{IC_BOX, 7, 10, 26, 20, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 10, 22, 20, 1, 0, 0, 0, IC_C, 0},
	{IC_BOX, 25, 25, 4, 3, 1, 0, 0, 0x0040C057, 0},
	{IC_BOX, 12, 13, 16, 6, 2, 0, 0, 0x00C8D0DA, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_manette */
	{IC_BOX, 5, 13, 30, 15, 7, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 12, 16, 3, 9, 0, 0, 0, IC_C, 0},
	{IC_FILL, 9, 19, 9, 3, 0, 0, 0, IC_C, 0},
	{IC_BOX, 25, 16, 4, 4, 2, 0, 0, 0x00E5484D, 0},
	{IC_BOX, 28, 21, 4, 4, 2, 0, 0, 0x002E9E6B, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_moniteur */
	{IC_BOX, 6, 9, 28, 20, 3, 0, 0, 0x000F2E1F, 0},
	{IC_FILL, 9, 19, 6, 2, 0, 0, 0, 0x009BF0BF, 0},
	{IC_FILL, 15, 12, 2, 9, 0, 0, 0, 0x009BF0BF, 0},
	{IC_FILL, 17, 12, 2, 2, 0, 0, 0, 0x009BF0BF, 0},
	{IC_FILL, 19, 12, 2, 13, 0, 0, 0, 0x009BF0BF, 0},
	{IC_FILL, 21, 23, 2, 2, 0, 0, 0, 0x009BF0BF, 0},
	{IC_FILL, 23, 17, 2, 8, 0, 0, 0, 0x009BF0BF, 0},
	{IC_FILL, 25, 17, 2, 2, 0, 0, 0, 0x009BF0BF, 0},
	{IC_FILL, 27, 17, 2, 4, 0, 0, 0, 0x009BF0BF, 0},
	{IC_FILL, 27, 19, 5, 2, 0, 0, 0, 0x009BF0BF, 0},
	{IC_FILL, 17, 29, 6, 3, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 13, 31, 14, 2, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_navigateur */
	{IC_BOX, 7, 7, 26, 26, 13, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 14, 7, 12, 26, 6, 0, 0, IC_C, 0},
	{IC_BOX, 16, 9, 8, 22, 4, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 7, 19, 26, 2, 0, 0, 0, IC_C, 0},
	{IC_FILL, 19, 7, 2, 26, 0, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_mixer */
	{IC_BOX, 10, 8, 2, 24, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 19, 8, 2, 24, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 28, 8, 2, 24, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 7, 20, 8, 5, 2, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 16, 12, 8, 5, 2, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 25, 24, 8, 5, 2, 0, 0, 0x00FFFFFF, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_son */
	{IC_BOX, 8, 16, 6, 8, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 14, 13, 2, 14, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 16, 11, 2, 18, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 18, 9, 2, 22, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 23, 15, 2, 10, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 28, 11, 2, 18, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_piano */
	{IC_BOX, 7, 10, 6, 20, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 14, 10, 6, 20, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 21, 10, 6, 20, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 28, 10, 6, 20, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 11, 10, 4, 12, 0, 0, 0, 0x00111317, 0},
	{IC_FILL, 18, 10, 4, 12, 0, 0, 0, 0x00111317, 0},
	{IC_FILL, 25, 10, 4, 12, 0, 0, 0, 0x00111317, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_fmtracker */
	{IC_BOX, 7, 7, 26, 26, 3, 0, 0, 0x001B1F24, 0},
	{IC_FILL, 10, 11, 8, 2, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 22, 11, 8, 2, 0, 0, 0, IC_CL, 0},
	{IC_FILL, 10, 15, 8, 2, 0, 0, 0, IC_CL, 0},
	{IC_FILL, 8, 19, 24, 4, 0, 0, 0, 0x0062B87A, 0},
	{IC_FILL, 22, 23, 8, 2, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 10, 27, 8, 2, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 22, 27, 8, 2, 0, 0, 0, IC_CL, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_terminal */
	{IC_TEXTAT, 9, 26, 16, 2, 0, 0, 0, 0x007CE38B, 3},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_corbeille */
	{IC_BOX, 15, 5, 10, 5, 2, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 6, 9, 28, 4, 2, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 9, 15, 22, 21, 4, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 14, 19, 2, 13, 0, 0, 0, IC_C, 0},
	{IC_FILL, 19, 19, 2, 13, 0, 0, 0, IC_C, 0},
	{IC_FILL, 24, 19, 2, 13, 0, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_control */
	{IC_TEXT, 0, 0, 28, 0, 0, 0, 0, 0x00FFFFFF, 2},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_theme */
	{IC_BOX, 7, 9, 15, 15, 7, 0, 0, 0x00E5484D, 0},
	{IC_BOX, 18, 9, 15, 15, 7, 0, 0, 0x002F74E0, 0},
	{IC_BOX, 12, 18, 15, 15, 7, 0, 0, 0x00F5B33B, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_clavier */
	{IC_BOX, 5, 11, 30, 19, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_FILL, 8, 14, 4, 3, 0, 0, 0, IC_C, 0},
	{IC_FILL, 14, 14, 4, 3, 0, 0, 0, IC_C, 0},
	{IC_FILL, 20, 14, 4, 3, 0, 0, 0, IC_C, 0},
	{IC_FILL, 26, 14, 4, 3, 0, 0, 0, IC_C, 0},
	{IC_FILL, 11, 19, 4, 3, 0, 0, 0, IC_C, 0},
	{IC_FILL, 17, 19, 4, 3, 0, 0, 0, IC_C, 0},
	{IC_FILL, 23, 19, 4, 3, 0, 0, 0, IC_C, 0},
	{IC_FILL, 12, 24, 16, 3, 0, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_ecran */
	{IC_BOX, 6, 8, 28, 20, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 8, 10, 24, 16, 1, 0, 0, IC_CL, 0},
	{IC_FILL, 18, 28, 4, 3, 0, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 13, 31, 14, 2, 1, 0, 0, 0x00FFFFFF, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/* ics_dockconf */
	{IC_BOX, 6, 10, 28, 12, 3, 0, 0, IC_CL, 0},
	{IC_BOX, 4, 24, 32, 10, 3, 0, 0, 0x00FFFFFF, 0},
	{IC_BOX, 9, 26, 6, 6, 2, 0, 0, IC_C, 0},
	{IC_BOX, 17, 26, 6, 6, 2, 0, 0, IC_C, 0},
	{IC_BOX, 25, 26, 6, 6, 2, 0, 0, IC_C, 0},
	{IC_END, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};
static const struct ic_ent uk_icones[35] = {
	{"welcome", 0x004F5BD5, 0}, /* ics_bienvenue */
	{"calculator", 0x005B6472, 3}, /* ics_calc */
	{"targets", 0x00E5484D, 14}, /* ics_cibles */
	{"paint", 0x00F59E0B, 62}, /* ics_dessin */
	{"editor", 0x006E56CF, 69}, /* ics_editeur */
	{"files", 0x001BA1B5, 75}, /* ics_fichiers */
	{"clock", 0x00E8833A, 78}, /* ics_horloge */
	{"recorder", 0x00D6336C, 83}, /* ics_magneto */
	{"taskman", 0x0030A46C, 123}, /* ics_moniteur */
	{"jet", 0x001E88E5, 136}, /* ics_navigateur */
	{"piano", 0x002B2F36, 156}, /* ics_piano */
	{"fmtracker", 0x002F7D8C, 164}, /* ics_fmtracker */
	{"terminal", 0x001B1F24, 173}, /* ics_terminal */
	{"corbeille", 0x005B6472, 175}, /* ics_corbeille */
	{"control", 0x005B6472, 182}, /* ics_control */
	{"theme", 0x008E6BD8, 184}, /* ics_theme */
	{"clavier", 0x003A4458, 188}, /* ics_clavier */
	{"ecran", 0x001E88E5, 198}, /* ics_ecran */
	{"dockconf", 0x0030A46C, 203}, /* ics_dockconf */
	{"panelconf", 0x0030A46C, 203}, /* ics_dockconf */
	{"mixer", 0x00495766, 142}, /* ics_mixer */
	{"sound", 0x00E5484D, 149}, /* ics_son */
	{"letters", 0x002F74E0, 19}, /* ics_letters */
	{"sheet", 0x002E9E6B, 25}, /* ics_sheet */
	{"slides", 0x00E8590C, 31}, /* ics_slides */
	{"cardfile", 0x00B7791F, 37}, /* ics_cardfile */
	{"circuits", 0x0030A46C, 56}, /* ics_circuits */
	{"calendar", 0x00D23A30, 42}, /* ics_calendar */
	{"notes", 0x00E2B02C, 51}, /* ics_notes */
	{"archiver", 0x00B7791F, 91}, /* ics_archiver */
	{"iconedit", 0x00D6336C, 95}, /* ics_iconedit */
	{"mandelbrot", 0x001E2A5A, 105}, /* ics_fractale */
	{"disks", 0x005B6472, 112}, /* ics_disques */
	{"padconf", 0x004F5BD5, 117}, /* ics_manette */
	{"", 0, 0},
};

/* couleurs d'une application inconnue */
static const u32 uk_ic_colours[8] = {0x004F5BD5, 0x00E5484D, 0x00F59E0B, 0x006E56CF,
				     0x001BA1B5, 0x00E8833A, 0x0030A46C, 0x001E88E5};

static const char uk_s_gear[] = "\xE2\x9A\x99";    /* uk_s_engrenage : U+2699 */
static const char uk_s_diamond[] = "\xE2\x97\x86"; /* uk_s_losange : U+25C6 */
static const char uk_s_prompt[] = ">_";            /* uk_s_invite */

static const char uk_ic_s_apps[] = "SD0:/apps/";
static const char uk_ic_s_bundle[] = ".app/icon.rgba";
static const char uk_ic_s_res[] = "SD0:/res/icons/";
static const char uk_ic_s_ext[] = ".rgba";

/* chaîne d'une opération IC_TEXT / IC_TEXTAT (champ +12) */
static long ic_text_str(long op)
{
	switch (LDW(op, 12)) {
	case 1:
		return S(uk_s_diamond);
	case 2:
		return S(uk_s_gear);
	default:
		return S(uk_s_prompt);
	}
}

/* ---------------------------------------------------------------------------
 *  Icônes en images
 *
 *  Fichier (tools/icons/build.py) : +0 'ARIC', +4 version 16 bits, +6 n 16 bits,
 *  +8 n entrées { côté 16 bits, 0 16 bits, décalage 32 bits } ; pixels : mots
 *  0xAARRGGBB, alpha prémultiplié, de haut en bas. Seule la taille utile est
 *  lue. Une taille absente du fichier : la plus proche au-dessus, réduite une
 *  fois (bilinéaire).
 *
 *  Cache : une liste par programme (uk_ic_list). Entrée ICE_SIZE 0 : pas
 *  d'image pour ce nom, quelle que soit la taille ; -1 : entrée morte.
 * ------------------------------------------------------------------------- */

/* uk_ic_cat : destination, chaîne -> son NUL dans la destination */
static long ic_cat(long d, long s)
{
	for (;;) {
		long c = LDB(s, 0);
		s++;
		STB(d, 0, c);
		if (!c)
			return d;
		d++;
	}
}

/* uk_ic_open : nom -> fichier de son icône ouvert (< 0 : aucun) */
static long ic_open(long name)
{
	long d, fd;
	d = ic_cat(UKA(ic_path), S(uk_ic_s_apps));
	d = ic_cat(d, name);
	ic_cat(d, S(uk_ic_s_bundle));
	fd = SYS2(SYS_OPEN, UKA(ic_path), O_READ);
	if (fd >= 0)
		return fd;
	d = ic_cat(UKA(ic_path), S(uk_ic_s_res));
	d = ic_cat(d, name);
	ic_cat(d, S(uk_ic_s_ext));
	return SYS2(SYS_OPEN, UKA(ic_path), O_READ);
}

/* uk_ic_lerp : pixels a, b, t = 0..256 (part de b) -> pixel (32 bits) */
static u32 ic_lerp(u32 a, u32 b, u32 t)
{
	u32 m = 0x00FF00FF, s = 256 - t, lo, hi;
	lo = (a & m) * s;
	lo += (b & m) * t;
	lo = (lo >> 8) & m;
	hi = ((a >> 8) & m) * s;
	hi += ((b >> 8) & m) * t;
	return (hi & ~m) | lo;
}

/* uk_ic_pos : position p sur le côté n, côté z de la source -> *i0 = pixel de
 * la source, *i1 = pixel suivant, rend la part du suivant (0..255) : centre du
 * pixel p */
static long ic_pos(long p, long z, long n, long *i0, long *i1)
{
	long v, part, last, a, b;
	v = (long)((((u64)p << 1) + 1) * (u64)z << 7);
	v = UDIV(v, n);
	v = ULT(v, 128) ? 0 : v - 128; /* subs + csel lo */
	part = v & 255;
	a = LSR(v, 8);
	last = z - 1;
	if (UGT(a, last))
		a = last;
	b = a + 1;
	if (UGT(b, last))
		b = last;
	*i0 = a;
	*i1 = b;
	return part;
}

/* uk_ic_scale : source, son côté, destination, son côté : mise à l'échelle
 * bilinéaire (pixels prémultipliés) */
static void ic_scale(long src, long sz, long dst, long dz)
{
	long y, x, r0, r1, c0, c1, py, px, l0, l1;
	u32 top, bot;
	for (y = 0; ULT(y, dz); y++) {
		py = ic_pos(y, sz, dz, &r0, &r1); /* part de la ligne suivante */
		l0 = src + ((r0 * sz) << 2);      /* ligne, ligne suivante */
		l1 = src + ((r1 * sz) << 2);
		for (x = 0; ULT(x, dz); x++) {
			px = ic_pos(x, sz, dz, &c0, &c1);
			top = ic_lerp((u32)LDW(l0, c0 << 2), (u32)LDW(l0, c1 << 2), (u32)px);
			bot = ic_lerp((u32)LDW(l1, c0 << 2), (u32)LDW(l1, c1 << 2), (u32)px);
			STW(dst, 0, ic_lerp(top, bot, (u32)py));
			dst += 4;
		}
	}
}

/* uk_ic_get : nom, taille -> entrée du cache avec ses pixels (0 : pas
 * d'image) */
static long ic_get(long name, long size)
{
	long e, retry = 0, fd = 0, got, n, p, end, side, off, ent, pix, len, t;
	long s2, o2;
	if (UGE(size - 1, IC_SIZE_MAX))
		return 0;
	if (UGE(uk_strlen(name) - 1, IC_NAME_MAX))
		return 0;
	/* retry (x22) : entrée « pas d'image » de ce nom, revue maintenant */
	e = UKA(ic_list);
	for (;;) {
		e = LDX(e, ICE_NEXT);
		if (!e)
			goto load;
		if (!LDX(e, ICE_SIZE)) {
			if (!uk_str_eq(name, e + ICE_NAME))
				continue;
			t = SYS0(SYS_TIME_MS);
			if (ULT(t - LDX(e, ICE_TIME), IC_RETRY))
				return 0;
			STX(e, ICE_TIME, t); /* (on regarde à nouveau) */
			retry = e;
			goto load;
		}
		if (LDX(e, ICE_SIZE) != size)
			continue;
		if (uk_str_eq(name, e + ICE_NAME))
			return e;
	}
load:
	uk_kits();
	fd = ic_open(name);
	if (fd < 0)
		goto absent;
	got = SYS3(SYS_READ, fd, UKA(ic_hdr), ICF_HDR);
	if (got < 8)
		goto close;
	if ((u32)LDW(uk.ic_hdr, 0) != (u32)ICF_MAGIC)
		goto close;
	n = LDH(uk.ic_hdr, 6);
	if (!n || UGT(n, ICF_MAX))
		goto close;
	p = UKA(ic_hdr) + 8;
	end = p + (n << 3); /* fin des entrées */
	if (UGT(end - UKA(ic_hdr), got))
		goto close;
	side = 0; /* côté, décalage */
	off = 0;
	/* la plus petite >= taille, sinon la plus grande */
	while (ULT(p, end)) {
		s2 = LDH(p, 0);
		o2 = LDW(p, 4);
		p += 8;
		if (side) {
			if (UGE(side, size)) {
				if (ULT(s2, size) || UGE(s2, side))
					continue;
			} else if (ULE(s2, side)) {
				continue; /* (la retenue est trop petite) */
			}
		}
		side = s2;
		off = o2;
	}
	if (!side)
		goto close;
	ent = uk_alloc(((size * size) << 2) + ICE_PIX); /* l'entrée et ses pixels */
	if (!ent)
		goto close;
	pix = ent + ICE_PIX;
	if (side != size) { /* à la bonne taille : lue en place */
		pix = uk_alloc((side * side) << 2);
		if (!pix)
			goto drop;
	}
	if (SYS3(SYS_SEEK, fd, off, 0) < 0)
		goto bad;
	len = (side * side) << 2;
	if (SYS3(SYS_READ, fd, pix, len) != len)
		goto bad;
	SYS1(SYS_CLOSE, fd);
	if (side != size) {
		ic_scale(pix, side, ent + ICE_PIX, size);
		uk_free(pix);
	}
	STX(ent, ICE_SIZE, size);
	ic_cat(ent + ICE_NAME, name);
	STX(ent, ICE_NEXT, uk.ic_list);
	uk.ic_list = ent;
	if (retry)
		STX(retry, ICE_SIZE, -1); /* elle a fini par arriver */
	return ent;
bad:
	if (ent + ICE_PIX != pix)
		uk_free(pix);
drop:
	uk_free(ent);
close:
	SYS1(SYS_CLOSE, fd);
absent:
	if (retry)
		return 0; /* (déjà notée) */
	e = uk_alloc(ICE_PIX);
	if (!e)
		return 0;
	ic_cat(e + ICE_NAME, name);
	STX(e, ICE_TIME, SYS0(SYS_TIME_MS));
	STX(e, ICE_NEXT, uk.ic_list);
	uk.ic_list = e;
	return 0;
}

/* uk_ic_blit : pixels prémultipliés, x, y, côté, x4 = 0, ou bit 32 et couleur :
 * l'image ne donne que l'alpha (glyphe). Composé dans la surface, découpé à
 * ui_clip. Fond opaque : s + d * (255 - a) / 255. Fenêtre WF_ALPHA dont le fond
 * est transparent (octet haut : 0 opaque) : les deux couvertures s'ajoutent,
 * le résultat reste en alpha droit pour Elegant. */
static void ic_blit(long src, long x, long y, long side, long glyph)
{
	long v = uk.vars, right, bottom, left, top, W, surf, row, dl, cx;
	u32 m = 0x00FF00FF, half = 0x00800080, amask, gc = (u32)glyph;
	u32 s, a, d, b, inv, lo, mid, rest, cov, o, ch, k;
	right = LDX(v, UV_W); /* bornes : surface, découpe */
	bottom = LDX(v, UV_H);
	left = 0;
	top = 0;
	if (uk.clip_on) {
		left = uk.clip[0];
		top = uk.clip[1];
		if (uk.clip[2] < right)
			right = uk.clip[2];
		if (uk.clip[3] < bottom)
			bottom = uk.clip[3];
	}
	if (top < y)
		top = y;
	if (bottom > y + side)
		bottom = y + side;
	if (left < x)
		left = x;
	if (right > x + side)
		right = x + side;
	W = LDX(v, UV_W);
	surf = LDX(v, UV_SURF);
	amask = (uk.flags & WF_ALPHA) ? 255 : 0; /* masque de la transparence du fond */
	for (; top < bottom; top++) {
		row = src + (((top - y) * side) << 2); /* ligne source */
		dl = surf + ((top * W) << 2);          /* ligne destination */
		for (cx = left; cx < right; cx++) {
			s = (u32)LDW(row, (cx - x) << 2);
			a = s >> 24; /* alpha */
			if (!a)
				continue;
			if (glyph) { /* glyphe : la couleur, prémultipliée */
				lo = (gc & m) * a + half;
				lo = ((lo + ((lo >> 8) & m)) >> 8) & m;
				mid = ((gc >> 8) & 0xFF) * a + 128;
				mid = (mid + (mid >> 8)) >> 8;
				s = lo | (mid << 8) | (a << 24);
			}
			if (a == 255) {
				STW(dl, cx << 2, s & 0x00FFFFFF);
				continue;
			}
			d = (u32)LDW(dl, cx << 2);
			b = (d >> 24) & amask;
			if (!b) { /* fond opaque, canaux deux par deux */
				inv = 255 - a;
				lo = (d & m) * inv + half;
				lo = ((lo + ((lo >> 8) & m)) >> 8) & m;
				mid = ((d >> 8) & 0xFF) * inv + 128;
				mid = (mid + (mid >> 8)) >> 8;
				STW(dl, cx << 2, (s & 0x00FFFFFF) + lo + (mid << 8));
				continue;
			}
			/* fond transparent */
			rest = (u32)UDIVW((255 - b) * (255 - a) + 127, 255); /* ce qui reste du fond */
			cov = a + rest;                                       /* couverture du résultat */
			o = (255 - cov) << 24;
			for (ch = 0; ch < 24; ch += 8) {
				k = ((s >> ch) & 0xFF) * 255;
				k = ((d >> ch) & 0xFF) * rest + k;
				k = (u32)UDIVW(k, cov);
				if (k > 255)
					k = 255;
				o |= k << ch;
			}
			STW(dl, cx << 2, o);
		}
	}
}

/* ui_glyph : nom du glyphe (« glyph-power »...), x, y, côté, couleur : un
 * bouton d'interface, tools/icons le rend en masque, le jeton du thème le
 * colorie ici. Fichier absent : un point de cette couleur, le bouton se voit.
 * (Valeur rendue : celle de ui_rbox pour le point ; après l'image, l'asm
 * laissait un registre de travail : 0 ici.) */
long ui_glyph(long name, long x, long y, long side, long col)
{
	long c = (long)((u32)col & 0x00FFFFFF), e, q;
	e = ic_get(name, side);
	if (e) {
		ic_blit(e + ICE_PIX, x, y, side, c | (1L << 32));
		return 0;
	}
	q = LSR(side, 2);
	return ui_rbox(x + q, y + q, LSR(side, 1), LSR(side, 1), q, c, c);
}

/* ---- repli : le dessin par le code ---------------------------------------- */

/* uk_ic_s : valeur sur la grille de 40 -> pixels (taille de l'icône : uk_ic_t) */
static long ic_s(long v)
{
	return UDIV(v * uk.ic_t[0] + 20, 40);
}

/* uk_ic_font : 0 Sans, 1 Bold, 2 Mono -> police du programme */
static long ic_font(long n)
{
	return n * UI_FONT_SIZE + uk.vars + UV_FONT;
}

/* (ui_icone) texte centré dans la tuile, taille px : ligne de base au milieu
 * + 0,36 taille */
static long ic_text_centred(long s, long px, long font, u32 col)
{
	long tw = ui_text_width(s, px, font), size = uk.ic_t[0], tx, ty;
	tx = uk.ic_t[1] + ASR(size - tw, 1);
	ty = uk.ic_t[2] + LSR(size, 1) + UDIV(36 * px, 100);
	return ui_text(s, tx, ty, col, px, font, 0);
}

/* ui_icone : nom de l'application, x, y, taille (px) : dessine son icône dans
 * la surface courante. (Valeur rendue : sans usage ; l'asm laissait celle du
 * dernier dessin, ou un registre de travail après une image : 0 ici.) */
long ui_icone(long name, long x, long y, long size)
{
	long e, op = 0, sum, sx, sy, w, h, px, font, c;
	u32 tile, col, hi;
	u8 kind;
	uk.ic_t[0] = size; /* taille, x, y */
	uk.ic_t[1] = x;
	uk.ic_t[2] = y;
	e = ic_get(name, size); /* son image, si elle en a une */
	if (e) {
		ic_blit(e + ICE_PIX, uk.ic_t[1], uk.ic_t[2], uk.ic_t[0], 0);
		return 0;
	}
	/* repli : l'icône de ce nom, par le code */
	for (e = S(uk_icones); LDB(e, 0); e += UK_ICONE_SIZE)
		if (uk_str_eq(name, e))
			break;
	if (!LDB(e, 0)) { /* inconnue : initiale, couleur tirée du nom */
		sum = 0;
		for (h = name; LDB(h, 0); h++)
			sum += LDB(h, 0);
		tile = uk_ic_colours[sum & 7];
	} else {
		tile = (u32)LDW(e, 16); /* couleur */
		op = S(uk_ic_ops) + LDW(e, 20) * (long)sizeof(struct ic_op); /* symbole */
	}
	/* tuile : dégradé (haut ton 140, bas ton 116), reflet en haut */
	c = ui_tone(tile, 116);
	ui_rbox(uk.ic_t[1], uk.ic_t[2], uk.ic_t[0], uk.ic_t[0], LSR(uk.ic_t[0], 2), ui_tone(tile, 140), c);
	c = ui_mix(ui_tone(tile, 140), 0x00FFFFFF, 90);
	w = LSR(uk.ic_t[0], 2); /* (rayon) */
	ui_fill(uk.ic_t[1] + w, uk.ic_t[2] + 1, uk.ic_t[0] - (w << 1), 1, c);
	if (op) {
		for (; (kind = (u8)LDB(op, 0)) != 0; op += 16) { /* --- symbole */
			col = (u32)LDW(op, 8); /* couleur */
			hi = col >> 24;        /* couleurs spéciales : octet haut 1, 2 */
			if (hi == 1)
				col = tile;
			if (hi == 2)
				col = (u32)ui_mix(tile, 0x00FFFFFF, 150);
			if (kind < IC_TEXT) {
				/* boîte ou rectangle, mis à l'échelle (côtés au moins 1) */
				sx = ic_s(LDB(op, 1)) + uk.ic_t[1];
				sy = ic_s(LDB(op, 2)) + uk.ic_t[2];
				w = ic_s(LDB(op, 3));
				if (ULT(w, 1))
					w = 1;
				h = ic_s(LDB(op, 4));
				if (ULT(h, 1))
					h = 1;
				if (kind == IC_BOX)
					ui_rbox(sx, sy, w, h, ic_s(LDB(op, 5)), col, col);
				else
					ui_fill(sx, sy, w, h, col);
				continue;
			}
			/* texte */
			px = ic_s(LDB(op, 3));      /* taille */
			font = ic_font(LDB(op, 4)); /* police */
			if (kind != IC_TEXTAT) {    /* centré dans la tuile */
				ic_text_centred(ic_text_str(op), px, font, col);
				continue;
			}
			sx = ic_s(LDB(op, 1)) + uk.ic_t[1];
			sy = ic_s(LDB(op, 2)) + uk.ic_t[2];
			ui_text(ic_text_str(op), sx, sy, col, px, font, 0);
		}
		return 0;
	}
	/* --- application inconnue : initiale (minuscule ASCII -> majuscule) */
	kind = (u8)LDB(name, 0);
	if ((u32)(kind - 'a') <= 25)
		kind -= 32;
	STB(UKA(ic_letter), 0, kind);
	STB(UKA(ic_letter), 1, 0);
	ic_text_centred(UKA(ic_letter), ic_s(20), ic_font(1), 0x00FFFFFF);
	return 0;
}
