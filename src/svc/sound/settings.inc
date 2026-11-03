; ============================================================================
;  sound/settings.inc - les reglages retenus : SD0:/etc/sound.txt (docs/SON.md
;  4.1), ecrit par sound une seconde apres un changement :
;
;	main = <sortie preferee>
;	output = <volume>, <muet>, <sortie>
;	app = <programme>, <volume>, <muet>, <sortie choisie (vide : principale)>
;
;  Les sorties et les programmes sont retenus par leur nom ; une application
;  routee vers une sortie absente joue sur la principale, et y revient quand
;  la sortie revient.
; ============================================================================

APM_MAX		= 32			; programmes retenus
AM_NAME		= 0			; 32 o
AM_VOL		= 32			; (AM_VOL, AM_MUTE contigus)
AM_MUTE		= 40
AM_ROUTE	= 48			; 32 o
AM_SIZE		= 80
OMM_MAX		= 16			; sorties retenues
OM_NAME		= 0
OM_VOL		= 32			; (OM_VOL, OM_MUTE contigus)
OM_MUTE		= 40
OM_SIZE		= 48
SAVE_MAX	= 4096

; x0 = nom, x1 = 1 : creer s'il manque -> x0 = son entree (0 : absente / plein)
appmem_find:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	adr	x19,appmem
	mov	x20,APM_MAX
.t:	ldrb	w0,[x19,AM_NAME]
	cbz	w0,.n
	add	x0,x19,AM_NAME
	mov	x1,x21
	bl	str_eq
	cbnz	x0,.oui
.n:	add	x19,x19,AM_SIZE
	subs	x20,x20,1
	b.ne	.t
	cbz	x22,.non
	adr	x19,appmem			; une place libre
	mov	x20,APM_MAX
.l:	ldrb	w0,[x19,AM_NAME]
	cbz	w0,.new
	add	x19,x19,AM_SIZE
	subs	x20,x20,1
	b.ne	.l
.non:	mov	x0,0
	b	.out
.new:	mov	x0,100
	str	x0,[x19,AM_VOL]
	str	xzr,[x19,AM_MUTE]
	strb	wzr,[x19,AM_ROUTE]
	add	x0,x19,AM_NAME
	mov	x1,x21
	mov	x2,32
	bl	str_copy_n
.oui:	mov	x0,x19
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom de sortie, x1 = 1 : creer -> x0 = son entree (0 : absente / plein)
outmem_find:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	adr	x19,outmem
	mov	x20,OMM_MAX
.t:	ldrb	w0,[x19,OM_NAME]
	cbz	w0,.n
	add	x0,x19,OM_NAME
	mov	x1,x21
	bl	str_eq
	cbnz	x0,.oui
.n:	add	x19,x19,OM_SIZE
	subs	x20,x20,1
	b.ne	.t
	cbz	x22,.non
	adr	x19,outmem
	mov	x20,OMM_MAX
.l:	ldrb	w0,[x19,OM_NAME]
	cbz	w0,.new
	add	x19,x19,OM_SIZE
	subs	x20,x20,1
	b.ne	.l
.non:	mov	x0,0
	b	.out
.new:	mov	x0,100
	str	x0,[x19,OM_VOL]
	str	xzr,[x19,OM_MUTE]
	add	x0,x19,OM_NAME
	mov	x1,x21
	mov	x2,32
	bl	str_copy_n
.oui:	mov	x0,x19
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; SD0:/etc/sound.txt -> pref_name, outmem, appmem
settings_load:
	prologue
	adr	x0,p_sound
	adr	x1,settings_line
	bl	ini_read
	epilogue

; (ini_read) x0 = cle, x1 = valeur
settings_line:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x19
	adr	x1,k_main
	bl	str_eq
	cbz	x0,.output
	adr	x0,pref_name
	mov	x1,x20
	mov	x2,32
	bl	str_copy_n
	b	.out
.output:
	mov	x0,x19
	adr	x1,k_output
	bl	str_eq
	cbz	x0,.app
	mov	x0,x20				; volume, muet, nom
	bl	ini_field
	mov	x21,x1
	bl	parse_dec
	mov	x22,x0
	cbz	x21,.out
	mov	x0,x21
	bl	ini_field
	mov	x21,x1
	bl	parse_dec
	mov	x20,x0
	cbz	x21,.out
	mov	x0,x21
	bl	ini_field
	ldrb	w1,[x0]
	cbz	w1,.out
	mov	x1,1
	bl	outmem_find
	cbz	x0,.out
	cmp	x22,100
	mov	x1,100
	csel	x22,x22,x1,ls
	cmp	x20,0
	cset	x20,ne
	stp	x22,x20,[x0,OM_VOL]
	b	.out
