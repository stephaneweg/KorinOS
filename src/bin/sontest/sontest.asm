; sontest - test de la sortie son par AudioKit : gamme do4..do5 (300 ms par
; note) puis la 440 Hz pendant 2 s, sinus genere par rotation de vecteur (FP).
;   sontest            flux s16 stereo 44,1 kHz (format natif)
;   sontest <freq>     flux s16 MONO a <freq> Hz (conversion et
;                      reechantillonnage d'AudioKit), ex. sontest 22050
; A la fin : la cadence mesuree (trames jouees par seconde, attendu : la
; frequence du flux), les manques, la sortie et le chemin (service sound ou
; son du noyau).
;   sontest driver [freq] [s]   faux pilote (essai de svc/inc/sndif.inc) :
;                      s'inscrit aupres de sound comme sortie USB (elle
;                      devient la principale) a <freq> Hz (48000), demande
;                      un canal DMA, consomme son anneau au rythme de
;                      l'horloge pendant <s> secondes (6), puis dit les
;                      trames lues, la cadence et la crete (ce que sound y
;                      a melange)
;   sontest mic [freq] [s]      faux micro : s'inscrit comme entree USB a
;                      <freq> Hz (48000) et y ecrit un triangle a 440 Hz
;                      (crete 8191) au rythme de l'horloge pendant <s>
;                      secondes (6) ; dit combien de temps une capture l'a
;                      ecoute (SR_WANTED)
include '../../inc/armpi.inc'
include '../../inc/sound.inc'
app_header
include '../../inc/kit.inc'

BLOCK	= 512				; trames par ecriture

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	cbz	x0,.std
	ldrb	w1,[x0]
	cmp	w1,'d'
	b.eq	drv_mode
	cmp	w1,'m'
	b.eq	mic_mode
	ldrb	w1,[x0]
	sub	w1,w1,'0'
	cmp	w1,9
	b.hi	.std
	bl	parse_dec			; sontest <frequence> : mono
	movl	x1,4000
	cmp	x0,x1
	b.lo	.std
	adr	x1,rate
	str	x0,[x1]
	adr	x1,chans
	mov	x2,1
	str	x2,[x1]
.std:	adr	x0,rate				; theta par Hz : 2 pi / frequence
	ldr	x0,[x0]
	scvtf	d1,x0
	adr	x1,two_pi
	ldr	d0,[x1]
	fdiv	d0,d0,d1
	adr	x1,two_pi_over_rate
	str	d0,[x1]
	adr	x0,m_start
	bl	puts
	adr	x0,rate
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_hz
	bl	puts
	adr	x0,chans
	ldr	x1,[x0]
	adr	x0,m_stereo
	cmp	x1,1
	b.ne	.p
	adr	x0,m_mono
.p:	bl	puts
	adr	x0,rate
	ldr	x0,[x0]
	adr	x1,chans
	ldr	x1,[x1]
	mov	x2,16
	mov	x3,0
	bl	ak_open
	tbnz	x0,63,.fail
	adr	x1,stream
	str	x0,[x1]
	mov	x19,0				; note
.note:	cmp	x19,8
	b.hs	.la
	adr	x0,freqs
	ldr	d0,[x0,x19,lsl 3]
	bl	frames_ms300
	bl	tone
	add	x19,x19,1
	b	.note
