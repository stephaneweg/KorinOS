/* printerkit.h -- PrinterKit depuis le C (docs/IMPRESSION.md ; printerkit d'Onyx) : l'interface generee (printerkit_abi.h)
 * et ses structures. Une application imprime en quatre temps : le dialogue, un travail, ses pages, la fin.
 *
 *   PrintSetup s; print_setup_default ((long) &s);
 *   PrintDialogInfo di = { sizeof di, "Mon document", pages, 0, 0, 0, 0 };
 *   if (print_dialog ((long) &s, (long) &di))
 *   {
 *       long j = print_begin ((long) &s, (long) "Mon document");
 *       long f = print_font (j, (long) "DejaVu Sans", 0);
 *       if (print_page (j, 0, 0))
 *           print_text (j, f, 12000, 72000, 84000, (long) "Bonjour", 0);
 *       print_end (j);
 *   }
 *
 * Longueurs : milliemes de point (1/72000 de pouce ; print_mpt), y descend depuis le haut de la page.
 * Couleurs : 0xRRGGBB. Le programme ouvre le kit (printerkit_kit_open) et se lie avec -lprinterkit.
 */
#ifndef PRINTERKIT_H
#define PRINTERKIT_H

#include <printerkit_abi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PrintSetup		/* ce que l'utilisateur a choisi */
{
	unsigned size;			/* sizeof (PrintSetup) */
	char printer[64];		/* le nom de l'imprimante ("PDF" : l'imprimante PDF) */
	char media[64];			/* le papier, nom IPP ("iso_a4_210x297mm") */
	int paper_w, paper_h;		/* la page, orientation appliquee */
	int margin_l, margin_t, margin_r, margin_b;	/* ce que l'imprimante ne peut imprimer */
	int orientation;		/* PRINT_PORTRAIT, PRINT_LANDSCAPE */
	int copies, mono, quality;
	int from, to;			/* pages a imprimer, depuis 1 ; 0, 0 : toutes */
	char output[256];		/* l'imprimante PDF : le fichier a ecrire */
	int reserved[16];
} PrintSetup;

typedef struct PrintDialogInfo
{
	unsigned size;			/* sizeof (PrintDialogInfo) */
	const char *title;		/* le nom du document */
	int pages;			/* combien de pages (0 : inconnu) */
	int current;			/* la page montree, depuis 1 (0 : pas de choix « page courante ») */
	int flags;			/* PRINT_DLG_* */
	int paper_w, paper_h;		/* (reserve) */
} PrintDialogInfo;

typedef struct PrintPrinter
{
	char name[64], kind[8], uri[160], model[64];	/* kind : pdf, ipp */
	int color, copies, dpi, is_default;
	unsigned quality;		/* bits 3, 4, 5 */
	char media_default[64];
} PrintPrinter;

typedef struct PrintFound { char address[32], name[64], model[64]; int usable; } PrintFound;
typedef struct PrintJobInfo { unsigned id; int state, pages, page; char printer[64], title[80], message[96]; } PrintJobInfo;

/* points -> milliemes de point */
static inline long print_mpt (float pt) { return (long) (pt * 1000.0f + (pt < 0 ? -0.5f : 0.5f)); }

#ifdef __cplusplus
}
#endif
#endif
