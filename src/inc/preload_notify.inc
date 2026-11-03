; ============================================================================
;  inc/preload_notify.inc - le nom d'un programme prechargeable, et la
;  notification « Jet est préchargé » (init2 preload, la commande preload).
;  A inclure apres notify.inc, dans le segment executable.
; ============================================================================

; x0 = chemin (« SD0:/apps/jet.app/main », « SD0:/bin/fasmarm »), x1 = sortie
; (64 o) : le nom (« jet », « fasmarm ») -> x1 = sa fin
preload_name:
	mov	x2,x0
.fin:	ldrb	w3,[x2]
	cbz	w3,.bout
	add	x2,x2,1
	b	.fin
.bout:	mov	x3,x2				; fin du dernier composant
.deb:	cmp	x2,x0
	b.ls	.main
	ldurb	w4,[x2,-1]
	cmp	w4,'/'
	b.eq	.main
	sub	x2,x2,1
	b	.deb
.main:	sub	x4,x3,x2			; « main » : le paquet
	cmp	x4,4
	b.ne	.copie
	ldr	w4,[x2]
	movl	w5,0x6E69616D			; « main »
	cmp	w4,w5
	b.ne	.copie
	cmp	x2,x0
	b.ls	.copie
	sub	x3,x2,1
	mov	x2,x3
.par:	cmp	x2,x0
	b.ls	.app
	ldurb	w4,[x2,-1]
	cmp	w4,'/'
	b.eq	.app
	sub	x2,x2,1
	b	.par
.app:	sub	x4,x3,x2
	cmp	x4,4
	b.ls	.copie
	ldur	w4,[x3,-4]
	movl	w5,0x7070612E			; « .app »
	cmp	w4,w5
	b.ne	.copie
	sub	x3,x3,4
.copie:	mov	x4,63
.c:	cmp	x2,x3
	b.hs	.z
	cbz	x4,.z
	ldrb	w5,[x2],1
	strb	w5,[x1],1
	sub	x4,x4,1
	b	.c
.z:	strb	wzr,[x1]
	ret

; x0 = chemin : notification « Jet est préchargé »
preload_notify:
	prologue
	sub	sp,sp,96
	mov	x1,sp
	bl	preload_name
	ldrb	w0,[sp]				; une majuscule au debut
	sub	w2,w0,'a'
	cmp	w2,25
	b.hi	.suite
	sub	w0,w0,32
	strb	w0,[sp]
.suite:	adr	x2,pn_texte
.t:	ldrb	w0,[x2],1
	strb	w0,[x1],1
	cbnz	w0,.t
	adr	x0,pn_titre
	mov	x1,sp
	bl	notifier
	add	sp,sp,96
	epilogue

pn_titre	db 'Pr',0xC3,0xA9,'chargement',0
pn_texte	db ' est pr',0xC3,0xA9,'charg',0xC3,0xA9,0
		align 4
