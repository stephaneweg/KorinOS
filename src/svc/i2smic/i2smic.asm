; i2smic - pilote du micro I2S (MEMS type INMP441) du Pi 4 (bloc PCM),
; docs/SON.md. Lance par init2 si la ligne SD0:/sys/svc/i2smic de
; SD0:/etc/services.txt n'est pas en commentaire (l'applet Son la met ou
; l'enleve : micro « a la demande »). Il s'inscrit aupres du service
; " sound " comme entree « Micro I2S » a 44,1 kHz (svc/inc/sndif.inc) ; sound
; lui donne un canal DMA « lite ».
; Repris de l'ancien son du noyau (audio_i2s.asm) : BCLK = GPIO18, LRCLK =
; GPIO19, DOUT -> GPIO20 (PCM_DIN), L/R du micro a la masse (canal gauche),
; 3,3 V. Le Pi est maitre : horloge PCM = 64 x 44 100 Hz = 2,8224 MHz depuis
; PLLD (750 MHz / 265,73, MASH 1) ; trames de 64 bits, canaux de 24 bits
; decales d'un bit (I2S), extension de signe. Reception par DMA (DREQ 3) dans
; deux tampons en anneau ; chaque tampon rempli est converti en s16 stereo
; (gauche duplique) vers l'anneau d'entree de sound.
;  * au depart : essai (80 ms) ; une ligne muette (DIN tiree a la masse) =
;    pas de micro : il sort (code 0, init2 ne le relance pas) ;
;  * le materiel ne tourne que quand une capture ecoute le micro (SR_WANTED,
;    dit par sound) ; arrete une seconde apres la derniere.
; Sous QEMU (bloc PCM non emule) : il sort aussitot (code 0).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/sound.inc'

GPIO_PHYS	= 0xFE200000
CM_PHYS		= 0xFE101000
PCM_PHYS	= 0xFE203000
DMA_PHYS	= 0xFE007000

GPFSEL1		= 0x04
GPFSEL2		= 0x08
GPIO_PUPDN1	= 0xE8			; GPIO 16..31, 2 bits chacun
CM_PCMCTL	= 0x98
CM_PASSWD	= 0x5A000000
PCM_CS		= 0x00
PCM_MODE	= 0x08
PCM_RXC		= 0x0C
PCM_TXC		= 0x10
PCM_DREQ	= 0x14
PCM_FIFO_BUS	= 0x7E203004

PCS_EN		= 1 shl 0
PCS_RXON	= 1 shl 1
PCS_TXCLR	= 1 shl 3
PCS_RXCLR	= 1 shl 4
PCS_DMAEN	= 1 shl 9
PCS_RXSEX	= 1 shl 23
PCS_STBY	= 1 shl 25

DMA_ENABLE	= 0xFF0
DMA_CS		= 0x00
DMA_CONBLK	= 0x04
CS_RESET	= 1 shl 31
CS_ERROR	= 1 shl 8
CS_GO		= (1 shl 28) or (15 shl 20) or (1 shl 16) or 1
TI_I2S_RX	= (3 shl 16) or (1 shl 10) or (1 shl 4) or (1 shl 3)
					; DREQ 3, SRC_DREQ, DEST_INC, WAIT_RESP
BUS_ALIAS	= 0xC0000000

FRAMES		= 1024			; trames par tampon (2 mots par trame)
BUF_BYTES	= FRAMES * 8
CB_OFF		= 2 * BUF_BYTES
DMA_BYTES	= CB_OFF + 4096
IDLE_MS		= 1000			; materiel arrete apres la derniere capture

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	mov	x0,0				; DMA de capture : priorite haute
	mov	x1,THREAD_PRIO_HIGH
	sys	SYS_THREAD_SET_PRIORITY
	adr	x0,fw_rev			; QEMU : PCM non emule
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
.real:	movl	x0,GPIO_PHYS
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
	movl	x0,PCM_PHYS
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	adr	x1,pcm_regs
	str	x0,[x1]
	movl	x0,DMA_PHYS
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	adr	x1,dma
	str	x0,[x1]
	movl	x0,DMA_BYTES
	adr	x1,dma_phys
	sys	SYS_DMA_ALLOC
	cmp	x0,0
	b.le	.nomem
	adr	x1,dma_va
	str	x0,[x1]
	adr	x0,dma_phys
	ldr	x0,[x0]
	movl	x1,DMA_BYTES
	add	x0,x0,x1
	movl	x1,0x40000000
	cmp	x0,x1
	b.hi	.nomem
	mov	x0,SND_IN_I2S			; entree de sound, un canal DMA
	movl	x1,SND_RATE_STD
	mov	x2,1
	adr	x3,s_name
	bl	sndif_input
	tbnz	x0,63,.nosnd
	adr	x0,sif_in_dma
	ldr	x0,[x0]
	tbnz	x0,63,.nodma
	bl	probe				; un micro repond ?
	cbnz	x0,.present
	adr	x0,m_absent
	b	.leave
