; zipcli.inc - ce que zip et unzip partagent (comme bin/arccli.h d'Onyx) : motifs (* ? sans la casse,
; ou un dossier : tout ce qu'il contient), noms d'archive faits d'un chemin, chaines, tailles, dates.
; Apres tool.inc (t_putc, t_puts...) ; sur AppKit (malloc, str_len, fmt_dec).

; w0 = octet -> w0 en minuscule (ASCII) ; touche w1
zc_lower:
	sub	w1,w0,'A'
	cmp	w1,25
	b.hi	.r
	add	w0,w0,32
.r:	ret

; x0 = motif (* ?), x1 = nom -> x0 = 1 s'il y correspond (sans la casse ASCII)
zc_glob:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	ldrb	w2,[x19]
	cbz	w2,.fin
	cmp	w2,'*'
	b.ne	.car
.et:	ldrb	w3,[x19,1]
	cmp	w3,'*'
	b.ne	.e1
	add	x19,x19,1
	b	.et
.e1:	cbz	w3,.oui
.boucle:
	ldrb	w3,[x20]
	cbz	w3,.non
	add	x0,x19,1
	mov	x1,x20
	bl	zc_glob
	cbnz	x0,.oui
	add	x20,x20,1
	b	.boucle
.car:	ldrb	w3,[x20]
	cbz	w3,.non
	cmp	w2,'?'
	b.eq	.av
	mov	w0,w2
	bl	zc_lower
	mov	w4,w0
	mov	w0,w3
	bl	zc_lower
	cmp	w0,w4
	b.ne	.non
.av:	add	x19,x19,1
	add	x20,x20,1
	b	.c
.fin:	ldrb	w3,[x20]
	cmp	w3,0
	cset	x0,eq
	b	.out
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom, x1 = dossier -> x0 = 1 si le nom est sous ce dossier (« dossier/... », sans la casse)
zc_under:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldrb	w0,[x20]
	cbz	w0,.non
.c:	ldrb	w0,[x20],1
	cbz	w0,.fin
	bl	zc_lower
	mov	w2,w0
	ldrb	w0,[x19],1
	bl	zc_lower
	cmp	w0,w2
	b.ne	.non
	b	.c
.fin:	ldrb	w0,[x19]
	cmp	w0,'/'
	cset	x0,eq
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom, x1 = tableau de motifs, x2 = nombre -> x0 = 1 si un motif le choisit (aucun motif : tous)
zc_chosen:
	cbz	x2,.tous
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
.p:	cbz	x21,.non
	ldr	x1,[x20],8
	sub	x21,x21,1
	mov	x2,sp				; le motif sans ses / de la fin
	mov	x3,0
.cp:	ldrb	w4,[x1,x3]
	strb	w4,[x2,x3]
	cbz	w4,.cpf
	add	x3,x3,1
	cmp	x3,500
	b.lo	.cp
	strb	wzr,[x2,x3]
.cpf:	cbz	x3,.t
	sub	x4,x3,1
	ldrb	w5,[x2,x4]
	cmp	w5,'/'
	b.ne	.t
	strb	wzr,[x2,x4]
	mov	x3,x4
	b	.cpf
.t:	mov	x0,sp
	mov	x1,x19
	bl	zc_glob
	cbnz	x0,.oui
	mov	x0,x19
	mov	x1,sp
	bl	zc_under
	cbnz	x0,.oui
	b	.p
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	add	sp,sp,512
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.tous:	mov	x0,1
	ret

; x0, x1 = chaines -> x0 = 1 si egales sans la casse ASCII
zc_streqi:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	ldrb	w0,[x19],1
	bl	zc_lower
	mov	w2,w0
	ldrb	w0,[x20],1
	bl	zc_lower
	cmp	w0,w2
	b.ne	.non
	cbnz	w0,.c
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source : copie (0 final)
zc_copy:
	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,zc_copy
	ret

; x0 = destination (une chaine), x1 = source : ajoutee a la fin
zc_append:
	ldrb	w2,[x0]
	cbz	w2,zc_copy
	add	x0,x0,1
	b	zc_append

; x0 = chaine -> x0 = 1 si elle contient un point
zc_has_dot:
	ldrb	w1,[x0],1
	cbz	w1,.non
	cmp	w1,'.'
	b.ne	zc_has_dot
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = nom -> x0 = 1 s'il a une partie « .. » (refuse a l'extraction)
zc_dotdot:
	mov	x1,x0				; debut de la partie
.c:	ldrb	w2,[x0]
	cbz	w2,.part
	cmp	w2,'/'
	b.eq	.part
	cmp	w2,'\'
	b.eq	.part
	add	x0,x0,1
	b	.c
.part:	sub	x3,x0,x1
	cmp	x3,2
	b.ne	.suite
	ldrb	w3,[x1]
	cmp	w3,'.'
	b.ne	.suite
	ldrb	w3,[x1,1]
	cmp	w3,'.'
	b.ne	.suite
	mov	x0,1
	ret
