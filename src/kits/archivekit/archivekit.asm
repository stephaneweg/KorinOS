; ============================================================================
;  kits/archivekit/archivekit.asm - ArchiveKit : compression et archives
;  (docs/KITS.md) -> SD0:/lib/archivekit.so
;
;  * CRC-32 (instructions CRC d'ARMv8, presentes sur Cortex-A72 et A76),
;    Adler-32 ;
;  * deflate (ar_deflate.inc), inflate, zlib, gzip (ar_inflate.inc) ;
;  * archives lues, sur la carte ou en memoire (ar_read.inc) : ZIP, ZIP64,
;    tar, tar.gz, gz ; archives ecrites (ar_write.inc) : ZIP, tar, tar.gz, gz.
;
;  AAPCS64, poignees opaques (des blocs du kit, reconnus a leur premier mot),
;  erreurs negatives. Aucune variable globale hors de la liaison a AppKit :
;  deux threads peuvent travailler en meme temps sur deux poignees.
;
;  Memoire : pages de VM_MAP, a zero, peuplees a la demande : les grands etats
;  (deflate 0,7 Mo, inflate 0,35 Mo) ne coutent que les pages touchees.
;  Pas de TLS, pas de chiffrement : une entree chiffree rend AR_E_CRYPT.
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
processor cpu64_v8 +cpu64_fp +cpu64_simd +cpu64_crc
include '../../inc/fs.inc'
include 'archivekit.inc'

AR_MAGIC_BUF	= 0x4655424B5241	; "ARKBUF"
AR_MAGIC_RD	= 0x44524B5241		; "ARKRD"
AR_MAGIC_WR	= 0x52574B5241		; "ARKWR"

; debut commun des poignees
H_MAGIC		= 0
H_DATA		= 8
H_SIZE		= 16
B_SINK		= 32			; (tampon) son puits

; puits : ou vont les octets
SK_KIND		= 0
SK_PTR		= 8			; tampon, ou descripteur
SK_POS		= 16			; octets ecrits
SK_CAP		= 24
SK_SIZE		= 32
SKK_NULL	= 0			; compte seulement
SKK_FIXED	= 1			; tampon de l'appelant (AR_E_SPACE)
SKK_GROW	= 2			; bloc du kit, qui grandit
SKK_FD		= 3			; fichier
SKK_PROBE	= 4			; tampon fixe, AR_STOP quand il est plein

segment readable executable
include 'archivekit.abi'
kit_use '../../kits/appkit/appkit.abi'		; fichiers

macro ar_enter {
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
}
; x0 = poignee : tampon ou ecrivain, sinon bad
macro h_check bad* {
local .ok
	tbnz	x0,63,bad
	cbz	x0,bad
	ldr	x1,[x0,H_MAGIC]
	movl	x2,AR_MAGIC_BUF
	cmp	x1,x2
	b.eq	.ok
	movl	x2,AR_MAGIC_WR
	cmp	x1,x2
	b.ne	bad
.ok:
}
macro bl_kits {
	str	x30,[sp,-16]!
	bl	ar_kits
	ldr	x30,[sp],16
}
macro ar_leave {
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
}

; AppKit ouvert au premier besoin ; garde x0-x7
ar_kits:
	adr	x16,ar_kits_ok
	ldr	x17,[x16]
	cbnz	x17,.ok
	stp	x29,x30,[sp,-16]!
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	stp	x4,x5,[sp,-16]!
	stp	x6,x7,[sp,-16]!
	bl	kits_open
	adr	x16,ar_kits_ok
	mov	x17,1
	str	x17,[x16]
	ldp	x6,x7,[sp],16
	ldp	x4,x5,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	ldp	x29,x30,[sp],16
.ok:	ret

; --- memoire --------------------------------------------------------------------

; x0 = octets -> x0 = bloc a zero, 0 si echec
ar_alloc:
	prologue
	str	x19,[sp,-16]!
	add	x19,x0,16
	add	x19,x19,4095
	and	x19,x19,not 4095
	mov	x0,0
	mov	x1,x19
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.fail
	str	x19,[x0]
	add	x0,x0,16
	b	.out
.fail:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = bloc (0 : rien)
ar_mfree:
	cbz	x0,.ret
	prologue
	sub	x0,x0,16
	ldr	x1,[x0]
	sys	SYS_VM_UNMAP
	epilogue
.ret:	ret

; x0 = destination, x1 = source, x2 = octets : vers l'avant (recouvrement permis si
; destination < source). Touche x0-x4.
ar_memcpy:
.l:	cmp	x2,16
	b.lo	.t
	ldp	x3,x4,[x1],16
	stp	x3,x4,[x0],16
	sub	x2,x2,16
	b	.l
