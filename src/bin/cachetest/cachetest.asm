; ============================================================================
;  cachetest - une lecture voit-elle toujours la derniere ecriture, le cache de
;  fatfs etant plein de ce fichier ? Chaque etape ecrit (dans un bloc deja lu,
;  a cheval sur un bloc, troncature, fichier refait sur les memes clusters),
;  puis relit tout par 4, 64 et 256 Ko et compare a la reference.
;	cachetest [chemin]
;  Sortie : une ligne par echec, puis "cachetest : PASS / FAIL (n)".
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

TAILLE		= 0x40000

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,.def
	ldrb	w0,[x19]
	cbnz	w0,.go
.def:	adr	x19,p_def
.go:	mov	x20,0				; echecs
	mov	x23,TAILLE			; taille de la reference
	mov	x0,1				; 1 : fichier neuf
	mov	x1,0
	mov	x2,x23
	bl	motif
	bl	tout_ecrire
	bl	verifier
	mov	x0,2				; 2 : milieu d'un bloc, non aligne
	movl	x1,70001
	mov	x2,1000
	bl	morceau
	mov	x0,3				; 3 : un bloc entier, aligne
	movl	x1,0x10000
	movl	x2,0x10000
	bl	morceau
	mov	x0,4				; 4 : un secteur, aligne
	movl	x1,0x20000
	mov	x2,512
	bl	morceau
	mov	x0,5				; 5 : a cheval sur deux blocs
	movl	x1,0x2FF00
	mov	x2,512
	bl	morceau
	mov	x0,6				; 6 : la fin du fichier grandit
	mov	x1,x23
	mov	x2,4096
	bl	morceau
	movl	x23,100000			; 7 : troncature, plus court
	mov	x0,7
	mov	x1,0
	mov	x2,x23
	bl	motif
	bl	tout_ecrire
	bl	verifier
	mov	x0,x19				; 8 : effacer, refaire : memes clusters
	sys	SYS_UNLINK
	mov	x23,TAILLE
	mov	x0,8
	mov	x1,0
	mov	x2,x23
	bl	motif
	bl	tout_ecrire
	bl	verifier
	mov	x0,x19
	sys	SYS_UNLINK
	adr	x0,m_fin
	bl	puts
	cbz	x20,.pass
	adr	x0,m_fail
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_nlp
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
.pass:	adr	x0,m_pass
	bl	puts
	mov	x0,0
	sys	SYS_EXIT

; x0 = graine, x1 = position, x2 = octets : la reference a cet endroit
motif:
	adr	x3,ref
	add	x3,x3,x1
	mov	x4,0
.m:	cmp	x4,x2
	b.hs	.f
	add	x5,x4,x1
	mul	x6,x5,x0
	eor	x6,x6,x5,lsr 8
	eor	x6,x6,x5,lsr 16
	add	x6,x6,x0
	strb	w6,[x3,x4]
	add	x4,x4,1
	b	.m
.f:	ret

; x0 = graine, x1 = position, x2 = octets : ecrits sur place, puis verifie
morceau:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x1
	mov	x22,x2
	bl	motif
	add	x0,x21,x22
	cmp	x0,x23
	csel	x23,x0,x23,hi
	mov	x0,x19
	mov	x1,O_WRITE
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x24,x0
	mov	x1,x21
	mov	x2,0
	sys	SYS_SEEK
	mov	x0,x24
	adr	x1,ref
	add	x1,x1,x21
	mov	x2,x22
	sys	SYS_WRITE
	cmp	x0,x22
	b.ne	.err2
	mov	x0,x24
	sys	SYS_CLOSE
	bl	verifier
	b	.out
.err2:	mov	x0,x24
	sys	SYS_CLOSE
.err:	add	x20,x20,1
	adr	x0,m_ecr
	bl	puts
.out:	ldp	x21,x22,[sp],16
	epilogue

; Toute la reference dans le fichier, refait
tout_ecrire:
	prologue
	mov	x0,x19
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x24,x0
	adr	x1,ref
	mov	x2,x23
	sys	SYS_WRITE
	mov	x21,x0
	mov	x0,x24
	sys	SYS_CLOSE
	cmp	x21,x23
	b.eq	.out
.err:	add	x20,x20,1
	adr	x0,m_ecr
	bl	puts
.out:	epilogue

; Le fichier relu par 4, 64 et 256 Ko doit etre la reference, deux fois chaque
verifier:
	prologue
	stp	x21,x22,[sp,-16]!
	adr	x21,morceaux
.t:	ldr	w22,[x21],4
	cbz	w22,.out
	bl	relire
	bl	relire
	b	.t
.out:	ldp	x21,x22,[sp],16
	epilogue

; x22 = taille des lectures
relire:
	prologue
	stp	x25,x26,[sp,-16]!
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x24,x0
	mov	x25,0
.l:	mov	x0,x24
	adr	x1,lu
	add	x1,x1,x25
	mov	x2,x22
	sys	SYS_READ
	cmp	x0,0
	b.le	.f
	add	x25,x25,x0
	movl	x1,TAILLE*2
	cmp	x25,x1
	b.lo	.l
.f:	mov	x0,x24
	sys	SYS_CLOSE
	cmp	x25,x23
	b.ne	.err
	adr	x1,ref
	adr	x2,lu
	mov	x3,0
.c:	cmp	x3,x23
	b.hs	.out
	ldrb	w4,[x1,x3]
	ldrb	w5,[x2,x3]
	cmp	w4,w5
	b.ne	.err
	add	x3,x3,1
	b	.c
.err:	add	x20,x20,1
	adr	x0,m_lu
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_nlp
	bl	puts
.out:	ldp	x25,x26,[sp],16
	epilogue

		align 4
morceaux	dw 4096, 65536, 262144, 0
p_def	db 'SD0:/cachetest.bin',0
m_ecr	db 'cachetest : ecriture ratee',10,0
m_lu	db 'cachetest : differe, lectures de ',0
m_fin	db 'cachetest : ',0
m_pass	db 'PASS',10,0
m_fail	db 'FAIL (',0
m_nlp	db ')',10,0
		align 8

segment readable writeable
kit_data
		align 16
ref		rb TAILLE + 0x10000
lu		rb TAILLE * 2 + 0x40000
