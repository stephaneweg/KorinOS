; ============================================================================
;  svc/inc/hid.inc - claviers et souris USB quelconques : lecture du descripteur
;  de rapport HID et decodage des rapports, pour les pilotes usbkeyboard et
;  usbmouse (repris de src/kernel/usb_hid.asm).
;
;  Reimplemente d'apres la specification USB HID 1.11 (items courts/longs,
;  globaux, locaux, principaux) et les tables d'usages (pages 01 bureau,
;  07 clavier, 09 boutons). hid_parse repere, pour une fonction HID :
;    clavier : octet des modificateurs (usages E0-E7), tableau de touches
;              (6 en general) ou table de bits (claviers « NKRO »)
;    souris  : boutons, X, Y (relatifs ou absolus : tablette, ecran), molette
;  avec leur numero de rapport (Report ID) quand le peripherique en utilise
;  (claviers composites : rapport 1 clavier, 2 souris, 3 multimedia...).
;  usb_report_parsed traduit chaque rapport recu : le clavier par
;  usb_report_kbd (format « boot » reconstitue), la souris en EV_MOUSE_*.
;  Les evenements partent par hid_pousser (x0 = type, x1..x3 = a, b, c),
;  fourni par le pilote ; mem_zero aussi (x0 = adresse, x1 = octets).
;
;	include 'hid.inc'		(constantes)
;	segment executable ... hid_code
;	segment writeable ...  hid_donnees
; ============================================================================

EV_KEY		= 1
EV_MOUSE_REL	= 2
EV_MOUSE_ABS	= 3
EV_WHEEL	= 4

HID_MAX_USAGES	= 16

; nature d'une fonction HID (HF_KIND)
HID_KBD		= 1
HID_MOUSE	= 2
HID_TABLET	= 3
HID_REPORT	= 4			; decode d'apres le descripteur de rapport

; --- fonction HID (une par interface HID) -----------------------------------
HF_ADDR		= 0			; adresse USB
HF_SPEED	= 4
HF_KIND		= 8
HF_EP		= 12
HF_MPS		= 16
HF_TOGGLE	= 20
HF_IFACE	= 24
HF_SUBCLASS	= 28
HF_PREV		= 32			; rapport precedent (8 octets)
HF_RDLEN	= 40			; longueur du descripteur de rapport
; champs trouves par hid_parse (usb_hid.asm) ; positions en bits, -1 : absent
HF_HAS		= 44			; bit 0 clavier, bit 1 souris
HF_IDS		= 48			; 1 : rapports numerotes (1er octet)
HF_K_ID		= 52
HF_K_MOD	= 56
HF_K_ARR	= 60
HF_K_ARRN	= 64
HF_K_ARRS	= 68
HF_K_BMP	= 72
HF_K_BMPMIN	= 76
HF_K_BMPN	= 80
HF_M_ID		= 84
HF_M_BTN	= 88
HF_M_BTNN	= 92
HF_M_X		= 96
HF_M_XS		= 100
HF_M_Y		= 104
HF_M_YS		= 108
HF_M_W		= 112
HF_M_WS		= 116
HF_M_ABS	= 120
HF_M_XMAX	= 124
HF_M_YMAX	= 128
HF_MPS0		= 132			; paquet de controle de l'appareil (EP0)
HF_SIZE		= 144


HP_PAGE		= 0
HP_RSIZE	= 4
HP_RCOUNT	= 8
HP_RID		= 12
HP_NUSAGE	= 16
HP_HAVEMIN	= 20
HP_UMIN		= 24
HP_UMAX		= 28
HP_LMIN		= 32
HP_LMAX		= 40
HP_USAGES	= 48			; HID_MAX_USAGES x 4
HP_OFFS		= 48 + HID_MAX_USAGES*4	; position par numero de rapport (16 x 4)
HP_SIZE		= HP_OFFS + 64

