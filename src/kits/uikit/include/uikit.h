/*
 * uikit.h -- UIKit d'ARMPI depuis le C et le C++ (docs/KITS.md, « Pour un en-tete C uikit.h » ;
 * docs/WEBKIT.md). UIKit est un kit en assembleur (SD0:/lib/uikit.so) ; ses fonctions suivent
 * l'AAPCS64 et ne prennent que des entiers, des pointeurs et des poignees.
 *
 * Les prototypes, les relais et les constantes sont GENERES depuis src/kits/uikit/uikit.abi et les
 * .inc par tools/kits/abi2c.py (uikit_abi.h, uikit_abi.S, uikit_abi_open.c : tools/webkit/build-ui.sh
 * les fait et les compile dans libuikit_c.a) : chaque argument y est un long (un pointeur se passe
 * converti : (long) p). Ce fichier ajoute ce que le .abi ne dit pas : les variables de l'interface
 * (ui_data des programmes en assembleur) et des aides typees.
 *
 *   static struct ui_vars ui;
 *   if (!uikit_open (&ui)) return 1;                 // kits ouverts, polices chargees
 *   ui_window (640, 480, (long) "Titre");
 *   ui_fill (0, 0, ui.w, ui.h, 0xFFFFFF); ui_update_all ();
 *   while (ui_event (100) != WEV_CLOSE) ...
 *
 * Les pixels de la fenetre courante (ui.surf, 32 bits 0x00RRGGBB, pas = ui.w * 4) se dessinent
 * directement, puis ui_update (x, y, l, h) les montre. Plusieurs fenetres par programme : la
 * poignee est celle que rend ui_window ; ui.win, ui.surf, ui.w, ui.h et les widgets crees suivent
 * ui_window_use (win) ; ui_event rend les evenements de toutes les fenetres, EV_WIN dit laquelle,
 * ui_w_event va tout seul a la bonne ; WEV_CLOSE reste au programme : ui_window_close (win).
 */
#ifndef _ARMPI_UIKIT_H
#define _ARMPI_UIKIT_H

#include <stdint.h>
#include "uikit_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* les variables de l'interface : la macro ui_data de uikit.inc, champ pour champ */
struct ui_vars
{
	long win;				/* poignee de la fenetre courante */
	uint32_t *surf;				/* ses pixels */
	long w, h;				/* sa taille */
	unsigned char ev[32];			/* le dernier evenement (EV_* : type, sorte, x, y, code, caractere, mods, fenetre) */
	unsigned char font[UI_FONT_SIZE], font_b[UI_FONT_SIZE], font_m[UI_FONT_SIZE];
};

/* EV_MODS : les deux touches de chaque modificateur */
#define UI_MOD_CTRL 0x11
#define UI_MOD_SHIFT 0x22
#define UI_MOD_ALT 0x44

/* un champ 32 bits de l'evenement (EV_TYPE, EV_KIND, EV_X, EV_Y, EV_CODE, EV_CHAR, EV_MODS, EV_WIN) */
static inline int ui_ev_field (const struct ui_vars *v, int off)
{
	return *(const int32_t *) (v->ev + off);
}

/* les kits ouverts (UIKit et ce qu'il prend : AppKit) et ui_init fait -> 1 si les polices sont la */
static inline int uikit_open (struct ui_vars *v)
{
	if (!uikit_kit_open ())
		return 0;
	return (int) ui_init ((long) v);
}

/* les fonctions qui rendent plusieurs valeurs (x0, x1, x2) : les relais generes n'en donnent qu'une */
static inline long uikit_call3 (long (*fn) (), long a, long b, long c, long d, long *r1, long *r2)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x3 __asm__ ("x3") = d;
	register long (*x9) () __asm__ ("x9") = fn;
	__asm__ volatile ("blr x9" : "+r" (x0), "+r" (x1), "+r" (x2), "+r" (x3), "+r" (x9) : :
		"x4", "x5", "x6", "x7", "x8", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30", "memory", "cc");
	if (r1) *r1 = x1;
	if (r2) *r2 = x2;
	return x0;
}

/* -> widget concerne, *code = UN_*, *taken = 1 si un widget a pris l'evenement */
static inline long uikit_w_event (long *code, long *taken) { return uikit_call3 ((long (*) ()) ui_w_event, 0, 0, 0, 0, code, taken); }
/* -> octets (WEV_DROP), *kind = UDT_* */
static inline long uikit_drop_get (void *buf, long cap, long *kind) { return uikit_call3 ((long (*) ()) ui_drop_get, (long) buf, cap, 0, 0, kind, 0); }
/* -> texte, *data = sa donnee */
static inline long uikit_list_get (long w, long i, long *data) { return uikit_call3 ((long (*) ()) ui_list_get, w, i, 0, 0, data, 0); }
/* -> colonne triee (-1 : aucune), *desc = 1 decroissant */
static inline long uikit_list_sorted (long w, long *desc) { return uikit_call3 ((long (*) ()) ui_list_sorted, w, 0, 0, 0, desc, 0); }
/* -> octets avant le point, *px = x de ce point */
static inline long uikit_text_hit (const char *s, long size, long font, long x, long *px) { return uikit_call3 ((long (*) ()) ui_text_hit, (long) s, size, font, x, px, 0); }
/* -> debut de la selection, *end = sa fin */
static inline long uikit_textbox_selection (long w, long *end) { return uikit_call3 ((long (*) ()) ui_textbox_selection, w, 0, 0, 0, end, 0); }
/* -> ligne, *col = colonne */
static inline long uikit_textarea_caret (long w, long *col) { return uikit_call3 ((long (*) ()) ui_textarea_caret, w, 0, 0, 0, col, 0); }
/* -> pixels, *w, *h = leur taille */
static inline uint32_t *uikit_canvas_pixels (long c, long *w, long *h) { return (uint32_t *) uikit_call3 ((long (*) ()) ui_canvas_pixels, c, 0, 0, 0, w, h); }
/* -> largeur, *h = hauteur, *px = pixels 0xAARRGGBB */
static inline long uikit_image_size (long img, long *h, long *px) { return uikit_call3 ((long (*) ()) ui_image_size, img, 0, 0, 0, h, px); }

#ifdef __cplusplus
}
#endif

#endif
