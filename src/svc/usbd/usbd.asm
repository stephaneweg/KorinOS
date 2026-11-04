; usbd - le bus USB en service (docs/USB.md, feuille de route section 3),
; lance par init2. La mecanique, sans politique :
;  * les controleurs : DWC2 (port USB-C, dwc2.inc) et xHCI VL805 derriere
;    PCIe (ports USB-A, xhci.inc) ; enumeration, hubs ;
;  * chaque interface d'un peripherique est annoncee a hwmanager, qui lance
;    le pilote de drivers.txt (usbkeyboard, usbmouse...) ;
;  * le service « usb » fait les transferts pour les pilotes : descripteurs,
;    controle, interruption (lecture : reponse differee, quand un rapport
;    arrive), bulk (par le tampon partage du pilote et un tampon DMA) ;
;  * chaque peripherique est publie sur le bus (docs/BUS.md) :
;    « korin.usb.device.<numero> », retire avec lui.
; Isochrone (iso.inc : cartes son USB) : anneau partage avec le pilote.
; Aucune interruption : la boucle scrute toutes les 2 ms ; un second thread
; tient les canaux isochrones du DWC2 (toutes les ms).
; « usb_noyau » dans SD0:/etc/options.txt : la pile USB du noyau est lancee,
; usbd s'efface.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/usb.inc'
include '../../inc/bus.inc'
include '../../kits/buskit/buskit.inc'

DEV_MAX		= 16
DV_CTRL		= 0			; 0 DWC2, 1 xHCI ; -1 : place libre
DV_POIGNEE	= 8			; propre au controleur
DV_VIDPID	= 16			; vendeur << 16 | produit
DV_VITESSE	= 24
DV_CFGLEN	= 32
DV_CFG		= 40			; descripteur de configuration (CFG_MAX)
CFG_MAX		= 464
DV_TAILLE	= 40 + CFG_MAX + 8

TUY_MAX		= 16
TU_PID		= 0			; pilote proprietaire (0 : libre)
TU_CTRL		= 8
TU_POIGNEE	= 16			; tuyau du controleur
TU_REQ		= 24			; lecture en attente : numero de requete (0 : aucune)
TU_DEV		= 32			; numero du peripherique
TU_TAILLE	= 40

TAMP_MAX	= 8			; tampons partages des pilotes (bulk)
TA_PID		= 0
TA_ADR		= 8
TA_NUM		= 16
TA_TAILLE	= 24
BULK_MAX	= 65536			; un transfert bulk au plus

E_INVAL		= -22
E_NODEV		= -19
E_NOMEM		= -12
E_PERM		= -1

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	bl	usb_noyau			; la pile du noyau a la main ?
	cbnz	x0,.efface
	adr	x0,devs				; table vide
	mov	x1,0
