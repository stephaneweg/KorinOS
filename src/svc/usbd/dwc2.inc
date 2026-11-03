; ============================================================================
;  svc/usbd/dwc2.inc - controleur hote Synopsys DesignWare OTG (DWC2) pour usbd
;  (docs/USB.md, interface � backend �). Repris de
;  src/kernel/arch/pi4/usb_dwc2.asm (bulk : usb_msd.asm, msd_bulk).
;
;  Port USB-C du Pi 4 (0xFE980000 ; emule par QEMU raspi4b). Mode DMA
;  interne (buffer DMA), tout par scrutation, pas d'IRQ. Canal 0 : controle
;  et interruption ; canal 5 : bulk. Memoire DMA non cachee (dma_zone),
;  vue par le controleur a l'adresse physique | 0xC0000000.
;
;  Poignee : adresse | vitesse << 8 | MPS0 << 16 (vitesse : DW_SPEED_*,
;  codage du registre HPRT0).
;
;  Branchement a chaud (docs/USB.md) : arbre des peripheriques enumeres
;  (dw_noeuds), scrute toutes les 200 ms : HPRT0 et GET_STATUS des ports de
;  chaque hub connu.
;
;  Limite (comme le noyau) : pas de transactions SPLIT (un peripherique
;  FS/LS derriere un hub haute vitesse ne marche pas sur materiel reel).
; ============================================================================

DWC_PHYS	= 0xFE980000
DWC_TAILLE	= 0x10000

DWC_GAHBCFG	= 0x008
DWC_GUSBCFG	= 0x00C
DWC_GRSTCTL	= 0x010
DWC_GRXFSIZ	= 0x024
DWC_GNPTXFSIZ	= 0x028
DWC_GSNPSID	= 0x040
DWC_HPTXFSIZ	= 0x100
DWC_HCFG	= 0x400
DWC_HFNUM	= 0x408
DWC_HPRT0	= 0x440
DWC_HCCHAR	= 0x500			; canal 0 (canal n : + n * 0x20)
DWC_HCSPLT	= 0x504
DWC_HCINT	= 0x508
DWC_HCINTMSK	= 0x50C
DWC_HCTSIZ	= 0x510
DWC_HCDMA	= 0x514
DWC_PCGCCTL	= 0xE00

DW_HPRT_RST	= 1 shl 8
DW_HPRT_PWR	= 1 shl 12
DW_HPRT_ENA	= 1 shl 2
DW_HPRT_W1C	= 0x2E			; CONNDET | ENA | ENACHG | OVRCURRCHG

DW_HCCHAR_CHENA	= 1 shl 31
DW_HCCHAR_ODDFRM = 1 shl 29
DW_HCCHAR_EC1	= 1 shl 20		; periodique : une transaction par trame
DW_HCCHAR_LSPD	= 1 shl 17
DW_HCCHAR_EPIN	= 1 shl 15
DW_EPTYPE_BULK	= 2
DW_EPTYPE_INTR	= 3

DW_HCINT_ERREURS = (1 shl 2) or (1 shl 3) or (1 shl 7) or (1 shl 8) or (1 shl 10)

DW_PID_DATA1	= 2
DW_PID_SETUP	= 3

DW_SPEED_HIGH	= 0
DW_SPEED_FULL	= 1
DW_SPEED_LOW	= 2

DW_BUS		= 0xC0000000		; alias bus du VideoCore
DW_CANAL_BULK	= 5 * 0x20
DW_DELAI	= 500			; ms : etape de controle
DW_DELAI_INTR	= 20			; ms : transfert interruption
DW_DELAI_BULK	= 5000			; ms : transfert bulk complet

DW_EIO		= -5
DW_EPIPE	= -32			; STALL
DW_ETIME	= -110

; zone DMA (4 Ko) : SETUP, tampon de controle, donnees des tuyaux
DW_DMA_TAILLE	= 4096
DW_O_SETUP	= 0
DW_O_BUF	= 512
DW_BUF		= 512
DW_O_TUY	= 2048
DW_TUY_MPS	= 64			; par tuyau

; tuyaux d'interruption
DW_TUY_MAX	= 16
TP_UTIL		= 0
TP_ADR		= 4
TP_EP		= 8			; numero (sans le bit IN)
TP_VIT		= 12
TP_MPS		= 16
TP_BASC		= 20			; 0 DATA0, 1 DATA1
TP_INTER	= 24			; ms
TP_PROCH	= 32			; prochaine lecture (ms)
TP_TAILLE	= 40

; bascules bulk : cle (adresse << 8 | extremite), PID suivant
DW_BASC_MAX	= 32

; arbre des peripheriques (hubs compris) ; ND_POIG = 0 : place libre
DW_NOEUD_MAX	= 32
ND_POIG		= 0			; poignee (adresse | vitesse << 8 | MPS0 << 16)
ND_PARENT	= 4			; adresse du hub parent (0 : port racine)
ND_PORT		= 8			; port sur le parent (1..)
ND_USBD		= 12			; numero usbd (0 : hub, ou non attache)
ND_HUB		= 16			; 1 : hub
ND_NPORTS	= 20			; hub : nombre de ports
ND_TAILLE	= 32

DW_SCAN_MS	= 200			; periode de scrutation des ports

; ----------------------------------------------------------------------------
;  Entree
; ----------------------------------------------------------------------------

; -> x0 = 0 (controleur pret, peripheriques enumeres), < 0 si absent
dw_init:
	prologue
	stp	x19,x20,[sp,-16]!
	movl	x0,DWC_PHYS			; registres
	mov	x1,DWC_TAILLE
	sys	SYS_DEV_MAP
	tbnz	x0,63,.absent
	adr	x1,dw_regs
	str	x0,[x1]
	mov	x0,DW_DMA_TAILLE		; tampons DMA
	bl	dma_zone
	tbnz	x0,63,.nomem
	adr	x2,dw_dma_va
	stp	x0,x1,[x2]
	adr	x0,dw_fw_rev			; QEMU : revision du firmware < 0x50000000
	sys	SYS_FW_CALL
	adr	x0,dw_fw_rev
	ldr	w0,[x0,20]
	movl	x1,0x50000000
	cmp	x0,x1
	cset	w0,lo
	adr	x1,dw_qemu
	str	w0,[x1]
	cbnz	w0,.alim_ok
	adr	x0,dw_fw_alim			; vrai Pi : alimenter le controleur USB
	sys	SYS_FW_CALL			; (peripherique 3, allume + attendre)
	adr	x0,m_dw_alim
	bl	dw_log
	adr	x0,dw_fw_alim
	ldr	w0,[x0,24]
	mov	x1,8
	bl	print_hex
	adr	x0,m_dw_nl
	bl	puts
.alim_ok:
	bl	dw_coeur_init
	cbz	x0,.absent
	bl	dw_port_reset			; -> vitesse, -1 si rien de branche
	tbnz	x0,63,.vide
	mov	x19,x0
	adr	x1,dw_connecte
	mov	w2,1
	str	w2,[x1]
	mov	x0,x19
	mov	x1,0				; port racine
	mov	x2,1
	bl	dw_enumerer
	mov	x0,0
	b	.out
.vide:	adr	x0,m_dw_vide
	bl	dw_log
	mov	x0,0
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.absent:
	adr	x0,m_dw_absent
	bl	dw_log
	mov	x0,E_NODEV