macro hid_code {
; ---------------------------------------------------------------------------
;  Analyse du descripteur
; ---------------------------------------------------------------------------

; x0 = fonction HID, x1 = descripteur de rapport, x2 = longueur
; -> HF_HAS (bit 0 clavier, bit 1 souris) et les champs HF_K_* / HF_M_*
hid_parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	add	x21,x1,x2
	mov	w0,-1				; aucun champ connu
	str	w0,[x19,HF_K_MOD]
	str	w0,[x19,HF_K_ARR]
	str	w0,[x19,HF_K_BMP]
	str	w0,[x19,HF_M_BTN]
	str	w0,[x19,HF_M_X]
	str	w0,[x19,HF_M_Y]
	str	w0,[x19,HF_M_W]
	str	wzr,[x19,HF_HAS]
	str	wzr,[x19,HF_IDS]
	adr	x0,hp_state			; etat de l'analyse a zero
	mov	x1,HP_SIZE
	bl	mem_zero
	adr	x22,hp_state
.item:	cmp	x20,x21
	b.hs	.end
	ldrb	w23,[x20],1			; prefixe
	cmp	w23,0xFE			; item long : on le saute
	b.ne	.short
	ldrb	w0,[x20]
	add	x20,x20,x0
	add	x20,x20,2
	b	.item
.short:	and	w24,w23,3			; taille des donnees : 0, 1, 2, 4
	cmp	w24,3
	b.ne	.sz
	mov	w24,4
.sz:	mov	x25,0				; donnees (non signees)
	mov	x26,0				; donnees (signees)
	cbz	w24,.typ
	ldrb	w0,[x20]
	mov	x25,x0
	sxtb	x26,w0
	cmp	w24,1
	b.eq	.adv
	ldrh	w0,[x20]
	mov	x25,x0
	sxth	x26,w0
	cmp	w24,2
	b.eq	.adv
	ldr	w0,[x20]
	mov	x25,x0
	sxtw	x26,w0
.adv:	add	x20,x20,x24
.typ:	ubfx	w0,w23,2,2			; type
	lsr	w1,w23,4			; etiquette
	cmp	w0,1
	b.eq	.global
	cmp	w0,2
	b.eq	.local
	cbnz	w0,.item			; (reserve)
	; --- principal ---------------------------------------------------------------
	cmp	w1,8				; Input
	b.ne	.clear_locals
	mov	x0,x19
	mov	x1,x25
	bl	hid_input_item
.clear_locals:
	str	wzr,[x22,HP_NUSAGE]
	str	wzr,[x22,HP_HAVEMIN]
	b	.item
.global:
	cbnz	w1,.g1
	str	w25,[x22,HP_PAGE]		; Usage Page
	b	.item
.g1:	cmp	w1,1
	b.ne	.g2
	str	x26,[x22,HP_LMIN]		; Logical Minimum (signe)
	b	.item
.g2:	cmp	w1,2
	b.ne	.g7
	ldr	x0,[x22,HP_LMIN]		; Logical Maximum : signe si le minimum l'est
	tbnz	x0,63,.g2s
	str	x25,[x22,HP_LMAX]
	b	.item
.g2s:	str	x26,[x22,HP_LMAX]
	b	.item
.g7:	cmp	w1,7
	b.ne	.g8
	str	w25,[x22,HP_RSIZE]		; Report Size
	b	.item
.g8:	cmp	w1,8
	b.ne	.g9
	and	w0,w25,0xFF			; Report ID
	str	w0,[x22,HP_RID]
	mov	w0,1
	str	w0,[x19,HF_IDS]
	b	.item
.g9:	cmp	w1,9
	b.ne	.item
	str	w25,[x22,HP_RCOUNT]		; Report Count
	b	.item
.local:	ldr	w2,[x22,HP_PAGE]		; usage complet : page << 16 | usage
	cmp	w24,4
	b.eq	.full
	and	w25,w25,0xFFFF
	orr	w25,w25,w2,lsl 16
.full:	cbnz	w1,.l1
	ldr	w0,[x22,HP_NUSAGE]		; Usage
	cmp	w0,HID_MAX_USAGES
	b.hs	.item
	add	x2,x22,HP_USAGES
	str	w25,[x2,x0,lsl 2]
	add	w0,w0,1
	str	w0,[x22,HP_NUSAGE]
	b	.item
.l1:	cmp	w1,1
	b.ne	.l2
	str	w25,[x22,HP_UMIN]		; Usage Minimum
	mov	w0,1
	str	w0,[x22,HP_HAVEMIN]
	b	.item
.l2:	cmp	w1,2
	b.ne	.item
	str	w25,[x22,HP_UMAX]		; Usage Maximum
	b	.item
.end:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fonction, x1 = drapeaux de l'item Input (bit 0 constante, bit 1 variable,
; bit 2 relatif) : note les champs utiles, avance la position du rapport
hid_input_item:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x22,hp_state
	ldr	w0,[x22,HP_RID]			; position (bits) dans ce rapport
	and	w0,w0,15
	add	x1,x22,HP_OFFS
	ldr	w21,[x1,x0,lsl 2]
	ldr	w23,[x22,HP_RSIZE]
	ldr	w24,[x22,HP_RCOUNT]
	tbnz	w20,0,.advance			; remplissage
	; page du premier usage (a defaut : la page globale)
	ldr	w0,[x22,HP_HAVEMIN]
	cbz	w0,.u0
	ldr	w2,[x22,HP_UMIN]
	b	.pg
