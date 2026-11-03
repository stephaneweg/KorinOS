; usbaudio - pilote des cartes son et casques USB Audio Class 1 (UAC1),
; docs/SON.md 5. Lance par hwmanager pour l'interface AudioControl
; (drivers.txt : « usb:01/01 = SD0:/sys/svc/usbaudio », arguments
; « <peripherique> <interface> »). Il s'arrete de lui-meme quand la carte
; est retiree (usbd ferme ses tuyaux : UI_STATE = 1, ou E_NODEV).
;  * descripteurs (usbd, op 1) : interfaces AudioStreaming (classe 1,
;    sous-classe 2), format Type I PCM 16 bits, extremite isochrone ;
;    lecture = OUT stereo, capture = IN mono ou stereo ; frequence : 44,1 kHz
;    si la carte la propose, sinon 48 kHz, sinon la premiere ;
;  * lecture : SET_INTERFACE, SET_CUR (frequence), tuyau isochrone OUT
;    d'usbd (op 8) ; inscrit aupres de sound comme sortie « USB : <produit> »
;    a la frequence de la carte (sound reechantillonne) ; toutes les 2 ms,
;    l'anneau d'usbd est garde a ~24 ms d'avance depuis celui de sound ;
;  * capture : inscrite comme entree ; l'interface et le tuyau IN ne sont
;    ouverts qu'a la premiere capture qui ecoute (SR_WANTED), puis gardes ;
;    mono -> stereo, vers l'anneau d'entree de sound.
; Pas de retour de synchronisation, volume de la carte ignore (le volume est
; dans sound). Repris de l'ancien usb_audio.asm du noyau (branche usb-noyau).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/sound.inc'
include '../../inc/usb.inc'

UA_OUT_FOUND	= 0
UA_OUT_IF	= 4
UA_OUT_ALT	= 8
UA_OUT_EP	= 12
UA_OUT_MPS	= 16
UA_OUT_RATE	= 20
UA_CAP_FOUND	= 24
UA_CAP_IF	= 28
UA_CAP_ALT	= 32
UA_CAP_EP	= 36
UA_CAP_MPS	= 40
UA_CAP_RATE	= 44
UA_CAP_CH	= 48
UA_FMT_OK	= 52			; interface en cours d'analyse : format
UA_FMT_CH	= 56			;   valide, canaux, frequence choisie
UA_FMT_RATE	= 60
UA_CUR_IF	= 64
UA_CUR_ALT	= 68
UA_CUR_AS	= 72			; 1 : interface AudioStreaming
UA_SIZE		= 80
LEAD_MS		= 24			; avance de l'anneau isochrone
E_NODEV_	= -19

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0				; « peripherique interface »
	bl	kits_open
	mov	x0,0				; remplissage des trames : priorite haute
	mov	x1,THREAD_PRIO_HIGH
	sys	SYS_THREAD_SET_PRIORITY
	cbz	x19,quit
	mov	x0,x19
	bl	parse_dec
	adr	x1,dev
	str	x0,[x1]
	cbz	x0,quit
	adr	x0,s_usb			; le service usb (usbd)
	movl	x1,5000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	adr	x20,msg				; descripteur de configuration
	mov	x0,USB_OP_DESCRIPTEURS
	str	x0,[x20]
	adr	x0,dev
	ldr	x0,[x0]
	str	x0,[x20,WM_A]
	str	wzr,[x20,WM_LEN]
	bl	svc_call
	tbnz	x0,63,quit
	ldr	x2,[x20,WM_B]			; longueur
	cmp	x2,464
	mov	x1,464
	csel	x2,x2,x1,ls
	adr	x0,cfg
	add	x1,x20,WM_DATA
	str	x2,[sp,-16]!
	bl	mem_move
	ldr	x1,[sp],16
	ldr	x0,[x20,WM_C]			; vendeur, produit
	adr	x2,vidpid
	str	x0,[x2]
	bl	uac_parse
	adr	x19,ua
	ldr	w0,[x19,UA_OUT_FOUND]
	ldr	w1,[x19,UA_CAP_FOUND]
	orr	w0,w0,w1
	cbnz	w0,.found
	adr	x0,m_none
	bl	puts
	b	quit