.out:	ldp	x19,x20,[sp],16
	epilogue

; travail periodique (toutes les DW_SCAN_MS au plus) : branchements et
; retraits sur le port racine puis sur les ports des hubs connus
dw_scruter:
	prologue
	adr	x0,dw_regs
	ldr	x0,[x0]
	cbz	x0,.out
	sys	SYS_TIME_MS
	adr	x1,dw_proch_scan
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.out
	add	x0,x0,DW_SCAN_MS
	str	x0,[x1]
	bl	dw_scruter_racine
	cbnz	x0,.out				; racine changee : hubs au prochain tour
	bl	dw_scruter_hubs
.out:	epilogue

; port racine (HPRT0) -> x0 = 1 s'il a change
dw_scruter_racine:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,dw_regs
	ldr	x19,[x19]
	ldr	w20,[x19,DWC_HPRT0]
	and	w0,w20,1
	adr	x1,dw_connecte
	ldr	w1,[x1]
	cmp	w0,w1
	b.ne	.change
	tbnz	w20,1,.change			; CONNDET : retire puis rebranche
	mov	x0,0
	b	.out
.change:
	mov	w1,DW_HPRT_ENA			; acquitter (W1C) sans ecrire 1 dans ENA
	bic	w0,w20,w1
	str	w0,[x19,DWC_HPRT0]
	adr	x1,dw_connecte
	ldr	w0,[x1]
	and	w2,w20,1
	str	w2,[x1]
	cbz	w0,.branch
	adr	x0,m_dw_retrait
	mov	x1,0
	mov	x2,1
	bl	dw_log_port
	mov	x0,0				; tout est derriere le port racine
	mov	x1,1
	bl	dw_detacher_port
.branch:
	tbz	w20,0,.fait
	adr	x0,m_dw_branch
	mov	x1,0
	mov	x2,1
	bl	dw_log_port
	bl	dw_port_reset
	tbnz	x0,63,.fait
	mov	x1,0
	mov	x2,1
	bl	dw_enumerer
.fait:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; ports de chaque hub connu : GET_STATUS, C_PORT_CONNECTION
dw_scruter_hubs:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,0				; indice du noeud
.noeud:	cmp	x19,DW_NOEUD_MAX
	b.hs	.out
	adr	x20,dw_noeuds
	add	x20,x20,x19,lsl 5		; (ND_TAILLE = 32)
	ldr	w21,[x20,ND_POIG]
	cbz	w21,.suiv
	ldr	w0,[x20,ND_HUB]
	cbz	w0,.suiv
	ldr	w22,[x20,ND_NPORTS]
	mov	x23,1
.port:	cmp	x23,x22
	b.hi	.suiv
	mov	x0,x21
	mov	x1,x23
	bl	dw_hub_statut
	tbnz	x0,63,.suiv			; hub parti : son parent s'en charge
	tbz	w0,16,.pn			; C_PORT_CONNECTION
	mov	x24,x0
	mov	x0,x21				; CLEAR_FEATURE(C_PORT_CONNECTION)
	usb_setup x1,0x23,USB_REQ_CLEAR_FEATURE,16,0,0
	orr	x1,x1,x23,lsl 32
	bl	dw_ctrl
	tbnz	w24,0,.branch
	adr	x0,m_dw_retrait			; plus rien sur ce port
	and	x1,x21,0x7F
	mov	x2,x23
	bl	dw_log_port
	and	x0,x21,0x7F
	mov	x1,x23
	bl	dw_detacher_port
	b	.pn
.branch:
	and	x0,x21,0x7F			; ce qui y etait (echange rapide)
	mov	x1,x23
	bl	dw_detacher_port
	cbz	x0,.b2
	adr	x0,m_dw_retrait
	and	x1,x21,0x7F
	mov	x2,x23
	bl	dw_log_port
.b2:	adr	x0,m_dw_branch
	and	x1,x21,0x7F
	mov	x2,x23
	bl	dw_log_port
	mov	x0,100				; stabilisation de la connexion
	sys	SYS_SLEEP
	mov	x0,x21
	mov	x1,x23
	bl	dw_hub_port_brancher
.pn:	add	x23,x23,1
	b	.port
.suiv:	add	x19,x19,1
	b	.noeud
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ----------------------------------------------------------------------------
;  Arbre des peripheriques
; ----------------------------------------------------------------------------

; -> x0 = noeud libre, 0 si l'arbre est plein
dw_noeud_libre:
	adr	x0,dw_noeuds
	mov	x1,0
.t:	cmp	x1,DW_NOEUD_MAX
	b.hs	.plein
	ldr	w2,[x0,ND_POIG]
	cbz	w2,.r
	add	x0,x0,ND_TAILLE
	add	x1,x1,1
	b	.t
.plein:	mov	x0,0
.r:	ret

; -> x0 = adresse (1..127) qu'aucun noeud n'utilise, < 0 si aucune ; tourne
; a partir de la derniere donnee (une adresse liberee resservira tard)
dw_adresse_libre:
	adr	x5,dw_next_addr
	ldr	w0,[x5]
	mov	x4,127				; essais
.essai:	add	w0,w0,1
	cmp	w0,127
	b.ls	.dans
	mov	w0,1
.dans:	adr	x1,dw_noeuds
	mov	x2,0
.t:	cmp	x2,DW_NOEUD_MAX
	b.hs	.libre
	ldr	w3,[x1,ND_POIG]
	cbz	w3,.n
	and	w3,w3,0x7F
	cmp	w3,w0
	b.eq	.pris
.n:	add	x1,x1,ND_TAILLE
	add	x2,x2,1
	b	.t
.pris:	subs	x4,x4,1
	b.ne	.essai
	mov	x0,-1
	ret
.libre:	str	w0,[x5]
	ret

; x0 = adresse du hub parent (0 : port racine), x1 = port, ou -1 pour tous :
; tout ce qui y est branche est detache -> x0 = noeuds detaches (ce niveau)
dw_detacher_port:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; indice
	mov	x22,0				; compte
.t:	cmp	x21,DW_NOEUD_MAX
	b.hs	.out
	adr	x0,dw_noeuds
	add	x0,x0,x21,lsl 5
	ldr	w1,[x0,ND_POIG]
	cbz	w1,.n
	ldr	w1,[x0,ND_PARENT]
	cmp	w1,w19
	b.ne	.n
	tbnz	x20,63,.det
	ldr	w1,[x0,ND_PORT]
	cmp	w1,w20
	b.ne	.n
.det:	bl	dw_detacher_noeud
	add	x22,x22,1
.n:	add	x21,x21,1
	b	.t
.out:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = noeud : ce qui est derriere (hub) d'abord, puis usbd_detacher, ses
; tuyaux et bascules oublies, noeud libere (son adresse peut resservir)
dw_detacher_noeud:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	w20,[x19,ND_POIG]
	and	w20,w20,0x7F			; adresse
	mov	x0,x20
	mov	x1,-1
	bl	dw_detacher_port
	ldr	w0,[x19,ND_USBD]
	cbz	w0,.tuy
	bl	usbd_detacher
.tuy:	adr	x0,dw_tuyaux
	mov	x1,0
