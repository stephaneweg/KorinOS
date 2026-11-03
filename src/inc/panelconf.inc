; ============================================================================
;  inc/panelconf.inc - les reglages du panel (SD0:/etc/panel.ini), communs au
;  panel (sys/panel) et a son applet du panneau de configuration. A inclure
;  apres dockconf.inc (dont il reprend dl_ndesks, dl_desk, dl_corbeille,
;  dl_taille et la lecture de ces cles). Une ligne par reglage :
;
;	pin       = terminal		un raccourci, dans l'ordre (12 au plus)
;	desk      = Principal		un espace de travail, son nom (1 a 6)
;	corbeille = oui | non
;	taille    = auto | grande | moyenne | petite
;
;  Sans panel.ini : dock.ini de l'ancien dock (ses lanceurs deviennent des
;  raccourcis), sinon terminal et fichiers.
;
;	bl applis_load ; bl pan_load		pan_npin, pan_pin
; ============================================================================

PANEL_MAXPINS	= 12

; (ini_read) x0 = cle, x1 = valeur
pan_line:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x1,pan_k_pin
	bl	str_eq
	cbnz	x0,.pin
	mov	x0,x19
	adr	x1,dl_k_launcher
	bl	str_eq
	cbnz	x0,.pin
	mov	x0,x19
	adr	x1,dl_k_lanceur
	bl	str_eq
	cbnz	x0,.pin
	mov	x0,x19
	mov	x1,x20
	bl	dl_line
	b	.out
.pin:	mov	x0,x20
	bl	pan_add
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom : un raccourci de plus
pan_add:
	prologue
	ldrb	w1,[x0]
	cbz	w1,.out
	mov	x1,x0
	adr	x2,pan_npin
	ldr	x3,[x2]
	cmp	x3,PANEL_MAXPINS
	b.hs	.out
	add	x4,x3,1
	str	x4,[x2]
	adr	x0,pan_pin
	add	x0,x0,x3,lsl 5
	mov	x2,AP_NOM
	bl	str_copy_n
.out:	epilogue

pan_load:
	prologue
	str	x19,[sp,-16]!
	adr	x0,dl_ncats
	str	xzr,[x0]
	adr	x0,dl_nlan
	str	xzr,[x0]
	adr	x0,dl_ndesks
	str	xzr,[x0]
	adr	x0,pan_npin
	str	xzr,[x0]
	adr	x0,dl_corbeille
	mov	x1,1
	str	x1,[x0]
	adr	x0,dl_taille
	mov	x1,-1
	str	x1,[x0]
	adr	x0,pan_p_ini
	adr	x1,pan_line
	bl	ini_read
	cbnz	x0,.desks
	adr	x0,dl_p_dock
	adr	x1,pan_line
	bl	ini_read
	cbnz	x0,.desks
	adr	x0,dl_d_terminal
	bl	pan_add
	adr	x0,dl_d_files
	bl	pan_add
.desks:	adr	x19,dl_ndesks			; aucun espace nomme : 4, de 1 a 4
	ldr	x0,[x19]
	cbnz	x0,.out
	mov	x0,4
	str	x0,[x19]
	mov	x19,0
.dn:	mov	x0,DL_DESK
	adr	x1,dl_desk
	madd	x0,x19,x0,x1
	add	w1,w19,'1'
	strb	w1,[x0]
	strb	wzr,[x0,1]
	add	x19,x19,1
	cmp	x19,4
	b.lo	.dn
.out:	ldr	x19,[sp],16
	epilogue

; x0 = application -> x0 = 1 si elle est une partie du bureau (categorie
; Shell : ni dans le lanceur, ni parmi les raccourcis)
pan_shell:
	prologue
	adr	x1,ap_cat
	ldrb	w0,[x1,x0]
	mov	x1,CA_NOM
	adr	x2,ca_nom
	madd	x0,x0,x1,x2
	adr	x1,pan_s_shell
	bl	str_ci_cmp
	cmp	x0,0
	cset	x0,eq
	epilogue

pan_p_ini	db 'SD0:/etc/panel.ini',0
pan_k_pin	db 'pin',0
pan_s_shell	db 'Shell',0
		align 4

macro panelconf_data {
		align 8
pan_npin	dd 0
pan_pin		rb PANEL_MAXPINS * AP_NOM
}
