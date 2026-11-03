; ============================================================================
;  svc/inc/hidpad.inc - manettes USB HID, pour le pilote usbgamepad : lecture du
;  descripteur de rapport (specification USB HID 1.11, usages 01:04 Joystick,
;  01:05 Gamepad) et traduction de chaque rapport en EV_PAD_RAW vers HIDManager,
;  qui applique le mapping (inc/pad.inc). Rien n'est interprete ici : les boutons
;  restent numerotes dans l'ordre du rapport.
;
;  Le pilote n'a pas pu etre essaye : QEMU n'emule aucune manette USB.
; ============================================================================

PD_NONE		= -1
PD_AX_MAX	= 6			; X, Y, Z, Rx, Ry, Rz
PA_POS		= 0			; un axe : position en bits, taille, min, max
PA_SIZE		= 4
PA_MIN		= 8
PA_MAX_		= 12
PA_LEN		= 16

; x1 = descripteur de rapport, x2 = longueur -> x0 = 1 : c'est une manette
pad_parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x1
	add	x20,x1,x2
	mov	w0,PD_NONE
	adr	x1,pd_btn_pos
	str	w0,[x1]
	adr	x1,pd_hat_pos
	str	w0,[x1]
	adr	x1,pd_ax
	mov	x2,0
.ia:	str	w0,[x1,PA_POS]
	add	x1,x1,PA_LEN
	add	x2,x2,1
	cmp	x2,PD_AX_MAX
	b.lo	.ia
	mov	w21,0				; page d'usage
	mov	w22,0				; taille
	mov	w23,0				; nombre
	mov	w24,0				; 1 : manette
	mov	w25,0				; position
	mov	x26,0				; usages locaux
	mov	w27,0				; logique min
	mov	w28,0				; logique max
.it:	cmp	x19,x20
	b.hs	.fin
	ldrb	w0,[x19],1
	cmp	w0,0xFE
	b.eq	.fin				; (element long : aucun chez une manette)
	and	w1,w0,3
	cmp	w1,3
	cinc	w1,w1,eq			; 0, 1, 2, 4 octets
	mov	x2,0				; valeur
	mov	x3,0
.v:	cmp	x3,x1
	b.hs	.v1
	ldrb	w4,[x19,x3]
	lsl	x5,x3,3
	lsl	x4,x4,x5
	orr	x2,x2,x4
	add	x3,x3,1
	b	.v
.v1:	add	x19,x19,x1
	and	w3,w0,0xFC
	cmp	w3,0x04				; Usage Page
	b.ne	.a
	mov	w21,w2
	b	.it
.a:	cmp	w3,0x14				; Logical Minimum (signe)
	b.ne	.b
	mov	w27,w2
	cmp	w1,1
	b.ne	.a1
	sxtb	w27,w2
	b	.it
.a1:	cmp	w1,2
	b.ne	.it
	sxth	w27,w2
	b	.it
.b:	cmp	w3,0x24				; Logical Maximum
	b.ne	.c
	mov	w28,w2
	b	.it
.c:	cmp	w3,0x74				; Report Size
	b.ne	.d
	mov	w22,w2
	b	.it
.d:	cmp	w3,0x94				; Report Count
	b.ne	.e
	mov	w23,w2
	b	.it