.u0:	ldr	w0,[x22,HP_NUSAGE]
	ldr	w2,[x22,HP_PAGE]
	lsl	w2,w2,16
	cbz	w0,.pg
	ldr	w2,[x22,HP_USAGES]
.pg:	lsr	w3,w2,16			; page
	and	w2,w2,0xFFFF			; premier usage
	ldr	w4,[x22,HP_RID]
	tbnz	w20,1,.var
	; --- tableau : touches du clavier -------------------------------------------
	cmp	w3,7
	b.ne	.advance
	ldr	w0,[x19,HF_K_ARR]
	tbz	w0,31,.advance			; deja trouve
	str	w21,[x19,HF_K_ARR]
	str	w24,[x19,HF_K_ARRN]
	str	w23,[x19,HF_K_ARRS]
	str	w4,[x19,HF_K_ID]
	b	.kbd
.var:	cmp	w3,7
	b.ne	.btn
	cmp	w23,1				; bits de clavier
	b.ne	.advance
	cmp	w2,0xE0
	b.ne	.bitmap
	ldr	w0,[x19,HF_K_MOD]		; modificateurs E0-E7
	tbz	w0,31,.advance
	str	w21,[x19,HF_K_MOD]
	str	w4,[x19,HF_K_ID]
	b	.kbd
.bitmap:
	ldr	w0,[x19,HF_K_BMP]		; une touche par bit
	tbz	w0,31,.advance
	str	w21,[x19,HF_K_BMP]
	str	w2,[x19,HF_K_BMPMIN]
	str	w24,[x19,HF_K_BMPN]
	str	w4,[x19,HF_K_ID]
.kbd:	ldr	w0,[x19,HF_HAS]
	orr	w0,w0,1
	str	w0,[x19,HF_HAS]
	b	.advance
.btn:	cmp	w3,9				; boutons
	b.ne	.desk
	ldr	w0,[x19,HF_M_BTN]
	tbz	w0,31,.advance
	cmp	w23,1
	b.ne	.advance
	str	w21,[x19,HF_M_BTN]
	str	w24,[x19,HF_M_BTNN]
	b	.advance
.desk:	cmp	w3,1				; bureau : X, Y, molette
	b.ne	.advance
	mov	x5,0				; index dans l'item
.u:	cmp	w5,w24
	b.hs	.advance
	ldr	w0,[x22,HP_HAVEMIN]		; usage de ce champ
	cbz	w0,.ulist
	ldr	w6,[x22,HP_UMIN]
	add	w6,w6,w5
	b	.uhave
