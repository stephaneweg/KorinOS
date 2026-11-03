; irqtest - essai des interruptions des pilotes (src/kernel/irq.asm, docs/ABI.md),
; sur le canal DMA 4, libre sur le Pi 4 comme sous QEMU : IRQ_WAIT (delai, levee,
; ligne calmee), ligne liee a un service (SVC_NEXT, IRQ_ACK), refus. Sortie :
; « irqtest : PASS / FAIL ». Programme de SD0:/sys (les droits du materiel).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

DMA_PHYS	= 0xFE007000
DMA_CHAN	= 4
DMA_LINE	= 112 + DMA_CHAN
DMA_ENABLE_R	= 0xFF0
DMA_CS		= 0
DMA_CONBLK	= 4
CS_RESET	= 1 shl 31
CS_GO		= 1
CS_DONE		= 6			; INT, END : a 1 pour les effacer
TI_DMA		= 1 or (1 shl 4) or (1 shl 8)	; INTEN, SRC_INC, DEST_INC
BUS_ALIAS	= 0xC0000000
SIG_BIT		= 2

macro expect cond*, name* {
	b.#cond	@f
	adr	x0,name
	bl	failed
@@:
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	bl	kits_open
	movl	x0,DMA_PHYS
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nodev
	mov	x20,x0
	mov	x0,0x1000			; bloc de controle, non cache
	adr	x1,phys
	sys	SYS_DMA_ALLOC
	tbnz	x0,63,.nodev
	mov	x21,x0
	adr	x1,phys
	ldr	x22,[x1]
	ldr	w0,[x20,DMA_ENABLE_R]
	orr	w0,w0,1 shl DMA_CHAN
	str	w0,[x20,DMA_ENABLE_R]
	add	x20,x20,DMA_CHAN shl 8
	movl	w0,CS_RESET
	str	w0,[x20,DMA_CS]
	mov	x0,1
	sys	SYS_SLEEP

	mov	x0,30				; ligne du timer : refusee
	mov	x1,-1
	mov	x2,0
	sys	SYS_IRQ_BIND
	cmp	x0,-22
	expect	eq,t_range
	mov	x0,DMA_LINE			; pas encore tenue
	mov	x1,10
	sys	SYS_IRQ_WAIT
	cmp	x0,-22
	expect	eq,t_unbound
	mov	x0,DMA_LINE			; attente simple
	mov	x1,-1
	mov	x2,0
	sys	SYS_IRQ_BIND
	cmp	x0,0
	expect	eq,t_bind
	mov	x0,DMA_LINE			; rien : le delai
	mov	x1,30
	sys	SYS_IRQ_WAIT
	cmp	x0,0
	expect	eq,t_delay
	bl	dma_go
	mov	x0,DMA_LINE			; levee
	mov	x1,500
	sys	SYS_IRQ_WAIT
	cmp	x0,1
	expect	eq,t_fired
	bl	dma_calm
	mov	x0,DMA_LINE			; calmee : plus rien
	mov	x1,30
	sys	SYS_IRQ_WAIT
	cmp	x0,0
	expect	eq,t_calm
	mov	x0,DMA_LINE
	sys	SYS_IRQ_UNBIND
	cmp	x0,0
	expect	eq,t_unbind

	adr	x0,s_name			; liee a un service
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,.nosvc
	mov	x23,x0
	mov	x0,DMA_LINE
	mov	x1,x23
	mov	x2,SIG_BIT
	sys	SYS_IRQ_BIND
	cmp	x0,0
	expect	eq,t_bind
	mov	x24,2				; deux fois : IRQ_ACK a remis la ligne
.twice:	bl	dma_go
	mov	x0,x23
	adr	x1,msg
	mov	x2,500
	sys	SYS_SVC_NEXT
	cmp	x0,0
	expect	gt,t_next
	adr	x1,msg
	ldp	x0,x2,[x1]
	movl	x3,SVC_OP_SIGNAL
	cmp	x0,x3
	expect	eq,t_op
	cmp	x2,SIG_BIT
	expect	eq,t_bits
	bl	dma_calm
	mov	x0,DMA_LINE
	sys	SYS_IRQ_ACK
	subs	x24,x24,1
	b.ne	.twice
	mov	x0,x23				; plus rien en attente
	adr	x1,msg
	mov	x2,20
	sys	SYS_SVC_NEXT
	cmp	x0,0
	expect	eq,t_idle
	b	.end
.nosvc:	adr	x0,t_svc
	bl	failed
	b	.end
.nodev:	adr	x0,t_dev
	bl	failed
.end:	adr	x1,fails
	ldr	x19,[x1]
	adr	x0,m_pass
	cbz	x19,.say
	adr	x0,m_fail
.say:	bl	puts
	mov	x0,x19
	sys	SYS_EXIT

; lance une copie de 16 octets dans la page non cachee (x21, physique x22)
dma_go:
	mov	x0,TI_DMA
	str	w0,[x21]
	add	x1,x22,0x800
	movl	x2,BUS_ALIAS
	orr	x1,x1,x2
	str	w1,[x21,4]
	add	x1,x1,0x100
	str	w1,[x21,8]
	mov	w0,16
	str	w0,[x21,12]
	str	wzr,[x21,16]
	str	wzr,[x21,20]
	dsb	sy
	orr	x1,x22,x2
	str	w1,[x20,DMA_CONBLK]
	mov	w0,CS_GO
	str	w0,[x20,DMA_CS]
	ret

dma_calm:
	mov	w0,CS_DONE
	str	w0,[x20,DMA_CS]
	dsb	sy
	ret

; x0 = nom de l'essai
failed:
	prologue
	str	x0,[sp,-16]!
	adr	x0,m_failed
	bl	puts
	ldr	x0,[sp],16
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x1,fails
	ldr	x0,[x1]
	add	x0,x0,1
	str	x0,[x1]
	epilogue

segment readable writeable
kit_data
		align 8
phys		dd 0
fails		dd 0
s_name		db 'irqtest',0
m_failed	db 'irqtest : echec : ',0
m_nl		db 10,0
m_pass		db 'irqtest : PASS',10,0
m_fail		db 'irqtest : FAIL',10,0
t_dev		db 'registres ou memoire DMA',0
t_range		db 'ligne refusee',0
t_unbound	db 'ligne pas tenue',0
t_bind		db 'irq_bind',0
t_delay		db 'irq_wait : delai',0
t_fired		db 'irq_wait : levee',0
t_calm		db 'irq_wait : ligne calmee',0
t_unbind	db 'irq_unbind',0
t_svc		db 'svc_register',0
t_next		db 'svc_next',0
t_op		db 'op SVC_OP_SIGNAL',0
t_bits		db 'bits du signal',0
t_idle		db 'plus de signal',0
		align 8
msg		rb WS_MSG