.la:	adr	x0,m_la
	bl	puts
	adr	x0,f440
	ldr	d0,[x0]
	adr	x0,rate
	ldr	x0,[x0]
	lsl	x0,x0,1				; 2 s
	bl	tone
	adr	x0,zero				; un peu de silence a la fin
	ldr	d0,[x0]
	adr	x0,rate
	ldr	x0,[x0]
	mov	x1,5
	udiv	x0,x0,x1			; 200 ms
	bl	tone
	adr	x0,stream			; attendre la fin de la lecture
	ldr	x0,[x0]
	bl	ak_drain
	sys	SYS_TIME_MS
	adr	x1,t_start
	ldr	x1,[x1]
	sub	x20,x0,x1			; duree (ms)
	adr	x0,stream
	ldr	x0,[x0]
	bl	ak_position
	mov	x21,x0				; trames jouees (44,1 kHz)
	adr	x0,stream
	ldr	x0,[x0]
	adr	x1,status
	bl	ak_status
	adr	x0,m_played
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_in
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_rate
	bl	puts
	mov	x0,1000				; cadence : trames * 1000 / ms
	mul	x0,x21,x0
	cmp	x20,0
	csinc	x1,x20,xzr,ne
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_expect
	bl	puts
	adr	x0,m_hw
	bl	puts
	adr	x0,status
	ldr	x0,[x0,16]
	bl	print_dec
	adr	x0,m_backend
	bl	puts
	bl	ak_backend
	bl	print_dec
	adr	x0,m_end
	bl	puts
	adr	x0,stream
	ldr	x0,[x0]
	bl	ak_close
	mov	x0,0
	sys	SYS_EXIT
