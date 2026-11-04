; pwmaudio - pilote de la sortie analogique (prise jack) du Pi 4 : PWM1
; (GPIO 40/41), docs/SON.md. Lance par init2 (SD0:/etc/services.txt : la
; prise ne se detecte pas). Il s'inscrit aupres du service " sound " comme
; sortie « Prise jack » a 44,1 kHz (svc/inc/sndif.inc) ; sound lui donne un
; canal DMA « lite » (7-14 : Elegant prend le plus haut des canaux complets
; 0-6, aucun conflit). Repris de l'ancien son du noyau (audio.asm) :
;  * horloge PWM : PLLD / 6 = 125 MHz ; plage 2834 -> 44 105 Hz par canal ;
;  * DMA cyclique sur deux tampons de 1024 trames (u32 par canal, DREQ 1) :
;    CB0 -> tampon 0 -> CB1 -> tampon 1 -> CB0... ; a chaque fin de tampon
;    (interruption ; 5 ms sans elle), le tampon que le DMA ne lit pas est
;    rempli (sndif_read, s16 -> plage PWM, canaux inverses sur le Pi 4).
; Sous QEMU (PWM non emule : un acces est une erreur de bus) ou avec
; « no_pwmaudio » dans SD0:/etc/options.txt : il sort aussitot (code 0 :
; init2 ne le relance pas).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/sound.inc'
include '../inc/board.inc'

GPIO_PHYS	= 0xFE200000
CM_PHYS		= 0xFE101000
PWM_PAGE	= 0xFE20C000
DMA_PHYS	= 0xFE007000

