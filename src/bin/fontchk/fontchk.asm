; fontchk - lit deux fois un fichier dans deux tampons et compare
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x19,path
	mov	x0,x19
	adr	x1,stbuf
	sys	SYS_STAT
	adr	x0,stbuf
	ldr	x20,[x0]
	mov	x0,x20
	bl	malloc
	mov	x21,x0
	mov	x0,x20
	bl	malloc
	mov	x22,x0
	mov	x1,x21
	bl	readall
	mov	x1,x22
	bl	readall
	mov	x23,0
	mov	x24,0				; ecarts
.c:	cmp	x23,x20
	b.hs	.e
	ldrb	w0,[x21,x23]
	ldrb	w1,[x22,x23]
	cmp	w0,w1
	b.eq	.n
	cbnz	x24,.cnt
	adr	x0,m1
	bl	lang_puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m2
	bl	puts
	add	x0,x21,x23
	bl	print_dec
	adr	x0,m3
	bl	puts
	add	x0,x22,x23
	bl	print_dec
	adr	x0,nl
	bl	puts
.cnt:	add	x24,x24,1
.n:	add	x23,x23,1
	b	.c
.e:	adr	x0,m4
	bl	lang_puts
	mov	x0,x24
	bl	print_dec
	adr	x0,nl
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
; x1 = tampon
readall:
	prologue
	str	x1,[sp,-16]!
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	ldr	x1,[sp]
	mov	x2,x20
	str	x0,[sp,8]
	sys	SYS_READ
	ldr	x0,[sp,8]
	sys	SYS_CLOSE
	add	sp,sp,16
	epilogue
segment readable writeable
kit_data
path	db 'SD0:/fonts/DejaVuSans.ttf',0
m1	db 'fontchk : premier ecart a ',0
m2	db ' (adr1 ',0
m3	db ', adr2 ',0
m4	db 'fontchk : octets differents ',0
nl	db 10,0
	align 8
stbuf	dd 0,0
