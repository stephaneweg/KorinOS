; volume - le volume du son, simplement (docs/OUTILS.md, d'apres volume.c d'Onyx) : la sortie
; principale du mixer (service sound, par AudioKit). soundctl reste l'outil complet (chaque
; sortie par son numero, routes) ; volume fait les gestes courants avec des mots.
;   volume                          le volume de la sortie principale
;   volume 0..100                   le regle (et remet le son)
;   volume mute | unmute | toggle   coupe, remet, bascule
;   volume output                   les sorties, laquelle joue
;   volume output auto|jack|hdmi|usb|<numero>|<nom>   la sortie preferee (auto : le choix de sound)
;   volume apps                     les programmes qui jouent, le volume de chacun
;   volume app <nom|pid> 0..100|mute|unmute           le volume d'un programme
; Les reglages sont retenus par sound (SD0:/etc/sound.txt).
include '../../inc/armpi.inc'
include '../../inc/sound.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so

start:
	bl	t_debut
	bl	ak_state
	cbz	x0,.nosvc
	mov	x27,x0				; etat (lecture seule, tenu a jour par sound)
	cmp	x19,2
	b.lo	show
	ldr	x21,[x20,8]
	mov	x0,x21
	adr	x1,c_mute
	bl	str_eq
	cbnz	x0,cmd_mute
	mov	x0,x21
	adr	x1,c_unmute
	bl	str_eq
	cbnz	x0,cmd_unmute
	mov	x0,x21
	adr	x1,c_toggle
	bl	str_eq
	cbnz	x0,cmd_toggle
	mov	x0,x21
	adr	x1,c_output
	bl	str_eq
	cbnz	x0,cmd_output
	mov	x0,x21
	adr	x1,c_apps
	bl	str_eq
	cbnz	x0,cmd_apps
	mov	x0,x21
	adr	x1,c_app
	bl	str_eq
	cbnz	x0,cmd_app
	cmp	x19,2
	b.ne	usage
	mov	x0,x21
	bl	num100
	tbnz	x0,63,usage
	mov	x22,x0
	bl	main_out
	cbz	x0,noout
	ldr	w0,[x0,SO_ID]
	mov	x1,x22
	mov	x2,0
	bl	ak_out_volume
	tbnz	x0,63,failed
	mov	x0,50				; le temps que sound publie son etat
	sys	SYS_SLEEP
	b	show
.nosvc:	adr	x0,m_nosvc
	bl	t_err_msg
	mov	x0,1
	bl	t_fin

usage:	adr	x0,m_usage
	bl	t_err_msg
	mov	x0,2
	bl	t_fin

noout:	adr	x0,m_noout
	bl	t_err_msg
	mov	x0,1
	bl	t_fin

failed:	adr	x0,m_failed
	bl	t_err_msg
	mov	x0,1
	bl	t_fin

; x0 = texte -> x0 = 0..100, -1 si ce n'en est pas un
num100:
	mov	x1,0
	ldrb	w2,[x0]
	cbz	w2,.non
.c:	ldrb	w2,[x0],1
	cbz	w2,.f
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.non
	mov	x3,10
	mul	x1,x1,x3
	add	x1,x1,x2
	cmp	x1,100
	b.hi	.non
	b	.c
.f:	mov	x0,x1
	ret
.non:	mov	x0,-1
	ret

; -> x0 = entree SO_* de la sortie principale (0 : aucune)
main_out:
	ldr	x1,[x27,SS_MAIN]
	cbz	x1,.non
	add	x0,x27,SS_OUTS
	mov	x2,SS_OUT_MAX
.c:	ldr	w3,[x0,SO_ID]
	cmp	x3,x1
	b.eq	.r
	add	x0,x0,64
	subs	x2,x2,1
	b.ne	.c
.non:	mov	x0,0
.r:	ret

cmd_mute:
	mov	x22,1
	b	set_mute
cmd_unmute:
	mov	x22,0
	b	set_mute
cmd_toggle:
	bl	main_out
	cbz	x0,noout
	ldr	w1,[x0,SO_MUTE]
	cmp	w1,0
	cset	x22,eq
set_mute:
	bl	main_out
	cbz	x0,noout
	ldr	w1,[x0,SO_VOLUME]
	ldr	w0,[x0,SO_ID]
	mov	x2,x22
	bl	ak_out_volume
	tbnz	x0,63,failed
	mov	x0,50
	sys	SYS_SLEEP
	; (tombe dans show)

; « volume 70 % (muet), sortie 1 « jack » »
show:
	bl	main_out
	cbz	x0,noout
	mov	x21,x0
	adr	x0,m_volume
	bl	t_dire
	ldr	w0,[x21,SO_VOLUME]
	bl	t_putn
	adr	x0,m_pct
	bl	t_puts
	ldr	w0,[x21,SO_MUTE]
	cbz	w0,.s
	adr	x0,m_mute
	bl	t_dire
.s:	adr	x0,m_on
	bl	t_dire
	mov	x0,x21
	bl	out_desc
	bl	t_nl
	mov	x0,0
	bl	t_fin