.v:	mov	x2,-1
	str	x2,[x0,DV_CTRL]
	add	x0,x0,DV_TAILLE
	add	x1,x1,1
	cmp	x1,DEV_MAX
	b.lo	.v
	adr	x0,s_nom
	mov	x1,SVF_FINS			; (pilotes qui s'arretent)
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	mov	x0,3000				; le bus (init2 lance busd avant)
	bl	bp_open
	mov	x0,BULK_MAX			; tampon DMA des transferts bulk
	bl	dma_zone
	tbnz	x0,63,quit
	adr	x2,bulk_va
	stp	x0,x1,[x2]			; bulk_va, bulk_phys
	adr	x0,m_pret
	bl	puts
	bl	dw_init				; port USB-C (DWC2)
	adr	x1,dw_ok
	cmp	x0,0
	cset	x0,ge
	str	x0,[x1]
	bl	xh_init				; ports USB-A (xHCI derriere PCIe)
	adr	x1,xh_ok
	cmp	x0,0
	cset	x0,ge
	str	x0,[x1]
.loop:	adr	x0,canal
	ldr	x0,[x0]
	adr	x1,msg
	mov	x2,2				; 2 ms au plus : puis scruter
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.scrute
	bl	requete
.scrute:
	bl	scruter
	b	.loop
.efface:
	adr	x0,m_efface
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
quit:	mov	x0,1				; (sur une erreur : init2 relance)
	sys	SYS_EXIT

; -> x0 = 1 si « usb_noyau » figure dans SD0:/etc/options.txt
usb_noyau:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,p_options
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	mov	x19,x0
	adr	x1,tampon
	mov	x2,1023
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.non
	adr	x0,tampon
	strb	wzr,[x0,x20]
.ligne:	adr	x1,s_usb_noyau			; le mot en debut de ligne
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
.out:	ldp	x19,x20,[sp],16
	epilogue

; (msg) une requete, x0 = son numero
requete:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x20,msg
	ldr	x0,[x20]
	movl	x1,SVC_OP_FIN
	cmp	x0,x1
	b.eq	.fin
	cmp	x0,USB_OP_DESCRIPTEURS
	b.eq	.desc
	cmp	x0,USB_OP_CONTROLE
	b.eq	.ctrl
	cmp	x0,USB_OP_INTR_OUVRIR
	b.eq	.ouvrir
	cmp	x0,USB_OP_INTR_LIRE
	b.eq	.lire
	cmp	x0,USB_OP_BULK_TAMPON
	b.eq	.tampon
	cmp	x0,USB_OP_BULK
	b.eq	.bulk
	cmp	x0,USB_OP_BULK_NB
	b.eq	.bulk_nb
	cmp	x0,USB_OP_ISO_OPEN		; isochrone (iso.inc)
	b.eq	.iso_o
	cmp	x0,USB_OP_ISO_CLOSE
	b.eq	.iso_c
	mov	x0,E_INVAL
	b	.rep
.iso_o:	bl	iso_open
	b	.rep
.iso_c:	bl	iso_close
	b	.rep
.fin:	ldr	x0,[x20,8]			; (avis) : ses tuyaux sont fermes
	bl	fermer_tuyaux
	ldr	x0,[x20,8]
	bl	iso_close_pid
	b	.out
.desc:	ldr	x0,[x20,8]
	bl	dev_de
	cbz	x0,.nodev
	mov	x21,x0
	add	x0,x20,48			; descripteur de configuration
	add	x1,x21,DV_CFG
	ldr	x2,[x21,DV_CFGLEN]
	str	x2,[x20,16]
	bl	mem_move
	ldr	x0,[x21,DV_VIDPID]
	str	x0,[x20,24]
	ldr	x0,[x21,DV_VITESSE]
	str	x0,[x20,32]
	mov	x0,0
	b	.rep
.ctrl:	ldr	x0,[x20,8]
	bl	dev_de
	cbz	x0,.nodev
	ldr	x3,[x20,24]			; longueur
	cmp	x3,464
	b.hi	.inval
	ldr	x1,[x20,16]			; SETUP
	add	x2,x20,48			; donnees (aller et retour)
	ldr	x4,[x0,DV_CTRL]
	ldr	x0,[x0,DV_POIGNEE]
	stp	x4,x0,[sp,-16]!			; (controleur, poignee)
	cbnz	x4,.c_xh
	bl	dw_control
	b	.c_fait
.c_xh:	bl	xh_control
.c_fait:
	ldp	x4,x5,[sp],16
	tbnz	x0,63,.rep
	ldr	x1,[x20,16]			; CLEAR_FEATURE(ENDPOINT_HALT) reussi :
	and	x2,x1,0xFFFFFFFF		; le controleur remet son extremite
	movl	x3,0x00000102			; (bascule DATA0, anneau) a zero
	cmp	x2,x3
	b.ne	.rep
	str	x0,[sp,-16]!
	lsr	x1,x1,32
	and	x1,x1,0xFF
	mov	x0,x5
	cbnz	x4,.r_xh
	bl	dw_bulk_raz
	b	.r_fait
.r_xh:	bl	xh_bulk_raz
.r_fait:
	ldr	x0,[sp],16
	b	.rep
.ouvrir:
	ldr	x0,[x20,8]
	bl	dev_de
	cbz	x0,.nodev
	mov	x21,x0
	adr	x22,tuyaux			; une place libre
	mov	x0,0
.t:	cmp	x0,TUY_MAX
	b.hs	.nomem
	ldr	x1,[x22,TU_PID]
	cbz	x1,.place
	add	x22,x22,TU_TAILLE
	add	x0,x0,1
	b	.t
.place:	ldr	x1,[x20,16]			; extremite, taille, intervalle
	ldr	x2,[x20,24]
	ldr	x3,[x20,32]
	ldr	x4,[x21,DV_CTRL]
	ldr	x0,[x21,DV_POIGNEE]
	str	x4,[x22,TU_CTRL]
	cbnz	x4,.o_xh
	bl	dw_intr_ouvrir
	b	.o_fait
.o_xh:	bl	xh_intr_ouvrir
.o_fait:
	tbnz	x0,63,.rep
	str	x0,[x22,TU_POIGNEE]
	str	xzr,[x22,TU_REQ]
	ldr	x0,[x20,8]
	str	x0,[x22,TU_DEV]
	ldr	w0,[x20,44]			; le pilote
	str	x0,[x22,TU_PID]
	adr	x0,tuyaux			; -> numero du tuyau
	sub	x0,x22,x0
	mov	x1,TU_TAILLE
	udiv	x0,x0,x1
	b	.rep
.lire:	ldr	x0,[x20,8]			; tuyau du pilote ?
	cmp	x0,TUY_MAX
	b.hs	.inval
	adr	x22,tuyaux
	mov	x1,TU_TAILLE
	madd	x22,x0,x1,x22
	ldr	x1,[x22,TU_PID]
	ldr	w2,[x20,44]
	cmp	x1,x2
	b.ne	.perm
	ldr	x0,[x22,TU_REQ]			; une lecture deja en attente : rendue
	cbz	x0,.attente
	sub	x0,x0,1
	adr	x1,rep
	str	xzr,[x1]
	sys	SYS_SVC_REPLY
.attente:
	add	x0,x19,1			; reponse quand un rapport arrive
	str	x0,[x22,TU_REQ]
	b	.out
.tampon:
	ldr	w0,[x20,44]			; (un seul par pilote)
	bl	tampon_de
	cbz	x0,.t_place
	mov	x21,x0
	b	.t_map
.t_place:
	adr	x21,tampons
	mov	x0,0
.tp:	cmp	x0,TAMP_MAX
	b.hs	.nomem
	ldr	x1,[x21,TA_PID]
	cbz	x1,.t_map
	add	x21,x21,TA_TAILLE
	add	x0,x0,1
	b	.tp
.t_map:	ldr	x0,[x20,8]			; taille (64 Ko + 4 Ko au plus)
	movl	x1,BULK_MAX + 4096
	cmp	x0,x1
	csel	x0,x0,x1,ls
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.rep
	str	x0,[x21,TA_NUM]
	sys	SYS_BUF_MAP			; (son adresse ici)
	tbnz	x0,63,.rep
	str	x0,[x21,TA_ADR]
	ldr	x0,[x21,TA_NUM]			; autorise au pilote
	ldr	w1,[x20,44]
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.rep
	ldr	w0,[x20,44]
	str	x0,[x21,TA_PID]
	ldr	x0,[x21,TA_NUM]			; -> son numero (buf_map chez le pilote)
	b	.rep
.bulk:	ldr	w0,[x20,44]			; le tampon partage du pilote
	bl	tampon_de
	cbz	x0,.inval
	ldr	x22,[x0,TA_ADR]
	ldr	x0,[x20,32]			; + decalage
	add	x22,x22,x0
	ldr	x21,[x20,48]			; longueur
	cmp	x21,BULK_MAX
	b.hi	.inval
	ldr	x0,[x20,8]
	bl	dev_de
	cbz	x0,.nodev
	str	x0,[sp,-16]!
	ldr	x1,[x20,16]			; sortie : copie vers le tampon DMA
	tbnz	x1,7,.b_go
	adr	x0,bulk_va
	ldr	x0,[x0]
	mov	x1,x22
	mov	x2,x21
	bl	mem_move
.b_go:	ldr	x6,[sp],16
	ldr	x1,[x20,16]			; extremite, taille max.
	ldr	x2,[x20,24]
	adr	x3,bulk_va
	ldp	x3,x4,[x3]			; tampon DMA : adresse, physique
	mov	x5,x21
	ldr	x7,[x6,DV_CTRL]
	ldr	x0,[x6,DV_POIGNEE]
	cbnz	x7,.b_xh
	bl	dw_bulk
	b	.b_fait
.b_xh:	bl	xh_bulk
.b_fait:
	cmp	x0,0
	b.le	.rep
	ldr	x1,[x20,16]			; entree : copie vers le pilote
	tbz	x1,7,.rep
	str	x0,[sp,-16]!
	mov	x2,x0
	mov	x0,x22
	adr	x1,bulk_va
	ldr	x1,[x1]
	bl	mem_move
	ldr	x0,[sp],16
	b	.rep
.bulk_nb:					; lecture IN sans attente (usbnet)
	ldr	w0,[x20,44]			; le tampon partage du pilote
	bl	tampon_de
	cbz	x0,.inval
	ldr	x22,[x0,TA_ADR]
	ldr	x0,[x20,32]			; + decalage
	add	x22,x22,x0
	ldr	x21,[x20,48]			; longueur
	ldr	x0,[x20,8]
	bl	dev_de
	cbz	x0,.nodev
	ldr	x7,[x0,DV_CTRL]
	cbnz	x7,.nb_xh
	adr	x2,nb_dev			; (arrete si le pilote ou le
	ldr	x1,[x20,8]			; peripherique s'en va)
	str	x1,[x2]
	ldr	w1,[x20,44]
	str	x1,[x2,8]
	ldr	x0,[x0,DV_POIGNEE]
	ldr	x1,[x20,16]			; extremite, taille max.
	ldr	x2,[x20,24]
	mov	x3,x21
	bl	dw_bulk_nb
	cmp	x0,0
	b.le	.rep
	str	x0,[sp,-16]!
	mov	x2,x0
	mov	x0,x22
	bl	mem_move			; (x1 = les donnees du controleur)
	ldr	x0,[sp],16
	b	.rep
.nb_xh:	mov	x0,-38				; (xHCI : pas encore, E_NOSYS)
	b	.rep
.nodev:	mov	x0,E_NODEV
	b	.rep
.nomem:	mov	x0,E_NOMEM
	b	.rep
.perm:	mov	x0,E_PERM
	b	.rep
.inval:	mov	x0,E_INVAL
.rep:	str	x0,[x20]
	mov	x0,x19
	mov	x1,x20
	sys	SYS_SVC_REPLY
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid -> x0 = l'entree de son tampon partage (bulk), 0 si aucun
tampon_de:
	adr	x1,tampons
	mov	x2,0
.t:	cmp	x2,TAMP_MAX
	b.hs	.non
	ldr	x3,[x1,TA_PID]
	cmp	x3,x0
	b.eq	.oui
	add	x1,x1,TA_TAILLE
	add	x2,x2,1
	b	.t
.oui:	mov	x0,x1
	ret
.non:	mov	x0,0
	ret

; x0 = numero de peripherique -> x0 = son entree, 0 si inconnu
dev_de:
	sub	x0,x0,1
	cmp	x0,DEV_MAX
	b.hs	.non
	adr	x1,devs
	mov	x2,DV_TAILLE
	madd	x0,x0,x2,x1
	ldr	x1,[x0,DV_CTRL]
	tbnz	x1,63,.non
	ret
.non:	mov	x0,0
	ret

; x0 = pid : ses tuyaux sont fermes (une lecture en attente est rendue)
fermer_tuyaux:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x0,nb_pid			; sa lecture sans attente
	ldr	x1,[x0]
	cmp	x1,x21
	b.ne	.nb_ok
	str	xzr,[x0]
	bl	dw_bulk_nb_stop
.nb_ok:	adr	x19,tuyaux
	mov	x20,0
.t:	cmp	x20,TUY_MAX
	b.hs	.out
	ldr	x0,[x19,TU_PID]
	cmp	x0,x21
	b.ne	.n
	str	xzr,[x19,TU_PID]
	ldr	x0,[x19,TU_POIGNEE]		; cote controleur
	ldr	x1,[x19,TU_CTRL]
	cbnz	x1,.f_xh
	bl	dw_intr_fermer
	b	.f_req
.f_xh:	bl	xh_intr_fermer
.f_req:	ldr	x0,[x19,TU_REQ]
	cbz	x0,.n
	str	xzr,[x19,TU_REQ]
	sub	x0,x0,1
	adr	x1,rep
	str	xzr,[x1]
	sys	SYS_SVC_REPLY
.n:	add	x19,x19,TU_TAILLE
	add	x20,x20,1
	b	.t
.out:	mov	x0,x21				; son tampon partage (bulk)
	bl	tampon_de
	cbz	x0,.fini
	str	xzr,[x0,TA_PID]
	ldr	x0,[x0,TA_NUM]
	sys	SYS_BUF_FREE
.fini:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; travail periodique : controleurs, puis les lectures en attente
scruter:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,dw_ok
	ldr	x0,[x0]
	cbz	x0,.x
	bl	dw_scruter
.x:	adr	x0,xh_ok
	ldr	x0,[x0]
	cbz	x0,.t0
	bl	xh_scruter
	bl	iso_xh_service			; TRB isochrones en file
.t0:	adr	x19,tuyaux
	mov	x20,0
.t:	cmp	x20,TUY_MAX
	b.hs	.out
	ldr	x0,[x19,TU_PID]
	cbz	x0,.n
	ldr	x0,[x19,TU_REQ]
	cbz	x0,.n
	ldr	x0,[x19,TU_POIGNEE]
	ldr	x1,[x19,TU_CTRL]
	cbnz	x1,.l_xh
	bl	dw_intr_lire
	b	.l_fait
.l_xh:	bl	xh_intr_lire
.l_fait:
	cmp	x0,0
	b.eq	.n				; rien encore
	mov	x21,x0				; rapport (ou erreur) : la reponse
	adr	x0,rep
	str	x21,[x0]
	b.lt	.envoi
	cmp	x21,464
	mov	x2,464
	csel	x2,x21,x2,ls
	add	x0,x0,48
	bl	mem_move			; (x1 = les donnees du controleur)
.envoi:	ldr	x0,[x19,TU_REQ]
	str	xzr,[x19,TU_REQ]
	sub	x0,x0,1
	adr	x1,rep
	sys	SYS_SVC_REPLY
.n:	add	x19,x19,TU_TAILLE
	add	x20,x20,1
	b	.t
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Pour les controleurs
; ---------------------------------------------------------------------------

; x0 = taille -> x0 = adresse (non cachee, zeros), x1 = adresse physique ;
; x0 < 0 si refuse
dma_zone:
	prologue
	sub	sp,sp,16
	mov	x1,sp
	sys	SYS_DMA_ALLOC
	ldr	x1,[sp]
	add	sp,sp,16
	epilogue

; x0 = controleur, x1 = poignee, x2 = descripteur de peripherique (18 o),
; x3 = descripteur de configuration, x4 = sa longueur, x5 = vitesse
; -> x0 = numero de peripherique (1..), < 0 si la table est pleine ;
; chaque interface est annoncee a hwmanager
usbd_attacher:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	adr	x6,dev_suivant			; une place libre, en tournant : un
	ldr	x20,[x6]			; numero libere ne resert que tard
	mov	x7,DEV_MAX			; (un ancien pilote ne trouve pas un autre
.s:	cbz	x7,.plein			; peripherique sous son numero)
	sub	x7,x7,1
	cmp	x20,DEV_MAX
	csel	x20,xzr,x20,hs
	adr	x19,devs
	mov	x6,DV_TAILLE
	madd	x19,x20,x6,x19
	ldr	x6,[x19,DV_CTRL]
	tbnz	x6,63,.place
	add	x20,x20,1
	b	.s
.place:	add	x6,x20,1
	adr	x7,dev_suivant
	str	x6,[x7]
	str	x0,[x19,DV_CTRL]
	str	x1,[x19,DV_POIGNEE]
	str	x5,[x19,DV_VITESSE]
	ldrh	w0,[x21,8]			; idVendor, idProduct
	ldrh	w1,[x21,10]
	orr	x0,x1,x0,lsl 16
	str	x0,[x19,DV_VIDPID]
	cmp	x23,CFG_MAX
	mov	x0,CFG_MAX
	csel	x23,x23,x0,ls
	str	x23,[x19,DV_CFGLEN]
	add	x0,x19,DV_CFG
	mov	x1,x22
	mov	x2,x23
	bl	mem_move
	add	x20,x20,1			; numero du peripherique
	adr	x0,m_dev			; « usbd : peripherique 1 (VVVV:PPPP) »
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_par
	bl	puts
	ldr	x0,[x19,DV_VIDPID]
	lsr	x0,x0,16
	mov	x1,4
	bl	print_hex
	adr	x0,m_2p
	bl	puts
	ldr	x0,[x19,DV_VIDPID]
	and	x0,x0,0xFFFF
	mov	x1,4
	bl	print_hex
	adr	x0,m_parf
	bl	puts
	add	x21,x19,DV_CFG			; les interfaces (reglage 0)
	add	x22,x21,x23
.d:	add	x0,x21,2
	cmp	x0,x22
	b.hi	.fini
	ldrb	w0,[x21]			; bLength
	cmp	w0,2
	b.lo	.fini
	ldrb	w1,[x21,1]			; bDescriptorType
	cmp	w1,USB_DT_INTERFACE
	b.ne	.dn
	ldrb	w1,[x21,3]			; bAlternateSetting
	cbnz	w1,.dn
	mov	x0,x20
	ldrb	w1,[x21,2]			; bInterfaceNumber
	ldrb	w2,[x21,5]			; classe, sous-classe, protocole
	ldrb	w3,[x21,6]
	ldrb	w4,[x21,7]
	lsl	w2,w2,16
	orr	w2,w2,w3,lsl 8
	orr	w2,w2,w4
	ldr	x3,[x19,DV_VIDPID]
	bl	annoncer
.dn:	ldrb	w0,[x21]
	add	x21,x21,x0
	b	.d
.fini:	mov	x0,x20
	mov	x1,x19
	bl	usb_publier
	mov	x0,x20
	b	.out
.plein:	mov	x0,E_NOMEM
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero de peripherique : il a ete retire (le controleur a deja
; libere ses ressources) ; ses tuyaux sont fermes (une lecture en attente
; recoit E_NODEV : le pilote s'arrete) ; hwmanager est prevenu
usbd_detacher:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	bl	dev_de
	cbz	x0,.out
	str	x0,[sp,-16]!
	mov	x0,x21				; ses tuyaux isochrones
	bl	iso_close_dev
	ldr	x0,[sp],16
	mov	x1,-1				; place libre
	str	x1,[x0,DV_CTRL]
	adr	x0,nb_dev			; sa lecture sans attente
	ldr	x1,[x0]
	cmp	x1,x21
	b.ne	.nb_ok
	ldr	x1,[x0,8]
	cbz	x1,.nb_ok
	str	xzr,[x0,8]
	bl	dw_bulk_nb_stop
.nb_ok:
	adr	x0,m_dev			; « usbd : peripherique 2 retire »
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_retire
	bl	puts
	adr	x19,tuyaux
	mov	x20,0
.t:	cmp	x20,TUY_MAX
	b.hs	.hw
	ldr	x0,[x19,TU_PID]
	cbz	x0,.n
	ldr	x0,[x19,TU_DEV]
	cmp	x0,x21
	b.ne	.n
	str	xzr,[x19,TU_PID]
	ldr	x0,[x19,TU_POIGNEE]		; cote controleur
	ldr	x1,[x19,TU_CTRL]
	cbnz	x1,.f_xh
	bl	dw_intr_fermer
	b	.f_req
.f_xh:	bl	xh_intr_fermer
.f_req:	ldr	x0,[x19,TU_REQ]
	cbz	x0,.n
	str	xzr,[x19,TU_REQ]
	sub	x0,x0,1
	adr	x1,rep
	mov	x2,E_NODEV
	str	x2,[x1]
	sys	SYS_SVC_REPLY
.n:	add	x19,x19,TU_TAILLE
	add	x20,x20,1
	b	.t
.hw:	mov	x0,x21				; le bus : sujet retire
	mov	x1,0
	bl	usb_publier
	adr	x0,hw_canal			; hwmanager : retrait
	ldr	x0,[x0]
	cbz	x0,.out
	sub	x0,x0,1
	adr	x1,hw_msg
	mov	x2,HW_OP_USB_RETRAIT
	stp	x2,x21,[x1]
	sys	SYS_SVC_CALL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero de peripherique, x1 = son entree : « korin.usb.device.<n> »
; publie (gardee), « vvvv:pppp cc/ss/pp... » (vendeur, produit, puis classe,
; sous-classe, protocole de chaque interface) ; x1 = 0 : le sujet est retire
usb_publier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	mov	x20,x1
	mov	x0,sp				; le sujet
	adr	x1,s_topic_usb
	bl	bp_cat
	mov	x1,x19
	bl	bp_dec
	strb	wzr,[x0]
	mov	x2,BUS_DELETE
	mov	x1,0
	cbz	x20,.pub
	adr	x0,bus_val			; la valeur
	ldr	x1,[x20,DV_VIDPID]
	lsr	x1,x1,16
	mov	x2,4
	bl	bp_hex
	mov	w1,':'
	strb	w1,[x0],1
	ldr	x1,[x20,DV_VIDPID]
	and	x1,x1,0xFFFF
	mov	x2,4
	bl	bp_hex
	add	x21,x20,DV_CFG			; les interfaces (reglage 0)
	ldr	x22,[x20,DV_CFGLEN]
	add	x22,x21,x22
.d:	add	x1,x21,8
	cmp	x1,x22
	b.hi	.fini
	adr	x1,bus_val + 240		; (la valeur : 255 octets au plus)
	cmp	x0,x1
	b.hs	.fini
	ldrb	w1,[x21]			; bLength
	cmp	w1,2
	b.lo	.fini
	ldrb	w1,[x21,1]
	cmp	w1,USB_DT_INTERFACE
	b.ne	.dn
	ldrb	w1,[x21,3]			; bAlternateSetting
	cbnz	w1,.dn
	mov	w1,' '
	strb	w1,[x0],1
	ldrb	w1,[x21,5]
	mov	x2,2
	bl	bp_hex
	mov	w1,'/'
	strb	w1,[x0],1
	ldrb	w1,[x21,6]
	mov	x2,2
	bl	bp_hex
	mov	w1,'/'
	strb	w1,[x0],1
	ldrb	w1,[x21,7]
	mov	x2,2
	bl	bp_hex
.dn:	ldrb	w1,[x21]
	add	x21,x21,x1
	b	.d
.fini:	strb	wzr,[x0]
	adr	x1,bus_val
	mov	x2,BUS_RETAIN
.pub:	mov	x0,sp
	bl	bp_publish
	add	sp,sp,64
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = peripherique, x1 = interface, x2 = classe/sous-classe/protocole,
; x3 = vendeur/produit : a hwmanager (qui lance le pilote)
annoncer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	adr	x0,m_itf			; « usbd :   interface 0 : 03/01/01 »
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_2p2
	bl	puts
	lsr	x0,x21,16
	mov	x1,2
	bl	print_hex
	adr	x0,m_slash
	bl	puts
	lsr	x0,x21,8
	and	x0,x0,0xFF
	mov	x1,2
	bl	print_hex
	adr	x0,m_slash
	bl	puts
	and	x0,x21,0xFF
	mov	x1,2
	bl	print_hex
	adr	x0,m_nl
	bl	puts
	adr	x0,hw_canal			; le canal de hwmanager (une fois)
	ldr	x0,[x0]
	cbnz	x0,.envoi
	adr	x0,s_hwmanager
	mov	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	add	x0,x0,1
	adr	x1,hw_canal
	str	x0,[x1]
.envoi:	sub	x0,x0,1
	adr	x1,hw_msg
	mov	x2,HW_OP_USB
	stp	x2,x19,[x1]
	stp	x20,x21,[x1,16]
	str	x22,[x1,32]
	sys	SYS_SVC_CALL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur, x1 = chiffres : en hexadecimal sur la console
print_hex:
	prologue
	sub	sp,sp,32
	add	x2,sp,x1
	strb	wzr,[x2]
.c:	and	x3,x0,15
	cmp	x3,10
	add	x4,x3,'0'
	add	x5,x3,'A'-10
	csel	x3,x4,x5,lo
	sub	x2,x2,1
	strb	w3,[x2]
	lsr	x0,x0,4
	mov	x3,sp
	cmp	x2,x3
	b.hi	.c
	mov	x0,sp
	bl	puts
	add	sp,sp,32
	epilogue

include 'dwc2.inc'
include 'xhci.inc'
include 'iso.inc'
include '../inc/buspub.inc'

s_nom		db 'usb',0
s_topic_usb	db BUS_TOPIC_USB,0
s_hwmanager	db 'hwmanager',0
s_usb_noyau	db 'usb_noyau',0
p_options	db 'SD0:/etc/options.txt',0
m_pret		db 'usbd : en service',10,0
m_efface	db 'usbd : usb_noyau (options.txt) : la pile USB du noyau a la main',10,0
m_dev		db 'usbd : peripherique ',0
m_par		db ' (',0
m_2p		db ':',0
m_parf		db ')',10,0
m_retire	db ' retire',10,0
m_itf		db 'usbd :   interface ',0
m_2p2		db ' : ',0
m_slash		db '/',0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 8
canal		dd -1
hw_canal	dd 0				; canal de hwmanager + 1 (0 : a ouvrir)
bp_canal	dd 0				; canal de busd + 1 (buspub.inc)
dw_ok		dd 0
xh_ok		dd 0
dev_suivant	dd 0				; prochaine place essayee
nb_dev		dd 0				; lecture bulk sans attente : peripherique,
nb_pid		dd 0				;   pilote (0 : aucune)
devs		rb DEV_MAX * DV_TAILLE
tuyaux		rb TUY_MAX * TU_TAILLE
tampons		rb TAMP_MAX * TA_TAILLE
bulk_va		dd 0				; tampon DMA des transferts bulk
bulk_phys	dd 0
msg		rb 512
rep		rb 512
hw_msg		rb 512
tampon		rb 1024
bus_val		rb 256
dw_donnees
xh_donnees
iso_data