.e:	cmp	w3,0x84				; Report ID
	b.ne	.f
	adr	x4,pd_id
	strb	w2,[x4]
	mov	w25,8				; (les rapports numerotes : l'octet du numero)
	b	.it
.f:	cmp	w3,0x08				; Usage
	b.ne	.g
	lsl	x26,x26,8
	orr	x26,x26,x2
	cmp	w21,1
	b.ne	.it
	and	w4,w2,0xFF
	cmp	w4,4
	b.eq	.pad
	cmp	w4,5
	b.ne	.it
.pad:	mov	w24,1
	b	.it
.g:	cmp	w3,0x80				; Input
	b.ne	.h
	mul	w5,w22,w23			; bits du champ
	tbnz	w2,0,.next			; constante : du remplissage
	cmp	w21,9
	b.ne	.g1
	adr	x4,pd_btn_pos			; les boutons
	ldr	w6,[x4]
	tbz	w6,31,.next
	str	w25,[x4]
	adr	x4,pd_btn_n
	str	w23,[x4]
	b	.next
.g1:	cmp	w21,1
	b.ne	.next
	mov	w6,w25				; usages, le dernier declare en bas
	mov	w7,w23
.g2:	cbz	w7,.next
	sub	w7,w7,1
	mul	w8,w7,w22
	add	w8,w8,w6
	lsr	x9,x26,0
	and	w10,w9,0xFF
	lsr	x26,x26,8
	cmp	w10,0x39			; chapeau
	b.ne	.g3
	adr	x4,pd_hat_pos
	str	w8,[x4]
	adr	x4,pd_hat_min
	str	w27,[x4]
	b	.g2
.g3:	sub	w10,w10,0x30
	cmp	w10,PD_AX_MAX
	b.hs	.g2
	lsl	x10,x10,4
	adr	x4,pd_ax
	add	x4,x4,x10
	str	w8,[x4,PA_POS]
	str	w22,[x4,PA_SIZE]
	str	w27,[x4,PA_MIN]
	str	w28,[x4,PA_MAX_]
	b	.g2
.next:	add	w25,w25,w5
	mov	x26,0
	b	.it
.h:	cmp	w3,0xA0				; Collection : les usages locaux tombent
	b.eq	.h1
	cmp	w3,0x90				; Output
	b.eq	.h1
	cmp	w3,0xB0				; Feature
	b.ne	.it
.h1:	mov	x26,0
	b	.it
.fin:	mov	w0,w24
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = rapport, w1 = position, w2 = bits -> w0 (non signe)
pad_bits:
	mov	w3,0				; valeur
	mov	w4,0				; bit
.l:	cmp	w4,w2
	b.hs	.f
	add	w5,w1,w4
	lsr	w6,w5,3
	ldrb	w6,[x0,x6]
	and	w5,w5,7
	lsr	w6,w6,w5
	and	w6,w6,1
	lsl	w6,w6,w4
	orr	w3,w3,w6
	add	w4,w4,1
	b	.l
.f:	mov	w0,w3
	ret

; x20 = hf, x2 = rapport : EV_PAD_RAW
pad_report:
	prologue
	stp	x19,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x19,x2
	adr	x0,pd_id			; un rapport qui n'est pas le notre
	ldrb	w0,[x0]
	cbz	w0,.ok
	ldrb	w1,[x19]
	cmp	w0,w1
	b.ne	.out
.ok:	mov	w21,0				; bruts
	adr	x0,pd_btn_pos
	ldr	w1,[x0]
	tbnz	w1,31,.hat
	adr	x0,pd_btn_n
	ldr	w2,[x0]
	cmp	w2,28
	mov	w0,28
	csel	w2,w0,w2,hi
	mov	x0,x19
	bl	pad_bits
	mov	w21,w0
.hat:	adr	x0,pd_hat_pos
	ldr	w1,[x0]
	tbnz	w1,31,.axes
	mov	x0,x19
	mov	w2,4
	bl	pad_bits
	adr	x1,pd_hat_min
	ldr	w1,[x1]
	sub	w0,w0,w1
	cmp	w0,7
	b.hi	.axes
	adr	x1,pd_hat_tab			; 8 directions -> haut, bas, gauche, droite
	ldrb	w0,[x1,x0]
	orr	w21,w21,w0,lsl 28
.axes:	mov	x22,0				; lx, ly, rx, ry
	mov	x23,0
.ax:	adr	x0,pd_ax_of
	ldrb	w0,[x0,x23]			; X, Y, Z, Rx, Ry, Rz -> rang (4 : aucun)
	cmp	w0,4
	b.hs	.an
	lsl	w0,w0,4
	adr	x4,pd_ax
	add	x4,x4,x23,lsl 4
	ldr	w1,[x4,PA_POS]
	tbnz	w1,31,.an
	stp	x0,x4,[sp,-16]!
	mov	x0,x19
	ldr	w2,[x4,PA_SIZE]
	bl	pad_bits
	ldp	x7,x4,[sp],16
	ldr	w2,[x4,PA_MIN]
	ldr	w3,[x4,PA_MAX_]
	sub	w5,w3,w2
	cbz	w5,.an
	sub	w0,w0,w2
	mov	w6,2000
	mul	w0,w0,w6
	sdiv	w0,w0,w5
	sub	w0,w0,1000
	and	x0,x0,0xFFFF
	lsl	x0,x0,x7
	orr	x22,x22,x0
.an:	add	x23,x23,1
	cmp	x23,PD_AX_MAX
	b.lo	.ax
	mov	x0,EV_PAD_RAW
	mov	w1,w21
	mov	x2,x22
	adr	x3,pd_vidpid
	ldr	w3,[x3]
	bl	hid_pousser
.out:	ldp	x22,x23,[sp],16
	ldp	x19,x21,[sp],16
	epilogue

pd_hat_tab:			; bits : 1 haut, 2 bas, 4 gauche, 8 droite
	db	1,9,8,10,2,6,4,5
pd_ax_of:			; X lx, Y ly, Z rx, Rz ry ; Rx et Ry : des gachettes
	db	0,1,2,4,4,3
	align	4