.ulist:	ldr	w0,[x22,HP_NUSAGE]
	cbz	w0,.advance
	sub	w0,w0,1
	cmp	w5,w0
	csel	w0,w5,w0,lo
	add	x6,x22,HP_USAGES
	ldr	w6,[x6,x0,lsl 2]
.uhave:	and	w6,w6,0xFFFF
	madd	w7,w5,w23,w21			; position du champ
	cmp	w6,0x30
	b.ne	.uy
	ldr	w0,[x19,HF_M_X]
	tbz	w0,31,.unext
	str	w7,[x19,HF_M_X]
	str	w23,[x19,HF_M_XS]
	ubfx	w0,w20,2,1			; relatif ?
	eor	w0,w0,1
	str	w0,[x19,HF_M_ABS]
	ldr	x0,[x22,HP_LMAX]
	str	w0,[x19,HF_M_XMAX]
	str	w4,[x19,HF_M_ID]
	ldr	w0,[x19,HF_HAS]
	orr	w0,w0,2
	str	w0,[x19,HF_HAS]
	b	.unext
.uy:	cmp	w6,0x31
	b.ne	.uw
	ldr	w0,[x19,HF_M_Y]
	tbz	w0,31,.unext
	str	w7,[x19,HF_M_Y]
	str	w23,[x19,HF_M_YS]
	ldr	x0,[x22,HP_LMAX]
	str	w0,[x19,HF_M_YMAX]
	b	.unext
.uw:	cmp	w6,0x38
	b.ne	.unext
	ldr	w0,[x19,HF_M_W]
	tbz	w0,31,.unext
	str	w7,[x19,HF_M_W]
	str	w23,[x19,HF_M_WS]
.unext:	add	w5,w5,1
	b	.u
.advance:
	mul	w0,w23,w24			; taille x nombre
	add	w21,w21,w0
	ldr	w0,[x22,HP_RID]
	and	w0,w0,15
	add	x1,x22,HP_OFFS
	str	w21,[x1,x0,lsl 2]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Decodage d'un rapport
; ---------------------------------------------------------------------------

; x0 = donnees, x1 = position (bits), x2 = taille (1-32) -> x0 = valeur
hid_bits:
	mov	x3,0				; valeur
	mov	x4,0				; bit ecrit
.b:	cmp	x4,x2
	b.hs	.end
	lsr	x5,x1,3
	ldrb	w5,[x0,x5]
	and	x6,x1,7
	lsrv	x5,x5,x6
	and	x5,x5,1
	lslv	x5,x5,x4
	orr	x3,x3,x5
	add	x1,x1,1
	add	x4,x4,1
	b	.b
.end:	mov	x0,x3
	ret

; idem, valeur signee (extension du bit de poids fort)
hid_sbits:
	str	x2,[sp,-16]!
	str	x30,[sp,8]
	bl	hid_bits
	ldr	x2,[sp]
	ldr	x30,[sp,8]
	add	sp,sp,16
	cbz	x2,.r
	sub	x2,x2,1
	lsrv	x1,x0,x2
	tbz	x1,0,.r
	mov	x1,-1				; negatif : bits hauts a 1
	lslv	x1,x1,x2
	orr	x0,x0,x1
.r:	ret

; x0 = fonction, x1 = octets recus, x2 = rapport : clavier et/ou souris
usb_report_parsed:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x2				; donnees
	mov	w21,0				; numero de rapport
	ldr	w0,[x19,HF_IDS]
	cbz	w0,.kbd
	ldrb	w21,[x20],1
	; --- clavier : rapport Â« boot Â» reconstitue dans hid_boot -------------------------
.kbd:	ldr	w0,[x19,HF_HAS]
	tbz	w0,0,.mouse
	ldr	w0,[x19,HF_K_ID]
	cmp	w0,w21
	b.ne	.mouse
	adr	x22,hid_boot
	str	xzr,[x22]
	ldr	w1,[x19,HF_K_MOD]
	tbnz	w1,31,.keys
	mov	x0,x20
	mov	x2,8
	bl	hid_bits
	strb	w0,[x22]
