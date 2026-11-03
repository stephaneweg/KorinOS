; hdmiaudio - pilote de la sortie son HDMI0 du Pi 4 (bloc MAI, trames
; IEC958), docs/SON.md. Lance par hwmanager quand l'ecran annonce du son dans
; son EDID (drivers.txt : « hdmi0:audio = SD0:/sys/svc/hdmiaudio 0 »), arrete
; par lui (kill) quand l'ecran est debranche. Il s'inscrit aupres du service
; " sound " comme sortie « HDMI : <nom de l'ecran> » a 44,1 kHz
; (svc/inc/sndif.inc) ; sound lui donne un canal DMA « lite ».
; Repris de l'ancien son du noyau (audio_hdmi.asm) : il programme lui-meme le
; MAI, l'ACR (N = 6272, CTS d'apres l'horloge pixel du firmware) et
; l'InfoFrame audio, mais ni la PHY ni le mode video (le firmware doit voir
; un ecran HDMI au demarrage : hdmi_drive=2 dans config.txt). Echantillons
; 24 bits (16 bits << 8), DMA cyclique sur deux tampons, DREQ 10 ; toutes
; les 5 ms, le tampon que le DMA ne lit pas est rempli (sndif_read ->
; sous-trames IEC958). Sous QEMU (HDMI non emule) : il sort aussitot (code 0).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/sound.inc'

HDMI_PAGE	= 0xFEF00000		; HDMI0 (+0x700), PHY (+0xF00), RAM (+0x1B00)
HD_PHYS		= 0xFEF20000
DMA_PHYS	= 0xFE007000
HDMI_OFF	= 0x700
HPHY_OFF	= 0xF00
HRAM_OFF	= 0x1B00

HDMI_MAI_CHMAP	= 0x9C
HDMI_MAI_CONFIG	= 0xA0
HDMI_AUDIO_PKT	= 0xB8
HDMI_RAM_CFG	= 0xBC
HDMI_RAM_STS	= 0xC4
HDMI_CRP_CFG	= 0xC8
HDMI_CTS0	= 0xCC
HDMI_CTS1	= 0xD0
HD_MAI_CTL	= 0x10
HD_MAI_THR	= 0x14
HD_MAI_FMT	= 0x18
HD_MAI_SMP	= 0x20
HRAM_AUDIO0	= 0x90
HPHY_PWRDOWN	= 0x04
MAI_DATA_BUS	= 0x7EF2001C

MAI_RESET	= 1 shl 0
MAI_ERRFULL	= 1 shl 1
MAI_ERREMPTY	= 1 shl 2
MAI_ENABLE	= 1 shl 3
MAI_FLUSH	= 1 shl 9
MAI_WHOLSMP	= 1 shl 12
MAI_CHALIGN	= 1 shl 13
MAI_DELAYED	= 1 shl 15

HDMI_ACR_N	= 6272			; N recommande pour 44,1 kHz
IEC_BLOCK	= 192			; trames par bloc IEC958

DMA_ENABLE	= 0xFF0
DMA_CS		= 0x00
DMA_CONBLK	= 0x04
CS_RESET	= 1 shl 31
CS_ERROR	= 1 shl 8
CS_GO		= (1 shl 28) or (15 shl 20) or (1 shl 16) or 1
TI_HDMI		= (10 shl 16) or (1 shl 9) or (1 shl 8) or (1 shl 6) or (1 shl 3)
					; DREQ 10, 128 bits, SRC_INC, DEST_DREQ, WAIT_RESP
BUS_ALIAS	= 0xC0000000

FRAMES		= SND_BLOCK
BUF_BYTES	= FRAMES * 8		; deux sous-trames de 32 bits par trame
CB_OFF		= 2 * BUF_BYTES
DMA_BYTES	= CB_OFF + 4096

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0				; argument : 0 (HDMI0)
	bl	kits_open
	mov	x0,0				; DMA et remplissage : priorite haute
	mov	x1,THREAD_PRIO_HIGH
	sys	SYS_THREAD_SET_PRIORITY
	cbz	x19,.fw
	ldrb	w0,[x19]
	cmp	w0,'0'
	b.eq	.fw
	adr	x0,m_port			; HDMI1 : pas encore
	b	.leave
