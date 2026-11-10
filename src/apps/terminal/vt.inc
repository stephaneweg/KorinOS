; vt.inc - le Terminal : emulateur VT/xterm (grille de cellules, analyseur)
;
; Grille lignes x colonnes de cellules de 16 octets :
;	+0 point de code (0 : vide, dessine comme une espace)
;	+4 attributs VA_*
;	+8 couleur du texte, +12 couleur du fond : 0 par defaut, VC_IDX | n
;	   (palette de 256), VC_RGB | 0xRRGGBB (24 bits)
; Une cellule effacee avec le stylo par defaut vaut 0 : la memoire rendue
; par SYS_VM_MAP est une grille vide. Chaque ligne fait VT_ROWB octets
; (VT_MAXCOLS cellules) ; une table de pointeurs par ecran (rowp_main,
; rowp_alt) donne ses lignes : defiler fait tourner des pointeurs.
;
; Analyseur : l'automate de Paul Williams (VT500), reduit au sous-ensemble
; xterm de docs/TERMINAL-ET-FORK.md 1.4. UTF-8 decode en points de code (une
; cellule par point de code, sans table de largeurs). Les sequences inconnues
; sont avalees, jamais affichees.
;
; Pas encore de pseudo-terminal (service vterm, etape 2) : LF vaut CR LF
; (VM_LFCR, ce que fera ONLCR) et les reponses (DSR, DA) ne partent nulle part.

VT_MAXCOLS	= 256
VT_MAXROWS	= 128
VT_ROWSH	= 12			; VT_ROWB = 1 shl VT_ROWSH
VT_ROWB		= VT_MAXCOLS*16
VT_GRIDB	= VT_MAXROWS*VT_ROWB
VT_SBMAX	= 1000			; lignes d'historique
VT_MEM		= 3*VT_GRIDB + VT_SBMAX*VT_ROWB	; principal, alternatif, ombre, historique
VT_NPAR		= 16			; parametres CSI gardes

; attributs (+4)
VA_BOLD		= 1
VA_DIM		= 2
VA_ITALIC	= 4			; garde, dessine droit (pas de mono italique)
VA_UNDER	= 8
VA_BLINK	= 16			; garde, sans clignotement
VA_REV		= 32
VA_HIDDEN	= 64
VA_STRIKE	= 128
; couleurs (+8, +12)
VC_RGB		= 0x01000000
VC_IDX		= 0x02000000

; modes (V_MODES)
VM_CURSOR	= 1			; ?25 curseur visible
VM_WRAP		= 2			; ?7 retour a la ligne automatique
VM_PASTE	= 4			; ?2004 collage encadre (note seulement)
VM_LFCR		= 8			; LF fait aussi CR (ONLCR, en attendant vterm)
VM_APPCK	= 16			; ?1 fleches en mode application (ESC O A)
VM_DEFAUT	= VM_CURSOR or VM_WRAP or VM_LFCR

; etats de l'analyseur
ST_GROUND	= 0
ST_ESC		= 1
ST_ESCI		= 2			; ESC + intermediaire
ST_CSI		= 3			; CSI, rien encore
ST_CSIP		= 4			; parametres
ST_CSII		= 5			; intermediaire
ST_CSIX		= 6			; CSI mal forme : avale jusqu'au final
ST_STR		= 7			; OSC, DCS, SOS, PM, APC : avale jusqu'a BEL ou ST

; etat du terminal (bloc vt)
V_ROWS		= 0
V_COLS		= 8
V_CX		= 16
V_CY		= 24
V_WRAP		= 32			; 1 : derniere colonne ecrite, retour en attente
V_TOP		= 40			; zone de defilement (incluse)
V_BOT		= 48
V_PEN		= 56			; stylo : comme une cellule (point de code 0)
V_TBL		= 72			; table des lignes de l'ecran montre
V_ALT		= 80			; 1 : ecran alternatif
V_STATE		= 88
V_UNEED		= 96			; UTF-8 : octets attendus
V_UCP		= 104
V_NP		= 112			; parametres CSI lus
V_PREFIX	= 120			; ? > < =
V_INTER		= 128			; intermediaire (ESC, CSI)
V_PAR		= 136			; VT_NPAR x 32 bits
V_MODES		= 200
V_SBN		= 208			; lignes d'historique
V_SBHEAD	= 216			; prochaine place dans l'anneau
V_SCROLLS	= 224			; lignes sorties par le haut (repere de la saisie)
V_LASTCP	= 232			; dernier caractere (REP)
V_SC		= 240			; curseur sauve (ESC 7, CSI s, ?1048)
V_SC2		= 288			; curseur sauve par ?1049
V_SIZE		= 336
; curseur sauve
SC_X		= 0
SC_Y		= 8
SC_PEN		= 16
SC_WRAP		= 32
SC_OK		= 40

; -> x0 = 0 (< 0 : pas de memoire)
vt_init:
	prologue
	mov	x0,0
	movl	x1,VT_MEM
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.out
	adr	x2,rowp_main
	adr	x3,rowp_alt
	movl	x6,VT_GRIDB
	mov	x4,0
