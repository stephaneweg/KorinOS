; ============================================================================
;  svc/inc/module.inc - un module de demarrage (docs/MICRO-NOYAU.md, section 4) :
;  un fichier que le firmware charge avant le noyau (ligne « initramfs » de
;  config.txt), lu par src/kernel/modules.asm.
;
;	include '../inc/module.inc'
;	module 'vfs', 'chemin/du/contenu'	; contenu : un ELF de SD0:/sys/svc
; ============================================================================

macro module nom*, fichier* {
local debut, fin
debut:
	db 'ARMPIMOD'
	dd fin - debut				; taille, en-tete compris
	db nom, 0
	db (debut + 64 - $) dup 0		; nom sur 48 octets
	file fichier
fin:
}