.u:	cmp	x1,DW_TUY_MAX
	b.hs	.basc
	ldr	w2,[x0,TP_UTIL]
	cbz	w2,.un
	ldr	w2,[x0,TP_ADR]
	cmp	w2,w20
	b.ne	.un
	str	wzr,[x0,TP_UTIL]
.un:	add	x0,x0,TP_TAILLE
	add	x1,x1,1
	b	.u
.basc:	adr	x0,dw_bascules
	mov	x1,0
.b:	cmp	x1,DW_BASC_MAX
	b.hs	.libre
	ldr	w2,[x0]
	lsr	w2,w2,8
	cmp	w2,w20
	b.ne	.bn
	str	wzr,[x0]
	str	wzr,[x0,4]
.bn:	add	x0,x0,8
	add	x1,x1,1
	b	.b
.libre:	str	wzr,[x19,ND_POIG]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = message, x1 = hub parent (0 : racine), x2 = port :
; "usbd : dwc2 : <message>N (hub A)"
dw_log_port:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	bl	dw_log
	mov	x0,x20
	bl	print_dec
	cbz	x19,.nl
	adr	x0,m_dw_dhub
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_dw_parf
	bl	puts
.nl:	adr	x0,m_dw_nl
	bl	puts
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine : � usbd : dwc2 : � puis la chaine
dw_log:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,m_dw
	bl	puts
	mov	x0,x19
	bl	puts
	ldr	x19,[sp],16
	epilogue

; ----------------------------------------------------------------------------
;  Controleur
; ----------------------------------------------------------------------------

; -> x0 = 1 si le coeur DWC2 est present et initialise en mode hote
dw_coeur_init:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,dw_regs
	ldr	x19,[x19]
	ldr	w0,[x19,DWC_GSNPSID]
	lsr	w1,w0,16
	movl	w2,0x4F54			; 'OT' : identifiant Synopsys OTG
	cmp	w1,w2
	b.ne	.fail
	adr	x0,m_dw_coeur
	bl	dw_log
	ldr	w0,[x19,DWC_GSNPSID]
	mov	x1,8
	bl	print_hex
	adr	x0,m_dw_nl
	bl	puts

	str	wzr,[x19,DWC_PCGCCTL]		; horloges non gelees
	str	wzr,[x19,DWC_GAHBCFG]		; IRQ globale coupee pendant l'init
	movl	x20,100000			; AHB au repos
.idle:	ldr	w0,[x19,DWC_GRSTCTL]
	tbnz	w0,31,.idle_ok
	subs	x20,x20,1
	b.ne	.idle
.idle_ok:
	mov	w0,1				; reset logiciel du coeur (CSFTRST)
	str	w0,[x19,DWC_GRSTCTL]
	movl	x20,100000
.rst:	ldr	w0,[x19,DWC_GRSTCTL]
	tbz	w0,0,.rst_ok
	subs	x20,x20,1
	b.ne	.rst
.rst_ok:
	mov	x0,20
	sys	SYS_SLEEP

	ldr	w0,[x19,DWC_GUSBCFG]		; forcer le mode hote
	and	w0,w0,0xBFFFFFFF		; FDMOD = 0
	orr	w0,w0,1 shl 29			; FHMOD = 1
	str	w0,[x19,DWC_GUSBCFG]
	mov	x0,50
	sys	SYS_SLEEP

	mov	w0,1 shl 5			; DMA interne, IRQ globale coupee
	str	w0,[x19,DWC_GAHBCFG]
	ldr	w0,[x19,DWC_HCFG]		; PHY UTMI+ : FSLSPCLKSEL = 0
	and	w0,w0,0xFFFFFFFC
	str	w0,[x19,DWC_HCFG]

	mov	w0,0x200			; FIFO (en mots de 32 bits)
	str	w0,[x19,DWC_GRXFSIZ]
	movl	w0,(0x100 shl 16) or 0x200
	str	w0,[x19,DWC_GNPTXFSIZ]
	movl	w0,(0x100 shl 16) or 0x300
	str	w0,[x19,DWC_HPTXFSIZ]
	movl	w0,(0x10 shl 6) or (1 shl 5)	; vider toutes les FIFO TX
	str	w0,[x19,DWC_GRSTCTL]
	movl	x20,100000
.txf:	ldr	w0,[x19,DWC_GRSTCTL]
	tbz	w0,5,.txf_ok
	subs	x20,x20,1
	b.ne	.txf
.txf_ok:
	mov	w0,1 shl 4			; vider la FIFO RX
	str	w0,[x19,DWC_GRSTCTL]
	movl	x20,100000
.rxf:	ldr	w0,[x19,DWC_GRSTCTL]
	tbz	w0,4,.rxf_ok
	subs	x20,x20,1
	b.ne	.rxf
.rxf_ok:
	ldr	w0,[x19,DWC_HPRT0]		; alimentation du port
	mov	w1,DW_HPRT_W1C
	bic	w0,w0,w1
	orr	w0,w0,DW_HPRT_PWR
	str	w0,[x19,DWC_HPRT0]
	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; Attend un peripherique sur le port racine (1 s) puis le remet a zero
; -> x0 = vitesse (DW_SPEED_*), -1 si rien de branche
dw_port_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,dw_regs
	ldr	x19,[x19]
	mov	x20,100				; jusqu'a 1 s
.conn:	ldr	w0,[x19,DWC_HPRT0]
	tbnz	w0,0,.present
	mov	x0,10
	sys	SYS_SLEEP
	subs	x20,x20,1
	b.ne	.conn
	mov	x0,-1
	b	.out
.present:
	mov	x0,100				; stabilisation de la connexion
	sys	SYS_SLEEP
	ldr	w0,[x19,DWC_HPRT0]
	mov	w1,DW_HPRT_W1C
	bic	w0,w0,w1
	orr	w0,w0,DW_HPRT_RST
	str	w0,[x19,DWC_HPRT0]
	mov	x0,60				; reset USB >= 50 ms
	sys	SYS_SLEEP
	ldr	w0,[x19,DWC_HPRT0]
	mov	w1,DW_HPRT_W1C or DW_HPRT_RST
	bic	w0,w0,w1
	str	w0,[x19,DWC_HPRT0]
	mov	x20,50
.ena:	mov	x0,10
	sys	SYS_SLEEP
	ldr	w0,[x19,DWC_HPRT0]
	tbnz	w0,2,.enabled
	subs	x20,x20,1
	b.ne	.ena
	mov	x0,-1
	b	.out
.enabled:
	ubfx	x20,x0,17,2			; vitesse du port
	mov	w1,DW_HPRT_ENA			; acquitter les changements (sans couper ENA)
	bic	w0,w0,w1
	str	w0,[x19,DWC_HPRT0]
	adr	x0,m_dw_port
	bl	dw_log
	adr	x0,dw_noms_vitesse
	add	x0,x0,x20,lsl 3
	bl	puts
	adr	x0,m_dw_nl
	bl	puts
	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; ----------------------------------------------------------------------------
;  Transferts
; ----------------------------------------------------------------------------

