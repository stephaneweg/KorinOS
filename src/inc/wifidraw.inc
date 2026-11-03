; ============================================================================
;  inc/wifidraw.inc - petits dessins du Wi-Fi (UIKit) : barres de signal,
;  cadenas, coche ; pour la barre de menus, wifimenu et l'applet Wi-Fi du
;  panneau de configuration.
;
;	segment executable : include '../../inc/wifidraw.inc' (apres UIKit)
; ============================================================================

; w0 = RSSI (dBm, signe) -> x0 = barres allumees (1..4)
wd_level:
	sxtw	x0,w0
	mov	x1,4
	cmn	x0,55
	b.ge	.r
	mov	x1,3
	cmn	x0,65
	b.ge	.r
	mov	x1,2
	cmn	x0,75
	b.ge	.r
	mov	x1,1
.r:	mov	x0,x1
	ret

; x0 = x, x1 = haut (icone de 16 x 14), x2 = barres allumees (0..4),
; w3 = couleur allumee, w4 = eteinte
wd_bars:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	mov	w23,w4
	mov	x24,0
.b:	mov	x0,4				; hauteur : 4, 7, 10, 13
	mov	x1,3
	madd	x2,x24,x1,x0
	add	x0,x20,13
	sub	x1,x0,x2
	lsl	x0,x24,2
	add	x0,x19,x0
	mov	x3,x2
	mov	x2,3
	cmp	x24,x21
	csel	w4,w22,w23,lo
	bl	ui_fill
	add	x24,x24,1
	cmp	x24,4
	b.lo	.b
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = haut (cadenas de 8 x 10), w2 = couleur
wd_lock:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	add	x0,x19,2			; anse : un cadre de 4 x 5
	mov	x1,x20
	mov	x2,4
	mov	x3,1
	mov	w4,w21
	bl	ui_fill
	add	x0,x19,1
	add	x1,x20,1
	mov	x2,1
	mov	x3,4
	mov	w4,w21
	bl	ui_fill
	add	x0,x19,6
	add	x1,x20,1
	mov	x2,1
	mov	x3,4
	mov	w4,w21
	bl	ui_fill
	mov	x0,x19				; corps : 8 x 5
	add	x1,x20,5
	mov	x2,8
	mov	x3,5
	mov	w4,w21
	bl	ui_fill
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = haut (coche de 10 x 8), w2 = couleur
wd_check:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	mov	x22,0				; branche courte : 4 pas vers le bas
.c:	add	x0,x19,x22
	add	x1,x20,3
	add	x1,x1,x22
	mov	x2,2
	mov	x3,2
	mov	w4,w21
	bl	ui_fill
	add	x22,x22,1
	cmp	x22,3
	b.lo	.c
	mov	x22,0				; branche longue : vers le haut
.l:	add	x0,x19,3
	add	x0,x0,x22
	add	x1,x20,5
	sub	x1,x1,x22
	mov	x2,2
	mov	x3,2
	mov	w4,w21
	bl	ui_fill
	add	x22,x22,1
	cmp	x22,6
	b.lo	.l
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
