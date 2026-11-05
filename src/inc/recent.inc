; ============================================================================
;  inc/recent.inc - les applications recentes, la plus recente d'abord :
;  SD0:/etc/recent.txt, un nom de paquet par ligne (« terminal »). Tenues par
;  la coquille qui voit les applications demarrer (inc/apprun.inc : le panel
;  sur le bureau, l'accueil en poche), lues par le lanceur (onglet
;  « Recentes ») et l'accueil (rangee « Recentes »). A inclure apres
;  applist.inc.
;
;	bl applis_load ; bl rc_load		rc_n, rc_app (applications)
;	mov x0,app ; bl rc_touch		devant, et le fichier reecrit
; ============================================================================

RC_MAX		= 12
RC_BUF		= RC_MAX * AP_NOM + 64

; rc_app <- les applications du fichier connues du catalogue (dans l'ordre,
; sans doublon)
rc_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,rc_n
	str	xzr,[x0]
	adr	x0,rc_path
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,rc_buf
	mov	x2,RC_BUF-1
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	csel	x20,xzr,x20,lt
	adr	x19,rc_buf
	strb	wzr,[x19,x20]
.line:	ldrb	w0,[x19]
	cbz	w0,.out
	mov	x20,x19				; fin de ligne -> 0 (CR, espaces : aussi)
.e:	ldrb	w0,[x20]
	cbz	w0,.cut
	cmp	w0,10
	b.eq	.cut
	add	x20,x20,1
	b	.e
.cut:	ldrb	w22,[x20]
	strb	wzr,[x20]
	mov	x0,x19
	bl	ini_trim
	ldrb	w1,[x0]
	cbz	w1,.next
	bl	app_find
	tbnz	x0,63,.next
	mov	x21,x0
	adr	x0,rc_n				; deja la ? plus de place ?
	ldr	x2,[x0]
	cmp	x2,RC_MAX
	b.hs	.out
	adr	x1,rc_app
	mov	x3,0
.d:	cmp	x3,x2
	b.hs	.add
	ldrb	w4,[x1,x3]
	cmp	x4,x21
	b.eq	.next
	add	x3,x3,1
	b	.d
.add:	strb	w21,[x1,x2]
	add	x2,x2,1
	str	x2,[x0]
.next:	cbz	w22,.out
	add	x19,x20,1
	b	.line
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = application : devant la liste, le fichier reecrit
rc_touch:
	prologue
	adr	x1,rc_app
	adr	x2,rc_n
	ldr	x3,[x2]
	mov	x4,0				; sa place (sinon la derniere, qui sort)
.f:	cmp	x4,x3
	b.hs	.new
	ldrb	w5,[x1,x4]
	cmp	x5,x0
	b.eq	.shift
	add	x4,x4,1
	b	.f
.new:	cmp	x3,RC_MAX
	b.hs	.full
	add	x3,x3,1
	str	x3,[x2]
.full:	sub	x4,x3,1
.shift:	cbz	x4,.put				; decale ce qui est devant elle
	sub	x5,x4,1
	ldrb	w6,[x1,x5]
	strb	w6,[x1,x4]
	mov	x4,x5
	b	.shift
.put:	strb	w0,[x1]
	bl	rc_save
	epilogue

; SD0:/etc/recent.txt <- rc_app
rc_save:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,rc_buf
	strb	wzr,[x19]
	mov	x20,0
.a:	adr	x0,rc_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.w
	adr	x0,rc_app
	ldrb	w0,[x0,x20]
	adr	x1,ap_nom
	add	x1,x1,x0,lsl 5
	mov	x0,x19
	bl	str_cat
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
	mov	x19,x0
	add	x20,x20,1
	b	.a
.w:	adr	x0,rc_path
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x20,x0
	adr	x1,rc_buf
	sub	x2,x19,x1
	sys	SYS_WRITE
	mov	x0,x20
	sys	SYS_CLOSE
.out:	ldp	x19,x20,[sp],16
	epilogue

rc_path		db 'SD0:/etc/recent.txt',0
		align 4

macro recent_data {
		align 8
rc_n		dd 0
rc_app		rb RC_MAX
rc_buf		rb RC_BUF
		align 8
}
