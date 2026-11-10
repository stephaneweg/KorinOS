; svctest - essai des services nommes (docs/MICRO-NOYAU.md) : lance le
; service d'essai SD0:/sys/svc/echo, l'ouvre, lui fait N allers-retours et
; affiche leur duree ; verifie aussi qu'un programme hors de SD0:/sys ne peut
; pas enregistrer de service ni toucher au materiel ; lance aussi le service
; d'essai du materiel (SD0:/sys/svc/hwtest).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

N	= 10000

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,s_essai			; enregistrement interdit ici
	mov	x1,0
	sys	SYS_SVC_REGISTER
	mov	x19,x0
	adr	x0,m_droit
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	movl	x0,0xFE003000			; acces au materiel interdit ici
	mov	x1,0x20
	sys	SYS_DEV_MAP
	mov	x19,x0
	adr	x0,m_mat
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,p_mat			; le service d'essai du materiel
	mov	x1,0
	sys	SYS_SPAWN
	tbnz	x0,63,.err
	sys	SYS_WAIT
	adr	x0,p_echo			; le service
	mov	x1,0
	sys	SYS_SPAWN
	mov	x21,x0
	tbnz	x0,63,.err
	adr	x0,s_echo
	mov	x1,2000
	sys	SYS_SVC_OPEN
	mov	x19,x0
	tbnz	x0,63,.err
	sys	SYS_TIME_MS
	mov	x22,x0
	mov	x20,0				; allers-retours
.l:	mov	x0,N
	cmp	x20,x0
	b.hs	.fin
	adr	x1,msg
	mov	x0,7
	stp	x0,x20,[x1]			; op 7, a = i
	mov	x0,x19
	sys	SYS_SVC_CALL
	add	x1,x20,7
	cmp	x0,x1
	b.ne	.faux
	add	x20,x20,1
	b	.l
.fin:	sys	SYS_TIME_MS
	sub	x22,x0,x22
	adr	x0,m_ar
	bl	puts
	mov	x0,N
	bl	print_dec
	adr	x0,m_en
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	mov	x0,1000				; microsecondes par aller-retour
	mul	x0,x22,x0
	mov	x1,N
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_us
	bl	puts
	mov	x0,x21				; fin du service : ses appels echouent
	sys	SYS_KILL
	mov	x0,x21
	sys	SYS_WAIT
	adr	x1,msg
	mov	x0,7
	stp	x0,xzr,[x1]
	mov	x0,x19
	sys	SYS_SVC_CALL
	mov	x19,x0
	adr	x0,m_mort
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.faux:	adr	x0,m_faux
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
.err:	mov	x19,x0
	adr	x0,m_err
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

s_essai	db 'essai',0
s_echo	db 'echo',0
p_echo	db 'SD0:/sys/svc/echo',0
p_mat	db 'SD0:/sys/svc/hwtest',0
m_mat	db 'svctest : acces au materiel hors de SD0:/sys -> ',0
m_droit	db 'svctest : enregistrement hors de SD0:/sys -> ',0
m_ar	db 'svctest : ',0
m_en	db ' allers-retours en ',0
m_ms	db ' ms, soit ',0
m_us	db ' us chacun',10,0
m_mort	db 'svctest : service arrete -> ',0
m_faux	db 'svctest : reponse fausse',10,0
m_err	db 'svctest : erreur ',0
m_nl	db 10,0
	align 8

segment readable writeable
kit_data
	align 8
msg	rb 512
