; applets/pkgman.inc - applet Logiciels du panneau de configuration : les paquets
; ont leur fenetre, pkgman (docs/PAQUETS.md) ; choisir l'applet la lance, le bouton
; la rouvre.

sw_ouvrir:
	prologue
	adr	x0,sw_s_note
	mov	x1,R_NOTE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	applet_row
	adr	x0,ap_bouton
	adr	x1,sw_s_bouton
	str	x1,[x0]
	bl	sw_appliquer
	epilogue

sw_appliquer:
	prologue
	adr	x0,sw_p_app
	mov	x1,0
	sys	SYS_SPAWN
	epilogue

sw_titre	db 'Logiciels',0
sw_s_bouton	db 'Ouvrir',0
sw_s_note	db 'Mises ',0xC3,0xA0,' jour, paquets install',0xC3,0xA9,'s, paquets disponibles : dans la fen',0xC3,0xAA,'tre Logiciels.',0
sw_p_app	db 'SD0:/apps/pkgman.app/main',0
		align 4