.app:	mov	x0,x19
	adr	x1,k_app
	bl	str_eq
	cbz	x0,.out
	mov	x0,x20				; nom, volume, muet, sortie
	bl	ini_field
	mov	x21,x1
	ldrb	w1,[x0]
	cbz	w1,.out
	mov	x1,1
	bl	appmem_find
	cbz	x0,.out
	mov	x19,x0
	cbz	x21,.out
	mov	x0,x21
	bl	ini_field
	mov	x21,x1
	bl	parse_dec
	cmp	x0,100
	mov	x1,100
	csel	x0,x0,x1,ls
	str	x0,[x19,AM_VOL]
	cbz	x21,.out
	mov	x0,x21
	bl	ini_field
	mov	x21,x1
	bl	parse_dec
	cmp	x0,0
	cset	x0,ne
	str	x0,[x19,AM_MUTE]
	cbz	x21,.out
	mov	x0,x21
	bl	ini_field
	mov	x1,x0
	add	x0,x19,AM_ROUTE
	mov	x2,32
	bl	str_copy_n
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x19 = ecriture dans savebuf, x0 = chaine -> x19 avance
sv_str:
	ldrb	w1,[x0],1
	cbz	w1,.ret
	strb	w1,[x19],1
	b	sv_str
.ret:	ret

; x0 = valeur -> x19 avance
sv_dec:
	prologue
	mov	x1,x19
	bl	fmt_dec
	mov	x19,x0
	epilogue

; Ecrit SD0:/etc/sound.txt
settings_save:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,savebuf
	adr	x0,s_head
	bl	sv_str
	adr	x0,pref_name
	ldrb	w0,[x0]
	cbz	w0,.outs
	adr	x0,k_main
	bl	sv_str
	adr	x0,s_eq
	bl	sv_str
	adr	x0,pref_name
	bl	sv_str
	mov	w0,10
	strb	w0,[x19],1
.outs:	adr	x20,outmem
	mov	x21,OMM_MAX
.o:	ldrb	w0,[x20,OM_NAME]
	cbz	w0,.on
	adr	x0,k_output
	bl	sv_str
	adr	x0,s_eq
	bl	sv_str
	ldr	x0,[x20,OM_VOL]
	bl	sv_dec
	adr	x0,s_comma
	bl	sv_str
	ldr	x0,[x20,OM_MUTE]
	bl	sv_dec
	adr	x0,s_comma
	bl	sv_str
	add	x0,x20,OM_NAME
	bl	sv_str
	mov	w0,10
	strb	w0,[x19],1
.on:	add	x20,x20,OM_SIZE
	subs	x21,x21,1
	b.ne	.o
	adr	x20,appmem
	mov	x21,APM_MAX
.a:	ldrb	w0,[x20,AM_NAME]
	cbz	w0,.an
	adr	x0,k_app
	bl	sv_str
	adr	x0,s_eq
	bl	sv_str
	add	x0,x20,AM_NAME
	bl	sv_str
	adr	x0,s_comma
	bl	sv_str
	ldr	x0,[x20,AM_VOL]
	bl	sv_dec
	adr	x0,s_comma
	bl	sv_str
	ldr	x0,[x20,AM_MUTE]
	bl	sv_dec
	adr	x0,s_comma
	bl	sv_str
	add	x0,x20,AM_ROUTE
	bl	sv_str
	mov	w0,10
	strb	w0,[x19],1
.an:	add	x20,x20,AM_SIZE
	subs	x21,x21,1
	b.ne	.a
	adr	x0,p_sound
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x20,x0
	adr	x1,savebuf
	sub	x2,x19,x1
	sys	SYS_WRITE
	mov	x0,x20
	sys	SYS_CLOSE
	b	.out
.fail:	adr	x0,m_savefail
	bl	puts
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

p_sound		db 'SD0:/etc/sound.txt',0
k_main		db 'main',0
k_output	db 'output',0
k_app		db 'app',0
s_eq		db ' = ',0
s_comma		db ', ',0
s_head		db '# SD0:/etc/sound.txt : r',0xC3,0xA9,'glages du son, ',0xC3,0xA9,'crits par sound (docs/SON.md 4)',10
		db '# main = sortie pr',0xC3,0xA9,'f',0xC3,0xA9,'r',0xC3,0xA9,'e ; output = volume, muet, sortie ;',10
		db '# app = programme, volume, muet, sortie choisie (vide : la principale)',10,0
m_savefail	db 'sound : SD0:/etc/sound.txt non ',0xC3,0xA9,'crit',10,0
		align 4

macro settings_data {
		align 8
appmem		rb APM_MAX * AM_SIZE
outmem		rb OMM_MAX * OM_SIZE
savebuf		rb SAVE_MAX
}