.present:
	adr	x0,m_ok
	bl	puts
	adr	x0,sif_in_dma
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_in
	bl	puts
	adr	x0,sif_in
	ldr	x0,[x0,SIF_ID]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x19,0				; 1 : materiel en marche
	mov	x20,0				; prochain tampon a convertir
	mov	x21,0				; derniere ecoute (ms)
.loop:	bl	sndif_wanted
	cbz	x0,.idle
	sys	SYS_TIME_MS
	mov	x21,x0
	cbnz	x19,.run
	bl	hw_start			; une capture ecoute : en marche
	mov	x19,1
	bl	current
	eor	x20,x0,1			; d'abord celui qui n'est pas en cours
	b	.run
.idle:	cbz	x19,.wait
	sys	SYS_TIME_MS
	sub	x0,x0,x21
	cmp	x0,IDLE_MS
	b.lo	.run
	bl	hw_stop
	mov	x19,0
	b	.wait
.run:	cbz	x19,.wait
	bl	current
	tbnz	x0,63,.err
	cmp	x0,x20
	b.eq	.wait				; pas encore rempli
	mov	x0,x20
	bl	convert
	eor	x20,x20,1
	b	.loop
.err:	adr	x0,m_dmaerr
	bl	puts
	bl	hw_stop
	mov	x19,0
.wait:	bl	sndif_tick
	mov	x0,5
	sys	SYS_SLEEP
	b	.loop
.nomap:	adr	x0,m_nomap
	b	.fail
.nomem:	adr	x0,m_nomem
	b	.fail
.nosnd:	adr	x0,m_nosnd
	b	.fail
.nodma:	adr	x0,m_nodma
.fail:	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; -> x0 = 1 si un micro repond (un echantillon non nul en ~80 ms)
probe:
	prologue
	str	x19,[sp,-16]!
	bl	hw_start
	mov	x0,80
	sys	SYS_SLEEP
	adr	x0,dma_va
	ldr	x19,[x0]
	mov	x1,0
	mov	x0,0
.scan:	ldr	w2,[x19,x1,lsl 2]
	cbnz	w2,.yes
	add	x1,x1,2				; canal gauche seulement
	cmp	x1,2*FRAMES*2
	b.lo	.scan
	b	.stop
.yes:	mov	x0,1
.stop:	str	x0,[sp,-16]!
	bl	hw_stop
	ldr	x0,[sp],16
	ldr	x19,[sp],16
	epilogue

; Broches, horloge, bloc PCM, DMA de reception
hw_start:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,dma_va			; tampons a zero
	ldr	x0,[x0]
	movl	x1,DMA_BYTES/8
.z:	str	xzr,[x0],8
	subs	x1,x1,1
	b.ne	.z
	adr	x0,gpio				; GPIO 18, 19 (GPFSEL1 bits 24-29),
	ldr	x0,[x0]				; 20 (GPFSEL2 bits 0-2) -> ALT0
	ldr	w1,[x0,GPFSEL1]
	movl	w2,0x3F000000
	bic	w1,w1,w2
	movl	w2,(4 shl 24) or (4 shl 27)
	orr	w1,w1,w2
	str	w1,[x0,GPFSEL1]
	ldr	w1,[x0,GPFSEL2]
	and	w1,w1,not 7
	orr	w1,w1,4
	str	w1,[x0,GPFSEL2]
	ldr	w1,[x0,GPIO_PUPDN1]		; GPIO20 : tirage a la masse (10)
	and	w1,w1,not (3 shl 8)
	orr	w1,w1,2 shl 8
	str	w1,[x0,GPIO_PUPDN1]
	adr	x20,cm				; horloge PCM : PLLD / 265,73, MASH 1
	ldr	x20,[x20]
	add	x20,x20,CM_PCMCTL
	movl	w0,CM_PASSWD or (1 shl 5)	; KILL
	str	w0,[x20]
	movl	x21,100000
.busy:	ldr	w0,[x20]
	tbz	w0,7,.stopped
	subs	x21,x21,1
	b.ne	.busy
.stopped:
	movl	w0,CM_PASSWD or (265 shl 12) or 2994
	str	w0,[x20,4]
	movl	w0,CM_PASSWD or (1 shl 9) or 6
	str	w0,[x20]
	movl	w0,CM_PASSWD or (1 shl 9) or 6 or (1 shl 4)
	str	w0,[x20]
	mov	x0,2
	sys	SYS_SLEEP
	adr	x20,pcm_regs			; maitre, trame 64 bits, canaux 24 bits
	ldr	x20,[x20]			; en position 1 / 33
	str	wzr,[x20,PCM_CS]
	mov	w0,PCS_RXCLR or PCS_TXCLR
	str	w0,[x20,PCM_CS]
	mov	x0,1
	sys	SYS_SLEEP
	movl	w0,(1 shl 31) or (1 shl 30) or (1 shl 20) or (1 shl 15) or (1 shl 14) or (33 shl 4)
	str	w0,[x20,PCM_RXC]
	str	wzr,[x20,PCM_TXC]
	movl	w0,(1 shl 22) or (1 shl 20) or (63 shl 10) or 32	; CLKI FSI FLEN FSLEN
	str	w0,[x20,PCM_MODE]
	movl	w0,(0x10 shl 24) or (0x30 shl 16) or (0x18 shl 8) or 0x18
	str	w0,[x20,PCM_DREQ]
	movl	w0,PCS_STBY
	str	w0,[x20,PCM_CS]
	mov	x0,1
	sys	SYS_SLEEP
	movl	w0,PCS_STBY or PCS_EN or PCS_DMAEN or PCS_RXSEX
	str	w0,[x20,PCM_CS]
	adr	x0,dma_va			; CB0 -> tampon 0 -> CB1 -> tampon 1
	ldr	x9,[x0]
	add	x9,x9,CB_OFF
	adr	x0,dma_phys
	ldr	x22,[x0]
	mov	x5,0