.fw:	adr	x0,fw_rev			; QEMU : HDMI non emule
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
.real:	movl	x0,HDMI_PAGE			; registres
	mov	x1,0x2000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	adr	x1,hpage
	str	x0,[x1]
	movl	x0,HD_PHYS
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	adr	x1,hd
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
	adr	x0,dma_phys			; (DMA lite : premier Go)
	ldr	x0,[x0]
	movl	x1,DMA_BYTES
	add	x0,x0,x1
	movl	x1,0x40000000
	cmp	x0,x1
	b.hi	.nomem
	adr	x0,fw_clk			; horloge pixel (pour CTS)
	sys	SYS_FW_CALL
	tbnz	x0,63,.noclk
	adr	x0,fw_clk
	ldr	w0,[x0,24]
	cbz	w0,.noclk
	adr	x1,pixclk
	str	x0,[x1]
	bl	edid_name			; « HDMI : <ecran> »
	mov	x0,SND_OUT_HDMI
	movl	x1,SND_RATE_STD
	mov	x2,1
	adr	x3,name
	bl	sndif_output
	tbnz	x0,63,.nosnd
	adr	x0,sif_out_dma
	ldr	x0,[x0]
	tbnz	x0,63,.nodma
	adr	x0,hpage			; (le firmware avait-il allume l'audio ?)
	ldr	x0,[x0]
	add	x0,x0,HDMI_OFF
	ldr	w0,[x0,HDMI_RAM_CFG]
	ubfx	x0,x0,16,1
	adr	x1,fw_audio
	str	x0,[x1]
	bl	sndif_irq_bind
	bl	hdmi_start
	adr	x0,m_ok
	bl	puts
	adr	x0,sif_out_dma
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_out
	bl	puts
	adr	x0,sif_out
	ldr	x0,[x0,SIF_ID]
	bl	print_dec
	adr	x0,m_fwa
	bl	puts
	adr	x0,fw_audio
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_par
	bl	puts
	mov	x19,0				; prochain tampon a remplir
.loop:	bl	dma_current
	tbnz	x0,63,.dmaerr
	cmp	x0,x19
	b.eq	.wait
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
	adr	x0,m_dmaerr
	bl	puts
	bl	dma_run
	mov	x19,0
	b	.loop
.nomap:	adr	x0,m_nomap
	b	.fail
.nomem:	adr	x0,m_nomem
	b	.fail
.noclk:	adr	x0,m_noclk
	b	.fail
.nosnd:	adr	x0,m_nosnd
	b	.fail
.nodma:	adr	x0,m_nodma
.fail:	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; name <- « HDMI : <nom de l'ecran> » (descripteur 0xFC de l'EDID, bloc 0),
; « HDMI » a defaut
edid_name:
	prologue
	adr	x0,name
	adr	x1,s_hdmi
	bl	str_cat
	mov	x9,x0				; fin
	adr	x0,fw_edid
	sys	SYS_FW_CALL
	tbnz	x0,63,.out
	adr	x0,fw_edid
	ldr	w1,[x0,24]			; etat (0 : lu)
	cbnz	w1,.out
	add	x2,x0,28			; les 128 octets
	mov	x3,54				; descripteurs de 18 octets
.d:	cmp	x3,108
	b.hi	.out
	add	x4,x2,x3
	ldrb	w5,[x4]
	ldrb	w6,[x4,1]
	orr	w5,w5,w6
	ldrb	w6,[x4,3]
	cbnz	w5,.n
	cmp	w6,0xFC				; nom de l'ecran
	b.eq	.got
.n:	add	x3,x3,18
	b	.d
.got:	adr	x1,s_sep
.s:	ldrb	w5,[x1],1
	strb	w5,[x9],1
	cbnz	w5,.s
	sub	x9,x9,1
	add	x4,x4,5
	mov	x6,13