GPFSEL4		= 0x10
CM_PWMCTL	= 0xA0			; (dans la page du gestionnaire d'horloges)
CM_PASSWD	= 0x5A000000
PWM1_OFF	= 0x800			; PWM1 dans sa page
PWM_CTL		= 0x00
PWM_DMAC	= 0x08
PWM_RNG1	= 0x10
PWM_RNG2	= 0x20
PWM_FIF1_BUS	= 0x7E20C818
PWM_RANGE	= 2834

DMA_ENABLE	= 0xFF0
DMA_CS		= 0x00
DMA_CONBLK	= 0x04
CS_RESET	= 1 shl 31
CS_ERROR	= 1 shl 8
CS_GO		= (1 shl 28) or (15 shl 20) or (1 shl 16) or 1
TI_PWM		= (1 shl 16) or (1 shl 9) or (1 shl 8) or (1 shl 6) or (1 shl 3)
					; DREQ 1, 128 bits, SRC_INC, DEST_DREQ, WAIT_RESP
BUS_ALIAS	= 0xC0000000

FRAMES		= SND_BLOCK		; trames par tampon DMA
BUF_BYTES	= FRAMES * 8		; u32 gauche + u32 droite
CB_OFF		= 2 * BUF_BYTES		; blocs de controle apres les deux tampons
DMA_BYTES	= CB_OFF + 4096

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	mov	x0,0				; DMA et remplissage : priorite haute
	mov	x1,THREAD_PRIO_HIGH
	sys	SYS_THREAD_SET_PRIORITY
	adr	x0,s_no_opt			; no_pwmaudio : rien
	bl	option_word
	cbz	x0,.fw
	adr	x0,m_no_opt
	b	.leave
.fw:	adr	x0,fw_rev			; QEMU : PWM non emule
	sys	SYS_FW_CALL
	tbnz	x0,63,.emul
	adr	x0,fw_rev
	ldr	w0,[x0,20]
	movl	w1,0x50000000
	cmp	w0,w1
	b.hs	.real
.emul:	adr	x0,m_emul
.leave:	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.real:	bl	board_pi5			; Pi 5 : pas de prise jack
	cbz	x0,.pi4
	adr	x0,m_pi5
	b	.leave
.pi4:	movl	x0,GPIO_PHYS			; registres
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	adr	x1,gpio
	str	x0,[x1]
	movl	x0,CM_PHYS
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	adr	x1,cm
	str	x0,[x1]
	movl	x0,PWM_PAGE
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	add	x0,x0,PWM1_OFF
	adr	x1,pwm
	str	x0,[x1]
	movl	x0,DMA_PHYS
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	adr	x1,dma
	str	x0,[x1]
	movl	x0,DMA_BYTES			; tampons + blocs (non caches)
	adr	x1,dma_phys
	sys	SYS_DMA_ALLOC
	cmp	x0,0
	b.le	.nomem
	adr	x1,dma_va
	str	x0,[x1]
	adr	x0,dma_phys			; le DMA lite n'adresse que le
	ldr	x0,[x0]				; premier Go
	movl	x1,DMA_BYTES
	add	x0,x0,x1
	movl	x1,0x40000000
	cmp	x0,x1
	b.hi	.nomem
	mov	x0,SND_OUT_PWM			; sortie de sound, un canal DMA
	movl	x1,SND_RATE_STD
	mov	x2,1
	adr	x3,s_name
	bl	sndif_output
	tbnz	x0,63,.nosnd
	adr	x0,sif_out_dma
	ldr	x0,[x0]
	tbnz	x0,63,.nodma
	bl	sndif_irq_bind
	bl	pwm_start
	adr	x0,m_ok				; « pwmaudio : PWM1 (prise jack)... »
	bl	puts
	adr	x0,sif_out_dma
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_out
	bl	puts
	adr	x0,sif_out
	ldr	x0,[x0,SIF_ID]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x19,0				; prochain tampon a remplir
.loop:	bl	dma_current			; tampon lu par le DMA
	tbnz	x0,63,.dmaerr
	cmp	x0,x19
	b.eq	.wait				; on attend qu'il quitte notre cible
	mov	x0,x19
	bl	fill
	eor	x19,x19,1
	b	.loop
.wait:	bl	sndif_tick
	adr	x0,ch_regs
	ldr	x0,[x0]
	bl	sndif_irq_wait
	b	.loop
.dmaerr:
	adr	x0,m_dmaerr			; erreur du canal : on relance
	bl	puts
	bl	dma_run
	mov	x19,0
	b	.loop
.nomap:	adr	x0,m_nomap
	b	.fail
.nomem:	adr	x0,m_nomem
	b	.fail
.nosnd:	adr	x0,m_nosnd
	b	.fail
.nodma:	adr	x0,m_nodma
.fail:	bl	puts
	mov	x0,1				; (init2 relance)
	sys	SYS_EXIT

; Broches, horloge, PWM, silence, DMA cyclique
pwm_start:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,gpio				; GPIO 40, 41 -> ALT0 (GPFSEL4 : 100 100)
	ldr	x0,[x0]
	ldr	w1,[x0,GPFSEL4]
	and	w1,w1,not 0x3F
	mov	w2,0x24
	orr	w1,w1,w2
	str	w1,[x0,GPFSEL4]
	adr	x19,cm				; horloge PWM : PLLD (6) / 6 = 125 MHz
	ldr	x19,[x19]
	add	x19,x19,CM_PWMCTL
	movl	w0,CM_PASSWD or (1 shl 5)	; KILL
	str	w0,[x19]
	movl	x20,100000
.busy:	ldr	w0,[x19]
	tbz	w0,7,.stopped
	subs	x20,x20,1
	b.ne	.busy
.stopped:
	movl	w0,CM_PASSWD or (6 shl 12)	; DIVI = 6
	str	w0,[x19,4]
	movl	w0,CM_PASSWD or 6
	str	w0,[x19]
	movl	w0,CM_PASSWD or 6 or (1 shl 4)	; ENAB
	str	w0,[x19]
	mov	x0,2
	sys	SYS_SLEEP
	adr	x19,pwm				; deux canaux, FIFO, plage
	ldr	x19,[x19]
	str	wzr,[x19,PWM_CTL]
	mov	w0,PWM_RANGE
	str	w0,[x19,PWM_RNG1]
	str	w0,[x19,PWM_RNG2]
	movl	w0,(1 shl 0) or (1 shl 5) or (1 shl 8) or (1 shl 13) or (1 shl 6)
	str	w0,[x19,PWM_CTL]		; PWEN1 USEF1 PWEN2 USEF2 CLRF1
	mov	x0,2
	sys	SYS_SLEEP
	movl	w0,(1 shl 31) or 0x0707		; DMAC : ENAB, seuils 7
	str	w0,[x19,PWM_DMAC]
	adr	x0,dma_va			; silence dans les deux tampons
	ldr	x0,[x0]
	mov	w1,PWM_RANGE/2
	mov	x2,2*FRAMES*2
.s:	str	w1,[x0],4
	subs	x2,x2,1
	b.ne	.s
	adr	x0,dma_va			; CB0 -> tampon 0 -> CB1 -> tampon 1
	ldr	x19,[x0]
	adr	x0,dma_phys
	ldr	x20,[x0]
	add	x9,x19,CB_OFF
	mov	x5,0
.cb:	add	x6,x9,x5,lsl 5
	movl	w0,TI_PWM
	adr	x1,sif_irq			; INTEN : notre ligne seulement
	ldr	x1,[x1]
	cmp	x1,0
	cinc	w0,w0,ne
	str	w0,[x6]				; TI
	mov	x0,BUF_BYTES
	mul	x0,x0,x5
	add	x0,x20,x0
	orr	w0,w0,BUS_ALIAS
	str	w0,[x6,4]			; SOURCE_AD
	movl	w0,PWM_FIF1_BUS
	str	w0,[x6,8]			; DEST_AD = FIFO
	mov	w0,BUF_BYTES
	str	w0,[x6,12]			; TXFR_LEN
	str	wzr,[x6,16]
	eor	x0,x5,1				; NEXTCONBK : l'autre bloc
	lsl	x0,x0,5
	add	x0,x0,CB_OFF
	add	x0,x20,x0
	orr	w0,w0,BUS_ALIAS
	str	w0,[x6,20]
	str	xzr,[x6,24]
	add	x5,x5,1
	cmp	x5,2
	b.lo	.cb
	dmb	sy
	bl	dma_run
	ldp	x19,x20,[sp],16
	epilogue

; Canal : active (DMA_ENABLE, notre bit seulement), remis a zero, lance sur CB0
dma_run:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,dma
	ldr	x19,[x0]
	adr	x0,sif_out_dma
	ldr	x20,[x0]
	ldr	w1,[x19,DMA_ENABLE]
	mov	w2,1
	lslv	w2,w2,w20
	orr	w1,w1,w2
	str	w1,[x19,DMA_ENABLE]
	add	x19,x19,x20,lsl 8		; registres du canal
	adr	x0,ch_regs
	str	x19,[x0]
	movl	w0,CS_RESET
	str	w0,[x19,DMA_CS]
	mov	x0,1
	sys	SYS_SLEEP
	adr	x0,dma_phys
	ldr	x0,[x0]
	add	x0,x0,CB_OFF
	orr	w0,w0,BUS_ALIAS
	adr	x1,cb0_bus
	str	w0,[x1]
	str	w0,[x19,DMA_CONBLK]
	movl	w0,CS_GO
	str	w0,[x19,DMA_CS]
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = tampon lu par le DMA (0 : CB0, 1 : CB1), -1 si le canal est en erreur
dma_current:
	adr	x0,ch_regs
	ldr	x0,[x0]
	ldr	w1,[x0,DMA_CS]
	tst	w1,CS_ERROR
	b.ne	.err
	ldr	w1,[x0,DMA_CONBLK]
	adr	x2,cb0_bus
	ldr	w2,[x2]
	cmp	w1,w2
	cset	x0,ne
	ret
.err:	mov	x0,-1
	ret

; x0 = tampon : FRAMES trames de sound -> valeurs PWM (canaux inverses)
fill:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x1,dma_va
	ldr	x1,[x1]
	mov	x2,BUF_BYTES
	madd	x19,x0,x2,x1
	adr	x0,pcm
	mov	x1,FRAMES
	bl	sndif_read			; (silence s'il manque des trames)
	adr	x20,pcm
	mov	x5,0
	mov	w11,PWM_RANGE
.cv:	ldrsh	w6,[x20],2			; gauche
	ldrsh	w7,[x20],2			; droite
	add	w6,w6,32768
	mul	w6,w6,w11
	lsr	w6,w6,16
	add	w7,w7,32768
	mul	w7,w7,w11
	lsr	w7,w7,16
	stp	w7,w6,[x19],8
	add	x5,x5,1
	cmp	x5,FRAMES
	b.lo	.cv
	dmb	sy
	ldp	x19,x20,[sp],16
	epilogue

; x0 = mot -> x0 = 1 s'il commence une ligne de SD0:/etc/options.txt
option_word:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x0,p_options
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	mov	x19,x0
	adr	x1,optbuf
	mov	x2,1023
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.non
	adr	x0,optbuf
	strb	wzr,[x0,x20]
.ligne:	mov	x2,0
.c:	ldrb	w3,[x21,x2]
	cbz	w3,.fin
	ldrb	w4,[x0,x2]
	cmp	w3,w4
	b.ne	.suiv
	add	x2,x2,1
	b	.c
.fin:	ldrb	w4,[x0,x2]
	cmp	w4,' '
	b.ls	.oui
.suiv:	ldrb	w3,[x0],1
	cbz	w3,.non
	cmp	w3,10
	b.ne	.suiv
	b	.ligne
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include '../inc/sndif.inc'
board_code

s_name		db 'Prise jack',0
s_no_opt	db 'no_pwmaudio',0
p_options	db 'SD0:/etc/options.txt',0
m_no_opt	db 'pwmaudio : no_pwmaudio (options.txt) : rien',10,0
m_pi5		db 'pwmaudio : Pi 5 : pas de prise jack',10,0
m_emul		db 'pwmaudio : pas de PWM (QEMU) : rien ',0xC3,0xA0,' faire',10,0
m_ok		db 'pwmaudio : PWM1 (prise jack), 44,1 kHz, canal DMA ',0
m_out		db ', sortie ',0
m_nomap		db 'pwmaudio : registres inaccessibles (dev_map)',10,0
m_nomem		db 'pwmaudio : pas de m',0xC3,0xA9,'moire DMA (premier Go)',10,0
m_nosnd		db 'pwmaudio : le service sound ne r',0xC3,0xA9,'pond pas',10,0
m_nodma		db 'pwmaudio : pas de canal DMA libre',10,0
m_dmaerr	db 'pwmaudio : erreur du canal DMA, relance',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
board_data
		align 16
fw_rev		dw 28, 0, 0x00000001, 4, 0, 0, 0	; GET_FIRMWARE_REVISION
		align 8
gpio		dd 0
cm		dd 0
pwm		dd 0
dma		dd 0
ch_regs		dd 0
dma_va		dd 0
dma_phys	dd 0
cb0_bus		dd 0
sndif_data
		align 16
pcm		rb FRAMES * 4
optbuf		rb 1024
