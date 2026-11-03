; ============================================================================
;  pad.inc - les manettes (docs/EMULATEURS.md). Tout passe par HIDManager :
;  le pilote donne l'etat brut (EV_PAD_RAW) ; HIDManager applique le mapping de
;  SD0:/etc/gamepad.ini aux boutons de manette et aux touches du clavier, et
;  depose EV_PAD dans l'anneau de ses abonnes ; Elegant le transmet a la fenetre
;  qui a le clavier, comme une touche : WEV_PAD. La touche mappee reste une touche.
;
;  Manette 0 : le clavier ; manettes 1..3 : les pilotes.
;  Boutons par leur PLACE sur une manette de type Xbox, comme Onyx : PAD_A en
;  bas, PAD_B a droite, PAD_X a gauche, PAD_Y en haut.
; ============================================================================

HID_OP_PADMAP	= 4			; relit SD0:/etc/gamepad.ini -> 0
EV_PAD_RAW	= 16			; pilote -> HIDManager : +8 boutons bruts (bit 0 : le 1er ;
					; bits 28..31 : chapeau haut, bas, gauche, droite),
					; +16 lx, ly, rx, ry (16 bits signes, -1000..1000),
					; +24 vid << 16 | pid (32 bits)
EV_PAD		= 17			; HIDManager -> abonnes : +4 manette, +8 boutons PAD_*,
					; +12 boutons bruts, +16 axes, comme EV_PAD_RAW,
					; +24 vid << 16 | pid
WEV_PAD_BUTTONS	= 8			; (WEV_PAD, evenement de fenetre) comme EV_PAD : +4, +8
WEV_PAD_RAW_B	= 12			; (+12 boutons bruts, +24 modele : l'applet Manette)
WEV_PAD_AXES	= 16
PAD_MAX		= 4

PAD_UP		= 1 shl 0
PAD_DOWN	= 1 shl 1
PAD_LEFT	= 1 shl 2
PAD_RIGHT	= 1 shl 3
PAD_A		= 1 shl 4
PAD_B		= 1 shl 5
PAD_X		= 1 shl 6
PAD_Y		= 1 shl 7
PAD_L		= 1 shl 8
PAD_R		= 1 shl 9
PAD_L2		= 1 shl 10
PAD_R2		= 1 shl 11
PAD_SELECT	= 1 shl 12
PAD_START	= 1 shl 13
PAD_L3		= 1 shl 14
PAD_R3		= 1 shl 15
PAD_HOME	= 1 shl 16
PAD_NBUTTONS	= 17