.c:	ldrb	w5,[x4],1
	cmp	w5,0x0A
	b.eq	.end
	cmp	w5,' '
	b.lo	.end
	strb	w5,[x9],1
	subs	x6,x6,1
	b.ne	.c
.end:	sub	x9,x9,1				; espaces de fin retires
	ldrb	w5,[x9]
	cmp	w5,' '
	b.eq	.end
	strb	wzr,[x9,1]
.out:	epilogue

; MAI, ACR, InfoFrame audio, silence, DMA
hdmi_start:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	; MaiSampleRate : 108 MHz / 44 100 ~ N / M, M <= 256
	movl	x19,108000000
	movl	x20,SND_RATE_STD
	mov	x23,0				; meilleur N
	mov	x24,1				; meilleur M
	mov	x21,-1				; meilleure erreur
	mov	x9,1
.m:	cmp	x9,256
	b.hi	.m_done
	mul	x10,x19,x9
	lsr	x11,x20,1
	add	x11,x10,x11
	udiv	x11,x11,x20			; N arrondi
	movl	x12,0xFFFFFF
	cmp	x11,x12
	b.hi	.m_next
	mul	x12,x11,x20
	subs	x12,x12,x10
	cneg	x12,x12,mi
	cmn	x21,1
	b.eq	.take
	mul	x13,x12,x24
	mul	x14,x21,x9
	cmp	x13,x14
	b.hs	.m_next
.take:	mov	x21,x12
	mov	x23,x11
	mov	x24,x9
.m_next:
	add	x9,x9,1
	b	.m
.m_done:
	adr	x0,pixclk			; CTS = pixclk * N / (128 * fs)
	ldr	x0,[x0]
	mov	x1,HDMI_ACR_N
	mul	x0,x0,x1
	mov	x1,128
	mul	x1,x1,x20
	udiv	x21,x0,x1
	adr	x0,hpage
	ldr	x19,[x0]
	add	x19,x19,HDMI_OFF
	adr	x0,hd
	ldr	x20,[x0]
	movl	w0,MAI_RESET or MAI_FLUSH or MAI_DELAYED or MAI_ERREMPTY or MAI_ERRFULL
	str	w0,[x20,HD_MAI_CTL]
	sub	x1,x24,1			; MaiSampleRate = N << 8 | (M - 1)
	orr	x0,x1,x23,lsl 8
	str	w0,[x20,HD_MAI_SMP]
	movl	w0,(8 shl 8) or (2 shl 16)	; 44,1 kHz (code 8), PCM
	str	w0,[x20,HD_MAI_FMT]
	movl	w0,0x10101010
	str	w0,[x20,HD_MAI_THR]
	movl	w0,(1 shl 27) or (1 shl 26) or 3	; FormatReverse, BitReverse, 2 canaux
	str	w0,[x19,HDMI_MAI_CONFIG]
	mov	w0,0x10
	str	w0,[x19,HDMI_MAI_CHMAP]
	movl	w0,3 or (0x0F shl 10) or (1 shl 24) or (1 shl 29)
	str	w0,[x19,HDMI_AUDIO_PKT]
	movl	w0,(1 shl 24) or HDMI_ACR_N	; CTS externe, N
	str	w0,[x19,HDMI_CRP_CFG]
	str	w21,[x19,HDMI_CTS0]
	str	w21,[x19,HDMI_CTS1]
	ldr	w0,[x19,HDMI_RAM_CFG]		; InfoFrame audio (paquet 4)
	and	w0,w0,not (1 shl 4)
	str	w0,[x19,HDMI_RAM_CFG]
	mov	x0,4
	mov	x1,0
	bl	ram_wait
	adr	x0,hpage
	ldr	x21,[x0]
	movl	x0,HRAM_OFF + HRAM_AUDIO0
	add	x21,x21,x0
	movl	w0,0x000A0184			; type 0x84, version 1, longueur 10
	str	w0,[x21]
	mov	w0,0x170			; somme de controle 0x70, 2 canaux
	str	w0,[x21,4]
	mov	x0,8
