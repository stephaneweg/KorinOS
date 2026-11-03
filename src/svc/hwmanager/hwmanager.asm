; hwmanager - decouverte du materiel (docs/FEUILLE-DE-ROUTE.md, section 1),
; lance par init2. Il detecte le materiel et lance le pilote adequat (un
; service de SD0:/sys/svc) d'apres la table SD0:/etc/drivers.txt
; (« detection = pilote [arguments] ») :
;  * bus I2C 1 (non enumerable) : il sonde les adresses de la table
;    (« i2c1:5d = SD0:/sys/svc/gt911 ») toutes les 2 s pendant une minute (un
;    peripherique peut demarrer apres le systeme) ; des qu'un pilote du bus
;    est lance, il ne touche plus au bus (le pilote le pilote seul) ;
;  * USB : usbd lui annonce chaque interface (op HW_OP_USB) ; il cherche,
;    de la plus precise a la moins precise, les cles « usb:VVVV:PPPP »
;    (vendeur:produit), « usb:cc/ss/pp », « usb:cc/ss », « usb:cc » (classe,
;    sous-classe, protocole) et lance le pilote avec « <peripherique>
;    <interface> » ;
;  * les peripheriques du SoC d'apres la carte (soc:genet, soc:wifi) ;
;  * le son HDMI (docs/SON.md 5) : sur un Pi 4, l'EDID de l'ecran (lu par le
;    firmware) dit s'il prend le son (extension CEA : audio de base, ou un
;    bloc Audio Data) ; si oui, le pilote de « hdmi0:audio » est lance. Toutes
;    les 2 s, l'etat « hot plug » de HDMI0 (registre HOTPLUG, bit 0) : ecran
;    debranche -> le pilote est arrete (kill) ; rebranche -> EDID relu.
;    Option no_hdmiaudio (SD0:/etc/options.txt) : rien.
; Il s'enregistre sous « hwmanager » (les annonces).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../inc/i2c.inc'				; GPIO, I2C 1 en logiciel
include '../../inc/usb.inc'

DET_MAX		= 8			; lignes de la table
DET_ADR		= 0			; adresse I2C (0 : place libre)
DET_FAIT	= 8			; 1 : pilote lance
DET_PILOTE	= 16			; « chemin [arguments] »
DET_TAILLE	= 16 + 112
SONDES		= 30			; essais (toutes les 2 s)
NOMS_MAX	= 16			; pilotes USB retenus (notifications)
USBT_MAX	= 16			; lignes « usb:... »
UT_CLE		= 0			; ce qui suit « usb: » (16 o)
UT_PILOTE	= 16
UT_TAILLE	= 16 + 112
SOC_MAX		= 4			; lignes "soc:..." (genet, wifi)
SC_PILOTE	= 16			; cle (16 o), puis le pilote
SC_PID		= 128			; pid du pilote lance (0 : aucun)
SC_ESSAIS	= 136			; relances
SC_TAILLE	= 144

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,p_drivers			; la table
	adr	x1,table_ligne
	bl	ini_read
	adr	x0,s_nom
	mov	x1,SVF_FINS			; (pilotes du SoC a relancer)
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	mov	x19,x0
	adr	x0,m_pret
	bl	puts
	bl	soc_start			; pilotes du SoC d'apres la carte
	mov	x21,0				; I2C 1 : 0 a ouvrir, 1 ouvert, 2 fini
	mov	x22,SONDES
.loop:	cmp	x21,2				; detection sur l'I2C 1
	b.eq	.attente
	cbnz	x21,.sonde
	adr	x0,det_n
	ldr	x0,[x0]
	cbz	x0,.fini			; (rien a chercher sur ce bus)
	bl	i2c_ouvrir
	tbnz	x0,63,.fini
	mov	x21,1
.sonde:	bl	sonder_i2c
	cbnz	x0,.fini			; un pilote a le bus
	subs	x22,x22,1
	b.eq	.fini
	mov	x2,2000
	b	.next