.found:	bl	product_name			; « USB : <produit> »
	adr	x0,m_card
	bl	puts
	adr	x0,name
	bl	puts
	adr	x0,m_nl
	bl	puts
	ldr	w0,[x19,UA_OUT_FOUND]
	cbz	w0,.cap
	ldr	w1,[x19,UA_OUT_ALT]		; lecture : interface, frequence
	ldr	w2,[x19,UA_OUT_IF]
	bl	set_interface
	tbnz	x0,63,.gone
	ldr	w1,[x19,UA_OUT_EP]
	ldr	w2,[x19,UA_OUT_RATE]
	bl	set_rate			; (refus tolere : frequence unique)
	ldr	w0,[x19,UA_OUT_RATE]		; octets par ms (16.16) : rate x 4
	lsl	x0,x0,18
	mov	x1,1000
	udiv	x0,x0,x1
	mov	x1,4				; | octets par trame << 32
	orr	x3,x0,x1,lsl 32
	ldr	w1,[x19,UA_OUT_EP]
	ldr	w2,[x19,UA_OUT_MPS]
	bl	iso_open
	tbnz	x0,63,.noiso
	adr	x1,out_ring
	str	x0,[x1]
	mov	x0,SND_OUT_USB			; sortie de sound
	ldr	w1,[x19,UA_OUT_RATE]
	mov	x2,0
	adr	x3,name
	bl	sndif_output
	tbnz	x0,63,.nosnd
	adr	x0,m_out
	bl	puts
	ldr	w0,[x19,UA_OUT_RATE]
	bl	print_dec
	adr	x0,m_hz
	bl	puts
	ldr	w0,[x19,UA_OUT_RATE]		; avance en octets
	mov	x1,LEAD_MS*4
	mul	x0,x0,x1
	mov	x1,1000
	udiv	x0,x0,x1
	and	x0,x0,not 3
	adr	x1,lead
	str	x0,[x1]
.cap:	ldr	w0,[x19,UA_CAP_FOUND]
	cbz	w0,.loop
	mov	x0,SND_IN_USB			; entree de sound (micro de la carte)
	ldr	w1,[x19,UA_CAP_RATE]
	mov	x2,0
	adr	x3,name
	bl	sndif_input
	tbnz	x0,63,.loop
	adr	x0,m_cap
	bl	puts
	ldr	w0,[x19,UA_CAP_RATE]
	bl	print_dec
	adr	x0,m_hz
	bl	puts
.loop:	bl	feed_out
	tbnz	x0,63,.gone
	bl	feed_cap
	tbnz	x0,63,.gone
	bl	sndif_tick
	mov	x0,2
	sys	SYS_SLEEP
	b	.loop
.gone:	adr	x0,m_gone
	b	.leave
.noiso:	adr	x0,m_noiso
	b	.leave
.nosnd:	adr	x0,m_nosnd
.leave:	bl	puts
quit:	mov	x0,0				; (code 0 : pas de relance)
	sys	SYS_EXIT

; x20 = message (msg) -> x0 = resultat du service usb
svc_call:
	prologue
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x20
	sys	SYS_SVC_CALL
	epilogue

; x1 = SETUP, x2 = octets de donnees (dans msg + 48) -> x0 = resultat
usb_ctrl:
	prologue
	str	x20,[sp,-16]!
	adr	x20,msg
	mov	x0,USB_OP_CONTROLE
	str	x0,[x20]
	adr	x0,dev
	ldr	x0,[x0]
	stp	x0,x1,[x20,WM_A]
	str	x2,[x20,WM_C]
	str	wzr,[x20,WM_LEN]
	bl	svc_call
	ldr	x20,[sp],16
	epilogue

; w1 = reglage, w2 = interface -> x0 = resultat
set_interface:
	prologue
	mov	x3,0x01 or (USB_REQ_SET_INTERFACE shl 8)
	orr	x3,x3,x1,lsl 16
	orr	x1,x3,x2,lsl 32
	mov	x2,0
	bl	usb_ctrl
	epilogue

; w1 = extremite (bit 7 : IN), w2 = frequence : SET_CUR(SAMPLING_FREQ), 3 o
set_rate:
	prologue
	adr	x3,msg
	add	x3,x3,WM_DATA
	strb	w2,[x3]
	lsr	w4,w2,8
	strb	w4,[x3,1]
	lsr	w4,w2,16
	strb	w4,[x3,2]
	movl	x3,0x22 or (1 shl 8) or (0x0100 shl 16) or (3 shl 48)
	orr	x1,x3,x1,lsl 32
	mov	x2,3
	bl	usb_ctrl
	epilogue

; w1 = extremite, w2 = taille max., x3 = octets par ms | trame << 32
; -> x0 = anneau isochrone mappe, < 0 si refuse
iso_open:
	prologue
	str	x20,[sp,-16]!
	adr	x20,msg
	mov	x0,USB_OP_ISO_OPEN
	str	x0,[x20]
	adr	x0,dev
	ldr	x0,[x0]
	stp	x0,x1,[x20,WM_A]
	stp	x2,x3,[x20,WM_C]
	str	wzr,[x20,WM_LEN]
	bl	svc_call
	tbnz	x0,63,.out
	ldr	x0,[x20,WM_B]
	sys	SYS_BUF_MAP