.zero:	cmp	x0,0x24
	b.hi	.ifr
	str	wzr,[x21,x0]
	add	x0,x0,4
	b	.zero
.ifr:	ldr	w0,[x19,HDMI_RAM_CFG]
	orr	w0,w0,1 shl 4
	str	w0,[x19,HDMI_RAM_CFG]
	mov	x0,4
	mov	x1,1
	bl	ram_wait
	adr	x0,pcm				; silence IEC958 dans les deux tampons
	mov	x1,FRAMES
.z:	str	wzr,[x0],4
	subs	x1,x1,1
	b.ne	.z
	mov	x0,0
	bl	iec_convert
	mov	x0,1
	bl	iec_convert
	adr	x0,dma_va			; CB0 -> tampon 0 -> CB1 -> tampon 1
	ldr	x22,[x0]
	adr	x0,dma_phys
	ldr	x23,[x0]
	add	x9,x22,CB_OFF
	mov	x5,0
.cb:	add	x6,x9,x5,lsl 5
	movl	w0,TI_HDMI
	adr	x1,sif_irq			; INTEN : notre ligne seulement
	ldr	x1,[x1]
	cmp	x1,0
	cinc	w0,w0,ne
	str	w0,[x6]
	mov	x0,BUF_BYTES
	mul	x0,x0,x5
	add	x0,x23,x0
	orr	w0,w0,BUS_ALIAS
	str	w0,[x6,4]
	movl	w0,MAI_DATA_BUS
	str	w0,[x6,8]
	mov	w0,BUF_BYTES
	str	w0,[x6,12]
	str	wzr,[x6,16]
	eor	x0,x5,1
	lsl	x0,x0,5
	add	x0,x0,CB_OFF
	add	x0,x23,x0
	orr	w0,w0,BUS_ALIAS
	str	w0,[x6,20]
	str	xzr,[x6,24]
	add	x5,x5,1
	cmp	x5,2
	b.lo	.cb
	dmb	sy
	bl	dma_run
	adr	x0,hpage			; generateur de la PHY actif
	ldr	x0,[x0]
	add	x0,x0,HPHY_OFF
	ldr	w1,[x0,HPHY_PWRDOWN]
	and	w1,w1,not (1 shl 4)
	str	w1,[x0,HPHY_PWRDOWN]
	movl	w0,(2 shl 4) or MAI_WHOLSMP or MAI_CHALIGN or MAI_ENABLE
	str	w0,[x20,HD_MAI_CTL]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = bit de RamPacketStatus, x1 = valeur attendue (100 ms au plus)
ram_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	sys	SYS_TIME_MS
	add	x21,x0,100
.w:	adr	x0,hpage
	ldr	x0,[x0]
	add	x0,x0,HDMI_OFF
	ldr	w0,[x0,HDMI_RAM_STS]
	lsrv	w0,w0,w19
	and	w0,w0,1
	cmp	w0,w20
	b.eq	.out
	sys	SYS_TIME_MS
	cmp	x0,x21
	b.lo	.w
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Canal : notre bit de DMA_ENABLE, remise a zero, lance sur CB0
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
	add	x19,x19,x20,lsl 8
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

; -> x0 = tampon lu par le DMA (0/1), -1 si le canal est en erreur
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

; x0 = tampon : FRAMES trames de sound -> sous-trames IEC958
fill:
	prologue
	str	x0,[sp,-16]!
	adr	x0,pcm
	mov	x1,FRAMES
	bl	sndif_read
	ldr	x0,[sp],16
	bl	iec_convert
	epilogue

; x0 = tampon DMA : pcm (s16 stereo) -> sous-trames IEC958
;   bit 31 P (parite paire des bits 4..30), 30 C (statut du canal), 29 U,
;   28 V, 27..4 echantillon 24 bits, 3..0 preambule 0x0F en debut de bloc
iec_status:					; 40 premiers bits C : PCM grand public,
	db 4, 0, 0, 0, 0xFB			; 44,1 kHz, 24 bits, origine 44,1 kHz
	align 4
