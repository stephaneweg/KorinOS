; ============================================================================
;  maptest - essai de SVC_MAP_CALLER (src/kernel/mapcaller.asm), avec le service
;  SD0:/sys/svc/mapsvc : lecture, ecriture dans une region pas encore touchee,
;  page en lecture seule, refus ; munmap et mort de l'appelant pendant que le
;  service tient les pages : rien ne plante, aucune page ne fuit.
;  Sortie : une ligne par echec, puis « maptest : PASS / FAIL (n) ».
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

OP_READ		= 1
OP_WRITE	= 2
OP_HOLD		= 3
OP_PAGES	= 4
E_FAULT_T	= -14
E_INVAL_T	= -22
ZONE		= 65536
TOURS		= 10
TENU_MS		= 200
TH_STACK	= 16384
FUITE_MAX_KO	= 300

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,.pere
	ldrb	w0,[x19]
	cmp	w0,'f'
	b.eq	fils
.pere:	adr	x0,p_svc			; le service
	mov	x1,0
	sys	SYS_SPAWN
	tbnz	x0,63,.ko
	adr	x0,s_svc
	mov	x1,2000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.ko
	adr	x1,canal
	str	x0,[x1]

	; lecture, a cheval sur deux pages
	adr	x19,buf
	add	x19,x19,3000
	mov	x1,0
	mov	x2,0
.mk:	strb	w1,[x19,x1]
	and	x3,x1,255
	strb	w3,[x19,x1]
	add	x2,x2,x3
	add	x1,x1,1
	mov	x4,6000
	cmp	x1,x4
	b.lo	.mk
	mov	x20,x2
	mov	x0,OP_READ
	mov	x1,x19
	mov	x2,6000
	mov	x3,0
	bl	appel
	mov	x1,x20
	adr	x2,t_read
	bl	verifie

	; ecriture dans une region jamais touchee
	mov	x0,0
	mov	x1,ZONE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x19,x0
	mov	x0,OP_WRITE
	mov	x1,x19
	mov	x2,ZONE
	mov	x3,0x33
	bl	appel
	mov	x1,ZONE
	adr	x2,t_write
	bl	verifie
	mov	x20,ZONE-1
	ldrb	w0,[x19,x20]
	mov	x1,0x33
	adr	x2,t_writeb
	bl	verifie

	; le code : se lit, ne s'ecrit pas
	adr	x1,start
	mov	x0,OP_READ
	mov	x2,256
	mov	x3,0
	bl	appel
	cmp	x0,0
	cset	x0,gt
	mov	x1,1
	adr	x2,t_code
	bl	verifie
	adr	x1,start
	mov	x0,OP_WRITE
	mov	x2,256
	mov	x3,1
	bl	appel
	mov	x1,E_FAULT_T
	adr	x2,t_ro
	bl	verifie

	; refus
	mov	x0,OP_READ
	movl	x1,0x6F00000000			; rien ici
	mov	x2,4096
	mov	x3,0
	bl	appel
	mov	x1,E_FAULT_T
	adr	x2,t_bad
	bl	verifie
	mov	x0,OP_READ
	mov	x1,x19
	movl	x2,0x200000
	mov	x3,0
	bl	appel
	mov	x1,E_INVAL_T
	adr	x2,t_big
	bl	verifie

	; pages : 4097 octets depuis le milieu d'une page, 2 pages
	mov	x0,OP_PAGES
	add	x1,x19,2048
	mov	x2,4097
	mov	x3,0
	bl	appel
	mov	x1,2
	adr	x2,t_pages
	bl	verifie

	; le service mappe l'appelant, l'appelant part : la page attend le service
	bl	libre
	mov	x22,x0
	mov	x20,TOURS
.un:	mov	x0,0
	mov	x1,ZONE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x21,x0
	adr	x1,zone
	str	x21,[x1]
	adr	x0,th_appel
	adr	x1,th_pile+TH_STACK
	mov	x2,0
	sys	SYS_THREAD_CREATE
	mov	x23,x0
	mov	x0,50
	sys	SYS_SLEEP
	mov	x0,x21				; tenue par le service
	mov	x1,ZONE
	sys	SYS_VM_UNMAP
	mov	x1,0
	adr	x2,t_unmap
	bl	verifie
	mov	x0,x23
	sys	SYS_THREAD_JOIN
	subs	x20,x20,1
	b.ne	.un
	bl	libre
	sub	x0,x22,x0
	bl	fuite
	adr	x2,t_leak1
	bl	verifie

	mov	x20,TOURS