.suite:	cbz	w2,.non
	add	x0,x0,1
	mov	x1,x0
	b	.c
.non:	mov	x0,0
	ret

; x0 = dossier, x1 = nom -> x0 = « dossier/nom » (malloc ; dossier vide : le nom seul)
zc_join:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	str_len
	mov	x21,x0
	mov	x0,x20
	bl	str_len
	add	x0,x0,x21
	add	x0,x0,2
	bl	malloc
	mov	x22,x0
	mov	x2,x0
	mov	x1,x19
.a:	ldrb	w3,[x1],1
	cbz	w3,.af
	strb	w3,[x2],1
	b	.a
.af:	cmp	x2,x22
	b.eq	.b
	ldurb	w3,[x2,-1]
	cmp	w3,'/'
	b.eq	.b
	cmp	w3,':'
	b.eq	.b
	mov	w3,'/'
	strb	w3,[x2],1
.b:	ldrb	w3,[x20],1
	strb	w3,[x2],1
	cbnz	w3,.b
	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin -> x0 = son nom dans une archive (malloc) : sans volume (« SD0: »), sans / en tete,
; sans . ni .. (« SD0:/a/./b/../c.txt » -> « a/c.txt »)
zc_norm:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x1,0				; un volume : « XXX: » dans les 6 premiers
.vo:	ldrb	w2,[x19,x1]
	cbz	w2,.nv
	cmp	w2,':'
	b.eq	.vol
	add	x1,x1,1
	cmp	x1,6
	b.lo	.vo
	b	.nv
.vol:	add	x1,x1,1
	add	x19,x19,x1
.nv:	mov	x0,x19
	bl	str_len
	add	x0,x0,2
	bl	malloc
	mov	x20,x0				; debut
	mov	x21,x0				; ecriture
.comp:	ldrb	w2,[x19]
	cbz	w2,.fin
	cmp	w2,'/'
	b.eq	.un
	cmp	w2,'\'
	b.eq	.un
	mov	x3,x19				; la partie : jusqu'au / suivant
.ce:	ldrb	w2,[x3]
	cbz	w2,.cf
	cmp	w2,'/'
	b.eq	.cf
	cmp	w2,'\'
	b.eq	.cf
	add	x3,x3,1
	b	.ce
.cf:	sub	x4,x3,x19
	cmp	x4,1
	b.ne	.dd
	ldrb	w2,[x19]
	cmp	w2,'.'
	b.eq	.next
.dd:	cmp	x4,2
	b.ne	.copie
	ldrb	w2,[x19]
	cmp	w2,'.'
	b.ne	.copie
	ldrb	w2,[x19,1]
	cmp	w2,'.'
	b.ne	.copie
.pop:	cmp	x21,x20				; « .. » : la partie d'avant retiree
	b.eq	.next
	sub	x21,x21,1
	ldrb	w2,[x21]
	cmp	w2,'/'
	b.ne	.pop
	b	.next
.copie:	cmp	x21,x20
	b.eq	.cc
	mov	w2,'/'
	strb	w2,[x21],1
.cc:	ldrb	w2,[x19],1
	strb	w2,[x21],1
	cmp	x19,x3
	b.lo	.cc
.next:	mov	x19,x3
	b	.comp
.un:	add	x19,x19,1
	b	.comp
.fin:	strb	wzr,[x21]
	mov	x0,x20
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur, x1 = largeur : alignee a droite
zc_put_w:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x1
	mov	x1,sp
	bl	fmt_dec
	mov	x2,sp
	sub	x20,x0,x2
.b:	cmp	x20,x19
	b.hs	.t
	mov	w0,' '
	bl	t_putc
	add	x20,x20,1
	b	.b
.t:	mov	x0,sp
	bl	t_puts
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine, x1 = largeur : alignee a gauche (des blancs apres)
zc_put_pad:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	str_len
	sub	x20,x20,x0
	mov	x0,x19
	bl	t_puts
.b:	cmp	x20,0
	b.le	.out
	mov	w0,' '
	bl	t_putc
	sub	x20,x20,1
	b	.b
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = 0..99 : deux chiffres
zc_two:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x2,10
	udiv	x3,x0,x2
	msub	x20,x3,x2,x0
	udiv	x4,x3,x2
	msub	x19,x4,x2,x3
	add	w0,w19,'0'
	bl	t_putc
	add	w0,w20,'0'
	bl	t_putc
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur : 8 chiffres hexadecimaux
zc_hex8:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,28
.c:	lsrv	x0,x19,x20
	and	x0,x0,15
	cmp	x0,10
	add	w1,w0,'0'
	add	w2,w0,'a'-10
	csel	w0,w1,w2,lo
	bl	t_putc
	subs	x20,x20,4
	b.ge	.c
	ldp	x19,x20,[sp],16
	epilogue