iec_convert:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x1,dma_va
	ldr	x1,[x1]
	mov	x2,BUF_BYTES
	madd	x19,x0,x2,x1			; destination
	adr	x20,pcm
	adr	x21,iec_frame
	ldr	x22,[x21]			; trame dans le bloc
	mov	x5,0
.f:	cmp	x5,FRAMES
	b.hs	.done
	mov	w8,0				; bit C de cette trame
	cmp	x22,40
	b.hs	.cbit
	lsr	x9,x22,3
	adr	x10,iec_status
	ldrb	w9,[x10,x9]
	and	x10,x22,7
	lsrv	w9,w9,w10
	and	w8,w9,1
.cbit:	mov	x6,0
.sub:	add	x7,x6,x5,lsl 1
	ldrsh	w9,[x20,x7,lsl 1]
	lsl	w9,w9,8				; 24 bits
	and	w9,w9,0xFFFFFF
	lsl	w9,w9,4
	orr	w9,w9,w8,lsl 30
	eor	w10,w9,w9,lsr 16		; parite des bits 4..30
	eor	w10,w10,w10,lsr 8
	eor	w10,w10,w10,lsr 4
	eor	w10,w10,w10,lsr 2
	eor	w10,w10,w10,lsr 1
	and	w10,w10,1
	orr	w9,w9,w10,lsl 31
	cbnz	x22,.np
	orr	w9,w9,0x0F			; preambule de debut de bloc
.np:	str	w9,[x19,x7,lsl 2]
	add	x6,x6,1
	cmp	x6,2
	b.lo	.sub
	add	x22,x22,1
	cmp	x22,IEC_BLOCK
	csel	x22,xzr,x22,hs
	add	x5,x5,1
	b	.f
.done:	str	x22,[x21]
	dmb	sy
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include '../inc/sndif.inc'

s_hdmi		db 'HDMI',0
s_sep		db ' : ',0
m_port		db 'hdmiaudio : seul HDMI0 (argument 0) est pris en charge',10,0
m_emul		db 'hdmiaudio : pas de HDMI (QEMU) : rien ',0xC3,0xA0,' faire',10,0
m_ok		db 'hdmiaudio : HDMI0 (IEC958), 44,1 kHz, canal DMA ',0
m_out		db ', sortie ',0
m_fwa		db ' (audio allum',0xC3,0xA9,' par le firmware : ',0
m_par		db ')',10,0
m_nomap		db 'hdmiaudio : registres inaccessibles (dev_map)',10,0
m_nomem		db 'hdmiaudio : pas de m',0xC3,0xA9,'moire DMA (premier Go)',10,0
m_noclk		db 'hdmiaudio : horloge pixel inconnue (firmware)',10,0
m_nosnd		db 'hdmiaudio : le service sound ne r',0xC3,0xA9,'pond pas',10,0
m_nodma		db 'hdmiaudio : pas de canal DMA libre',10,0
m_dmaerr	db 'hdmiaudio : erreur du canal DMA, relance',10,0
		align 8

segment readable writeable
kit_data
		align 16
fw_rev		dw 28, 0, 0x00000001, 4, 0, 0, 0	; GET_FIRMWARE_REVISION
		align 16
fw_clk		dw 32, 0, 0x00030002, 8, 0, 14, 0, 0	; GET_CLOCK_RATE (pixel BVB)
		align 16
fw_edid		dw 160, 0, 0x00030020, 136, 0, 0, 0	; GET_EDID_BLOCK 0
		rb 128
		dw 0
		align 8
hpage		dd 0
hd		dd 0
dma		dd 0
ch_regs		dd 0
dma_va		dd 0
dma_phys	dd 0
cb0_bus		dd 0
pixclk		dd 0
fw_audio	dd 0
iec_frame	dd 0
name		rb 64
sndif_data
		align 16
pcm		rb FRAMES * 4
