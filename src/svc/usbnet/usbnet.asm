; usbnet - carte reseau USB CDC-ECM (classe 02h, sous-classe 06h), pilote
; lance par hwmanager (drivers.txt : usb:02/06) avec "<peripherique>
; <interface>" (docs/RESEAU.md etape 1, docs/USB.md). Repris de
; src/kernel/arch/pi4/usb_ecm.asm (historique git), d'apres CDC 1.2 / ECM 1.2 :
;  * configuration : interface de donnees (classe 0Ah) dans son reglage a deux
;    extremites bulk (SET_INTERFACE), filtre de paquets, MAC par la chaine
;    iMACAddress (12 chiffres hexadecimaux) ;
;  * reception : lecture bulk IN sans attente de usbd (op BULK_NB : le canal
;    reste arme, un NAK rend 0) ; emission : op BULK (et paquet vide quand
;    la trame est un multiple de la taille de paquet) ;
;  * interface "usb0" de netd (priorite 10), anneau de trames (netif.inc).
; Teste avec QEMU -device usb-net (configuration 1 = ECM).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/usb.inc'
include '../../inc/netd.inc'

TAMPON		= 8192			; tampon partage avec usbd : RX +0, TX +4096
RX_MAX		= 2048			; > 1536 : la trame arrive d'un bloc
TX_OFF		= 4096
E_NODEV		= -19
E_BUSY		= -16

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0				; "<peripherique> <interface>"
	bl	kits_open
	cbz	x19,quit
	mov	x0,x19
	bl	parse_dec
	adr	x1,dev
	str	x0,[x1]
	adr	x0,s_usb			; usbd
	mov	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	bl	decrire				; ECM ? extremites, chaine de la MAC
	cbz	x0,.inutile
	bl	configurer			; SET_INTERFACE, filtre, MAC
	adr	x1,msg				; le tampon partage
	mov	x2,USB_OP_BULK_TAMPON
	str	x2,[x1]
	mov	x2,TAMPON
	str	x2,[x1,8]
	bl	usb_call
	tbnz	x0,63,quit
	sys	SYS_BUF_MAP
	tbnz	x0,63,quit
	adr	x1,tampon
	str	x0,[x1]
	adr	x0,m_pret			; "usbnet : ECM pret, MAC ..."
	bl	puts
	bl	print_mac
	mov	x0,NIF_USB			; interface usb0 de netd
	adr	x1,mac
	ldr	x1,[x1]
	mov	x2,NIF_PRIO_USB
	adr	x3,s_usb0
	bl	netif_register
	tbnz	x0,63,.sans_netd
	mov	x0,1				; lien (QEMU : toujours)
	mov	x1,100
	mov	x2,0
	bl	netif_link
.loop:	mov	x20,0				; travail fait dans ce tour
	mov	x21,8				; au plus 8 trames recues par tour
.rx:	bl	lire
	cmp	x0,0
	b.le	.rx_fin
	mov	x1,x0
	adr	x0,tampon
	ldr	x0,[x0]
	bl	netif_rx_put
	add	x20,x20,1
	subs	x21,x21,1
	b.ne	.rx
	b	.tx
.rx_fin:
	cmn	x0,19				; E_NODEV : la carte est partie
	b.eq	.parti
	cmn	x0,38				; E_NOSYS : controleur sans lecture
	b.eq	.xhci				; sans attente (xHCI, pas encore)
.tx:	adr	x0,tampon			; trames a emettre
	ldr	x0,[x0]
	add	x0,x0,TX_OFF
	bl	netif_tx_get
	cbz	x0,.tick
	bl	ecrire
	cmn	x0,19
	b.eq	.parti
	add	x20,x20,1
	b	.tx
.tick:	bl	netif_tick
	cbnz	x20,.loop
	mov	x0,1
	sys	SYS_SLEEP
	b	.loop
.xhci:	adr	x0,m_xhci
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.parti:	adr	x0,m_parti
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.sans_netd:
	adr	x0,m_sans_netd
	bl	puts
	b	quit
