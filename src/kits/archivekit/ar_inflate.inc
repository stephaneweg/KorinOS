; ============================================================================
;  ar_inflate.inc - la decompression deflate (RFC 1951), zlib (RFC 1950), gzip
;  (RFC 1952) d'ArchiveKit
;
;  if_run decode tout d'un coup : l'entree est un bloc de memoire (ou tiree par
;  morceaux d'une archive, if_pull), la sortie un tampon de l'appelant (IF_SINK
;  = 0 : l'historique est ce tampon, et rien ne deborde : AR_E_SPACE) ou un
;  puits (fenetre de 32 Ko d'historique + IF_OCHUNK, passee au puits quand elle
;  est pleine). Le puits peut arreter le decodage (AR_STOP).
;
;  Tables : une consultation sur IF_LBITS (litteraux et longueurs) ou IF_DBITS
;  bits (distances) ; les codes plus longs sont decodes en canonique, bit a bit.
;  Bits : registre de 64 bits rempli par mots tant que l'entree a 8 octets
;  devant elle, octet par octet ensuite ; apres la fin de l'entree on lit des
;  zeros comptes (IF_OVER) : un flux tronque est vu a la fin du bloc au plus
;  tard, ou apres IF_OVERMAX octets inventes.
;
;  Registres du decodeur : x19 etat, x20 bits, x21 nombre de bits, x22 entree,
;  x23 fin de l'entree, x24 fin - 8, x25 sortie, x26 fin de la sortie,
;  x27 table des litteraux, x28 table des distances.
; ============================================================================

IF_LBITS	= 10
IF_DBITS	= 9
IF_ICHUNK	= 65536
IF_OCHUNK	= 262144
IF_HISTORY	= 32768
IF_OCAP		= IF_HISTORY + IF_OCHUNK
IF_OVERMAX	= 16

IF_IP		= 0
IF_IEND		= 8
IF_OVER		= 16			; octets inventes apres la fin de l'entree
IF_OBASE	= 24			; debut de l'historique
IF_OEND		= 32
IF_FLUSHED	= 40			; ce qui precede a deja eu sa somme et son puits
IF_SINK		= 48
IF_TOTAL	= 56			; octets deja passes
IF_CRC		= 64
IF_ADLER	= 72
IF_WRAP		= 80			; enveloppe (AR_RAW..AR_AUTO)
IF_WANT		= 88			; 1 CRC, 2 Adler du membre en cours
IF_SRC		= 96			; archive d'ou tirer l'entree (0 : tout est deja la)
IF_SRCOFF	= 104
IF_SRCLEFT	= 112
IF_GZTIME	= 120			; date du premier membre gzip
IF_FINAL	= 128
IF_MEMBERS	= 136
IF_LCOUNT	= 256			; u16[16]
IF_DCOUNT	= IF_LCOUNT + 32
IF_LSYM		= IF_DCOUNT + 32	; u16[288]
IF_DSYM		= IF_LSYM + 288*2	; u16[32]
IF_LENS		= IF_DSYM + 32*2	; u8[320 + 2]
IF_GZNAME	= 1536			; nom du premier membre gzip (256)
IF_LTAB		= 2048			; u16[1 shl IF_LBITS]
IF_DTAB		= IF_LTAB + (1 shl IF_LBITS)*2	; u16[1 shl IF_DBITS]
IF_IBUF		= 8192
IF_OBUF		= IF_IBUF + IF_ICHUNK + 16
IF_SIZE		= IF_OBUF + IF_OCAP + 16
assert IF_LENS + 322 <= IF_GZNAME
assert IF_DTAB + (1 shl IF_DBITS)*2 <= IF_IBUF

AR_STOP		= 1			; le puits a ce qu'il voulait

; x0 = etat, x1 = entree, x2 = octets, x3 = sortie (0 : celle du puits), x4 = capacite,
; x5 = puits (0 : la sortie), x6 = enveloppe
if_begin:
	add	x2,x1,x2
	stp	x1,x2,[x0,IF_IP]
	str	xzr,[x0,IF_OVER]
	str	x6,[x0,IF_WRAP]
	str	x5,[x0,IF_SINK]
	str	xzr,[x0,IF_SRC]
	str	xzr,[x0,IF_TOTAL]
	str	xzr,[x0,IF_GZTIME]
	strb	wzr,[x0,IF_GZNAME]
	str	xzr,[x0,IF_MEMBERS]
	cbnz	x5,.sink
	add	x4,x3,x4
	b	.set
.sink:	movl	x3,IF_OBUF
	add	x3,x0,x3
	movl	x4,IF_OCAP
	add	x4,x3,x4
.set:	stp	x3,x4,[x0,IF_OBASE]
	str	x3,[x0,IF_FLUSHED]
	ret

; x19 = etat : l'entree continue dans l'archive -> x0 = octets recus (0 : plus rien)
; x22, x23, x24 mis a jour
if_pull:
	prologue
	ldr	x0,[x19,IF_SRC]
	cbz	x0,.none
	ldr	x3,[x19,IF_SRCLEFT]
	cbz	x3,.none
	mov	x2,IF_ICHUNK
	cmp	x3,x2
	csel	x3,x2,x3,hi
	stp	x3,x9,[sp,-16]!			; (les registres x9-x12 sont gardes pour les appelants)
	stp	x10,x11,[sp,-16]!
	str	x12,[sp,-16]!
	mov	x2,IF_IBUF
	add	x2,x19,x2
	ldr	x1,[x19,IF_SRCOFF]
	bl	src_read			; x0 = archive, x1 = position, x2 = tampon, x3 = octets
	ldr	x12,[sp],16
	ldp	x10,x11,[sp],16
	ldp	x3,x9,[sp],16
	tbnz	x0,63,.none
	ldr	x1,[x19,IF_SRCOFF]
	add	x1,x1,x3
	str	x1,[x19,IF_SRCOFF]
	ldr	x1,[x19,IF_SRCLEFT]
	sub	x1,x1,x3
	str	x1,[x19,IF_SRCLEFT]
	mov	x22,IF_IBUF
	add	x22,x19,x22
	add	x23,x22,x3
	sub	x24,x23,8
	mov	x0,x3
	epilogue
.none:	mov	x0,0
	epilogue

; remplit x20 octet par octet jusqu'a plus de 56 bits -> x0 = 0, 1 : trop d'octets inventes
if_fill:
	prologue
.b:	cmp	x21,56
	b.hi	.ok
	cmp	x22,x23
	b.lo	.get
	bl	if_pull
	cbnz	x0,.b
	ldr	x0,[x19,IF_OVER]
	add	x0,x0,1
	str	x0,[x19,IF_OVER]
	cmp	x0,IF_OVERMAX
	b.hi	.bad
	add	x21,x21,8
	b	.b
.get:	ldrb	w0,[x22],1
	lsl	x0,x0,x21
	orr	x20,x20,x0
	add	x21,x21,8
	b	.b
.ok:	mov	x0,0
	epilogue
.bad:	mov	x0,1
	epilogue

; -> w0 = prochain octet de l'entree (apres alignement), negatif : fin de l'entree
if_byte:
	prologue
	and	x0,x21,7
	lsr	x20,x20,x0
	sub	x21,x21,x0
	ldr	x1,[x19,IF_OVER]
	lsl	x1,x1,3
	cmp	x21,x1
	b.ls	.ib
.have:	and	x0,x20,0xFF
	lsr	x20,x20,8
	sub	x21,x21,8
	epilogue
.ib:	cbnz	x1,.none
	mov	x20,0				; (rien de valable dans les bits)
	mov	x21,0
.ic:	cmp	x22,x23
	b.lo	.id
	bl	if_pull
	cbnz	x0,.ic
.none:	mov	x0,-1
	epilogue
.id:	ldrb	w0,[x22],1
	epilogue

; -> x0 = 0 : il reste de l'entree (un autre membre gzip), 1 : c'est fini
if_more:
	prologue
	mov	x0,0
	ldr	x1,[x19,IF_OVER]
	lsl	x1,x1,3
	and	x2,x21,not 7
	cmp	x2,x1
	b.hi	.out
	cmp	x22,x23
	b.lo	.out
	bl	if_pull
	cmp	x0,0
	cset	x0,eq
.out:	epilogue

; somme et puits pour ce qui n'est pas encore passe ; glisse la fenetre si x0 = 1
; -> x0 = 0, ou la reponse du puits
if_flush:
	prologue
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	str	x0,[sp,-16]!
	ldr	x1,[x19,IF_FLUSHED]
	sub	x2,x25,x1
	cbz	x2,.sl
	ldr	x3,[x19,IF_TOTAL]
	add	x3,x3,x2
	str	x3,[x19,IF_TOTAL]
	str	x25,[x19,IF_FLUSHED]
	ldr	x0,[x19,IF_WANT]
	cmp	x0,1
	b.ne	.ad
	ldr	x0,[x19,IF_CRC]
	stp	x1,x2,[sp,-16]!
	bl	ar_crc32
	str	x0,[x19,IF_CRC]
	ldp	x1,x2,[sp],16
	b	.si
.ad:	cmp	x0,2
	b.ne	.si
	ldr	x0,[x19,IF_ADLER]
	stp	x1,x2,[sp,-16]!
	bl	ar_adler32
	str	x0,[x19,IF_ADLER]
	ldp	x1,x2,[sp],16
.si:	ldr	x0,[x19,IF_SINK]
	cbz	x0,.ok
	bl	sink_write
	cbnz	x0,.out
.sl:	ldr	x0,[sp]
	cbz	x0,.ok
	ldr	x0,[x19,IF_SINK]
	cbz	x0,.ok
	; ne garder que l'historique
	ldr	x0,[x19,IF_OBASE]
	sub	x2,x25,x0
	movl	x3,IF_HISTORY
	subs	x1,x2,x3
	b.ls	.ok
	add	x1,x0,x1			; source
	mov	x2,x3
	add	x25,x0,x3
	str	x25,[x19,IF_FLUSHED]
	bl	ar_memcpy
.ok:	mov	x0,0
.out:	add	sp,sp,16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	epilogue

; x0 = bits du code (u8 par symbole), x1 = symboles, x2 = comptes (u16[16]), x3 = symboles
; tries (u16), x4 = table, x5 = bits de la table, x6 = 1 : code incomplet a un seul symbole
; permis -> x0 = 0, 1 : code invalide
if_build:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	sub	sp,sp,32			; decalages u16[16]
	stp	xzr,xzr,[x2]
	stp	xzr,xzr,[x2,16]
	mov	x7,0
.c:	ldrb	w8,[x0,x7]
	ldrh	w9,[x2,x8,lsl 1]
	add	w9,w9,1
	strh	w9,[x2,x8,lsl 1]
	add	x7,x7,1
	cmp	x7,x1
	b.lo	.c
	; code valide ?
	mov	x20,1				; places
	mov	x7,1
	mov	x21,0				; longueur maximale
.v:	ldrh	w8,[x2,x7,lsl 1]
	cbz	w8,.v1
	mov	x21,x7
.v1:	lsl	x20,x20,1
	subs	x20,x20,x8
	b.mi	.bad
	add	x7,x7,1
	cmp	x7,16
	b.lo	.v
	cbz	x20,.vok
	; incomplet : seulement rien du tout, ou un symbole de 1 bit
	cbz	x21,.vok0
	cmp	x21,1
	b.ne	.bad
	cbz	x6,.bad
.vok0:	cbz	x6,.bad
.vok:	; decalages et symboles tries
	strh	wzr,[sp,2]
	mov	x7,1
	mov	w8,0
.o:	strh	w8,[sp,x7,lsl 1]
	ldrh	w9,[x2,x7,lsl 1]
	add	w8,w8,w9
	add	x7,x7,1
	cmp	x7,16
	b.lo	.o
	mov	x7,0
.s:	ldrb	w8,[x0,x7]
	cbz	w8,.s1
	ldrh	w9,[sp,x8,lsl 1]
	add	w10,w9,1
	strh	w10,[sp,x8,lsl 1]
	strh	w7,[x3,x9,lsl 1]
.s1:	add	x7,x7,1
	cmp	x7,x1
	b.lo	.s
	; table : vide, puis les codes assez courts
	mov	x7,1
	lsl	x20,x7,x5			; entrees
	mov	x7,0
.z:	strh	wzr,[x4,x7,lsl 1]
	add	x7,x7,1
	cmp	x7,x20
	b.lo	.z
	mov	x7,1				; longueur
	mov	w8,0				; code
	mov	x9,0				; indice dans les symboles tries
.t:	cmp	x7,x5
	b.hi	.ok
	ldrh	w10,[x2,x7,lsl 1]
.t1:	cbz	w10,.t3
	rbit	w11,w8
	mov	w12,32
	sub	w12,w12,w7
	lsr	w11,w11,w12			; code inverse
	ldrh	w12,[x3,x9,lsl 1]
	lsl	w12,w12,4
	orr	w12,w12,w7			; symbole << 4 | longueur
	mov	x13,1
	lsl	x13,x13,x7			; pas
.t2:	strh	w12,[x4,x11,lsl 1]
	add	x11,x11,x13
	cmp	x11,x20
	b.lo	.t2
	add	w8,w8,1
	add	x9,x9,1
	sub	w10,w10,1
	b	.t1
.t3:	lsl	w8,w8,1
	add	x7,x7,1
	b	.t
.ok:	mov	x0,0
	b	.out
.bad:	mov	x0,1
.out:	add	sp,sp,32
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; decodage canonique, bit a bit, d'un code long (au moins 15 bits dans x20) :
; x0 = comptes, x1 = symboles tries -> w0 = symbole, negatif : code invalide
if_canon:
	mov	x2,0				; code
	mov	x3,0				; premier
	mov	x4,0				; indice
	mov	x5,1				; longueur
	mov	x6,x20
.l:	and	x7,x6,1
	lsr	x6,x6,1
	orr	x2,x2,x7
	ldrh	w7,[x0,x5,lsl 1]
	sub	x8,x2,x7
	cmp	x8,x3
	b.lt	.hit
	add	x4,x4,x7
	add	x3,x3,x7
	lsl	x3,x3,1
	lsl	x2,x2,1
	add	x5,x5,1
	cmp	x5,15
	b.ls	.l
	mov	x0,-1
	ret
.hit:	sub	x2,x2,x3
	add	x4,x4,x2
	ldrh	w0,[x1,x4,lsl 1]
	lsr	x20,x20,x5
	sub	x21,x21,x5
	ret

; les tables des codes fixes -> x0 = 0
if_fixed:
	prologue
	add	x0,x19,IF_LENS
	mov	x1,0
.a:	mov	w2,8
	cmp	x1,144
	b.lo	.s
	mov	w2,9
	cmp	x1,256
	b.lo	.s
	mov	w2,7
	cmp	x1,280
	b.lo	.s
	mov	w2,8
.s:	strb	w2,[x0,x1]
	add	x1,x1,1
	cmp	x1,288
	b.lo	.a
	mov	x1,288
	add	x2,x19,IF_LCOUNT
	add	x3,x19,IF_LSYM
	mov	x4,x27
	mov	x5,IF_LBITS
	mov	x6,0
	bl	if_build
	cbnz	x0,.out
	add	x0,x19,IF_LENS
	mov	x1,0
	mov	w2,5
.d:	strb	w2,[x0,x1]
	add	x1,x1,1
	cmp	x1,32
	b.lo	.d
	mov	x1,32
	add	x2,x19,IF_DCOUNT
	add	x3,x19,IF_DSYM
	mov	x4,x28
	mov	x5,IF_DBITS
	mov	x6,0
	bl	if_build
.out:	epilogue

; l'en-tete d'un bloc dynamique -> x0 = 0, erreur
if_dynamic:
	prologue
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	bl	if_need
	cbnz	x0,.corrupt
	and	x9,x20,31
	add	x9,x9,257			; HLIT
	ubfx	x10,x20,5,5
	add	x10,x10,1			; HDIST
	ubfx	x11,x20,10,4
	add	x11,x11,4			; HCLEN
	lsr	x20,x20,14
	sub	x21,x21,14
	cmp	x9,286
	b.hi	.corrupt
	cmp	x10,30
	b.hi	.corrupt
	add	x0,x19,IF_LENS
	mov	x1,0
.z:	strb	wzr,[x0,x1]
	add	x1,x1,1
	cmp	x1,19
	b.lo	.z
	mov	x12,0
.cl:	cmp	x12,x11
	b.hs	.cle
	bl	if_need
	cbnz	x0,.corrupt
	and	x1,x20,7
	lsr	x20,x20,3
	sub	x21,x21,3
	adr	x0,df_clorder
	ldrb	w0,[x0,x12]
	add	x2,x19,IF_LENS
	strb	w1,[x2,x0]
	add	x12,x12,1
	b	.cl
.cle:	add	x0,x19,IF_LENS
	mov	x1,19
	add	x2,x19,IF_DCOUNT
	add	x3,x19,IF_DSYM
	mov	x4,x28
	mov	x5,7				; (les longueurs tiennent dans la table)
	mov	x6,0
	bl	if_build
	cbnz	x0,.corrupt
	; les longueurs des deux codes
	add	x11,x9,x10			; total
	mov	x12,0				; indice
.ln:	cmp	x12,x11
	b.hs	.lne
	bl	if_need
	cbnz	x0,.corrupt
	and	x0,x20,127
	ldrh	w0,[x28,x0,lsl 1]
	cbz	w0,.corrupt
	and	w1,w0,15
	lsr	x20,x20,x1
	sub	x21,x21,x1
	lsr	w0,w0,4
	add	x2,x19,IF_LENS
	cmp	w0,16
	b.hs	.rep
	strb	w0,[x2,x12]
	add	x12,x12,1
	b	.ln
.rep:	mov	w3,0				; valeur repetee
	cmp	w0,16
	b.ne	.r0
	cbz	x12,.corrupt
	sub	x3,x12,1
	ldrb	w3,[x2,x3]
	and	x4,x20,3
	lsr	x20,x20,2
	sub	x21,x21,2
	add	x4,x4,3
	b	.rp
.r0:	cmp	w0,17
	b.ne	.r1
	and	x4,x20,7
	lsr	x20,x20,3
	sub	x21,x21,3
	add	x4,x4,3
	b	.rp
.r1:	and	x4,x20,127
	lsr	x20,x20,7
	sub	x21,x21,7
	add	x4,x4,11
.rp:	add	x5,x12,x4
	cmp	x5,x11
	b.hi	.corrupt
.rw:	strb	w3,[x2,x12]
	add	x12,x12,1
	subs	x4,x4,1
	b.ne	.rw
	b	.ln
.lne:	add	x0,x19,IF_LENS
	mov	x1,256
	ldrb	w1,[x0,x1]
	cbz	w1,.corrupt			; pas de fin de bloc
	; distances d'abord : leurs longueurs suivent celles des litteraux
	add	x0,x19,IF_LENS
	add	x0,x0,x9
	mov	x1,x10
	add	x2,x19,IF_DCOUNT
	add	x3,x19,IF_DSYM
	mov	x4,x28
	mov	x5,IF_DBITS
	mov	x6,1
	bl	if_build
	cbnz	x0,.corrupt
	add	x0,x19,IF_LENS
	mov	x1,x9
	add	x2,x19,IF_LCOUNT
	add	x3,x19,IF_LSYM
	mov	x4,x27
	mov	x5,IF_LBITS
	mov	x6,1
	bl	if_build
	cbnz	x0,.corrupt
	b	.out
.corrupt:
	mov	x0,AR_E_CORRUPT
.out:	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	epilogue

; au moins 48 bits dans x20 -> x0 = 0, 1 : plus d'entree
if_need:
	cmp	x21,48
	b.lo	.fill
	mov	x0,0
	ret
.fill:	b	if_fill

; un bloc stocke -> x0 = 0, erreur
if_stored:
	prologue
	stp	x9,x10,[sp,-16]!
	; LEN, NLEN, en passant par if_byte (aligne)
	bl	if_byte
	tbnz	x0,63,.corrupt
	mov	x9,x0
	bl	if_byte
	tbnz	x0,63,.corrupt
	orr	x9,x9,x0,lsl 8
	bl	if_byte
	tbnz	x0,63,.corrupt
	mov	x10,x0
	bl	if_byte
	tbnz	x0,63,.corrupt
	orr	x10,x10,x0,lsl 8
	eor	x10,x10,x9
	movz	x0,0xFFFF
	cmp	x10,x0
	b.ne	.corrupt
	; ce qui reste dans le registre de bits
.bb:	cbz	x9,.ok
	cmp	x21,8
	b.lo	.bulk
	bl	if_byte
	tbnz	x0,63,.corrupt
	bl	.put
	cbnz	x0,.out
	sub	x9,x9,1
	b	.bb
.bulk:	mov	x20,0
	mov	x21,0
.bl:	cbz	x9,.ok
	cmp	x22,x23
	b.lo	.b1
	bl	if_pull
	cbnz	x0,.bl
	b	.corrupt
.b1:	cmp	x25,x26
	b.lo	.b2
	mov	x0,1
	bl	if_flush_room
	cbnz	x0,.out
.b2:	sub	x2,x23,x22
	cmp	x2,x9
	csel	x2,x9,x2,hi
	sub	x3,x26,x25
	cmp	x2,x3
	csel	x2,x3,x2,hi
	mov	x10,x2
	mov	x0,x25
	mov	x1,x22
	bl	ar_memcpy
	add	x25,x25,x10
	add	x22,x22,x10
	sub	x9,x9,x10
	b	.bl
.ok:	mov	x0,0
	b	.out
.corrupt:
	mov	x0,AR_E_CORRUPT
.out:	ldp	x9,x10,[sp],16
	epilogue
; w0 = octet a sortir -> x0 = 0, erreur
.put:	prologue
	str	x0,[sp,-16]!
	cmp	x25,x26
	b.lo	.p1
	bl	if_flush_room
	cbnz	x0,.p2
.p1:	ldr	x0,[sp]
	strb	w0,[x25],1
	mov	x0,0
.p2:	add	sp,sp,16
	epilogue

; la sortie est pleine : de la place, ou AR_E_SPACE sans puits -> x0 = 0, erreur
if_flush_room:
	prologue
	ldr	x0,[x19,IF_SINK]
	cbz	x0,.full
	mov	x0,1
	bl	if_flush
	epilogue
.full:	mov	x0,AR_E_SPACE
	epilogue

; tous les blocs d'un flux deflate -> x0 = 0, erreur
if_stream:
	prologue
.block:	mov	x0,0
	bl	if_need
	cbnz	x0,.corrupt
	and	x0,x20,1
	str	x0,[x19,IF_FINAL]
	ubfx	x1,x20,1,2
	lsr	x20,x20,3
	sub	x21,x21,3
	cbz	x1,.stored
	cmp	x1,1
	b.eq	.fixed
	cmp	x1,2
	b.ne	.corrupt
	bl	if_dynamic
	cbnz	x0,.out
	b	.dec
.fixed:	bl	if_fixed
	cbnz	x0,.corrupt
	b	.dec
.stored:
	bl	if_stored
	cbnz	x0,.out
	b	.next
; --- decodage d'un bloc
.dec:	cmp	x22,x24
	b.hs	.rs
	ldr	x0,[x22]
	lsl	x0,x0,x21
	orr	x20,x20,x0
	mov	x1,63
	sub	x1,x1,x21
	add	x22,x22,x1,lsr 3
	orr	x21,x21,56
.rd:	and	x0,x20,(1 shl IF_LBITS)-1
	ldrh	w1,[x27,x0,lsl 1]
	cbz	w1,.lslow
	and	w2,w1,15
	lsr	x20,x20,x2
	sub	x21,x21,x2
	lsr	w3,w1,4
.sym:	cmp	w3,256
	b.hs	.nlit
	cmp	x25,x26
	b.hs	.ofull
.olit:	strb	w3,[x25],1
	; (deux de plus sans recharger : au moins 36 bits restent)
	and	x0,x20,(1 shl IF_LBITS)-1
	ldrh	w1,[x27,x0,lsl 1]
	cmp	w1,256 shl 4
	b.hs	.dec
	cbz	w1,.dec
	cmp	x25,x26
	b.hs	.dec
	and	w2,w1,15
	lsr	x20,x20,x2
	sub	x21,x21,x2
	lsr	w3,w1,4
	strb	w3,[x25],1
	and	x0,x20,(1 shl IF_LBITS)-1
	ldrh	w1,[x27,x0,lsl 1]
	cmp	w1,256 shl 4
	b.hs	.dec
	cbz	w1,.dec
	cmp	x25,x26
	b.hs	.dec
	and	w2,w1,15
	lsr	x20,x20,x2
	sub	x21,x21,x2
	lsr	w3,w1,4
	strb	w3,[x25],1
	b	.dec
.rs:	bl	if_fill
	cbnz	x0,.corrupt
	b	.rd
.lslow:	add	x0,x19,IF_LCOUNT
	add	x1,x19,IF_LSYM
	bl	if_canon
	tbnz	x0,63,.corrupt
	mov	w3,w0
	b	.sym
.ofull:	stp	x3,xzr,[sp,-16]!
	bl	if_flush_room
	ldp	x3,xzr,[sp],16
	cbnz	x0,.out
	b	.olit
.nlit:	b.eq	.eob
	sub	w3,w3,257
	cmp	w3,29
	b.hs	.corrupt
	adr	x0,if_lbase
	ldrh	w4,[x0,x3,lsl 1]
	adr	x0,if_lext
	ldrb	w5,[x0,x3]
	mov	x6,1
	lsl	x6,x6,x5
	sub	x6,x6,1
	and	x6,x6,x20
	lsr	x20,x20,x5
	sub	x21,x21,x5
	add	x4,x4,x6			; longueur
	; distance
	and	x0,x20,(1 shl IF_DBITS)-1
	ldrh	w1,[x28,x0,lsl 1]
	cbz	w1,.dslow
	and	w2,w1,15
	lsr	x20,x20,x2
	sub	x21,x21,x2
	lsr	w3,w1,4
.dsym:	cmp	w3,30
	b.hs	.corrupt
	adr	x0,if_dbase
	ldrh	w6,[x0,x3,lsl 1]
	adr	x0,if_dext
	ldrb	w5,[x0,x3]
	mov	x7,1
	lsl	x7,x7,x5
	sub	x7,x7,1
	and	x7,x7,x20
	lsr	x20,x20,x5
	sub	x21,x21,x5
	add	x6,x6,x7			; distance
.chk:	ldr	x0,[x19,IF_OBASE]
	sub	x0,x25,x0
	cmp	x6,x0
	b.hi	.corrupt
	add	x0,x25,x4
	add	x0,x0,16
	cmp	x0,x26
	b.hi	.near
	sub	x1,x25,x6
	cmp	x6,8
	b.lo	.short
.c8:	ldr	x2,[x1],8
	str	x2,[x25],8
	subs	x4,x4,8
	b.hi	.c8
	add	x25,x25,x4
	b	.dec
.short:	ldrb	w2,[x1],1
	strb	w2,[x25],1
	subs	x4,x4,1
	b.ne	.short
	b	.dec
.dslow:	stp	x4,xzr,[sp,-16]!
	add	x0,x19,IF_DCOUNT
	add	x1,x19,IF_DSYM
	bl	if_canon
	ldp	x4,xzr,[sp],16
	tbnz	x0,63,.corrupt
	mov	w3,w0
	b	.dsym
.near:	; pres de la fin de la sortie
	ldr	x0,[x19,IF_SINK]
	cbz	x0,.exact
	stp	x4,x6,[sp,-16]!
	mov	x0,1
	bl	if_flush
	ldp	x4,x6,[sp],16
	cbnz	x0,.out
	b	.chk
.exact:	add	x0,x25,x4
	cmp	x0,x26
	b.hi	.space
	sub	x1,x25,x6
	b	.short
.space:	mov	x0,AR_E_SPACE
	b	.out
.eob:	ldr	x0,[x19,IF_OVER]
	lsl	x0,x0,3
	cmp	x0,x21
	b.hi	.corrupt			; des bits inventes ont servi
.next:	ldr	x0,[x19,IF_FINAL]
	cbz	x0,.block
	mov	x0,0
	b	.out
.corrupt:
	mov	x0,AR_E_CORRUPT
.out:	epilogue

; les 4 octets qui suivent, petit-boutien (x0 = 0) ou grand (1) -> x0 = valeur, negatif : fin
if_u32:
	prologue
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	mov	x9,x0
	mov	x10,0
	mov	x11,0
.a:	bl	if_byte
	tbnz	x0,63,.out
	cbnz	x9,.be
	lsl	x0,x0,x11
	orr	x10,x10,x0
	b	.b
.be:	orr	x10,x0,x10,lsl 8
.b:	add	x11,x11,8
	cmp	x11,32
	b.lo	.a
	mov	x0,x10
.out:	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	epilogue

; l'en-tete gzip (apres 1F 8B : deja lus) -> x0 = 0, erreur
if_gzhead:
	prologue
	stp	x9,x10,[sp,-16]!
	bl	if_byte
	cmp	x0,8
	b.ne	.corrupt
	bl	if_byte
	tbnz	x0,63,.corrupt
	mov	x9,x0				; drapeaux
	tst	x9,0xE0
	b.ne	.corrupt
	mov	x0,0
	bl	if_u32
	tbnz	x0,63,.corrupt
	ldr	x1,[x19,IF_MEMBERS]
	cbnz	x1,.nt
	str	x0,[x19,IF_GZTIME]
.nt:	bl	if_byte
	bl	if_byte
	tbnz	x0,63,.corrupt
	tbz	x9,2,.nx			; FEXTRA
	bl	if_byte
	tbnz	x0,63,.corrupt
	mov	x10,x0
	bl	if_byte
	tbnz	x0,63,.corrupt
	orr	x10,x10,x0,lsl 8
.xl:	cbz	x10,.nx
	bl	if_byte
	tbnz	x0,63,.corrupt
	sub	x10,x10,1
	b	.xl
.nx:	tbz	x9,3,.nn			; FNAME
	mov	x10,0
.nl:	bl	if_byte
	tbnz	x0,63,.corrupt
	ldr	x1,[x19,IF_MEMBERS]
	cbnz	x1,.nm
	cmp	x10,255
	b.hs	.nm
	add	x1,x19,IF_GZNAME
	strb	w0,[x1,x10]
	add	x10,x10,1
.nm:	cbnz	x0,.nl
	ldr	x1,[x19,IF_MEMBERS]
	cbnz	x1,.nn
	add	x1,x19,IF_GZNAME
	strb	wzr,[x1,x10]
.nn:	tbz	x9,4,.nc			; FCOMMENT
.cl:	bl	if_byte
	tbnz	x0,63,.corrupt
	cbnz	x0,.cl
.nc:	tbz	x9,1,.ok			; FHCRC
	bl	if_byte
	bl	if_byte
	tbnz	x0,63,.corrupt
.ok:	mov	x0,0
	b	.out
.corrupt:
	mov	x0,AR_E_CORRUPT
.out:	ldp	x9,x10,[sp],16
	epilogue

; x0 = etat prepare (if_begin) -> x0 = octets decodes, erreur (AR_STOP : le puits s'est arrete)
if_run:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,0
	mov	x21,0
	ldp	x22,x23,[x19,IF_IP]
	sub	x24,x23,8
	ldp	x25,x26,[x19,IF_OBASE]
	add	x27,x19,IF_LTAB
	add	x28,x19,IF_DTAB
	str	xzr,[x19,IF_CRC]
	mov	x0,1
	str	x0,[x19,IF_ADLER]
	str	xzr,[x19,IF_WANT]
	ldr	x0,[x19,IF_WRAP]
	cbz	x0,.raw
	bl	if_byte				; premiers octets
	tbnz	x0,63,.corrupt
	mov	x9,x0
	bl	if_byte
	tbnz	x0,63,.corrupt
	mov	x10,x0
	ldr	x0,[x19,IF_WRAP]
	cmp	x0,AR_AUTO
	b.ne	.w
	mov	x0,AR_ZLIB
	cmp	x9,0x1F
	b.ne	.wa
	cmp	x10,0x8B
	b.ne	.wa
	mov	x0,AR_GZIP
.wa:	str	x0,[x19,IF_WRAP]
.w:	cmp	x0,AR_GZIP
	b.eq	.gz
	cmp	x0,AR_ZLIB
	b.ne	.corrupt
	; zlib
	and	x1,x9,15
	cmp	x1,8
	b.ne	.corrupt
	lsr	x1,x9,4
	cmp	x1,7
	b.hi	.corrupt
	tbnz	x10,5,.corrupt			; FDICT
	lsl	x1,x9,8
	orr	x1,x1,x10
	mov	x2,31
	udiv	x3,x1,x2
	msub	x3,x3,x2,x1
	cbnz	x3,.corrupt
	mov	x0,2
	str	x0,[x19,IF_WANT]
	bl	if_stream
	cbnz	x0,.out
	mov	x0,0
	bl	if_flush
	cbnz	x0,.out
	mov	x0,1
	bl	if_u32
	tbnz	x0,63,.corrupt
	ldr	x1,[x19,IF_ADLER]
	cmp	x0,x1
	b.ne	.crc
	b	.done
.raw:	mov	x0,1
	str	x0,[x19,IF_WANT]
	bl	if_stream
	cbnz	x0,.out
	mov	x0,0
	bl	if_flush
	cbnz	x0,.out
	b	.done
.gz:	; gzip : un ou plusieurs membres
	cmp	x9,0x1F
	b.ne	.corrupt
	cmp	x10,0x8B
	b.ne	.corrupt
.mem:	stp	x9,x10,[sp,-16]!
	bl	if_gzhead
	ldp	x9,x10,[sp],16
	cbnz	x0,.out
	mov	x0,1
	str	x0,[x19,IF_WANT]
	str	xzr,[x19,IF_CRC]
	ldr	x0,[x19,IF_TOTAL]
	str	x0,[sp,-16]!			; octets au debut du membre
	bl	if_stream
	cbnz	x0,.gzo
	mov	x0,0
	bl	if_flush
	cbnz	x0,.gzo
	mov	x0,0
	bl	if_u32
	tbnz	x0,63,.gzc
	ldr	x1,[x19,IF_CRC]
	cmp	x0,x1
	b.ne	.gzcrc
	mov	x0,0
	bl	if_u32
	tbnz	x0,63,.gzc
	ldr	x1,[x19,IF_TOTAL]
	ldr	x2,[sp]
	sub	x1,x1,x2
	mov	w1,w1
	cmp	x0,x1
	b.ne	.gzcrc
	add	sp,sp,16
	ldr	x0,[x19,IF_MEMBERS]
	add	x0,x0,1
	str	x0,[x19,IF_MEMBERS]
	bl	if_more
	cbnz	x0,.done
	bl	if_byte				; un autre membre, ou des octets de bourrage
	tbnz	x0,63,.done
	cmp	x0,0x1F
	b.ne	.done
	bl	if_byte
	cmp	x0,0x8B
	b.ne	.done
	b	.mem
.gzo:	add	sp,sp,16
	b	.out
.gzc:	add	sp,sp,16
	b	.corrupt
.gzcrc:	add	sp,sp,16
.crc:	mov	x0,AR_E_CRC
	b	.out
.corrupt:
	mov	x0,AR_E_CORRUPT
	b	.out
.done:	ldr	x0,[x19,IF_TOTAL]
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

if_lbase	dh 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
if_dbase	dh 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
if_lext		db 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
if_dext		db 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
		align	4