.keys:	mov	x23,2				; prochain octet de touche (2..7)
	ldr	w1,[x19,HF_K_ARR]
	tbnz	w1,31,.bmp
	mov	x24,0				; tableau
.arr:	ldr	w0,[x19,HF_K_ARRN]
	cmp	w24,w0
	b.hs	.send
	cmp	x23,8
	b.hs	.send
	ldr	w1,[x19,HF_K_ARR]
	ldr	w2,[x19,HF_K_ARRS]
	madd	w1,w24,w2,w1
	mov	x0,x20
	bl	hid_bits
	cbz	x0,.arr_next
	strb	w0,[x22,x23]
	add	x23,x23,1
.arr_next:
	add	x24,x24,1
	b	.arr
.bmp:	ldr	w1,[x19,HF_K_BMP]
	tbnz	w1,31,.send
	mov	x24,0
.bit:	ldr	w0,[x19,HF_K_BMPN]
	cmp	w24,w0
	b.hs	.send
	cmp	x23,8
	b.hs	.send
	ldr	w1,[x19,HF_K_BMP]
	add	w1,w1,w24
	mov	x0,x20
	mov	x2,1
	bl	hid_bits
	cbz	x0,.bit_next
	ldr	w0,[x19,HF_K_BMPMIN]
	add	w0,w0,w24
	strb	w0,[x22,x23]
	add	x23,x23,1
.bit_next:
	add	x24,x24,1
	b	.bit
.send:	mov	x0,x19
	mov	x1,x22
	bl	usb_report_kbd
	; --- souris -------------------------------------------------------------------------
.mouse:	ldr	w0,[x19,HF_HAS]
	tbz	w0,1,.out
	ldr	w0,[x19,HF_M_ID]
	cmp	w0,w21
	b.ne	.out
	mov	x24,0				; boutons
	ldr	w1,[x19,HF_M_BTN]
	tbnz	w1,31,.xy
	ldr	w2,[x19,HF_M_BTNN]
	cmp	w2,8
	mov	w3,8
	csel	w2,w2,w3,ls
	mov	x0,x20
	bl	hid_bits
	mov	x24,x0
.xy:	ldr	w1,[x19,HF_M_X]
	ldr	w2,[x19,HF_M_XS]
	mov	x0,x20
	ldr	w3,[x19,HF_M_ABS]
	cbnz	w3,.ax
	bl	hid_sbits
	b	.gx
.ax:	bl	hid_bits
.gx:	mov	x22,x0
	mov	x23,0
	ldr	w1,[x19,HF_M_Y]
	tbnz	w1,31,.push
	ldr	w2,[x19,HF_M_YS]
	mov	x0,x20
	ldr	w3,[x19,HF_M_ABS]
	cbnz	w3,.ay
	bl	hid_sbits
	b	.gy
.ay:	bl	hid_bits
.gy:	mov	x23,x0
.push:	ldr	w0,[x19,HF_M_ABS]
	cbnz	w0,.abs
	mov	x0,EV_MOUSE_REL
	mov	x1,x22
	mov	x2,x23
	mov	x3,x24
	bl	hid_pousser
	b	.wheel
.abs:	movl	x2,32767			; 0..maximum logique -> 0..32767
	ldr	w3,[x19,HF_M_XMAX]
	cbz	w3,.ay2
	mul	x22,x22,x2
	udiv	x22,x22,x3
.ay2:	ldr	w3,[x19,HF_M_YMAX]
	cbz	w3,.apush
	mul	x23,x23,x2
	udiv	x23,x23,x3
.apush:	mov	x0,EV_MOUSE_ABS
	mov	x1,x22
	mov	x2,x23
	mov	x3,x24
	bl	hid_pousser
.wheel:	ldr	w1,[x19,HF_M_W]
	tbnz	w1,31,.out
	ldr	w2,[x19,HF_M_WS]
	mov	x0,x20
	bl	hid_sbits
	cbz	x0,.out
	mov	x1,x0
	mov	x0,EV_WHEEL
	mov	x2,0
	mov	x3,0
	bl	hid_pousser
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Rapports au format « boot » (claviers, souris) et tablette de QEMU
; ---------------------------------------------------------------------------

