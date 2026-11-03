; ============================================================================
;  inc/notify.inc - envoyer une notification a notifyd (service « notify »,
;  op 1 : titre, 0, texte, 0). Sans notifyd (avant le bureau, en mode
;  console) : rien.
;
;	segment executable ... include '../../inc/notify.inc'
;	bl notifier			; x0 = titre, x1 = texte (UTF-8)
; ============================================================================

; x0 = titre, x1 = texte -> x0 = 0, ou < 0 si notifyd est absent
notifier:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,512			; message
	mov	x19,x0
	mov	x20,x1
	adr	x0,nt_nom
	mov	x1,0				; (sans attendre)
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	mov	x2,1				; op 1
	str	x2,[sp]
	add	x3,sp,48			; titre, 0, texte, 0
	add	x4,sp,48+463
	mov	x1,x19
.t:	ldrb	w2,[x1],1
	strb	w2,[x3],1
	cbz	w2,.tx
	cmp	x3,x4
	b.lo	.t
	strb	wzr,[x3,-1]
.tx:	mov	x1,x20
.x:	cmp	x3,x4
	b.hs	.coupe
	ldrb	w2,[x1],1
	strb	w2,[x3],1
	cbnz	w2,.x
	b	.envoi
.coupe:	strb	wzr,[x4]
.envoi:	mov	x1,sp
	sys	SYS_SVC_CALL
.out:	add	sp,sp,512
	ldp	x19,x20,[sp],16
	epilogue

nt_nom	db 'notify',0
	align 4
