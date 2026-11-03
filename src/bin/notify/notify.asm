; notify - « notify <titre> <message> » : une notification (notifyd, comme
; dans Onyx). Un titre de plusieurs mots s'ecrit entre guillemets :
;	notify "Cle USB" USB0: est pret
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0				; arguments
	bl	kits_open
	cbz	x19,.usage
.esp:	ldrb	w0,[x19]			; espaces devant
	cmp	w0,' '
	b.ne	.titre
	add	x19,x19,1
	b	.esp
.titre:	cbz	w0,.usage
	cmp	w0,'"'				; « "titre en plusieurs mots" »
	b.ne	.mot
	add	x19,x19,1
	mov	x20,x19
.g:	ldrb	w0,[x20]
	cbz	w0,.seul
	cmp	w0,'"'
	b.eq	.coupe
	add	x20,x20,1
	b	.g
.mot:	mov	x20,x19				; un mot
.m:	ldrb	w0,[x20]
	cbz	w0,.seul
	cmp	w0,' '
	b.eq	.coupe
	add	x20,x20,1
	b	.m
.coupe:	strb	wzr,[x20],1
.e2:	ldrb	w0,[x20]			; le message : le reste
	cmp	w0,' '
	b.ne	.envoi
	add	x20,x20,1
	b	.e2
.seul:	adr	x20,s_vide			; titre sans message
.envoi:	mov	x0,x19
	mov	x1,x20
	bl	notifier
	tbz	x0,63,.fin
	adr	x0,m_absent
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT
.usage:	adr	x0,m_usage
	bl	lang_puts
.fin:	mov	x0,0
	sys	SYS_EXIT

include '../../inc/notify.inc'

s_vide		db 0
m_usage		db 'usage : notify <titre> <message>  (titre en plusieurs mots : entre guillemets)',10,0
m_absent	db 'notify : notifyd absent (pas de bureau ?)',10,0
		align 8

segment readable writeable
kit_data
