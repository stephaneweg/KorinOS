; ============================================================================
;  svcfault - essai : un appel de service dont le message est en lecture seule.
;  La reponse ne peut pas y etre ecrite : ce programme doit mourir seul, sans
;  bloquer les services (le noyau ne copie plus sous svc_lock). Le shell qui
;  l'a lance doit repondre ensuite.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,s_blk
	mov	x1,1000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.ko
	adr	x1,msg_ro			; (ici : lecture seule)
	sys	SYS_SVC_CALL
	adr	x0,m_rate			; pas attendu : la reponse a ete ecrite
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
.ko:	adr	x0,m_ko
	bl	puts
	mov	x0,2
	sys	SYS_EXIT

s_blk		db 'blk.sd0',0
m_rate		db 'svcfault : ECHEC, reponse ecrite en lecture seule',10,0
m_ko		db 'svcfault : blk.sd0 absent',10,0
		align 8
msg_ro		dd 1				; BLK_INFO
		rb 512 - 8

segment readable writeable
kit_data
