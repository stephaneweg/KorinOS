; blkbench - debit du service bloc de la carte, sans systeme de fichiers
; (diagnostic, lecture seule) :
;	blkbench [peripherique]	(sd0 par defaut, sd0p1 : celui de fatfs)
; lit blk.sd0 par commandes de 1 a 256 secteurs, le pilote dormant jusqu'a la
; fin du DMA, puis la sondant ; affiche ms, Ko/s et le temps par commande dans
; le pilote (preparation, commande, donnees, sommeils).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/blk.inc'

SONDE		= 8000			; attente active de la seconde passe (us)
DEBUT		= 200000		; premier secteur lu
TAMPON		= 256*512

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	adr	x0,s_nom
	cbz	x19,.defaut
	ldrb	w1,[x19]
	cbz	w1,.defaut
	mov	x1,x19
.cp:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.cp
.defaut:
	adr	x0,s_blk
	mov	x1,1000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.err
	mov	x19,x0
	adr	x1,msg
	mov	x0,BLK_INFO
	str	x0,[x1]
	mov	x0,x19
	sys	SYS_SVC_CALL
	tbnz	x0,63,.err
	adr	x1,msg
	ldr	x22,[x1,32]			; pid du service
	mov	x0,x22
	bl	coeurs
	movl	x0,TAMPON or BUF_LOW
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.err
	mov	x20,x0
	mov	x1,x22
	sys	SYS_BUF_GRANT
	adr	x1,msg
	mov	x0,BLK_ATTACHER
	str	x0,[x1]
	str	x20,[x1,8]
	movl	x0,TAMPON
	str	x0,[x1,16]
	mov	x0,x19
	sys	SYS_SVC_CALL
	tbnz	x0,63,.err
	movl	x21,BUF_VA_BASE
	mov	x0,BUF_SLOT
	madd	x21,x20,x0,x21			; le tampon
	mov	x27,0				; attente active : aucune
.passe:	adr	x0,m_dort
	cbz	x27,.tit
	adr	x0,m_sonde
.tit:	bl	puts
	adr	x22,tailles
.t:	ldr	w23,[x22],4
	cbz	w23,.pfin
	ldr	w26,[x22],4
	str	x26,[sp,-16]!
	mov	x0,x27
	bl	stats
	sys	SYS_TIME_MS
	mov	x24,x0
	movl	x25,DEBUT
.l:	adr	x1,msg
	mov	x0,BLK_LIRE
	str	x0,[x1]
	stp	x25,x23,[x1,8]
	str	x21,[x1,24]
	mov	x0,x19
	sys	SYS_SVC_CALL
	tbnz	x0,63,.err
	add	x25,x25,x23
	subs	x26,x26,x23
	b.hi	.l
	sys	SYS_TIME_MS
	sub	x24,x0,x24
	ldr	x28,[sp],16
	mov	x0,x23
	bl	print_dec
	adr	x0,m_sect
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	mov	x0,500
	mul	x0,x0,x28
	add	x1,x24,1
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_kos
	bl	puts
	mov	x0,-1
	bl	stats
	mov	x28,x0
	adr	x25,msg
	adr	x0,m_prep
	bl	puts
	ldr	x0,[x25,8]
	bl	print_dec
	adr	x0,m_cmd
	bl	puts
	ldr	x0,[x25,16]
	bl	par_cmd
	adr	x0,m_rep
	bl	puts
	ldr	x0,[x25,24]
	bl	par_cmd
	adr	x0,m_don
	bl	puts
	ldr	x0,[x25,32]
	bl	par_cmd
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
.err:	mov	x19,x0
	adr	x0,m_err
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = attente active en us (-1 : inchangee) -> x0 = sommeils, compteurs dans msg
stats:
	prologue
	adr	x1,msg
	mov	x2,BLK_STATS
	stp	x2,x0,[x1]
	mov	x0,x19
	sys	SYS_SVC_CALL
	epilogue

; x0 = pid : ses threads et leur coeur
coeurs:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x1,thl
	mov	x2,16
	sys	SYS_THREAD_LIST
	mov	x20,x0
	cmp	x20,0
	b.le	.out
	adr	x19,thl
.t:	add	x0,x19,40
	bl	puts
	adr	x0,m_coeur
	bl	puts
	ldr	x0,[x19,24]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	add	x19,x19,64
	subs	x20,x20,1
	b.ne	.t
.out:	ldp	x19,x20,[sp],16
	epilogue

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

; x0 = us au total -> affiche les us par commande
par_cmd:
	prologue
	ldr	x1,[x25,8]
	cmp	x1,0
	csinc	x1,x1,xzr,ne
	udiv	x0,x0,x1
	bl	print_dec
	epilogue

		align 4
; secteurs par commande, secteurs lus
tailles		dw 1,2048, 8,4096, 16,4096, 32,8192, 64,16384, 256,16384, 0
m_sect		db ' secteurs par commande : ',0
m_ms		db ' ms, ',0
m_kos		db ' Ko/s',10,0
m_err		db 'blkbench : erreur ',0
m_nl		db 10,0
m_coeur		db ' : coeur ',0
m_charges	db 'charge des coeurs (%) : ',0
m_esp		db ' ',0
m_dort		db '-- le pilote dort jusqu''a la fin du DMA',10,0
m_sonde		db '-- le pilote sonde la fin du DMA',10,0
m_prep		db '  ',0
m_cmd		db ' commandes, en us par commande : preparation ',0
m_rep		db ', reponse ',0
m_don		db ', donnees ',0
m_som		db ' ; sommeils ',0
		align 8

segment readable writeable
kit_data
		align 8
msg		rb 512
thl		rb 16*64
info		rb 80
s_blk		db 'blk.'
s_nom		db 'sd0',0
		rb 16