.r:	add	x5,x0,x4,lsl VT_ROWSH
	str	x5,[x2,x4,lsl 3]
	add	x5,x5,x6
	str	x5,[x3,x4,lsl 3]
	add	x4,x4,1
	cmp	x4,VT_MAXROWS
	b.lo	.r
	add	x5,x0,x6,lsl 1			; ombre (ce qui est a l'ecran)
	adr	x7,vt_shadow
	str	x5,[x7]
	add	x5,x5,x6			; historique
	adr	x7,vt_sb
	str	x5,[x7]
	adr	x9,vt
	adr	x1,rowp_main
	str	x1,[x9,V_TBL]
	mov	x1,VM_DEFAUT
	str	x1,[x9,V_MODES]
	bl	pal_init
	mov	x0,0
.out:	epilogue

; ---------------------------------------------------------------------------
;  Cellules et lignes
; ---------------------------------------------------------------------------

; x0 = ligne -> x0 = ses cellules (ecran montre). Feuille (x0, x1).
vt_row:
	adr	x1,vt
	ldr	x1,[x1,V_TBL]
	ldr	x0,[x1,x0,lsl 3]
	ret

; x0 = cellules, x1 = nombre, x2 = moitie haute (couleurs ; basse : 0). Feuille.
cells_fill:
	cmp	x1,0
	b.le	.r
.l:	stp	xzr,x2,[x0],16
	subs	x1,x1,1
	b.ne	.l
.r:	ret

; x0 = destination, x1 = source, x2 = nombre de cellules (recouvrement
; permis). Feuille (x0-x2, v0).
cells_move:
	cmp	x2,0
	b.le	.r
	cmp	x0,x1
	b.eq	.r
	b.hi	.back
.f:	ldr	q0,[x1],16
	str	q0,[x0],16
	subs	x2,x2,1
	b.ne	.f
	ret
.back:	add	x0,x0,x2,lsl 4
	add	x1,x1,x2,lsl 4
.b:	ldr	q0,[x1,-16]!
	str	q0,[x0,-16]!
	subs	x2,x2,1
	b.ne	.b
.r:	ret

; x0 = ligne, x1 = premiere colonne, x2 = fin (exclue, bornee aux colonnes) :
; cellules effacees avec le fond du stylo
vt_clear_span:
	prologue
	adr	x9,vt
	ldr	x3,[x9,V_COLS]
	cmp	x2,x3
	csel	x2,x3,x2,hi
	cmp	x1,x2
	b.hs	.out
	ldr	x3,[x9,V_TBL]
	ldr	x0,[x3,x0,lsl 3]
	add	x0,x0,x1,lsl 4
	sub	x1,x2,x1
	ldr	x2,[x9,V_PEN+8]			; fond du stylo, texte par defaut
	and	x2,x2,0xFFFFFFFF00000000
	bl	cells_fill
.out:	epilogue

; x0 = premiere ligne, x1 = fin (exclue) : lignes entieres effacees
vt_clear_rows:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.l:	cmp	x19,x20
	b.ge	.out
	mov	x0,x19
	mov	x1,0
	mov	x2,VT_MAXCOLS
	bl	vt_clear_span
	add	x19,x19,1
	b	.l
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = cellules d'une ligne : copiee au bout de l'historique (colonnes en
; trop a 0)
sb_push:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x20,vt
	ldr	x1,[x20,V_SBHEAD]
	adr	x2,vt_sb
	ldr	x2,[x2]
	add	x0,x2,x1,lsl VT_ROWSH		; place de la ligne
	add	x1,x1,1
	cmp	x1,VT_SBMAX
	csel	x1,xzr,x1,hs
	str	x1,[x20,V_SBHEAD]
	ldr	x1,[x20,V_SBN]
	add	x1,x1,1
	mov	x2,VT_SBMAX
	cmp	x1,x2
	csel	x1,x2,x1,hi
	str	x1,[x20,V_SBN]
	adr	x3,scroll			; vue remontee : elle garde ses lignes
	ldr	x4,[x3]
	cbz	x4,.cp
	add	x4,x4,1
	cmp	x4,x1
	csel	x4,x1,x4,hi
	str	x4,[x3]
.cp:	ldr	x2,[x20,V_COLS]
	stp	x0,x2,[sp,-16]!
	mov	x1,x19
	bl	cells_move
	ldp	x0,x2,[sp],16
	add	x0,x0,x2,lsl 4
	mov	x1,VT_MAXCOLS
	sub	x1,x1,x2
	mov	x2,0
	bl	cells_fill
	ldp	x19,x20,[sp],16
	epilogue

; x0 = haut, x1 = bas (inclus), x2 = n, x3 = 1 : historique permis. La zone
; monte de n lignes, des lignes vides entrent en bas. Les lignes qui sortent
; du haut de l'ecran principal vont dans l'historique.
vt_up:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x20,x19
	b.lt	.out
	sub	x0,x20,x19
	add	x0,x0,1
	cmp	x21,x0
	csel	x21,x0,x21,gt
	cmp	x21,0
	b.le	.out
	adr	x23,vt
	ldr	x22,[x23,V_TBL]
	mov	x24,0				; historique ?
	cbz	x3,.loop
	ldr	x0,[x23,V_ALT]
	cbnz	x0,.loop
	cbnz	x19,.loop
	mov	x24,1
.loop:	cbz	x21,.out
	cbz	x24,.rot
	ldr	x0,[x22,x19,lsl 3]
	bl	sb_push
	ldr	x0,[x23,V_SCROLLS]
	add	x0,x0,1
	str	x0,[x23,V_SCROLLS]
.rot:	ldr	x9,[x22,x19,lsl 3]		; la ligne du haut passe en bas
	mov	x10,x19
.sh:	cmp	x10,x20
	b.hs	.last
	add	x11,x10,1
	ldr	x12,[x22,x11,lsl 3]
	str	x12,[x22,x10,lsl 3]
	mov	x10,x11
	b	.sh
.last:	str	x9,[x22,x20,lsl 3]
	mov	x0,x20
	mov	x1,0
	mov	x2,VT_MAXCOLS
	bl	vt_clear_span
	sub	x21,x21,1
	b	.loop
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = haut, x1 = bas (inclus), x2 = n : la zone descend de n lignes, des
; lignes vides entrent en haut
vt_down:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x20,x19
	b.lt	.out
	sub	x0,x20,x19
	add	x0,x0,1
	cmp	x21,x0
	csel	x21,x0,x21,gt
	cmp	x21,0
	b.le	.out
	adr	x22,vt
	ldr	x22,[x22,V_TBL]
.loop:	cbz	x21,.out
	ldr	x9,[x22,x20,lsl 3]		; la ligne du bas passe en haut
	mov	x10,x20
.sh:	cmp	x10,x19
	b.ls	.first
	sub	x11,x10,1
	ldr	x12,[x22,x11,lsl 3]
	str	x12,[x22,x10,lsl 3]
	mov	x10,x11
	b	.sh
.first:	str	x9,[x22,x19,lsl 3]
	mov	x0,x19
	mov	x1,0
	mov	x2,VT_MAXCOLS
	bl	vt_clear_span
	sub	x21,x21,1
	b	.loop
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Curseur
; ---------------------------------------------------------------------------

; x0 = colonne, x1 = ligne (signees) : bornees a l'ecran ; retour en attente
; annule. Feuille.
vt_goto:
	adr	x9,vt
	cmp	x0,0
	csel	x0,xzr,x0,lt
	ldr	x2,[x9,V_COLS]
	sub	x2,x2,1
	cmp	x0,x2
	csel	x0,x2,x0,gt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	ldr	x2,[x9,V_ROWS]
	sub	x2,x2,1
	cmp	x1,x2
	csel	x1,x2,x1,gt
	str	x0,[x9,V_CX]
	str	x1,[x9,V_CY]
	str	xzr,[x9,V_WRAP]
	ret

; une ligne plus bas ; en bas de la zone de defilement, elle monte
vt_lf:
	prologue
	adr	x9,vt
	ldr	x0,[x9,V_CY]
	ldr	x1,[x9,V_BOT]
	cmp	x0,x1
	b.ne	.down
	ldr	x0,[x9,V_TOP]
	mov	x2,1
	mov	x3,1
	bl	vt_up
	b	.out
.down:	ldr	x2,[x9,V_ROWS]
	sub	x2,x2,1
	cmp	x0,x2
	b.hs	.out
	add	x0,x0,1
	str	x0,[x9,V_CY]
.out:	epilogue

; une ligne plus haut ; en haut de la zone, elle descend (ESC M)
vt_ri:
	prologue
	adr	x9,vt
	ldr	x0,[x9,V_CY]
	ldr	x1,[x9,V_TOP]
	cmp	x0,x1
	b.ne	.up
	mov	x0,x1
	ldr	x1,[x9,V_BOT]
	mov	x2,1
	bl	vt_down
	b	.out
.up:	cbz	x0,.out
	sub	x0,x0,1
	str	x0,[x9,V_CY]
.out:	epilogue

; x0 = place (V_SC, V_SC2) : curseur et stylo sauves. Feuille.
vt_save:
	adr	x9,vt
	add	x1,x9,x0
	ldr	x2,[x9,V_CX]
	ldr	x3,[x9,V_CY]
	stp	x2,x3,[x1,SC_X]
	ldp	x2,x3,[x9,V_PEN]
	stp	x2,x3,[x1,SC_PEN]
	ldr	x2,[x9,V_WRAP]
	mov	x3,1
	stp	x2,x3,[x1,SC_WRAP]
	ret

; x0 = place : curseur et stylo rendus (rien de sauve : coin, stylo par defaut)
vt_restore:
	prologue
	adr	x9,vt
	add	x4,x9,x0
	ldr	x2,[x4,SC_OK]
	cbz	x2,.home
	ldp	x2,x3,[x4,SC_PEN]
	stp	x2,x3,[x9,V_PEN]
	ldp	x0,x1,[x4,SC_X]
	bl	vt_goto
	b	.out
.home:	stp	xzr,xzr,[x9,V_PEN]
	mov	x0,0
	mov	x1,0
	bl	vt_goto
.out:	epilogue

; x0 = 1 : ecran alternatif efface en entrant
vt_alt_enter:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x9,vt
	ldr	x1,[x9,V_ALT]
	cbnz	x1,.clr
	mov	x1,1
	str	x1,[x9,V_ALT]
	adr	x1,rowp_alt
	str	x1,[x9,V_TBL]
	adr	x1,scroll			; pas d'historique sur cet ecran
	str	xzr,[x1]
.clr:	cbz	x19,.out
	mov	x0,0
	adr	x9,vt
	ldr	x1,[x9,V_ROWS]
	bl	vt_clear_rows
.out:	ldr	x19,[sp],16
	epilogue

; retour a l'ecran principal. Feuille.
vt_alt_leave:
	adr	x9,vt
	ldr	x1,[x9,V_ALT]
	cbz	x1,.r
	str	xzr,[x9,V_ALT]
	adr	x1,rowp_main
	str	x1,[x9,V_TBL]
.r:	ret

; remise a zero douce (DECSTR, et fin d'un programme) : stylo, zone, modes,
; curseurs sauves, analyseur ; l'ecran reste. Feuille.
vt_soft_reset:
	adr	x9,vt
	stp	xzr,xzr,[x9,V_PEN]
	str	xzr,[x9,V_TOP]
	ldr	x1,[x9,V_ROWS]
	sub	x1,x1,1
	str	x1,[x9,V_BOT]
	ldr	x1,[x9,V_MODES]
	orr	x1,x1,VM_CURSOR or VM_WRAP
	mov	x2,VM_APPCK or VM_PASTE
	bic	x1,x1,x2
	str	x1,[x9,V_MODES]
	str	xzr,[x9,V_SC+SC_OK]
	str	xzr,[x9,V_SC2+SC_OK]
	str	xzr,[x9,V_STATE]
	str	xzr,[x9,V_UNEED]
	str	xzr,[x9,V_WRAP]
	ret

; remise a zero complete (ESC c) ; l'historique reste
vt_ris:
	prologue
	bl	vt_alt_leave
	bl	vt_soft_reset
	adr	x9,vt
	mov	x1,VM_DEFAUT
	str	x1,[x9,V_MODES]
	mov	x0,0
	ldr	x1,[x9,V_ROWS]
	bl	vt_clear_rows
	mov	x0,0
	mov	x1,0
	bl	vt_goto
	epilogue

; x0 = 0 : du curseur a la fin, 1 : du debut au curseur, 2 : tout l'ecran,
; 3 : l'historique (ED)
vt_erase:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,vt
	str	xzr,[x19,V_WRAP]
	cmp	x0,1
	b.eq	.above
	cmp	x0,2
	b.eq	.all
	cmp	x0,3
	b.eq	.sb
	ldr	x0,[x19,V_CY]
	ldr	x1,[x19,V_CX]
	mov	x2,VT_MAXCOLS
	bl	vt_clear_span
	ldr	x0,[x19,V_CY]
	add	x0,x0,1
	ldr	x1,[x19,V_ROWS]
	bl	vt_clear_rows
	b	.out
.above:	mov	x0,0
	ldr	x1,[x19,V_CY]
	bl	vt_clear_rows
	ldr	x0,[x19,V_CY]
	mov	x1,0
	ldr	x2,[x19,V_CX]
	add	x2,x2,1
	bl	vt_clear_span
	b	.out
.all:	mov	x0,0
	ldr	x1,[x19,V_ROWS]
	bl	vt_clear_rows
	b	.out
.sb:	str	xzr,[x19,V_SBN]
	str	xzr,[x19,V_SBHEAD]
	adr	x0,scroll
	str	xzr,[x0]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = colonnes, x1 = lignes : nouvelle taille. Pas de reformatage : les
; lignes sont coupees ou allongees (cellules vides) ; si la ligne du curseur
; sort par le bas, l'ecran monte (vers l'historique).
vt_resize:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x2,2				; bornes
	cmp	x0,x2
	csel	x0,x2,x0,lt
	mov	x2,VT_MAXCOLS
	cmp	x0,x2
	csel	x0,x2,x0,gt
	mov	x2,1
	cmp	x1,x2
	csel	x1,x2,x1,lt
	mov	x2,VT_MAXROWS
	cmp	x1,x2
	csel	x1,x2,x1,gt
	mov	x19,x0
	mov	x20,x1
	adr	x21,vt
	ldr	x22,[x21,V_COLS]		; taille d'avant
	ldr	x23,[x21,V_ROWS]
	ldr	x0,[x21,V_CY]
	cmp	x0,x20
	b.lo	.grow
	sub	x2,x0,x20			; la ligne du curseur reste la derniere
	add	x2,x2,1
	mov	x0,0
	sub	x1,x23,1
	mov	x3,1
	bl	vt_up
	sub	x0,x20,1
	str	x0,[x21,V_CY]
.grow:	mov	x24,0				; places neuves vides, dans les deux ecrans
.g:	cmp	x24,x20
	b.hs	.set
	cmp	x24,x23
	csel	x0,x22,xzr,lo			; premiere cellule neuve de la ligne
	cmp	x0,x19
	b.hs	.gn
	sub	x1,x19,x0
	stp	x0,x1,[sp,-16]!
	adr	x9,rowp_main
	ldr	x2,[x9,x24,lsl 3]
	add	x0,x2,x0,lsl 4
	mov	x2,0
	bl	cells_fill
	ldp	x0,x1,[sp],16
	adr	x9,rowp_alt
	ldr	x2,[x9,x24,lsl 3]
	add	x0,x2,x0,lsl 4
	mov	x2,0
	bl	cells_fill
.gn:	add	x24,x24,1
	b	.g
.set:	str	x19,[x21,V_COLS]
	str	x20,[x21,V_ROWS]
	str	xzr,[x21,V_TOP]
	sub	x0,x20,1
	str	x0,[x21,V_BOT]
	ldr	x0,[x21,V_CX]
	ldr	x1,[x21,V_CY]
	bl	vt_goto
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Ecriture
; ---------------------------------------------------------------------------

; w0 = point de code : ecrit au curseur avec le stylo
vt_print:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	adr	x20,vt
	str	x19,[x20,V_LASTCP]
	ldr	x0,[x20,V_WRAP]
	cbz	x0,.put
	str	xzr,[x20,V_WRAP]		; retour a la ligne en attente
	str	xzr,[x20,V_CX]
	bl	vt_lf
.put:	ldr	x0,[x20,V_CY]
	bl	vt_row
	ldr	x1,[x20,V_CX]
	add	x0,x0,x1,lsl 4
	ldp	x2,x3,[x20,V_PEN]
	orr	x2,x2,x19
	stp	x2,x3,[x0]
	ldr	x2,[x20,V_COLS]
	add	x1,x1,1
	cmp	x1,x2
	b.hs	.edge
	str	x1,[x20,V_CX]
	b	.out
.edge:	ldr	x1,[x20,V_MODES]		; derniere colonne : le curseur y reste
	tbz	x1,1,.out			; (VM_WRAP)
	mov	x1,1
	str	x1,[x20,V_WRAP]
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = commande C0 (< 0x20)
vt_c0:
	prologue
	adr	x9,vt
	cmp	w0,8
	b.eq	.bs
	cmp	w0,9
	b.eq	.ht
	cmp	w0,13
	b.eq	.cr
	cmp	w0,10
	b.lo	.out				; BEL et les autres : rien
	cmp	w0,12
	b.hi	.out				; (SO, SI... : rien)
	bl	vt_lf				; LF, VT, FF
	adr	x9,vt
	ldr	x0,[x9,V_MODES]
	tbz	x0,3,.nocr			; (VM_LFCR)
	str	xzr,[x9,V_CX]
.nocr:	str	xzr,[x9,V_WRAP]
	b	.out
.cr:	str	xzr,[x9,V_CX]
	str	xzr,[x9,V_WRAP]
	b	.out
.bs:	ldr	x1,[x9,V_CX]
	cbz	x1,.bs0
	sub	x1,x1,1
	str	x1,[x9,V_CX]
.bs0:	str	xzr,[x9,V_WRAP]
	b	.out
.ht:	ldr	x1,[x9,V_CX]			; taquets toutes les 8 colonnes
	orr	x1,x1,7
	add	x1,x1,1
	ldr	x2,[x9,V_COLS]
	sub	x2,x2,1
	cmp	x1,x2
	csel	x1,x2,x1,hi
	str	x1,[x9,V_CX]
	str	xzr,[x9,V_WRAP]
.out:	epilogue

; w0 = octet : l'entree de l'analyseur (sortie d'un programme, echo du Terminal)
term_putb:
	prologue
	stp	x19,x20,[sp,-16]!
	and	w19,w0,0xFF
	adr	x20,vt
	ldr	x1,[x20,V_STATE]
	cmp	w19,0x1B
	b.eq	.esc
	cmp	w19,0x18			; CAN, SUB : sequence abandonnee
	b.eq	.cancel
	cmp	w19,0x1A
	b.eq	.cancel
	cmp	x1,ST_STR
	b.ne	.nostr
	cmp	w19,7				; BEL termine un OSC ; le reste est avale
	b.ne	.out
.cancel:
	str	xzr,[x20,V_STATE]
	str	xzr,[x20,V_UNEED]
	b	.out
.nostr:	cmp	w19,0x20
	b.hs	.notc0
	str	xzr,[x20,V_UNEED]		; (UTF-8 coupe : abandonne)
	mov	w0,w19
	bl	vt_c0				; execute aussi au milieu d'une sequence
	b	.out
.notc0:	cmp	w19,0x7F
	b.eq	.out
	cmp	x1,ST_GROUND
	b.eq	.ground
	cmp	x1,ST_ESC
	b.eq	.escst
	cmp	x1,ST_ESCI
	b.eq	.esci
	mov	w0,w19
	bl	vt_csi_byte
	b	.out
.esc:	mov	x0,ST_ESC
	str	x0,[x20,V_STATE]
	str	xzr,[x20,V_UNEED]
	str	xzr,[x20,V_INTER]
	b	.out
.ground:
	mov	w0,w19
	bl	vt_utf8
	b	.out
.escst:	mov	w0,w19
	bl	vt_esc
	b	.out
.esci:	cmp	w19,0x30			; ESC ( B, ESC # 8... : avales
	b.lo	.out
	str	xzr,[x20,V_STATE]
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = octet (>= 0x20, hors sequence) : UTF-8 -> point de code -> vt_print
vt_utf8:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	adr	x20,vt
	ldr	x1,[x20,V_UNEED]
	cmp	w19,0x80
	b.hs	.hi
	cbz	x1,.ascii
	str	xzr,[x20,V_UNEED]		; sequence coupee
	mov	w0,0xFFFD
	bl	vt_print
.ascii:	mov	w0,w19
	bl	vt_print
	b	.out
.hi:	cmp	w19,0xC0
	b.hs	.lead
	cbz	x1,.bad				; suite sans debut
	ldr	x2,[x20,V_UCP]
	and	w3,w19,0x3F
	orr	x2,x3,x2,lsl 6
	str	x2,[x20,V_UCP]
	sub	x1,x1,1
	str	x1,[x20,V_UNEED]
	cbnz	x1,.out
	mov	x0,x2
	movl	x3,0x10FFFF
	cmp	x0,x3
	b.hi	.bad
	and	x3,x0,0xFFFFF800		; demi-codets (D800-DFFF)
	mov	x4,0xD800
	cmp	x3,x4
	b.eq	.bad
	bl	vt_print
	b	.out
.lead:	cbz	x1,.lead2
	str	xzr,[x20,V_UNEED]
	mov	w0,0xFFFD
	bl	vt_print
.lead2:	cmp	w19,0xC2
	b.lo	.bad
	mov	x1,1
	and	w2,w19,0x1F
	cmp	w19,0xE0
	b.lo	.set
	mov	x1,2
	and	w2,w19,0x0F
	cmp	w19,0xF0
	b.lo	.set
	mov	x1,3
	and	w2,w19,0x07
	cmp	w19,0xF5
	b.lo	.set
.bad:	str	xzr,[x20,V_UNEED]
	mov	w0,0xFFFD
	bl	vt_print
	b	.out
.set:	str	x1,[x20,V_UNEED]
	str	x2,[x20,V_UCP]
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = octet apres ESC (0x20..0xFF)
vt_esc:
	prologue
	adr	x9,vt
	cmp	w0,0x30
	b.lo	.inter
	str	xzr,[x9,V_STATE]		; (le plus souvent : fini)
	cmp	w0,0x7F
	b.hs	.out
	cmp	w0,'['
	b.eq	.csi
	cmp	w0,']'
	b.eq	.str
	cmp	w0,'P'				; DCS
	b.eq	.str
	cmp	w0,'X'				; SOS
	b.eq	.str
	cmp	w0,'^'				; PM
	b.eq	.str
	cmp	w0,'_'				; APC
	b.eq	.str
	cmp	w0,'7'
	b.eq	.save
	cmp	w0,'8'
	b.eq	.rest
	cmp	w0,'D'
	b.eq	.ind
	cmp	w0,'E'
	b.eq	.nel
	cmp	w0,'M'
	b.eq	.ri
	cmp	w0,'c'
	b.ne	.out				; (ESC = ESC > ESC \ ... : rien)
	bl	vt_ris
	b	.out
.inter:	mov	x1,ST_ESCI
	str	x1,[x9,V_STATE]
	str	x0,[x9,V_INTER]
	b	.out
.csi:	mov	x1,ST_CSI
	str	x1,[x9,V_STATE]
	str	xzr,[x9,V_NP]
	str	wzr,[x9,V_PAR]
	str	xzr,[x9,V_PREFIX]
	str	xzr,[x9,V_INTER]
	b	.out
.str:	mov	x1,ST_STR
	str	x1,[x9,V_STATE]
	b	.out
.save:	mov	x0,V_SC
	bl	vt_save
	b	.out
.rest:	mov	x0,V_SC
	bl	vt_restore
	b	.out
.nel:	str	xzr,[x9,V_CX]
.ind:	str	xzr,[x9,V_WRAP]
	bl	vt_lf
	b	.out
.ri:	str	xzr,[x9,V_WRAP]
	bl	vt_ri
.out:	epilogue

; w0 = octet d'une sequence CSI (>= 0x20)
vt_csi_byte:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	adr	x20,vt
	ldr	x1,[x20,V_STATE]
	cmp	w19,0x80
	b.hs	.ignore
	cmp	w19,0x40
	b.hs	.final
	cmp	x1,ST_CSIX
	b.eq	.out
	cmp	w19,0x30
	b.lo	.inter
	cmp	w19,0x3C
	b.hs	.prefix
	cmp	x1,ST_CSII			; chiffre apres un intermediaire
	b.eq	.ignore
	mov	x2,ST_CSIP
	str	x2,[x20,V_STATE]
	ldr	x2,[x20,V_NP]
	cbnz	x2,.has
	mov	x2,1
	str	x2,[x20,V_NP]
	str	wzr,[x20,V_PAR]
.has:	cmp	w19,';'
	b.eq	.sep
	cmp	w19,':'				; sous-parametres (38:2:r:g:b) : comme ;
	b.eq	.sep
	add	x3,x20,V_PAR			; chiffre
	sub	x4,x2,1
	ldr	w5,[x3,x4,lsl 2]
	mov	w6,10
	mul	w5,w5,w6
	sub	w7,w19,'0'
	add	w5,w5,w7
	mov	w6,0xFFFF
	cmp	w5,w6
	csel	w5,w6,w5,hi
	str	w5,[x3,x4,lsl 2]
	b	.out
.sep:	cmp	x2,VT_NPAR
	b.hs	.out
	add	x3,x20,V_PAR
	str	wzr,[x3,x2,lsl 2]
	add	x2,x2,1
	str	x2,[x20,V_NP]
	b	.out
.prefix:
	cmp	x1,ST_CSI
	b.ne	.ignore
	str	x19,[x20,V_PREFIX]
	mov	x2,ST_CSIP
	str	x2,[x20,V_STATE]
	b	.out
.inter:	str	x19,[x20,V_INTER]
	mov	x2,ST_CSII
	str	x2,[x20,V_STATE]
	b	.out
.ignore:
	mov	x2,ST_CSIX
	str	x2,[x20,V_STATE]
	b	.out
.final:	str	xzr,[x20,V_STATE]
	cmp	x1,ST_CSIX
	b.eq	.out
	mov	w0,w19
	bl	vt_csi
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero, x1 = valeur par defaut -> x0 = parametre CSI (0 ou absent :
; la valeur par defaut). Feuille.
vt_par:
	adr	x9,vt
	ldr	x2,[x9,V_NP]
	cmp	x0,x2
	b.hs	.def
	add	x2,x9,V_PAR
	ldr	w0,[x2,x0,lsl 2]
	cbz	w0,.def
	ret
.def:	mov	x0,x1
	ret

; w0 = final : execute la sequence CSI lue
vt_csi:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	adr	x20,vt
	ldr	x1,[x20,V_PREFIX]
	cmp	x1,'?'
	b.eq	.dec
	cbnz	x1,.out				; CSI > c, CSI > 4 ; 1 m... : avales
	ldr	x1,[x20,V_INTER]
	cbz	x1,.table
	cmp	x1,'!'				; CSI ! p : DECSTR ; autres (CSI SP q...) : avales
	b.ne	.out
	cmp	w19,'p'
	b.ne	.out
	bl	vt_soft_reset
	b	.out
.dec:	cmp	w19,'h'
	b.eq	.set
	cmp	w19,'l'
	b.ne	.out
	mov	x0,0
	bl	vt_decset
	b	.out
.set:	mov	x0,1
	bl	vt_decset
	b	.out
.table:	adr	x2,csi_tab
.t:	ldp	x3,x4,[x2],16
	cbz	x3,.out
	cmp	x3,x19
	b.ne	.t
	blr	x4
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = 1 : CSI ? n h, 0 : CSI ? n l, pour chaque parametre
vt_decset:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	adr	x20,vt
	mov	x19,0
.l:	ldr	x1,[x20,V_NP]
	cmp	x19,x1
	b.hs	.out
	add	x2,x20,V_PAR
	ldr	w22,[x2,x19,lsl 2]
	add	x19,x19,1
	mov	x3,VM_APPCK
	cmp	w22,1
	b.eq	.mode
	mov	x3,VM_WRAP
	cmp	w22,7
	b.eq	.mode
	mov	x3,VM_CURSOR
	cmp	w22,25
	b.eq	.mode
	mov	x3,VM_PASTE
	cmp	w22,2004
	b.eq	.mode
	cmp	w22,47
	b.eq	.alt
	cmp	w22,1047
	b.eq	.alt
	cmp	w22,1048
	b.eq	.sc
	cmp	w22,1049
	b.ne	.l
	cbz	x21,.l1049
	mov	x0,V_SC2			; ?1049h : sauve, ecran alternatif vide
	bl	vt_save
	mov	x0,1
	bl	vt_alt_enter
	b	.l
.l1049:	bl	vt_alt_leave			; ?1049l : ecran principal, curseur rendu
	mov	x0,V_SC2
	bl	vt_restore
	b	.l
.alt:	cbz	x21,.altl
	mov	x0,0
	bl	vt_alt_enter
	b	.l
.altl:	cmp	w22,1047			; ?1047l : l'alternatif est efface
	b.ne	.leave
	ldr	x0,[x20,V_ALT]
	cbz	x0,.leave
	mov	x0,0
	ldr	x1,[x20,V_ROWS]
	bl	vt_clear_rows
.leave:	bl	vt_alt_leave
	b	.l
.sc:	mov	x0,V_SC
	cbz	x21,.scr
	bl	vt_save
	b	.l
.scr:	bl	vt_restore
	b	.l
.mode:	ldr	x4,[x20,V_MODES]
	orr	x5,x4,x3
	bic	x4,x4,x3
	cmp	x21,0
	csel	x4,x5,x4,ne
	str	x4,[x20,V_MODES]
	b	.l
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- sequences CSI (table csi_tab) ------------------------------------------

csi_cuu:
	prologue
	mov	x0,0
	mov	x1,1
	bl	vt_par
	adr	x9,vt
	ldr	x1,[x9,V_CY]
	ldr	x2,[x9,V_TOP]			; borne : haut de la zone si dedans
	cmp	x1,x2
	csel	x2,xzr,x2,lt
	sub	x1,x1,x0
	cmp	x1,x2
	csel	x1,x2,x1,lt
	ldr	x0,[x9,V_CX]
	bl	vt_goto
	epilogue

csi_cud:
	prologue
	mov	x0,0
	mov	x1,1
	bl	vt_par
	adr	x9,vt
	ldr	x1,[x9,V_CY]
	ldr	x2,[x9,V_BOT]			; borne : bas de la zone si dedans
	ldr	x3,[x9,V_ROWS]
	sub	x3,x3,1
	cmp	x1,x2
	csel	x2,x3,x2,gt
	add	x1,x1,x0
	cmp	x1,x2
	csel	x1,x2,x1,gt
	ldr	x0,[x9,V_CX]
	bl	vt_goto
	epilogue

csi_cuf:
	prologue
	mov	x0,0
	mov	x1,1
	bl	vt_par
	adr	x9,vt
	ldr	x1,[x9,V_CX]
	add	x0,x1,x0
	ldr	x1,[x9,V_CY]
	bl	vt_goto
	epilogue

csi_cub:
	prologue
	mov	x0,0
	mov	x1,1
	bl	vt_par
	adr	x9,vt
	ldr	x1,[x9,V_CX]
	sub	x0,x1,x0
	ldr	x1,[x9,V_CY]
	bl	vt_goto
	epilogue

csi_cnl:
	prologue
	bl	csi_cud
	adr	x9,vt
	str	xzr,[x9,V_CX]
	epilogue

csi_cpl:
	prologue
	bl	csi_cuu
	adr	x9,vt
	str	xzr,[x9,V_CX]
	epilogue

csi_cha:
	prologue
	mov	x0,0
	mov	x1,1
	bl	vt_par
	sub	x0,x0,1
	adr	x9,vt
	ldr	x1,[x9,V_CY]
	bl	vt_goto
	epilogue

csi_vpa:
	prologue
	mov	x0,0
	mov	x1,1
	bl	vt_par
	sub	x1,x0,1
	adr	x9,vt
	ldr	x0,[x9,V_CX]
	bl	vt_goto
	epilogue

csi_cup:
	prologue
	str	x19,[sp,-16]!
	mov	x0,0
	mov	x1,1
	bl	vt_par
	sub	x19,x0,1
	mov	x0,1
	mov	x1,1
	bl	vt_par
	sub	x0,x0,1
	mov	x1,x19
	bl	vt_goto
	ldr	x19,[sp],16
	epilogue

csi_ed:
	prologue
	adr	x9,vt
	ldr	x0,[x9,V_NP]
	cbz	x0,.go
	ldr	w0,[x9,V_PAR]
.go:	bl	vt_erase
	epilogue

csi_el:
	prologue
	adr	x9,vt
	str	xzr,[x9,V_WRAP]
	ldr	x3,[x9,V_NP]
	mov	x4,0
	cbz	x3,.m
	ldr	w4,[x9,V_PAR]
.m:	ldr	x0,[x9,V_CY]
	ldr	x1,[x9,V_CX]
	mov	x2,VT_MAXCOLS
	cbz	x4,.go				; 0 : du curseur a la fin
	add	x2,x1,1				; 1 : du debut au curseur
	mov	x1,0
	cmp	x4,1
	b.eq	.go
	mov	x2,VT_MAXCOLS			; 2 : toute la ligne
	cmp	x4,2
	b.ne	.out
.go:	bl	vt_clear_span
.out:	epilogue

; IL / DL : x0 = 1 insere, 0 supprime (curseur dans la zone)
vt_ildl:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,0
	mov	x1,1
	bl	vt_par
	mov	x2,x0
	adr	x20,vt
	ldr	x0,[x20,V_CY]
	ldr	x1,[x20,V_TOP]
	cmp	x0,x1
	b.lo	.out
	ldr	x1,[x20,V_BOT]
	cmp	x0,x1
	b.hi	.out
	cbz	x19,.dl
	bl	vt_down
	b	.cr
.dl:	mov	x3,0
	bl	vt_up
.cr:	str	xzr,[x20,V_CX]
	str	xzr,[x20,V_WRAP]
.out:	ldp	x19,x20,[sp],16
	epilogue

csi_il:
	mov	x0,1
	b	vt_ildl

csi_dl:
	mov	x0,0
	b	vt_ildl

; ICH / DCH / ECH : x0 = 0 insere, 1 supprime, 2 efface n cellules au curseur
vt_chars:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x0,0
	mov	x1,1
	bl	vt_par
	adr	x20,vt
	str	xzr,[x20,V_WRAP]
	ldr	x1,[x20,V_CX]
	ldr	x2,[x20,V_COLS]
	sub	x21,x2,x1			; cellules du curseur a la fin
	cmp	x0,x21
	csel	x22,x21,x0,hi			; n borne
	cmp	x19,2
	b.eq	.ech
	ldr	x0,[x20,V_CY]
	bl	vt_row
	ldr	x1,[x20,V_CX]
	add	x9,x0,x1,lsl 4			; cellule du curseur
	sub	x2,x21,x22
	cbnz	x19,.dch
	add	x0,x9,x22,lsl 4			; ICH : decale a droite
	mov	x1,x9
	str	x9,[sp,-16]!
	bl	cells_move
	ldr	x0,[sp],16
	b	.clr
.dch:	mov	x0,x9				; DCH : decale a gauche
	add	x1,x9,x22,lsl 4
	str	x9,[sp,-16]!
	bl	cells_move
	ldr	x9,[sp],16
	sub	x2,x21,x22
	add	x0,x9,x2,lsl 4
.clr:	mov	x1,x22
	ldr	x2,[x20,V_PEN+8]
	and	x2,x2,0xFFFFFFFF00000000
	bl	cells_fill
	b	.out
.ech:	ldr	x0,[x20,V_CY]
	ldr	x1,[x20,V_CX]
	add	x2,x1,x22
	bl	vt_clear_span
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

csi_ich:
	mov	x0,0
	b	vt_chars

csi_dch:
	mov	x0,1
	b	vt_chars

csi_ech:
	mov	x0,2
	b	vt_chars

csi_su:
	prologue
	mov	x0,0
	mov	x1,1
	bl	vt_par
	mov	x2,x0
	adr	x9,vt
	ldr	x0,[x9,V_TOP]
	ldr	x1,[x9,V_BOT]
	mov	x3,1
	bl	vt_up
	epilogue

csi_sd:
	prologue
	mov	x0,0
	mov	x1,1
	bl	vt_par
	mov	x2,x0
	adr	x9,vt
	ldr	x0,[x9,V_TOP]
	ldr	x1,[x9,V_BOT]
	bl	vt_down
	epilogue

; CSI haut ; bas r : zone de defilement (haut < bas), curseur au coin
csi_stbm:
	prologue
	str	x19,[sp,-16]!
	mov	x0,0
	mov	x1,1
	bl	vt_par
	mov	x19,x0
	adr	x9,vt
	ldr	x1,[x9,V_ROWS]
	mov	x0,1
	bl	vt_par
	adr	x9,vt
	ldr	x1,[x9,V_ROWS]
	cmp	x0,x1
	csel	x0,x1,x0,hi
	cmp	x19,x0
	b.hs	.out
	sub	x19,x19,1
	sub	x0,x0,1
	str	x19,[x9,V_TOP]
	str	x0,[x9,V_BOT]
	mov	x0,0
	mov	x1,0
	bl	vt_goto
.out:	ldr	x19,[sp],16
	epilogue

csi_scp:
	mov	x0,V_SC
	b	vt_save

csi_rcp:
	mov	x0,V_SC
	b	vt_restore

; CSI n b : le dernier caractere n fois
csi_rep:
	prologue
	str	x19,[sp,-16]!
	mov	x0,0
	mov	x1,1
	bl	vt_par
	mov	x19,x0
	adr	x9,vt
	ldr	x1,[x9,V_ROWS]
	ldr	x2,[x9,V_COLS]
	mul	x1,x1,x2
	cmp	x19,x1
	csel	x19,x1,x19,hi
	ldr	x0,[x9,V_LASTCP]
	cbz	x0,.out
.l:	cbz	x19,.out
	adr	x9,vt
	ldr	w0,[x9,V_LASTCP]
	bl	vt_print
	sub	x19,x19,1
	b	.l
.out:	ldr	x19,[sp],16
	epilogue

; CSI n I : n taquets en avant
csi_cht:
	prologue
	str	x19,[sp,-16]!
	mov	x0,0
	mov	x1,1
	bl	vt_par
	mov	x19,x0
	cmp	x19,VT_MAXCOLS
	b.ls	.l
	mov	x19,VT_MAXCOLS
.l:	cbz	x19,.out
	mov	w0,9
	bl	vt_c0
	sub	x19,x19,1
	b	.l
.out:	ldr	x19,[sp],16
	epilogue

; CSI n Z : n taquets en arriere
csi_cbt:
	prologue
	mov	x0,0
	mov	x1,1
	bl	vt_par
	adr	x9,vt
	ldr	x1,[x9,V_CX]
.l:	cbz	x0,.st
	cbz	x1,.st
	sub	x1,x1,1
	and	x1,x1,not 7
	sub	x0,x0,1
	b	.l
.st:	str	x1,[x9,V_CX]
	str	xzr,[x9,V_WRAP]
	epilogue

; CSI ... m : attributs et couleurs
csi_sgr:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x20,vt
	ldr	x21,[x20,V_NP]
	mov	w22,0
	mov	w23,0
	mov	w24,0
	cbz	x21,.done			; CSI m : tout par defaut
	ldr	w22,[x20,V_PEN+4]		; attributs
	ldr	w23,[x20,V_PEN+8]		; texte
	ldr	w24,[x20,V_PEN+12]		; fond
	mov	x19,0
.l:	cmp	x19,x21
	b.hs	.done
	add	x9,x20,V_PAR
	ldr	w0,[x9,x19,lsl 2]
	add	x19,x19,1
	cbz	w0,.reset
	mov	w1,VA_BOLD
	cmp	w0,1
	b.eq	.on
	mov	w1,VA_DIM
	cmp	w0,2
	b.eq	.on
	mov	w1,VA_ITALIC
	cmp	w0,3
	b.eq	.on
	mov	w1,VA_UNDER
	cmp	w0,4
	b.eq	.on
	cmp	w0,21				; double souligne : souligne
	b.eq	.on
	mov	w1,VA_BLINK
	cmp	w0,5
	b.eq	.on
	cmp	w0,6
	b.eq	.on
	mov	w1,VA_REV
	cmp	w0,7
	b.eq	.on
	mov	w1,VA_HIDDEN
	cmp	w0,8
	b.eq	.on
	mov	w1,VA_STRIKE
	cmp	w0,9
	b.eq	.on
	mov	w1,VA_BOLD or VA_DIM
	cmp	w0,22
	b.eq	.off
	mov	w1,VA_ITALIC
	cmp	w0,23
	b.eq	.off
	mov	w1,VA_UNDER
	cmp	w0,24
	b.eq	.off
	mov	w1,VA_BLINK
	cmp	w0,25
	b.eq	.off
	mov	w1,VA_REV
	cmp	w0,27
	b.eq	.off
	mov	w1,VA_HIDDEN
	cmp	w0,28
	b.eq	.off
	mov	w1,VA_STRIKE
	cmp	w0,29
	b.eq	.off
	cmp	w0,38
	b.eq	.extfg
	cmp	w0,39
	b.eq	.deffg
	cmp	w0,48
	b.eq	.extbg
	cmp	w0,49
	b.eq	.defbg
	sub	w1,w0,30			; 30-37
	cmp	w1,8
	b.lo	.fg
	sub	w1,w0,90-8			; 90-97 -> 8-15
	sub	w2,w0,90
	cmp	w2,8
	b.lo	.fg
	sub	w1,w0,40			; 40-47
	cmp	w1,8
	b.lo	.bg
	sub	w1,w0,100-8			; 100-107 -> 8-15
	sub	w2,w0,100
	cmp	w2,8
	b.lo	.bg
	b	.l				; (inconnu : ignore)
.on:	orr	w22,w22,w1
	b	.l
.off:	bic	w22,w22,w1
	b	.l
.fg:	mov	w23,VC_IDX
	orr	w23,w23,w1
	b	.l
.bg:	mov	w24,VC_IDX
	orr	w24,w24,w1
	b	.l
.deffg:	mov	w23,0
	b	.l
.defbg:	mov	w24,0
	b	.l
.reset:	mov	w22,0
	mov	w23,0
	mov	w24,0
	b	.l
.extfg:	bl	.ext
	cbz	w0,.l
	mov	w23,w0
	b	.l
.extbg:	bl	.ext
	cbz	w0,.l
	mov	w24,w0
	b	.l
.done:	str	w22,[x20,V_PEN+4]
	str	w23,[x20,V_PEN+8]
	str	w24,[x20,V_PEN+12]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
; (sous-routine : x19 = parametre suivant 38 / 48) -> w0 = couleur (0 :
; forme inconnue) ; x19 avance. 5 ; n : palette ; 2 ; r ; g ; b : 24 bits.
.ext:	mov	w0,0
	cmp	x19,x21
	b.hs	.xr
	add	x9,x20,V_PAR
	ldr	w1,[x9,x19,lsl 2]
	add	x19,x19,1
	cmp	w1,5
	b.eq	.x5
	cmp	w1,2
	b.ne	.xr
	mov	w0,VC_RGB
	mov	x3,3
.xc:	lsl	w0,w0,8				; (VC_RGB monte de 24 bits)
	mov	w1,0
	cmp	x19,x21
	b.hs	.xd
	ldr	w1,[x9,x19,lsl 2]
	and	w1,w1,0xFF
.xd:	add	x19,x19,1
	orr	w0,w0,w1
	subs	x3,x3,1
	b.ne	.xc
	mov	w1,VC_RGB			; VC_RGB << 24 sorti du mot : remis
	orr	w0,w0,w1
	ret
.x5:	mov	w1,0
	cmp	x19,x21
	b.hs	.x5d
	ldr	w1,[x9,x19,lsl 2]
	and	w1,w1,0xFF
.x5d:	add	x19,x19,1
	mov	w0,VC_IDX
	orr	w0,w0,w1
.xr:	ret

; table des finaux CSI sans prefixe ni intermediaire (dans le segment de donnees)
macro csi_table {
csi_tab:
	dd 'A',csi_cuu, 'B',csi_cud, 'C',csi_cuf, 'D',csi_cub
	dd 'E',csi_cnl, 'F',csi_cpl, 'G',csi_cha, '`',csi_cha
	dd 'H',csi_cup, 'f',csi_cup, 'd',csi_vpa, 'e',csi_cud
	dd 'a',csi_cuf, 'J',csi_ed, 'K',csi_el, 'L',csi_il
	dd 'M',csi_dl, '@',csi_ich, 'P',csi_dch, 'X',csi_ech
	dd 'S',csi_su, 'T',csi_sd, 'r',csi_stbm, 'm',csi_sgr
	dd 's',csi_scp, 'u',csi_rcp, 'b',csi_rep, 'I',csi_cht
	dd 'Z',csi_cbt
	dd 0,0
}