; x0 = entree SO_* : « 1 « jack » (jack) »
out_desc:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	w0,[x19,SO_ID]
	bl	t_putn
	adr	x0,m_q1
	bl	t_puts
	add	x0,x19,SO_NAME
	bl	t_puts
	adr	x0,m_q2
	bl	t_puts
	ldr	w0,[x19,SO_TYPE]
	cmp	w0,3
	b.ls	.t
	mov	w0,0
.t:	adr	x1,types
	ldrb	w0,[x1,x0]
	add	x0,x1,x0
	bl	t_dire
	adr	x0,m_par2
	bl	t_puts
	ldr	x19,[sp],16
	epilogue

; volume output [choix]
cmd_output:
	cmp	x19,3
	b.lo	.liste
	ldr	x21,[x20,16]
	mov	x0,x21
	adr	x1,c_auto
	bl	str_eq
	mov	x22,0
	cbnz	x0,.pose
	mov	x0,x21				; un numero
	bl	num100
	tbnz	x0,63,.mot
	mov	x22,x0
	b	.cherche
.mot:	mov	x22,-1
.cherche:					; la sortie de ce numero, de ce type ou de ce nom
	add	x23,x27,SS_OUTS
	mov	x24,SS_OUT_MAX
.c:	ldr	w25,[x23,SO_ID]
	cbz	w25,.cn
	tbnz	x22,63,.parnom
	cmp	x25,x22
	b.eq	.trouve
	b	.cn
.parnom:
	ldr	w0,[x23,SO_TYPE]
	adr	x1,c_types
	cmp	w0,3
	b.hi	.nom
	ldrb	w0,[x1,x0]
	add	x0,x1,x0
	mov	x1,x21
	bl	str_eq
	cbnz	x0,.trouve
.nom:	add	x0,x23,SO_NAME
	mov	x1,x21
	bl	str_eq
	cbnz	x0,.trouve
.cn:	add	x23,x23,64
	subs	x24,x24,1
	b.ne	.c
	adr	x0,m_nosuch
	bl	t_err_msg
	mov	x0,1
	bl	t_fin
.trouve:
	mov	x22,x25
.pose:	mov	x0,x22
	bl	ak_main
	tbnz	x0,63,failed
	mov	x0,50				; le temps que sound bascule
	sys	SYS_SLEEP
.liste:	ldr	x0,[x27,SS_MAIN]
	adr	x0,m_sorties
	bl	t_dire
	add	x23,x27,SS_OUTS
	mov	x24,SS_OUT_MAX
.l:	ldr	w0,[x23,SO_ID]
	cbz	w0,.ln
	ldr	w0,[x23,SO_FLAGS]
	mov	w1,' '
	mov	w2,'*'
	tst	w0,1
	csel	w0,w2,w1,ne
	bl	t_putc
	mov	w0,' '
	bl	t_putc
	mov	x0,x23
	bl	out_desc
	adr	x0,m_vol2
	bl	t_dire
	ldr	w0,[x23,SO_VOLUME]
	bl	t_putn
	adr	x0,m_pct
	bl	t_puts
	ldr	w0,[x23,SO_MUTE]
	cbz	w0,.lm
	adr	x0,m_mute
	bl	t_dire
.lm:	ldr	w0,[x23,SO_FLAGS]
	tbz	w0,1,.lp
	adr	x0,m_pref
	bl	t_dire
.lp:	bl	t_nl
.ln:	add	x23,x23,64
	subs	x24,x24,1
	b.ne	.l
	adr	x0,m_main_note
	bl	t_dire
	mov	x0,0
	bl	t_fin

; volume apps
cmd_apps:
	bl	list_apps
	mov	x0,0
	bl	t_fin

list_apps:
	prologue
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	add	x23,x27,SS_CLIS
	mov	x24,SS_CLI_MAX
	mov	x25,0				; vus
.c:	ldr	w0,[x23,SC_PID]
	cbz	w0,.cn
	cbnz	x25,.l
	adr	x0,m_apps_head
	bl	t_dire
.l:	add	x25,x25,1
	ldr	w0,[x23,SC_PID]
	mov	x1,5
	bl	put_w
	ldr	w0,[x23,SC_VOLUME]
	mov	x1,8
	bl	put_w
	ldr	w0,[x23,SC_PEAK]
	mov	x1,100
	mul	x0,x0,x1
	movl	x1,32767
	udiv	x0,x0,x1
	mov	x1,6
	bl	put_w
	adr	x0,m_pct2
	bl	t_puts
	add	x0,x23,SC_NAME
	bl	t_puts
	ldr	w0,[x23,SC_MUTE]
	cbz	w0,.nl
	adr	x0,m_mute2
	bl	t_dire
.nl:	bl	t_nl
.cn:	add	x23,x23,64
	subs	x24,x24,1
	b.ne	.c
	cbnz	x25,.out
	adr	x0,m_noapp
	bl	t_dire
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	epilogue

