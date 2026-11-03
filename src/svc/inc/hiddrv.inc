; ============================================================================
;  svc/inc/hiddrv.inc - corps commun des pilotes HID USB (usbkeyboard,
;  usbmouse ; docs/USB.md). Lance par hwmanager avec « <peripherique>
;  <interface> » : lit les descripteurs (usbd), le descripteur de rapport
;  (hid.inc), regle l'interface (SET_PROTOCOL boot a defaut, SET_IDLE),
;  ouvre son extremite d'interruption et traduit chaque rapport en
;  evenements, deposes dans l'anneau que HIDManager lui donne (op 3).
;  Une interface composite (clavier + souris + multimedia, rapports
;  numerotes) est traduite en entier, quel que soit le pilote.
;
;	PILOTE_NOM equ 'usbkeyboard'	(avant l'include)
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/usb.inc'
include 'hid.inc'
if defined PILOTE_PAD
include '../../inc/pad.inc'
end if

HID_OP_PILOTE	= 3
HID_PAD		= 5			; (HF_KIND) manette : hidpad.inc
ANNEAU_EV	= 64
ANNEAU_N	= 64
E_NODEV		= -19
E_INVAL		= -22

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0				; « <peripherique> <interface> »
	bl	kits_open
	cbz	x19,quit
	mov	x0,x19
	bl	parse_dec
	adr	x1,dev
	str	x0,[x1]
.esp:	ldrb	w0,[x19],1			; l'interface, apres l'espace
	cbz	w0,quit
	cmp	w0,' '
	b.ne	.esp
	mov	x0,x19
	bl	parse_dec
	adr	x1,iface
	str	x0,[x1]
	adr	x0,s_usb			; usbd
	mov	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	bl	decrire				; descripteurs -> hf
	cbz	x0,.inutile
	bl	regler				; rapport, protocole, repos
	ldr	w0,[x20,HF_KIND]
	cbz	w0,.inutile
	adr	x0,s_hid			; l'anneau vers HIDManager
	mov	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,msg
	mov	x2,HID_OP_PILOTE
	str	x2,[x1]
	sys	SYS_SVC_CALL
	tbnz	x0,63,quit
	sys	SYS_BUF_MAP
	tbnz	x0,63,quit
	adr	x1,anneau
	str	x0,[x1]
	adr	x1,msg				; l'extremite d'interruption
	mov	x2,USB_OP_INTR_OUVRIR
	str	x2,[x1]
	adr	x2,dev
	ldr	x2,[x2]
	str	x2,[x1,8]
	ldr	w2,[x20,HF_EP]
	orr	x2,x2,0x80
	str	x2,[x1,16]
	ldr	w2,[x20,HF_MPS]
	str	x2,[x1,24]
	adr	x2,intervalle
	ldr	x2,[x2]
	str	x2,[x1,32]
	adr	x0,canal
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	tbnz	x0,63,quit
	adr	x1,tuyau
	str	x0,[x1]
	bl	annoncer
.lire:	adr	x1,msg				; un rapport (reponse differee)
	mov	x2,USB_OP_INTR_LIRE
	str	x2,[x1]
	adr	x2,tuyau
	ldr	x2,[x2]
	str	x2,[x1,8]
	adr	x0,canal
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	cmp	x0,E_NODEV			; peripherique retire, ou tuyau perdu
	b.eq	.fin				; (usbd relance) : fini
	cmp	x0,E_INVAL
	b.eq	.fin
	cmp	x0,0
	b.le	.lire				; (attente trop longue : on redemande)
	adr	x2,msg+48
	ldr	w1,[x20,HF_KIND]
	mov	x0,x20
	cmp	w1,HID_REPORT
	b.eq	.parsed
if defined PILOTE_PAD
	cmp	w1,HID_PAD
	b.ne	.npad
	bl	pad_report
	b	.lire
.npad:
end if
	mov	x1,x2
	cmp	w1,HID_KBD
	b.eq	.kbd
	cmp	w1,HID_MOUSE
	b.eq	.mouse
	bl	usb_report_tablet
	b	.lire
.parsed:
	bl	usb_report_parsed
	b	.lire
.kbd:	bl	usb_report_kbd
	b	.lire
.mouse:	bl	usb_report_mouse
	b	.lire
.inutile:
	adr	x0,m_inutile
	bl	puts
.fin:	mov	x0,0				; (pas de relance)
	sys	SYS_EXIT
quit:	mov	x0,1
	sys	SYS_EXIT

; descripteurs de usbd -> hf (interface, extremite IN d'interruption,
; longueur du descripteur de rapport), intervalle ; x20 = hf
; -> x0 = 1 si l'interface a son extremite
decrire:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x20,hf
	mov	x0,x20
	mov	x1,HF_SIZE
	bl	mem_zero
	adr	x1,msg
	mov	x2,USB_OP_DESCRIPTEURS
	str	x2,[x1]
	adr	x2,dev
	ldr	x2,[x2]
	str	x2,[x1,8]
	adr	x0,canal
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.non
	adr	x1,msg
	ldr	x2,[x1,32]			; vitesse
	adr	x3,vitesse
	str	x2,[x3]
	ldr	x2,[x1,16]			; longueur
	add	x21,x1,48
	add	x22,x21,x2
	mov	w23,0				; 1 : dans notre interface
	adr	x0,iface
	ldr	x24,[x0]
.d:	add	x0,x21,2
	cmp	x0,x22
	b.hi	.non
	ldrb	w0,[x21]
	cmp	w0,2
	b.lo	.non
	ldrb	w1,[x21,1]
	cmp	w1,USB_DT_INTERFACE
	b.eq	.itf
	cbz	w23,.n
	cmp	w1,USB_DT_HID			; longueur du descripteur de rapport
	b.eq	.hid
	cmp	w1,USB_DT_ENDPOINT
	b.eq	.ep
	b	.n
.itf:	mov	w23,0
	ldrb	w2,[x21,2]			; bInterfaceNumber, reglage 0
	cmp	x2,x24
	b.ne	.n
	ldrb	w2,[x21,3]
	cbnz	w2,.n
	mov	w23,1
	str	w24,[x20,HF_IFACE]
	ldrb	w2,[x21,6]			; sous-classe, protocole
	str	w2,[x20,HF_SUBCLASS]
	ldrb	w2,[x21,7]
	adr	x3,protocole
	str	x2,[x3]
	b	.n
.hid:	ldrb	w2,[x21,7]
	ldrb	w3,[x21,8]
	orr	w2,w2,w3,lsl 8
	str	w2,[x20,HF_RDLEN]
	b	.n
.ep:	ldrb	w2,[x21,2]			; IN, interruption
	tbz	w2,7,.n
	ldrb	w3,[x21,3]
	and	w3,w3,3
	cmp	w3,3
	b.ne	.n
	and	w2,w2,0xF
	str	w2,[x20,HF_EP]
	ldrb	w2,[x21,4]
	ldrb	w3,[x21,5]
	orr	w2,w2,w3,lsl 8
	and	w2,w2,0x7FF
	cmp	w2,64
	mov	w3,64
	csel	w2,w2,w3,ls
	str	w2,[x20,HF_MPS]
	ldrb	w2,[x21,6]			; bInterval
	adr	x3,vitesse			; haute vitesse : 2^(n-1) micro-trames
	ldr	x3,[x3]
	cmp	x3,USB_HS
	b.lo	.ms
	sub	w2,w2,1
	mov	w4,1
	lslv	w2,w4,w2
	lsr	w2,w2,3
.ms:	cmp	w2,1
	mov	w4,1
	csel	w2,w2,w4,hs
	adr	x3,intervalle
	str	x2,[x3]
	mov	x0,1
	b	.out
.n:	ldrb	w0,[x21]
	add	x21,x21,x0
	b	.d
.non:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = SETUP, x1 = longueur -> x0 = octets (donnees en msg+48), < 0 si erreur
controle:
	prologue
	adr	x2,msg
	mov	x3,USB_OP_CONTROLE
	str	x3,[x2]
	adr	x3,dev
	ldr	x3,[x3]
	stp	x3,x0,[x2,8]
	str	x1,[x2,24]
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x2
	sys	SYS_SVC_CALL
	epilogue

; descripteur de rapport (hid_parse), sinon protocole « boot » ; SET_IDLE(0)
; x20 = hf ; HF_KIND : 0 si inutilisable
regler:
	prologue
	stp	x21,x22,[sp,-16]!
	ldr	w21,[x20,HF_IFACE]
	ldr	w1,[x20,HF_RDLEN]
	cbz	w1,.boot
	mov	w0,464
	cmp	w1,w0
	csel	w1,w1,w0,ls
	str	w1,[x20,HF_RDLEN]
	usb_setup x0, 0x81, USB_REQ_GET_DESCRIPTOR, USB_DT_REPORT shl 8, 0, 0
	orr	x0,x0,x21,lsl 32		; destinataire : l'interface
	orr	x0,x0,x1,lsl 48
	bl	controle
	cmp	x0,2
	b.lt	.boot
	mov	x2,x0
if defined PILOTE_PAD
	stp	x2,xzr,[sp,-16]!
	adr	x1,msg+48
	bl	pad_parse
	ldp	x2,x3,[sp],16
	cbz	x0,.npad
	mov	w0,HID_PAD
	str	w0,[x20,HF_KIND]
	bl	pad_vidpid
	b	.idle
.npad:
end if
	mov	x0,x20
	adr	x1,msg+48
	bl	hid_parse
	ldr	w0,[x20,HF_HAS]
	cbz	w0,.boot
	mov	w0,HID_REPORT			; decode d'apres le descripteur
	str	w0,[x20,HF_KIND]
	b	.idle
.boot:	adr	x0,protocole			; protocole boot : 1 clavier, 2 souris,
	ldr	x22,[x0]			; sinon pointeur absolu (tablette QEMU)
	mov	w0,HID_TABLET
	cmp	x22,1
	mov	w1,HID_KBD
	csel	w0,w1,w0,eq
	cmp	x22,2
	mov	w1,HID_MOUSE
	csel	w0,w1,w0,eq
	str	w0,[x20,HF_KIND]
	ldr	w0,[x20,HF_SUBCLASS]
	cmp	w0,1
	b.ne	.idle
	usb_setup x0, 0x21, HID_REQ_SET_PROTOCOL, 0, 0, 0	; SET_PROTOCOL(boot)
	orr	x0,x0,x21,lsl 32
	mov	x1,0
	bl	controle
.idle:	usb_setup x0, 0x21, HID_REQ_SET_IDLE, 0, 0, 0	; rapport sur changement
	orr	x0,x0,x21,lsl 32
	mov	x1,0
	bl	controle
	ldp	x21,x22,[sp],16
	epilogue

; « usbkeyboard : peripherique 1 interface 0 : clavier »
if defined PILOTE_PAD
; le descripteur de l'appareil -> pd_vidpid (le mapping du modele, chez HIDManager)
pad_vidpid:
	prologue
	usb_setup x0, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE shl 8, 0, 18
	mov	x1,18
	bl	controle
	cmp	x0,12
	b.lt	.out
	adr	x1,msg+48
	ldrh	w2,[x1,8]
	ldrh	w3,[x1,10]
	orr	w2,w3,w2,lsl 16
	adr	x1,pd_vidpid
	str	w2,[x1]
.out:	epilogue
include 'hidpad.inc'
end if

annoncer:
	prologue
	adr	x0,m_nom
	bl	puts
	adr	x0,dev
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_itf
	bl	puts
	adr	x0,iface
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_2p
	bl	puts
	ldr	w0,[x20,HF_KIND]
if defined PILOTE_PAD
	cmp	w0,HID_PAD
	b.ne	.npad
	adr	x0,s_pad
	b	.dit
.npad:
end if
	cmp	w0,HID_REPORT
	b.ne	.genre
	ldr	w0,[x20,HF_HAS]
	adr	x1,s_has
	add	x0,x1,x0,lsl 4
	b	.dit
.genre:	adr	x1,s_genres
	add	x0,x1,x0,lsl 4
.dit:	bl	puts
	adr	x0,m_nl
	bl	puts
	epilogue

; x0 = type, x1 = a, x2 = b, x3 = c : un evenement dans l'anneau de
; HIDManager (perdu s'il est plein)
hid_pousser:
	adr	x9,anneau
	ldr	x9,[x9]
	ldp	x10,x11,[x9]			; tete (ici), queue (HIDManager)
	sub	x12,x10,x11
	cmp	x12,ANNEAU_N
	b.hs	.plein
	and	x12,x10,ANNEAU_N-1
	add	x12,x9,x12,lsl 5
	add	x12,x12,ANNEAU_EV
	stp	x0,x1,[x12]
	stp	x2,x3,[x12,16]
	dmb	ish				; l'evenement avant la tete
	add	x10,x10,1
	str	x10,[x9]
.plein:	ret

; x0 = adresse, x1 = octets
mem_zero:
	cbz	x1,.f
	strb	wzr,[x0],1
	sub	x1,x1,1
	b	mem_zero
.f:	ret

hid_code

s_usb		db 'usb',0
s_hid		db 'hid',0
m_nom		db PILOTE_NOM,' : peripherique ',0
m_itf		db ' interface ',0
m_2p		db ' : ',0
m_nl		db 10,0
m_inutile	db PILOTE_NOM,' : interface sans rapport utilisable',10,0
		align 4
s_genres	db 'inutilisable',0,0,0,0
		db 'clavier (boot)',0,0
		db 'souris (boot)',0,0,0
		db 'tablette',0,0,0,0,0,0,0,0
s_has		db '?',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
		db 'clavier',0,0,0,0,0,0,0,0,0
		db 'souris',0,0,0,0,0,0,0,0,0,0
		db 'clavier, souris',0
		align 8

if defined PILOTE_PAD
s_pad		db 'manette',0
		align 8
end if

segment readable writeable
kit_data
hid_donnees
		align 8
canal		dd -1
dev		dd 0
iface		dd 0
vitesse		dd 0
protocole	dd 0
intervalle	dd 8
tuyau		dd 0
anneau		dd 0
hf		rb HF_SIZE
msg		rb 512

if defined PILOTE_PAD
		align 8
pd_btn_pos	rb 4
pd_btn_n	rb 4
pd_hat_pos	rb 4
pd_hat_min	rb 4
pd_vidpid	rb 4
pd_id		rb 4
pd_ax		rb PD_AX_MAX*PA_LEN
end if