.tue:	adr	x0,p_moi
	adr	x1,s_fils
	sys	SYS_SPAWN
	mov	x21,x0
	tbnz	x0,63,.ko
	mov	x0,50
	sys	SYS_SLEEP
	mov	x0,x21
	sys	SYS_KILL
	mov	x0,x21
	sys	SYS_WAIT
	mov	x0,TENU_MS
	sys	SYS_SLEEP
	subs	x20,x20,1
	b.ne	.tue
	mov	x0,100
	sys	SYS_SLEEP
	bl	libre
	sub	x0,x22,x0
	bl	fuite
	adr	x2,t_leak2
	bl	verifie

	adr	x0,canal			; le service repond encore
	mov	x0,OP_READ
	adr	x1,buf
	mov	x2,16
	mov	x3,0
	bl	appel
	cmp	x0,0
	cset	x0,ge
	mov	x1,1
	adr	x2,t_alive
	bl	verifie

	adr	x19,echecs
	ldr	x19,[x19]
	cbnz	x19,.fail
	adr	x0,m_pass
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.fail:	adr	x0,m_fail
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_fin
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
.ko:	mov	x19,x0
	adr	x0,m_ko
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,2
	sys	SYS_EXIT

; le fils : tenu par le service, il se fait tuer
fils:	adr	x0,s_svc
	mov	x1,2000
	sys	SYS_SVC_OPEN
	adr	x1,canal
	str	x0,[x1]
	mov	x0,0
	mov	x1,ZONE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x1,x0
	mov	x0,OP_HOLD
	mov	x2,ZONE
	mov	x3,TENU_MS
	bl	appel
	mov	x0,0
	sys	SYS_EXIT

; thread : tient la zone
th_appel:
	adr	x1,zone
	ldr	x1,[x1]
	mov	x0,OP_HOLD
	mov	x2,ZONE
	mov	x3,TENU_MS
	bl	appel
	mov	x0,0
	sys	SYS_THREAD_EXIT

; x0 = op, x1 = a, x2 = b, x3 = c -> resultat
appel:
	prologue
	sub	sp,sp,512
	stp	x0,x1,[sp]
	stp	x2,x3,[sp,16]
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,sp
	sys	SYS_SVC_CALL
	add	sp,sp,512
	epilogue

; -> x0 = memoire libre (Ko)
libre:
	prologue
	adr	x0,info
	sys	SYS_SYSINFO
	adr	x0,info
	ldr	x0,[x0,8]
	epilogue

; x0 = Ko perdus -> x0 = 0 si c'est peu, x1 = 0
fuite:
	mov	x1,0
	cmp	x0,FUITE_MAX_KO
	b.gt	.oui
	mov	x0,0
.oui:	ret

; x0 = obtenu, x1 = attendu, x2 = nom de l'essai
verifie:
	cmp	x0,x1
	b.ne	.non
	ret
.non:	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,echecs
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	adr	x0,m_echec
	bl	puts
	mov	x0,x21
	bl	puts
	adr	x0,m_obt
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_att
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

p_svc	db 'SD0:/sys/svc/mapsvc',0
p_moi	db 'SD0:/bin/maptest',0
s_svc	db 'mapsvc',0
s_fils	db 'fils',0
t_read	db 'lecture',0
t_write	db 'ecriture',0
t_writeb db 'ecriture relue',0
t_code	db 'code lu',0
t_ro	db 'code en ecriture',0
t_bad	db 'adresse sans page',0
t_big	db 'trop grand',0
t_pages	db 'pages',0
t_unmap	db 'munmap pendant le mappage',0
t_leak1	db 'fuite apres munmap',0
t_leak2	db 'fuite apres mort',0
t_alive	db 'service vivant',0
m_echec	db 'maptest : ECHEC ',0
m_obt	db ' : ',0
m_att	db ' au lieu de ',0
m_nl	db 10,0
m_pass	db 'maptest : PASS',10,0
m_fail	db 'maptest : FAIL (',0
m_fin	db ')',10,0
m_ko	db 'maptest : erreur ',0
	align 8

segment readable writeable
kit_data
	align 8
canal	dd 0
echecs	dd 0
zone	dd 0
info	rb 80
buf	rb 12288
	align 16
th_pile	rb TH_STACK