.t:	cbz	x2,.e
	ldrb	w3,[x1],1
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.t
.e:	ret

; x0 = destination, w1 = octet, x2 = octets. Touche x0-x2.
ar_memset:
.l:	cbz	x2,.e
	strb	w1,[x0],1
	sub	x2,x2,1
	b	.l
.e:	ret

; --- sommes -----------------------------------------------------------------------

; x0 = crc, x1 = donnees, x2 = octets -> crc. Touche x0-x3.
ar_crc32:
	mvn	w0,w0
.w:	cmp	x2,8
	b.lo	.b
	ldr	x3,[x1],8
	crc32x	w0,w0,x3
	sub	x2,x2,8
	b	.w
.b:	cbz	x2,.e
	ldrb	w3,[x1],1
	crc32b	w0,w0,w3
	sub	x2,x2,1
	b	.b
.e:	mvn	w0,w0
	ret

; x0 = adler, x1 = donnees, x2 = octets -> adler. Touche x0-x7.
ar_adler32:
	and	w3,w0,0xFFFF
	lsr	w4,w0,16
	mov	w7,65521
.blk:	cbz	x2,.e
	mov	x5,5552				; (au-dela, b deborderait 32 bits)
	cmp	x2,x5
	csel	x5,x2,x5,lo
	sub	x2,x2,x5
.l:	ldrb	w6,[x1],1
	add	w3,w3,w6
	add	w4,w4,w3
	subs	x5,x5,1
	b.ne	.l
	udiv	w6,w3,w7
	msub	w3,w6,w7,w3
	udiv	w6,w4,w7
	msub	w4,w6,w7,w4
	b	.blk
.e:	orr	w0,w3,w4,lsl 16
	ret

; --- puits ------------------------------------------------------------------------

; x0 = puits, x1 = donnees, x2 = octets -> x0 = 0, erreur, AR_STOP
sink_write:
	ar_enter
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldr	x0,[x19,SK_KIND]
	cmp	x0,SKK_FD
	b.eq	.fd
	cmp	x0,SKK_GROW
	b.eq	.grow
	ldr	x3,[x19,SK_POS]
	cbz	x0,.count
	ldr	x4,[x19,SK_CAP]
	sub	x4,x4,x3			; place
	cmp	x0,SKK_PROBE
	b.eq	.probe
	cmp	x21,x4
	b.hi	.space
.copy:	ldr	x0,[x19,SK_PTR]
	add	x0,x0,x3
	mov	x1,x20
	mov	x2,x21
	bl	ar_memcpy
	ldr	x3,[x19,SK_POS]
.count:	add	x3,x3,x21
	str	x3,[x19,SK_POS]
.ok:	mov	x0,0
	b	.out
.probe:	cmp	x21,x4
	csel	x21,x4,x21,hi
	ldr	x0,[x19,SK_PTR]
	add	x0,x0,x3
	mov	x1,x20
	mov	x2,x21
	bl	ar_memcpy
	ldr	x3,[x19,SK_POS]
	add	x3,x3,x21
	str	x3,[x19,SK_POS]
	ldr	x4,[x19,SK_CAP]
	cmp	x3,x4
	b.lo	.ok
	mov	x0,AR_STOP
	b	.out
.space:	mov	x0,AR_E_SPACE
	b	.out
