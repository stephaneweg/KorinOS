; echo - service d'essai (SD0:/sys/svc/echo) : s'enregistre sous « echo » et
; repond a chaque requete op + a, avec les memes donnees (svctest le mesure).
include '../../inc/armpi.inc'
app_header

segment readable executable

start:
	adr	x0,s_nom
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	mov	x19,x0				; canal
.loop:	mov	x0,x19
	adr	x1,msg
	mov	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.loop			; rien (ou avis du noyau)
	mov	x20,x0
	adr	x1,msg				; resultat : op + a
	ldp	x2,x3,[x1]
	add	x2,x2,x3
	str	x2,[x1]
	mov	x0,x20
	sys	SYS_SVC_REPLY
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

s_nom	db 'echo',0
	align 8

segment readable writeable
msg	rb 512
