; ============================================================================
;  ar_deflate.inc - la compression deflate (RFC 1951) d'ArchiveKit
;
;  Un flux : df_begin, df_feed (autant de fois qu'il faut), df_finish ; ce qui
;  sort va au puits (ar_sink.inc). La memoire entre dans une fenetre de 32 Ko
;  d'historique + DF_CHUNK ; quand elle est pleine, df_run la comprime, puis
;  df_slide ramene l'historique au debut.
;
;  Recherche des correspondances : chaines de hachage (3 octets), a positions
;  absolues sur 32 bits (DF_BASE + decalage) : rien a remettre a zero entre
;  deux entrees d'une archive ni apres un glissement, une position trop vieille
;  se voit a sa distance, et un retour a zero du compteur est sans danger (le
;  candidat est verifie sur les octets). Paresseuse a partir du niveau 4,
;  gourmande en dessous, memes reglages que zlib (good, lazy, nice, chain).
;
;  Blocs : au plus DF_TOKENS jetons (ou une fenetre) ; pour chacun on calcule le
;  cout en Huffman dynamique, en codes fixes et en bloc stocke, et on prend le
;  moins cher : des donnees incompressibles ne grossissent que de 5 octets par
;  bloc. Longueurs de codes : Moffat-Katajainen (Huffman minimal, sur place),
;  puis limite a 15 (ou 7) bits par correction de l'inegalite de Kraft.
; ============================================================================

DF_HISTORY	= 32768
DF_CHUNK	= 131072
DF_WINCAP	= DF_HISTORY + DF_CHUNK
DF_LOOK		= 262			; ce qu'il faut devant soi pour chercher (hors de la fin)
DF_MAXDIST	= 32767			; (prev est un anneau de 32768 entrees)
DF_TOKENS	= 32767
DF_HBITS	= 15
DF_OSIZE	= 65536
DF_OSLACK	= 64			; marge d'un jeton

DF_SINK		= 0
DF_LEVEL	= 8
DF_GOOD		= 16
DF_LAZY		= 24
DF_NICE		= 32
DF_CHAINN	= 40
DF_POS		= 48			; prochain octet a traiter (decalage dans la fenetre)
DF_END		= 56			; fin des octets recus
DF_BLK		= 64			; debut du bloc en cours
DF_FLOOR	= 72			; on ne remonte pas avant (debut du flux)
DF_BASE		= 80			; position absolue = BASE + decalage
DF_NTOK		= 88
DF_BB		= 96			; bits en attente
DF_BC		= 104
DF_OP		= 112			; octets dans le tampon de sortie
DF_WRAP		= 120
DF_CRC		= 128
DF_ADLER	= 136
DF_TOTAL	= 144			; octets recus
DF_ERR		= 152			; premiere erreur du puits
DF_FINAL	= 160
DF_BEND		= 168			; fin du bloc en cours d'ecriture
DF_LFREQ	= 256			; u32[288]
DF_DFREQ	= DF_LFREQ + 288*4	; u32[32]
DF_BFREQ	= DF_DFREQ + 32*4	; u32[19]
DF_LLEN		= DF_BFREQ + 20*4	; u8[288]
DF_DLEN		= DF_LLEN + 288		; u8[32]
DF_BLEN		= DF_DLEN + 32		; u8[20]
DF_LTAB		= DF_BLEN + 20		; u32[288] : code inverse | longueur << 16
DF_DTAB		= DF_LTAB + 288*4
DF_BTAB		= DF_DTAB + 32*4
DF_FLTAB	= DF_BTAB + 20*4	; les codes fixes
DF_FDTAB	= DF_FLTAB + 288*4
DF_SEQ		= DF_FDTAB + 32*4	; u8[320] : longueurs mises bout a bout
DF_RLE		= DF_SEQ + 320		; u16[320] : symbole | extra << 8
DF_SORT		= DF_RLE + 320*2	; u64[288] : frequence << 16 | symbole
DF_WORK		= DF_SORT + 288*8	; u32[288]
DF_CNT		= DF_WORK + 288*4	; u32[34]
DF_TOK		= 16384			; u32[DF_TOKENS+1]
DF_HEAD		= DF_TOK + (DF_TOKENS+1)*4	; u32[1 shl DF_HBITS]
DF_PREV		= DF_HEAD + (1 shl DF_HBITS)*4	; u32[32768]
DF_OBUF		= DF_PREV + 32768*4
DF_WIN		= DF_OBUF + DF_OSIZE + DF_OSLACK
DF_SIZE		= DF_WIN + DF_WINCAP + 32
assert DF_CNT + 34*4 <= DF_TOK

; x0 = puits -> x0 = etat (0 : plus de memoire)
df_new:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	movl	x0,DF_SIZE
	bl	ar_alloc
	cbz	x0,.out
	str	x19,[x0,DF_SINK]
	mov	x19,x0
	mov	x1,1
	str	x1,[x19,DF_BASE]
	bl	df_fixed
	mov	x0,x19
.out:	ldr	x19,[sp],16
	epilogue

; x19 = etat : les tables des codes fixes
df_fixed:
	prologue
	mov	x0,0
	add	x1,x19,DF_LLEN
.a:	mov	w2,8
	cmp	x0,144
	b.lo	.s
	mov	w2,9
	cmp	x0,256
	b.lo	.s
	mov	w2,7
	cmp	x0,280
	b.lo	.s
	mov	w2,8
.s:	strb	w2,[x1,x0]
	add	x0,x0,1
	cmp	x0,288
	b.lo	.a
	add	x0,x19,DF_LLEN
	mov	x1,288
	add	x2,x19,DF_FLTAB
	bl	df_codes
	mov	x0,0
	add	x1,x19,DF_DLEN
	mov	w2,5
.d:	strb	w2,[x1,x0]
	add	x0,x0,1
	cmp	x0,32
	b.lo	.d
	add	x0,x19,DF_DLEN
	mov	x1,32
	movl	x2,DF_FDTAB
	add	x2,x19,x2
	bl	df_codes
	epilogue

; x0 = etat, x1 = niveau 0..9, x2 = enveloppe : un nouveau flux (l'etat peut resservir)
df_begin:
	ldr	x3,[x0,DF_BASE]			; (les vieilles positions tombent hors de portee)
	ldr	x4,[x0,DF_END]
	add	x3,x3,x4
	add	x3,x3,1
	str	x3,[x0,DF_BASE]
	str	x1,[x0,DF_LEVEL]
	str	x2,[x0,DF_WRAP]
	adr	x3,df_config
	add	x3,x3,x1,lsl 3
	ldrh	w4,[x3]
	ldrh	w5,[x3,2]
	ldrh	w6,[x3,4]
	ldrh	w7,[x3,6]
	str	x4,[x0,DF_GOOD]
	str	x5,[x0,DF_LAZY]
	str	x6,[x0,DF_NICE]
	str	x7,[x0,DF_CHAINN]
	str	xzr,[x0,DF_POS]
	str	xzr,[x0,DF_END]
	str	xzr,[x0,DF_BLK]
	str	xzr,[x0,DF_FLOOR]
	str	xzr,[x0,DF_NTOK]
	str	xzr,[x0,DF_BB]
	str	xzr,[x0,DF_BC]
	str	xzr,[x0,DF_OP]
	str	xzr,[x0,DF_CRC]
	mov	x1,1
	str	x1,[x0,DF_ADLER]
	str	xzr,[x0,DF_TOTAL]
	str	xzr,[x0,DF_ERR]
	ret

; x0 = etat, x1 = donnees, x2 = octets -> x0 = 0, erreur
df_feed:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbz	x21,.ok
	ldr	x0,[x19,DF_TOTAL]
	add	x0,x0,x21
	str	x0,[x19,DF_TOTAL]
	ldr	x0,[x19,DF_CRC]
	mov	x1,x20
	mov	x2,x21
	bl	ar_crc32
	str	x0,[x19,DF_CRC]
	ldr	x0,[x19,DF_WRAP]
	cmp	x0,AR_ZLIB
	b.ne	.loop
	ldr	x0,[x19,DF_ADLER]
	mov	x1,x20
	mov	x2,x21
	bl	ar_adler32
	str	x0,[x19,DF_ADLER]
.loop:	cbz	x21,.ok
	movl	x0,DF_WINCAP
	ldr	x1,[x19,DF_END]
	sub	x22,x0,x1
	cmp	x22,x21
	csel	x22,x21,x22,hi
	movl	x0,DF_WIN
	add	x0,x19,x0
	add	x0,x0,x1
	mov	x1,x20
	mov	x2,x22
	bl	ar_memcpy
	add	x20,x20,x22
	sub	x21,x21,x22
	ldr	x0,[x19,DF_END]
	add	x0,x0,x22
	str	x0,[x19,DF_END]
	movl	x1,DF_WINCAP
	cmp	x0,x1
	b.lo	.loop
	mov	x0,0
	bl	df_run
	cbnz	x0,.out
	bl	df_slide
	b	.loop
.ok:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat -> x0 = 0, erreur : tout est comprime et passe au puits, dernier bloc
; marque ; le flux s'arrete sur un octet entier
df_finish:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,1
	bl	df_run
	cbnz	x0,.out
	bl	df_align
	bl	df_oflush
	ldr	x0,[x19,DF_ERR]
.out:	ldr	x19,[sp],16
	epilogue

; x19 = etat : garde l'historique, ramene le reste au debut (tout ce qui precede
; DF_POS est deja dans un bloc ferme)
df_slide:
	prologue
	ldr	x0,[x19,DF_POS]
	movl	x1,DF_HISTORY
	subs	x3,x0,x1			; de combien on glisse
	b.ls	.out
	movl	x4,DF_WIN
	add	x4,x19,x4
	ldr	x2,[x19,DF_END]
	sub	x5,x2,x3			; nouvelle fin
	str	x5,[x19,DF_END]
	ldr	x5,[x19,DF_POS]
	sub	x5,x5,x3
	str	x5,[x19,DF_POS]
	str	x5,[x19,DF_BLK]
	ldr	x5,[x19,DF_BASE]
	add	x5,x5,x3
	str	x5,[x19,DF_BASE]
	ldr	x5,[x19,DF_FLOOR]
	subs	x5,x5,x3
	csel	x5,xzr,x5,lo
	str	x5,[x19,DF_FLOOR]
	sub	x2,x2,x3			; octets a garder
	add	x1,x4,x3
	mov	x0,x4
	bl	ar_memcpy
.out:	epilogue

; --- recherche -----------------------------------------------------------------

; insere la position x21 -> w0 = candidat precedent (absolu)
macro df_insert {
	ldr	w1,[x20,x21]
	and	w1,w1,0xFFFFFF
	movz	w0,0xA7BD
	movk	w0,0x1E35,lsl 16
	mul	w1,w1,w0
	lsr	w1,w1,32-DF_HBITS
	add	w2,w27,w21
	ldr	w0,[x23,x1,lsl 2]
	str	w2,[x23,x1,lsl 2]
	and	w3,w2,0x7FFF
	str	w0,[x24,x3,lsl 2]
}

; w0 = candidat, x1 = meilleure longueur connue, x2 = essais ; x20 fenetre,
; x21 position, x27 base -> x0 = longueur, x1 = distance (0 : rien de mieux).
; Lit jusqu'a 8 octets au-dela de la fin (la fenetre a sa marge), coupe ensuite.
df_match:
	ldr	x3,[x19,DF_END]
	sub	x3,x3,x21			; octets devant soi
	mov	x4,258
	cmp	x3,x4
	csel	x3,x4,x3,hi			; x3 = longueur maximale
	mov	x10,0				; distance trouvee
	mov	x11,x1				; meilleure
	cmp	x11,x3
	b.hs	.done
	ldr	x5,[x19,DF_FLOOR]
	sub	x5,x21,x5
	mov	x4,DF_MAXDIST
	cmp	x5,x4
	csel	x5,x4,x5,hi			; x5 = distance maximale
	ldr	x6,[x19,DF_NICE]
	cmp	x6,x3
	csel	x6,x3,x6,hi
	add	x7,x20,x21			; ici
	add	w12,w27,w21			; position absolue
.next:	sub	w13,w12,w0			; distance
	sub	w14,w13,1
	cmp	x14,x5
	b.hs	.done				; (0 ou trop loin)
	sub	x15,x7,x13
	add	x16,x11,x7
	sub	x16,x16,1
	ldrh	w17,[x16]			; les deux octets qui feraient mieux
	add	x16,x15,x11
	sub	x16,x16,1
	ldrh	w16,[x16]
	cmp	w17,w16
	b.ne	.skip
	mov	x16,0
.cmp:	ldr	x17,[x7,x16]
	ldr	x14,[x15,x16]
	eor	x17,x17,x14
	cbnz	x17,.diff
	add	x16,x16,8
	cmp	x16,x3
	b.lo	.cmp
	mov	x16,x3
	b	.len
.diff:	rbit	x17,x17
	clz	x17,x17
	add	x16,x16,x17,lsr 3
	cmp	x16,x3
	csel	x16,x3,x16,hi
.len:	cmp	x16,x11
	b.ls	.skip
	mov	x11,x16
	mov	x10,x13
	cmp	x11,x6
	b.hs	.done
.skip:	subs	x2,x2,1
	b.eq	.done
	and	w0,w0,0x7FFF
	ldr	w0,[x24,x0,lsl 2]
	b	.next
.done:	mov	x0,x11
	mov	x1,x10
	ret

; x0 = 1 : c'est la fin du flux -> x0 = 0, erreur. Comprime jusqu'a DF_END (moins
; DF_LOOK si le flux continue) ; ferme le bloc en cours.
df_run:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	str	x28,[sp,-16]!
	str	x0,[x19,DF_FINAL]
	movl	x20,DF_WIN
	add	x20,x19,x20
	ldr	x21,[x19,DF_POS]
	ldr	x22,[x19,DF_END]
	cbnz	x0,.lim
	subs	x22,x22,DF_LOOK
	csel	x22,xzr,x22,lo
.lim:	ldr	x0,[x19,DF_LEVEL]
	cbz	x0,.store
	movl	x23,DF_HEAD
	add	x23,x19,x23
	movl	x24,DF_PREV
	add	x24,x19,x24
	mov	x25,DF_TOK
	add	x25,x19,x25
	ldr	x27,[x19,DF_BASE]
	mov	x26,2				; longueur precedente
	mov	x28,0				; distance precedente
	cmp	x0,4
	b.lo	.greedy

; --- paresseuse : une correspondance attend de voir si la suivante fait mieux
.none:						; aucun octet en attente
	cmp	x21,x22
	b.hs	.close
	ldr	x0,[x19,DF_END]
	add	x1,x21,2
	cmp	x1,x0
	b.hs	.nolook
	df_insert
	mov	x1,2
	ldr	x2,[x19,DF_CHAINN]
	bl	df_match
	mov	x26,x0
	mov	x28,x1
	cmp	x26,3
	b.ne	.n1
	cmp	x28,4096
	b.ls	.n1
	mov	x26,2
.n1:	add	x21,x21,1
	b	.wait
.nolook:
	mov	x26,2
	add	x21,x21,1

.wait:						; l'octet x21-1 attend
	cmp	x21,x22
	b.hs	.lastlit
	ldr	x0,[x19,DF_END]
	add	x1,x21,2
	cmp	x1,x0
	b.hs	.nomatch
	df_insert
	ldr	x3,[x19,DF_LAZY]
	cmp	x26,x3
	b.hs	.nolazy
	mov	x1,x26
	ldr	x2,[x19,DF_CHAINN]
	ldr	x3,[x19,DF_GOOD]
	cmp	x26,x3
	b.lo	.w1
	lsr	x2,x2,2
.w1:	bl	df_match
	b	.w2
.nolazy:
	mov	x0,2
	mov	x1,0
	b	.w3
.nomatch:
	mov	x0,2
	mov	x1,0
	b	.w3
.w2:	cbz	x1,.w4				; rien de mieux
	cmp	x0,3
	b.ne	.w3
	cmp	x1,4096
	b.ls	.w3
.w4:	mov	x0,2
	mov	x1,0
.w3:	cmp	x26,3
	b.lo	.wlit
	cmp	x0,x26
	b.hi	.wlit
	; la correspondance precedente est gardee : elle commence en x21-1
	sub	x0,x26,3
	orr	x0,x0,x28,lsl 8
	str	w0,[x25],4
	sub	x4,x21,1
	add	x4,x4,x26			; fin de la correspondance
	ldr	x5,[x19,DF_END]
	subs	x5,x5,2				; dernier + 1 qu'on peut hacher
	csel	x5,xzr,x5,lo
	cmp	x4,x5
	csel	x5,x4,x5,lo
	add	x21,x21,1
.ins:	cmp	x21,x5
	b.hs	.insd
	df_insert
	add	x21,x21,1
	b	.ins
.insd:	mov	x21,x4
	mov	x26,2
	mov	x0,x25
	bl	.tokchk
	b	.none
.wlit:	mov	x26,x0				; un litteral part, l'octet courant attend
	mov	x28,x1
	sub	x2,x21,1
	ldrb	w2,[x20,x2]
	str	w2,[x25],4
	add	x21,x21,1
	sub	x0,x21,1			; (consomme : jusqu'a l'octet en attente)
	bl	.tokat
	b	.wait
.lastlit:
	sub	x2,x21,1
	ldrb	w2,[x20,x2]
	str	w2,[x25],4
	b	.close

; --- gourmande (niveaux 1 a 3)
.greedy:
.g:	cmp	x21,x22
	b.hs	.close
	ldr	x0,[x19,DF_END]
	add	x1,x21,2
	cmp	x1,x0
	b.hs	.glit
	df_insert
	mov	x1,2
	ldr	x2,[x19,DF_CHAINN]
	bl	df_match
	cbz	x1,.glit
	cmp	x0,3
	b.ne	.gm
	cmp	x1,4096
	b.hi	.glit
.gm:	sub	x2,x0,3
	orr	x2,x2,x1,lsl 8
	str	w2,[x25],4
	add	x4,x21,x0			; fin
	ldr	x5,[x19,DF_LAZY]		; (longueur maximale inseree)
	cmp	x0,x5
	b.hi	.gskip
	ldr	x5,[x19,DF_END]
	subs	x5,x5,2
	csel	x5,xzr,x5,lo
	cmp	x4,x5
	csel	x5,x4,x5,lo
	add	x21,x21,1
.gi:	cmp	x21,x5
	b.hs	.gskip
	df_insert
	add	x21,x21,1
	b	.gi
.gskip:	mov	x21,x4
	mov	x0,x25
	bl	.tokchk
	b	.g
.glit:	ldrb	w2,[x20,x21]
	str	w2,[x25],4
	add	x21,x21,1
	bl	.tokchk
	b	.g

; bloc plein : le fermer. .tokchk : tout ce qui precede x21 est dans les jetons ;
; .tokat : jusqu'a x0.
.tokchk:
	mov	x0,x21
.tokat:	movl	x2,DF_TOK + DF_TOKENS*4
	add	x2,x19,x2
	cmp	x25,x2
	b.lo	.tokr
	stp	x30,xzr,[sp,-16]!
	mov	x1,DF_TOK
	add	x1,x19,x1
	sub	x2,x25,x1
	lsr	x2,x2,2
	str	x2,[x19,DF_NTOK]
	mov	x1,0
	bl	df_block
	mov	x25,DF_TOK
	add	x25,x19,x25
	ldp	x30,xzr,[sp],16
.tokr:	ret

; fin de la tranche : le bloc en cours se ferme
.close:	str	x21,[x19,DF_POS]
	mov	x1,DF_TOK
	add	x1,x19,x1
	sub	x2,x25,x1
	lsr	x2,x2,2
	str	x2,[x19,DF_NTOK]
	mov	x0,x21
	ldr	x1,[x19,DF_FINAL]
	cbnz	x1,.blk
	cbz	x2,.ret				; (rien a fermer)
.blk:	bl	df_block
	b	.ret

.store:	str	x22,[x19,DF_POS]
	ldr	x0,[x19,DF_BLK]
	mov	x1,x22
	ldr	x2,[x19,DF_FINAL]
	cmp	x0,x1
	b.ne	.st
	cbz	x2,.ret
.st:	bl	df_stored
	ldr	x0,[x19,DF_POS]
	str	x0,[x19,DF_BLK]
.ret:	ldr	x0,[x19,DF_ERR]
	ldr	x28,[sp],16
	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; --- sortie -----------------------------------------------------------------------

; passe le tampon de sortie au puits (une erreur est gardee, la suite est jetee)
df_oflush:
	prologue
	ldr	x2,[x19,DF_OP]
	cbz	x2,.out
	str	xzr,[x19,DF_OP]
	ldr	x0,[x19,DF_ERR]
	cbnz	x0,.out
	ldr	x0,[x19,DF_SINK]
	movl	x1,DF_OBUF
	add	x1,x19,x1
	bl	sink_write
	str	x0,[x19,DF_ERR]
.out:	epilogue

; x0 = valeur, x1 = bits (<= 32)
df_bits:
	prologue
	ldr	x2,[x19,DF_BB]
	ldr	x3,[x19,DF_BC]
	lsl	x0,x0,x3
	orr	x2,x2,x0
	add	x3,x3,x1
	str	x2,[x19,DF_BB]
	str	x3,[x19,DF_BC]
	bl	df_obytes
	epilogue

; les octets entiers du tampon de bits vers la sortie
df_obytes:
	prologue
	ldr	x0,[x19,DF_OP]
	mov	x1,DF_OSIZE
	cmp	x0,x1
	b.lo	.a
	bl	df_oflush
.a:	ldr	x0,[x19,DF_OP]
	movl	x1,DF_OBUF
	add	x1,x19,x1
	ldr	x2,[x19,DF_BB]
	ldr	x3,[x19,DF_BC]
.b:	cmp	x3,8
	b.lo	.c
	strb	w2,[x1,x0]
	add	x0,x0,1
	lsr	x2,x2,8
	sub	x3,x3,8
	b	.b
.c:	str	x0,[x19,DF_OP]
	str	x2,[x19,DF_BB]
	str	x3,[x19,DF_BC]
	epilogue

; complete l'octet en cours
df_align:
	prologue
	ldr	x3,[x19,DF_BC]
	add	x3,x3,7
	and	x3,x3,not 7
	str	x3,[x19,DF_BC]
	bl	df_obytes
	epilogue

; x0 = debut, x1 = fin (decalages), x2 = 1 : dernier : des blocs stockes
df_stored:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
.bloc:	sub	x23,x21,x20
	movz	x0,0xFFFF
	cmp	x23,x0
	csel	x23,x0,x23,hi
	add	x0,x20,x23
	cmp	x0,x21
	cset	x0,eq
	and	x0,x0,x22			; dernier bit
	mov	x1,3				; (type 00)
	bl	df_bits
	bl	df_align
	mov	x0,x23
	mvn	w1,w23
	and	w1,w1,0xFFFF
	orr	x0,x0,x1,lsl 16
	mov	x1,32
	bl	df_bits
	bl	df_oflush			; le tampon est libre
	movl	x1,DF_WIN
	add	x1,x19,x1
	add	x1,x1,x20
	mov	x2,x23
	ldr	x0,[x19,DF_ERR]
	cbnz	x0,.skip
	ldr	x0,[x19,DF_SINK]
	bl	sink_write
	str	x0,[x19,DF_ERR]
.skip:	add	x20,x20,x23
	cmp	x20,x21
	b.lo	.bloc
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; --- arbres de Huffman ---------------------------------------------------------------

; x0 = frequences (u32), x1 = symboles, x2 = longueur maximale, x3 = longueurs (u8)
; (x19 = etat, pour ses tableaux de travail)
df_tree:
	mov	x4,0
.z:	strb	wzr,[x3,x4]
	add	x4,x4,1
	cmp	x4,x1
	b.lo	.z
	movz	x4,DF_SORT and 0xFFFF
	add	x4,x19,x4
	mov	x5,0				; m symboles utiles
	mov	x6,0
.col:	ldr	w7,[x0,x6,lsl 2]
	cbz	w7,.cn
	lsl	x7,x7,16
	orr	x7,x7,x6
	mov	x8,x5				; insertion dans la liste triee
.ins:	cbz	x8,.put
	sub	x9,x8,1
	ldr	x10,[x4,x9,lsl 3]
	cmp	x10,x7
	b.ls	.put
	str	x10,[x4,x8,lsl 3]
	mov	x8,x9
	b	.ins
.put:	str	x7,[x4,x8,lsl 3]
	add	x5,x5,1
.cn:	add	x6,x6,1
	cmp	x6,x1
	b.lo	.col
	cbz	x5,.ret
	cmp	x5,1
	b.ne	.many
	ldr	x7,[x4]
	and	x7,x7,0xFFFF
	mov	w8,1
	strb	w8,[x3,x7]
.ret:	ret
.many:
	; Moffat-Katajainen : A[0..m[ trie -> A[i] = longueur du code i
	movz	x6,DF_WORK and 0xFFFF
	add	x6,x19,x6
	mov	x7,0
.cp:	ldr	x8,[x4,x7,lsl 3]
	lsr	x8,x8,16
	str	w8,[x6,x7,lsl 2]
	add	x7,x7,1
	cmp	x7,x5
	b.lo	.cp
	ldr	w8,[x6]
	ldr	w9,[x6,4]
	add	w8,w8,w9
	str	w8,[x6]
	mov	x7,0				; racine
	mov	x8,2				; feuille
	mov	x9,1				; suivant
	sub	x10,x5,1
.ph1:	cmp	x9,x10
	b.hs	.ph1e
	; premier fils
	cmp	x8,x5
	b.hs	.r1
	ldr	w11,[x6,x7,lsl 2]
	ldr	w12,[x6,x8,lsl 2]
	cmp	w11,w12
	b.hs	.l1
.r1:	ldr	w11,[x6,x7,lsl 2]
	str	w11,[x6,x9,lsl 2]
	str	w9,[x6,x7,lsl 2]
	add	x7,x7,1
	b	.c2
.l1:	ldr	w11,[x6,x8,lsl 2]
	str	w11,[x6,x9,lsl 2]
	add	x8,x8,1
.c2:	; second fils
	cmp	x8,x5
	b.hs	.r2
	cmp	x7,x9
	b.hs	.l2
	ldr	w11,[x6,x7,lsl 2]
	ldr	w12,[x6,x8,lsl 2]
	cmp	w11,w12
	b.hs	.l2
.r2:	ldr	w11,[x6,x7,lsl 2]
	ldr	w12,[x6,x9,lsl 2]
	add	w12,w12,w11
	str	w12,[x6,x9,lsl 2]
	str	w9,[x6,x7,lsl 2]
	add	x7,x7,1
	b	.ph1n
.l2:	ldr	w11,[x6,x8,lsl 2]
	ldr	w12,[x6,x9,lsl 2]
	add	w12,w12,w11
	str	w12,[x6,x9,lsl 2]
	add	x8,x8,1
.ph1n:	add	x9,x9,1
	b	.ph1
.ph1e:
	sub	x9,x5,2
	str	wzr,[x6,x9,lsl 2]
	; profondeur des noeuds
.ph2:	cbz	x9,.ph2e
	sub	x9,x9,1
	ldr	w11,[x6,x9,lsl 2]
	ldr	w11,[x6,x11,lsl 2]
	add	w11,w11,1
	str	w11,[x6,x9,lsl 2]
	b	.ph2
.ph2e:	mov	x7,1				; avbl
	mov	x8,0				; used
	mov	x9,0				; dpth
	sub	x10,x5,2			; root
	mov	x11,x5				; next (+1)
	sub	x11,x11,1
.ph3:	cbz	x7,.ph3e
.ph3a:	tbnz	x10,63,.ph3b
	ldr	w12,[x6,x10,lsl 2]
	cmp	x12,x9
	b.ne	.ph3b
	add	x8,x8,1
	sub	x10,x10,1
	b	.ph3a
.ph3b:	cmp	x7,x8
	b.ls	.ph3c
	str	w9,[x6,x11,lsl 2]
	sub	x11,x11,1
	sub	x7,x7,1
	b	.ph3b
.ph3c:	lsl	x7,x8,1
	add	x9,x9,1
	mov	x8,0
	b	.ph3
.ph3e:
	; histogramme, plafonne a la longueur maximale
	movz	x7,DF_CNT and 0xFFFF
	add	x7,x19,x7
	mov	x8,0
.hz:	str	wzr,[x7,x8,lsl 2]
	add	x8,x8,1
	cmp	x8,34
	b.lo	.hz
	mov	x8,0
.h:	ldr	w9,[x6,x8,lsl 2]
	cmp	x9,x2
	csel	x9,x2,x9,hi
	ldr	w10,[x7,x9,lsl 2]
	add	w10,w10,1
	str	w10,[x7,x9,lsl 2]
	add	x8,x8,1
	cmp	x8,x5
	b.lo	.h
	; Kraft : somme de cnt[i] << (max - i)
	mov	x8,0
	mov	x9,1
.k:	cmp	x9,x2
	b.hi	.ke
	ldr	w10,[x7,x9,lsl 2]
	sub	x11,x2,x9
	lsl	x10,x10,x11
	add	x8,x8,x10
	add	x9,x9,1
	b	.k
.ke:	mov	x9,1
	lsl	x9,x9,x2			; 1 << max
.fix:	cmp	x8,x9
	b.ls	.fixe
	ldr	w10,[x7,x2,lsl 2]
	sub	w10,w10,1
	str	w10,[x7,x2,lsl 2]
	sub	x10,x2,1
.fx:	cbz	x10,.fxe
	ldr	w11,[x7,x10,lsl 2]
	cbnz	w11,.fxf
	sub	x10,x10,1
	b	.fx
.fxf:	sub	w11,w11,1
	str	w11,[x7,x10,lsl 2]
	add	x10,x10,1
	ldr	w11,[x7,x10,lsl 2]
	add	w11,w11,2
	str	w11,[x7,x10,lsl 2]
.fxe:	sub	x8,x8,1
	b	.fix
.fixe:
	; les moins frequents recoivent les codes les plus longs
	mov	x8,0				; indice dans la liste triee
	mov	x9,x2				; longueur
.as:	cbz	x9,.ret2
	ldr	w10,[x7,x9,lsl 2]
.as1:	cbz	w10,.as2
	ldr	x11,[x4,x8,lsl 3]
	and	x11,x11,0xFFFF
	strb	w9,[x3,x11]
	add	x8,x8,1
	sub	w10,w10,1
	b	.as1
.as2:	sub	x9,x9,1
	b	.as
.ret2:	ret

; x0 = longueurs (u8), x1 = symboles, x2 = table (u32 : code inverse | longueur << 16)
df_codes:
	sub	sp,sp,64
	stp	xzr,xzr,[sp]
	stp	xzr,xzr,[sp,16]			; (u16[16] : nombre par longueur)
	mov	x3,0
.c:	ldrb	w4,[x0,x3]
	ldrh	w5,[sp,x4,lsl 1]
	add	w5,w5,1
	strh	w5,[sp,x4,lsl 1]
	add	x3,x3,1
	cmp	x3,x1
	b.lo	.c
	strh	wzr,[sp]
	mov	w3,0				; code
	mov	x4,1
	add	x7,sp,32			; u16[16] : prochain code
.n:	ldrh	w5,[sp,x4,lsl 1]
	sub	x6,x4,1
	ldrh	w6,[sp,x6,lsl 1]
	add	w3,w3,w6
	lsl	w3,w3,1
	strh	w3,[x7,x4,lsl 1]
	add	x4,x4,1
	cmp	x4,16
	b.lo	.n
	mov	x3,0
.s:	ldrb	w4,[x0,x3]
	mov	w5,0
	cbz	w4,.w
	ldrh	w5,[x7,x4,lsl 1]
	add	w6,w5,1
	strh	w6,[x7,x4,lsl 1]
	rbit	w5,w5
	mov	w6,32
	sub	w6,w6,w4
	lsr	w5,w5,w6
	orr	w5,w5,w4,lsl 16
.w:	str	w5,[x2,x3,lsl 2]
	add	x3,x3,1
	cmp	x3,x1
	b.lo	.s
	add	sp,sp,64
	ret

; sommes un tableau : x0 = frequences, x1 = longueurs (u8), x2 = symboles, x3 = cumul
; -> x3 cumule ; x0 inchange, x4 x5 x6 uses
macro df_cost {
local .a, .b
	mov	x4,0
.a:	cmp	x4,x2
	b.hs	.b
	ldr	w5,[x0,x4,lsl 2]
	ldrb	w6,[x1,x4]
	mul	x5,x5,x6
	add	x3,x3,x5
	add	x4,x4,1
	b	.a
.b:
}

; symbole et bits supplementaires d'une longueur : x2 = longueur - 3 -> x3 = symbole
; - 257, x4 = bits en plus, x5 = leur valeur
macro df_lensym {
local .a, .b
	mov	x4,0
	mov	x5,0
	mov	x3,x2
	cmp	x2,8
	b.lo	.b
	cmp	x2,255
	b.ne	.a
	mov	x3,28
	b	.b
.a:	clz	w5,w2
	mov	w4,31
	sub	w4,w4,w5			; n
	sub	w3,w4,1
	lsl	w3,w3,2
	sub	w4,w4,2				; bits en plus
	lsr	w5,w2,w4
	and	w5,w5,3
	add	w3,w3,w5
	mov	w5,1
	lsl	w5,w5,w4
	sub	w5,w5,1
	and	w5,w5,w2
.b:
}

; symbole d'une distance : x1 = distance -> x6 = symbole, x7 = bits en plus, x8 = valeur
macro df_distsym {
local .a
	sub	w6,w1,1
	mov	x7,0
	mov	x8,0
	cmp	w6,4
	b.lo	.a
	clz	w8,w6
	mov	w9,31
	sub	w9,w9,w8			; n
	sub	w7,w9,1				; bits en plus
	lsr	w8,w6,w7
	and	w8,w8,1
	add	w8,w8,w9,lsl 1			; symbole
	mov	w9,1
	lsl	w9,w9,w7
	sub	w9,w9,1
	and	w9,w9,w6			; valeur
	mov	w6,w8
	mov	w8,w9
.a:
}

; x0 = fin du bloc (decalage), x1 = 1 : dernier ; DF_NTOK jetons -> ecrit le bloc,
; DF_BLK = fin du bloc
df_block:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	str	x0,[x19,DF_BEND]
	; frequences
	add	x22,x19,DF_LFREQ
	add	x23,x19,DF_DFREQ
	mov	x0,x22
	mov	x1,0
	mov	x2,(288+32+20)*4
	bl	ar_memset
	mov	w0,1
	mov	x1,256
	str	w0,[x22,x1,lsl 2]
	mov	x10,DF_TOK
	add	x10,x19,x10
	ldr	x11,[x19,DF_NTOK]
	add	x11,x10,x11,lsl 2
	mov	x12,0				; bits en plus
.tal:	cmp	x10,x11
	b.hs	.tald
	ldr	w0,[x10],4
	lsr	w1,w0,8
	and	w2,w0,0xFF
	cbnz	w1,.talm
	ldr	w3,[x22,x2,lsl 2]
	add	w3,w3,1
	str	w3,[x22,x2,lsl 2]
	b	.tal
.talm:	df_lensym
	add	x12,x12,x4
	add	x3,x3,257
	ldr	w5,[x22,x3,lsl 2]
	add	w5,w5,1
	str	w5,[x22,x3,lsl 2]
	df_distsym
	add	x12,x12,x7
	ldr	w8,[x23,x6,lsl 2]
	add	w8,w8,1
	str	w8,[x23,x6,lsl 2]
	b	.tal
.tald:	mov	x24,x12				; bits en plus
	; arbres
	mov	x0,x22
	mov	x1,286
	mov	x2,15
	add	x3,x19,DF_LLEN
	bl	df_tree
	mov	x0,x23
	mov	x1,30
	mov	x2,15
	add	x3,x19,DF_DLEN
	bl	df_tree
	; HLIT, HDIST
	add	x0,x19,DF_LLEN
	mov	x25,286
.hl:	cmp	x25,257
	b.ls	.hle
	sub	x1,x25,1
	ldrb	w2,[x0,x1]
	cbnz	w2,.hle
	mov	x25,x1
	b	.hl
.hle:	add	x0,x19,DF_DLEN
	mov	x26,30
.hd:	cmp	x26,1
	b.ls	.hde
	sub	x1,x26,1
	ldrb	w2,[x0,x1]
	cbnz	w2,.hde
	mov	x26,x1
	b	.hd
.hde:	bl	df_rle				; -> DF_RLE, DF_BFREQ ; x0 = jetons, x1 = bits en plus
	mov	x27,x0
	add	x24,x24,x1			; (provisoire : retire plus bas)
	str	x1,[sp,-16]!
	add	x0,x19,DF_BFREQ
	mov	x1,19
	mov	x2,7
	add	x3,x19,DF_BLEN
	bl	df_tree
	; HCLEN
	adr	x0,df_clorder
	add	x1,x19,DF_BLEN
	mov	x4,19
.hc:	cmp	x4,4
	b.ls	.hce
	sub	x2,x4,1
	ldrb	w2,[x0,x2]
	ldrb	w2,[x1,x2]
	cbnz	w2,.hce
	sub	x4,x4,1
	b	.hc
.hce:	mov	x7,x4				; HCLEN
	; cout dynamique
	mov	x3,x4
	add	x3,x3,x3,lsl 1			; 3 bits par longueur
	add	x3,x3,14
	add	x3,x3,x24
	add	x0,x19,DF_BFREQ
	add	x1,x19,DF_BLEN
	mov	x2,19
	df_cost
	add	x0,x19,DF_LFREQ
	add	x1,x19,DF_LLEN
	mov	x2,286
	df_cost
	add	x0,x19,DF_DFREQ
	add	x1,x19,DF_DLEN
	mov	x2,30
	df_cost
	mov	x8,x3				; x8 = bits en dynamique
	ldr	x1,[sp],16			; retirer les bits du RLE pour le cout fixe
	sub	x24,x24,x1
	; cout fixe (les longueurs fixes sont dans la table)
	mov	x3,x24
	movl	x10,DF_FLTAB
	add	x10,x19,x10
	mov	x4,0
.fc:	ldr	w5,[x22,x4,lsl 2]
	ldr	w6,[x10,x4,lsl 2]
	lsr	w6,w6,16
	mul	x5,x5,x6
	add	x3,x3,x5
	add	x4,x4,1
	cmp	x4,286
	b.lo	.fc
	mov	x4,0
.fd:	ldr	w5,[x23,x4,lsl 2]
	add	x3,x3,x5,lsl 2
	add	x3,x3,x5
	add	x4,x4,1
	cmp	x4,30
	b.lo	.fd
	mov	x9,x3				; x9 = bits en fixe
	; cout stocke (en bits) : octets + 5 par tranche de 65535 + l'alignement moyen
	ldr	x0,[x19,DF_BLK]
	sub	x1,x20,x0
	movz	x2,0xFFFF
	udiv	x2,x1,x2
	add	x2,x2,1
	add	x2,x2,x2,lsl 2
	add	x2,x2,x1
	lsl	x2,x2,3				; x2 = bits en stocke
	cmp	x8,x9
	csel	x3,x8,x9,lo			; le meilleur des deux codes
	cmp	x2,x3
	b.ls	.stored
	cmp	x8,x9
	b.hs	.fixed
	; --- dynamique
	mov	x0,2
	lsl	x0,x0,1
	orr	x0,x0,x21
	mov	x1,3
	str	x7,[sp,-16]!
	bl	df_bits
	ldr	x7,[sp],16
	sub	x0,x25,257
	sub	x1,x26,1
	orr	x0,x0,x1,lsl 5
	sub	x1,x7,4
	orr	x0,x0,x1,lsl 10
	mov	x1,14
	mov	x20,x7
	bl	df_bits
	mov	x24,0
.hw:	cmp	x24,x20
	b.hs	.hwe
	adr	x0,df_clorder
	ldrb	w0,[x0,x24]
	add	x1,x19,DF_BLEN
	ldrb	w0,[x1,x0]
	mov	x1,3
	bl	df_bits
	add	x24,x24,1
	b	.hw
.hwe:	; les longueurs, en jetons RLE
	add	x0,x19,DF_BLEN
	mov	x1,19
	add	x2,x19,DF_BTAB
	bl	df_codes
	mov	x24,0
.rw:	cmp	x24,x27
	b.hs	.rwe
	movz	x0,DF_RLE and 0xFFFF
	add	x0,x19,x0
	ldrh	w26,[x0,x24,lsl 1]
	and	x1,x26,0xFF
	add	x2,x19,DF_BTAB
	ldr	w2,[x2,x1,lsl 2]
	and	w0,w2,0xFFFF
	mov	x25,x1
	lsr	w1,w2,16
	bl	df_bits
	cmp	x25,16
	b.lo	.rn
	lsr	w0,w26,8
	mov	x1,2
	cmp	x25,16
	b.eq	.rx
	mov	x1,3
	cmp	x25,17
	b.eq	.rx
	mov	x1,7
.rx:	bl	df_bits
.rn:	add	x24,x24,1
	b	.rw
.rwe:	add	x0,x19,DF_LLEN
	mov	x1,286
	add	x2,x19,DF_LTAB
	bl	df_codes
	add	x0,x19,DF_DLEN
	mov	x1,30
	movz	x2,DF_DTAB and 0xFFFF
	add	x2,x19,x2
	bl	df_codes
	add	x24,x19,DF_LTAB
	movz	x25,DF_DTAB and 0xFFFF
	add	x25,x19,x25
	b	.emit
.fixed:	mov	x0,1
	lsl	x0,x0,1
	orr	x0,x0,x21
	mov	x1,3
	bl	df_bits
	movl	x24,DF_FLTAB
	add	x24,x19,x24
	movl	x25,DF_FDTAB
	add	x25,x19,x25
.emit:	bl	df_tokens
	b	.done
.stored:
	ldr	x0,[x19,DF_BLK]
	mov	x1,x20
	mov	x2,x21
	bl	df_stored
.done:	ldr	x0,[x19,DF_BEND]
	str	x0,[x19,DF_BLK]
	str	xzr,[x19,DF_NTOK]
	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; x24 = codes de litteraux et longueurs, x25 = codes de distance : les jetons, puis
; la fin de bloc
df_tokens:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	mov	x20,DF_TOK
	add	x20,x19,x20
	ldr	x21,[x19,DF_NTOK]
	add	x21,x20,x21,lsl 2
	ldr	x22,[x19,DF_BB]
	ldr	x23,[x19,DF_BC]
	movl	x26,DF_OBUF
	add	x26,x19,x26
	movl	x27,DF_OSIZE
	add	x27,x26,x27			; limite
	ldr	x0,[x19,DF_OP]
	add	x26,x26,x0
.t:	cmp	x20,x21
	b.hs	.eob
	ldr	w0,[x20],4
	lsr	w1,w0,8
	and	w2,w0,0xFF
	cbnz	w1,.m
	ldr	w3,[x24,x2,lsl 2]
	and	x4,x3,0xFFFF
	lsl	x4,x4,x23
	orr	x22,x22,x4
	add	x23,x23,x3,lsr 16
	b	.fl
.m:	df_lensym
	add	x3,x3,257
	ldr	w3,[x24,x3,lsl 2]
	and	x10,x3,0xFFFF
	lsl	x10,x10,x23
	orr	x22,x22,x10
	add	x23,x23,x3,lsr 16
	cbz	x4,.m1
	lsl	x5,x5,x23
	orr	x22,x22,x5
	add	x23,x23,x4
.m1:	df_distsym
	ldr	w6,[x25,x6,lsl 2]
	and	x10,x6,0xFFFF
	lsl	x10,x10,x23
	orr	x22,x22,x10
	add	x23,x23,x6,lsr 16
	cbz	x7,.fl
	lsl	x8,x8,x23
	orr	x22,x22,x8
	add	x23,x23,x7
.fl:	str	x22,[x26]
	lsr	x0,x23,3
	add	x26,x26,x0
	lsl	x0,x0,3
	sub	x23,x23,x0
	lsr	x22,x22,x0
	cmp	x26,x27
	b.lo	.t
	bl	.sync
	bl	df_oflush
	bl	.load
	b	.t
.eob:	mov	x0,256
	ldr	w0,[x24,x0,lsl 2]
	and	x1,x0,0xFFFF
	lsl	x1,x1,x23
	orr	x22,x22,x1
	add	x23,x23,x0,lsr 16
	bl	.sync
	bl	df_obytes
	ldp	x26,x27,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue
.sync:	movl	x0,DF_OBUF
	add	x0,x19,x0
	sub	x0,x26,x0
	str	x0,[x19,DF_OP]
	str	x22,[x19,DF_BB]
	str	x23,[x19,DF_BC]
	ret
.load:	movl	x0,DF_OBUF
	add	x0,x19,x0
	ldr	x1,[x19,DF_OP]
	add	x26,x0,x1
	ldr	x22,[x19,DF_BB]
	ldr	x23,[x19,DF_BC]
	ret

; x25 = HLIT, x26 = HDIST : les longueurs mises bout a bout, codees par les symboles
; 16, 17, 18 -> DF_RLE, DF_BFREQ ; x0 = jetons, x1 = bits en plus
df_rle:
	movz	x4,DF_SEQ and 0xFFFF
	add	x4,x19,x4
	add	x0,x19,DF_LLEN
	mov	x1,0
.a:	ldrb	w2,[x0,x1]
	strb	w2,[x4,x1]
	add	x1,x1,1
	cmp	x1,x25
	b.lo	.a
	add	x0,x19,DF_DLEN
	mov	x2,0
.b:	ldrb	w3,[x0,x2]
	strb	w3,[x4,x1]
	add	x1,x1,1
	add	x2,x2,1
	cmp	x2,x26
	b.lo	.b
	mov	x5,x1				; n
	movz	x6,DF_RLE and 0xFFFF
	add	x6,x19,x6
	add	x7,x19,DF_BFREQ
	mov	x8,0				; jetons
	mov	x9,0				; bits en plus
	mov	x10,0				; i
.r:	cmp	x10,x5
	b.hs	.fin
	ldrb	w11,[x4,x10]
	mov	x12,x10				; fin de la serie
.s:	add	x12,x12,1
	cmp	x12,x5
	b.hs	.se
	ldrb	w13,[x4,x12]
	cmp	w13,w11
	b.eq	.s
.se:	sub	x12,x12,x10			; longueur de la serie
	add	x10,x10,x12
	cbnz	w11,.nz
.z:	cmp	x12,11
	b.lo	.z2
	mov	x13,138
	cmp	x12,x13
	csel	x13,x13,x12,hi
	sub	x14,x13,11
	lsl	x14,x14,8
	add	x14,x14,18
	strh	w14,[x6,x8,lsl 1]
	add	x8,x8,1
	mov	x14,18
	ldr	w15,[x7,x14,lsl 2]
	add	w15,w15,1
	str	w15,[x7,x14,lsl 2]
	add	x9,x9,7
	sub	x12,x12,x13
	b	.z
.z2:	cmp	x12,3
	b.lo	.lits
	sub	x14,x12,3
	lsl	x14,x14,8
	add	x14,x14,17
	strh	w14,[x6,x8,lsl 1]
	add	x8,x8,1
	mov	x14,17
	ldr	w15,[x7,x14,lsl 2]
	add	w15,w15,1
	str	w15,[x7,x14,lsl 2]
	add	x9,x9,3
	b	.r
.nz:	; un de la longueur, puis des repetitions
	strh	w11,[x6,x8,lsl 1]
	add	x8,x8,1
	ldr	w15,[x7,x11,lsl 2]
	add	w15,w15,1
	str	w15,[x7,x11,lsl 2]
	sub	x12,x12,1
.nr:	cmp	x12,3
	b.lo	.lits
	mov	x13,6
	cmp	x12,x13
	csel	x13,x13,x12,hi
	sub	x14,x13,3
	lsl	x14,x14,8
	add	x14,x14,16
	strh	w14,[x6,x8,lsl 1]
	add	x8,x8,1
	mov	x14,16
	ldr	w15,[x7,x14,lsl 2]
	add	w15,w15,1
	str	w15,[x7,x14,lsl 2]
	add	x9,x9,2
	sub	x12,x12,x13
	b	.nr
.lits:	cbz	x12,.r
	strh	w11,[x6,x8,lsl 1]
	add	x8,x8,1
	ldr	w15,[x7,x11,lsl 2]
	add	w15,w15,1
	str	w15,[x7,x11,lsl 2]
	sub	x12,x12,1
	b	.lits
.fin:	mov	x0,x8
	mov	x1,x9
	ret

df_clorder	db 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
		align	4
; good, lazy, nice, chain (par niveau ; 0 : stocke)
df_config	dh 0,0,0,0
		dh 4,4,8,4
		dh 4,5,16,8
		dh 4,6,32,32
		dh 4,4,16,16
		dh 8,16,32,32
		dh 8,16,128,128
		dh 8,32,128,256
		dh 32,128,258,1024
		dh 32,258,258,4096
		align	4
