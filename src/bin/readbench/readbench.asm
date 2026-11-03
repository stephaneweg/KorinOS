; readbench - debit de lecture d'un fichier (diagnostic de la carte) :
;	readbench [chemin]
; lit 16 Mo par requetes de 4, 16, 64 et 256 Ko, le pilote de la carte dormant
; jusqu'a la fin du DMA, puis la sondant ; affiche ms et Ko/s de chacune, et ce
; que la carte y a passe (le reste : fatfs et les changements de contexte).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/blk.inc'

TOTAL		= 16*1024*1024
SONDE		= 8000			; attente active de la seconde passe (us)

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,.def
	ldrb	w0,[x19]
	cbnz	w0,.go
.def:	adr	x19,p_jet
.go:	adr	x0,s_blk
	mov	x1,1000
	sys	SYS_SVC_OPEN
	adr	x1,canal
	str	x0,[x1]
	mov	x27,0				; attente active : aucune
.passe:	adr	x0,m_dort
	cbz	x27,.tit
	adr	x0,m_sonde
.tit:	bl	puts
	adr	x20,tailles
.t:	ldr	w21,[x20],4
	cbz	w21,.pfin
	mov	x0,x27
	bl	stats
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x22,x0
	sys	SYS_TIME_MS
	mov	x23,x0
	mov	x24,0
.l:	mov	x0,x22
	adr	x1,tampon
	mov	x2,x21
	sys	SYS_READ
	cmp	x0,0
	b.le	.bout
	add	x24,x24,x0
	movl	x1,TOTAL
	cmp	x24,x1
	b.lo	.l
.bout:	sys	SYS_TIME_MS
	sub	x23,x0,x23
	mov	x0,x22
	sys	SYS_CLOSE
	lsr	x0,x21,10
	bl	print_dec
	adr	x0,m_ko
	bl	puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	lsr	x0,x24,10
	mov	x1,1000
	mul	x0,x0,x1
	add	x1,x23,1
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_kos
	bl	puts
	mov	x0,-1
	bl	stats
	mov	x28,x0
	adr	x25,msg
	adr	x0,m_carte
	bl	puts
	ldr	x0,[x25,8]
	bl	print_dec
	adr	x0,m_cmd
	bl	puts
	ldp	x0,x1,[x25,16]
	ldr	x2,[x25,32]
	add	x0,x0,x1
	add	x0,x0,x2
	mov	x1,1000
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_som
	bl	puts
	mov	x0,x28
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	b	.t
.pfin:	bl	charges
	cbnz	x27,.fin
	mov	x27,SONDE
	b	.passe
.fin:	mov	x0,0
	sys	SYS_EXIT
.err:	adr	x0,m_err
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; affiche la charge de chaque coeur, en %
charges:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,m_charges
	bl	puts
	adr	x0,info
	sys	SYS_SYSINFO
	adr	x19,info+40
	mov	x20,4
.c:	ldr	x0,[x19],8
	bl	print_dec
	adr	x0,m_esp
	bl	puts
	subs	x20,x20,1
	b.ne	.c
	adr	x0,m_nl
	bl	puts
	ldp	x19,x20,[sp],16
	epilogue

; x0 = attente active en us (-1 : inchangee) -> x0 = sommeils, compteurs dans msg
stats:
	prologue
	adr	x1,msg
	mov	x2,BLK_STATS
	stp	x2,x0,[x1]
	adr	x0,canal
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	epilogue

		align 4
tailles		dw 4096, 16384, 65536, 262144, 0
p_jet		db 'SD0:/apps/jet.app/main',0
m_ko		db ' Ko par requete : ',0
m_ms		db ' ms, ',0
s_blk		db 'blk.sd0',0
m_dort		db '-- le pilote dort jusqu''a la fin du DMA',10,0
m_sonde		db '-- le pilote sonde la fin du DMA',10,0
m_carte		db '  carte : ',0
m_cmd		db ' commandes, ',0
m_som		db ' ms ; sommeils ',0
m_nl		db 10,0
m_charges	db 'charge des coeurs (%) : ',0
m_esp		db ' ',0
m_kos		db ' Ko/s',10,0
m_err		db 'readbench : ouverture impossible',10,0
		align 8

segment readable writeable
kit_data
		align 8
tampon		rb 262144
canal		rd 1
		align 8
msg		rb 512
info		rb 80
