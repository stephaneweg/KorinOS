; gpuinfo - l'etat du GPU et de la carte, par le service v3d (docs/V3D.md) :
; pret ou eteint, jobs faits, temperature, frequences, alertes du firmware.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/v3d.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,s_v3d
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.absent
	adr	x1,msg
	mov	x2,V3D_OP_HEALTH
	str	x2,[x1]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.absent
	adr	x19,msg
	adr	x0,m_state
	bl	lang_puts
	ldr	x0,[x19,V3DH_STATE]
	bl	print_dec
	adr	x0,m_jobs
	bl	lang_puts
	ldr	x0,[x19,V3DH_JOBS]
	bl	print_dec
	adr	x0,m_resets
	bl	lang_puts
	ldr	x0,[x19,V3DH_RESETS]
	bl	print_dec
	adr	x0,m_temp
	bl	puts
	ldr	x0,[x19,V3DH_TEMP]
	mov	x1,100				; dixiemes de degre
	udiv	x0,x0,x1
	mov	x1,10
	udiv	x20,x0,x1
	msub	x21,x20,x1,x0
	mov	x0,x20
	bl	print_dec
	adr	x0,m_point
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_gpu
	bl	puts
	ldr	x0,[x19,V3DH_V3D_HZ]
	movl	x1,1000000
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_arm
	bl	puts
	ldr	x0,[x19,V3DH_ARM_HZ]
	movl	x1,1000000
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_thr
	bl	lang_puts
	ldr	x20,[x19,V3DH_THROTTLED]
	mov	x0,x20
	bl	print_dec
	adr	x0,m_now
	bl	lang_puts
	and	x0,x20,0xF			; 1 sous-tension, 2 frequence bridee, 4 ralenti, 8 chaleur
	bl	print_dec
	adr	x0,m_past
	bl	lang_puts
	lsr	x0,x20,16			; (les memes bits, depuis le demarrage)
	and	x0,x0,0xF
	bl	print_dec
	adr	x0,m_nl
	bl	lang_puts
	mov	x0,0
	sys	SYS_EXIT
.absent:
	adr	x0,m_absent
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

segment readable writeable
kit_data

s_v3d		db 'v3d',0
m_state		db 'gpu : etat ',0
m_jobs		db ' (1 pret, 0 absent, -1 eteint), jobs ',0
m_resets	db ', remises a zero ',0
m_temp		db 10,'temperature ',0
m_point		db '.',0
m_gpu		db ' C, GPU ',0
m_arm		db ' MHz, ARM ',0
m_thr		db ' MHz',10,'alertes ',0
m_now		db ' : maintenant ',0
m_past		db ', depuis le demarrage ',0
m_nl		db ' (1 sous-tension, 2 frequence bridee, 4 ralenti, 8 chaleur)',10,0
m_absent	db 'gpuinfo : pas de service v3d',10,0
		align 16
msg		rb 512
