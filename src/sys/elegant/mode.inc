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
POL_FLAGS	= 72			; (valeur) POLF_*
POL_PLACE	= 80			; x0 = fenetre creee (placee, empilee, focus)
POL_KEY		= 88			; x0 = entree brute IN_KEY -> x0 = 1 si prise (pas a l'app)
POL_GONE	= 96			; x0 = fenetre qui va disparaitre
POL_RAISE	= 104			; x0 = fenetre ramenee devant (WS_OP_RAISE)

POLF_POCHE	= 1			; mode poche : cadres a deux glyphes (‒ accueil, ×),
					; fenetres a cadre ni deplacees ni redimensionnees
					; a la main (elegant/pocket.inc)

macro pol_call off {
	adr	x16,policy
	ldr	x16,[x16]
	ldr	x16,[x16,off]
	blr	x16
}