.fini:	mov	x21,2
.attente:
	movl	x2,60000
	adr	x0,hdmi_on			; (son HDMI surveille : toutes les 2 s)
	ldr	x0,[x0]
	cbz	x0,.next
	mov	x2,2000
.next:	mov	x0,x19				; annonces (a venir) : rien encore
	adr	x1,msg
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	str	x0,[sp,-16]!
	bl	hdmi_poll
	ldr	x0,[sp],16
	cbz	x0,.loop
	mov	x20,x0
	adr	x1,msg
	ldr	x0,[x1]
	movl	x2,SVC_OP_FIN			; (avis) un process s'est arrete
	cmp	x0,x2
	b.ne	.req
	ldr	x0,[x1,8]
	ldr	x1,[x1,16]
	bl	soc_fin
	b	.loop
.req:	cmp	x0,HW_OP_USB_RETRAIT
	b.eq	.retrait
	cmp	x0,HW_OP_USB
	mov	x0,E_INVAL
	b.ne	.rep
	bl	annonce_usb
	b	.rep
.retrait:
	adr	x0,m_usb			; (le pilote s'arrete de lui-meme)
	bl	puts
	adr	x0,msg
	ldr	x0,[x0,8]
	bl	print_dec
	adr	x0,m_retire
	bl	puts
	adr	x0,msg
	ldr	x0,[x0,8]
	bl	noter_retire
	mov	x0,0
.rep:	adr	x1,msg
	str	x0,[x1]
	mov	x0,x20
	sys	SYS_SVC_REPLY
	b	.loop
quit:	mov	x0,1				; (sur une erreur : init2 relance)
	sys	SYS_EXIT

; (ini_read de drivers.txt) x0 = cle, x1 = valeur :
; « i2c1:<adresse en hexa> = chemin [arguments] » -> une ligne de la table
table_ligne:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x19				; « hdmi0:audio = pilote »
	adr	x1,k_hdmi
	bl	str_eq
	cbz	x0,.socs
	adr	x0,hdmi_line
	mov	x1,x20
	mov	x2,128
	bl	str_copy_n
	b	.out
.socs:	adr	x1,k_soc			; "soc:genet", "soc:wifi"
	mov	x2,0
.sc:	ldrb	w3,[x1,x2]
	cbz	w3,.soc
	ldrb	w4,[x19,x2]
	cmp	w3,w4
	b.ne	.pas_soc
	add	x2,x2,1
	b	.sc
.soc:	add	x19,x19,x2
	adr	x2,soc_n
	ldr	x3,[x2]
	cmp	x3,SOC_MAX
	b.hs	.out
	add	x4,x3,1
	str	x4,[x2]
	adr	x1,soct
	mov	x2,SC_TAILLE
	madd	x0,x3,x2,x1
	str	x0,[sp,-16]!
	mov	x1,x19				; la cle
	mov	x2,SC_PILOTE
	bl	str_copy_n
	ldr	x0,[sp],16
	add	x0,x0,SC_PILOTE			; le pilote
	mov	x1,x20
	mov	x2,SC_PID - SC_PILOTE
	bl	str_copy_n
	b	.out
.pas_soc:
	adr	x1,k_usb			; « usb:... »
	mov	x2,0
.u:	ldrb	w3,[x1,x2]
	cbz	w3,.usb
	ldrb	w4,[x19,x2]
	cmp	w3,w4
	b.ne	.i2c
	add	x2,x2,1
	b	.u
.usb:	add	x19,x19,x2
	adr	x2,usbt_n
	ldr	x3,[x2]
	cmp	x3,USBT_MAX
	b.hs	.out
	add	x4,x3,1
	str	x4,[x2]
	adr	x1,usbt
	mov	x2,UT_TAILLE
	madd	x0,x3,x2,x1
	str	x0,[sp,-16]!
	mov	x1,x19				; la cle
	mov	x2,UT_PILOTE
	bl	str_copy_n
	ldr	x0,[sp],16
	add	x0,x0,UT_PILOTE			; le pilote
	mov	x1,x20
	mov	x2,UT_TAILLE - UT_PILOTE
	bl	str_copy_n
	b	.out
.i2c:
	adr	x1,k_i2c1			; « i2c1: »
	mov	x2,0
.p:	ldrb	w3,[x1,x2]
	cbz	w3,.adr
	ldrb	w4,[x19,x2]
	cmp	w3,w4
	b.ne	.out
	add	x2,x2,1
	b	.p
.adr:	add	x19,x19,x2			; adresse en hexa
	mov	x0,0
.h:	ldrb	w1,[x19],1
	cbz	w1,.hfin
	sub	w2,w1,'0'
	cmp	w2,9
	b.ls	.hc
	sub	w2,w1,'a'
	cmp	w2,5
	b.hi	.out
	add	w2,w2,10
.hc:	add	x0,x2,x0,lsl 4
	b	.h
.hfin:	cbz	x0,.out
	cmp	x0,0x7F
	b.hi	.out
	adr	x2,det_n
	ldr	x3,[x2]
	cmp	x3,DET_MAX
	b.hs	.out
	add	x4,x3,1
	str	x4,[x2]
	adr	x1,table
	mov	x2,DET_TAILLE
	madd	x19,x3,x2,x1
	str	x0,[x19,DET_ADR]
	str	xzr,[x19,DET_FAIT]
	add	x0,x19,DET_PILOTE
	mov	x1,x20
	mov	x2,DET_TAILLE - DET_PILOTE
	bl	str_copy_n
.out:	ldp	x19,x20,[sp],16
	epilogue

; sonde les adresses I2C de la table ; lance le pilote de la premiere qui
; repond -> x0 = 1 si un pilote a ete lance (le bus est a lui)
sonder_i2c:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,table
	adr	x0,det_n
	ldr	x20,[x0]
.s:	cbz	x20,.rien
	ldr	x0,[x19,DET_ADR]
	bl	i2c_present
	cbnz	x0,.trouve
	add	x19,x19,DET_TAILLE
	sub	x20,x20,1
	b	.s
.trouve:
	adr	x0,m_i2c			; « hwmanager : i2c1 0x5D -> »
	bl	puts
	ldr	x0,[x19,DET_ADR]
	bl	print_hex
	adr	x0,m_fleche
	bl	puts
	add	x0,x19,DET_PILOTE
	bl	lancer
	mov	x0,1
	b	.out
.rien:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; (msg) annonce d'usbd : a = peripherique, b = interface, c = classe/
; sous-classe/protocole, d = vendeur/produit -> le pilote est lance ; x0 = 0
annonce_usb:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,msg
	adr	x0,m_usb			; « hwmanager : usb 1.0 (03/01/01) -> »
	bl	puts
	ldr	x0,[x19,8]
	bl	print_dec
	adr	x0,m_point
	bl	puts
	ldr	x0,[x19,16]
	bl	print_dec
	adr	x0,m_par
	bl	puts
	ldr	x0,[x19,24]			; cles : vendeur:produit, puis classe
	ldr	x1,[x19,32]
	bl	cles_usb
	adr	x0,cle_cc_ss_pp
	bl	puts
	adr	x0,m_parf
	bl	puts
	adr	x20,cles			; de la plus precise a la moins precise
.c:	ldr	x0,[x20],8
	cbz	x0,.aucun
	bl	chercher_usb
	cbz	x0,.c
	add	x1,x0,UT_PILOTE			; pilote, « <peripherique> <interface> »
	adr	x0,ligne_usb
	mov	x2,96
	bl	str_copy_n
	adr	x0,ligne_usb
	bl	str_len
	adr	x21,ligne_usb
	add	x21,x21,x0
	mov	w0,' '
	strb	w0,[x21],1
	ldr	x0,[x19,8]
	mov	x1,x21
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	ldr	x0,[x19,16]
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,ligne_usb
	bl	lancer
	ldr	x0,[x19,8]			; notification (une par peripherique)
	adr	x1,ligne_usb
	bl	noter_branche
	b	.out
.aucun:	adr	x0,m_aucun
	bl	puts
.out:	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = classe/sous-classe/protocole, x1 = vendeur/produit -> les cles
; « vvvv:pppp », « cc/ss/pp », « cc/ss », « cc » (hexa en minuscules)
cles_usb:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,cle_vp
	lsr	x1,x20,16
	mov	x2,4
	bl	hexa
	mov	w1,':'
	strb	w1,[x0],1
	and	x1,x20,0xFFFF
	mov	x2,4
	bl	hexa
	strb	wzr,[x0]
	adr	x0,cle_cc_ss_pp
	lsr	x1,x19,16
	mov	x2,2
	bl	hexa
	mov	w1,'/'
	strb	w1,[x0],1
	lsr	x1,x19,8
	and	x1,x1,0xFF
	mov	x2,2
	bl	hexa
	mov	w1,'/'
	strb	w1,[x0],1
	and	x1,x19,0xFF
	mov	x2,2
	bl	hexa
	strb	wzr,[x0]
	adr	x0,cle_cc_ss			; (les debuts de la precedente)
	adr	x1,cle_cc_ss_pp
	mov	x2,6
	bl	str_copy_n
	adr	x0,cle_cc
	adr	x1,cle_cc_ss_pp
	mov	x2,3
	bl	str_copy_n
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = valeur, x2 = chiffres -> x0 = fin (hexa minuscule)
hexa:
	add	x3,x0,x2
	mov	x4,x3
.c:	and	x5,x1,15
	cmp	x5,10
	add	x6,x5,'0'
	add	x7,x5,'a'-10
	csel	x5,x6,x7,lo
	sub	x4,x4,1
	strb	w5,[x4]
	lsr	x1,x1,4
	cmp	x4,x0
	b.hi	.c
	mov	x0,x3
	ret

; x0 = cle -> x0 = ligne « usb: » de la table, 0 si aucune
chercher_usb:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x19,usbt
	adr	x0,usbt_n
	ldr	x20,[x0]
.s:	cbz	x20,.non
	mov	x0,x19
	mov	x1,x21
	bl	str_eq
	cbnz	x0,.oui
	add	x19,x19,UT_TAILLE
	sub	x20,x20,1
	b	.s
.oui:	mov	x0,x19
	b	.out
.non:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = « chemin [arguments] » : lance le pilote (journal : chemin et pid)
lancer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x1,x0
	adr	x0,ligne			; copie : chemin, puis arguments
	mov	x2,128
	bl	str_copy_n
	adr	x19,ligne
	mov	x20,0
	mov	x0,x19
.c:	ldrb	w1,[x0]
	cbz	w1,.go
	cmp	w1,' '
	b.eq	.cut
	add	x0,x0,1
	b	.c
.cut:	strb	wzr,[x0],1
	mov	x20,x0
.go:	mov	x0,x19
	mov	x1,x20
	sys	SYS_SPAWN
	mov	x20,x0
	mov	x0,x19
	bl	puts
	adr	x0,m_fleche
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,x20				; -> pid
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Peripheriques du SoC (docs/RESEAU.md 2.4) : d'apres la revision de la
;  carte (firmware), cles soc:genet et soc:wifi de drivers.txt sur un Pi 4 B
;  (type 0x11), Pi 400 (0x13), CM4 (0x14) ; rien sous QEMU (firmware
;  < 0x50000000). Chaque pilote verifie lui-meme son materiel (fin 0 : pas de
;  relance) ; une fin en erreur est relancee 3 fois. Option no_wifi
;  (SD0:/etc/options.txt) : soc:wifi n'est pas lance.
; ---------------------------------------------------------------------------
soc_start:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,fw_rev
	sys	SYS_FW_CALL
	tbnz	x0,63,.out
	adr	x0,fw_rev
	ldr	w0,[x0,20]
	movl	w1,0x50000000
	cmp	w0,w1
	b.lo	.emul
	adr	x0,fw_board
	sys	SYS_FW_CALL
	tbnz	x0,63,.out
	adr	x0,fw_board
	ldr	w19,[x0,20]
	adr	x0,m_carte			; "hwmanager : carte, revision ..."
	bl	puts
	mov	x0,x19
	bl	print_hex
	adr	x0,m_nl
	bl	puts
	tbz	w19,23,.autre			; ancien format : pas un Pi 4
	ubfx	w20,w19,4,8
	cmp	w20,0x11
	b.eq	.pi4
	cmp	w20,0x13
	b.eq	.pi4
	cmp	w20,0x14
	b.ne	.autre
.pi4:	bl	hdmi_boot			; son HDMI (docs/SON.md 5)
	adr	x0,s_no_wifi
	bl	option_mot
	mov	x21,x0
	adr	x0,s_no_eth
	bl	option_mot
	mov	x22,x0
	adr	x19,soct
	adr	x0,soc_n
	ldr	x20,[x0]
.s:	cbz	x20,.out
	cbz	x21,.eth
	mov	x0,x19				; no_wifi : soc:wifi passe
	adr	x1,s_wifi
	bl	str_eq
	cbz	x0,.eth
	adr	x0,m_no_wifi
	bl	puts
	b	.n
.eth:	cbz	x22,.lance
	mov	x0,x19				; no_ethernet : soc:genet passe
	adr	x1,s_genet
	bl	str_eq
	cbz	x0,.lance
	adr	x0,m_no_eth
	bl	puts
	b	.n
.lance:	mov	x0,x19
	bl	soc_lancer
.n:	add	x19,x19,SC_TAILLE
	sub	x20,x20,1
	b	.s
.emul:	adr	x0,m_emul
	bl	puts
	b	.out
.autre:	adr	x0,m_autre
	bl	puts
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree de soct : lance son pilote
soc_lancer:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,m_soc			; "hwmanager : soc:genet -> "
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_fleche
	bl	puts
	add	x0,x19,SC_PILOTE
	bl	lancer
	cmp	x0,0
	csel	x0,xzr,x0,lt
	str	x0,[x19,SC_PID]
	ldr	x19,[sp],16
	epilogue

; x0 = pid termine, x1 = code de sortie : un pilote du SoC en erreur est
; relance (3 fois)
soc_fin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	cbz	x21,.out
	adr	x0,hdmi_pid			; le pilote HDMI s'est arrete
	ldr	x1,[x0]
	cmp	x1,x21
	b.ne	.soc
	str	xzr,[x0]
	b	.out
.soc:	adr	x19,soct
	adr	x0,soc_n
	ldr	x20,[x0]
.s:	cbz	x20,.out
	ldr	x0,[x19,SC_PID]
	cmp	x0,x21
	b.eq	.trouve
	add	x19,x19,SC_TAILLE
	sub	x20,x20,1
	b	.s
.trouve:
	str	xzr,[x19,SC_PID]
	cbz	w22,.out			; fin normale (materiel absent...)
	ldr	x0,[x19,SC_ESSAIS]
	cmp	x0,3
	b.hs	.abandon
	add	x0,x0,1
	str	x0,[x19,SC_ESSAIS]
	adr	x0,m_relance
	bl	puts
	mov	x0,1000				; un instant avant de relancer
	sys	SYS_SLEEP
	mov	x0,x19
	bl	soc_lancer
	b	.out
.abandon:
	adr	x0,m_abandon
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_nl
	bl	puts
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Son HDMI (docs/SON.md 5)
; ---------------------------------------------------------------------------
HDMI_PAGE	= 0xFEF00000
HDMI_HOTPLUG	= 0x700 + 0x1A8		; HDMI0, registre HOTPLUG (bit 0 : ecran ;
					; d'apres Linux vc4_hdmi_regs.h, a confirmer)

; Au demarrage (Pi 4) : registres, EDID -> pilote s'il y a du son
hdmi_boot:
	prologue
	adr	x0,hdmi_line			; pas de ligne dans drivers.txt : rien
	ldrb	w0,[x0]
	cbz	w0,.out
	adr	x0,s_no_hdmi
	bl	option_mot
	cbz	x0,.go
	adr	x0,m_no_hdmi
	bl	puts
	b	.out
.go:	movl	x0,HDMI_PAGE
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.edid
	adr	x1,hdmi_regs
	str	x0,[x1]
	movl	x1,HDMI_HOTPLUG
	ldr	w0,[x0,x1]
	and	x0,x0,1
	adr	x1,hdmi_hp
	str	x0,[x1]
	adr	x0,hdmi_on
	mov	x1,1
	str	x1,[x0]
.edid:	adr	x0,m_hdmi			; « hwmanager : HDMI0 (hot plug 1) »
	bl	puts
	adr	x0,hdmi_hp
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_hdmi2
	bl	puts
	bl	hdmi_check
.out:	epilogue

; Toutes les 2 s (boucle principale) : changement de l'etat hot plug
hdmi_poll:
	prologue
	adr	x0,hdmi_on
	ldr	x0,[x0]
	cbz	x0,.out
	sys	SYS_TIME_MS
	adr	x1,hdmi_next
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.out
	add	x0,x0,2000
	str	x0,[x1]
	adr	x0,hdmi_regs
	ldr	x0,[x0]
	movl	x1,HDMI_HOTPLUG
	ldr	w0,[x0,x1]
	and	x0,x0,1
	adr	x1,hdmi_hp
	ldr	x2,[x1]
	cmp	x0,x2
	b.eq	.out
	str	x0,[x1]
	cbz	x0,.unplug
	adr	x0,m_hdmi_on			; ecran branche : EDID relu
	bl	puts
	bl	hdmi_check
	b	.out
.unplug:
	adr	x0,m_hdmi_off			; debranche : pilote arrete
	bl	puts
	adr	x0,hdmi_pid
	ldr	x1,[x0]
	str	xzr,[x0]
	cbz	x1,.out
	mov	x0,x1
	sys	SYS_KILL
.out:	epilogue

; EDID (firmware) : l'ecran prend-il le son ? -> lance le pilote (s'il ne
; tourne pas deja)
hdmi_check:
	prologue
	str	x19,[sp,-16]!
	adr	x0,hdmi_pid
	ldr	x0,[x0]
	cbnz	x0,.out
	mov	x0,0				; bloc 0 : nombre d'extensions
	bl	edid_block
	cbz	x0,.unread
	ldrb	w0,[x0,126]
	cbz	w0,.noaudio
	mov	x0,1				; bloc 1 : extension CEA ?
	bl	edid_block
	cbz	x0,.noaudio
	mov	x19,x0
	ldrb	w1,[x19]
	cmp	w1,2
	b.ne	.noaudio
	ldrb	w1,[x19,3]			; audio de base
	tbnz	w1,6,.audio
	ldrb	w2,[x19,2]			; blocs de donnees : de 4 a d
	mov	x3,4
.b:	cmp	x3,x2
	b.hs	.noaudio
	ldrb	w4,[x19,x3]
	lsr	w5,w4,5
	cmp	w5,1				; Audio Data Block
	b.eq	.audio
	and	w4,w4,31
	add	x3,x3,x4
	add	x3,x3,1
	b	.b
.audio:	adr	x0,m_hdmi_audio
	bl	puts
	adr	x0,hdmi_line
	bl	lancer
	cmp	x0,0
	csel	x0,xzr,x0,lt
	adr	x1,hdmi_pid
	str	x0,[x1]
	b	.out
.noaudio:
	adr	x0,m_hdmi_mute
	bl	puts
	b	.out
.unread:
	adr	x0,m_hdmi_noedid
	bl	puts
.out:	ldr	x19,[sp],16
	epilogue

; x0 = bloc d'EDID (0, 1) -> x0 = ses 128 octets (lus par le firmware), 0 si
; illisible
edid_block:
	prologue
	adr	x1,fw_edid
	mov	w2,160
	str	w2,[x1]
	str	wzr,[x1,4]
	movl	w2,0x00030020			; GET_EDID_BLOCK
	str	w2,[x1,8]
	mov	w2,136
	str	w2,[x1,12]
	str	wzr,[x1,16]
	str	w0,[x1,20]
	str	wzr,[x1,24]
	str	wzr,[x1,156]
	mov	x0,x1
	sys	SYS_FW_CALL
	tbnz	x0,63,.non
	adr	x0,fw_edid
	ldr	w1,[x0,24]			; etat : 0 lu
	cbnz	w1,.non
	add	x0,x0,28
	b	.out
.non:	mov	x0,0
.out:	epilogue

; x0 = mot -> x0 = 1 s'il commence une ligne de SD0:/etc/options.txt
option_mot:
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
.ligne:	mov	x1,x21				; le mot en debut de ligne
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	cbz	w3,.fin_mot
	ldrb	w4,[x0,x2]
	cmp	w3,w4
	b.ne	.suiv
	add	x2,x2,1
	b	.c
.fin_mot:
	ldrb	w4,[x0,x2]			; suivi d'une fin de ligne
	cmp	w4,' '
	b.ls	.oui
.suiv:	ldrb	w3,[x0],1			; ligne suivante
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

; x0 = peripherique USB, x1 = « chemin [arguments] » du pilote lance : une
; notification « Peripherique USB branche » (la premiere interface
; seulement), le nom du pilote retenu pour le retrait
noter_branche:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	x19,x0,1
	cmp	x19,NOMS_MAX
	b.hs	.out
	adr	x20,noms_usb
	add	x20,x20,x19,lsl 5		; 32 octets par peripherique
	ldrb	w0,[x20]
	cbnz	w0,.out				; deja annonce
	mov	x2,x1				; le nom : apres le dernier « / »,
.d:	ldrb	w3,[x1],1			; jusqu'a l'espace
	cbz	w3,.nom
	cmp	w3,'/'
	b.ne	.d
	mov	x2,x1
	b	.d
.nom:	mov	x0,x20
	mov	x3,31
.c:	ldrb	w4,[x2],1
	cmp	w4,' '
	b.ls	.f
	strb	w4,[x0],1
	subs	x3,x3,1
	b.ne	.c
.f:	strb	wzr,[x0]
	adr	x0,t_branche
	mov	x1,x20
	bl	notifier
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = peripherique USB retire : notification avec le nom de son pilote
noter_retire:
	prologue
	str	x19,[sp,-16]!
	sub	x0,x0,1
	cmp	x0,NOMS_MAX
	b.hs	.out
	adr	x19,noms_usb
	add	x19,x19,x0,lsl 5
	ldrb	w0,[x19]
	cbz	w0,.out				; (sans pilote : rien)
	adr	x0,t_retire
	mov	x1,x19
	bl	notifier
	strb	wzr,[x19]
.out:	ldr	x19,[sp],16
	epilogue

include '../../inc/notify.inc'

; x0 = valeur : en hexadecimal sur la console
print_hex:
	prologue
	sub	sp,sp,32
	add	x1,sp,17
	strb	wzr,[x1]
	mov	x2,x0
.c:	and	x3,x2,15
	cmp	x3,10
	add	x4,x3,'0'
	add	x5,x3,'A'-10
	csel	x3,x4,x5,lo
	sub	x1,x1,1
	strb	w3,[x1]
	lsr	x2,x2,4
	cbnz	x2,.c
	mov	x0,x1
	bl	puts
	add	sp,sp,32
	epilogue

i2c_code

E_INVAL	= -22
s_nom		db 'hwmanager',0
k_i2c1		db 'i2c1:',0
k_usb		db 'usb:',0
k_soc		db 'soc:',0
s_wifi		db 'wifi',0
s_no_wifi	db 'no_wifi',0
s_no_eth	db 'no_ethernet',0
s_genet		db 'genet',0
k_hdmi		db 'hdmi0:audio',0
s_no_hdmi	db 'no_hdmiaudio',0
m_no_hdmi	db 'hwmanager : no_hdmiaudio (options.txt) : son HDMI non surveill',0xC3,0xA9,10,0
m_hdmi		db 'hwmanager : HDMI0, hot plug ',0
m_hdmi2		db ' ; son de l',0x27,0xC3,0xA9,'cran : ',0
m_hdmi_audio	db 'oui (EDID) -> ',0
m_hdmi_mute	db 'non (EDID)',10,0
m_hdmi_noedid	db 'EDID illisible',10,0
m_hdmi_on	db 'hwmanager : HDMI0 branch',0xC3,0xA9,' ; son de l',0x27,0xC3,0xA9,'cran : ',0
m_hdmi_off	db 'hwmanager : HDMI0 d',0xC3,0xA9,'branch',0xC3,0xA9,' : pilote du son arr',0xC3,0xAA,'t',0xC3,0xA9,10,0
m_no_eth	db 'hwmanager : no_ethernet (options.txt) : soc:genet non lanc',0xC3,0xA9,10,0
p_options	db 'SD0:/etc/options.txt',0
m_carte		db 'hwmanager : carte, r',0xC3,0xA9,'vision ',0
m_emul		db 'hwmanager : ',0xC3,0xA9,'mulateur : rien du SoC',10,0
m_autre		db 'hwmanager : pas un Pi 4 : pilotes du SoC non lanc',0xC3,0xA9,'s',10,0
m_no_wifi	db 'hwmanager : no_wifi (options.txt) : soc:wifi non lanc',0xC3,0xA9,10,0
m_soc		db 'hwmanager : soc:',0
m_relance	db 'hwmanager : pilote du SoC en erreur, relance',10,0
m_abandon	db 'hwmanager : pilote abandonn',0xC3,0xA9,' (3 essais) : soc:',0
m_usb		db 'hwmanager : usb ',0
m_point		db '.',0
m_par		db ' (',0
m_parf		db ') -> ',0
m_aucun		db 'aucun pilote',10,0
m_retire	db ' retire',10,0
t_branche	db 'P',0xC3,0xA9,'riph',0xC3,0xA9,'rique USB branch',0xC3,0xA9,0
t_retire	db 'P',0xC3,0xA9,'riph',0xC3,0xA9,'rique USB retir',0xC3,0xA9,0
	align 8
cles		dd cle_vp, cle_cc_ss_pp, cle_cc_ss, cle_cc, 0
p_drivers	db 'SD0:/etc/drivers.txt',0
m_pret		db 'hwmanager : en service',10,0
m_i2c		db 'hwmanager : i2c1 0x',0
m_fleche	db ' -> ',0
m_nl		db 10,0
	align 8

include '../../inc/applist.inc'		; ini_read, str_copy_n

segment readable writeable
kit_data
applis_data
i2c_data
	align 8
det_n	dd 0
soc_n	dd 0
	align 16
fw_rev		dw 28, 0, 0x00000001, 4, 0, 0, 0	; GET_FIRMWARE_REVISION
	align 16
fw_board	dw 28, 0, 0x00010002, 4, 0, 0, 0	; GET_BOARD_REVISION
	align 8
soct	rb SOC_MAX * SC_TAILLE
hdmi_on		dd 0				; 1 : HOTPLUG surveille
hdmi_regs	dd 0
hdmi_hp		dd 0				; dernier etat hot plug
hdmi_next	dd 0
hdmi_pid	dd 0				; pilote lance (0 : aucun)
hdmi_line	rb 128				; « hdmi0:audio = ... » de drivers.txt
		align 16
fw_edid		rb 160
optbuf	rb 1024
usbt_n	dd 0
noms_usb	rb NOMS_MAX * 32
usbt	rb USBT_MAX * UT_TAILLE
cle_vp		rb 16
cle_cc_ss_pp	rb 16
cle_cc_ss	rb 8
cle_cc		rb 8
ligne_usb	rb 128
table	rb DET_MAX * DET_TAILLE
msg	rb 512
ligne	rb 128
