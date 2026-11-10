; vt_keys.inc - le Terminal : touches -> octets pour le programme (xterm)
;
; Les octets rendus par key_encode sont ecrits sur le bout maitre du pty
; (service vterm, docs/VTERM.md) : la discipline de ligne les traite.
;
; Fleches ESC [ A..D (ESC O A..D en mode ?1), Debut / Fin ESC [ H / F,
; Inser / Suppr / Page haut / Page bas ESC [ 2~ 3~ 5~ 6~, F1-F4 ESC O P..S,
; F5-F12 ESC [ 15~ .. 24~ ; avec Maj, Alt, Ctrl : ESC [ 1 ; m X ou
; ESC [ n ; m ~ (m = 1 + Maj + 2 Alt + 4 Ctrl). Echap 0x1B, Tab 9, Maj+Tab
; ESC [ Z, Retour arriere 0x7F, Entree CR, Ctrl+lettre 1..26, Ctrl+Espace 0,
; Ctrl+[ \ ] 27..29 ; texte en UTF-8 ; Alt+touche simple : ESC devant.

K_MOD_CTRL	= 0x11
K_MOD_SHIFT	= 0x22
K_MOD_ALT	= 0x44

; touches speciales : code HID, genre (1 CSI final, 2 CSI n ~, 3 SS3 final), final ou n
macro keys_data {
kk_tab	db 0x52,1,'A',0, 0x51,1,'B',0, 0x4F,1,'C',0, 0x50,1,'D',0
	db 0x4A,1,'H',0, 0x4D,1,'F',0
	db 0x49,2,2,0, 0x4C,2,3,0, 0x4B,2,5,0, 0x4E,2,6,0
	db 0x3A,3,'P',0, 0x3B,3,'Q',0, 0x3C,3,'R',0, 0x3D,3,'S',0
	db 0x3E,2,15,0, 0x3F,2,17,0, 0x40,2,18,0, 0x41,2,19,0
	db 0x42,2,20,0, 0x43,2,21,0, 0x44,2,23,0, 0x45,2,24,0
	db 0,0,0,0
}

; w0 = code HID, w1 = caractere, w2 = modificateurs, x3 = tampon (16 o)
; -> x0 = octets ecrits (0 : rien a envoyer)
key_encode:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	w19,w0
	mov	w20,w1
	mov	w21,w2
	mov	x22,x3
	mov	x23,x3				; ecriture
	mov	w24,1				; parametre de modificateurs
	mov	w9,K_MOD_SHIFT
	tst	w21,w9
	b.eq	@f
	add	w24,w24,1
@@:	mov	w9,K_MOD_ALT
	tst	w21,w9
	b.eq	@f
	add	w24,w24,2
@@:	mov	w9,K_MOD_CTRL
	tst	w21,w9
	b.eq	@f
	add	w24,w24,4
@@:	adr	x9,kk_tab
.t:	ldrb	w0,[x9],4
	cbz	w0,.plain
	cmp	w0,w19
	b.ne	.t
	ldurb	w1,[x9,-3]			; genre
	ldurb	w2,[x9,-2]			; final ou numero
	mov	w0,0x1B
	strb	w0,[x23],1
	cmp	w1,2
	b.eq	.tilde
	cmp	w24,1
	b.ne	.modf
	mov	w0,'O'				; SS3 : F1-F4, fleches en mode ?1
	cmp	w1,3
	b.eq	.fin
	adr	x9,vt
	ldr	x3,[x9,V_MODES]
	tbnz	x3,4,.fin			; (VM_APPCK)
	mov	w0,'['
.fin:	strb	w0,[x23],1
	strb	w2,[x23],1
	b	.done
.modf:	mov	w0,'['				; ESC [ 1 ; m X
	strb	w0,[x23],1
	mov	w0,'1'
	strb	w0,[x23],1
	mov	w0,';'
	strb	w0,[x23],1
	mov	w0,w24
	bl	.dec
	strb	w2,[x23],1
	b	.done
.tilde:	mov	w0,'['				; ESC [ n (; m) ~
	strb	w0,[x23],1
	mov	w0,w2
	bl	.dec
	cmp	w24,1
	b.eq	.t2
	mov	w0,';'
	strb	w0,[x23],1
	mov	w0,w24
	bl	.dec
.t2:	mov	w0,'~'
	strb	w0,[x23],1
	b	.done
.plain:	mov	w0,0x0D
	cmp	w19,KEY_ENTER
	b.eq	.one
	mov	w0,0x1B
	cmp	w19,KEY_ESC
	b.eq	.one
	mov	w0,0x7F
	cmp	w19,KEY_BACKSPACE
	b.eq	.one
	cmp	w19,KEY_TAB
	b.ne	.ctrl
	mov	w0,9
	mov	w9,K_MOD_SHIFT
	tst	w21,w9
	b.eq	.one
	mov	w0,0x1B				; Maj+Tab : ESC [ Z
	strb	w0,[x23],1
	mov	w0,'['
	strb	w0,[x23],1
	mov	w0,'Z'
	strb	w0,[x23],1
	b	.done
.ctrl:	mov	w9,K_MOD_CTRL
	tst	w21,w9
	b.eq	.text
	orr	w0,w20,0x20			; lettre (le caractere, sinon la place HID)
	sub	w0,w0,'a'
	cmp	w0,26
	b.lo	.cl
	sub	w0,w19,4			; HID 0x04..0x1D : a..z
	cmp	w0,26
	b.lo	.cl
	mov	w0,0
	cmp	w20,' '
	b.eq	.one
	cmp	w19,0x2C			; (Espace)
	b.eq	.one
	mov	w0,27
	cmp	w20,'['
	b.eq	.one
	mov	w0,28
	cmp	w20,'\'
	b.eq	.one
	mov	w0,29
	cmp	w20,']'
	b.eq	.one
	b	.text
.cl:	add	w0,w0,1
	b	.one
.text:	cmp	w20,0x20
	b.lo	.done
	mov	w9,K_MOD_ALT
	tst	w21,w9
	b.eq	.utf
	mov	w0,0x1B
	strb	w0,[x23],1
.utf:	mov	w0,w20
	mov	x1,x23
	bl	utf8_put
	add	x23,x23,x0
	b	.done
.one:	mov	w9,K_MOD_ALT			; Alt : ESC devant
	tst	w21,w9
	b.eq	.o1
	mov	w1,0x1B
	strb	w1,[x23],1
.o1:	strb	w0,[x23],1
.done:	sub	x0,x23,x22
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
; (sous-routine) w0 = 0..99 en decimal a x23 ; garde w2
.dec:	cmp	w0,10
	b.lo	.d1
	mov	w1,10
	udiv	w3,w0,w1
	msub	w0,w3,w1,w0
	add	w3,w3,'0'
	strb	w3,[x23],1
.d1:	add	w0,w0,'0'
	strb	w0,[x23],1
	ret
