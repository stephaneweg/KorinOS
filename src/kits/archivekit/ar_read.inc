; ============================================================================
;  ar_read.inc - lire une archive : ZIP (avec ZIP64), tar, tar.gz, gz, sur la
;  carte ou dans un tampon. Les entrees sont listees a l'ouverture ; rien n'est
;  cru sans verification : tailles et positions sont bornees par la source, et
;  le contenu est verifie par son CRC a la lecture.
;
;  Un tar.gz est decompresse en memoire a l'ouverture (on ne peut pas sauter
;  dans un flux deflate) ; un .gz d'un seul fichier reste en place : il n'est
;  decode qu'a la lecture.
; ============================================================================

A_FMT		= 24
A_MEM		= 32			; la source en memoire, 0 : fichier
A_FD		= 40			; descripteur, -1 : aucun
A_LEN		= 48			; octets de la source
A_TBL		= 56			; entrees
A_N		= 64
A_CAP		= 72
A_POOL		= 80			; morceau de noms en cours (u64 : suivant, puis les noms)
A_POOLPOS	= 88
A_IF		= 96			; etat d'inflate
A_INNER		= 104			; le tar decompresse d'un tar.gz (bloc)
A_LONG		= 112			; nom long du tar en attente (bloc de RD_LONG octets)
A_PROBE		= 120			; 1 : on veut seulement le format
A_RAWLEN	= 128			; taille de l'archive sur la carte
A_BASE		= 256			; nom de base du fichier (256)
RD_SIZE		= 4096 - 16

RD_LONG		= 65536
RD_POOL		= 1 shl 20

; une entree
E_NAME		= 0
E_SIZE		= 8
E_PACK		= 16
E_OFF		= 24			; ZIP : en-tete local ; tar : donnees
E_TIME		= 32
E_CRC		= 40
E_METHOD	= 44
E_FLAGS		= 48			; bit 0 : dossier, bit 1 : chiffree
E_BYTES		= 56

; x0 = poignee, bad : etiquette d'erreur -> x19 = archive
macro rd_handle bad* {
	tbnz	x0,63,bad
	cbz	x0,bad
	ldr	x1,[x0,H_MAGIC]
	movl	x2,AR_MAGIC_RD
	cmp	x1,x2
	b.ne	bad
	mov	x19,x0
}

; x19 = archive, x1 = indice -> x20 = entree ; bad si hors bornes
macro rd_index bad* {
	ldr	x2,[x19,A_N]
	cmp	x1,x2
	b.hs	bad
	ldr	x20,[x19,A_TBL]
	mov	x2,E_BYTES
	madd	x20,x1,x2,x20
}

; x0 = archive, x1 = position, x2 = tampon, x3 = octets -> x0 = 0, erreur
src_read:
	ar_enter
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	ldr	x0,[x19,A_LEN]
	cmp	x20,x0
	b.hi	.short
	sub	x0,x0,x20
	cmp	x22,x0
	b.hi	.short
	ldr	x1,[x19,A_MEM]
	cbz	x1,.file
	add	x1,x1,x20
	mov	x0,x21
	mov	x2,x22
	bl	ar_memcpy
	b	.ok
.file:	ldr	x0,[x19,A_FD]
	mov	x1,x20
	mov	x2,0
	bl	f_position
	tbnz	x0,63,.out
.more:	cbz	x22,.ok
	ldr	x0,[x19,A_FD]
	mov	x1,x21
	mov	x2,x22
	bl	f_lire
	cmp	x0,0
	b.le	.io
	add	x21,x21,x0
	sub	x22,x22,x0
	b	.more
.io:	b.lt	.out
.short:	mov	x0,AR_E_CORRUPT
	b	.out
.ok:	mov	x0,0
.out:	ar_leave

; x19 = archive -> x0 = une entree de plus (a zero), 0 si plus de memoire
rd_add:
	prologue
	stp	x20,x21,[sp,-16]!
	ldr	x0,[x19,A_N]
	ldr	x1,[x19,A_CAP]
	cmp	x0,x1
	b.lo	.have
	lsl	x20,x1,1
	cbnz	x20,.a
	mov	x20,256
.a:	mov	x0,E_BYTES
	mul	x0,x0,x20
	bl	ar_alloc
	cbz	x0,.out
	mov	x21,x0
	ldr	x1,[x19,A_TBL]
	cbz	x1,.b
	ldr	x2,[x19,A_N]
	mov	x3,E_BYTES
	mul	x2,x2,x3
	bl	ar_memcpy
	ldr	x0,[x19,A_TBL]
	bl	ar_mfree
.b:	str	x21,[x19,A_TBL]
	str	x20,[x19,A_CAP]
	ldr	x0,[x19,A_N]
.have:	mov	x1,E_BYTES
	ldr	x2,[x19,A_TBL]
	madd	x2,x0,x1,x2
	add	x0,x0,1
	str	x0,[x19,A_N]
	mov	x0,x2
.out:	ldp	x20,x21,[sp],16
	epilogue

; x19 = archive, x0 = octets -> x0 = place dans les noms, 0 si plus de memoire
pool_room:
	prologue
	stp	x20,x21,[sp,-16]!
	mov	x20,x0
	ldr	x1,[x19,A_POOL]
	cbz	x1,.new
	ldr	x2,[x19,A_POOLPOS]
	add	x3,x2,x20
	movl	x4,RD_POOL
	cmp	x3,x4
	b.ls	.have
.new:	movl	x0,RD_POOL
	bl	ar_alloc
	cbz	x0,.out
	ldr	x1,[x19,A_POOL]
	str	x1,[x0]
	str	x0,[x19,A_POOL]
	mov	x1,x0
	mov	x2,8
.have:	add	x0,x1,x2
.out:	ldp	x20,x21,[sp],16
	epilogue

; x0 = octets, x1 = n -> x0 = 1 : UTF-8 valable
utf8_ok:
	add	x1,x0,x1
.a:	cmp	x0,x1
	b.hs	.yes
	ldrb	w2,[x0],1
	cmp	w2,0x80
	b.lo	.a
	mov	x3,1
	cmp	w2,0xC2
	b.lo	.no
	cmp	w2,0xE0
	b.lo	.c
	mov	x3,2
	cmp	w2,0xF0
	b.lo	.c
	mov	x3,3
	cmp	w2,0xF5
	b.hs	.no