; w0 = HCCHAR (sans CHENA), w1 = HCTSIZ, x2 = tampon (adresse physique),
; x3 = decalage du canal (n * 0x20), x4 = delai (ms)
; -> w0 = HCINT final (bit 31 force a 1 si delai depasse), w1 = HCTSIZ final
dw_xfer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	w19,w0
	mov	w20,w1
	mov	x22,x4
	adr	x21,dw_regs
	ldr	x21,[x21]
	add	x23,x21,x3			; registres du canal
	mov	w0,-1
	str	w0,[x23,DWC_HCINT]		; acquitter tout
	mov	w0,0x7FF
	str	w0,[x23,DWC_HCINTMSK]
	str	wzr,[x23,DWC_HCSPLT]
	orr	w0,w2,DW_BUS
	str	w0,[x23,DWC_HCDMA]
	str	w20,[x23,DWC_HCTSIZ]
	ubfx	w0,w19,18,2			; periodique : trame paire/impaire suivante
	cmp	w0,DW_EPTYPE_INTR
	b.ne	.go
	ldr	w0,[x21,DWC_HFNUM]
	tbnz	w0,0,.go
	orr	w19,w19,DW_HCCHAR_ODDFRM
.go:	orr	w0,w19,DW_HCCHAR_CHENA
	str	w0,[x23,DWC_HCCHAR]

	sys	SYS_TIME_MS
	add	x24,x0,x22
.wait:	ldr	w0,[x23,DWC_HCINT]
	tbnz	w0,1,.halted			; CHHLTD : fin (succes ou erreur)
	sys	SYS_TIME_MS
	cmp	x0,x24
	b.hs	.timeout
	sys	SYS_YIELD
	b	.wait
.timeout:
	ldr	w0,[x23,DWC_HCCHAR]		; arret force du canal
	orr	w0,w0,(1 shl 30) or DW_HCCHAR_CHENA
	str	w0,[x23,DWC_HCCHAR]
	sys	SYS_TIME_MS			; attendre l'arret (10 ms au plus)
	add	x24,x0,10
.h:	ldr	w0,[x23,DWC_HCINT]
	tbnz	w0,1,.h_ok
	sys	SYS_TIME_MS
	cmp	x0,x24
	b.lo	.h
.h_ok:	ldr	w19,[x23,DWC_HCINT]
	orr	w19,w19,1 shl 31
	b	.end
.halted:
	mov	w19,w0
.end:	ldr	w20,[x23,DWC_HCTSIZ]
	mov	w0,w19
	mov	w1,w20
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = HCINT -> x0 = 1 si transfert reussi (ne touche que x0, x1)
dw_xfer_ok:
	tbnz	w0,31,.ko			; delai depasse
	movl	w1,DW_HCINT_ERREURS
	tst	w0,w1
	b.ne	.ko
	tbz	w0,0,.ko			; XFERCOMPL absent
	mov	x0,1
	ret
.ko:	mov	x0,0
	ret

; x0 = poignee, x1 = paquet SETUP (bmRequestType | bRequest << 8 |
; wValue << 16 | wIndex << 32 | wLength << 48) ; donnees dans/depuis le
; tampon DMA (DW_O_BUF) -> x0 = octets transferes, -1 si erreur
dw_ctrl:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,dw_dma_va
	ldr	x0,[x0]
	str	x20,[x0,DW_O_SETUP]
	and	x0,x19,0x7F			; HCCHAR de base : EP0, controle
	ubfx	x1,x19,16,16
	orr	w21,w1,w0,lsl 22
	ubfx	x1,x19,8,8
	cmp	w1,DW_SPEED_LOW
	b.ne	.setup
	orr	w21,w21,DW_HCCHAR_LSPD
.setup:	mov	w0,w21				; --- etape SETUP
	movl	w1,8 or (1 shl 19) or (DW_PID_SETUP shl 29)
	adr	x2,dw_dma_phys
	ldr	x2,[x2]
	add	x2,x2,DW_O_SETUP
	mov	x3,0
	mov	x4,DW_DELAI
	bl	dw_xfer
	bl	dw_xfer_ok
	cbz	x0,.err

	lsr	x22,x20,48			; wLength
	mov	x23,0				; octets transferes
	cbz	x22,.status
	cmp	x22,DW_BUF
	b.hi	.err
	ubfx	x4,x19,16,16			; --- etape DATA (DATA1)
	add	x5,x22,x4
	sub	x5,x5,1
	udiv	x5,x5,x4			; nombre de paquets
	orr	w1,w22,w5,lsl 19
	orr	w1,w1,DW_PID_DATA1 shl 29
	mov	w0,w21
	tbz	x20,7,.data_out
	orr	w0,w0,DW_HCCHAR_EPIN
.data_out:
	adr	x2,dw_dma_phys
	ldr	x2,[x2]
	add	x2,x2,DW_O_BUF
	mov	x3,0
	mov	x4,DW_DELAI
	bl	dw_xfer
	mov	w24,w1
	bl	dw_xfer_ok
	cbz	x0,.err
	and	w24,w24,0x7FFFF			; reste a transferer
	sub	x23,x22,x24

.status:					; --- etape STATUS (sens oppose, DATA1)
	mov	w0,w21
	cbz	x22,.st_in
	tbnz	x20,7,.st_out
.st_in:	orr	w0,w0,DW_HCCHAR_EPIN
.st_out:
	movl	w1,(1 shl 19) or (DW_PID_DATA1 shl 29)
	adr	x2,dw_dma_phys
	ldr	x2,[x2]
	add	x2,x2,DW_O_BUF
	mov	x3,0
	mov	x4,DW_DELAI
	bl	dw_xfer
	bl	dw_xfer_ok
	cbz	x0,.err
	mov	x0,x23
	b	.out
.err:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = poignee, x1 = SETUP, x2 = tampon (memoire ordinaire), x3 = longueur
; -> x0 = octets transferes, ou < 0
dw_control:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	lsr	x0,x20,48			; wLength
	cmp	x0,DW_BUF
	b.hi	.inval
	tbnz	x20,7,.go			; sortie : vers le tampon DMA
	cmp	x22,x0
	csel	x2,x22,x0,lo
	adr	x0,dw_dma_va
	ldr	x0,[x0]
	add	x0,x0,DW_O_BUF
	mov	x1,x21
	bl	mem_move
.go:	mov	x0,x19
	mov	x1,x20
	bl	dw_ctrl
	tbnz	x0,63,.eio
	mov	x23,x0
	tbz	x20,7,.basc			; entree : vers l'appelant
	cmp	x23,x22
	csel	x2,x23,x22,lo
	mov	x0,x21
	adr	x1,dw_dma_va
	ldr	x1,[x1]
	add	x1,x1,DW_O_BUF
	bl	mem_move
.basc:	and	x0,x20,0xFFFF			; CLEAR_FEATURE(ENDPOINT_HALT) : DATA0
	cmp	x0,0x0102
	b.ne	.config
	ubfx	x0,x20,16,16
	cbnz	x0,.fin
	and	x0,x19,0x7F
	ubfx	x1,x20,32,8
	and	x1,x1,0xFF
	bl	dw_bascule_raz
	b	.fin
.config:
	cmp	x0,0x0900			; SET_CONFIGURATION : toutes a DATA0
	b.ne	.fin
	and	x0,x19,0x7F
	mov	x1,-1
	bl	dw_bascule_raz
.fin:	mov	x0,x23
	b	.out
