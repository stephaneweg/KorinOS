; dostime.inc - dates FAT et exFAT (docs/FICHIERS.md, dates) : date DOS << 16 | heure DOS,
; en heure locale comme sous Windows (2 s de precision) ; secondes Unix pour les programmes.
; SYS_CLOCK_GET : l'horloge du noyau. Utilise par : dos_date_forcee (rb 8, a zero).
DOS_1980	= 315532800			; le 1er janvier 1980

; x0 = secondes Unix -> x0 = date << 16 | heure (avant 1980 : le 1er janvier 1980)
unix_vers_dos:
	movl	x1,DOS_1980
	cmp	x0,x1
	csel	x0,x1,x0,lo
	movl	x1,86400
	udiv	x2,x0,x1			; jours depuis 1970
	msub	x3,x2,x1,x0			; secondes du jour
	movl	x4,719468			; jour -> annee, mois, jour (depuis le 1er mars de l'an 0)
	add	x2,x2,x4
	movl	x4,146097
	udiv	x5,x2,x4			; periode de 400 ans
	msub	x6,x5,x4,x2			; jour dans la periode
	mov	x4,1460
	udiv	x7,x6,x4
	movl	x4,36524
	udiv	x8,x6,x4
	movl	x4,146096
	udiv	x9,x6,x4
	sub	x7,x6,x7
	add	x7,x7,x8
	sub	x7,x7,x9
	mov	x4,365
	udiv	x7,x7,x4			; annee dans la periode
	mov	x4,400
	mul	x8,x5,x4
	add	x8,x8,x7			; annee
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
	add	x6,x6,1				; jour du mois
	add	x10,x9,3
	sub	x4,x9,9
	cmp	x9,10
	csel	x9,x10,x4,lo			; mois, de 1 a 12
	cmp	x9,2
	b.hi	.an
	add	x8,x8,1
.an:	sub	x8,x8,1980
	lsl	x8,x8,9
	orr	x8,x8,x9,lsl 5
	orr	x8,x8,x6
	mov	x4,3600
	udiv	x5,x3,x4
	msub	x3,x5,x4,x3
	mov	x4,60
	udiv	x6,x3,x4
	msub	x3,x6,x4,x3
	lsr	x3,x3,1
	lsl	x5,x5,11
	orr	x5,x5,x6,lsl 5
	orr	x5,x5,x3
	orr	x0,x5,x8,lsl 16
	ret

; w0 = date << 16 | heure -> x0 = secondes Unix (sans date : 0)
dos_vers_unix:
	lsr	w1,w0,16
	cbz	w1,.zero
	and	w2,w0,0xFFFF			; heure
	ubfx	w3,w1,9,7
	add	w3,w3,1980			; annee
	ubfx	w4,w1,5,4			; mois
	and	w5,w1,31			; jour
	cmp	w4,1
	csinc	w4,w4,wzr,hs			; (0 sur un disque : janvier)
	cmp	w5,1
	csinc	w5,w5,wzr,hs
	cmp	w4,2
	b.hi	.mars
	sub	w3,w3,1
	add	w4,w4,12
.mars:	sub	w4,w4,3				; mois, depuis mars
	mov	w6,400
	udiv	w7,w3,w6			; periode de 400 ans
	msub	w8,w7,w6,w3			; annee dans la periode
	mov	w6,153
	mul	w9,w4,w6
	add	w9,w9,2
	mov	w6,5
	udiv	w9,w9,w6
	add	w9,w9,w5
	sub	w9,w9,1				; jour dans l'annee
	mov	w6,365
	mul	w10,w8,w6
	add	w10,w10,w8,lsr 2
	mov	w6,100
	udiv	w11,w8,w6
	sub	w10,w10,w11
	add	w10,w10,w9			; jour dans la periode
	movl	x6,146097
	mul	x7,x7,x6
	add	x7,x7,x10
	movl	x6,719468
	sub	x7,x7,x6			; jours depuis 1970
	movl	x6,86400
	mul	x7,x7,x6
	ubfx	w3,w2,11,5
	mov	w6,3600
	mul	w3,w3,w6
	ubfx	w4,w2,5,6
	mov	w6,60
	mul	w4,w4,w6
	and	w5,w2,31
	add	w3,w3,w4
	add	w3,w3,w5,lsl 1
	add	x0,x7,x3
	ret
.zero:	mov	x0,0
	ret

; w0 = date DOS -> x0 = secondes UTC (sans date : 0)
dos_vers_utc:
	prologue
	bl	dos_vers_unix
	cbz	x0,.out
	str	x0,[sp,-16]!
	mov	x0,1
	sys	SYS_CLOCK_GET
	ldr	x1,[sp],16
	sub	x0,x1,x0
.out:	epilogue

; x0 = secondes UTC -> x0 = date DOS
utc_vers_dos:
	prologue
	str	x0,[sp,-16]!
	mov	x0,1
	sys	SYS_CLOCK_GET
	ldr	x1,[sp],16
	add	x0,x1,x0
	bl	unix_vers_dos
	epilogue

; -> x0 = la date a ecrire : maintenant, ou celle d'un utime (dos_date_forcee)
dos_maintenant:
	prologue
	adr	x0,dos_date_forcee
	ldr	x0,[x0]
	cbnz	x0,.out
	mov	x0,2
	sys	SYS_CLOCK_GET
	bl	unix_vers_dos
.out:	epilogue
