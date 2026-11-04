; calculatrice - calculatrice 4 operations en double precision
; Souris (boutons) et clavier (chiffres, + - * /, Entree, Retour arriere, Echap,
; pave numerique).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W	= 300
WIN_H	= 424
DISP_H	= 96
BTN_W	= 64
BTN_H	= 56
GAP	= 8
ENTRY_MAX = 15

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,ui_win				; variables de l'interface
	bl	ui_init
	mov	x0,WIN_W
	mov	x1,WIN_H
	adr	x2,title
	bl	tr2
	bl	ui_window
	tbnz	x0,63,quit
	bl	calc_reset
	bl	draw_all
.loop:	mov	x0,1000
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_THEME			; autre theme : le fond (peint par
	b.ne	.loop				; UIKit a l'ouverture), puis le reste
	mov	x0,0
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,WIN_H
	jeton	w4,TJ_FACE
	bl	ui_fill
	bl	draw_all
	b	.loop
.mouse:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1
	b.ne	.loop
	bl	button_at			; x0 = code ou 0
	cbz	x0,.loop
	bl	calc_input
	bl	draw_display
	b	.loop
.key:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.loop
	ldr	w2,[x0,EV_CODE]
	ldr	w3,[x0,EV_CHAR]
	bl	key_to_code
	cbz	x0,.loop
	bl	calc_input
	bl	draw_display
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; w2 = code HID, w3 = caractere -> x0 = code d'action (0 : aucun)
key_to_code:
	cmp	w2,KEY_ENTER
	b.eq	.eq
	cmp	w2,0x58				; Entree du pave
	b.eq	.eq
	cmp	w2,KEY_BACKSPACE
	b.eq	.bs
	cmp	w2,KEY_ESC
	b.eq	.clr
	cmp	w2,KEY_DELETE
	b.eq	.clr
	sub	w4,w2,0x59			; pave : 1..9
	cmp	w4,8
	b.hi	.kp0
	add	w0,w4,'1'
	ret
.kp0:	cmp	w2,0x62
	b.ne	.kpd
	mov	w0,'0'
	ret
.kpd:	cmp	w2,0x63
	b.ne	.kpo
	mov	w0,'.'
	ret
.kpo:	cmp	w2,0x54
	b.ne	.kpm
	mov	w0,'/'
	ret
.kpm:	cmp	w2,0x55
	b.ne	.kps
	mov	w0,'*'
	ret
.kps:	cmp	w2,0x56
	b.ne	.kpa
	mov	w0,'-'
	ret
.kpa:	cmp	w2,0x57
	b.ne	.chr
	mov	w0,'+'
	ret
.chr:	sub	w4,w3,'0'			; caracteres
	cmp	w4,9
	b.ls	.same
	cmp	w3,'.'
	b.eq	.same
	cmp	w3,','
	b.eq	.dot
	cmp	w3,'+'
	b.eq	.same
	cmp	w3,'-'
	b.eq	.same
	cmp	w3,'*'
	b.eq	.same
	cmp	w3,'/'
	b.eq	.same
	cmp	w3,'='
	b.eq	.eq
	cmp	w3,'c'
	b.eq	.clr
	cmp	w3,'C'
	b.eq	.clr
	mov	w0,0
	ret
.same:	mov	w0,w3
	ret
.dot:	mov	w0,'.'
	ret
.eq:	mov	w0,'='
	ret
.bs:	mov	w0,'<'
	ret
.clr:	mov	w0,'C'
	ret

; -> x0 = code du bouton sous la souris (ui_ev), 0 sinon
button_at:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,buttons
.b:	ldrb	w0,[x19]
	cmp	w0,0xFF
	b.eq	.none
	bl	btn_rect
	bl	ui_hit
	cbnz	x0,.hit
	add	x19,x19,16
	b	.b
.hit:	ldrh	w0,[x19,4]
	b	.out
.none:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x19 = entree de bouton -> x0,x1,x2,x3 = rectangle
btn_rect:
	ldrb	w4,[x19]			; colonne
	ldrb	w5,[x19,1]			; ligne
	ldrb	w6,[x19,2]			; largeur en colonnes
	mov	x7,BTN_W+GAP
	mov	x0,GAP+4
	madd	x0,x4,x7,x0
	mov	x7,BTN_H+GAP
	mov	x1,DISP_H+GAP+4
	madd	x1,x5,x7,x1
	mov	x7,BTN_W+GAP
	mul	x2,x6,x7
	sub	x2,x2,GAP
	mov	x3,BTN_H
	ret

; ---------------------------------------------------------------------------
;  Logique
; ---------------------------------------------------------------------------
calc_reset:
	adr	x0,saisie
	mov	w1,'0'
	strb	w1,[x0]
	strb	wzr,[x0,1]
	adr	x0,state
	stp	xzr,xzr,[x0]			; acc (double 0), op
	str	xzr,[x0,16]			; nouvelle saisie : non
	mov	x1,1
	str	x1,[x0,24]			; saisie "fraiche" (0 affiche)
	adr	x0,history
	strb	wzr,[x0]
	ret

; w0 = code d'action
calc_input:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	adr	x20,state
	sub	w1,w19,'0'
	cmp	w1,9
	b.ls	.digit
	cmp	w19,'.'
	b.eq	.digit
	cmp	w19,'C'
	b.eq	.clear
	cmp	w19,'<'
	b.eq	.back
	cmp	w19,'n'
	b.eq	.neg
	cmp	w19,'='
	b.eq	.equal
	; operateur
	ldr	x1,[x20,8]
	ldr	x2,[x20,24]			; saisie fraiche -> changement d'operateur seul
	cbz	x1,.first
	cbnz	x2,.setop
	bl	apply
	cbz	x0,.out
	b	.setop
.first:	bl	parse_entry			; acc = saisie
	str	d0,[x20]
.setop:	str	x19,[x20,8]
	str	xzr,[x20,16]			; plus "apres ="
	mov	x1,1
	str	x1,[x20,24]
	ldr	d0,[x20]
	adr	x0,saisie
	bl	fmt_double
	bl	make_history
	b	.out
.digit:	ldr	x1,[x20,24]
	cbz	x1,.append
	adr	x0,saisie			; nouvelle saisie
	strb	wzr,[x0]
	str	xzr,[x20,24]
	ldr	x1,[x20,16]			; apres "=" : on repart de zero
	cbz	x1,.append
	str	xzr,[x20,16]
	str	xzr,[x20,8]
	adr	x0,history
	strb	wzr,[x0]
.append:
	adr	x0,saisie
	bl	str_len
	cmp	x0,ENTRY_MAX
	b.hs	.out
	adr	x1,saisie
	cmp	w19,'.'
	b.ne	.ch
	mov	x2,0				; un seul point
.dp:	ldrb	w3,[x1,x2]
	cbz	w3,.dp_ok
	cmp	w3,'.'
	b.eq	.out
	add	x2,x2,1
	b	.dp
.dp_ok:	cbnz	x0,.ch
	mov	w3,'0'				; ".5" -> "0.5"
	strb	w3,[x1],1
	add	x0,x0,0
	strb	w19,[x1]
	strb	wzr,[x1,1]
	b	.out
.ch:	cmp	x0,1				; remplacer un "0" seul
	b.ne	.add
	ldrb	w3,[x1]
	cmp	w3,'0'
	b.ne	.add
	cmp	w19,'.'
	b.eq	.add
	mov	x0,0
.add:	strb	w19,[x1,x0]
	add	x0,x0,1
	strb	wzr,[x1,x0]
	b	.out
.back:	ldr	x1,[x20,24]
	cbnz	x1,.out
	adr	x0,saisie
	bl	str_len
	cmp	x0,1
	b.ls	.zero
	adr	x1,saisie
	sub	x0,x0,1
	strb	wzr,[x1,x0]
	cmp	x0,1				; "-" seul -> 0
	b.ne	.out
	ldrb	w2,[x1]
	cmp	w2,'-'
	b.ne	.out
.zero:	adr	x1,saisie
	mov	w2,'0'
	strb	w2,[x1]
	strb	wzr,[x1,1]
	b	.out
.neg:	adr	x0,saisie			; changer le signe de la saisie
	ldrb	w1,[x0]
	cmp	w1,'-'
	b.eq	.unneg
	bl	str_len
	adr	x1,saisie
	cmp	x0,1
	b.ne	.shift
	ldrb	w2,[x1]
	cmp	w2,'0'
	b.eq	.out
.shift:	add	x2,x0,1				; decaler d'un cran (avec le 0 final)
.sh:	ldrb	w3,[x1,x0]
	strb	w3,[x1,x2]
	sub	x2,x2,1
	subs	x0,x0,1
	b.pl	.sh
	mov	w3,'-'
	strb	w3,[x1]
	str	xzr,[x20,24]
	b	.out
.unneg:	mov	x1,x0
.us:	ldrb	w2,[x1,1]
	strb	w2,[x1],1
	cbnz	w2,.us
	b	.out
.equal:	ldr	x1,[x20,8]
	cbz	x1,.out
	bl	make_history_eq
	bl	apply
	cbz	x0,.out
	str	xzr,[x20,8]
	mov	x1,1
	str	x1,[x20,16]
	str	x1,[x20,24]
	b	.out
.clear:	bl	calc_reset
.out:	ldp	x19,x20,[sp],16
	epilogue

; acc = acc op saisie ; resultat formate dans entry -> x0 = 0 si erreur
apply:
	prologue
	str	x19,[sp,-16]!
	adr	x19,state
	bl	parse_entry			; d0 = saisie
	fmov	d1,d0
	ldr	d0,[x19]
	ldr	x1,[x19,8]
	cmp	x1,'+'
	b.ne	.s
	fadd	d0,d0,d1
	b	.r
.s:	cmp	x1,'-'
	b.ne	.m
	fsub	d0,d0,d1
	b	.r
.m:	cmp	x1,'*'
	b.ne	.d
	fmul	d0,d0,d1
	b	.r
.d:	fmov	d2,xzr
	fcmp	d1,d2
	b.eq	.err
	fdiv	d0,d0,d1
.r:	str	d0,[x19]
	adr	x0,saisie
	bl	fmt_double
	cbz	x0,.err
	mov	x0,1
	b	.out
.err:	bl	calc_reset
	adr	x0,saisie
	adr	x1,s_error
	bl	tr_cat
	adr	x0,history
	strb	wzr,[x0]
	mov	x1,1
	str	x1,[x19,24]
	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = destination, x1 = morceau de texte -> x0 = fin : str_cat traduit. Les espaces autour du
; morceau restent ici (un dictionnaire ne garde pas ceux d'une cle).
tr_cat:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	sub	sp,sp,240
	mov	x19,x0
	mov	x20,x1
.e:	ldrb	w0,[x20]			; espaces de tete
	cmp	w0,' '
	b.ne	.d
	strb	w0,[x19],1
	add	x20,x20,1
	b	.e
.d:	mov	x21,0				; longueur sans ceux de queue
	mov	x2,0
.c:	ldrb	w0,[x20,x2]
	strb	w0,[sp,x2]
	cbz	w0,.f
	add	x2,x2,1
	cmp	w0,' '
	csel	x21,x21,x2,eq
	cmp	x2,239
	b.lo	.c
.f:	strb	wzr,[sp,x21]
	add	x20,x20,x21
	mov	x0,sp
	bl	ui_tr
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
.q:	ldrb	w1,[x20],1
	strb	w1,[x0],1
	cbnz	w1,.q
	sub	x0,x0,1
	add	sp,sp,240
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> sa traduction ; x1 a x7 gardes
tr0:
	prologue
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	stp	x5,x6,[sp,-16]!
	str	x7,[sp,-16]!
	bl	ui_tr
	ldr	x7,[sp],16
	ldp	x5,x6,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	epilogue

; x2 = texte -> sa traduction ; les autres registres gardes
tr2:
	prologue
	str	x0,[sp,-16]!
	mov	x0,x2
	bl	tr0
	mov	x2,x0
	ldr	x0,[sp],16
	epilogue

; historique : "acc op"
make_history:
	prologue
	adr	x0,history
	adr	x1,saisie
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	adr	x1,state
	ldr	x1,[x1,8]
	bl	op_symbol
	bl	str_cat
	epilogue

; historique : "acc op saisie ="
make_history_eq:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	adr	x0,state
	ldr	d0,[x0]
	mov	x0,sp
	bl	fmt_double
	adr	x0,history
	mov	x1,sp
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	mov	x19,x0
	adr	x1,state
	ldr	x1,[x1,8]
	bl	op_symbol
	mov	x0,x19
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	adr	x1,saisie
	bl	str_cat
	adr	x1,s_eq
	bl	str_cat
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; x1 = code d'operateur -> x1 = symbole UTF-8
op_symbol:
	cmp	x1,'+'
	b.ne	.1
	adr	x1,s_plus
	ret
.1:	cmp	x1,'-'
	b.ne	.2
	adr	x1,s_minus
	ret
.2:	cmp	x1,'*'
	b.ne	.3
	adr	x1,s_times
	ret
.3:	adr	x1,s_div
	ret

; entry -> d0
parse_entry:
	adr	x0,saisie
	fmov	d0,xzr
	fmov	d3,10.0
	mov	x5,0				; signe
	ldrb	w1,[x0]
	cmp	w1,'-'
	b.ne	.int
	mov	x5,1
	add	x0,x0,1
.int:	ldrb	w1,[x0],1
	cbz	w1,.sign
	cmp	w1,'.'
	b.eq	.frac
	sub	w1,w1,'0'
	cmp	w1,9
	b.hi	.sign
	scvtf	d1,w1
	fmul	d0,d0,d3
	fadd	d0,d0,d1
	b	.int
.frac:	fmov	d2,1.0
.f:	ldrb	w1,[x0],1
	cbz	w1,.sign
	sub	w1,w1,'0'
	cmp	w1,9
	b.hi	.sign
	fdiv	d2,d2,d3
	scvtf	d1,w1
	fmul	d1,d1,d2
	fadd	d0,d0,d1
	b	.f
.sign:	cbz	x5,.ret
	fneg	d0,d0
.ret:	ret

; d0 = valeur, x0 = tampon (>= 32 o) -> x0 = 1 si ok, 0 si non representable
; 12 chiffres significatifs, zeros inutiles retires.
fmt_double:
	fcmp	d0,d0
	b.vs	.bad				; NaN
	mov	x9,x0
	fmov	d1,xzr
	fcmp	d0,d1
	b.ge	.pos
	mov	w2,'-'
	strb	w2,[x9],1
	fneg	d0,d0
.pos:	movl	x2,0x430C6BF526340000		; 1e15
	fmov	d1,x2
	fcmp	d0,d1
	b.ge	.bad
	fcvtzu	x2,d0				; chiffres de la partie entiere
	mov	x3,1
	mov	x4,10
.cnt:	cmp	x2,x4
	b.lo	.cnted
	udiv	x2,x2,x4
	add	x3,x3,1
	b	.cnt
.cnted:	mov	x5,12				; decimales = 12 - chiffres (0..9)
	subs	x5,x5,x3
	csel	x5,xzr,x5,lt
	cmp	x5,11
	mov	x6,11
	csel	x5,x6,x5,gt
	fmov	d2,1.0				; 10^decimales
	fmov	d3,10.0
	mov	x6,x5
.p:	cbz	x6,.pd
	fmul	d2,d2,d3
	sub	x6,x6,1
	b	.p
.pd:	fmul	d0,d0,d2
	frinta	d0,d0
	fcvtzu	x2,d0
	; chiffres a l'envers dans une pile locale
	sub	sp,sp,32
	mov	x6,0
.dg:	udiv	x7,x2,x4
	msub	x8,x7,x4,x2
	add	w8,w8,'0'
	strb	w8,[sp,x6]
	add	x6,x6,1
	mov	x2,x7
	cbnz	x2,.dg
.padz:	cmp	x6,x5				; au moins decimales+1 chiffres
	b.hi	.emit
	mov	w8,'0'
	strb	w8,[sp,x6]
	add	x6,x6,1
	b	.padz
.emit:	sub	x6,x6,1
	ldrb	w8,[sp,x6]
	strb	w8,[x9],1
	cbz	x6,.edone
	cmp	x6,x5
	b.ne	.emit
	mov	w8,'.'
	strb	w8,[x9],1
	b	.emit
.edone:	add	sp,sp,32
	cbz	x5,.term
.strip:	ldurb	w8,[x9,-1]			; retirer les zeros et le point inutiles
	cmp	w8,'0'
	b.ne	.dot
	sub	x9,x9,1
	b	.strip
.dot:	cmp	w8,'.'
	b.ne	.term
	sub	x9,x9,1
.term:	strb	wzr,[x9]
	ldrb	w8,[x0]				; "-0" -> "0"
	cmp	w8,'-'
	b.ne	.ok
	ldrb	w8,[x0,1]
	cmp	w8,'0'
	b.ne	.ok
	ldrb	w8,[x0,2]
	cbnz	w8,.ok
	strb	w8,[x0,1]
	mov	w8,'0'
	strb	w8,[x0]
.ok:	mov	x0,1
	ret
.bad:	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------
draw_all:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	draw_display
	adr	x19,buttons
.b:	ldrb	w0,[x19]
	cmp	w0,0xFF
	b.eq	.done
	bl	btn_rect
	ldr	x4,[x19,8]
	ldrb	w5,[x19,3]
	bl	ui_button
	add	x19,x19,16
	b	.b
.done:	bl	ui_update_all
	ldp	x19,x20,[sp],16
	epilogue

draw_display:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,GAP+4
	mov	x1,GAP+4
	mov	x2,WIN_W-2*GAP-8
	mov	x3,DISP_H-GAP
	jeton	w4,TJ_FACE
	bl	ui_fill
	adr	x0,history			; ligne d'historique, alignee a droite
	mov	x1,13
	adr	x2,ui_font
	bl	ui_text_width
	mov	x1,WIN_W-GAP-14
	sub	x1,x1,x0
	adr	x0,history
	mov	x2,36
	jeton	w3,TJ_TEXTE2
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x19,34				; taille du resultat : reduite si long
	adr	x0,saisie
	bl	str_len
	cmp	x0,11
	b.lo	.big
	mov	x19,24
.big:	adr	x0,saisie
	mov	x1,x19
	adr	x2,ui_font
	bl	ui_text_width
	mov	x1,WIN_W-GAP-14
	sub	x1,x1,x0
	adr	x0,saisie
	mov	x2,84
	jeton	w3,TJ_TEXTE1
	mov	x4,x19
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,0
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,DISP_H+GAP
	bl	ui_update
	ldp	x19,x20,[sp],16
	epilogue


segment readable writeable
kit_data

title	db 'Calculatrice',0
s_error	db 'Erreur',0
s_eq	db ' =',0
s_plus	db '+',0
s_minus	db 0xE2,0x88,0x92,0
s_times	db 0xC3,0x97,0
s_div	db 0xC3,0xB7,0
l_c	db 'C',0
l_bs	db 0xE2,0x86,0x90,0
l_neg	db 0xC2,0xB1,0
l_0	db '0',0
l_1	db '1',0
l_2	db '2',0
l_3	db '3',0
l_4	db '4',0
l_5	db '5',0
l_6	db '6',0
l_7	db '7',0
l_8	db '8',0
l_9	db '9',0
l_dot	db ',',0
l_eq	db '=',0

; boutons : colonne, ligne, largeur, accent, code (16 bits), -, libelle (64 bits)
macro btn col, row, span, acc, code, label {
	db col, row, span, acc
	dh code, 0
	dd label
}
	align 8
buttons:
	btn 0,0,1,0,'C',l_c
	btn 1,0,1,0,'<',l_bs
	btn 2,0,1,0,'n',l_neg
	btn 3,0,1,1,'/',s_div
	btn 0,1,1,0,'7',l_7
	btn 1,1,1,0,'8',l_8
	btn 2,1,1,0,'9',l_9
	btn 3,1,1,1,'*',s_times
	btn 0,2,1,0,'4',l_4
	btn 1,2,1,0,'5',l_5
	btn 2,2,1,0,'6',l_6
	btn 3,2,1,1,'-',s_minus
	btn 0,3,1,0,'1',l_1
	btn 1,3,1,0,'2',l_2
	btn 2,3,1,0,'3',l_3
	btn 3,3,1,1,'+',s_plus
	btn 0,4,2,0,'0',l_0
	btn 2,4,1,0,'.',l_dot
	btn 3,4,1,1,'=',l_eq
	db 0xFF,0,0,0,0,0,0,0
	dd 0
	align 8
state	dd 0, 0, 0, 0			; acc (double), op, apres "=", saisie fraiche
saisie	rb 32
history	rb 96
ui_data
