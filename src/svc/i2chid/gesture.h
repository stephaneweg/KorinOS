/* gesture.h - d'un pavé tactile (rapports « precision touchpad » lus par hidrd.c) à une souris, avec les
 * gestes de l'écran tactile (src/svc/gt911, repris d'Onyx sys/touch.cpp) là où ils ont un sens sur un
 * touchpad (décisions de l'utilisateur du 12/11, docs/etude/PC-PILOTES.md « Touchpad ») :
 *	un doigt glisse                    -> le pointeur suit (accéléré)
 *	un doigt posé puis levé sur place  -> clic gauche (deux doigts : droit, trois : milieu)
 *	double toucher : un toucher, puis le doigt reposé aussitôt (TAP_DTAP_MS) et gardé
 *	                                   -> glisser (bouton gauche appuyé jusqu'au lever ; sans bouger :
 *	                                      double clic)
 *	deux doigts                        -> molette (la page suit les doigts, comme gt911 ; natural = 0 :
 *	                                      comme une molette) ; de gauche à droite : molette horizontale
 *	                                      (GEV_HWHEEL : le carrousel des bureaux d'Elegant, 23/11/2026) ;
 *	                                      le sens se décide au début du geste, il ne change plus ensuite
 *	boutons du pavé                    -> boutons 1 à 3 ; un pavé à un seul bouton (« clickpad ») : droit
 *	                                      à deux doigts ou dans le coin bas droit
 * Sans rien du système : essayé par test/hidrd_test.c. docs/etude/TOUCHPAD-PC.md. */
#ifndef I2CHID_GESTURE_H
#define I2CHID_GESTURE_H

#include "hidrd.h"

/* événements (ceux de l'anneau de hidmanager) */
#define GEV_MOUSE_REL 2 /* a = dx, b = dy (vers le bas), c = boutons (0 gauche, 1 droit, 2 milieu) */
#define GEV_WHEEL 4     /* a = crans (vers le haut > 0) */
#define GEV_HWHEEL 5    /* a = crans horizontaux (vers la gauche > 0, comme vers le haut) */
#define GEV_TOUCHPAD (1L << 30) /* (dans c : mouvement du pavé ; hidmanager lui donne la vitesse « touchpad »
				 * de /etc/pointer.ini, et l'ôte) */

struct gest_cfg {
	int speed;       /* vitesse du pointeur, % (100) */
	int tap;         /* toucher pour cliquer (1) */
	int natural;     /* défilement « naturel » : le contenu suit les doigts (1) */
	int scroll_mm100; /* course des doigts pour un cran de molette, centièmes de mm (300) */
};

typedef void (*gest_emit)(void *ctx, int type, long a, long b, long c);

struct gest {
	struct gest_cfg cfg;
	int32_t xrange, yrange, xmm100, ymm100, xmin, ymin;
	/* trame en cours (mode hybride : plusieurs rapports pour une trame) */
	struct hid_contact acc[HID_MAX_FINGERS];
	int nacc, remaining, acc_button, acc_scan;
	/* trame précédente */
	struct hid_contact prev[HID_MAX_FINGERS];
	int nprev, prev_scan;
	long prev_ms;
	/* toucher en cours */
	long t0;
	int maxf, phys_used, drag;
	int32_t moved;  /* centièmes de mm */
	int32_t x0, y0; /* là où il a commencé */
	/* dernier toucher simple (un doigt) : le double toucher */
	long tap_ms;
	int32_t tap_x, tap_y;
	/* boutons : du pavé (bits 0-2 : gauche, droit, milieu), de chaque souris */
	int phys, phys_which, pad_buttons;
	int mouse_buttons[HID_MAX_MICE];
	int clickpad; /* 1 : un seul bouton physique (le pavé entier) */
	/* restes (1/256 de point, centièmes de mm) */
	int32_t fx, fy, scroll;
	int32_t scroll_x; /* (deux doigts) course horizontale pas encore donnée */
	int axis;         /* sens du défilement à deux doigts : 0 pas encore décidé, 1 vertical, 2 horizontal */
	int jumps; /* doigts « téléportés » ignorés (frame) */
};

void gest_init(struct gest *g, const struct hid_model *m, const struct gest_cfg *cfg);
/* les boutons envoyés (pavé et souris) */
int gest_buttons(const struct gest *g);
/* un rapport du pavé (hid_decode, kind 2), au temps now (ms) ; nslots : doigts par rapport */
void gest_report(struct gest *g, const struct hid_frame *fr, int nslots, long now, gest_emit emit, void *ctx);
/* un rapport de souris (kind 1) */
void gest_mouse(struct gest *g, const struct hid_frame *fr, gest_emit emit, void *ctx);

#endif
