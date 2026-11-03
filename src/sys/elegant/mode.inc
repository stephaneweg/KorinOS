; ============================================================================
;  elegant/mode.inc - interface d'un mode d'Elegant (inclus avant le code :
;  la macro pol_call doit etre definie avant son emploi)
; ============================================================================

; --- table d'un mode ---------------------------------------------------------
POL_NAME	= 0			; nom (shell= de SD0:/etc/system.ini)
POL_BAR_H	= 8			; hauteur reservee en bas (placement des fenetres)
POL_INIT	= 16			; () : apres la prise de l'ecran
POL_BACK	= 24			; () : fond, dans le clip courant
POL_FRONT	= 32			; () : par-dessus les fenetres (barres, menus)
POL_CLICK	= 40			; x0 = x, x1 = y : clic gauche -> x0 = 1 si traite
POL_HOVER	= 48			; x0 = x, x1 = y : survol -> x0 = 1 si traite
POL_TICK	= 56			; () : a chaque tour de boucle
POL_WINS	= 64			; () : liste, focus ou titre des fenetres changes

macro pol_call off {
	adr	x16,policy
	ldr	x16,[x16]
	ldr	x16,[x16,off]
	blr	x16
}

