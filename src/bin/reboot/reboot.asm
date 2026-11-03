; reboot - redemarre le Pi (docs/RESEAU.md 5.3) : demande a init2 (op
; INIT_OP_REBOOT), qui laisse finir les ecritures puis passe par le bloc PM.
;   reboot            redemarrage
;   reboot tryboot    une fois sur tryboot.txt (kernel=kernel8-essai.img...) :
;                     si ce noyau fige, couper / remettre l'alimentation
;                     relance l'ancien (a verifier sur le Pi 4)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	mov	x20,0				; drapeaux
	cbz	x19,.go
	mov	x0,x19
	adr	x1,s_tryboot
	bl	str_eq
	cbz	x0,.go
	mov	x20,1
.go:	adr	x0,m_reboot
	bl	lang_puts
	adr	x0,s_init2
	mov	x1,1000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.ko
	adr	x1,msg
	mov	x2,INIT_OP_REBOOT
	stp	x2,x20,[x1]
	str	wzr,[x1,WM_LEN]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.ko
	mov	x0,5000				; (le Pi redemarre pendant ce temps)
	sys	SYS_SLEEP
.ko:	adr	x0,m_ko
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

s_tryboot	db 'tryboot',0
s_init2		db 'init2',0
m_reboot	db 'reboot : red',0xC3,0xA9,'marrage...',10,0
m_ko		db 'reboot : init2 ne red',0xC3,0xA9,'marre pas le Pi',10,0
		align 8

segment readable writeable
kit_data
		align 8
msg		rb 512
