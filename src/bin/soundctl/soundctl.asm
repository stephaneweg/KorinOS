; soundctl - le mixer en ligne de commande (service sound, par AudioKit ;
; docs/SON.md 4) : utile par telnet, et pour les essais.
;   soundctl                         etat : sorties, principale, clients, entrees
;   soundctl volume <sortie> <0..100> [muet]
;   soundctl app <pid|nom> <0..100> [muet]
;   soundctl route <pid|nom> <sortie>       (0 : la principale)
;   soundctl main <sortie>                  (0 : automatique)
; [muet] : 1 pour couper, 0 (ou rien) pour remettre.
include '../../inc/armpi.inc'
include '../../inc/sound.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so

start:
	stp	x0,x1,[sp,-16]!
	bl	kits_open
	ldp	x0,x1,[sp],16
	mov	x19,x0
	bl	ak_state
	cbz	x0,.nosvc
	adr	x1,stbuf
	str	x0,[x1]
	cbz	x19,show
	mov	x0,x19
	bl	next_word				; la commande
	mov	x19,x1
	mov	x20,x0
	adr	x1,c_volume
	bl	str_eq
	cbnz	x0,cmd_volume
	mov	x0,x20
	adr	x1,c_app
	bl	str_eq
	cbnz	x0,cmd_app
	mov	x0,x20
	adr	x1,c_route
	bl	str_eq
	cbnz	x0,cmd_route
	mov	x0,x20
	adr	x1,c_main
	bl	str_eq
	cbnz	x0,cmd_main
	adr	x0,m_usage
	bl	lang_puts
	mov	x0,2
	sys	SYS_EXIT
.nosvc:	adr	x0,m_nosvc
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = resultat : « soundctl : 0 » et fin (code 0 si >= 0)
done:
	mov	x19,x0
	adr	x0,m_res
	bl	lang_puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	lsr	x0,x19,63
	sys	SYS_EXIT

cmd_volume:
	mov	x0,x19
	bl	num_arg				; sortie
	mov	x20,x0
	mov	x0,x1
	bl	num_arg				; volume
	mov	x21,x0
	mov	x0,x1
	bl	num_arg				; muet
	mov	x2,x0
	mov	x0,x20
	mov	x1,x21
	bl	ak_out_volume
	b	done

cmd_app:
	mov	x0,x19
	bl	pid_arg
	cbz	x0,noproc
	mov	x20,x0
	mov	x0,x1
	bl	num_arg
	mov	x21,x0
	mov	x0,x1
	bl	num_arg
	mov	x2,x0
	mov	x0,x20
	mov	x1,x21
	bl	ak_client_volume
	b	done

cmd_route:
	mov	x0,x19
	bl	pid_arg
	cbz	x0,noproc
	mov	x20,x0
	mov	x0,x1
	bl	num_arg
	mov	x1,x0
	mov	x0,x20
	bl	ak_route
	b	done

noproc:	adr	x0,m_noproc
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

cmd_main:
	mov	x0,x19
	bl	num_arg
	bl	ak_main
	b	done

; x0 = texte -> x0 = premier mot (termine par 0), x1 = suite (0 : fin)
next_word:
	ldrb	w2,[x0]
	cmp	w2,' '
	b.ne	.s
	add	x0,x0,1
	b	next_word
.s:	mov	x1,x0
.e:	ldrb	w2,[x1]
	cbz	w2,.end
	cmp	w2,' '
	b.eq	.cut
	add	x1,x1,1
	b	.e
.cut:	strb	wzr,[x1]
	add	x1,x1,1
	ret
.end:	mov	x1,0
	ret

; x0 = suite des arguments (0 : aucune) -> x0 = nombre (0 si absent), x1 = suite
num_arg:
	prologue
	cbz	x0,.zero
	bl	next_word
	str	x1,[sp,-16]!
	bl	parse_dec
	ldr	x1,[sp],16
	b	.out
.zero:	mov	x0,0
	mov	x1,0
.out:	epilogue

; x0 = suite -> x0 = pid (chiffres, ou nom d'un client du mixer, ou d'un
; process), x1 = suite
pid_arg:
	prologue
	stp	x19,x20,[sp,-16]!
	cbz	x0,.zero
	bl	next_word
	mov	x20,x1
	mov	x19,x0
	ldrb	w1,[x19]
	sub	w1,w1,'0'
	cmp	w1,9
	b.hi	.name
	bl	parse_dec
	b	.out
.name:	adr	x0,stbuf				; un client de ce nom
	ldr	x0,[x0]
	add	x2,x0,SS_CLIS
	mov	x3,SS_CLI_MAX
.c:	ldr	w4,[x2,SC_PID]
	cbz	w4,.cn
	stp	x2,x3,[sp,-16]!
	add	x0,x2,SC_NAME
	mov	x1,x19
	bl	str_eq
	ldp	x2,x3,[sp],16
	cbz	x0,.cn
	ldr	w0,[x2,SC_PID]
	b	.out
.cn:	add	x2,x2,64
	subs	x3,x3,1
	b.ne	.c
	mov	x0,x19				; sinon un process de ce nom
	mov	x1,0				; (procfind.inc)
	bl	proc_find
	b	.out
.zero:	mov	x0,0
.out:	mov	x1,x20
	ldp	x19,x20,[sp],16
	epilogue