.out:	ldr	x20,[sp],16
	epilogue

; Lecture : l'anneau isochrone garde « lead » octets d'avance (depuis sound)
; -> x0 = 0, < 0 si la carte est partie
feed_out:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,out_ring
	ldr	x19,[x0]
	cbz	x19,.ok
	ldr	x0,[x19,UI_STATE]
	cbnz	x0,.gone
	ldr	x20,[x19,UI_HEAD]
	add	x0,x19,UI_TAIL
	ldar	x0,[x0]
	sub	x0,x20,x0			; deja en avance
	adr	x1,lead
	ldr	x1,[x1]
	subs	x21,x1,x0
	b.le	.ok
	lsr	x21,x21,2			; trames a prendre
	cmp	x21,256
	mov	x0,256
	csel	x21,x21,x0,hi			; (pas moins de 256 d'un coup)
	cmp	x21,1024
	mov	x0,1024
	csel	x21,x21,x0,ls
	adr	x0,pcm
	mov	x1,x21
	bl	sndif_read			; (silence s'il manque des trames)
	adr	x1,pcm
	add	x2,x19,UI_DATA
	movl	x3,UI_BYTES-1
	mov	x4,0
.c:	cmp	x4,x21
	b.hs	.d
	ldr	w5,[x1,x4,lsl 2]
	and	x6,x20,x3
	str	w5,[x2,x6]
	add	x20,x20,4
	add	x4,x4,1
	b	.c
.d:	add	x0,x19,UI_HEAD
	stlr	x20,[x0]
.ok:	mov	x0,0
	b	.out
.gone:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Capture : ouverte a la premiere ecoute ; ce que la carte envoie -> sound
; -> x0 = 0, < 0 si la carte est partie
feed_cap:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,ua
	ldr	w0,[x19,UA_CAP_FOUND]
	cbz	w0,.ok
	adr	x0,cap_ring
	ldr	x20,[x0]
	cbnz	x20,.read
	bl	sndif_wanted			; personne n'ecoute : rien
	cbz	x0,.ok
	ldr	w1,[x19,UA_CAP_ALT]
	ldr	w2,[x19,UA_CAP_IF]
	bl	set_interface
	tbnz	x0,63,.gone
	ldr	w1,[x19,UA_CAP_EP]
	orr	w1,w1,0x80
	ldr	w2,[x19,UA_CAP_RATE]
	bl	set_rate
	ldr	w1,[x19,UA_CAP_EP]
	orr	w1,w1,0x80
	ldr	w2,[x19,UA_CAP_MPS]
	ldr	w3,[x19,UA_CAP_CH]		; octets par trame << 32
	lsl	x3,x3,33
	bl	iso_open
	tbnz	x0,63,.off
	adr	x1,cap_ring
	str	x0,[x1]
	adr	x0,m_capon
	bl	puts
	b	.ok
.off:	adr	x0,ua				; tuyau refuse : plus de capture
	str	wzr,[x0,UA_CAP_FOUND]
	b	.ok
.read:	ldr	x0,[x20,UI_STATE]
	cbnz	x0,.gone
	add	x0,x20,UI_HEAD
	ldar	x21,[x0]
	ldr	x22,[x20,UI_TAIL]
	sub	x0,x21,x22			; octets recus
	ldr	w1,[x19,UA_CAP_CH]
	lsl	x1,x1,1				; octets par trame
	udiv	x2,x0,x1			; trames
	cmp	x2,1024
	mov	x3,1024
	csel	x2,x2,x3,ls
	cbz	x2,.ok
	add	x3,x20,UI_DATA
	movl	x4,UI_BYTES-1
	adr	x5,pcm
	mov	x6,0
.f:	cmp	x6,x2
	b.hs	.put
	and	x7,x22,x4			; gauche (s16, octets un a un : l'anneau
	ldrb	w8,[x3,x7]			; n'est pas aligne sur 4)
	add	x22,x22,1
	and	x7,x22,x4
	ldrb	w9,[x3,x7]
	add	x22,x22,1
	orr	w8,w8,w9,lsl 8
	mov	w10,w8				; mono : droite = gauche
	cmp	w1,2
	b.eq	.st
	and	x7,x22,x4
	ldrb	w10,[x3,x7]
	add	x22,x22,1
	and	x7,x22,x4
	ldrb	w9,[x3,x7]
	add	x22,x22,1
	orr	w10,w10,w9,lsl 8
.st:	and	w8,w8,0xFFFF
	orr	w8,w8,w10,lsl 16
	str	w8,[x5,x6,lsl 2]
	add	x6,x6,1
	b	.f
.put:	add	x0,x20,UI_TAIL
	stlr	x22,[x0]
	adr	x0,pcm
	mov	x1,x2
	bl	sndif_write
.ok:	mov	x0,0
	b	.out
.gone:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Le nom de la carte : « USB : <iProduct> » (descripteur de chaine, UTF-16
; -> ASCII), « USB : carte son » a defaut
product_name:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,name
	adr	x1,s_prefix
	bl	str_cat
	mov	x19,x0				; fin du prefixe
	usb_setup x1, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE shl 8, 0, 18
	mov	x2,18
	bl	usb_ctrl
	cmp	x0,18
	b.lt	.def
	adr	x0,msg
	ldrb	w20,[x0,WM_DATA+15]		; iProduct
	cbz	w20,.def
	usb_setup x1, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DT_STRING shl 8, 0x0409, 255
	orr	x1,x1,x20,lsl 16		; (index de la chaine)
	mov	x2,255
	bl	usb_ctrl
	cmp	x0,4
	b.lt	.def
	adr	x1,msg
	add	x1,x1,WM_DATA
	ldrb	w2,[x1]				; bLength
	cmp	x2,x0
	csel	x2,x2,x0,ls
	mov	x3,2
	mov	x4,0
.c:	add	x5,x3,1
	cmp	x5,x2
	b.hs	.end
	cmp	x4,40
	b.hs	.end
	ldrh	w5,[x1,x3]
	cmp	w5,0x7F				; hors ASCII : « ? »
	mov	w6,'?'
	csel	w5,w5,w6,ls
	cmp	w5,' '
	csel	w5,w5,w6,hs
	strb	w5,[x19,x4]
	add	x4,x4,1
	add	x3,x3,2
	b	.c
.end:	strb	wzr,[x19,x4]
	cbnz	x4,.out
.def:	mov	x0,x19
	adr	x1,s_default
	bl	str_cat
.out:	ldp	x19,x20,[sp],16
	epilogue

; x1 = taille de la configuration (cfg) : interfaces de lecture et de capture
uac_parse:
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,ua
	adr	x20,cfg
	add	x21,x20,x1			; fin
.d:	cmp	x20,x21
	b.hs	.end
	ldrb	w2,[x20]
	cbz	w2,.end
	ldrb	w3,[x20,1]
	cmp	w3,4				; interface
	b.ne	.not_if
	str	wzr,[x19,UA_FMT_OK]
	str	wzr,[x19,UA_CUR_AS]
	ldrb	w4,[x20,2]
	str	w4,[x19,UA_CUR_IF]
	ldrb	w4,[x20,3]
	str	w4,[x19,UA_CUR_ALT]
	ldrb	w4,[x20,5]			; audio, AudioStreaming
	cmp	w4,1
	b.ne	.nx
	ldrb	w4,[x20,6]
	cmp	w4,2
	b.ne	.nx
	mov	w4,1
	str	w4,[x19,UA_CUR_AS]
	b	.nx
.not_if:
	cmp	w3,0x24				; descripteur d'interface de classe
	b.ne	.not_cs
	ldr	w4,[x19,UA_CUR_AS]
	cbz	w4,.nx
	ldrb	w4,[x20,2]
	cmp	w4,2				; FORMAT_TYPE
	b.ne	.nx
	ldrb	w4,[x20,3]
	cmp	w4,1				; type I
	b.ne	.nx
	ldrb	w5,[x20,4]			; canaux
	ldrb	w6,[x20,5]			; octets par sous-trame
	ldrb	w7,[x20,6]			; resolution
	cmp	w6,2
	b.ne	.nx
	cmp	w7,16
	b.ne	.nx
	cmp	w5,1
	b.lo	.nx
	cmp	w5,2
	b.hi	.nx
	str	w5,[x19,UA_FMT_CH]
	ldrb	w8,[x20,7]			; frequences (0 : plage)
	add	x9,x20,8
	cbz	w8,.range
	mov	w10,0				; 44,1 kHz vue ?
	mov	w11,0				; 48 kHz vue ?
	mov	w12,0				; premiere
.fq:	cbz	w8,.fq_end
	ldrb	w13,[x9]
	ldrb	w14,[x9,1]
	orr	w13,w13,w14,lsl 8
	ldrb	w14,[x9,2]
	orr	w13,w13,w14,lsl 16
	cbnz	w12,.seen
	mov	w12,w13
.seen:	movl	w14,44100
	cmp	w13,w14
	cset	w15,eq
	orr	w10,w10,w15
	movl	w14,48000
	cmp	w13,w14
	cset	w15,eq
	orr	w11,w11,w15
	add	x9,x9,3
	sub	w8,w8,1
	b	.fq
.fq_end:
	mov	w13,w12
	cbz	w11,.no48
	movl	w13,48000
.no48:	cbz	w10,.set
	movl	w13,44100
	b	.set
.range:	ldrb	w13,[x9]			; plage continue : 44,1 kHz si incluse
	ldrb	w14,[x9,1]
	orr	w13,w13,w14,lsl 8
	ldrb	w14,[x9,2]
	orr	w13,w13,w14,lsl 16
	movl	w14,44100
	cmp	w13,w14
	csel	w13,w14,w13,ls
.set:	str	w13,[x19,UA_FMT_RATE]
	mov	w4,1
	str	w4,[x19,UA_FMT_OK]
	b	.nx
.not_cs:
	cmp	w3,5				; extremite
	b.ne	.nx
	ldr	w4,[x19,UA_FMT_OK]
	cbz	w4,.nx
	ldrb	w4,[x20,3]
	and	w4,w4,3
	cmp	w4,1				; isochrone
	b.ne	.nx
	ldrb	w4,[x20,2]			; adresse
	ldrb	w5,[x20,4]
	ldrb	w6,[x20,5]
	orr	w5,w5,w6,lsl 8
	and	w5,w5,0x7FF			; taille max.
	tbnz	w4,7,.ep_in
	ldr	w6,[x19,UA_OUT_FOUND]		; lecture : stereo seulement
	cbnz	w6,.nx
	ldr	w6,[x19,UA_FMT_CH]
	cmp	w6,2
	b.ne	.nx
	and	w4,w4,0xF
	str	w4,[x19,UA_OUT_EP]
	str	w5,[x19,UA_OUT_MPS]
	ldr	w6,[x19,UA_CUR_IF]
	str	w6,[x19,UA_OUT_IF]
	ldr	w6,[x19,UA_CUR_ALT]
	str	w6,[x19,UA_OUT_ALT]
	ldr	w6,[x19,UA_FMT_RATE]
	str	w6,[x19,UA_OUT_RATE]
	mov	w6,1
	str	w6,[x19,UA_OUT_FOUND]
	b	.nx
.ep_in:	ldr	w6,[x19,UA_CAP_FOUND]
	cbnz	w6,.nx
	and	w4,w4,0xF
	str	w4,[x19,UA_CAP_EP]
	str	w5,[x19,UA_CAP_MPS]
	ldr	w6,[x19,UA_CUR_IF]
	str	w6,[x19,UA_CAP_IF]
	ldr	w6,[x19,UA_CUR_ALT]
	str	w6,[x19,UA_CAP_ALT]
	ldr	w6,[x19,UA_FMT_RATE]
	str	w6,[x19,UA_CAP_RATE]
	ldr	w6,[x19,UA_FMT_CH]
	str	w6,[x19,UA_CAP_CH]
	mov	w6,1
	str	w6,[x19,UA_CAP_FOUND]
.nx:	ldrb	w2,[x20]
	add	x20,x20,x2
	b	.d
.end:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ret

include '../inc/sndif.inc'

s_usb		db 'usb',0
s_prefix	db 'USB : ',0
s_default	db 'carte son',0
m_card		db 'usbaudio : ',0
m_none		db 'usbaudio : pas de flux PCM 16 bits (UAC1) : rien ',0xC3,0xA0,' faire',10,0
m_out		db 'usbaudio : lecture, ',0
m_cap		db 'usbaudio : micro, ',0
m_capon		db 'usbaudio : capture ouverte',10,0
m_hz		db ' Hz',10,0
m_gone		db 'usbaudio : carte retir',0xC3,0xA9,'e',10,0
m_noiso		db 'usbaudio : tuyau isochrone refus',0xC3,0xA9,' (usbd)',10,0
m_nosnd		db 'usbaudio : le service sound ne r',0xC3,0xA9,'pond pas',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 8
canal		dd 0
dev		dd 0
vidpid		dd 0
out_ring	dd 0				; anneaux isochrones d'usbd (0 : aucun)
cap_ring	dd 0
lead		dd 0				; avance de la lecture (octets)
ua		rb UA_SIZE
name		rb 64
sndif_data
		align 16
msg		rb 512
cfg		rb 512
pcm		rb 1024 * 4
