; bus - le bus d'evenements du systeme (busd, docs/BUS.md) en ligne de commande :
;	bus list			les sujets et leur valeur
;	bus get <nom>			la valeur d'un sujet
;	bus pub [-r] <nom> <valeur>	publie (-r : valeur gardee, BUS_RETAIN)
;	bus del <nom>			retire un sujet
;	bus watch [-c] <motif>		affiche les evenements jusqu'a Ctrl+C
;					(-c : d'abord les valeurs gardees)
; Motif : un nom, ou un prefixe suivi de "*" ("korin.usb.*", "*" : tout).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/buskit/buskit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so

start:
	mov	x19,x0				; arguments
	bl	kits_open
	cbz	x19,usage
	mov	x0,x19
	bl	next_word
	mov	x19,x0				; la commande
	mov	x20,x1				; le reste
	ldrb	w0,[x19]
	cbz	w0,usage
	bl	bus_available
	cbz	x0,absent
	mov	x0,x19
	adr	x1,s_list
	bl	str_eq
	cbnz	x0,cmd_list
	mov	x0,x19
	adr	x1,s_get
	bl	str_eq
	cbnz	x0,cmd_get
	mov	x0,x19
	adr	x1,s_pub
	bl	str_eq
	cbnz	x0,cmd_pub
	mov	x0,x19
	adr	x1,s_del
	bl	str_eq
	cbnz	x0,cmd_del
	mov	x0,x19
	adr	x1,s_watch
	bl	str_eq
	cbnz	x0,cmd_watch
usage:	adr	x0,m_usage
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT
absent:	adr	x0,m_absent
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT
fail:	cmp	x0,BUS_ENOENT			; x0 = BUS_E*
	b.ne	.e
	adr	x0,m_noent
	bl	lang_puts
	b	.x
.e:	mov	x19,x0
	adr	x0,m_error
	bl	lang_puts
	mov	x0,x19
	bl	print_dec
	adr	x0,s_nl
	bl	puts
.x:	mov	x0,1
	sys	SYS_EXIT
done:	mov	x0,0
	sys	SYS_EXIT

; bus list
cmd_list:
	mov	x21,0
.l:	mov	x0,x21
	adr	x1,t_name
	bl	bus_list
	cbz	x0,done
	mov	x21,x0
	adr	x0,t_name
	bl	puts
	adr	x0,s_eq
	bl	puts
	mov	x0,x21
	adr	x1,t_value
	mov	x2,BUS_VALUE_MAX
	bl	bus_get_id
	tbz	x0,63,.v
	mov	x0,0				; (retire entre-temps)
.v:	mov	x1,x0
	adr	x0,t_value
	bl	print_value
	b	.l

; bus get <nom>
cmd_get:
	mov	x0,x20
	bl	next_word
	ldrb	w1,[x0]
	cbz	w1,usage
	adr	x1,t_value
	mov	x2,BUS_VALUE_MAX
	bl	bus_get
	tbnz	x0,63,fail
	mov	x1,x0
	adr	x0,t_value
	bl	print_value
	b	done

; bus pub [-r] <nom> <valeur>
cmd_pub:
	mov	x21,0
	mov	x0,x20
	bl	next_word
	mov	x22,x0
	mov	x20,x1
	adr	x1,s_r
	bl	str_eq
	cbz	x0,.name
	mov	x21,BUS_RETAIN
	mov	x0,x20
	bl	next_word
	mov	x22,x0
	mov	x20,x1
.name:	ldrb	w0,[x22]
	cbz	w0,usage
	mov	x0,x20				; la valeur : le reste de la ligne
.sp:	ldrb	w1,[x0]
	cmp	w1,' '
	b.ne	.pub
	add	x0,x0,1
	b	.sp
.pub:	mov	x1,x0
	mov	x0,x22
	mov	x2,-1
	mov	x3,x21
	bl	bus_publish
	tbnz	x0,63,fail
	mov	x19,x0
	adr	x0,m_pub
	bl	lang_puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_pub2
	bl	lang_puts
	b	done

; bus del <nom>
cmd_del:
	mov	x0,x20
	bl	next_word
	ldrb	w1,[x0]
	cbz	w1,usage
	adr	x1,t_value
	mov	x2,0
	mov	x3,BUS_DELETE
	bl	bus_publish
	tbnz	x0,63,fail
	b	done

; bus watch [-c] <motif>
cmd_watch:
	mov	x21,0
	mov	x0,x20
	bl	next_word
	mov	x22,x0
	mov	x20,x1
	adr	x1,s_c
	bl	str_eq
	cbz	x0,.pat
	mov	x21,BUS_CURRENT
	mov	x0,x20
	bl	next_word
	mov	x22,x0
.pat:	ldrb	w0,[x22]
	cbz	w0,usage
	mov	x0,x22
	mov	x1,x21
	bl	bus_subscribe
	tbnz	x0,63,fail
	adr	x19,ev
.w:	mov	x0,x19
	mov	x1,60000
	bl	bus_wait
	cmp	x0,WEV_BUS
	b.ne	.w
	ldr	w0,[x19,BEV_LEN]
	cmn	w0,1
	b.eq	.del				; BUS_DELETED
	ldr	w0,[x19,BEV_TOPIC]
	adr	x1,t_name
	bl	bus_name
	adr	x0,t_name
	bl	puts
	adr	x0,s_eq
	bl	puts
	ldr	w1,[x19,BEV_LEN]		; courte : celle de l'evenement
	cmp	x1,BUS_INLINE
	b.hi	.long
	add	x0,x19,BEV_DATA
	bl	print_value
	b	.w
.long:	ldr	w0,[x19,BEV_TOPIC]		; longue : la valeur du moment
	adr	x1,t_value
	mov	x2,BUS_VALUE_MAX
	bl	bus_get_id
	tbz	x0,63,.lv
	mov	x0,0
.lv:	mov	x1,x0
	adr	x0,t_value
	bl	print_value
	b	.w
.del:	adr	x0,m_topic
	bl	lang_puts
	ldr	w0,[x19,BEV_TOPIC]
	bl	print_dec
	adr	x0,m_deleted
	bl	lang_puts
	b	.w

; x0 = texte -> x0 = premier mot (espaces sautes, coupe par un 0), x1 = la
; suite
next_word:
.sp:	ldrb	w1,[x0]
	cmp	w1,' '
	b.ne	.st
	add	x0,x0,1
	b	.sp
.st:	mov	x1,x0
.w:	ldrb	w2,[x1]
	cbz	w2,.end
	cmp	w2,' '
	b.eq	.cut
	add	x1,x1,1
	b	.w
.cut:	strb	wzr,[x1],1
.end:	ret

; x0 = valeur, x1 = octets : sur une ligne (controles en ".")
print_value:
	prologue
	mov	x2,BUS_VALUE_MAX
	cmp	x1,x2
	csel	x1,x1,x2,lo
	adr	x2,t_line
	mov	x3,0
.c:	cmp	x3,x1
	b.hs	.e
	ldrb	w4,[x0,x3]
	cmp	w4,' '
	b.hs	.k
	mov	w4,'.'
.k:	strb	w4,[x2,x3]
	add	x3,x3,1
	b	.c
.e:	mov	w4,10
	strb	w4,[x2,x3]
	add	x3,x3,1
	strb	wzr,[x2,x3]
	adr	x0,t_line
	bl	puts
	epilogue

s_list		db 'list',0
s_get		db 'get',0
s_pub		db 'pub',0
s_del		db 'del',0
s_watch		db 'watch',0
s_r		db '-r',0
s_c		db '-c',0
s_eq		db ' = ',0
s_nl		db 10,0
m_usage		db 'usage : bus list | get <nom> | pub [-r] <nom> <valeur> | del <nom> | watch [-c] <motif>',10,0
m_absent	db 'bus : busd ne r',0xC3,0xA9,'pond pas',10,0
m_noent		db 'bus : sujet inconnu',10,0
m_error		db 'bus : erreur ',0
m_pub		db 'bus : ',0
m_pub2		db ' abonnement(s) pr',0xC3,0xA9,'venu(s)',10,0
m_topic		db 'sujet ',0
m_deleted	db ' : retir',0xC3,0xA9,10,0
		align 8

segment readable writeable
kit_data
		align 8
ev		rb 32
t_name		rb BUS_NAME_MAX
t_value		rb BUS_VALUE_MAX
t_line		rb BUS_VALUE_MAX + 8