; x0 = secondes UTC (0 : aucune date) : « AAAA-MM-JJ HH:MM » en heure locale
zc_put_date:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	cbz	x0,.rien
	mov	x19,x0
	mov	x0,1				; decalage de l'heure locale
	sys	SYS_CLOCK_GET
	add	x19,x19,x0
	movl	x1,86400
	udiv	x2,x19,x1			; jours depuis 1970
	msub	x20,x2,x1,x19			; secondes du jour
	movl	x4,719468
	add	x2,x2,x4
	movl	x4,146097
	udiv	x5,x2,x4			; periode de 400 ans
	msub	x6,x5,x4,x2			; jour dans la periode
	mov	x4,1460
	udiv	x7,x6,x4
	sub	x7,x6,x7
	movl	x4,36524
	udiv	x8,x6,x4
	add	x7,x7,x8
	movl	x4,146096
	udiv	x8,x6,x4
	sub	x7,x7,x8
	mov	x4,365
	udiv	x7,x7,x4			; annee dans la periode
	mov	x4,400
	mul	x8,x5,x4
	add	x8,x8,x7			; annee (depuis mars)
	mov	x4,365
	mul	x9,x7,x4
	add	x9,x9,x7,lsr 2
	mov	x4,100
	udiv	x10,x7,x4
	sub	x9,x9,x10
	sub	x6,x6,x9			; jour dans l'annee
	mov	x4,5
	mul	x9,x6,x4
	add	x9,x9,2
	mov	x4,153
	udiv	x9,x9,x4			; mois, depuis mars
	mov	x4,153
	mul	x10,x9,x4
	add	x10,x10,2
	mov	x4,5
	udiv	x10,x10,x4
	sub	x6,x6,x10
	add	x21,x6,1			; jour du mois
	add	x10,x9,3
	sub	x4,x9,9
	cmp	x9,10
	csel	x22,x10,x4,lo			; mois, de 1 a 12
	cmp	x22,2
	csinc	x8,x8,x8,hi			; janvier, fevrier : l'annee suivante
	mov	x0,x8
	bl	t_putn
	mov	w0,'-'
	bl	t_putc
	mov	x0,x22
	bl	zc_two
	mov	w0,'-'
	bl	t_putc
	mov	x0,x21
	bl	zc_two
	mov	w0,' '
	bl	t_putc
	mov	x1,3600
	udiv	x0,x20,x1
	bl	zc_two
	mov	w0,':'
	bl	t_putc
	mov	x1,60
	udiv	x0,x20,x1
	udiv	x3,x0,x1
	msub	x0,x3,x1,x0
	bl	zc_two
	b	.out
.rien:	adr	x0,zc_s_nodate
	bl	t_puts
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = octets : « 512 o », « 37,5 Mo », « 29,7 Go »
zc_put_size:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x21,zc_s_o
	mov	x20,1
	cmp	x19,1024
	b.lo	.go
	adr	x21,zc_s_ko
	mov	x20,1024
	lsr	x0,x19,20
	cbz	x0,.go
	adr	x21,zc_s_mo
	movl	x20,0x100000
	lsr	x0,x19,30
	cbz	x0,.go
	adr	x21,zc_s_go
	movl	x20,0x40000000
.go:	udiv	x22,x19,x20
	mov	x0,x22
	bl	t_putn
	cmp	x20,1
	b.eq	.u
	cmp	x22,100
	b.hs	.u
	adr	x0,zc_s_dec
	bl	t_dire
	msub	x0,x22,x20,x19
	mov	x1,10
	mul	x0,x0,x1
	udiv	x0,x0,x20
	bl	t_putn
.u:	mov	x0,x21
	bl	t_dire
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = taille, x1 = taille compressee -> x0 = pourcentage gagne
zc_pct:
	cbz	x0,.zero
	cmp	x1,x0
	b.hs	.zero
	sub	x1,x0,x1
	mov	x2,100
	mul	x1,x1,x2
	udiv	x0,x1,x0
	ret
.zero:	mov	x0,0
	ret

; x0 = donnee (0 : aucune), x1 = texte : « nom : donnee : texte » sur l'erreur standard
zc_err:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	ldr	x0,[x20]
	bl	t_err
	adr	x0,zc_s_sep
	bl	t_derr
	cbz	x21,.t
	mov	x0,x21
	bl	t_err
	adr	x0,zc_s_sep
	bl	t_derr
.t:	mov	x0,x22
	bl	t_derr
	adr	x0,s_t_nl
	bl	t_err
	ldp	x21,x22,[sp],16
	epilogue

zc_s_nodate	db '---------- -----',0
zc_s_sep	db ' : ',0
zc_s_dec	db ',',0
zc_s_o		db ' o',0
zc_s_ko		db ' Ko',0
zc_s_mo		db ' Mo',0
zc_s_go		db ' Go',0
		align 4