; x0 = fonction, x1 = rapport clavier boot (mods, 0, 6 touches) -> EV_KEY
usb_report_kbd:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1				; nouveau rapport
	add	x21,x19,HF_PREV			; ancien rapport
	ldrb	w0,[x20,2]
	cmp	w0,1				; ErrorRollOver : on ignore le rapport
	b.eq	.end
	ldrb	w22,[x20]			; modificateurs
	ldrb	w0,[x21]
	eor	w23,w22,w0			; bits changes
	mov	w24,0
.mod:	lsrv	w0,w23,w24
	tbz	w0,0,.mod_next
	mov	x0,EV_KEY
	add	w1,w24,0xE0			; usages E0..E7
	lsrv	w2,w22,w24
	and	w2,w2,1
	mov	w3,w22
	bl	hid_pousser
.mod_next:
	add	w24,w24,1
	cmp	w24,8
	b.lo	.mod

	mov	x24,2				; touches relachees : dans l'ancien, pas le nouveau
.rel:	ldrb	w1,[x21,x24]
	cmp	w1,4
	b.lo	.rel_next
	mov	x0,x20
	bl	usb_key_in_report
	cbnz	x0,.rel_next
	ldrb	w1,[x21,x24]
	mov	x0,EV_KEY
	mov	x2,0
	mov	w3,w22
	bl	hid_pousser
.rel_next:
	add	x24,x24,1
	cmp	x24,8
	b.lo	.rel

	mov	x24,2				; touches enfoncees : dans le nouveau seulement
.prs:	ldrb	w1,[x20,x24]
	cmp	w1,4
	b.lo	.prs_next
	mov	x0,x21
	bl	usb_key_in_report
	cbnz	x0,.prs_next
	ldrb	w1,[x20,x24]
	mov	x0,EV_KEY
	mov	x2,1
	mov	w3,w22
	bl	hid_pousser
.prs_next:
	add	x24,x24,1
	cmp	x24,8
	b.lo	.prs

	ldr	x0,[x20]			; memoriser le rapport
	str	x0,[x21]
.end:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = rapport, w1 = code -> x0 = 1 si le code figure dans les octets 2..7
usb_key_in_report:
	mov	x2,2
.k:	ldrb	w3,[x0,x2]
	cmp	w3,w1
	b.eq	.yes
	add	x2,x2,1
	cmp	x2,8
	b.lo	.k
	mov	x0,0
	ret
.yes:	mov	x0,1
	ret

; x0 = fonction, x1 = rapport souris boot (boutons, dx, dy[, molette]) -> EV_MOUSE_REL
usb_report_mouse:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	mov	x0,EV_MOUSE_REL
	ldrsb	x1,[x19,1]
	ldrsb	x2,[x19,2]
	ldrb	w3,[x19]
	bl	hid_pousser
	ldrsb	x1,[x19,3]
	cbz	x1,.end
	mov	x0,EV_WHEEL
	mov	x2,0
	mov	x3,0
	bl	hid_pousser
.end:	ldr	x19,[sp],16
	epilogue

; x0 = fonction, x1 = rapport tablette QEMU (boutons, x16, y16, molette) -> EV_MOUSE_ABS
usb_report_tablet:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	mov	x0,EV_MOUSE_ABS
	ldrh	w1,[x19,1]
	ldrh	w2,[x19,3]
	ldrb	w3,[x19]
	bl	hid_pousser
	ldrsb	x1,[x19,5]
	cbz	x1,.end
	mov	x0,EV_WHEEL
	mov	x2,0
	mov	x3,0
	bl	hid_pousser
.end:	ldr	x19,[sp],16
	epilogue
}

macro hid_donnees {
		align 16
hp_state	rb HP_SIZE
hid_boot	rb 8
		align 8
}