.c:	cmp	x0,x1
	b.hs	.no
	ldrb	w2,[x0],1
	and	w2,w2,0xC0
	cmp	w2,0x80
	b.ne	.no
	subs	x3,x3,1
	b.ne	.c
	b	.a
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x19 = archive, x0 = nom brut, x1 = octets, x2 = 1 : UTF-8 sur -> x0 = nom propre (dans les
; noms de l'archive), x1 = 1 : il finissait par / ; 0 si plus de memoire.
; \ devient /, sans / ni ./ au debut, sans // ; CP437 converti en UTF-8.
rd_name:
	ar_enter
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	cbnz	x22,.room
	bl	utf8_ok
	mov	x22,x0
.room:	add	x0,x21,x21,lsl 1
	add	x0,x0,2
	bl	pool_room
	cbz	x0,.fail
	mov	x23,x0
	mov	x3,x0				; ecriture
	mov	x4,0
	adr	x7,cp437_tab
.c:	cmp	x4,x21
	b.hs	.ce
	ldrb	w5,[x20,x4]
	add	x4,x4,1
	cbz	w5,.ce
	cmp	w5,'\'
	b.ne	.c1
	mov	w5,'/'
.c1:	cmp	w5,0x80
	b.lo	.put
	cbnz	x22,.put
	sub	w5,w5,0x80
	ldrh	w5,[x7,x5,lsl 1]
	cmp	w5,0x80
	b.lo	.put
	cmp	w5,0x800
	b.lo	.two
	lsr	w6,w5,12
	orr	w6,w6,0xE0
	strb	w6,[x3],1
	ubfx	w6,w5,6,6
	orr	w6,w6,0x80
	strb	w6,[x3],1
	b	.last
.two:	lsr	w6,w5,6
	orr	w6,w6,0xC0
	strb	w6,[x3],1
.last:	and	w5,w5,0x3F
	orr	w5,w5,0x80
.put:	strb	w5,[x3],1
	b	.c
.ce:	strb	wzr,[x3]
	; nettoyage sur place
	mov	x4,x23				; lecture
	mov	x5,x23				; ecriture
	mov	x6,0				; finissait par /
.lead:	ldrb	w0,[x4]
	cmp	w0,'/'
	b.ne	.l1
	add	x4,x4,1
	b	.lead
.l1:	cmp	w0,'.'
	b.ne	.body
	ldrb	w0,[x4,1]
	cmp	w0,'/'
	b.ne	.body
	add	x4,x4,2
	b	.lead
.body:	ldrb	w0,[x4],1
	cbz	w0,.be
	cmp	w0,'/'
	b.ne	.b1
	cmp	x5,x23
	b.eq	.body
	ldrb	w1,[x5,-1]
	cmp	w1,'/'
	b.eq	.body
.b1:	strb	w0,[x5],1
	b	.body
.be:	cmp	x5,x23
	b.eq	.be1
	ldrb	w1,[x5,-1]
	cmp	w1,'/'
	b.ne	.be1
	sub	x5,x5,1
	mov	x6,1
.be1:	strb	wzr,[x5]
	sub	x0,x5,x23
	add	x0,x0,1
	ldr	x2,[x19,A_POOL]			; (x23 est dans le morceau en cours)
	sub	x2,x23,x2
	add	x2,x2,x0
	str	x2,[x19,A_POOLPOS]
	mov	x0,x23
	mov	x1,x6
	b	.out
.fail:	mov	x0,0
	mov	x1,0
.out:	ar_leave

; --- dates --------------------------------------------------------------------------

; x0 = annee, x1 = mois, x2 = jour -> x0 = jours depuis 1970 (annee >= 0)
days_civil:
	mov	x3,x1
	cmp	x1,2
	b.hi	.a
	sub	x0,x0,1
	add	x3,x3,12
.a:	sub	x3,x3,3				; mois depuis mars
	mov	x4,400
	udiv	x5,x0,x4			; ere
	msub	x6,x5,x4,x0			; annee de l'ere
	mov	x7,153
	mul	x3,x3,x7
	add	x3,x3,2
	mov	x7,5
	udiv	x3,x3,x7
	add	x3,x3,x2
	sub	x3,x3,1				; jour de l'annee
	mov	x7,365
	mul	x7,x6,x7
	add	x7,x7,x6,lsr 2
	mov	x4,100
	udiv	x4,x6,x4
	sub	x7,x7,x4
	add	x7,x7,x3
	movl	x4,146097
	mul	x5,x5,x4
	add	x0,x5,x7
	movl	x4,719468
	sub	x0,x0,x4
	ret

; w0 = date DOS (date << 16 | heure) -> x0 = secondes UTC, 0 : pas une date
dos_unix:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	ubfx	w1,w19,21,4			; mois
	ubfx	w2,w19,16,5			; jour
	cbz	w1,.none
	cmp	w1,12
	b.hi	.none
	cbz	w2,.none
	lsr	w0,w19,25
	add	w0,w0,1980
	bl	days_civil
	movl	x1,86400
	mul	x0,x0,x1
	ubfx	w1,w19,11,5
	mov	x2,3600
	mul	x1,x1,x2
	add	x0,x0,x1
	ubfx	w1,w19,5,6
	mov	x2,60
	mul	x1,x1,x2
	add	x0,x0,x1
	and	w1,w19,31
	add	x0,x0,x1,lsl 1
	b	.out
.none:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- ZIP ----------------------------------------------------------------------------

ZIP_LOCAL	= 0x04034B50
ZIP_CENTRAL	= 0x02014B50
ZIP_END		= 0x06054B50
ZIP_END64	= 0x06064B50
ZIP_LOC64	= 0x07064B50

; x19 = archive -> x0 = 0, erreur
rd_zip:
	ar_enter
	sub	sp,sp,128
	mov	x24,0				; bloc de la fin (fichier)
	mov	x25,0				; bloc du repertoire (fichier)
	ldr	x22,[x19,A_LEN]
	movl	x21,65557
	cmp	x22,x21
	csel	x21,x22,x21,lo			; x21 = octets de fin regardes
	cmp	x21,22
	b.lo	.nofmt
	ldr	x20,[x19,A_MEM]
	cbz	x20,.tfile
	add	x20,x20,x22
	sub	x20,x20,x21
	b	.scan
.tfile:	mov	x0,x21
	bl	ar_alloc
	cbz	x0,.nomem
	mov	x24,x0
	mov	x20,x0
	mov	x0,x19
	sub	x1,x22,x21
	mov	x2,x24
	mov	x3,x21
	bl	src_read
	cbnz	x0,.fail
.scan:	; la fin du repertoire, en partant de la fin
	sub	x23,x21,22
	movl	x3,ZIP_END
.s:	ldr	w0,[x20,x23]
	cmp	w0,w3
	b.eq	.found
.s1:	cbz	x23,.nofmt
	sub	x23,x23,1
	b	.s
.found:	add	x0,x20,x23
	ldrh	w1,[x0,20]			; commentaire
	add	x1,x1,x23
	add	x1,x1,22
	cmp	x1,x21
	b.hi	.s1
	ldrh	w5,[x0,10]			; entrees
	ldr	w6,[x0,12]			; taille du repertoire
	ldr	w7,[x0,16]			; position du repertoire
	sub	x1,x22,x21
	add	x8,x1,x23			; x8 = position de cette fin
	; ZIP64 ?
	cmp	x23,20
	b.lo	.std
	sub	x0,x0,20
	ldr	w1,[x0]
	movl	x2,ZIP_LOC64
	cmp	w1,w2
	b.ne	.std
	ldr	x1,[x0,8]			; position de la fin ZIP64
	cmp	x1,x22
	b.hs	.corrupt
	str	x1,[sp]
	mov	x0,x19
	add	x2,sp,64
	mov	x3,56
	bl	src_read
	cbnz	x0,.fail
	ldr	w0,[sp,64]
	movl	x2,ZIP_END64
	cmp	w0,w2
	b.ne	.corrupt
	ldr	x5,[sp,64+32]
	ldr	x6,[sp,64+40]
	ldr	x7,[sp,64+48]
	ldr	x8,[sp]
.std:	; x5 entrees, x6 taille, x7 position (decalage possible : donnees avant l'archive)
	cmp	x6,x8
	b.hi	.corrupt
	sub	x9,x8,x6			; ou le repertoire est vraiment
	cmp	x9,x7
	b.lo	.corrupt
	sub	x7,x9,x7			; decalage
	str	x7,[sp,32]
	str	x5,[sp,40]
	mov	x21,x9
	mov	x23,x6
	cbz	x23,.done			; archive vide
	ldr	x20,[x19,A_MEM]
	cbz	x20,.cfile
	add	x20,x20,x21
	b	.walk
.cfile:	mov	x0,x23
	bl	ar_alloc
	cbz	x0,.nomem
	mov	x25,x0
	mov	x20,x0
	mov	x0,x19
	mov	x1,x21
	mov	x2,x25
	mov	x3,x23
	bl	src_read
	cbnz	x0,.fail
.walk:	; x20 = lecture, x23 = reste
	cmp	x23,46
	b.lo	.wend
	ldr	w0,[x20]
	movl	x1,ZIP_CENTRAL
	cmp	w0,w1
	b.ne	.wend
	ldrh	w26,[x20,28]			; nom
	ldrh	w1,[x20,30]			; extra
	ldrh	w2,[x20,32]			; commentaire
	add	x3,x26,x1
	add	x3,x3,x2
	add	x3,x3,46
	cmp	x3,x23
	b.hi	.corrupt
	str	x3,[sp,48]
	bl	rd_add
	cbz	x0,.nomem
	mov	x21,x0
	ldrh	w1,[x20,8]			; drapeaux
	ldrh	w2,[x20,10]			; methode
	str	w2,[x21,E_METHOD]
	ldr	w2,[x20,16]
	str	w2,[x21,E_CRC]
	ldr	w2,[x20,20]
	str	x2,[x21,E_PACK]
	ldr	w2,[x20,24]
	str	x2,[x21,E_SIZE]
	ldr	w2,[x20,42]
	str	x2,[x21,E_OFF]
	mov	w2,0
	tbz	w1,0,.w1
	mov	w2,2
.w1:	str	w2,[x21,E_FLAGS]
	ldr	w0,[x20,12]
	bl	dos_unix
	str	x0,[x21,E_TIME]
	; ZIP64 : extra 1
	ldrh	w0,[x20,30]
	add	x1,x20,46
	add	x1,x1,x26
	add	x2,x1,x0			; fin de l'extra
.ex:	add	x3,x1,4
	cmp	x3,x2
	b.hi	.exd
	ldrh	w4,[x1]
	ldrh	w5,[x1,2]
	add	x6,x3,x5
	cmp	x6,x2
	b.hi	.exd
	cmp	w4,1
	b.ne	.exn
	; tailles et position, dans cet ordre, chacune seulement si son champ est plein
	mov	x7,x3
	movl	x8,0xFFFFFFFF
	ldr	x4,[x21,E_SIZE]
	cmp	x4,x8
	b.ne	.e1
	add	x9,x7,8
	cmp	x9,x6
	b.hi	.corrupt
	ldr	x4,[x7],8
	str	x4,[x21,E_SIZE]
.e1:	ldr	x4,[x21,E_PACK]
	cmp	x4,x8
	b.ne	.e2
	add	x9,x7,8
	cmp	x9,x6
	b.hi	.corrupt
	ldr	x4,[x7],8
	str	x4,[x21,E_PACK]
.e2:	ldr	x4,[x21,E_OFF]
	cmp	x4,x8
	b.ne	.exn
	add	x9,x7,8
	cmp	x9,x6
	b.hi	.corrupt
	ldr	x4,[x7],8
	str	x4,[x21,E_OFF]
.exn:	mov	x1,x6
	b	.ex
.exd:	; decalage, bornes
	ldr	x0,[sp,32]
	ldr	x1,[x21,E_OFF]
	adds	x1,x1,x0
	b.cs	.corrupt
	str	x1,[x21,E_OFF]
	cmp	x1,x22
	b.hs	.corrupt
	ldr	x0,[x21,E_PACK]
	cmp	x0,x22
	b.hi	.corrupt
	; le nom
	add	x0,x20,46
	mov	x1,x26
	ldrh	w2,[x20,8]
	ubfx	w2,w2,11,1
	bl	rd_name
	cbz	x0,.nomem
	str	x0,[x21,E_NAME]
	cbz	x1,.wn
	ldr	w0,[x21,E_FLAGS]
	orr	w0,w0,1
	str	w0,[x21,E_FLAGS]
.wn:	ldr	x3,[sp,48]
	add	x20,x20,x3
	sub	x23,x23,x3
	b	.walk
.wend:	; ce qui est annonce doit y etre (au-dela, c'est le champ de 16 bits qui a boucle)
	ldr	x0,[x19,A_N]
	ldr	x1,[sp,40]
	cmp	x0,x1
	b.lo	.corrupt
.done:	mov	x0,AR_FMT_ZIP
	str	x0,[x19,A_FMT]
	mov	x0,0
	b	.fail
.nofmt:	mov	x0,AR_E_FORMAT
	b	.fail
.nomem:	mov	x0,AR_E_NOMEM
	b	.fail
.corrupt:
	mov	x0,AR_E_CORRUPT
.fail:	mov	x20,x0
	mov	x0,x24
	bl	ar_mfree
	mov	x0,x25
	bl	ar_mfree
	mov	x0,x20
	add	sp,sp,128
	ar_leave

; --- tar ------------------------------------------------------------------------------

; x0 = bloc de 512 octets -> x0 = 1 : la somme de l'en-tete tar est bonne
tar_sum_ok:
	mov	x1,0
	mov	x2,0				; somme
	mov	x3,0				; somme signee (vieux tar)
.a:	ldrb	w4,[x0,x1]
	sub	x5,x1,148
	cmp	x5,8
	b.hs	.b
	mov	w4,' '
.b:	add	x2,x2,x4
	sxtb	x4,w4
	add	x3,x3,x4
	add	x1,x1,1
	cmp	x1,512
	b.lo	.a
	; le champ : octal, blancs et 0 autour
	add	x1,x0,148
	mov	x4,0
	mov	x5,0				; chiffres lus
	mov	x6,8
.d:	ldrb	w7,[x1],1
	cmp	w7,' '
	b.eq	.d1
	cbz	w7,.d1
	sub	w7,w7,'0'
	cmp	w7,7
	b.hi	.no
	add	x4,x7,x4,lsl 3
	add	x5,x5,1
	b	.d2
.d1:	cbnz	x5,.dd
.d2:	subs	x6,x6,1
	b.ne	.d
.dd:	cbz	x5,.no
	cmp	x4,x2
	b.eq	.yes
	cmp	x4,x3
	b.eq	.yes
.no:	mov	x0,0
	ret
.yes:	mov	x0,1
	ret

; x0 = bloc de 512 octets -> x0 = 1 : tout a zero
tar_zero:
	mov	x1,0
.a:	ldr	x2,[x0,x1]
	cbnz	x2,.no
	add	x1,x1,8
	cmp	x1,512
	b.lo	.a
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = champ, x1 = octets -> x0 = nombre (octal, ou base 256 de GNU)
tar_num:
	ldrb	w2,[x0]
	tbz	w2,7,.oct
	and	w2,w2,0x7F
	add	x1,x0,x1
	add	x0,x0,1
	mov	x3,x2
.b:	cmp	x0,x1
	b.hs	.be
	ldrb	w2,[x0],1
	orr	x3,x2,x3,lsl 8
	b	.b
.be:	mov	x0,x3
	ret
.oct:	add	x1,x0,x1
	mov	x3,0
.s:	cmp	x0,x1
	b.hs	.be
	ldrb	w2,[x0]
	cmp	w2,' '
	b.ne	.d
	add	x0,x0,1
	b	.s
.d:	cmp	x0,x1
	b.hs	.be
	ldrb	w2,[x0],1
	sub	w2,w2,'0'
	cmp	w2,7
	b.hi	.be
	add	x3,x2,x3,lsl 3
	b	.d

; x0 = cle (chaine), x1 = debut, x2 = fin -> x0 = 1 si le texte [debut, fin[ est cette cle
pax_key:
.a:	ldrb	w3,[x0],1
	cbz	w3,.e
	cmp	x1,x2
	b.hs	.no
	ldrb	w4,[x1],1
	cmp	w3,w4
	b.eq	.a
.no:	mov	x0,0
	ret
.e:	cmp	x1,x2
	b.ne	.no
	mov	x0,1
	ret

; x0 = octets de l'en-tete pax dans A_LONG -> x0 = nom en A_LONG (longueur), x1 = taille (-1 :
; absente), x2 = date (-1 : absente)
tar_pax:
	ar_enter
	ldr	x20,[x19,A_LONG]
	add	x21,x20,x0			; fin
	mov	x22,0				; longueur du nom
	mov	x23,-1
	mov	x24,-1
	mov	x25,x20
.rec:	cmp	x25,x21
	b.hs	.e
	mov	x0,x25
	mov	x1,0
.n:	cmp	x0,x21
	b.hs	.e
	ldrb	w2,[x0],1
	cmp	w2,' '
	b.eq	.n1
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.e
	mov	x3,10
	madd	x1,x1,x3,x2
	b	.n
.n1:	cbz	x1,.e
	sub	x2,x0,x25
	cmp	x1,x2
	b.ls	.e				; (trop court pour contenir sa cle)
	add	x26,x25,x1			; fin de la fiche
	cmp	x26,x21
	b.hi	.e
	mov	x4,x0				; cle
.eq:	cmp	x4,x26
	b.hs	.next
	ldrb	w2,[x4]
	cmp	w2,'='
	b.eq	.key
	add	x4,x4,1
	b	.eq
.key:	mov	x6,x0
	mov	x7,x4
	adr	x0,s_path
	mov	x1,x6
	mov	x2,x7
	bl	pax_key
	cbnz	x0,.path
	adr	x0,s_size
	mov	x1,x6
	mov	x2,x7
	bl	pax_key
	cbnz	x0,.size
	adr	x0,s_mtime
	mov	x1,x6
	mov	x2,x7
	bl	pax_key
	cbnz	x0,.mtime
	b	.next
.path:	add	x1,x7,1				; valeur
	cmp	x1,x26
	b.hs	.next
	sub	x2,x26,x1
	sub	x2,x2,1				; (sans le \n)
	mov	x0,x20
	mov	x22,x2
	bl	ar_memcpy
	strb	wzr,[x20,x22]
	; le nom ecrase le debut : ce qui suit reste plus loin, mais la fiche suivante
	; est intacte puisqu'on recule
	b	.next
.size:	add	x1,x7,1
	mov	x23,0
.sd:	cmp	x1,x26
	b.hs	.next
	ldrb	w2,[x1],1
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.next
	mov	x3,10
	madd	x23,x23,x3,x2
	b	.sd
.mtime:	add	x1,x7,1
	mov	x24,0
.md:	cmp	x1,x26
	b.hs	.next
	ldrb	w2,[x1],1
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.next
	mov	x3,10
	madd	x24,x24,x3,x2
	b	.md
.next:	mov	x25,x26
	b	.rec
.e:	mov	x0,x22
	mov	x1,x23
	mov	x2,x24
	ar_leave

; x19 = archive -> x0 = 0, erreur
rd_tar:
	ar_enter
	sub	sp,sp,512+368
	mov	x20,0				; position
	mov	x21,0				; nom long en attente (octets)
	mov	x22,-1				; taille pax
	mov	x23,-1				; date pax
.next:	ldr	x0,[x19,A_LEN]
	sub	x1,x0,x20
	cbz	x1,.end
	cmp	x1,512
	b.lo	.corrupt
	mov	x0,x19
	mov	x1,x20
	mov	x2,sp
	mov	x3,512
	bl	src_read
	cbnz	x0,.out
	mov	x0,sp
	bl	tar_zero
	cbnz	x0,.end
	mov	x0,sp
	bl	tar_sum_ok
	cbz	x0,.corrupt
	add	x0,sp,124
	mov	x1,12
	bl	tar_num
	mov	x24,x0				; taille
	cmn	x22,1
	csel	x24,x22,x24,ne
	mov	x22,-1
	ldr	x0,[x19,A_LEN]
	sub	x1,x0,x20
	sub	x1,x1,512
	cmp	x24,x1
	b.hi	.corrupt			; tronque
	add	x25,x20,512			; donnees
	add	x0,x24,511
	and	x0,x0,not 511
	add	x20,x25,x0			; en-tete suivant
	; ce type
	ldrb	w0,[sp,156]
	cmp	w0,'L'
	b.eq	.long
	cmp	w0,'x'
	b.eq	.pax
	cmp	w0,'K'
	b.eq	.drop
	cmp	w0,'g'
	b.eq	.next
	cmp	w0,'5'
	b.eq	.entry
	cmp	w0,'0'
	b.eq	.entry
	cmp	w0,'7'
	b.eq	.entry
	cbz	w0,.entry
	mov	x21,0				; liens, peripheriques : laisses
	mov	x23,-1
	b	.next
.drop:	b	.next
.long:	bl	.getlong
	cbnz	x0,.out
	mov	x21,x1
	b	.next
.pax:	bl	.getlong
	cbnz	x0,.out
	mov	x0,x1
	bl	tar_pax
	cbz	x0,.p1
	mov	x21,x0
.p1:	mov	x22,x1
	mov	x23,x2
	b	.next
.getlong:
	; les donnees de l'en-tete special (taille x24, en x25) dans A_LONG -> x0 = 0, x1 = octets
	stp	x29,x30,[sp,-16]!
	ldr	x0,[x19,A_LONG]
	cbnz	x0,.g1
	movl	x0,RD_LONG
	bl	ar_alloc
	cbz	x0,.gn
	str	x0,[x19,A_LONG]
.g1:	mov	x2,x0
	movl	x3,RD_LONG-1
	cmp	x24,x3
	csel	x3,x3,x24,hi
	strb	wzr,[x2,x3]
	mov	x26,x3
	mov	x0,x19
	mov	x1,x25
	bl	src_read
	mov	x1,x26
	ldp	x29,x30,[sp],16
	ret
.gn:	mov	x0,AR_E_NOMEM
	ldp	x29,x30,[sp],16
	ret
.entry:	bl	rd_add
	cbz	x0,.nomem
	mov	x26,x0
	str	x24,[x26,E_SIZE]
	str	x24,[x26,E_PACK]
	str	x25,[x26,E_OFF]
	add	x0,sp,136
	mov	x1,12
	bl	tar_num
	cmn	x23,1
	csel	x0,x23,x0,ne
	mov	x23,-1
	str	x0,[x26,E_TIME]
	ldrb	w0,[sp,156]
	mov	w1,0
	cmp	w0,'5'
	b.ne	.e1
	mov	w1,1
.e1:	str	w1,[x26,E_FLAGS]
	cbz	x21,.short
	ldr	x0,[x19,A_LONG]
	mov	x1,x21
	mov	x21,0
	mov	x2,1
	b	.nm
.short:	; prefixe ustar + nom
	add	x4,sp,512			; construction
	mov	x5,x4
	add	x0,sp,257
	ldr	x0,[x0]
	movl	x1,0x3030007261747375		; "ustar\0" "00"
	cmp	x0,x1
	b.ne	.sn
	add	x1,sp,345
	mov	x2,155
.sp:	cbz	x2,.sp1
	ldrb	w3,[x1],1
	cbz	w3,.sp1
	strb	w3,[x5],1
	sub	x2,x2,1
	b	.sp
.sp1:	cmp	x5,x4
	b.eq	.sn
	mov	w3,'/'
	strb	w3,[x5],1
.sn:	mov	x1,sp
	mov	x2,100
.sn1:	cbz	x2,.sn2
	ldrb	w3,[x1],1
	cbz	w3,.sn2
	strb	w3,[x5],1
	sub	x2,x2,1
	b	.sn1
.sn2:	mov	x0,x4
	sub	x1,x5,x4
	mov	x2,1
.nm:	bl	rd_name
	cbz	x0,.nomem
	str	x0,[x26,E_NAME]
	cbz	x1,.next
	ldr	w0,[x26,E_FLAGS]
	orr	w0,w0,1
	str	w0,[x26,E_FLAGS]
	b	.next
.end:	mov	x0,0
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
	b	.out
.corrupt:
	mov	x0,AR_E_CORRUPT
.out:	add	sp,sp,512+368
	ar_leave

; --- gzip -----------------------------------------------------------------------------

; x19 = archive -> x0 = etat d'inflate, 0 si plus de memoire
rd_infl:
	prologue
	ldr	x0,[x19,A_IF]
	cbnz	x0,.out
	movl	x0,IF_SIZE
	bl	ar_alloc
	str	x0,[x19,A_IF]
.out:	epilogue

; x19 = archive, x0 = etat, x1 = position, x2 = octets, x3 = sortie, x4 = capacite, x5 = puits,
; x6 = enveloppe : pret pour if_run, l'entree venant de la source
rd_ifbegin:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	ldr	x7,[x19,A_MEM]
	cbz	x7,.file
	add	x1,x7,x21
	bl	if_begin
	b	.out
.file:	mov	x1,0
	mov	x2,0
	bl	if_begin
	str	x19,[x20,IF_SRC]
	str	x21,[x20,IF_SRCOFF]
	str	x22,[x20,IF_SRCLEFT]
	mov	x0,IF_IBUF
	add	x0,x20,x0
	stp	x0,x0,[x20,IF_IP]
.out:	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; x19 = archive -> x0 = 0, erreur : un gz : tar.gz, ou un seul fichier
rd_gz:
	ar_enter
	sub	sp,sp,512+SK_SIZE+16
	add	x20,sp,512			; le puits
	mov	x0,SKK_PROBE
	str	x0,[x20,SK_KIND]
	mov	x0,sp
	str	x0,[x20,SK_PTR]
	str	xzr,[x20,SK_POS]
	mov	x0,512
	str	x0,[x20,SK_CAP]
	bl	rd_infl
	cbz	x0,.nomem
	mov	x21,x0
	mov	x1,0
	ldr	x2,[x19,A_LEN]
	mov	x3,0
	mov	x4,0
	mov	x5,x20
	mov	x6,AR_GZIP
	bl	rd_ifbegin
	mov	x0,x21
	bl	if_run
	tbnz	x0,63,.out
	; ISIZE : les 4 derniers octets (taille d'un seul membre)
	mov	x22,0
	ldr	x1,[x19,A_LEN]
	cmp	x1,18
	b.lo	.tail
	sub	x1,x1,4
	mov	x0,x19
	add	x2,x20,SK_SIZE
	str	wzr,[x2]
	mov	x3,4
	bl	src_read
	tbnz	x0,63,.out
	ldr	w22,[x20,SK_SIZE]
.tail:	ldr	x0,[x20,SK_POS]
	cmp	x0,512
	b.lo	.single
	mov	x0,sp
	bl	tar_sum_ok
	cbnz	x0,.tgz
	mov	x0,sp				; un tar vide : que des zeros, par enregistrements
	bl	tar_zero
	cbz	x0,.single
	mov	x1,10240
	udiv	x2,x22,x1
	msub	x2,x2,x1,x22
	cbnz	x2,.single
	cbz	x22,.single
.tgz:	mov	x0,AR_FMT_TGZ
	str	x0,[x19,A_FMT]
	ldr	x0,[x19,A_PROBE]
	cbnz	x0,.ok
	; tout en memoire
	mov	x0,SKK_GROW
	str	x0,[x20,SK_KIND]
	str	xzr,[x20,SK_PTR]
	str	xzr,[x20,SK_POS]
	str	xzr,[x20,SK_CAP]
	cbz	x22,.tgo
	add	x0,x22,1
	str	x0,[x20,SK_CAP]
	bl	ar_alloc
	str	x0,[x20,SK_PTR]
	cbnz	x0,.tgo
	str	xzr,[x20,SK_CAP]
.tgo:	mov	x0,x21
	mov	x1,0
	ldr	x2,[x19,A_LEN]
	mov	x3,0
	mov	x4,0
	mov	x5,x20
	mov	x6,AR_GZIP
	bl	rd_ifbegin
	mov	x0,x21
	bl	if_run
	ldr	x1,[x20,SK_PTR]
	str	x1,[x19,A_INNER]
	tbnz	x0,63,.out
	ldr	x2,[x19,A_LEN]
	str	x2,[x19,A_RAWLEN]
	cbnz	x1,.tg1
	mov	x0,AR_E_CORRUPT
	b	.out
.tg1:	str	x1,[x19,A_MEM]
	ldr	x1,[x20,SK_POS]
	str	x1,[x19,A_LEN]
	bl	rd_tar
	cbnz	x0,.out
	; part de chaque entree dans le fichier compresse
	ldr	x0,[x19,A_N]
	cbz	x0,.ok
	ldr	x1,[x19,A_TBL]
	ldr	x2,[x19,A_LEN]
	ldr	x3,[x19,A_RAWLEN]
.pk:	ldr	x4,[x1,E_SIZE]
	mul	x4,x4,x3
	udiv	x4,x4,x2
	str	x4,[x1,E_PACK]
	add	x1,x1,E_BYTES
	subs	x0,x0,1
	b.ne	.pk
	b	.ok
.single:
	mov	x0,AR_FMT_GZIP
	str	x0,[x19,A_FMT]
	ldr	x0,[x19,A_PROBE]
	cbnz	x0,.ok
	bl	rd_add
	cbz	x0,.nomem
	mov	x23,x0
	str	x22,[x23,E_SIZE]
	ldr	x0,[x19,A_LEN]
	str	x0,[x23,E_PACK]
	mov	w0,8
	str	w0,[x23,E_METHOD]
	ldr	x0,[x21,IF_GZTIME]
	str	x0,[x23,E_TIME]
	; le nom : celui de l'en-tete, sinon le fichier sans .gz, sinon " data "
	mov	x24,IF_GZNAME
	add	x24,x21,x24
	ldrb	w0,[x24]
	cbnz	w0,.nm
	add	x24,x19,A_BASE
	ldrb	w0,[x24]
	cbnz	w0,.nb
	adr	x24,s_data
	b	.nm
.nb:	mov	x0,x24
	bl	str_len
	cmp	x0,3
	b.ls	.nm
	sub	x1,x0,3
	add	x1,x24,x1
	ldrb	w2,[x1]
	cmp	w2,'.'
	b.ne	.nm
	ldrb	w2,[x1,1]
	orr	w2,w2,0x20
	cmp	w2,'g'
	b.ne	.nm
	ldrb	w2,[x1,2]
	orr	w2,w2,0x20
	cmp	w2,'z'
	b.ne	.nm
	strb	wzr,[x1]
.nm:	mov	x0,x24
	bl	str_len
	mov	x1,x0
	mov	x0,x24
	mov	x2,1
	bl	rd_name
	cbz	x0,.nomem
	str	x0,[x23,E_NAME]
.ok:	mov	x0,0
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
.out:	add	sp,sp,512+SK_SIZE+16
	ar_leave

; --- ouverture ------------------------------------------------------------------------

; x0 = source en memoire (0 : fichier), x1 = descripteur, x2 = octets, x3 = chemin (0 : aucun),
; x4 = 1 : le format seulement -> x0 = archive, erreur (descripteur ferme)
rd_open:
	ar_enter
	sub	sp,sp,512
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,x3
	mov	x24,x4
	mov	x0,RD_SIZE
	bl	ar_alloc
	cbz	x0,.nomem0
	mov	x19,x0
	movl	x1,AR_MAGIC_RD
	str	x1,[x19,H_MAGIC]
	str	x20,[x19,A_MEM]
	str	x21,[x19,A_FD]
	str	x22,[x19,A_LEN]
	str	x24,[x19,A_PROBE]
	cbz	x23,.head
	; le nom de base
	mov	x0,x23
	mov	x1,x23
.nb:	ldrb	w2,[x0],1
	cbz	w2,.nb1
	cmp	w2,'/'
	b.eq	.nb2
	cmp	w2,':'
	b.ne	.nb
.nb2:	mov	x1,x0
	b	.nb
.nb1:	add	x2,x19,A_BASE
	mov	x3,255
.nc:	ldrb	w0,[x1],1
	strb	w0,[x2],1
	cbz	w0,.head
	subs	x3,x3,1
	b.ne	.nc
	strb	wzr,[x2]
.head:	; les premiers octets
	mov	x25,512
	cmp	x22,x25
	csel	x25,x22,x25,lo
	str	xzr,[sp]
	mov	x0,x19
	mov	x1,0
	mov	x2,sp
	mov	x3,x25
	bl	src_read
	cbnz	x0,.fail
	ldr	w1,[sp]
	movl	x2,ZIP_LOCAL
	cmp	w1,w2
	b.eq	.zip
	movl	x2,ZIP_END
	cmp	w1,w2
	b.eq	.zip
	movl	x2,0x08074B50			; (archive decoupee, un seul morceau)
	cmp	w1,w2
	b.eq	.zip
	and	w2,w1,0xFFFF
	movz	w3,0x8B1F
	cmp	w2,w3
	b.eq	.gz
	cmp	x25,512
	b.lo	.other
	mov	x0,sp
	bl	tar_sum_ok
	cbnz	x0,.tar
	mov	x0,sp				; que des zeros : un tar vide
	bl	tar_zero
	cbz	x0,.other
	tst	x22,511
	b.ne	.other
	cmp	x22,1024
	b.hs	.tar
.other:	bl	rd_zip				; (ZIP avec des donnees devant : auto-extractible)
	b	.done
.zip:	bl	rd_zip
	b	.done
.gz:	bl	rd_gz
	b	.done
.tar:	mov	x0,AR_FMT_TAR
	str	x0,[x19,A_FMT]
	ldr	x0,[x19,A_PROBE]
	cbnz	x0,.ok
	bl	rd_tar
.done:	cbnz	x0,.fail
.ok:	mov	x0,x19
	b	.out
.fail:	mov	x20,x0
	mov	x0,x19
	bl	ar_close
	mov	x0,x20
	b	.out
.nomem0:
	tbnz	x21,63,.nm1
	mov	x0,x21
	bl	f_fermer
.nm1:	mov	x0,AR_E_NOMEM
.out:	add	sp,sp,512
	ar_leave

; x0 = chemin -> archive
ar_open:
	mov	x1,0
	b	rd_path

; x0 = chemin, x1 = 1 : format seulement -> archive
rd_path:
	bl_kits
	ar_enter
	mov	x19,x0
	mov	x20,x1
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x1,0
	mov	x2,2
	bl	f_position
	tbnz	x0,63,.fail
	mov	x22,x0
	mov	x0,x21
	mov	x1,0
	mov	x2,0
	bl	f_position
	mov	x0,0
	mov	x1,x21
	mov	x2,x22
	mov	x3,x19
	mov	x4,x20
	bl	rd_open
	b	.out
.fail:	mov	x19,x0
	mov	x0,x21
	bl	f_fermer
	mov	x0,x19
.out:	ar_leave

; x0 = tampon, x1 = octets -> archive
ar_open_mem:
	bl_kits
	cbz	x0,.bad
	mov	x2,x1
	mov	x1,-1
	mov	x3,0
	mov	x4,0
	b	rd_open
.bad:	mov	x0,AR_E_INVAL
	ret

; x0 = chemin -> AR_FMT_*, erreur
ar_probe:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x1,1
	bl	rd_path
	tbnz	x0,63,.out
	mov	x19,x0
	ldr	x20,[x19,A_FMT]
	bl	ar_close
	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = archive -> 0
ar_close:
	cbz	x0,.zero
	ar_enter
	rd_handle .bad
	str	xzr,[x19,H_MAGIC]
	ldr	x0,[x19,A_FD]
	tbnz	x0,63,.nf
	bl	f_fermer
.nf:	ldr	x0,[x19,A_TBL]
	bl	ar_mfree
	ldr	x0,[x19,A_IF]
	bl	ar_mfree
	ldr	x0,[x19,A_INNER]
	bl	ar_mfree
	ldr	x0,[x19,A_LONG]
	bl	ar_mfree
	ldr	x20,[x19,A_POOL]
.p:	cbz	x20,.pe
	ldr	x21,[x20]
	mov	x0,x20
	bl	ar_mfree
	mov	x20,x21
	b	.p
.pe:	mov	x0,x19
	bl	ar_mfree
	mov	x0,0
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave
.zero:	ret

; x0 = archive -> AR_FMT_*
ar_format:
	prologue
	str	x19,[sp,-16]!
	rd_handle .bad
	ldr	x0,[x19,A_FMT]
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ldr	x19,[sp],16
	epilogue

; x0 = archive -> nombre d'entrees
ar_count:
	prologue
	str	x19,[sp,-16]!
	rd_handle .bad
	ldr	x0,[x19,A_N]
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ldr	x19,[sp],16
	epilogue

; x0 = archive, x1 = indice, x2 = tampon AR_ENTRY_SIZE -> 0
ar_entry:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x3,x1
	mov	x4,x2
	rd_handle .bad
	mov	x1,x3
	rd_index .bad
	cbz	x4,.bad
	ldr	x0,[x20,E_NAME]
	str	x0,[x4,ARE_NAME]
	ldr	x0,[x20,E_SIZE]
	str	x0,[x4,ARE_SIZE]
	ldr	x0,[x20,E_PACK]
	str	x0,[x4,ARE_PACKED]
	ldr	x0,[x20,E_TIME]
	str	x0,[x4,ARE_TIME]
	ldr	w0,[x20,E_FLAGS]
	and	x1,x0,1
	str	x1,[x4,ARE_DIR]
	ubfx	x1,x0,1,1
	str	x1,[x4,ARE_FLAGS]
	ldr	w0,[x20,E_METHOD]
	str	x0,[x4,ARE_METHOD]
	ldr	w0,[x20,E_CRC]
	str	x0,[x4,ARE_CRC]
	mov	x0,0
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = archive, x1 = indice -> nom
ar_name:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x3,x1
	rd_handle .bad
	mov	x1,x3
	rd_index .bad
	ldr	x0,[x20,E_NAME]
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = archive, x1 = indice -> taille
ar_entry_size:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x3,x1
	rd_handle .bad
	mov	x1,x3
	rd_index .bad
	ldr	x0,[x20,E_SIZE]
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = a, x1 = b : chaines -> x0 = 0 : egales ; x2 = 1 : sans la casse ASCII
str_cmp:
.a:	ldrb	w3,[x0],1
	ldrb	w4,[x1],1
	cbz	x2,.c
	sub	w5,w3,'A'
	cmp	w5,26
	b.hs	.b1
	add	w3,w3,32
.b1:	sub	w5,w4,'A'
	cmp	w5,26
	b.hs	.c
	add	w4,w4,32
.c:	cmp	w3,w4
	b.ne	.no
	cbnz	w3,.a
	mov	x0,0
	ret
.no:	mov	x0,1
	ret

; x0 = archive, x1 = nom -> indice, E_NOENT
ar_find:
	ar_enter
	mov	x3,x1
	rd_handle .bad
	cbz	x3,.bad
	mov	x20,x3
	mov	x21,0				; 0 : exact, 1 : sans la casse
.pass:	ldr	x22,[x19,A_TBL]
	ldr	x23,[x19,A_N]
	mov	x24,0
.e:	cmp	x24,x23
	b.hs	.pn
	ldr	x0,[x22,E_NAME]
	mov	x1,x20
	mov	x2,x21
	bl	str_cmp
	cbz	x0,.hit
	add	x22,x22,E_BYTES
	add	x24,x24,1
	b	.e
.pn:	cbnz	x21,.none
	mov	x21,1
	b	.pass
.hit:	mov	x0,x24
	b	.out
.none:	mov	x0,E_NOENT
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave

; x0 = archive, x1 = indice, x2 = sortie, x3 = capacite, x4 = puits (0 : la sortie) -> octets
rd_entry:
	ar_enter
	stp	x2,x3,[sp,-16]!
	str	x4,[sp,-16]!
	sub	sp,sp,32
	mov	x2,x1
	mov	x24,x4
	mov	x3,x2
	rd_handle .bad
	mov	x1,x3
	rd_index .bad
	ldr	w0,[x20,E_FLAGS]
	tbz	w0,0,.nd
	mov	x0,0
	b	.out
.nd:	tbz	w0,1,.nc
	mov	x0,AR_E_CRYPT
	b	.out
.nc:	ldr	x21,[x20,E_OFF]			; x21 = donnees
	ldr	x22,[x20,E_PACK]		; x22 = octets dans la source
	ldr	w23,[x20,E_METHOD]
	ldr	x0,[x19,A_FMT]
	cmp	x0,AR_FMT_ZIP
	b.eq	.zip
	cmp	x0,AR_FMT_GZIP
	b.ne	.tar
	mov	x21,0
	ldr	x22,[x19,A_LEN]
	b	.defl
.tar:	ldr	x22,[x20,E_SIZE]
	mov	x23,0
	b	.check
.zip:	mov	x0,x19
	mov	x1,x21
	mov	x2,sp
	mov	x3,30
	bl	src_read
	cbnz	x0,.out
	ldr	w0,[sp]
	movl	x1,ZIP_LOCAL
	cmp	w0,w1
	b.ne	.corrupt
	ldrh	w0,[sp,26]
	ldrh	w1,[sp,28]
	add	x21,x21,30
	add	x21,x21,x0
	add	x21,x21,x1
.check:	ldr	x0,[x19,A_LEN]
	cmp	x21,x0
	b.hi	.corrupt
	sub	x0,x0,x21
	cmp	x22,x0
	b.hi	.corrupt
	cbz	x23,.store
	cmp	x23,8
	b.eq	.defl
	mov	x0,AR_E_METHOD
	b	.out
.store:	; stocke
	ldr	x0,[x20,E_SIZE]
	cmp	x0,x22
	b.ne	.corrupt
	ldr	x25,[sp,32+16]			; sortie
	cbnz	x24,.ssink
	ldr	x1,[sp,32+16+8]
	cmp	x22,x1
	b.hi	.space
	mov	x0,x19
	mov	x1,x21
	mov	x2,x25
	mov	x3,x22
	bl	src_read
	cbnz	x0,.out
	mov	x1,x25
	mov	x2,x22
	mov	x25,0
	b	.sum
.ssink:	; vers un puits, par morceaux
	bl	rd_infl
	cbz	x0,.nomem
	mov	x25,x0
	mov	x0,IF_IBUF
	add	x25,x25,x0
	mov	x26,x22
	str	xzr,[sp,16]			; crc
.sl:	cbz	x26,.sd
	mov	x3,IF_ICHUNK
	cmp	x26,x3
	csel	x3,x26,x3,lo
	str	x3,[sp,24]
	mov	x0,x19
	mov	x1,x21
	mov	x2,x25
	bl	src_read
	cbnz	x0,.out
	ldr	x0,[sp,16]
	mov	x1,x25
	ldr	x2,[sp,24]
	bl	ar_crc32
	str	x0,[sp,16]
	mov	x0,x24
	mov	x1,x25
	ldr	x2,[sp,24]
	bl	sink_write
	cmp	x0,AR_STOP
	b.eq	.sstop
	cbnz	x0,.out
	ldr	x2,[sp,24]
	add	x21,x21,x2
	sub	x26,x26,x2
	b	.sl
.sstop:	mov	x0,x22
	b	.out
.sd:	ldr	x0,[sp,16]
	b	.cmp
.sum:	mov	x0,0
	bl	ar_crc32
.cmp:	ldr	x1,[x19,A_FMT]
	cmp	x1,AR_FMT_ZIP
	b.ne	.sok
	ldr	w1,[x20,E_CRC]
	cmp	w0,w1
	b.ne	.crc
.sok:	mov	x0,x22
	b	.out
.defl:	; deflate
	ldr	x0,[x19,A_FMT]
	cmp	x0,AR_FMT_ZIP
	b.ne	.d0
	cbnz	x24,.d0
	ldr	x0,[x20,E_SIZE]
	ldr	x1,[sp,32+16+8]
	cmp	x0,x1
	b.hi	.space
.d0:	bl	rd_infl
	cbz	x0,.nomem
	mov	x25,x0
	mov	x1,x21
	mov	x2,x22
	ldr	x3,[sp,32+16]
	ldr	x4,[sp,32+16+8]
	mov	x5,x24
	mov	x6,AR_RAW
	ldr	x7,[x19,A_FMT]
	cmp	x7,AR_FMT_ZIP
	b.eq	.d1
	mov	x6,AR_GZIP
.d1:	bl	rd_ifbegin
	mov	x0,x25
	bl	if_run
	tbnz	x0,63,.out
	ldr	x1,[x19,A_FMT]
	cmp	x1,AR_FMT_ZIP
	b.ne	.out
	ldr	x1,[x20,E_SIZE]
	cmp	x0,x1
	b.ne	.corrupt
	ldr	w1,[x25,IF_CRC]
	ldr	w2,[x20,E_CRC]
	cmp	w1,w2
	b.ne	.crc
	b	.out
.space:	mov	x0,AR_E_SPACE
	b	.out
.crc:	mov	x0,AR_E_CRC
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
	b	.out
.corrupt:
	mov	x0,AR_E_CORRUPT
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	add	sp,sp,32+16+16
	ar_leave

; x0 = archive, x1 = indice, x2 = tampon, x3 = capacite -> octets
ar_read:
	cbz	x2,.bad
	mov	x4,0
	b	rd_entry
.bad:	mov	x0,AR_E_INVAL
	ret

; x0 = archive, x1 = indice -> 0, l'entree se lit
ar_test:
	prologue
	sub	sp,sp,SK_SIZE
	stp	xzr,xzr,[sp]
	stp	xzr,xzr,[sp,16]
	mov	x4,sp
	mov	x2,0
	mov	x3,0
	bl	rd_entry
	cmp	x0,0
	csel	x0,x0,xzr,lt
	add	sp,sp,SK_SIZE
	epilogue

; x0 = archive, x1 = indice -> tampon
ar_load:
	ar_enter
	mov	x21,x1
	mov	x22,x0
	bl	buf_new
	cbz	x0,.nomem
	mov	x19,x0
	; (une reservation d'apres la taille annoncee, si elle est plausible)
	mov	x0,x22
	mov	x1,x21
	bl	ar_entry_size
	tbnz	x0,63,.res
	mov	x20,x0
	add	x0,x20,1
	movl	x1,1 shl 28
	cmp	x0,x1
	b.hi	.res
	str	x0,[x19,B_SINK+SK_CAP]
	bl	ar_alloc
	str	x0,[x19,B_SINK+SK_PTR]
	cbnz	x0,.res
	str	xzr,[x19,B_SINK+SK_CAP]
.res:	mov	x0,x22
	mov	x1,x21
	mov	x2,0
	mov	x3,0
	add	x4,x19,B_SINK
	bl	rd_entry
	tbnz	x0,63,.fail
	mov	x0,x19
	bl	buf_done
	b	.out
.fail:	mov	x20,x0
	mov	x0,x19
	bl	ar_free
	mov	x0,x20
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
.out:	ar_leave

; x0 = chemin : cree les dossiers parents (l'existant ne gene pas) ; le dernier composant
; n'est cree que si x1 = 1
ar_mkdirs:
	ar_enter
	sub	sp,sp,1024
	mov	x20,x1
	mov	x19,sp				; (copie : le chemin de l'appelant peut etre constant)
	mov	x1,0
.cp:	ldrb	w2,[x0,x1]
	strb	w2,[x19,x1]
	cbz	w2,.cpe
	add	x1,x1,1
	cmp	x1,1023
	b.lo	.cp
	b	.out
.cpe:	mov	x21,x19
.s:	ldrb	w0,[x21]
	cbz	w0,.end
	cmp	w0,'/'
	b.ne	.n
	cmp	x21,x19
	b.eq	.n
	sub	x1,x21,1
	ldrb	w1,[x1]
	cmp	w1,':'
	b.eq	.n
	strb	wzr,[x21]
	mov	x0,x19
	bl	f_mkdir
	mov	w0,'/'
	strb	w0,[x21]
.n:	add	x21,x21,1
	b	.s
.end:	cbz	x20,.out
	mov	x0,x19
	bl	f_mkdir
.out:	mov	x0,0
	add	sp,sp,1024
	ar_leave

; x0 = chemin, x1 = secondes UTC : la date d'un fichier
ar_utime:
	cbz	x1,.ret
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x3,x1
	mov	x2,x0
	mov	x1,V_UTIME
	stp	x1,xzr,[sp]
	stp	x3,xzr,[sp,16]
	str	xzr,[sp,32]
	add	x4,sp,MSG_DATA
	mov	x5,0
.c:	cmp	x5,MSG_TAILLE-MSG_DATA-1
	b.hs	.out
	ldrb	w6,[x2,x5]
	strb	w6,[x4,x5]
	add	x5,x5,1
	cbnz	w6,.c
	str	w5,[sp,MSG_LEN]
	mov	x0,sp
	bl	ar_vfs
.out:	add	sp,sp,MSG_TAILLE
	ldp	x19,x20,[sp],16
	epilogue
.ret:	ret

; x0 = message rempli : au service vfs -> resultat
ar_vfs:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,s_vfs
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.absent
	mov	x1,x19
	sys	SYS_SVC_CALL
	b	.out
.absent:
	mov	x0,E_NOENT
.out:	ldr	x19,[sp],16
	epilogue

; x0 = archive, x1 = indice, x2 = chemin -> octets ecrits
ar_extract:
	bl_kits
	ar_enter
	sub	sp,sp,SK_SIZE
	mov	x3,x1
	mov	x4,x2
	rd_handle .bad
	mov	x1,x3
	rd_index .bad
	mov	x21,x1
	mov	x22,x4
	cbz	x22,.bad
	ldr	w0,[x20,E_FLAGS]
	tbz	w0,0,.file
	mov	x0,x22
	mov	x1,1
	bl	ar_mkdirs
	mov	x0,0
	b	.out
.file:	mov	x0,x22
	mov	x1,0
	bl	ar_mkdirs
	mov	x0,x22
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x23,x0
	mov	x1,SKK_FD
	str	x1,[sp,SK_KIND]
	str	x0,[sp,SK_PTR]
	str	xzr,[sp,SK_POS]
	str	xzr,[sp,SK_CAP]
	mov	x0,x19
	mov	x1,x21
	mov	x2,0
	mov	x3,0
	mov	x4,sp
	bl	rd_entry
	mov	x24,x0
	mov	x0,x23
	bl	f_fermer
	tbz	x24,63,.okf
	mov	x0,x22				; rien de faux sur la carte
	bl	f_unlink
	mov	x0,x24
	b	.out
.okf:	mov	x0,x22
	ldr	x1,[x20,E_TIME]
	bl	ar_utime
	mov	x0,x24
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	add	sp,sp,SK_SIZE
	ar_leave

; x0 = nom -> x0 = 1 : un composant .. (ou vide)
name_unsafe:
	ldrb	w1,[x0]
	cbz	w1,.yes
.c:	ldrb	w1,[x0]
	cmp	w1,'.'
	b.ne	.skip
	ldrb	w1,[x0,1]
	cmp	w1,'.'
	b.ne	.skip
	ldrb	w1,[x0,2]
	cbz	w1,.yes
	cmp	w1,'/'
	b.eq	.yes
.skip:	ldrb	w1,[x0],1
	cbz	w1,.no
	cmp	w1,'/'
	b.ne	.skip
	b	.c
.no:	mov	x0,0
	ret
.yes:	mov	x0,1
	ret

; x0 = archive, x1 = dossier -> fichiers ecrits
ar_extract_all:
	bl_kits
	ar_enter
	mov	x3,x1
	rd_handle .bad
	cbz	x3,.bad
	mov	x20,x3
	mov	x0,4096
	bl	ar_alloc
	cbz	x0,.nomem
	mov	x21,x0
	mov	x22,0				; indice
	mov	x23,0				; fichiers
	mov	x0,x20
	mov	x1,1
	bl	ar_mkdirs
.e:	ldr	x0,[x19,A_N]
	cmp	x22,x0
	b.hs	.done
	mov	x0,x19
	mov	x1,x22
	bl	ar_name
	mov	x24,x0
	bl	name_unsafe
	cbnz	x0,.nx
	; dossier/nom
	mov	x0,x21
	mov	x1,x20
.c1:	ldrb	w2,[x1],1
	cbz	w2,.c2
	strb	w2,[x0],1
	sub	x3,x0,x21
	cmp	x3,3000
	b.hs	.nx
	b	.c1
.c2:	ldrb	w2,[x0,-1]
	cmp	w2,'/'
	b.eq	.c3
	mov	w2,'/'
	strb	w2,[x0],1
.c3:	mov	x1,x24
.c4:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbz	w2,.c5
	sub	x3,x0,x21
	cmp	x3,4000
	b.hs	.nx
	b	.c4
.c5:	mov	x0,x19
	mov	x1,x22
	mov	x2,x21
	bl	ar_extract
	tbnz	x0,63,.fail
	ldr	x1,[x19,A_TBL]
	mov	x2,E_BYTES
	madd	x1,x22,x2,x1
	ldr	w1,[x1,E_FLAGS]
	tbnz	w1,0,.nx
	add	x23,x23,1
.nx:	add	x22,x22,1
	b	.e
.done:	mov	x24,x23
	b	.free
.fail:	mov	x24,x0
.free:	mov	x0,x21
	bl	ar_mfree
	mov	x0,x24
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave

s_path		db 'path',0
s_size		db 'size',0
s_mtime		db 'mtime',0
s_vfs		db 'vfs',0
s_data		db 'data',0
		align	4
cp437_tab	dh 0xC7,0xFC,0xE9,0xE2,0xE4,0xE0,0xE5,0xE7,0xEA,0xEB,0xE8,0xEF,0xEE,0xEC,0xC4,0xC5
		dh 0xC9,0xE6,0xC6,0xF4,0xF6,0xF2,0xFB,0xF9,0xFF,0xD6,0xDC,0xA2,0xA3,0xA5,0x20A7,0x192
		dh 0xE1,0xED,0xF3,0xFA,0xF1,0xD1,0xAA,0xBA,0xBF,0x2310,0xAC,0xBD,0xBC,0xA1,0xAB,0xBB
		dh 0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510
		dh 0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567
		dh 0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580
		dh 0x3B1,0xDF,0x393,0x3C0,0x3A3,0x3C3,0xB5,0x3C4,0x3A6,0x398,0x3A9,0x3B4,0x221E,0x3C6,0x3B5,0x2229
		dh 0x2261,0xB1,0x2265,0x2264,0x2320,0x2321,0xF7,0x2248,0xB0,0x2219,0xB7,0x221A,0x207F,0xB2,0x25A0,0xA0
		align	4