.grow:	ldr	x3,[x19,SK_POS]
	ldr	x4,[x19,SK_CAP]
	add	x5,x3,x21
	add	x5,x5,1				; (la place d'un 0 final)
	cmp	x5,x4
	b.ls	.copy
	lsl	x22,x4,1
	movl	x6,65536
	cmp	x22,x6
	csel	x22,x6,x22,lo
	cmp	x22,x5
	csel	x22,x5,x22,lo			; nouvelle capacite
	mov	x0,x22
	bl	ar_alloc
	cbz	x0,.nomem
	mov	x23,x0
	ldr	x1,[x19,SK_PTR]
	ldr	x2,[x19,SK_POS]
	cbz	x1,.gset
	bl	ar_memcpy
	ldr	x0,[x19,SK_PTR]
	bl	ar_mfree
.gset:	str	x23,[x19,SK_PTR]
	str	x22,[x19,SK_CAP]
	ldr	x3,[x19,SK_POS]
	b	.copy
.nomem:	mov	x0,AR_E_NOMEM
	b	.out
.fd:	cbz	x21,.ok
	ldr	x0,[x19,SK_PTR]
	mov	x1,x20
	mov	x2,x21
	bl	f_ecrire
	cmp	x0,0
	b.le	.fderr
	add	x20,x20,x0
	sub	x21,x21,x0
	ldr	x3,[x19,SK_POS]
	add	x3,x3,x0
	str	x3,[x19,SK_POS]
	b	.fd
.fderr:	b.lt	.out
	mov	x0,AR_E_IO
.out:	ar_leave

; x0 = puits en memoire : un 0 apres les donnees -> x0 = 0, erreur
sink_zero:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	strb	wzr,[sp]
	mov	x1,sp
	mov	x2,1
	bl	sink_write
	cbnz	x0,.out
	ldr	x1,[x19,SK_POS]
	sub	x1,x1,1
	str	x1,[x19,SK_POS]
.out:	add	sp,sp,16
	ldr	x19,[sp],16
	epilogue

; --- tampons du kit -------------------------------------------------------------------

; -> x0 = tampon vide, 0 si plus de memoire
buf_new:
	prologue
	mov	x0,4096-16
	bl	ar_alloc
	cbz	x0,.out
	movl	x1,AR_MAGIC_BUF
	str	x1,[x0,H_MAGIC]
	mov	x1,SKK_GROW
	str	x1,[x0,B_SINK+SK_KIND]
.out:	epilogue

; x0 = tampon : ses octets termines (un 0 apres), H_DATA et H_SIZE mis -> x0 = tampon, erreur
buf_done:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	add	x0,x19,B_SINK
	bl	sink_zero
	tbnz	x0,63,.bad
	ldr	x0,[x19,B_SINK+SK_PTR]
	str	x0,[x19,H_DATA]
	ldr	x0,[x19,B_SINK+SK_POS]
	str	x0,[x19,H_SIZE]
	mov	x0,x19
	b	.out
.bad:	str	x0,[sp,-16]!
	mov	x0,x19
	bl	ar_free
	ldr	x0,[sp],16
.out:	ldr	x19,[sp],16
	epilogue

; x0 = tampon, ecrivain -> pointeur sur ses octets
ar_data:
	h_check .bad
	ldr	x0,[x0,H_DATA]
	ret
.bad:	mov	x0,AR_E_INVAL
	ret

; x0 = tampon, ecrivain -> octets
ar_bytes:
	h_check .bad
	ldr	x0,[x0,H_SIZE]
	ret
.bad:	mov	x0,AR_E_INVAL
	ret

; x0 = tampon, ecrivain ou archive -> 0
ar_free:
	cbz	x0,.ret
	tbnz	x0,63,.bad
	ldr	x1,[x0,H_MAGIC]
	movl	x2,AR_MAGIC_BUF
	cmp	x1,x2
	b.eq	.buf
	movl	x2,AR_MAGIC_RD
	cmp	x1,x2
	b.eq	ar_close
	movl	x2,AR_MAGIC_WR
	cmp	x1,x2
	b.eq	wr_free
.bad:	mov	x0,AR_E_INVAL
	ret
.buf:	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	str	xzr,[x19,H_MAGIC]
	ldr	x0,[x19,B_SINK+SK_PTR]
	bl	ar_mfree
	mov	x0,x19
	bl	ar_mfree
	ldr	x19,[sp],16
	mov	x0,0
	epilogue
.ret:	mov	x0,0
	ret

; --- compression d'un bloc de memoire ------------------------------------------------

; x0 = octets, x1 = enveloppe -> taille maximale
ar_deflate_bound:
	add	x1,x0,x0,lsr 12
	add	x0,x1,128
	ret

; x0 = niveau -> x0 = niveau 0..9, negatif si invalide
ar_level:
	cmn	x0,1
	b.ne	.a
	mov	x0,AR_DEFAULT
	ret
.a:	cmp	x0,9
	b.ls	.r
	mov	x0,AR_E_INVAL
.r:	ret

; x0 = puits, x1 = enveloppe, x2 = niveau : l'en-tete -> x0 = 0, erreur
wrap_head:
	sub	sp,sp,32
	stp	x29,x30,[sp,16]
	cmp	x1,AR_ZLIB
	b.eq	.zl
	cmp	x1,AR_GZIP
	b.eq	.gz
	mov	x0,0
	b	.out
.zl:	mov	w1,0x78
	strb	w1,[sp]
	mov	w1,0x01
	cmp	x2,1
	b.ls	.zs
	mov	w1,0x5E
	cmp	x2,5
	b.ls	.zs
	mov	w1,0x9C
	cmp	x2,6
	b.ls	.zs
	mov	w1,0xDA
.zs:	strb	w1,[sp,1]
	mov	x1,sp
	mov	x2,2
	bl	sink_write
	b	.out
.gz:	mov	w1,0x1F
	strb	w1,[sp]
	mov	w1,0x8B
	strb	w1,[sp,1]
	mov	w1,8
	strb	w1,[sp,2]
	strb	wzr,[sp,3]
	str	wzr,[sp,4]
	mov	w1,0
	cmp	x2,9
	b.ne	.g1
	mov	w1,2
.g1:	cmp	x2,1
	b.ne	.g2
	mov	w1,4
.g2:	strb	w1,[sp,8]
	mov	w1,3				; Unix
	strb	w1,[sp,9]
	mov	x1,sp
	mov	x2,10
	bl	sink_write
.out:	ldp	x29,x30,[sp,16]
	add	sp,sp,32
	ret

; x0 = puits, x1 = etat deflate termine : la fin de l'enveloppe -> x0 = 0, erreur
wrap_tail:
	sub	sp,sp,32
	stp	x29,x30,[sp,16]
	ldr	x2,[x1,DF_WRAP]
	cmp	x2,AR_ZLIB
	b.eq	.zl
	cmp	x2,AR_GZIP
	b.eq	.gz
	mov	x0,0
	b	.out
.zl:	ldr	x2,[x1,DF_ADLER]
	rev	w2,w2
	str	w2,[sp]
	mov	x1,sp
	mov	x2,4
	bl	sink_write
	b	.out
.gz:	ldr	x2,[x1,DF_CRC]
	str	w2,[sp]
	ldr	x2,[x1,DF_TOTAL]
	str	w2,[sp,4]
	mov	x1,sp
	mov	x2,8
	bl	sink_write
.out:	ldp	x29,x30,[sp,16]
	add	sp,sp,32
	ret

; x0 = puits, x1 = source, x2 = octets, x3 = enveloppe, x4 = niveau -> x0 = 0, erreur
defl_all:
	ar_enter
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	bl	df_new
	cbz	x0,.nomem
	mov	x24,x0
	mov	x0,x19
	mov	x1,x22
	mov	x2,x23
	bl	wrap_head
	cbnz	x0,.fail
	mov	x0,x24
	mov	x1,x23
	mov	x2,x22
	bl	df_begin
	mov	x0,x24
	mov	x1,x20
	mov	x2,x21
	bl	df_feed
	cbnz	x0,.fail
	mov	x0,x24
	bl	df_finish
	cbnz	x0,.fail
	mov	x0,x19
	mov	x1,x24
	bl	wrap_tail
.fail:	mov	x19,x0
	mov	x0,x24
	bl	ar_mfree
	mov	x0,x19
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
.out:	ar_leave

; x0 = source, x1 = octets, x2 = sortie, x3 = capacite, x4 = enveloppe, x5 = niveau -> octets
ar_deflate:
	prologue
	sub	sp,sp,SK_SIZE
	cmp	x4,AR_GZIP
	b.hi	.bad
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	str	x4,[sp,-16]!
	mov	x0,x5
	bl	ar_level
	mov	x4,x0
	ldr	x3,[sp],16
	ldp	x5,x6,[sp],16
	ldp	x1,x2,[sp],16
	tbnz	x4,63,.bad
	mov	x0,SKK_FIXED
	str	x0,[sp,SK_KIND]
	str	x5,[sp,SK_PTR]
	str	xzr,[sp,SK_POS]
	str	x6,[sp,SK_CAP]
	mov	x0,sp
	bl	defl_all
	cbnz	x0,.out
	ldr	x0,[sp,SK_POS]
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	add	sp,sp,SK_SIZE
	epilogue

; x0 = source, x1 = octets, x2 = enveloppe, x3 = niveau -> tampon
ar_deflate_new:
	ar_enter
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x0,x3
	bl	ar_level
	tbnz	x0,63,.out
	mov	x23,x0
	cmp	x22,AR_GZIP
	b.hi	.bad
	bl	buf_new
	cbz	x0,.nomem
	mov	x19,x0
	add	x0,x19,B_SINK
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	x4,x23
	bl	defl_all
	cbnz	x0,.fail
	mov	x0,x19
	bl	buf_done
	b	.out
.fail:	mov	x20,x0
	mov	x0,x19
	bl	ar_free
	mov	x0,x20
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave

; x0 = source, x1 = octets, x2 = sortie, x3 = capacite, x4 = puits, x5 = enveloppe -> octets
infl_all:
	ar_enter
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
	cmp	x24,AR_AUTO
	b.hi	.bad
	movl	x0,IF_SIZE
	bl	ar_alloc
	cbz	x0,.nomem
	mov	x25,x0
	mov	x1,x19
	mov	x2,x20
	mov	x3,x21
	mov	x4,x22
	mov	x5,x23
	mov	x6,x24
	bl	if_begin
	mov	x0,x25
	bl	if_run
	mov	x19,x0
	mov	x0,x25
	bl	ar_mfree
	mov	x0,x19
	b	.out
.bad:	mov	x0,AR_E_INVAL
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
.out:	ar_leave

; x0 = source, x1 = octets, x2 = sortie, x3 = capacite, x4 = enveloppe -> octets
ar_inflate:
	mov	x5,x4
	mov	x4,0
	b	infl_all

; x0 = source, x1 = octets, x2 = enveloppe -> taille
ar_inflate_size:
	prologue
	sub	sp,sp,SK_SIZE
	mov	x5,x2
	stp	xzr,xzr,[sp]
	stp	xzr,xzr,[sp,16]
	mov	x4,sp
	mov	x2,0
	mov	x3,0
	bl	infl_all
	add	sp,sp,SK_SIZE
	epilogue

; x0 = source, x1 = octets, x2 = enveloppe, x3 = taille prevue -> tampon
ar_inflate_new:
	ar_enter
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,x3
	bl	buf_new
	cbz	x0,.nomem
	mov	x19,x0
	cbz	x23,.go
	add	x0,x23,1			; (taille annoncee par l'appelant)
	str	x0,[x19,B_SINK+SK_CAP]
	bl	ar_alloc
	str	x0,[x19,B_SINK+SK_PTR]
	cbnz	x0,.go
	str	xzr,[x19,B_SINK+SK_CAP]
.go:	mov	x0,x20
	mov	x1,x21
	mov	x2,0
	mov	x3,0
	add	x4,x19,B_SINK
	mov	x5,x22
	bl	infl_all
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

include 'ar_deflate.inc'
include 'ar_inflate.inc'
include 'ar_read.inc'
include 'ar_write.inc'

segment readable writeable
kit_data
ar_kits_ok	dd 0
ar_end_of_data:
