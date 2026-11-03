; ============================================================================
;  inc/applet.inc - ce qu'ont en commun les applets du bureau (sys/agenda,
;  sys/stickies) : lecture d'un fichier, leur place. Le programme fournit fbuf
;  (FBUF octets).
; ============================================================================

APPLET_Y	= 44			; place fixe : sous la barre de menus (une constante : la
APPLET_X	= 16			; session les lance avant qu'elle soit reservee)
APPLET_BAS	= 64			; (panel compris)

macro ldv reg, var {
	adr	x16,var
	ldr	reg,[x16]
}
macro stv reg, var {
	adr	x16,var
	str	reg,[x16]
}

; x0 = chemin, x1 = tampon, x2 = place -> x0 = octets lus (0 : pas de fichier)
read_into:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x1
	mov	x22,x2
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x20,0
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x1,x21
	mov	x2,x22
	sys	SYS_READ
	cmp	x0,0
	csel	x20,x0,xzr,gt
	mov	x0,x19
	sys	SYS_CLOSE
.out:	mov	x0,x20
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin -> x0 = octets dans fbuf, termine par 0 (0 : pas de fichier)
read_file:
	prologue
	adr	x1,fbuf
	mov	x2,FBUF-1
	bl	read_into
	adr	x1,fbuf
	strb	wzr,[x1,x0]
	epilogue