.fail:	mov	x19,x0
	adr	x0,m_fail
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; --- faux pilote ------------------------------------------------------------
; x0 = arguments (« driver [freq] [s] »)
drv_mode:
	add	x0,x0,6
	bl	arg_num
	mov	x19,x0
	movl	x2,48000
	cmp	x19,0
	csel	x19,x2,x19,eq
	mov	x0,x1
	bl	arg_num
	mov	x20,x0
	mov	x2,6
	cmp	x20,0
	csel	x20,x2,x20,eq
	mov	x0,SND_OUT_USB
	mov	x1,x19
	mov	x2,1				; un canal DMA (pour l'essai)
	adr	x3,s_drvname
	bl	sndif_output
	tbnz	x0,63,.fail
	mov	x21,x0
	adr	x0,m_drv
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_drvdma
	bl	puts
	adr	x0,sif_out_dma
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_drvhz
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_hzn
	bl	puts
	sys	SYS_TIME_MS
	mov	x22,x0				; depart
	mov	x23,0				; trames dues (consommees)
	mov	x24,0				; trames lues de l'anneau
	mov	x25,0				; crete
	mov	x0,1000
	mul	x20,x20,x0			; duree (ms)
.l:	sys	SYS_TIME_MS
	sub	x0,x0,x22
	cmp	x0,x20
	b.hs	.done
	mul	x0,x0,x19
	mov	x1,1000
	udiv	x0,x0,x1
	sub	x26,x0,x23			; a consommer
	mov	x1,BLOCK
	cmp	x26,x1
	csel	x26,x26,x1,lo
	cbz	x26,.t
	add	x23,x23,x26
	adr	x0,pcm
	mov	x1,x26
	bl	sndif_read
	add	x24,x24,x0
	adr	x1,pcm				; crete
	lsl	x0,x0,1
.p:	cbz	x0,.t
	ldrsh	w2,[x1],2
	cmp	w2,0
	cneg	w2,w2,lt
	cmp	x2,x25
	csel	x25,x2,x25,hi
	sub	x0,x0,1
	b	.p
.t:	bl	sndif_tick
	mov	x0,5
	sys	SYS_SLEEP
	b	.l
.done:	adr	x0,m_drvend
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_drvsil
	bl	puts
	sub	x0,x23,x24
	bl	print_dec
	adr	x0,m_drvrate
	bl	puts
	mov	x0,1000
	mul	x0,x24,x0
	udiv	x0,x0,x20
	bl	print_dec
	adr	x0,m_drvpeak
	bl	puts
	mov	x0,x25
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.fail:	mov	x19,x0
	adr	x0,m_drvfail
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; --- faux micro --------------------------------------------------------------
; x0 = arguments (« mic [freq] [s] »)
mic_mode:
	add	x0,x0,3
	bl	arg_num
	mov	x19,x0
	movl	x2,48000
	cmp	x19,0
	csel	x19,x2,x19,eq
	mov	x0,x1
	bl	arg_num
	mov	x20,x0
	mov	x2,6
	cmp	x20,0
	csel	x20,x2,x20,eq
	mov	x0,SND_IN_USB
	mov	x1,x19
	mov	x2,0
	adr	x3,s_micname
	bl	sndif_input
	tbnz	x0,63,drv_mode.fail
	mov	x21,x0
	adr	x0,m_mic
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_drvhz
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_hzn
	bl	puts
	movl	x0,440				; pas de phase : 440 x 2^32 / freq
	lsl	x0,x0,32
	udiv	x27,x0,x19
	mov	w28,0				; phase
	sys	SYS_TIME_MS
	mov	x22,x0
	mov	x23,0				; trames dues
	mov	x24,0				; trames ecrites
	mov	x25,0				; ms ecoutees
	mov	x0,1000
	mul	x20,x20,x0
.l:	sys	SYS_TIME_MS
	sub	x0,x0,x22
	cmp	x0,x20
	b.hs	.done
	mul	x0,x0,x19
	mov	x1,1000
	udiv	x0,x0,x1
	sub	x26,x0,x23
	mov	x1,BLOCK
	cmp	x26,x1
	csel	x26,x26,x1,lo
	cbz	x26,.t
	add	x23,x23,x26
	adr	x0,pcm				; triangle de -8192 a 8191
	mov	x1,x26
.g:	lsr	w2,w28,16
	mov	w3,65535
	sub	w3,w3,w2
	cmp	w2,32768
	csel	w2,w2,w3,lo
	sub	w2,w2,16384
	asr	w2,w2,1
	strh	w2,[x0],2
	strh	w2,[x0],2
	add	w28,w28,w27
	subs	x1,x1,1
	b.ne	.g
	adr	x0,pcm
	mov	x1,x26
	bl	sndif_write
	add	x24,x24,x0
.t:	bl	sndif_wanted
	cbz	x0,.tk
	add	x25,x25,5
.tk:	bl	sndif_tick
	mov	x0,5
	sys	SYS_SLEEP
	b	.l
.done:	adr	x0,m_mic2
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_micw
	bl	puts
	mov	x0,x25
	bl	print_dec
	adr	x0,m_micms
	bl	puts
	mov	x0,0
	sys	SYS_EXIT

; x0 = texte -> x0 = nombre decimal qui suit les espaces (0 : aucun), x1 = suite
arg_num:
	mov	x2,0
.sp:	ldrb	w3,[x0]
	cmp	w3,' '
	b.ne	.d
	add	x0,x0,1
	b	.sp
.d:	ldrb	w3,[x0]
	sub	w3,w3,'0'
	cmp	w3,9
	b.hi	.e
	mov	x4,10
	madd	x2,x2,x4,x3
	add	x0,x0,1
	b	.d
.e:	mov	x1,x0
	mov	x0,x2
	ret

include '../../svc/inc/sndif.inc'

; -> x0 = trames de 300 ms (d0 garde)
frames_ms300:
	adr	x0,rate
	ldr	x0,[x0]
	mov	x1,3
	mul	x0,x0,x1
	mov	x1,10
	udiv	x0,x0,x1
	ret

; d0 = frequence (Hz, 0 = silence), x0 = trames
tone:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,two_pi_over_rate		; theta par echantillon
	ldr	d1,[x0]
	fmul	d0,d0,d1
	fmul	d1,d0,d0			; cos/sin de theta (Taylor)
	fmov	d2,1.0
	fmov	d3,0.5
	fmul	d4,d1,d3
	fsub	d2,d2,d4
	fmul	d4,d1,d1
	adr	x0,inv24
	ldr	d5,[x0]
	fmul	d4,d4,d5
	fadd	d2,d2,d4			; cos
	adr	x0,inv6
	ldr	d5,[x0]
	fmul	d4,d1,d5
	fmul	d4,d4,d0
	fsub	d3,d0,d4			; sin
	adr	x0,osc
	stp	d2,d3,[x0,16]
	fmov	d4,1.0
	str	d4,[x0]
	str	xzr,[x0,8]
.blk:	cbz	x19,.out
	mov	x20,BLOCK
	cmp	x19,x20
	csel	x20,x19,x20,lo
	sub	x19,x19,x20
	adr	x21,pcm				; generer x20 trames
	adr	x0,osc
	ldp	d0,d1,[x0]
	ldp	d2,d3,[x0,16]
	adr	x0,amp
	ldr	d6,[x0]
	adr	x0,chans
	ldr	x3,[x0]
	mov	x22,x20
.s:	fmul	d7,d0,d2
	fmul	d16,d1,d3
	fsub	d7,d7,d16
	fmul	d16,d0,d3
	fmul	d17,d1,d2
	fadd	d1,d16,d17
	fmov	d0,d7
	fmul	d7,d1,d6
	fcvtzs	w2,d7
	strh	w2,[x21],2
	cmp	x3,1
	b.eq	.m
	strh	w2,[x21],2
.m:	subs	x22,x22,1
	b.ne	.s
	fmul	d7,d0,d0			; renormaliser
	fmul	d16,d1,d1
	fadd	d7,d7,d16
	fsqrt	d7,d7
	fdiv	d0,d0,d7
	fdiv	d1,d1,d7
	adr	x0,osc
	stp	d0,d1,[x0]
	adr	x0,t_start			; debut de la lecture
	ldr	x1,[x0]
	cbnz	x1,.w
	sys	SYS_TIME_MS
	adr	x1,t_start
	str	x0,[x1]
.w:	adr	x0,chans			; ecrire (en attendant la place)
	ldr	x2,[x0]
	lsl	x2,x2,1
	mul	x2,x2,x20
	adr	x0,stream
	ldr	x0,[x0]
	adr	x1,pcm
	bl	ak_write_all
	b	.blk
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue


segment readable writeable
kit_data
m_start	db 'sontest : gamme do4-do5 puis la 440 Hz (2 s), ',0
m_hz	db ' Hz ',0
m_stereo db 'st',0xC3,0xA9,'r',0xC3,0xA9,'o',10,0
m_mono	db 'mono',10,0
m_la	db 'sontest : la 440 Hz',10,0
m_played db 'sontest : termin',0xC3,0xA9,', ',0
m_in	db ' trames jou',0xC3,0xA9,'es en ',0
m_rate	db ' ms, cadence ',0
m_expect db ' trames/s (attendu 44100)',10,0
m_hw	db 'sontest : sortie mat',0xC3,0xA9,'rielle = ',0
m_backend db ' (0 virtuelle, 1 PWM, 2 HDMI, 3 USB), chemin = ',0
m_end	db ' (1 service sound, 2 son du noyau)',10,0
m_fail	db 'sontest : pas de flux son : ',0
m_nl	db 10,0
s_drvname db 'Essai sontest',0
m_drv	db 'sontest driver : sortie ',0
m_drvdma db ', canal DMA ',0
m_drvhz	db ', ',0
m_hzn	db ' Hz',10,0
m_drvend db 'sontest driver : ',0
m_drvsil db ' trames lues, ',0
m_drvrate db ' en silence (manques), cadence ',0
m_drvpeak db ' trames/s, cr',0xC3,0xAA,'te ',0
s_micname db 'Essai micro',0
m_mic	db 'sontest mic : entr',0xC3,0xA9,'e ',0
m_mic2	db 'sontest mic : ',0
m_micw	db ' trames ',0xC3,0xA9,'crites, ',0xC3,0xA9,'cout',0xC3,0xA9,'e environ ',0
m_micms	db ' ms',10,0
m_drvfail db 'sontest driver : inscription refus',0xC3,0xA9,'e : ',0
	align 8
rate	dd SND_RATE
chans	dd 2
stream	dd 0
t_start	dd 0
status	dd 0, 0, 0
freqs	dd 261.6256, 293.6648, 329.6276, 349.2282, 391.9954, 440.0, 493.8833, 523.2511
f440	dd 440.0
zero	dd 0.0
two_pi	dd 6.283185307179586
two_pi_over_rate dd 0.000142475857
inv24	dd 0.0416666667
inv6	dd 0.1666666667
amp	dd 10000.0
osc	dd 1.0, 0.0, 1.0, 0.0
pcm	rb BLOCK*4
sndif_data