.eio:	mov	x0,DW_EIO
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = cle (adresse << 8 | extremite) -> x0 = son entree (cle, PID suivant) ;
; creee a DATA0 au premier usage
dw_bascule:
	mov	w6,w0
	adr	x1,dw_bascules
	mov	x2,0
	mov	x3,0				; premiere place libre (0 : aucune)
.t:	cmp	x2,DW_BASC_MAX
	b.hs	.nouv
	ldr	w4,[x1]
	cmp	w4,w6
	b.eq	.trouve
	cbnz	w4,.n
	cbnz	x3,.n
	mov	x3,x1
.n:	add	x1,x1,8
	add	x2,x2,1
	b	.t
.trouve:
	mov	x0,x1
	ret
.nouv:	cbnz	x3,.place
	adr	x4,dw_basc_tour			; table pleine : une entree reprise
	ldr	w2,[x4]
	add	w5,w2,1
	and	w5,w5,DW_BASC_MAX - 1
	str	w5,[x4]
	adr	x3,dw_bascules
	add	x3,x3,x2,lsl 3
.place:	str	w6,[x3]
	str	wzr,[x3,4]
	mov	x0,x3
	ret

; x0 = adresse, x1 = extremite (bit 7 : IN), ou -1 pour toutes :
; bascules remises a DATA0 (bulk et tuyaux d'interruption)
dw_bascule_raz:
	adr	x2,dw_bascules
	mov	x3,0
.t:	cmp	x3,DW_BASC_MAX
	b.hs	.tuy
	ldr	w4,[x2]
	cbz	w4,.n
	lsr	w5,w4,8
	cmp	w5,w0
	b.ne	.n
	tbnz	x1,63,.raz
	and	w5,w4,0xFF
	cmp	w5,w1
	b.ne	.n
.raz:	str	wzr,[x2,4]
.n:	add	x2,x2,8
	add	x3,x3,1
	b	.t
.tuy:	adr	x2,dw_tuyaux
	mov	x3,0
.u:	cmp	x3,DW_TUY_MAX
	b.hs	.fin
	ldr	w4,[x2,TP_UTIL]
	cbz	w4,.m
	ldr	w4,[x2,TP_ADR]
	cmp	w4,w0
	b.ne	.m
	tbnz	x1,63,.raz2
	tbz	x1,7,.m				; (tuyaux IN seulement)
	ldr	w4,[x2,TP_EP]
	and	w5,w1,0xF
	cmp	w4,w5
	b.ne	.m
.raz2:	str	wzr,[x2,TP_BASC]
.m:	add	x2,x2,TP_TAILLE
	add	x3,x3,1
	b	.u
.fin:	ret

; x0 = poignee, x1 = extremite (bit 7 : IN) : bascule remise a DATA0
; (apres CLEAR_FEATURE(ENDPOINT_HALT))
dw_bulk_raz:
	and	x0,x0,0x7F
	and	x1,x1,0xFF
	b	dw_bascule_raz

; x0 = poignee, x1 = extremite (bit 7 : IN), x2 = taille max. de paquet,
; x3 = tampon DMA (adresse virtuelle), x4 = son adresse physique,
; x5 = longueur (<= 64 Ko) -> x0 = octets transferes, ou < 0 (canal 5)
dw_bulk:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	and	x21,x2,0x7FF
	mov	x22,x4				; position (physique)
	mov	x23,x5				; reste
	mov	x24,0				; fait
	cbz	x21,.inval
	mov	x0,0x10000
	cmp	x23,x0
	b.hi	.inval
	and	x0,x19,0x7F			; bascule de l'extremite
	and	x1,x20,0xFF
	orr	x0,x1,x0,lsl 8
	bl	dw_bascule
	mov	x25,x0
	and	x0,x20,0xF			; HCCHAR
	orr	w26,w21,w0,lsl 11
	tbz	x20,7,.out_ep
	orr	w26,w26,DW_HCCHAR_EPIN
.out_ep:
	orr	w26,w26,DW_EPTYPE_BULK shl 18
	and	x0,x19,0x7F
	orr	w26,w26,w0,lsl 22
	ubfx	x0,x19,8,8
	cmp	x0,DW_SPEED_LOW
	b.ne	.vit
	orr	w26,w26,DW_HCCHAR_LSPD
.vit:	lsl	x28,x21,8			; morceau : 256 paquets au plus
	sys	SYS_TIME_MS
	movl	x1,DW_DELAI_BULK
	add	x27,x0,x1			; echeance
.morceau:
	sys	SYS_TIME_MS
	subs	x4,x27,x0			; temps restant
	b.ls	.delai
	cmp	x23,x28
	csel	x19,x23,x28,ls			; n = ce morceau
	mov	x20,x19				; longueur programmee
	tbz	w26,15,.pk
	add	x20,x19,x21			; IN : multiple du paquet maximal
	sub	x20,x20,1
	udiv	x20,x20,x21
	mul	x20,x20,x21
.pk:	add	x5,x20,x21			; paquets (au moins un)
	sub	x5,x5,1
	udiv	x5,x5,x21
	cbnz	x5,.pk_ok
	mov	x5,1
.pk_ok:	orr	w1,w20,w5,lsl 19
	ldr	w2,[x25,4]
	orr	w1,w1,w2,lsl 29
	mov	w0,w26
	mov	x2,x22
	mov	x3,DW_CANAL_BULK
	bl	dw_xfer
	mov	w9,w0				; HCINT
	ubfx	w2,w1,29,2			; bascule DATA0/1 suivante
	str	w2,[x25,4]
	and	w3,w1,0x7FFFF
	sub	x11,x20,x3			; octets de ce morceau
	tbnz	w9,3,.stall
	tbnz	w9,31,.delai
	bl	dw_xfer_ok
	cbz	x0,.eio
	cmp	x11,x19
	csel	x11,x11,x19,ls
	add	x24,x24,x11
	add	x22,x22,x11
	sub	x23,x23,x11
	tbz	w26,15,.suite
	cmp	x11,x20				; paquet court : fin
	b.lo	.fini
.suite:	cbnz	x23,.morceau
.fini:	mov	x0,x24
	b	.out
.stall:	mov	x0,DW_EPIPE
	b	.out
.delai:	mov	x0,DW_ETIME
	b	.out
.eio:	mov	x0,DW_EIO
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ----------------------------------------------------------------------------
;  Lecture bulk IN sans attente (usbnet, docs/RESEAU.md etape 1) : le canal 6
;  reste arme (le controleur relance seul sur NAK) ; chaque appel regarde
;  s'il s'est arrete. Une seule extremite a la fois (la premiere qui le prend).
; ----------------------------------------------------------------------------
DW_CANAL_NB	= 6 * 0x20
DW_NB_MAX	= 4096

; x0 = poignee, x1 = extremite (bit 7 : IN), x2 = taille max. de paquet,
; x3 = octets voulus (<= 4096) -> x0 = octets recus (> 0), 0 si rien encore,
; < 0 erreur ; x1 = adresse des donnees (valides jusqu'au prochain appel)
dw_bulk_nb:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	and	x21,x2,0x7FF
	mov	x22,x3
	cbz	x21,.inval
	tbz	x20,7,.inval
	cbz	x22,.inval
	cmp	x22,DW_NB_MAX
	b.hi	.inval
	adr	x0,dw_nb_va			; zone DMA (une fois)
	ldr	x0,[x0]
	cbnz	x0,.zone
	mov	x0,DW_NB_MAX
	bl	dma_zone
	tbnz	x0,63,.out
	adr	x2,dw_nb_va
	stp	x0,x1,[x2]
.zone:	and	x0,x19,0x7F			; bascule de l'extremite
	and	x1,x20,0xFF
	orr	x23,x1,x0,lsl 8			; (cle : adresse << 8 | extremite)
	mov	x0,x23
	bl	dw_bascule
	mov	x25,x0
	adr	x24,dw_regs
	ldr	x24,[x24]
	add	x24,x24,DW_CANAL_NB		; registres du canal
	adr	x26,dw_nb_etat
	ldr	w0,[x26]			; arme ?
	cbz	w0,.armer
	ldr	w0,[x26,4]
	cmp	w0,w23
	b.ne	.occupe
	ldr	w0,[x24,DWC_HCINT]
	tbz	w0,1,.rien			; pas encore arrete : rien recu
	ldr	w1,[x24,DWC_HCTSIZ]
	str	wzr,[x26]			; desarme
	ubfx	w2,w1,29,2			; bascule DATA0/1 suivante
	str	w2,[x25,4]
	tbnz	w0,3,.stall
	and	w19,w1,0x7FFFF			; (reste)
	bl	dw_xfer_ok
	cbz	x0,.rien			; erreur passagere : on rearmera
	ldr	w0,[x26,8]
	sub	x0,x0,x19			; octets recus
	cbz	x0,.rien
	adr	x1,dw_nb_va
	ldr	x1,[x1]
	b	.out
.armer:	add	x0,x22,x21			; multiple du paquet maximal
	sub	x0,x0,1
	udiv	x0,x0,x21
	mul	x1,x0,x21
	str	w1,[x26,8]			; longueur programmee
	orr	w1,w1,w0,lsl 19			; paquets
	ldr	w2,[x25,4]
	orr	w1,w1,w2,lsl 29
	mov	w0,-1
	str	w0,[x24,DWC_HCINT]
	mov	w0,0x7FF
	str	w0,[x24,DWC_HCINTMSK]
	str	wzr,[x24,DWC_HCSPLT]
	adr	x0,dw_nb_phys
	ldr	x0,[x0]
	orr	w0,w0,DW_BUS
	str	w0,[x24,DWC_HCDMA]
	str	w1,[x24,DWC_HCTSIZ]
	and	x0,x20,0xF			; HCCHAR
	orr	w0,w21,w0,lsl 11
	orr	w0,w0,DW_HCCHAR_EPIN
	orr	w0,w0,DW_EPTYPE_BULK shl 18
	and	x1,x19,0x7F
	orr	w0,w0,w1,lsl 22
	ubfx	x1,x19,8,8
	cmp	x1,DW_SPEED_LOW
	b.ne	.v
	orr	w0,w0,DW_HCCHAR_LSPD
.v:	orr	w0,w0,DW_HCCHAR_CHENA
	mov	w1,1
	str	w1,[x26]
	str	w23,[x26,4]
	dmb	sy
	str	w0,[x24,DWC_HCCHAR]
.rien:	mov	x0,0
	b	.out
.occupe:
	mov	x0,-16				; (E_BUSY)
	b	.out
.stall:	mov	x0,DW_EPIPE
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Le canal 6 est arrete s'il est arme (son pilote s'est arrete)
dw_bulk_nb_stop:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,dw_nb_etat
	ldr	w0,[x19]
	cbz	w0,.out
	str	wzr,[x19]
	adr	x20,dw_regs
	ldr	x20,[x20]
	add	x20,x20,DW_CANAL_NB
	ldr	w0,[x20,DWC_HCINT]
	tbnz	w0,1,.out			; deja arrete
	ldr	w0,[x20,DWC_HCCHAR]		; arret force
	orr	w0,w0,(1 shl 30) or DW_HCCHAR_CHENA
	str	w0,[x20,DWC_HCCHAR]
	sys	SYS_TIME_MS
	add	x19,x0,10
.h:	ldr	w0,[x20,DWC_HCINT]
	tbnz	w0,1,.out
	sys	SYS_TIME_MS
	cmp	x0,x19
	b.lo	.h
.out:	ldp	x19,x20,[sp],16
	epilogue

; ----------------------------------------------------------------------------
;  Tuyaux d'interruption (IN)
; ----------------------------------------------------------------------------

; x0 = poignee, x1 = extremite (bit 7 : IN), x2 = taille max., x3 = intervalle
; (ms) -> x0 = tuyau, ou < 0
dw_intr_ouvrir:
	tbz	x1,7,.inval			; IN seulement
	and	x2,x2,0x7FF
	cbz	x2,.inval
	cmp	x2,DW_TUY_MPS
	mov	x4,DW_TUY_MPS
	csel	x2,x2,x4,ls
	cmp	x3,0
	csinc	x3,x3,xzr,ne			; intervalle 0 : 1 ms
	adr	x4,dw_tuyaux
	mov	x5,0
.t:	cmp	x5,DW_TUY_MAX
	b.hs	.plein
	ldr	w6,[x4,TP_UTIL]
	cbz	w6,.place
	add	x4,x4,TP_TAILLE
	add	x5,x5,1
	b	.t
.place:	mov	w6,1
	str	w6,[x4,TP_UTIL]
	and	x6,x0,0x7F
	str	w6,[x4,TP_ADR]
	and	x6,x1,0xF
	str	w6,[x4,TP_EP]
	ubfx	x6,x0,8,8
	str	w6,[x4,TP_VIT]
	str	w2,[x4,TP_MPS]
	str	wzr,[x4,TP_BASC]
	str	w3,[x4,TP_INTER]
	str	xzr,[x4,TP_PROCH]		; premiere lecture : tout de suite
	mov	x0,x5
	ret
.plein:	mov	x0,E_NOMEM
	ret
.inval:	mov	x0,E_INVAL
	ret

; x0 = tuyau -> x0 = octets recus (> 0), 0 si rien, < 0 erreur ;
; x1 = les donnees (valides jusqu'au prochain appel). Non bloquant : un
; transfert IN (canal 0) si l'intervalle est ecoule.
dw_intr_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	cmp	x0,DW_TUY_MAX
	b.hs	.inval
	mov	x20,x0
	adr	x19,dw_tuyaux
	mov	x1,TP_TAILLE
	madd	x19,x0,x1,x19
	ldr	w0,[x19,TP_UTIL]
	cbz	w0,.inval
	sys	SYS_TIME_MS
	ldr	x1,[x19,TP_PROCH]
	cmp	x0,x1
	b.lo	.rien
	ldr	w1,[x19,TP_INTER]
	add	x0,x0,x1
	str	x0,[x19,TP_PROCH]
	ldr	w0,[x19,TP_MPS]			; HCCHAR : IN, interruption
	ldr	w1,[x19,TP_EP]
	orr	w0,w0,w1,lsl 11
	orr	w0,w0,DW_HCCHAR_EPIN
	orr	w0,w0,(DW_EPTYPE_INTR shl 18) or DW_HCCHAR_EC1
	ldr	w1,[x19,TP_ADR]
	orr	w0,w0,w1,lsl 22
	ldr	w1,[x19,TP_VIT]
	cmp	w1,DW_SPEED_LOW
	b.ne	.fs
	orr	w0,w0,DW_HCCHAR_LSPD
.fs:	ldr	w1,[x19,TP_MPS]			; HCTSIZ : un paquet, PID selon bascule
	orr	w1,w1,1 shl 19
	ldr	w2,[x19,TP_BASC]
	cbz	w2,.pid0
	orr	w1,w1,DW_PID_DATA1 shl 29
.pid0:	adr	x2,dw_dma_phys
	ldr	x2,[x2]
	add	x2,x2,DW_O_TUY
	add	x2,x2,x20,lsl 6
	mov	x3,0
	mov	x4,DW_DELAI_INTR
	bl	dw_xfer
	tbnz	w0,31,.rien			; delai depasse : on reessaiera
	tbnz	w0,4,.rien			; NAK : rien de nouveau
	tbnz	w0,3,.stall
	mov	w2,(1 shl 2) or (1 shl 8)	; erreur AHB, babil
	tst	w0,w2
	b.ne	.eio
	mov	w21,w1
	bl	dw_xfer_ok			; erreur passagere : rien
	cbz	x0,.rien
	ldr	w0,[x19,TP_BASC]		; bascule DATA0/DATA1
	eor	w0,w0,1
	str	w0,[x19,TP_BASC]
	ldr	w1,[x19,TP_MPS]
	and	w21,w21,0x7FFFF
	sub	w0,w1,w21			; octets recus
	cbz	w0,.rien
	adr	x1,dw_dma_va
	ldr	x1,[x1]
	add	x1,x1,DW_O_TUY
	add	x1,x1,x20,lsl 6
	b	.out
.rien:	mov	x0,0
	b	.out
.stall:	mov	x0,DW_EPIPE
	b	.out
.eio:	mov	x0,DW_EIO
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tuyau : libere
dw_intr_fermer:
	cmp	x0,DW_TUY_MAX
	b.hs	.r
	adr	x1,dw_tuyaux
	mov	x2,TP_TAILLE
	madd	x1,x0,x2,x1
	str	wzr,[x1,TP_UTIL]
.r:	ret

; ----------------------------------------------------------------------------
;  Enumeration
; ----------------------------------------------------------------------------

; x0 = vitesse du peripherique (a l'adresse 0, port deja remis a zero),
; x1 = adresse du hub parent (0 : port racine), x2 = port sur le parent :
; adresse, descripteurs, configuration, noeud de l'arbre ; un hub est
; enumere ici, les autres sont annonces au coeur (usbd_attacher).
; Demarrage et branchement a chaud. -> x0 = 0, -1 si echec
dw_enumerer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	str	x27,[sp,-16]!
	sub	sp,sp,32			; descripteur de peripherique (18 o)
	mov	x24,x0				; vitesse
	mov	x25,x1
	mov	x26,x2
	cmp	w0,DW_SPEED_HIGH		; poignee : adresse 0, MPS0 8 ou 64
	mov	w1,8
	mov	w2,64
	csel	w1,w2,w1,eq
	lsl	x19,x24,8
	orr	x19,x19,x1,lsl 16

	mov	x0,x19				; GET_DESCRIPTOR(device, 8)
	usb_setup x1,0x80,USB_REQ_GET_DESCRIPTOR,0x0100,0,8
	bl	dw_ctrl
	tbnz	x0,63,.fail
	adr	x0,dw_dma_va
	ldr	x0,[x0]
	ldrb	w0,[x0,DW_O_BUF + 7]		; bMaxPacketSize0
	cbz	w0,.fail
	and	x19,x19,0xFFFF
	orr	x19,x19,x0,lsl 16

	bl	dw_noeud_libre			; noeud et adresse libres
	cbz	x0,.fail
	mov	x27,x0
	bl	dw_adresse_libre
	tbnz	x0,63,.fail
	mov	x20,x0
	orr	w0,w19,w20			; noeud pris des maintenant : l'adresse
	str	w0,[x27,ND_POIG]		; reste reservee meme si la suite echoue
	str	w25,[x27,ND_PARENT]		; (liberee au retrait du port)
	str	w26,[x27,ND_PORT]
	str	wzr,[x27,ND_USBD]
	str	wzr,[x27,ND_HUB]
	str	wzr,[x27,ND_NPORTS]
	mov	x0,x19				; SET_ADDRESS
	mov	x1,USB_REQ_SET_ADDRESS shl 8
	orr	x1,x1,x20,lsl 16
	bl	dw_ctrl
	tbnz	x0,63,.fail
	orr	x19,x19,x20
	mov	x0,10
	sys	SYS_SLEEP

	mov	x0,x19				; GET_DESCRIPTOR(device, 18)
	usb_setup x1,0x80,USB_REQ_GET_DESCRIPTOR,0x0100,0,18
	bl	dw_ctrl
	tbnz	x0,63,.fail
	mov	x0,sp
	adr	x1,dw_dma_va
	ldr	x1,[x1]
	add	x1,x1,DW_O_BUF
	mov	x2,18
	bl	mem_move
	ldrb	w21,[sp,4]			; bDeviceClass
	adr	x0,m_dw_adr			; � adresse n, VVVV:PPPP �
	bl	dw_log
	mov	x0,x20
	bl	print_dec
	adr	x0,m_dw_virg
	bl	puts
	ldrh	w0,[sp,8]
	mov	x1,4
	bl	print_hex
	adr	x0,m_dw_2p
	bl	puts
	ldrh	w0,[sp,10]
	mov	x1,4
	bl	print_hex
	adr	x0,m_dw_nl
	bl	puts

	mov	x0,x19				; GET_DESCRIPTOR(config, 9)
	usb_setup x1,0x80,USB_REQ_GET_DESCRIPTOR,0x0200,0,9
	bl	dw_ctrl
	tbnz	x0,63,.fail
	adr	x0,dw_dma_va
	ldr	x0,[x0]
	ldrh	w22,[x0,DW_O_BUF + 2]		; wTotalLength
	mov	w1,DW_BUF
	cmp	w22,w1
	csel	w22,w22,w1,ls
	mov	x0,x19				; configuration complete
	usb_setup x1,0x80,USB_REQ_GET_DESCRIPTOR,0x0200,0,0
	orr	x1,x1,x22,lsl 48
	bl	dw_ctrl
	tbnz	x0,63,.fail
	mov	x22,x0				; taille recue
	adr	x1,dw_dma_va
	ldr	x1,[x1]
	add	x1,x1,DW_O_BUF
	ldrb	w23,[x1,5]			; bConfigurationValue
	adr	x0,dw_cfg			; copie (le tampon DMA resservira)
	mov	x2,x22
	bl	mem_move

	mov	x0,x19				; SET_CONFIGURATION
	mov	x1,USB_REQ_SET_CONFIG shl 8
	orr	x1,x1,x23,lsl 16
	bl	dw_ctrl
	tbnz	x0,63,.fail

	cmp	w21,9				; hub : reste ici
	b.ne	.attacher
	mov	w0,1
	str	w0,[x27,ND_HUB]
	mov	x0,x19
	mov	x1,x27
	bl	dw_hub_enumerer
	b	.done
.attacher:
	mov	x0,0				; DWC2
	mov	x1,x19
	mov	x2,sp
	adr	x3,dw_cfg
	mov	x4,x22
	adr	x5,dw_vitesses
	ldrb	w5,[x5,x24]
	bl	usbd_attacher
	cmp	x0,0				; table du coeur pleine : 0
	csel	x0,x0,xzr,gt
	str	w0,[x27,ND_USBD]
.done:	mov	x0,0
	b	.out
.fail:	adr	x0,m_dw_enum_ko
	bl	dw_log
	mov	x0,-1
.out:	add	sp,sp,32
	ldr	x27,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = hub (poignee, deja configure), x1 = son noeud : alimente ses ports,
; remet a zero et enumere ce qui y est branche
dw_hub_enumerer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x22,x1
	usb_setup x1,0xA0,USB_REQ_GET_DESCRIPTOR,USB_DT_HUB shl 8,0,8
	bl	dw_ctrl
	tbnz	x0,63,.end
	adr	x0,dw_dma_va
	ldr	x0,[x0]
	ldrb	w20,[x0,DW_O_BUF + 2]		; bNbrPorts
	str	w20,[x22,ND_NPORTS]
	adr	x0,m_dw_hub
	bl	dw_log
	mov	x0,x20
	bl	print_dec
	adr	x0,m_dw_ports
	bl	puts

	mov	x21,1				; SET_FEATURE(PORT_POWER) sur chaque port
.pwr:	cmp	x21,x20
	b.hi	.pwr_done
	mov	x0,x19
	usb_setup x1,0x23,USB_REQ_SET_FEATURE,8,0,0
	orr	x1,x1,x21,lsl 32
	bl	dw_ctrl
	add	x21,x21,1
	b	.pwr
.pwr_done:
	mov	x0,100
	sys	SYS_SLEEP

	mov	x21,1
.port:	cmp	x21,x20
	b.hi	.end
	mov	x0,x19
	mov	x1,x21
	bl	dw_hub_statut
	tbnz	x0,63,.next
	tbz	w0,0,.next			; rien de branche
	mov	x0,x19
	mov	x1,x21
	bl	dw_hub_port_brancher
.next:	add	x21,x21,1
	b	.port
.end:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = hub (poignee), x1 = port (connecte) : SET_FEATURE(PORT_RESET), attente
; de C_PORT_RESET, puis enumeration du peripherique (parent : ce hub, ce
; port). Demarrage et branchement a chaud. -> x0 = 0, < 0 si echec
dw_hub_port_brancher:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	mov	x0,x19				; SET_FEATURE(PORT_RESET)
	usb_setup x1,0x23,USB_REQ_SET_FEATURE,4,0,0
	orr	x1,x1,x21,lsl 32
	bl	dw_ctrl
	mov	x22,10
.rst:	mov	x0,20
	sys	SYS_SLEEP
	mov	x0,x19
	mov	x1,x21
	bl	dw_hub_statut
	tbnz	x0,63,.ko
	tbnz	w0,20,.rst_done			; C_PORT_RESET
	tbz	w0,4,.rst_chk			; reset termine ?
	b	.rst_more
.rst_chk:
	tbnz	w0,1,.rst_done			; port active
.rst_more:
	subs	x22,x22,1
	b.ne	.rst
	b	.ko
.rst_done:
	mov	x20,x0				; statut
	mov	x0,x19				; CLEAR_FEATURE(C_PORT_RESET)
	usb_setup x1,0x23,USB_REQ_CLEAR_FEATURE,20,0,0
	orr	x1,x1,x21,lsl 32
	bl	dw_ctrl
	mov	x0,x19				; CLEAR_FEATURE(C_PORT_CONNECTION)
	usb_setup x1,0x23,USB_REQ_CLEAR_FEATURE,16,0,0
	orr	x1,x1,x21,lsl 32
	bl	dw_ctrl
	mov	x0,10
	sys	SYS_SLEEP
	mov	x0,DW_SPEED_FULL		; vitesse : bit 9 lente, bit 10 haute
	tbz	w20,9,.not_ls
	mov	x0,DW_SPEED_LOW
.not_ls:
	tbz	w20,10,.speed
	mov	x0,DW_SPEED_HIGH
.speed:	and	x1,x19,0x7F			; parent : ce hub, ce port
	mov	x2,x21
	bl	dw_enumerer
	b	.out
.ko:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = hub, x1 = port -> x0 = statut (wPortStatus | wPortChange << 16),
; negatif si erreur
dw_hub_statut:
	prologue
	usb_setup x2,0xA3,USB_REQ_GET_STATUS,0,0,4
	orr	x1,x2,x1,lsl 32
	bl	dw_ctrl
	tbnz	x0,63,.out
	adr	x0,dw_dma_va
	ldr	x0,[x0]
	ldr	w0,[x0,DW_O_BUF]
.out:	epilogue

; ----------------------------------------------------------------------------
dw_vitesses	db USB_HS, USB_FS, USB_LS	; DW_SPEED_* -> USB_*
		align 8
dw_noms_vitesse	db 'haute',0,0,0
		db 'pleine',0,0
		db 'lente',0,0,0
m_dw		db 'usbd : dwc2 : ',0
m_dw_coeur	db 'controleur GSNPSID=',0
m_dw_absent	db 'controleur absent',10,0
m_dw_alim	db 'alimentation, etat ',0
m_dw_vide	db 'aucun peripherique sur le port',10,0
m_dw_port	db 'port racine actif, vitesse ',0
m_dw_adr	db 'adresse ',0
m_dw_virg	db ', ',0
m_dw_2p		db ':',0
m_dw_hub	db 'hub, ',0
m_dw_ports	db ' ports',10,0
m_dw_enum_ko	db 'echec d',0x27,'enumeration',10,0
m_dw_branch	db 'branchement port ',0
m_dw_retrait	db 'retrait port ',0
m_dw_dhub	db ' (hub ',0
m_dw_parf	db ')',0
m_dw_nl		db 10,0
		align 8

; donnees (segment inscriptible de usbd)
macro dw_donnees {
		align 8
dw_regs		dd 0				; registres (adresse virtuelle)
dw_dma_va	dd 0				; zone DMA : adresse, physique
dw_dma_phys	dd 0
dw_proch_scan	dd 0				; prochaine scrutation des ports (ms)
dw_qemu		dw 0
dw_next_addr	dw 0				; derniere adresse donnee
dw_connecte	dw 0				; port racine : 1 branche
dw_basc_tour	dw 0
dw_nb_etat	dw 0, 0, 0, 0			; lecture sans attente : armee, cle, longueur
dw_nb_va	dd 0				; sa zone DMA : adresse, physique
dw_nb_phys	dd 0
dw_fw_rev	dw 28, 0, 0x00000001, 4, 0, 0, 0	; GET_FIRMWARE_REVISION
dw_fw_alim	dw 32, 0, 0x00028001, 8, 0, 3, 3, 0	; SET_POWER_STATE(USB, allume)
		align 8
dw_tuyaux	rb DW_TUY_MAX * TP_TAILLE
dw_bascules	rb DW_BASC_MAX * 8
dw_noeuds	rb DW_NOEUD_MAX * ND_TAILLE	; arbre des peripheriques
dw_cfg		rb DW_BUF
}