.inutile:
	adr	x0,m_inutile
	bl	puts
	mov	x0,0				; (pas de relance)
	sys	SYS_EXIT
quit:	mov	x0,1
	sys	SYS_EXIT

; msg -> x0 = resultat de usbd
usb_call:
	adr	x0,canal
	ldr	x0,[x0]
	adr	x1,msg
	sys	SYS_SVC_CALL
	ret

; x0 = paquet SETUP (sans donnees en sortie) -> x0 = octets ou < 0 ;
; donnees recues dans msg + 48
controle:
	prologue
	adr	x1,msg
	mov	x2,USB_OP_CONTROLE
	str	x2,[x1]
	adr	x2,dev
	ldr	x2,[x2]
	stp	x2,x0,[x1,8]			; a = peripherique, b = SETUP
	lsr	x0,x0,48			; c = longueur
	str	x0,[x1,24]
	str	wzr,[x1,40]
	bl	usb_call
	epilogue

; -> x0 = 1 si le peripherique a une configuration ECM (choisie au besoin :
; QEMU usb-net presente d'abord RNDIS, 02/02/FF, puis ECM)
decrire:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x1,msg				; la configuration choisie par usbd
	mov	x2,USB_OP_DESCRIPTEURS
	str	x2,[x1]
	adr	x2,dev
	ldr	x2,[x2]
	str	x2,[x1,8]
	bl	usb_call
	tbnz	x0,63,.non
	adr	x0,msg
	ldr	x1,[x0,16]			; longueur
	bl	analyser
	cbnz	x0,.oui
	movl	x0,0x80 or (6 shl 8) or (0x0100 shl 16) or (18 shl 48)
	bl	controle			; descripteur de peripherique
	cmp	x0,18
	b.lt	.non
	adr	x0,msg
	ldrb	w20,[x0,48+17]			; bNumConfigurations
	mov	x19,0
.cfg:	cmp	x19,x20
	b.hs	.non
	movl	x0,0x80 or (6 shl 8) or (0x0200 shl 16) or (464 shl 48)
	orr	x0,x0,x19,lsl 16
	bl	controle
	cmp	x0,9
	b.lt	.suiv
	mov	x1,x0
	bl	analyser
	cbnz	x0,.choisir
.suiv:	add	x19,x19,1
	b	.cfg
.choisir:
	adr	x0,msg				; SET_CONFIGURATION(bConfigurationValue)
	ldrb	w0,[x0,48+5]
	lsl	x0,x0,16
	mov	x1,9 shl 8
	orr	x0,x0,x1
	bl	controle
	tbnz	x0,63,.non
	adr	x0,m_config
	bl	puts
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x1 = longueur du descripteur de configuration en msg + 48
; -> x0 = 1 s'il contient une interface ECM et une interface de donnees a
; deux extremites bulk
analyser:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x12,in_ep
	str	xzr,[x12]
	adr	x12,out_ep
	str	xzr,[x12]
	adr	x12,mac_str
	str	wzr,[x12]
	adr	x0,msg
	cmp	x1,464
	mov	x2,464
	csel	x1,x1,x2,ls
	add	x2,x0,48			; descripteur
	add	x3,x2,x1			; fin
	mov	w5,0				; 1 : dans l'interface de controle ECM
	mov	w6,0				; 1 : dans une interface de donnees
	mov	w7,0				; ECM trouve
.d:	add	x4,x2,2
	cmp	x4,x3
	b.hi	.fin
	ldrb	w8,[x2]
	cbz	w8,.fin
	ldrb	w9,[x2,1]
	cmp	w9,4				; interface
	b.ne	.pas_if
	mov	w5,0
	mov	w6,0
	ldrb	w10,[x2,5]			; classe
	ldrb	w11,[x2,6]			; sous-classe
	cmp	w10,2
	b.ne	.donnees
	cmp	w11,6
	b.ne	.nx
	mov	w5,1
	mov	w7,1
	ldrb	w10,[x2,2]
	adr	x12,ctrl_if
	str	w10,[x12]
	b	.nx
.donnees:
	cmp	w10,0x0A
	b.ne	.nx
	ldrb	w10,[x2,4]			; bNumEndpoints
	cbz	w10,.nx
	mov	w6,1
	ldrb	w10,[x2,2]
	adr	x12,data_if
	str	w10,[x12]
	ldrb	w10,[x2,3]
	adr	x12,data_alt
	str	w10,[x12]
	b	.nx
.pas_if:
	cmp	w9,0x24				; descripteur fonctionnel CDC
	b.ne	.pas_fn
	cbz	w5,.nx
	ldrb	w10,[x2,2]
	cmp	w10,0x0F			; Ethernet Networking
	b.ne	.nx
	ldrb	w10,[x2,3]
	adr	x12,mac_str
	str	w10,[x12]
	b	.nx
.pas_fn:
	cmp	w9,5				; extremite
	b.ne	.nx
	cbz	w6,.nx
	ldrb	w10,[x2,3]
	and	w10,w10,3
	cmp	w10,2				; bulk
	b.ne	.nx
	ldrb	w10,[x2,2]
	ldrb	w11,[x2,4]
	ldrb	w12,[x2,5]
	orr	w11,w11,w12,lsl 8
	and	w11,w11,0x7FF
	tbz	w10,7,.ep_out
	adr	x12,in_ep
	stp	w10,w11,[x12]			; in_ep, in_mps
	b	.nx
.ep_out:
	adr	x12,out_ep
	stp	w10,w11,[x12]
.nx:	ldrb	w8,[x2]
	add	x2,x2,x8
	b	.d
.fin:	cbz	w7,.non
	adr	x0,in_ep
	ldr	w0,[x0]
	cbz	w0,.non
	adr	x0,out_ep
	ldr	w0,[x0]
	cbz	w0,.non
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; SET_INTERFACE(donnees, reglage), SET_ETHERNET_PACKET_FILTER, MAC
configurer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,data_alt			; SET_INTERFACE
	ldr	w0,[x0]
	lsl	x0,x0,16
	mov	x1,0x01 or (11 shl 8)
	orr	x0,x0,x1
	adr	x1,data_if
	ldr	w1,[x1]
	orr	x0,x0,x1,lsl 32
	bl	controle
	adr	x1,ctrl_if			; filtre : dirige, diffusion,
	ldr	w1,[x1]				; multidiffusion (echec tolere)
	movl	x0,0x21 or (0x43 shl 8) or (0x0E shl 16)
	orr	x0,x0,x1,lsl 32
	bl	controle
	adr	x0,mac_str			; chaine iMACAddress
	ldr	w2,[x0]
	cbz	w2,.defaut
	movl	x0,0x80 or (6 shl 8) or (0x0300 shl 16) or (0x0409 shl 32) or (64 shl 48)
	orr	x0,x0,x2,lsl 16
	bl	controle
	cmp	x0,26
	b.lt	.defaut
	adr	x20,msg
	add	x20,x20,48+2			; caracteres UTF-16
	adr	x21,mac
	mov	x22,0
.hex:	ldrh	w0,[x20],2
	bl	hex_digit
	tbnz	x0,63,.defaut
	mov	x19,x0
	ldrh	w0,[x20],2
	bl	hex_digit
	tbnz	x0,63,.defaut
	orr	x0,x0,x19,lsl 4
	strb	w0,[x21,x22]
	add	x22,x22,1
	cmp	x22,6
	b.lo	.hex
	b	.out
.defaut:
	adr	x21,mac				; adresse locale administree
	movl	x0,0x00015042			; 42:50:01:00:...
	str	x0,[x21]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = caractere -> x0 = 0..15, -1 si pas hexadecimal
hex_digit:
	sub	w1,w0,'0'
	cmp	w1,9
	b.ls	.ok
	orr	w0,w0,0x20
	sub	w1,w0,'a'
	cmp	w1,5
	b.hi	.no
	add	w1,w1,10
.ok:	mov	x0,x1
	ret
.no:	mov	x0,-1
	ret

; -> x0 = octets d'une trame recue (au debut du tampon), 0 : rien, < 0
lire:
	prologue
	adr	x1,msg
	mov	x2,USB_OP_BULK_NB
	str	x2,[x1]
	adr	x2,dev
	ldr	x2,[x2]
	adr	x3,in_ep
	ldp	w3,w4,[x3]			; extremite IN, taille max.
	orr	w3,w3,0x80
	stp	x2,x3,[x1,8]
	stp	x4,xzr,[x1,24]			; c = taille max., d = decalage 0
	mov	x2,RX_MAX
	str	x2,[x1,48]
	bl	usb_call
	cmn	x0,16				; E_BUSY (une autre carte) : rien
	b.ne	.out
	mov	x0,0
.out:	epilogue

; x0 = octets a emettre (en TX_OFF du tampon) -> x0 = resultat
ecrire:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	bulk_out
	tbnz	x0,63,.out
	adr	x1,out_ep			; multiple de la taille de paquet :
	ldr	w1,[x1,4]			; paquet vide (fin de trame)
	cbz	w1,.out
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19
	cbnz	x2,.out
	mov	x0,0
	bl	bulk_out
.out:	ldr	x19,[sp],16
	epilogue

; x0 = octets (en TX_OFF) -> x0
bulk_out:
	prologue
	adr	x1,msg
	mov	x2,USB_OP_BULK
	str	x2,[x1]
	str	x0,[x1,48]
	adr	x2,dev
	ldr	x2,[x2]
	adr	x3,out_ep
	ldp	w3,w4,[x3]
	stp	x2,x3,[x1,8]
	mov	x5,TX_OFF
	stp	x4,x5,[x1,24]
	bl	usb_call
	epilogue

print_mac:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,mac
	mov	x20,0
.m:	ldrb	w0,[x19,x20]
	bl	print_hex2
	add	x20,x20,1
	cmp	x20,6
	b.hs	.f
	adr	x0,m_2p
	bl	puts
	b	.m
.f:	adr	x0,m_nl
	bl	puts
	ldp	x19,x20,[sp],16
	epilogue

; w0 = octet -> deux chiffres hexadecimaux
print_hex2:
	prologue
	sub	sp,sp,16
	and	w1,w0,15
	lsr	w0,w0,4
	and	w0,w0,15
	cmp	w0,10
	add	w2,w0,'0'
	add	w3,w0,'a'-10
	csel	w0,w2,w3,lo
	strb	w0,[sp]
	cmp	w1,10
	add	w2,w1,'0'
	add	w3,w1,'a'-10
	csel	w1,w2,w3,lo
	strb	w1,[sp,1]
	strb	wzr,[sp,2]
	mov	x0,sp
	bl	puts
	add	sp,sp,16
	epilogue

include '../inc/netif.inc'

s_usb		db 'usb',0
s_usb0		db 'usb0',0
m_pret		db 'usbnet : ECM pret, MAC ',0
m_parti		db 'usbnet : carte retiree',10,0
m_xhci		db 'usbnet : port USB-A (xHCI) pas encore pris en charge : brancher sur l',0x27,'USB-C',10,0
m_sans_netd	db 'usbnet : netd ne repond pas',10,0
m_inutile	db 'usbnet : pas une interface ECM',10,0
m_config	db 'usbnet : configuration ECM choisie',10,0
m_2p		db ':',0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 8
canal		dd 0
dev		dd 0
tampon		dd 0
mac		dd 0
ctrl_if		dw 0
data_if		dw 0
data_alt	dw 0
mac_str		dw 0
in_ep		dw 0, 0				; extremite, taille max.
out_ep		dw 0, 0
netif_data
msg		rb 512