; volume app <nom|pid> <0..100|mute|unmute>
cmd_app:
	cmp	x19,4
	b.ne	.usage
	ldr	x21,[x20,16]			; qui
	ldr	x22,[x20,24]			; quoi
	mov	x25,-1				; volume (-1 : le sien)
	mov	x26,0				; muet
	mov	x0,x22
	adr	x1,c_mute
	bl	str_eq
	cbz	x0,.u
	mov	x26,1
	b	.qui
.u:	mov	x0,x22
	adr	x1,c_unmute
	bl	str_eq
	cbnz	x0,.qui
	mov	x0,x22
	bl	num100
	tbnz	x0,63,.usage
	mov	x25,x0
.qui:	mov	x0,x21				; un pid ?
	bl	num_pid
	mov	x24,x0
	add	x22,x27,SS_CLIS
	mov	x23,SS_CLI_MAX
	mov	x28,0				; faits
.c:	ldr	w0,[x22,SC_PID]
	cbz	w0,.cn
	cbz	x24,.parnom
	cmp	x0,x24
	b.ne	.cn
	b	.pose
.parnom:
	add	x0,x22,SC_NAME
	mov	x1,x21
	bl	str_eq
	cbz	x0,.cn
.pose:	ldr	w0,[x22,SC_PID]
	ldr	w1,[x22,SC_VOLUME]
	tbnz	x25,63,.v
	mov	x1,x25
.v:	mov	x2,x26
	bl	ak_client_volume
	tbnz	x0,63,failed
	add	x28,x28,1
.cn:	add	x22,x22,64
	subs	x23,x23,1
	b.ne	.c
	cbnz	x28,.ok
	ldr	x0,[x20]
	bl	t_err
	adr	x0,s_t_sep
	bl	t_err
	mov	x0,x21
	bl	t_err
	adr	x0,m_notplaying
	bl	t_derr
	mov	x0,1
	bl	t_fin
.ok:	mov	x0,50
	sys	SYS_SLEEP
	bl	list_apps
	mov	x0,0
	bl	t_fin
.usage:	adr	x0,m_usage_app
	bl	t_err_msg
	mov	x0,2
	bl	t_fin

; x0 = texte -> x0 = pid s'il n'a que des chiffres, 0 sinon
num_pid:
	mov	x1,0
	ldrb	w2,[x0]
	cbz	w2,.non
.c:	ldrb	w2,[x0],1
	cbz	w2,.f
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.non
	mov	x3,10
	mul	x1,x1,x3
	add	x1,x1,x2
	b	.c
.f:	mov	x0,x1
	ret
.non:	mov	x0,0
	ret

; x0 = nombre, x1 = largeur : aligne a droite
put_w:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x1
	mov	x1,sp
	bl	fmt_dec
	mov	x1,sp
	sub	x0,x0,x1
	sub	x19,x19,x0
.b:	cmp	x19,0
	b.le	.t
	mov	w0,' '
	bl	t_putc
	sub	x19,x19,1
	b	.b
.t:	mov	x0,sp
	bl	t_puts
	add	sp,sp,32
	ldr	x19,[sp],16
	epilogue

c_mute		db 'mute',0
c_unmute	db 'unmute',0
c_toggle	db 'toggle',0
c_output	db 'output',0
c_apps		db 'apps',0
c_app		db 'app',0
c_auto		db 'auto',0
c_types		db 4, 5, 10, 15			; (decalages : les mots de la commande)
		db 0, 'jack',0, 'hdmi',0, 'usb',0
types		db 4, 14, 19, 24		; (decalages : les noms affiches)
		db 'virtuelle',0, 'jack',0, 'HDMI',0, 'USB',0
m_usage		db 'usage : volume [0..100 | mute | unmute | toggle | output [auto|jack|hdmi|usb|<sortie>] | apps | app <nom|pid> <0..100|mute|unmute>]',0
m_usage_app	db 'usage : volume app <nom|pid> <0..100|mute|unmute>',0
m_nosvc		db 'pas de service sound',0
m_noout		db 'aucune sortie de son',0
m_failed	db 'refus',0xC3,0xA9,' par le service sound',0
m_nosuch	db 'pas de sortie de ce nom (volume output : les sorties)',0
m_notplaying	db ' ne joue pas (volume apps : les programmes qui jouent)',10,0
m_volume	db 'volume ',0
m_pct		db ' %',0
m_pct2		db ' %  ',0
m_mute		db ' (muet)',0
m_mute2		db '  (muet)',0
m_on		db ', sortie ',0
m_q1		db ' ',0xC2,0xAB,' ',0
m_q2		db ' ',0xC2,0xBB,' (',0
m_par2		db ')',0
m_vol2		db ', volume ',0
m_pref		db ', pr',0xC3,0xA9,'f',0xC3,0xA9,'r',0xC3,0xA9,'e',0
m_sorties	db 'sorties (* : celle qui joue) :',10,0
m_main_note	db 'volume output auto|jack|hdmi|usb|<num',0xC3,0xA9,'ro> : la sortie pr',0xC3,0xA9,'f',0xC3,0xA9,'r',0xC3,0xA9,'e',10,0
m_apps_head	db '  PID  VOLUME  CR',0xC3,0x8A,'TE  NOM',10,0
m_noapp		db 'aucun programme ne joue',10,0
	align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
