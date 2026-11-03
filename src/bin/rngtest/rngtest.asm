; rngtest - essai du generateur aleatoire (appel 101 getrandom, random.asm) :
; le bloc ChaCha20 contre le vecteur de la RFC 8439 (2.3.2), puis deux tirages
; de 32 octets (differents), puis 1 Mo tire (debit) et le nombre de bits a 1
; (environ la moitie).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,cle				; vecteur : cle 00..1f, compteur 1,
	mov	x1,0				; nonce 000000090000004a00000000
.k:	strb	w1,[x0,x1]
	add	x1,x1,1
	cmp	x1,32
	b.lo	.k
	adr	x0,cle
	mov	w1,1
	adr	x2,nonce
	adr	x3,bloc
	bl	chacha20_block
	adr	x0,bloc
	adr	x1,attendu
	mov	x2,0
.c:	ldrb	w3,[x0,x2]
	ldrb	w4,[x1,x2]
	cmp	w3,w4
	b.ne	.faux
	add	x2,x2,1
	cmp	x2,16
	b.lo	.c
	adr	x0,m_rfc_ok
	bl	puts
	b	.tirage
.faux:	adr	x0,m_rfc_ko
	bl	puts
.tirage:
	mov	x19,2
.t:	adr	x0,bloc
	mov	x1,32
	mov	x8,SYS_GETRANDOM
	svc	0
	cmp	x0,32
	b.ne	.ko
	adr	x0,m_alea
	bl	puts
	adr	x0,bloc
	mov	x1,32
	bl	hexa
	subs	x19,x19,1
	b.ne	.t
	mov	x0,1048576			; debit, bits
	bl	malloc
	cbz	x0,.fin
	mov	x20,x0
	sys	SYS_TIME_MS
	mov	x21,x0
	mov	x22,0
.m:	add	x0,x20,x22
	mov	x1,65536
	mov	x8,SYS_GETRANDOM
	svc	0
	add	x22,x22,65536
	cmp	x22,1048576
	b.lo	.m
	sys	SYS_TIME_MS
	sub	x21,x0,x21
	mov	x22,0				; bits a 1
	mov	x0,0
.b:	ldr	x1,[x20,x0]
	fmov	d0,x1
	cnt	v0.8b,v0.8b
	addv	b0,v0.8b
	umov	w1,v0.b[0]
	add	x22,x22,x1
	add	x0,x0,8
	cmp	x0,1048576
	b.lo	.b
	adr	x0,m_mo
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_bits
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_sur
	bl	puts
.fin:	mov	x0,0
	sys	SYS_EXIT
.ko:	adr	x0,m_ko
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = octets, x1 = nombre : en hexadecimal, puis fin de ligne
hexa:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,144
	mov	x19,x0
	mov	x2,0
	mov	x3,sp
.o:	ldrb	w4,[x19,x2]
	lsr	w5,w4,4
	and	w4,w4,15
	cmp	w5,10
	add	w6,w5,'0'
	add	w7,w5,'a'-10
	csel	w5,w6,w7,lo
	strb	w5,[x3],1
	cmp	w4,10
	add	w6,w4,'0'
	add	w7,w4,'a'-10
	csel	w4,w6,w7,lo
	strb	w4,[x3],1
	add	x2,x2,1
	cmp	x2,x1
	b.lo	.o
	mov	w4,10
	strb	w4,[x3],1
	strb	wzr,[x3]
	mov	x0,sp
	bl	puts
	add	sp,sp,144
	ldp	x19,x20,[sp],16
	epilogue

include '../../inc/chacha20.inc'

m_rfc_ok	db 'rngtest : ChaCha20 conforme (RFC 8439, 2.3.2)',10,0
m_rfc_ko	db 'rngtest : ChaCha20 FAUX',10,0
m_alea		db 'rngtest : getrandom 32 o : ',0
m_ko		db 'rngtest : getrandom en erreur',10,0
m_mo		db 'rngtest : 1 Mo en ',0
m_bits		db ' ms, bits a 1 : ',0
m_sur		db ' sur 8388608',10,0
		align 4
nonce		db 0,0,0,9, 0,0,0,0x4A, 0,0,0,0
attendu		db 0x10,0xF1,0xE7,0xE4,0xD1,0x3B,0x59,0x15,0x50,0x0F,0xDD,0x1F,0xA3,0x20,0x71,0xC4
		align 8

segment readable writeable
kit_data
		align 8
cle		rb 32
bloc		rb 64
