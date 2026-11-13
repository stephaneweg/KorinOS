/*
 * gamepad.h -- les manettes vues d'une app en C (traduction de src/inc/pad.inc, docs/EMULATEURS.md) : tout passe
 * par HIDManager, qui depose EV_PAD ; Elegant le transmet a la fenetre qui a le clavier : WEV_PAD. Manette 0 :
 * le clavier ; manettes 1..3 : les pilotes. Boutons par leur PLACE sur une manette de type Xbox : PAD_A en bas,
 * PAD_B a droite, PAD_X a gauche, PAD_Y en haut.
 */
#ifndef KORIN_GAMEPAD_H
#define KORIN_GAMEPAD_H

#ifndef HID_OP_PADMAP
#define HID_OP_PADMAP		4		/* relit SD0:/etc/gamepad.ini -> 0 */
#endif
#ifndef WEV_PAD_BUTTONS
#define WEV_PAD_BUTTONS		8		/* (WEV_PAD) +4 manette, +8 boutons PAD_* */
#endif
#ifndef WEV_PAD_RAW_B
#define WEV_PAD_RAW_B		12		/* +12 boutons bruts, +24 modele */
#endif
#ifndef WEV_PAD_AXES
#define WEV_PAD_AXES		16
#endif
#ifndef PAD_MAX
#define PAD_MAX			4
#endif

#ifndef PAD_UP
#define PAD_UP		(1u << 0)
#endif
#ifndef PAD_DOWN
#define PAD_DOWN	(1u << 1)
#endif
#ifndef PAD_LEFT
#define PAD_LEFT	(1u << 2)
#endif
#ifndef PAD_RIGHT
#define PAD_RIGHT	(1u << 3)
#endif
#ifndef PAD_A
#define PAD_A		(1u << 4)
#endif
#ifndef PAD_B
#define PAD_B		(1u << 5)
#endif
#ifndef PAD_X
#define PAD_X		(1u << 6)
#endif
#ifndef PAD_Y
#define PAD_Y		(1u << 7)
#endif
#ifndef PAD_L
#define PAD_L		(1u << 8)
#endif
#ifndef PAD_R
#define PAD_R		(1u << 9)
#endif
#ifndef PAD_L2
#define PAD_L2		(1u << 10)
#endif
#ifndef PAD_R2
#define PAD_R2		(1u << 11)
#endif
#ifndef PAD_SELECT
#define PAD_SELECT	(1u << 12)
#endif
#ifndef PAD_START
#define PAD_START	(1u << 13)
#endif
#ifndef PAD_L3
#define PAD_L3		(1u << 14)
#endif
#ifndef PAD_R3
#define PAD_R3		(1u << 15)
#endif
#ifndef PAD_HOME
#define PAD_HOME	(1u << 16)
#endif
#ifndef PAD_NBUTTONS
#define PAD_NBUTTONS	17
#endif

#endif
