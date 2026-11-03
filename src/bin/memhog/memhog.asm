; memhog - essai de la memoire rare : un cache plein doit se vider a temps.
;	memhog read <chemin>	lit tout le fichier (remplit le cache de fatfs)
;	memhog touch <Mo>	mmap et touche toute la memoire libre + <Mo>
;	memhog sbrk <Mo>	la meme chose d'un seul appel sbrk
; touch et sbrk ne passent que si le cache rend au moins <Mo> : sinon le process
; est tue, ou sbrk echoue. Sortie : pages obtenues et temps, puis la memoire
; reste prise jusqu'a la fin du programme.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

CHUNK		= 0x100000

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,.usage
	mov	x20,x19				; deuxieme mot
.w:	ldrb	w0,[x20]
	cbz	w0,.usage
	add	x20,x20,1
	cmp	w0,' '
	b.ne	.w
	strb	wzr,[x20,-1]
	mov	x0,x19
	adr	x1,s_read
	bl	str_eq
	cbnz	x0,lire
	mov	x0,x19
	adr	x1,s_touch
	bl	str_eq
	mov	x21,x0
	mov	x0,x19
	adr	x1,s_sbrk
	bl	str_eq
	orr	x0,x0,x21
	cbnz	x0,prendre
.usage:	adr	x0,m_usage
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

lire:	mov	x0,x20
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,echec
	mov	x22,x0
	mov	x23,0
.l:	mov	x0,x22
	adr	x1,tampon
	movl	x2,CHUNK
	sys	SYS_READ
	cmp	x0,0
	b.le	.f
	add	x23,x23,x0
	b	.l
.f:	mov	x0,x22
	sys	SYS_CLOSE
	lsr	x0,x23,20
	bl	print_dec
	adr	x0,m_lu
	bl	puts
	mov	x0,0
	sys	SYS_EXIT

; x20 = <Mo>, x21 = 1 : touch, 0 : sbrk
prendre:
	mov	x22,0				; Mo demandes
.a:	ldrb	w0,[x20],1
	sub	w0,w0,'0'
	cmp	w0,9
	b.hi	.af
	mov	x1,10
	mul	x22,x22,x1
	add	x22,x22,x0
	b	.a
.af:	mov	x0,0
	adr	x1,stats
	sys	SYS_VM_STATS
	tbnz	x0,63,echec
	adr	x1,stats
	ldp	x23,x0,[x1,VS_FREE_LOW]
	add	x23,x23,x0			; pages libres
	add	x24,x23,x22,lsl 8		; + <Mo>
	mov	x0,x23
	lsr	x0,x0,8
	bl	print_dec
	adr	x0,m_libre
	bl	puts
	lsr	x0,x24,8
	bl	print_dec
	adr	x0,m_veut
	bl	puts
	sys	SYS_TIME_MS
	mov	x25,x0
	cbz	x21,.sb
	mov	x0,0
	lsl	x1,x24,12
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	cmp	x0,0
	b.lo	echec
	mov	x1,x24
	mov	w2,1
.t:	str	w2,[x0]
	add	x0,x0,4096
	subs	x1,x1,1
	b.ne	.t
	b	.bon
.sb:	lsl	x0,x24,12
	sys	SYS_SBRK
	tbnz	x0,63,echec
.bon:	sys	SYS_TIME_MS
	sub	x25,x0,x25
	adr	x0,m_ok
	bl	puts
	mov	x0,x25
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	mov	x0,0
	sys	SYS_EXIT

echec:	mov	x19,x0
	adr	x0,m_err
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

s_read	db 'read',0
s_touch	db 'touch',0
s_sbrk	db 'sbrk',0
m_usage	db 'memhog read <chemin> | touch <Mo> | sbrk <Mo>',10,0
m_lu	db ' Mo lus',10,0
m_libre	db ' Mo libres, ',0
m_veut	db ' Mo demandes',10,0
m_ok	db 'memhog : obtenu en ',0
m_ms	db ' ms',10,0
m_err	db 'memhog : erreur ',0
m_nl	db 10,0
	align 8

segment readable writeable
kit_data
	align 16
stats	rb 64
tampon	rb CHUNK
