; ============================================================================
;  writetest - ecrit un fichier de motif sur la carte, le relit et compare.
;  Le fichier reste (SD0:/writetest.bin) : tools ou PC peuvent le relire ; le
;  motif : mot n du fichier = n x 0x9E3779B97F4A7C15, sur 64 bits.
;  Sortie : duree d'ecriture, puis « writetest : PASS / FAIL ».
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

TOTAL		= 0x800000		; 8 Mo
MORCEAU		= 0x40000		; 256 Ko
MULT		= 0x9E3779B97F4A7C15

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	bl	kits_open
	adr	x0,p_fic
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x20,x0
	sys	SYS_TIME_MS
	mov	x23,x0
	mov	x21,0				; octets
.w:	movl	x0,TOTAL
	cmp	x21,x0
	b.hs	.wfin
	mov	x0,x21
	bl	remplit
	mov	x0,x20
	adr	x1,tampon
	movl	x2,MORCEAU
	sys	SYS_WRITE
	movl	x1,MORCEAU
	cmp	x0,x1
	b.ne	.err
	add	x21,x21,x1
	b	.w
.wfin:	mov	x0,x20
	sys	SYS_CLOSE
	sys	SYS_TIME_MS
	sub	x23,x0,x23
	adr	x0,m_ms
	bl	puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,p_fic			; relecture
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x20,x0
	mov	x21,0
.r:	movl	x0,TOTAL
	cmp	x21,x0
	b.hs	.bon
	mov	x0,x21
	bl	remplit
	mov	x0,x20
	adr	x1,relu
	movl	x2,MORCEAU
	sys	SYS_READ
	movl	x1,MORCEAU
	cmp	x0,x1
	b.ne	.faux
	adr	x0,tampon
	adr	x2,relu
	mov	x3,0
.c:	ldr	x4,[x0,x3]
	ldr	x5,[x2,x3]
	cmp	x4,x5
	b.ne	.faux
	add	x3,x3,8
	cmp	x3,x1
	b.lo	.c
	add	x21,x21,x1
	b	.r
.bon:	adr	x0,m_pass
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.faux:	adr	x0,m_faux
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
.err:	mov	x19,x0
	adr	x0,m_err
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,2
	sys	SYS_EXIT

; x0 = position dans le fichier : tampon <- le motif de ce morceau
remplit:
	lsr	x0,x0,3				; numero du mot
	movl	x1,MULT
	adr	x2,tampon
	movl	x3,MORCEAU/8
.m:	mul	x4,x0,x1
	str	x4,[x2],8
	add	x0,x0,1
	subs	x3,x3,1
	b.ne	.m
	ret

p_fic	db 'SD0:/writetest.bin',0
m_ms	db 'writetest : ecrit en ms : ',0
m_pass	db 'writetest : PASS',10,0
m_faux	db 'writetest : FAIL a ',0
m_err	db 'writetest : erreur ',0
m_nl	db 10,0
	align 8

segment readable writeable
kit_data
	align 16
tampon	rb MORCEAU
relu	rb MORCEAU
