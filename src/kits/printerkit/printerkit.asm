; ============================================================================
;  kits/printerkit/printerkit.asm - PrinterKit : l'impression -> SD0:/lib/printerkit.so
;  (docs/IMPRESSION.md ; printerkit d'Onyx, user/Kits/printerkit)
;
;  Une application imprime en quatre temps : le dialogue, un travail, ses
;  pages, la fin. Les pages sont ENREGISTREES dans la file de printd
;  (SD0:/var/spool/print/<id>.opj, format plus bas) ; le service en fait un
;  PDF, ou ce que l'imprimante reseau prend. Tout le savoir (imprimantes,
;  papiers, polices, IPP) est chez printd : ici, le fichier des pages, les
;  appels au service et le dialogue.
;
;  Le fichier : « OPJ2 », puis des enregistrements : genre et longueur de la
;  charge (32 bits chacun), la charge ; petit-boutiste, longueurs en milliemes
;  de point :
;    PJR_FONT     police, octets d'un TrueType
;    PJR_FONTREF  police, style a fabriquer, chemin du fichier, 0
;    PJR_PAGE     l, h   ... le dessin de la page ... PJR_ENDPAGE
;    PJR_RECT     x, y, l, h, couleur
;    PJR_GLYPH    police, style, taille, x, base, glyphe, caractere, couleur
;    PJR_TEXT     police, style, taille, x, base, couleur, octets, UTF-8
;    PJR_IMAGE    x, y, l, h, largeur, hauteur, genre, donnees
;    PJR_PATH     couleur, epaisseur (0 : rempli), n, n x (verbe, x, y)
;    PJR_END      nombre de pages
;
;  Un seul travail a la fois par process : son etat est dans les variables du
;  kit (privees au process). Fonctions AAPCS.
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
include '../../kits/uikit/uikit.inc'
include '../../kits/archivekit/archivekit.inc'
include 'printerkit.inc'
include 'printd.inc'

PJR_FONT	= 1
PJR_PAGE	= 2
PJR_RECT	= 3
PJR_GLYPH	= 4
PJR_TEXT	= 5
PJR_IMAGE	= 6
PJR_PATH	= 7
PJR_ENDPAGE	= 8
PJR_END		= 9
PJR_FONTREF	= 10

IMG_DEFLATE	= 0			; genres d'une image : 0xAARRGGBB compresses (zlib)
IMG_JPEG	= 1

V_MOVE		= 0			; verbes d'un trace
V_LINE		= 1
V_CURVE		= 2
V_CLOSE		= 3