.cb:	add	x6,x9,x5,lsl 5
	movl	w0,TI_I2S_RX
	str	w0,[x6]
	movl	w0,PCM_FIFO_BUS
	str	w0,[x6,4]			; SOURCE : FIFO PCM
	mov	x0,BUF_BYTES
	mul	x0,x0,x5
	add	x0,x22,x0
	orr	w0,w0,BUS_ALIAS
	str	w0,[x6,8]			; DEST : tampon
	mov	w0,BUF_BYTES
	str	w0,[x6,12]
	str	wzr,[x6,16]
	eor	x0,x5,1
	lsl	x0,x0,5
	add	x0,x0,CB_OFF
	add	x0,x22,x0
	orr	w0,w0,BUS_ALIAS
	str	w0,[x6,20]
	str	xzr,[x6,24]
	add	x5,x5,1
	cmp	x5,2
	b.lo	.cb
	dmb	sy
	adr	x0,dma				; canal : notre bit, remise a zero
	ldr	x19,[x0]
	adr	x0,sif_in_dma
	ldr	x21,[x0]
	ldr	w1,[x19,DMA_ENABLE]
	mov	w2,1
	lslv	w2,w2,w21
	orr	w1,w1,w2
	str	w1,[x19,DMA_ENABLE]
	add	x19,x19,x21,lsl 8
	adr	x0,ch_regs
	str	x19,[x0]
	movl	w0,CS_RESET
	str	w0,[x19,DMA_CS]
	mov	x0,1
	sys	SYS_SLEEP
	add	x0,x22,CB_OFF
	orr	w0,w0,BUS_ALIAS
	adr	x1,cb0_bus
	str	w0,[x1]
	str	w0,[x19,DMA_CONBLK]
	movl	w0,CS_GO
	str	w0,[x19,DMA_CS]
	movl	w0,PCS_STBY or PCS_EN or PCS_DMAEN or PCS_RXSEX or PCS_RXON
	str	w0,[x20,PCM_CS]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

hw_stop:
	adr	x0,pcm_regs
	ldr	x0,[x0]
	str	wzr,[x0,PCM_CS]
	adr	x0,ch_regs
	ldr	x0,[x0]
	cbz	x0,.ret
	str	wzr,[x0,DMA_CS]			; DMA en pause
.ret:	ret

; -> x0 = tampon en cours d'ecriture par le DMA (0/1), -1 : erreur du canal
current:
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

; x0 = tampon rempli : gauche (s32, 24 bits utiles) -> s16 stereo (duplique)
; -> anneau d'entree de sound
convert:
	prologue
	adr	x1,dma_va
	ldr	x1,[x1]
	mov	x2,BUF_BYTES
	madd	x3,x0,x2,x1
	adr	x4,pcm
	mov	x1,0
.cv:	add	x5,x3,x1,lsl 3
	ldr	w2,[x5]
	asr	w2,w2,8
	and	w2,w2,0xFFFF
	orr	w2,w2,w2,lsl 16
	str	w2,[x4,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,FRAMES
	b.lo	.cv
	adr	x0,pcm
	mov	x1,FRAMES
	bl	sndif_write
	epilogue

include '../inc/sndif.inc'

s_name		db 'Micro I2S',0
m_emul		db 'i2smic : pas de bloc PCM (QEMU) : rien ',0xC3,0xA0,' faire',10,0
m_absent	db 'i2smic : pas de micro I2S (ligne muette)',10,0
m_ok		db 'i2smic : micro I2S (INMP441), 44,1 kHz, canal DMA ',0
m_in		db ', entr',0xC3,0xA9,'e ',0
m_nomap		db 'i2smic : registres inaccessibles (dev_map)',10,0
m_nomem		db 'i2smic : pas de m',0xC3,0xA9,'moire DMA (premier Go)',10,0
m_nosnd		db 'i2smic : le service sound ne r',0xC3,0xA9,'pond pas',10,0
m_nodma		db 'i2smic : pas de canal DMA libre',10,0
m_dmaerr	db 'i2smic : erreur du canal DMA, arr',0xC3,0xAA,'t',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 16
fw_rev		dw 28, 0, 0x00000001, 4, 0, 0, 0	; GET_FIRMWARE_REVISION
		align 8
gpio		dd 0
cm		dd 0
pcm_regs	dd 0
dma		dd 0
ch_regs		dd 0
dma_va		dd 0
dma_phys	dd 0
cb0_bus		dd 0
sndif_data
		align 16
pcm		rb FRAMES * 4
