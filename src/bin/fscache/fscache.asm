; fscache - le cache de donnees de fatfs (compteurs, essais avec et sans) :
;	fscache [volume] [on|off|drop]
; volume : sd0 par defaut, meme avec on, off ou drop seul ; off : coupe et vide,
; drop : vide, on : remet.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	adr	x20,nom				; "fs." + volume
	mov	x21,0				; op
	cbz	x19,.open
	ldrb	w0,[x19]
	cbz	w0,.open
	mov	x0,x19				; "fscache off" : sd0
	bl	mot_op
	mov	x21,x0
	cbnz	x21,.open
	add	x1,x20,3
.v:	ldrb	w0,[x19],1
	cbz	w0,.vf
	cmp	w0,' '
	b.eq	.vo
	sub	w2,w0,'A'			; (vfs : en minuscules)
	cmp	w2,25
	b.hi	.lc
	add	w0,w0,32
.lc:	strb	w0,[x1],1
	b	.v
.vo:	strb	wzr,[x1]
	mov	x0,x19
	bl	mot_op
	mov	x21,x0
	cbnz	x21,.open
	adr	x0,m_usage
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
.vf:	strb	wzr,[x1]
.open:	mov	x0,x20
	mov	x1,1000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.err
	adr	x1,msg
	mov	x2,FS_CACHE
	stp	x2,x21,[x1]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.err
	adr	x19,msg
	adr	x0,m_hits
	bl	lang_puts
	ldr	x0,[x19,MSG_A]
	bl	print_dec
	adr	x0,m_miss
	bl	lang_puts
	ldr	x0,[x19,MSG_B]
	bl	print_dec
	adr	x0,m_used
	bl	lang_puts
	ldr	x0,[x19,MSG_C]
	lsr	x0,x0,4
	bl	print_dec
	adr	x0,m_evict
	bl	lang_puts
	ldr	x0,[x19,MSG_D]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.err:	mov	x19,x0
	adr	x0,m_err
	bl	lang_puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = mot -> x0 = FS_CC_*, 0 s'il n'est pas on, off ni drop
mot_op:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,s_on
	bl	str_eq
	mov	x20,FS_CC_ON
	cbnz	x0,.got
	mov	x0,x19
	adr	x1,s_off
	bl	str_eq
	mov	x20,FS_CC_OFF
	cbnz	x0,.got
	mov	x0,x19
	adr	x1,s_drop
	bl	str_eq
	mov	x20,FS_CC_DROP
	cbnz	x0,.got
	mov	x20,0
.got:	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

m_hits	db 'succes ',0
m_miss	db ', defauts ',0
m_used	db ', en memoire ',0
m_evict	db ' Mo, remplaces ',0
m_nl	db 10,0
m_err	db 'fscache : erreur ',0
s_on	db 'on',0
s_off	db 'off',0
s_drop	db 'drop',0
m_usage	db 'fscache [volume] [on|off|drop]',10,0
	align 8

segment readable writeable
nom	db 'fs.sd0',0
	rb 32
kit_data
	align 8
msg	rb MSG_TAILLE