; Etat du mixer
show:
	adr	x0,stbuf
	ldr	x19,[x0]
	adr	x0,m_main
	bl	lang_puts
	ldr	x0,[x19,SS_MAIN]
	bl	print_dec
	adr	x0,m_gen
	bl	lang_puts
	ldr	x0,[x19,SS_GEN]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	add	x20,x19,SS_OUTS			; sorties
	mov	x21,SS_OUT_MAX
.o:	ldr	w0,[x20,SO_ID]
	cbz	w0,.on
	adr	x0,m_out
	bl	lang_puts
	ldr	w0,[x20,SO_ID]
	bl	print_dec
	adr	x0,m_q1
	bl	puts
	add	x0,x20,SO_NAME
	bl	puts
	adr	x0,m_q2
	bl	puts
	ldr	w0,[x20,SO_TYPE]
	adr	x1,types
	ldrb	w0,[x1,x0]
	add	x0,x1,x0
	bl	lang_puts
	adr	x0,m_sp
	bl	puts
	ldr	w0,[x20,SO_RATE]
	bl	print_dec
	adr	x0,m_hzvol
	bl	puts
	ldr	w0,[x20,SO_VOLUME]
	bl	print_dec
	ldr	w0,[x20,SO_MUTE]
	cbz	w0,.f
	adr	x0,m_mute
	bl	lang_puts
.f:	ldr	w0,[x20,SO_FLAGS]
	tbz	w0,0,.f2
	adr	x0,m_ismain
	bl	lang_puts
.f2:	ldr	w0,[x20,SO_FLAGS]
	tbz	w0,1,.pk
	adr	x0,m_pref
	bl	lang_puts
.pk:	adr	x0,m_peak
	bl	lang_puts
	ldr	w0,[x20,SO_PEAK]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.on:	add	x20,x20,64
	subs	x21,x21,1
	b.ne	.o
	add	x20,x19,SS_CLIS			; clients
	mov	x21,SS_CLI_MAX
.c:	ldr	w0,[x20,SC_PID]
	cbz	w0,.cn
	adr	x0,m_cli
	bl	puts
	ldr	w0,[x20,SC_PID]
	bl	print_dec
	adr	x0,m_q1
	bl	puts
	add	x0,x20,SC_NAME
	bl	puts
	adr	x0,m_q3
	bl	puts
	ldr	w0,[x20,SC_VOLUME]
	bl	print_dec
	ldr	w0,[x20,SC_MUTE]
	cbz	w0,.cm
	adr	x0,m_mute
	bl	lang_puts
.cm:	adr	x0,m_on
	bl	lang_puts
	ldr	w0,[x20,SC_OUTPUT]
	bl	print_dec
	adr	x0,m_route
	bl	lang_puts
	ldr	w0,[x20,SC_ROUTE]
	bl	print_dec
	adr	x0,m_streams
	bl	lang_puts
	ldr	w0,[x20,SC_STREAMS]
	bl	print_dec
	adr	x0,m_peak
	bl	lang_puts
	ldr	w0,[x20,SC_PEAK]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.cn:	add	x20,x20,64
	subs	x21,x21,1
	b.ne	.c
	movl	x0,SS_INS			; entrees
	add	x20,x19,x0
	mov	x21,SS_IN_MAX
.i:	ldr	w0,[x20,SO_ID]
	cbz	w0,.in
	adr	x0,m_in
	bl	lang_puts
	ldr	w0,[x20,SO_ID]
	bl	print_dec
	adr	x0,m_q1
	bl	puts
	add	x0,x20,SO_NAME
	bl	puts
	adr	x0,m_q2
	bl	puts
	ldr	w0,[x20,SO_RATE]
	bl	print_dec
	adr	x0,m_hz
	bl	puts
.in:	add	x20,x20,64
	subs	x21,x21,1
	b.ne	.i
	mov	x0,0
	sys	SYS_EXIT

		align 4
include '../../inc/procfind.inc'		; proc_find

segment readable writeable
kit_data
c_volume	db 'volume',0
c_app		db 'app',0
c_route		db 'route',0
c_main		db 'main',0
m_usage		db 'usage : soundctl [volume <sortie> <0..100> [muet] | app <pid|nom> <0..100> [muet] | route <pid|nom> <sortie> | main <sortie>]',10,0
m_noproc	db 'soundctl : pas de process de ce nom',10,0
m_nosvc		db 'soundctl : pas de service sound',10,0
m_res		db 'soundctl : ',0
m_main		db 'sortie principale : ',0
m_gen		db ' (g',0xC3,0xA9,'n',0xC3,0xA9,'ration ',0
m_out		db 'sortie ',0
m_in		db 'entr',0xC3,0xA9,'e ',0
m_cli		db 'client ',0
m_q1		db ' ',0xC2,0xAB,' ',0
m_q2		db ' ',0xC2,0xBB,' ',0
m_q3		db ' ',0xC2,0xBB,' volume ',0
m_sp		db ' ',0
m_hzvol		db ' Hz, volume ',0
m_hz		db ' Hz',10,0
m_mute		db ' (muet)',0
m_ismain	db ', principale',0
m_pref		db ', pr',0xC3,0xA9,'f',0xC3,0xA9,'r',0xC3,0xA9,'e',0
m_peak		db ', cr',0xC3,0xAA,'te ',0
m_on		db ', sur la sortie ',0
m_route		db ' (choisie : ',0
m_streams	db '), flux ',0
m_nl		db 10,0
types		db 4, 14, 19, 24	; (decalages depuis types)
		db 'virtuelle',0, 'jack',0, 'HDMI',0, 'USB',0
		align 8
stbuf		dd 0