PJ_BUF		= 0x10000		; tampon du fichier
PJ_NPTS		= 8192			; points d'un trace
PJ_NFONT	= 32
PF_SIZE		= 24			; une police : cle, style, donnees
PF_KEY		= 0			; (cle de printd, -1 : donnees de l'application)
PF_SYNTH	= 8
PF_DATA		= 16
PT_SIZE		= 12
MSG_TEXT	= 456			; texte par message (mesure)

; le dialogue
PD_DW		= 430
PD_LX		= 18
PD_CX		= 132
PD_CW		= 278
PD_ROW		= 36
PD_NPR		= 12
PD_NMEDIA	= 40
RD_CANCEL	= 0
RD_PRINT	= 1
RD_PRINTER	= 2

macro ldv reg, var {
	adr	x16,var
	ldr	reg,[x16]
}
macro stv reg, var {
	adr	x16,var
	str	reg,[x16]
}

segment readable executable
include 'printerkit.abi'
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'
kit_use '../../kits/archivekit/archivekit.abi'

; AppKit et UIKit ouverts au premier besoin ; garde x0-x7
pk_kits:
	adr	x16,pk_kits_ok
	ldr	x17,[x16]
	cbnz	x17,.ok
	stp	x29,x30,[sp,-16]!
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	stp	x4,x5,[sp,-16]!
	stp	x6,x7,[sp,-16]!
	bl	kits_open
	adr	x16,pk_kits_ok
	mov	x17,1
	str	x17,[x16]
	ldp	x6,x7,[sp],16
	ldp	x4,x5,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	ldp	x29,x30,[sp],16
.ok:	ret

; --- petites aides (feuilles, x0-x7 seulement) ------------------------------

; x0 = destination, x1 = octets
pk_zero:
	cbz	x1,.f
.l:	strb	wzr,[x0],1
	subs	x1,x1,1
	b.ne	.l
.f:	ret

; x0 = destination, x1 = source, x2 = octets
pk_copy:
	cbz	x2,.f
.l:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.l
.f:	ret

; x0 = destination, x1 = taille, x2 = source (0 : vide) : copie coupee, 0 final
pk_scpy:
	cbz	x1,.f
	sub	x1,x1,1
	cbz	x2,.fin
.l:	cbz	x1,.fin
	ldrb	w3,[x2],1
	cbz	w3,.fin
	strb	w3,[x0],1
	sub	x1,x1,1
	b	.l
.fin:	strb	wzr,[x0]
.f:	ret

; x0 = texte -> x0 = longueur
pk_slen:
	mov	x1,x0
.l:	ldrb	w2,[x1],1
	cbnz	w2,.l
	sub	x0,x1,x0
	sub	x0,x0,1
	ret

; x0, x1 = textes -> x0 = 1 si egaux
pk_seq:
.l:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.non
	cbnz	w2,.l
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; --- le service --------------------------------------------------------------

; x0 = message -> x0 = resultat (< 0 : pas de service). printd absent : lance.
pk_call:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	pk_kits
	adr	x0,pk_s_svc
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbz	x0,63,.ok
	adr	x0,pk_s_bin
	mov	x1,0
	bl	f_spawn
	adr	x0,pk_s_svc
	movl	x1,4000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
.ok:	mov	x1,x19
	sys	SYS_SVC_CALL
	tbnz	x0,63,.out
	ldr	x0,[x19,WM_OP]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = op, x1 = a, x2 = texte (0 : aucun) (donnees) -> x0 = resultat ; x1 = sa donnee (mem pk_msg)
pk_simple:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x3,x1
	mov	x4,x2
	adr	x0,pk_msg
	mov	x1,WM_DATA
	bl	pk_zero
	adr	x0,pk_msg
	str	x19,[x0,WM_OP]
	str	x3,[x0,WM_A]
	add	x0,x0,WM_DATA
	mov	x1,512-WM_DATA
	mov	x2,x4
	bl	pk_scpy
	adr	x0,pk_msg
	bl	pk_call
	ldr	x19,[sp],16
	epilogue

; --- le fichier des pages -----------------------------------------------------

; ecrit le tampon -> x0 = 0
pj_flush:
	prologue
	ldv	x2,pj_n
	cbz	x2,.ok
	adr	x1,pj_buf
	ldv	x0,pj_fd
	bl	f_ecrire
	stv	xzr,pj_n
.ok:	mov	x0,0
	epilogue

; x0 = source, x1 = octets
pj_put:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldv	x2,pj_n
	add	x2,x2,x20
	movl	x3,PJ_BUF
	cmp	x2,x3
	b.ls	.copy
	bl	pj_flush
	movl	x3,PJ_BUF
	cmp	x20,x3
	b.lo	.copy
	ldv	x0,pj_fd		; (gros bloc : sans passer par le tampon)
	mov	x1,x19
	mov	x2,x20
	bl	f_ecrire
	b	.out
.copy:	ldv	x4,pj_n
	adr	x0,pj_buf
	add	x0,x0,x4
	mov	x1,x19
	mov	x2,x20
	add	x4,x4,x20
	stv	x4,pj_n
	bl	pk_copy
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = genre, x1 = longueur de la charge
pj_head:
	prologue
	sub	sp,sp,16
	stp	w0,w1,[sp]
	mov	x0,sp
	mov	x1,8
	bl	pj_put
	add	sp,sp,16
	epilogue

; x0 = genre, x1 = charge, x2 = octets
pj_rec:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x1,x2
	bl	pj_head
	mov	x0,x19
	mov	x1,x20
	bl	pj_put
	ldp	x19,x20,[sp],16
	epilogue

; (macro d'entree d'une fonction de dessin : hors page ou sans travail : rien)
macro pj_on_or_ret {
	cbnz	x0,@f
	ret
@@:	adr	x16,pj_on
	ldr	x16,[x16]
	cbnz	x16,@f
	ret
@@:
}

; --- un travail ---------------------------------------------------------------

; x0 = PrintSetup, x1 = titre -> travail (0 : echec)
print_begin:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cbz	x19,.non
	bl	pk_kits
	ldv	x0,pj_open
	cbnz	x0,.non			; (un travail est ouvert)
	adr	x0,pj_s			; le dialogue d'une version plus ancienne : plus court
	mov	x1,PS_BYTES
	bl	pk_zero
	ldr	w2,[x19,PS_SIZE]
	cmp	x2,PS_BYTES
	mov	x3,PS_BYTES
	csel	x2,x2,x3,lo
	adr	x0,pj_s
	mov	x1,x19
	bl	pk_copy
	adr	x0,pj_title
	mov	x1,80
	mov	x2,x20
	bl	pk_scpy
	adr	x0,pj_title
	ldrb	w1,[x0]
	cbnz	w1,.nom
	mov	x1,80
	adr	x2,pk_s_doc
	bl	pk_scpy
.nom:	mov	x0,PD_NEW
	mov	x1,0
	mov	x2,0
	bl	pk_simple
	cmp	x0,0
	b.le	.non
	stv	x0,pj_id
	adr	x0,pk_msg+WM_DATA
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	bl	f_ouvrir
	tbnz	x0,63,.non
	stv	x0,pj_fd
	stv	xzr,pj_n
	stv	xzr,pj_seen
	stv	xzr,pj_on
	stv	xzr,pj_pages
	stv	xzr,pj_nfont
	stv	xzr,pj_npts
	stv	xzr,pj_imgf
	mov	x0,1
	stv	x0,pj_open
	adr	x0,pk_s_magic
	mov	x1,4
	bl	pj_put
	adr	x0,pj_s
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; fin de la page en cours
pj_end_page:
	prologue
	ldv	x0,pj_on
	cbz	x0,.out
	stv	xzr,pj_on
	mov	x0,PJR_ENDPAGE
	mov	x1,0
	bl	pj_head
	ldv	x0,pj_pages
	add	x0,x0,1
	stv	x0,pj_pages
.out:	epilogue

; x0 = travail, x1 = l, x2 = h -> 1, 0 : hors des pages choisies
print_page:
	cbz	x0,.non0
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	bl	pj_end_page
	stv	xzr,pj_npts
	ldv	x3,pj_seen
	add	x3,x3,1
	stv	x3,pj_seen
	adr	x4,pj_s
	ldrsw	x5,[x4,PS_FROM]
	ldrsw	x6,[x4,PS_TO]
	cmp	x5,0
	b.le	.oui
	cmp	x3,x5
	b.lt	.non
	cmp	x6,0
	b.le	.oui
	cmp	x3,x6
	b.gt	.non
.oui:	cmp	x19,0
	b.gt	.w
	ldr	w19,[x4,PS_PAPER_W]
.w:	cmp	x20,0
	b.gt	.h
	ldr	w20,[x4,PS_PAPER_H]
.h:	mov	x0,1
	stv	x0,pj_on
	sub	sp,sp,16
	stp	w19,w20,[sp]
	mov	x0,PJR_PAGE
	mov	x1,sp
	mov	x2,8
	bl	pj_rec
	add	sp,sp,16
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue
.non0:	mov	x0,0
	ret

; x0 = travail, x1..x4 = x, y, l, h, x5 = couleur
print_rect:
	pj_on_or_ret
	cmp	x3,0
	b.le	.r
	cmp	x4,0
	b.le	.r
	prologue
	sub	sp,sp,32
	stp	w1,w2,[sp]
	stp	w3,w4,[sp,8]
	and	x5,x5,0xFFFFFF
	str	w5,[sp,16]
	mov	x0,PJR_RECT
	mov	x1,sp
	mov	x2,20
	bl	pj_rec
	add	sp,sp,32
	epilogue
.r:	ret

; x0 = travail, x1..x4 = x0, y0, x1, y1, x5 = epaisseur, x6 = couleur
print_line:
	pj_on_or_ret
	cmp	x1,x3
	b.ne	.pash
	; (droite et de niveau : un rectangle, nets sur la grille de l'imprimante)
	cmp	x2,x4
	csel	x7,x2,x4,lt		; y le plus haut
	csel	x8,x4,x2,lt
	sub	x4,x8,x7
	mov	x2,x7
	mov	x3,x5
	asr	x7,x5,1
	sub	x1,x1,x7
	mov	x5,x6
	b	print_rect
.pash:	cmp	x2,x4
	b.ne	.biais
	cmp	x1,x3
	csel	x7,x1,x3,lt
	csel	x8,x3,x1,lt
	sub	x3,x8,x7
	mov	x1,x7
	mov	x4,x5
	asr	x7,x5,1
	sub	x2,x2,x7
	mov	x5,x6
	b	print_rect
.biais:	cmp	x5,0
	b.gt	.t
	mov	x5,250
.t:	prologue
	sub	sp,sp,48
	and	x6,x6,0xFFFFFF
	stp	w6,w5,[sp]
	mov	w7,2
	str	w7,[sp,8]
	mov	w7,V_MOVE
	str	w7,[sp,12]
	stp	w1,w2,[sp,16]
	mov	w7,V_LINE
	str	w7,[sp,24]
	stp	w3,w4,[sp,28]
	mov	x0,PJR_PATH
	mov	x1,sp
	mov	x2,36
	bl	pj_rec
	add	sp,sp,48
	epilogue

; x0 = travail, x1..x4 = x, y, l, h, x5 = epaisseur, x6 = couleur
print_frame:
	pj_on_or_ret
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	mov	x22,x4
	mov	x23,x5
	mov	x24,x6
	mov	x3,x21			; haut
	mov	x4,x23
	mov	x5,x24
	bl	print_rect
	adr	x0,pj_s
	mov	x1,x19			; bas
	add	x2,x20,x22
	sub	x2,x2,x23
	mov	x3,x21
	mov	x4,x23
	mov	x5,x24
	bl	print_rect
	adr	x0,pj_s
	mov	x1,x19			; gauche
	mov	x2,x20
	mov	x3,x23
	mov	x4,x22
	mov	x5,x24
	bl	print_rect
	adr	x0,pj_s
	add	x1,x19,x21		; droite
	sub	x1,x1,x23
	mov	x2,x20
	mov	x3,x23
	mov	x4,x22
	mov	x5,x24
	bl	print_rect
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = verbe, x1 = x, x2 = y (x3-x5 touches)
pj_pt:
	ldv	x3,pj_npts
	cmp	x3,PJ_NPTS
	b.hs	.plein
	adr	x4,pj_pts
	mov	x5,PT_SIZE
	madd	x4,x3,x5,x4
	str	w0,[x4]
	stp	w1,w2,[x4,4]
	add	x3,x3,1
	stv	x3,pj_npts
.plein:	ret

print_path_move:
	pj_on_or_ret
	mov	x0,V_MOVE
	b	pj_pt
print_path_line:
	pj_on_or_ret
	mov	x0,V_LINE
	b	pj_pt
print_path_close:
	pj_on_or_ret
	mov	x0,V_CLOSE
	mov	x1,0
	mov	x2,0
	b	pj_pt
print_path_curve:
	pj_on_or_ret
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x3
	mov	x20,x4
	mov	x21,x5
	mov	x22,x6
	mov	x0,V_CURVE
	bl	pj_pt
	mov	x0,V_CURVE
	mov	x1,x19
	mov	x2,x20
	bl	pj_pt
	mov	x0,V_CURVE
	mov	x1,x21
	mov	x2,x22
	bl	pj_pt
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = couleur, x1 = epaisseur (0 : rempli)
pj_path:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldv	x2,pj_npts
	cmp	x2,2
	b.lo	.fin
	mov	x3,PT_SIZE
	mul	x1,x2,x3
	add	x1,x1,12
	mov	x0,PJR_PATH
	bl	pj_head
	sub	sp,sp,16
	and	x19,x19,0xFFFFFF
	stp	w19,w20,[sp]
	ldv	x2,pj_npts
	str	w2,[sp,8]
	mov	x0,sp
	mov	x1,12
	bl	pj_put
	add	sp,sp,16
	ldv	x2,pj_npts
	mov	x3,PT_SIZE
	mul	x1,x2,x3
	adr	x0,pj_pts
	bl	pj_put
.fin:	stv	xzr,pj_npts
	ldp	x19,x20,[sp],16
	epilogue

print_path_fill:
	cbz	x0,.f
	mov	x0,x1
	mov	x1,0
	ldv	x2,pj_on
	cbnz	x2,pj_path
	stv	xzr,pj_npts
.f:	ret

print_path_stroke:
	cbz	x0,.f
	mov	x0,x2
	cmp	x1,0
	b.gt	.w
	mov	x1,250
.w:	ldv	x2,pj_on
	cbnz	x2,pj_path
	stv	xzr,pj_npts
.f:	ret

print_image_mode:
	stv	x1,pj_imgf
	ret

; x0 = travail, x1 = pixels, x2 = largeur, x3 = hauteur, x4..x7 = x, y, l, h
; Plus de pixels que l'imprimante n'en montre (300 au pouce) : moyennes d'abord
; (k x k), puis compresses.
print_image:
	pj_on_or_ret
	cbz	x1,.r
	cmp	x2,0
	b.le	.r
	cmp	x3,0
	b.le	.r
	cmp	x6,0
	b.le	.r
	cmp	x7,0
	b.le	.r
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,48
	mov	x19,x1
	mov	x20,x2			; pw
	mov	x21,x3			; ph
	stp	w4,w5,[sp]		; le debut de la charge
	stp	w6,w7,[sp,8]
	mov	x8,240			; (milliemes de point par pixel a 300 dpi)
	udiv	x6,x6,x8		; pixels voulus
	udiv	x7,x7,x8
	mov	x22,1			; k
.k:	add	x8,x22,1
	udiv	x9,x20,x8
	cmp	x9,x6
	b.lo	.kf
	udiv	x9,x21,x8
	cmp	x9,x7
	b.lo	.kf
	mov	x22,x8
	b	.k
.kf:	udiv	x23,x20,x22		; ow
	udiv	x24,x21,x22		; oh
	stp	w23,w24,[sp,16]
	mov	w8,IMG_DEFLATE
	str	w8,[sp,24]
	mul	x28,x23,x24
	lsl	x28,x28,2			; x28 : octets des pixels
	add	x1,x28,4095
	and	x1,x1,not 4095
	str	x1,[sp,-16]!
	mov	x0,0
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	ldr	x1,[sp],16
	tbnz	x0,63,.fin
	str	x0,[sp,32]			; (la region)
	str	x1,[sp,40]
	mov	x9,x0				; x9 : pixels en sortie
	ldv	x25,pj_imgf
	and	x25,x25,PRINT_IMG_ALPHA
	mov	x26,0			; yy
.y:	cmp	x26,x24
	b.hs	.pack
	mov	x27,0			; xx
.x:	cmp	x27,x23
	b.hs	.yn
	; le bloc k x k
	mul	x8,x26,x22
	mul	x7,x27,x22
	mul	x10,x8,x20		; debut du bloc
	add	x10,x10,x7
	add	x10,x19,x10,lsl 2
	mov	x11,0			; a, r, g, b
	mov	x12,0
	mov	x13,0
	mov	x14,0
	mov	x15,x22			; v
.v:	mov	x7,x22			; u
	mov	x8,x10
.u:	ldr	w0,[x8],4
	mov	x1,255
	cbz	x25,.op
	lsr	w1,w0,24
.op:	add	x11,x11,x1
	ubfx	x2,x0,16,8
	madd	x12,x2,x1,x12
	ubfx	x2,x0,8,8
	madd	x13,x2,x1,x13
	and	x2,x0,255
	madd	x14,x2,x1,x14
	subs	x7,x7,1
	b.ne	.u
	add	x10,x10,x20,lsl 2
	subs	x15,x15,1
	b.ne	.v
	cbz	x11,.vide
	udiv	x12,x12,x11
	udiv	x13,x13,x11
	udiv	x14,x14,x11
.vide:	mul	x1,x22,x22
	udiv	x11,x11,x1
	orr	x0,x14,x13,lsl 8
	orr	x0,x0,x12,lsl 16
	orr	x0,x0,x11,lsl 24
	str	w0,[x9],4
	add	x27,x27,1
	b	.x
.yn:	add	x26,x26,1
	b	.y
.pack:	; (le fichier des pages reste petit : la carte s'ecrit lentement, et une page est surtout blanche)
	ldr	x0,[sp,32]
	mov	x1,x28
	mov	x2,AR_ZLIB
	mov	x3,1
	bl	ar_deflate_new
	cmp	x0,0
	b.le	.rel
	mov	x19,x0
	bl	ar_bytes
	mov	x20,x0
	add	x1,x20,28
	mov	x0,PJR_IMAGE
	bl	pj_head
	mov	x0,sp
	mov	x1,28
	bl	pj_put
	mov	x0,x19
	bl	ar_data
	mov	x1,x20
	bl	pj_put
	mov	x0,x19
	bl	ar_free
.rel:	ldp	x0,x1,[sp,32]
	sys	SYS_VM_UNMAP
.fin:	add	sp,sp,48
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.r:	ret

; x0 = travail, x1 = fichier JPEG, x2 = octets, x3 = largeur | hauteur << 32, x4..x7 = x, y, l, h
print_image_jpeg:
	pj_on_or_ret
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x1
	mov	x20,x2
	stp	w4,w5,[sp]
	stp	w6,w7,[sp,8]
	str	x3,[sp,16]
	mov	w8,IMG_JPEG
	str	w8,[sp,24]
	add	x1,x20,28
	mov	x0,PJR_IMAGE
	bl	pj_head
	mov	x0,sp
	mov	x1,28
	bl	pj_put
	mov	x0,x19
	mov	x1,x20
	bl	pj_put
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; x0 = travail, x1 = famille (0 : DejaVu Sans), x2 = style -> police (-1 : aucune)
print_font:
	cbz	x0,.vide
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	cbnz	x19,.f
	adr	x19,pk_s_sans
.f:	mov	x0,PD_FONT
	and	x1,x2,3
	mov	x2,x19
	bl	pk_simple
	tbnz	x0,63,.non
	mov	x19,x0			; cle
	adr	x20,pk_msg
	ldr	x21,[x20,WM_A]		; style a fabriquer
	ldv	x3,pj_nfont
	mov	x4,0
	adr	x5,pj_fonts
.c:	cmp	x4,x3
	b.hs	.nv
	ldr	x6,[x5,PF_KEY]
	ldr	x7,[x5,PF_SYNTH]
	cmp	x6,x19
	b.ne	.s
	cmp	x7,x21
	b.ne	.s
	mov	x0,x4
	b	.out
.s:	add	x5,x5,PF_SIZE
	add	x4,x4,1
	b	.c
.nv:	cmp	x3,PJ_NFONT
	b.hs	.non
	str	x19,[x5,PF_KEY]
	str	x21,[x5,PF_SYNTH]
	str	xzr,[x5,PF_DATA]
	add	x22,x3,1
	stv	x22,pj_nfont
	sub	x22,x22,1
	add	x0,x20,WM_DATA		; l'enregistrement : police, style, chemin, 0
	bl	pk_slen
	add	x19,x0,1
	add	x1,x19,8
	mov	x0,PJR_FONTREF
	bl	pj_head
	sub	sp,sp,16
	stp	w22,w21,[sp]
	mov	x0,sp
	mov	x1,8
	bl	pj_put
	add	sp,sp,16
	add	x0,x20,WM_DATA
	mov	x1,x19
	bl	pj_put
	mov	x0,x22
	b	.out
.non:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.vide:	mov	x0,-1
	ret

; x0 = travail, x1 = TrueType, x2 = octets -> police (la meme adresse : la meme)
print_font_data:
	cbz	x0,.vide
	cbz	x1,.vide
	cmp	x2,12
	b.lo	.vide
	ldv	x3,pj_nfont
	mov	x4,0
	adr	x5,pj_fonts
.c:	cmp	x4,x3
	b.hs	.nv
	ldr	x6,[x5,PF_DATA]
	cmp	x6,x1
	b.ne	.s
	mov	x0,x4
	ret
.s:	add	x5,x5,PF_SIZE
	add	x4,x4,1
	b	.c
.nv:	cmp	x3,PJ_NFONT
	b.hs	.vide
	mov	x6,-1
	str	x6,[x5,PF_KEY]
	str	xzr,[x5,PF_SYNTH]
	str	x1,[x5,PF_DATA]
	add	x6,x3,1
	stv	x6,pj_nfont
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	add	x1,x2,4
	mov	x0,PJR_FONT
	bl	pj_head
	sub	sp,sp,16
	str	w21,[sp]
	mov	x0,sp
	mov	x1,4
	bl	pj_put
	add	sp,sp,16
	mov	x0,x19
	mov	x1,x20
	bl	pj_put
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.vide:	mov	x0,-1
	ret

; x0 = travail, x1 = police | style << 16, x2 = taille, x3 = x, x4 = base, x5 = glyphe, x6 = caractere, x7 = couleur
print_glyph:
	pj_on_or_ret
	and	x8,x1,0xFFFF
	ldv	x9,pj_nfont
	cmp	x8,x9
	b.hs	.r
	adr	x9,pj_fonts		; style + celui a fabriquer
	mov	x10,PF_SIZE
	madd	x9,x8,x10,x9
	ldr	x9,[x9,PF_SYNTH]
	lsr	x1,x1,16
	orr	x9,x9,x1
	prologue
	sub	sp,sp,32
	stp	w8,w9,[sp]
	stp	w2,w3,[sp,8]
	stp	w4,w5,[sp,16]
	and	x7,x7,0xFFFFFF
	stp	w6,w7,[sp,24]
	mov	x0,PJR_GLYPH
	mov	x1,sp
	mov	x2,32
	bl	pj_rec
	add	sp,sp,32
	epilogue
.r:	ret

; x0 = cle de la police, x1 = taille, x2 = texte -> avance (les longs textes par morceaux)
pk_measure:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0
	tbnz	x19,63,.out
.m:	ldrb	w3,[x21]
	cbz	w3,.out
	adr	x0,pk_msg
	mov	x1,WM_DATA
	bl	pk_zero
	adr	x0,pk_msg
	mov	x1,PD_MEASURE
	str	x1,[x0,WM_OP]
	stp	x19,x20,[x0,WM_A]
	add	x4,x0,WM_DATA
	mov	x5,0
.c:	ldrb	w3,[x21,x5]
	cbz	w3,.fin
	cmp	x5,MSG_TEXT
	b.lo	.p
	and	w6,w3,0xC0		; (pas au milieu d'un caractere)
	cmp	w6,0x80
	b.ne	.fin
.p:	strb	w3,[x4,x5]
	add	x5,x5,1
	b	.c
.fin:	strb	wzr,[x4,x5]
	add	x21,x21,x5
	bl	pk_call
	tbnz	x0,63,.out
	add	x22,x22,x0
	b	.m
.out:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = police -> x0 = sa case (0 : inconnue) (x1-x7 gardes)
pk_font:
	cmp	x1,0
	b.lt	.non
	ldv	x8,pj_nfont
	cmp	x1,x8
	b.hs	.non
	adr	x0,pj_fonts
	mov	x8,PF_SIZE
	madd	x0,x1,x8,x0
	ret
.non:	mov	x0,0
	ret

; x0 = travail, x1 = police, x2 = taille, x3 = x, x4 = base, x5 = UTF-8, x6 = couleur -> avance
print_text:
	cbz	x0,.vide
	cbz	x5,.vide
	cmp	x2,0
	b.le	.vide
	mov	x9,x30
	bl	pk_font
	mov	x30,x9
	cbz	x0,.vide
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x2
	mov	x22,x5
	ldv	x8,pj_on
	cbz	x8,.mes
	sub	sp,sp,32
	ldr	x8,[x19,PF_SYNTH]
	stp	w1,w8,[sp]
	stp	w2,w3,[sp,8]
	and	x6,x6,0xFFFFFF
	stp	w4,w6,[sp,16]
	mov	x0,x22
	bl	pk_slen
	mov	x20,x0
	str	w20,[sp,24]
	add	x1,x20,28
	mov	x0,PJR_TEXT
	bl	pj_head
	mov	x0,sp
	mov	x1,28
	bl	pj_put
	add	sp,sp,32
	mov	x0,x22
	mov	x1,x20
	bl	pj_put
.mes:	ldr	x0,[x19,PF_KEY]
	mov	x1,x21
	mov	x2,x22
	bl	pk_measure
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.vide:	mov	x0,0
	ret

; x0 = travail, x1 = police, x2 = taille, x3 = UTF-8 -> avance
print_text_width:
	cbz	x0,.vide
	cbz	x3,.vide
	mov	x9,x30
	bl	pk_font
	mov	x30,x9
	cbz	x0,.vide
	ldr	x0,[x0,PF_KEY]
	mov	x1,x2
	mov	x2,x3
	b	pk_measure
.vide:	mov	x0,0
	ret

; x0 = travail, x1 = police, x2 = taille, x3 = 3 longs
print_font_metrics:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x3
	mov	x20,x2
	cbz	x0,.def
	bl	pk_font
	cbz	x0,.def
	ldr	x3,[x0,PF_KEY]
	tbnz	x3,63,.def
	adr	x0,pk_msg
	mov	x1,PD_METRICS
	str	x1,[x0,WM_OP]
	stp	x3,x20,[x0,WM_A]
	bl	pk_call
	cmp	x0,1
	b.ne	.def
	adr	x0,pk_msg
	ldp	x1,x2,[x0,WM_A]
	ldr	x3,[x0,WM_C]
	b	.out
.def:	mov	x3,93			; (comme Onyx : sans police)
	mul	x1,x20,x3
	mov	x3,100
	udiv	x1,x1,x3
	mov	x3,24
	mul	x2,x20,x3
	mov	x3,100
	udiv	x2,x2,x3
	mov	x3,120
	mul	x3,x20,x3
	mov	x4,100
	udiv	x3,x3,x4
.out:	cbz	x19,.f
	stp	x1,x2,[x19]
	str	x3,[x19,16]
.f:	ldp	x19,x20,[sp],16
	epilogue

; x0 = travail : rien d'imprime
print_abort:
	cbz	x0,.r
	prologue
	bl	pj_shut
	ldv	x1,pj_id
	mov	x0,PD_ABORT
	mov	x2,0
	bl	pk_simple
	epilogue
.r:	ret

; ferme le fichier
pj_shut:
	prologue
	ldv	x0,pj_open
	cbz	x0,.out
	bl	pj_flush
	ldv	x0,pj_fd
	bl	f_fermer
	stv	xzr,pj_open
	stv	xzr,pj_on
.out:	epilogue

; x0 = travail -> son numero en file (> 0), < 0 : refuse
print_end:
	cbz	x0,.non0
	prologue
	stp	x19,x20,[sp,-16]!
	bl	pj_end_page
	sub	sp,sp,16
	ldv	x0,pj_pages
	str	w0,[sp]
	mov	x1,sp
	mov	x0,PJR_END
	mov	x2,4
	bl	pj_rec
	add	sp,sp,16
	bl	pj_shut
	ldv	x19,pj_pages
	cbz	x19,.vide
	adr	x20,pk_msg
	mov	x0,x20
	mov	x1,512
	bl	pk_zero
	mov	x0,PD_SUBMIT
	str	x0,[x20,WM_OP]
	ldv	x0,pj_id
	str	x0,[x20,WM_A]
	adr	x1,pj_s
	ldr	w0,[x1,PS_COPIES]
	cmp	w0,1
	b.ge	.c
	mov	w0,1
.c:	str	x0,[x20,WM_B]
	ldr	w0,[x1,PS_MONO]
	ldr	w2,[x1,PS_QUALITY]
	orr	x0,x0,x2,lsl 8
	str	x0,[x20,WM_C]
	str	x19,[x20,WM_D]
	add	x0,x20,WM_DATA		; imprimante, titre, papier, sortie
	add	x1,x1,PS_PRINTER
	mov	x2,64
	bl	pk_copy
	add	x0,x20,WM_DATA+64
	adr	x1,pj_title
	mov	x2,80
	bl	pk_copy
	adr	x1,pj_s+PS_MEDIA
	add	x0,x20,WM_DATA+144
	mov	x2,64
	bl	pk_copy
	adr	x1,pj_s+PS_OUTPUT
	add	x0,x20,WM_DATA+208
	mov	x2,256
	bl	pk_copy
	mov	x0,x20
	bl	pk_call
	cmp	x0,1
	b.ne	.vide
	ldv	x0,pj_id
	b	.out
.vide:	ldv	x1,pj_id
	mov	x0,PD_ABORT
	mov	x2,0
	bl	pk_simple
	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue
.non0:	mov	x0,-1
	ret

; --- le reglage ------------------------------------------------------------------

; x0 = PrintSetup, x1 = papier (0 : celui du reglage), x2 = orientation -> 1, 0 : papier inconnu
print_setup_paper:
	cbz	x0,.non
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	adr	x20,pk_msg
	mov	x0,x20
	mov	x1,512
	bl	pk_zero
	mov	x0,PD_SETUP
	str	x0,[x20,WM_OP]
	str	x22,[x20,WM_A]
	ldrsw	x0,[x19,PS_COPIES]
	str	x0,[x20,WM_B]
	ldrsw	x0,[x19,PS_QUALITY]
	str	x0,[x20,WM_C]
	ldrsw	x0,[x19,PS_MONO]
	str	x0,[x20,WM_D]
	add	x0,x20,WM_DATA
	add	x1,x19,PS_PRINTER
	mov	x2,64
	bl	pk_copy
	add	x0,x20,WM_DATA+64
	mov	x1,64
	mov	x2,x21
	cbnz	x2,.m
	add	x2,x19,PS_MEDIA
.m:	bl	pk_scpy
	mov	x0,x20
	bl	pk_call
	tbnz	x0,63,.out0
	mov	x21,x0
	add	x0,x19,PS_PRINTER	; imprimante, papier, 6 mots
	add	x1,x20,WM_DATA
	mov	x2,128+24
	bl	pk_copy
	str	w22,[x19,PS_ORIENTATION]
	ldr	x0,[x20,WM_A]
	str	w0,[x19,PS_COPIES]
	ldr	x0,[x20,WM_B]
	str	w0,[x19,PS_QUALITY]
	ldr	x0,[x20,WM_C]
	str	w0,[x19,PS_MONO]
	mov	x0,x21
	b	.out
.out0:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.non:	mov	x0,0
	ret

; x0 = PrintSetup : l'imprimante par defaut (nom vide), son papier, debout, un exemplaire
print_setup_default:
	cbz	x0,.r
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x1,PS_BYTES
	bl	pk_zero
	mov	w0,PS_BYTES
	str	w0,[x19,PS_SIZE]
	mov	w0,1
	str	w0,[x19,PS_COPIES]
	mov	w0,PRINT_NORMAL
	str	w0,[x19,PS_QUALITY]
	mov	x0,x19
	mov	x1,0
	mov	x2,PRINT_PORTRAIT
	bl	print_setup_paper
	ldr	x19,[sp],16
	epilogue
.r:	ret

; --- les imprimantes et la file ----------------------------------------------------

; x0 = out, x1 = places, x2 = op, x3 = octets d'une entree -> nombre (a rang, reponse 1 : une entree)
pk_list:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,0
.l:	cmp	x23,x20
	b.hs	.fin
	mov	x0,x21
	mov	x1,x23
	mov	x2,0
	bl	pk_simple
	cmp	x0,1
	b.ne	.fin
	mul	x0,x23,x22
	add	x0,x19,x0
	adr	x1,pk_msg+WM_DATA
	mov	x2,x22
	bl	pk_copy
	add	x23,x23,1
	b	.l
.fin:	mov	x0,x23
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

print_printers:
	mov	x2,PD_PRINTER
	mov	x3,PR_BYTES
	b	pk_list

print_jobs:
	mov	x2,PD_JOB
	mov	x3,PJI_BYTES
	b	pk_list

print_printers_find:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,PD_SCAN
	mov	x1,0
	mov	x2,0
	bl	pk_simple
	cmp	x0,0
	b.le	.non
	cmp	x20,x0
	csel	x1,x20,x0,lt
	mov	x0,x19
	mov	x2,PD_FOUND
	mov	x3,PF_BYTES
	bl	pk_list
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = imprimante, x1 = rang, x2 = nom, x3 = taille, x4 = libelle, x5 = taille -> 1, 0
print_printer_media:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x2
	mov	x20,x3
	mov	x21,x4
	mov	x22,x5
	mov	x2,x0
	mov	x0,PD_MEDIA
	bl	pk_simple
	cmp	x0,1
	b.ne	.non
	cbz	x19,.l
	mov	x0,x19
	mov	x1,x20
	adr	x2,pk_msg+WM_DATA
	bl	pk_scpy
.l:	cbz	x21,.oui
	mov	x0,x21
	mov	x1,x22
	adr	x2,pk_msg+WM_DATA+64
	bl	pk_scpy
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = op, x1 = nom, x2 = adresse, x3 = out, x4 = taille -> 1 / 0 (out : texte)
pk_named:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	mov	x22,x4
	mov	x4,x0
	adr	x0,pk_msg
	mov	x1,512
	bl	pk_zero
	adr	x0,pk_msg
	str	x4,[x0,WM_OP]
	add	x0,x0,WM_DATA
	mov	x1,64
	mov	x2,x19
	bl	pk_scpy
	adr	x0,pk_msg+WM_DATA+64
	mov	x1,160
	mov	x2,x20
	bl	pk_scpy
	adr	x0,pk_msg
	bl	pk_call
	cmp	x0,1
	cset	x19,eq
	b.ge	.t
	tbz	x0,63,.t
	cbz	x21,.fin		; (pas de service)
	mov	x0,x21
	mov	x1,x22
	adr	x2,pk_s_nosvc
	bl	pk_scpy
	b	.fin
.t:	cbz	x21,.fin
	mov	x0,x21
	mov	x1,x22
	adr	x2,pk_msg+WM_DATA
	bl	pk_scpy
.fin:	mov	x0,x19
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

print_printer_add:
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,PD_ADD
	b	pk_named
print_printer_remove:
	mov	x1,x0
	mov	x0,PD_REMOVE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	b	pk_named
print_printer_default:
	mov	x1,x0
	mov	x0,PD_DEFAULT
	mov	x2,0
	mov	x3,0
	mov	x4,0
	b	pk_named
print_printer_status:
	mov	x4,x2
	mov	x3,x1
	mov	x1,x0
	mov	x0,PD_REFRESH
	mov	x2,0
	b	pk_named

print_job_cancel:
	mov	x1,x0
	mov	x0,PD_CANCEL
	mov	x2,0
	b	pk_simple

print_jobs_forget:
	mov	x0,PD_FORGET
	mov	x1,0
	mov	x2,0
	b	pk_simple

; --- le dialogue ---------------------------------------------------------------------

; x0 = texte, x1 = y : un libelle dans la colonne de gauche
pd_lab:
	prologue
	mov	x4,x0
	mov	x0,PD_LX
	add	x1,x1,4
	mov	x2,PD_CX-PD_LX-6
	mov	x3,22
	mov	x5,0
	bl	ui_label
	epilogue

; x0 = elements, x1 = y, x2 = valeur -> dropdown
pd_drop:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x2
	mov	x4,x0
	mov	x0,PD_CX
	mov	x2,PD_CW
	mov	x3,26
	bl	ui_dropdown
	mov	x1,x19
	mov	x19,x0
	bl	ui_w_set_value
	mov	x0,x19
	ldr	x19,[sp],16
	epilogue

; x0 = imprimante (PrintPrinter) -> x0 = 1 si reseau
pd_is_ipp:
	prologue
	add	x0,x0,PR_KIND
	adr	x1,pk_s_pdf
	bl	pk_seq
	eor	x0,x0,1
	epilogue

; les papiers de l'imprimante choisie -> pd_nmedia, pd_mname, pd_mlist
pd_media_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	pd_cur
	mov	x19,x0
	mov	x20,0
	adr	x21,pd_mlist
.l:	cmp	x20,PD_NMEDIA
	b.hs	.fin
	adr	x2,pd_mname
	add	x2,x2,x20,lsl 6
	mov	x0,x19
	mov	x1,x20
	mov	x3,64
	adr	x4,pd_lbl
	mov	x5,40
	bl	print_printer_media
	cbz	x0,.fin
	cbz	x20,.pr
	mov	w0,'|'
	strb	w0,[x21],1
.pr:	adr	x0,pd_lbl
.c:	ldrb	w1,[x0],1
	cbz	w1,.cf
	strb	w1,[x21],1
	b	.c
.cf:	add	x20,x20,1
	b	.l
.fin:	strb	wzr,[x21]
	stv	x20,pd_nmedia
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = l'imprimante choisie (PrintPrinter)
pd_cur:
	ldv	x0,pd_st_printer
	mov	x1,PR_BYTES
	adr	x2,pd_pr
	madd	x0,x0,x1,x2
	ret

; (dialogue) x0 = dialogue, x1 = widget, x2 = code
pd_cb:
	cmp	x2,UN_CHANGE
	b.ne	.r
	ldv	x3,pd_wprinter
	cmp	x1,x3
	b.eq	.printer
	ldv	x3,pd_wfrom
	cmp	x1,x3
	b.eq	.range
	ldv	x3,pd_wto
	cmp	x1,x3
	b.eq	.range
.r:	ret
.range:	prologue			; une borne touchee : le choix « de .. a »
	ldv	x0,pd_wrange
	mov	x1,1
	bl	ui_w_set_value
	epilogue
.printer:
	prologue
	stp	x0,xzr,[sp,-16]!
	mov	x0,x1
	bl	ui_w_get_value
	mov	x2,x0
	ldp	x0,x1,[sp],16
	ldv	x3,pd_st_printer
	cmp	x2,x3
	b.eq	.f
	mov	x1,RD_PRINTER
	bl	ui_dialog_close
.f:	epilogue

; ce que l'utilisateur a regle, lu dans les widgets
pd_keep:
	prologue
	ldv	x0,pd_wprinter
	bl	ui_w_get_value
	adr	x1,pd_new_printer
	str	x0,[x1]
	ldv	x0,pd_wrange
	bl	ui_w_get_value
	cbz	x0,.pc
	mov	x0,2
	b	.pm
.pc:	ldv	x0,pd_wcur
	cbz	x0,.pm
	bl	ui_w_get_value
.pm:	stv	x0,pd_st_mode
	ldv	x0,pd_wfrom
	bl	ui_w_get_value
	stv	x0,pd_st_from
	ldv	x0,pd_wto
	bl	ui_w_get_value
	ldv	x1,pd_st_from
	cmp	x0,x1
	csel	x0,x1,x0,lt
	stv	x0,pd_st_to
	ldv	x0,pd_wcopies
	cbz	x0,.co
	bl	ui_w_get_value
	stv	x0,pd_st_copies
.co:	ldv	x0,pd_wcolor
	cbz	x0,.cl
	bl	ui_w_get_value
	stv	x0,pd_st_mono
.cl:	ldv	x0,pd_wqual
	cbz	x0,.ql
	bl	ui_w_get_value
	adr	x1,pd_qval
	ldr	x0,[x1,x0,lsl 3]
	stv	x0,pd_st_quality
.ql:	ldv	x0,pd_wmedia
	cbz	x0,.me
	bl	ui_w_get_value
	ldv	x1,pd_nmedia
	cbz	x1,.me
	adr	x2,pd_mname
	add	x2,x2,x0,lsl 6
	adr	x0,pd_st_media
	mov	x1,64
	bl	pk_scpy
.me:	ldv	x0,pd_wori
	cbz	x0,.fin
	bl	ui_w_get_value
	stv	x0,pd_st_ori
.fin:	epilogue

; construit et montre le dialogue pour l'imprimante choisie -> x0 = RD_*
pd_show:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	bl	pd_cur
	mov	x20,x0				; x20 = imprimante
	bl	pd_is_ipp
	mov	x21,x0				; x21 = reseau
	; le nombre de lignes
	mov	x22,1
	cbz	x21,.n1
	add	x22,x22,1			; copies
	ldr	w0,[x20,PR_COLOR]
	cbz	w0,.n0
	add	x22,x22,1			; couleur
.n0:	ldr	w0,[x20,PR_QUALITY]
	and	w0,w0,0x38
	sub	w1,w0,1
	tst	w0,w1
	b.eq	.n1
	add	x22,x22,1			; qualite : plus d'une
.n1:	adr	x24,pd_info
	ldr	w25,[x24,PDI_CURRENT]
	ldr	w0,[x24,PDI_FLAGS]
	and	x26,x0,PRINT_DLG_OWN_PAPER
	cbnz	x26,.n2
	add	x22,x22,2			; papier, orientation
.n2:	mov	x0,PD_ROW
	mul	x22,x22,x0
	add	x22,x22,44+24+10+54
	add	x22,x22,28*2
	cbz	x25,.n3
	add	x22,x22,28
.n3:	mov	x0,PD_DW
	mov	x1,x22
	adr	x2,pd_title
	bl	ui_dialog_begin
	cbz	x0,.out0
	mov	x19,x0				; x19 = dialogue
	adr	x0,pd_wprinter			; tous les widgets a 0
	mov	x1,8*11
	bl	pk_zero
	mov	x0,PD_LX			; (le haut droit reste aux boutons du cadre)
	mov	x1,12
	mov	x2,PD_DW-PD_LX-80
	mov	x3,24
	adr	x4,pd_title
	mov	x5,ULB_BOLD
	bl	ui_label
	mov	x23,44				; x23 = y
	adr	x0,pk_s_printer
	mov	x1,x23
	bl	pd_lab
	adr	x0,pd_names
	mov	x1,x23
	ldv	x2,pd_st_printer
	bl	pd_drop
	stv	x0,pd_wprinter
	mov	x0,PD_CX			; le modele
	add	x1,x23,30
	mov	x2,PD_CW
	mov	x3,20
	add	x4,x20,PR_MODEL
	mov	x5,ULB_DIM
	bl	ui_label
	add	x23,x23,PD_ROW+24
	; les pages
	adr	x0,pk_s_pages
	mov	x1,x23
	bl	pd_lab
	mov	x0,PD_CX
	mov	x1,x23
	mov	x2,PD_CW
	mov	x3,24
	adr	x4,pk_s_all
	mov	x5,1
	bl	ui_radio
	ldv	x1,pd_st_mode
	cbnz	x1,.ra
	mov	x1,1
	bl	ui_w_set_value
.ra:	add	x23,x23,28
	cbz	x25,.rc
	mov	x0,PD_CX
	mov	x1,x23
	mov	x2,PD_CW
	mov	x3,24
	adr	x4,pk_s_current
	mov	x5,1
	bl	ui_radio
	stv	x0,pd_wcur
	ldv	x1,pd_st_mode
	cmp	x1,1
	b.ne	.rc0
	mov	x1,1
	bl	ui_w_set_value
.rc0:	add	x23,x23,28
.rc:	mov	x0,PD_CX
	mov	x1,x23
	mov	x2,76
	mov	x3,24
	adr	x4,pk_s_from
	mov	x5,1
	bl	ui_radio
	stv	x0,pd_wrange
	ldv	x1,pd_st_mode
	cmp	x1,2
	b.ne	.rf
	mov	x1,1
	bl	ui_w_set_value
.rf:	ldrsw	x6,[x24,PDI_PAGES]		; (la borne : le nombre de pages, s'il est connu)
	movl	x5,9999
	cmp	x6,0
	csel	x5,x6,x5,gt
	stv	x5,pd_most
	mov	x0,PD_CX+78
	sub	x1,x23,1
	mov	x2,84
	mov	x3,26
	mov	x4,1
	ldv	x6,pd_st_from
	bl	ui_numeric
	stv	x0,pd_wfrom
	mov	x0,PD_CX+78+84+6
	add	x1,x23,3
	mov	x2,20
	mov	x3,22
	adr	x4,pk_s_to
	mov	x5,0
	bl	ui_label
	mov	x0,PD_CX+78+84+28
	sub	x1,x23,1
	mov	x2,84
	mov	x3,26
	mov	x4,1
	ldv	x5,pd_most
	ldv	x6,pd_st_to
	bl	ui_numeric
	stv	x0,pd_wto
	add	x23,x23,28+10
	cbz	x21,.medias
	; copies, couleur, qualite (imprimante reseau)
	adr	x0,pk_s_copies
	mov	x1,x23
	bl	pd_lab
	ldr	w5,[x20,PR_COPIES]
	cmp	w5,1
	b.ge	.cm
	mov	w5,1
.cm:	mov	x0,PD_CX
	mov	x1,x23
	mov	x2,84
	mov	x3,26
	mov	x4,1
	ldv	x6,pd_st_copies
	bl	ui_numeric
	stv	x0,pd_wcopies
	add	x23,x23,PD_ROW
	ldr	w0,[x20,PR_COLOR]
	cbz	w0,.ql
	adr	x0,pk_s_colour
	mov	x1,x23
	bl	pd_lab
	adr	x0,pk_s_colours
	mov	x1,x23
	ldv	x2,pd_st_mono
	cmp	x2,0
	cset	x2,ne
	bl	pd_drop
	stv	x0,pd_wcolor
	add	x23,x23,PD_ROW
.ql:	ldr	w0,[x20,PR_QUALITY]		; la liste des qualites
	adr	x1,pd_qlist
	adr	x2,pd_qval
	mov	x3,3
	mov	x4,0
	mov	x6,0				; choisie
.q:	mov	x5,1
	lsl	x5,x5,x3
	tst	x0,x5
	b.eq	.qn
	str	x3,[x2,x4,lsl 3]
	ldv	x5,pd_st_quality
	cmp	x3,x5
	csel	x6,x4,x6,eq
	cbz	x4,.q1
	mov	w5,'|'
	strb	w5,[x1],1
.q1:	adr	x5,pk_qnames
	sub	x7,x3,3
	add	x5,x5,x7,lsl 4
.qc:	ldrb	w7,[x5],1
	cbz	w7,.qf
	strb	w7,[x1],1
	b	.qc
.qf:	add	x4,x4,1
.qn:	add	x3,x3,1
	cmp	x3,5
	b.ls	.q
	strb	wzr,[x1]
	cmp	x4,2
	b.lo	.medias
	stv	x6,pd_tmp
	adr	x0,pk_s_quality
	mov	x1,x23
	bl	pd_lab
	adr	x0,pd_qlist
	mov	x1,x23
	ldv	x2,pd_tmp
	bl	pd_drop
	stv	x0,pd_wqual
	add	x23,x23,PD_ROW
.medias:
	cbnz	x26,.bts
	bl	pd_media_load
	bl	pd_media_sel
	stv	x0,pd_tmp
	adr	x0,pk_s_paper
	mov	x1,x23
	bl	pd_lab
	adr	x0,pd_mlist
	mov	x1,x23
	ldv	x2,pd_tmp
	bl	pd_drop
	stv	x0,pd_wmedia
	add	x23,x23,PD_ROW
	adr	x0,pk_s_orient
	mov	x1,x23
	bl	pd_lab
	adr	x0,pk_s_orients
	mov	x1,x23
	ldv	x2,pd_st_ori
	bl	pd_drop
	stv	x0,pd_wori
.bts:	mov	x0,x19
	mov	x1,0
	adr	x2,pk_s_print
	mov	x3,RD_PRINT
	mov	x4,UB_DEFAULT
	mov	x5,PD_DW-12
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x19
	mov	x1,1
	adr	x2,pk_s_cancel
	mov	x3,RD_CANCEL
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x19
	mov	x1,RD_CANCEL
	bl	ui_dialog_cancel
	mov	x0,x19
	adr	x1,pd_cb
	bl	ui_dialog_run
	mov	x20,x0
	cmp	x20,RD_CANCEL
	b.eq	.end
	bl	pd_keep
.end:	mov	x0,x19
	bl	ui_dialog_end
	mov	x0,x20
	b	.out
.out0:	mov	x0,RD_CANCEL
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = le papier a montrer : celui choisi, sinon celui du document, sinon celui de l'imprimante
pd_media_sel:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x20,pd_nmedia
	mov	x19,0
	adr	x21,pd_mname
.a:	cmp	x19,x20
	b.hs	.b
	add	x0,x21,x19,lsl 6
	adr	x1,pd_st_media
	bl	pk_seq
	cbnz	x0,.oui
	add	x19,x19,1
	b	.a
.b:	bl	pd_cur
	add	x22,x0,PR_MEDIA_DEFAULT
	mov	x19,0
.c:	cmp	x19,x20
	b.hs	.non
	add	x0,x21,x19,lsl 6
	mov	x1,x22
	bl	pk_seq
	cbnz	x0,.oui
	add	x19,x19,1
	b	.c
.non:	mov	x19,0
.oui:	mov	x0,x19
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PrintSetup, x1 = PrintDialogInfo -> 1 : imprimer, 0 : abandon
print_dialog:
	cbz	x0,.non0
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	pk_kits
	adr	x0,pd_info
	mov	x1,PDI_BYTES
	bl	pk_zero
	cbz	x20,.i
	ldr	w2,[x20,PDI_SIZE]
	cmp	x2,PDI_BYTES
	mov	x3,PDI_BYTES
	csel	x2,x2,x3,lo
	adr	x0,pd_info
	mov	x1,x20
	bl	pk_copy
.i:	ldr	w0,[x19,PS_SIZE]
	cbnz	w0,.s
	mov	x0,x19
	bl	print_setup_default
.s:	; les imprimantes
	adr	x0,pd_pr
	mov	x1,PD_NPR
	bl	print_printers
	stv	x0,pd_npr
	cbz	x0,.non
	mov	x21,x0
	adr	x22,pd_names
	mov	x20,0
	stv	xzr,pd_st_printer
.n:	cmp	x20,x21
	b.hs	.nf
	mov	x0,PR_BYTES
	adr	x1,pd_pr
	madd	x8,x20,x0,x1
	cbz	x20,.n1
	mov	w0,'|'
	strb	w0,[x22],1
.n1:	mov	x0,x8
.nc:	ldrb	w1,[x0],1
	cbz	w1,.nd
	strb	w1,[x22],1
	b	.nc
.nd:	mov	x0,x8			; la choisie : celle du reglage, sinon par defaut
	add	x1,x19,PS_PRINTER
	bl	pk_seq
	cbz	x0,.nn
	stv	x20,pd_st_printer
.nn:	add	x20,x20,1
	b	.n
.nf:	strb	wzr,[x22]
	; le reste du reglage
	ldrsw	x0,[x19,PS_FROM]
	cmp	x0,0
	mov	x1,2
	csel	x1,x1,xzr,gt
	stv	x1,pd_st_mode
	mov	x1,1
	csel	x0,x0,x1,gt
	stv	x0,pd_st_from
	adr	x2,pd_info
	ldrsw	x3,[x2,PDI_PAGES]
	cmp	x3,0
	csel	x3,x3,x1,gt
	ldrsw	x0,[x19,PS_TO]
	cmp	x0,0
	csel	x0,x0,x3,gt
	stv	x0,pd_st_to
	ldrsw	x0,[x19,PS_COPIES]
	cmp	x0,0
	csel	x0,x0,x1,gt
	stv	x0,pd_st_copies
	ldrsw	x0,[x19,PS_MONO]
	stv	x0,pd_st_mono
	ldrsw	x0,[x19,PS_QUALITY]
	stv	x0,pd_st_quality
	ldrsw	x0,[x19,PS_ORIENTATION]
	; (document plus large que haut, sans papier choisi : couche)
	stv	x0,pd_st_ori
	adr	x0,pd_st_media
	mov	x1,64
	add	x2,x19,PS_MEDIA
	bl	pk_scpy
	; le titre
	adr	x0,pd_title
	mov	x1,160
	adr	x2,pk_s_print
	bl	pk_scpy
	adr	x2,pd_info
	ldr	x2,[x2,PDI_TITLE]
	cbz	x2,.boucle
	adr	x0,pd_title
	bl	pk_slen
	adr	x1,pd_title
	add	x0,x1,x0
	mov	x1,3
	adr	x2,pk_s_dash
	bl	pk_copy2
	adr	x0,pd_title
	bl	pk_slen
	adr	x1,pd_title
	add	x0,x1,x0
	mov	x1,160-16
	adr	x2,pd_info
	ldr	x2,[x2,PDI_TITLE]
	bl	pk_scpy
.boucle:
	bl	pd_show
	cmp	x0,RD_PRINTER
	b.ne	.fin
	ldv	x0,pd_new_printer
	stv	x0,pd_st_printer
	b	.boucle
.fin:	cmp	x0,RD_PRINT
	b.ne	.non
	ldv	x0,pd_new_printer
	stv	x0,pd_st_printer
	bl	pd_cur
	mov	x20,x0
	bl	pd_is_ipp
	mov	x21,x0
	add	x0,x19,PS_PRINTER
	mov	x1,64
	mov	x2,x20
	bl	pk_scpy
	mov	w1,1
	mov	w2,0
	cbz	x21,.mn
	ldv	x1,pd_st_copies
	ldv	x2,pd_st_mono
.mn:	str	w1,[x19,PS_COPIES]
	str	w2,[x19,PS_MONO]
	ldv	x0,pd_st_quality
	str	w0,[x19,PS_QUALITY]
	ldv	x3,pd_st_mode
	mov	x0,0
	mov	x1,0
	cmp	x3,2
	b.ne	.m1
	ldv	x0,pd_st_from
	ldv	x1,pd_st_to
	b	.m2
.m1:	cmp	x3,1
	b.ne	.m2
	adr	x0,pd_info
	ldr	w0,[x0,PDI_CURRENT]
	mov	x1,x0
.m2:	str	w0,[x19,PS_FROM]
	str	w1,[x19,PS_TO]
	mov	x0,x19
	adr	x1,pd_st_media
	ldv	x2,pd_st_ori
	bl	print_setup_paper
	cbnz	x21,.oui
	; l'imprimante PDF : le fichier a ecrire
	adr	x0,pd_def
	mov	x1,88
	adr	x2,pd_info
	ldr	x2,[x2,PDI_TITLE]
	cbnz	x2,.d
	adr	x2,pk_s_doc
.d:	mov	x3,x2				; (le dernier element du chemin)
.d0:	ldrb	w4,[x3],1
	cbz	w4,.d1
	cmp	w4,'/'
	csel	x2,x3,x2,eq
	cmp	w4,':'
	csel	x2,x3,x2,eq
	b	.d0
.d1:	bl	pk_scpy
	adr	x0,pd_def			; (son extension s'en va)
	bl	pk_slen
	adr	x1,pd_def
.d2:	cbz	x0,.d4
	sub	x0,x0,1
	ldrb	w2,[x1,x0]
	cmp	w2,'.'
	b.ne	.d2
	cbz	x0,.d4
	strb	wzr,[x1,x0]
.d4:	adr	x0,pd_def
	bl	pk_slen
	adr	x1,pd_def
	add	x0,x1,x0
	mov	x1,5
	adr	x2,pk_s_ext
	bl	pk_copy2
	adr	x0,pk_s_docs
	adr	x1,pd_def
	add	x2,x19,PS_OUTPUT
	mov	x3,256
	bl	ui_file_save
	b	.out
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.non0:	mov	x0,0
	ret

; x0 = destination, x1 = octets, x2 = source
pk_copy2:
	mov	x3,x1
	mov	x1,x2
	mov	x2,x3
	b	pk_copy

align 4
pk_s_svc	db 'print',0
pk_s_bin	db 'SD0:/sys/printd',0
pk_s_magic	db 'OPJ2'
pk_s_doc	db 'Document',0
pk_s_sans	db 'DejaVu Sans',0
pk_s_pdf	db 'pdf',0
pk_s_nosvc	db 'le service d',39,'impression ne repond pas',0
pk_s_printer	db 'Imprimante',0
pk_s_pages	db 'Pages',0
pk_s_all	db 'Toutes',0
pk_s_current	db 'Page courante',0
pk_s_from	db 'De',0
pk_s_to		db 'à',0
pk_s_copies	db 'Exemplaires',0
pk_s_colour	db 'Couleur',0
pk_s_colours	db 'Couleur|Noir et blanc',0
pk_s_quality	db 'Qualité',0
pk_s_paper	db 'Papier',0
pk_s_orient	db 'Orientation',0
pk_s_orients	db 'Portrait|Paysage',0
pk_s_print	db 'Imprimer',0
pk_s_cancel	db 'Annuler',0
pk_s_dash	db ' - ',0
pk_s_ext	db '.pdf',0
pk_s_docs	db 'SD0:/home/documents',0
align 16
pk_qnames:	db 'Brouillon',0,0,0,0,0,0,0
		db 'Normale',0,0,0,0,0,0,0,0,0
		db 'Haute',0,0,0,0,0,0,0,0,0,0,0
align 4

segment readable writeable
kit_data
pk_kits_ok	dd 0
pj_open		dd 0
pj_fd		dd 0
pj_id		dd 0
pj_n		dd 0
pj_on		dd 0
pj_seen		dd 0
pj_pages	dd 0
pj_nfont	dd 0
pj_npts		dd 0
pj_imgf		dd 0
pd_npr		dd 0
pd_nmedia	dd 0
pd_most		dd 0
pd_tmp		dd 0
pd_new_printer	dd 0
pd_st_printer	dd 0
pd_st_mode	dd 0
pd_st_from	dd 0
pd_st_to	dd 0
pd_st_copies	dd 0
pd_st_mono	dd 0
pd_st_quality	dd 0
pd_st_ori	dd 0
pd_wprinter	dd 0			; (les widgets : 11 mots, remis a 0 ensemble)
pd_wcur		dd 0
pd_wrange	dd 0
pd_wfrom	dd 0
pd_wto		dd 0
pd_wcopies	dd 0
pd_wcolor	dd 0
pd_wqual	dd 0
pd_wmedia	dd 0
pd_wori		dd 0
pd_wfree	dd 0
pd_qval		rd 4
pd_info		rb PDI_BYTES
pj_s		rb PS_BYTES
pj_title	rb 80
pd_st_media	rb 64
pd_title	rb 160
pd_def		rb 96
pd_lbl		rb 40
pd_qlist	rb 48
pk_msg		rb 512
align 8
pj_fonts	rb PJ_NFONT*PF_SIZE
pd_names	rb PD_NPR*64
pd_mlist	rb PD_NMEDIA*40
pd_mname	rb PD_NMEDIA*64
pd_pr		rb PD_NPR*PR_BYTES
pj_pts		rb PJ_NPTS*PT_SIZE
pj_buf		rb PJ_BUF
