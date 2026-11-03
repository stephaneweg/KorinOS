; ============================================================================
;  svc/usbd/xhci.inc - ports USB-A du Pi 4 : pont PCIe du BCM2711 + xHCI du VL805
;  (docs/USB.md : interface des controleurs). Repris de
;  src/kernel/arch/pi4/pcie.asm et usb_xhci.asm, en service :
;   * registres par SYS_DEV_MAP (pont 0xFD500000, BAR du xHCI par la fenetre
;     sortante 0x6_0000_0000 -> bus 0xF8000000) ;
;   * microcode du VL805 par SYS_FW_CALL (NOTIFY_XHCI_RESET) ;
;   * une zone DMA de 2 Mo (dma_zone, non cachee) ; adresse vue par le VL805
;     = adresse physique (fenetre entrante : bus 0 -> RAM 0, 1 Go) ;
;   * pas de thread : usbd appelle ; tout par scrutation.
;  Poignee = numero de slot. Les hubs restent ici (route, TT) ; chaque autre
;  peripherique est configure puis confie a usbd_attacher.
;  Arbre : chaque slot note son hub parent (0 : port racine), son port, son
;  numero usbd ; xh_scruter (toutes les 200 ms) suit les branchements et
;  retraits (PORTSC des ports racines, GET_STATUS des ports de hub).
;  Limites : pas de hub SuperSpeed.
; ============================================================================
		align 4

; x0 = adresse dans la zone -> adresse DMA (tmp : registre libre)
macro xh_dma reg*, tmp* {
	adr	tmp,xh_delta
	ldr	tmp,[tmp]
	add	reg,reg,tmp
}

; --- pont PCIe (deplacements depuis la base mappee) -------------------------
XH_PCIE_PHYS		= 0xFD500000
XH_PCIE_TAILLE		= 0x10000
PCIE_CLASS_REV		= 0x0008
PCIE_CMD		= 0x0004
PCIE_HDR_TYPE		= 0x000E
PCIE_CACHE_LINE		= 0x000C
PCIE_SEC_BUS		= 0x0019
PCIE_SUB_BUS		= 0x001A
PCIE_MEM_BASE		= 0x0020
PCIE_MEM_LIMIT		= 0x0022
PCIE_BRIDGE_CTL		= 0x003E
PCIE_CAP		= 0x00AC
PCIE_LNKCAP		= PCIE_CAP + 0x0C
PCIE_LNKSTA		= PCIE_CAP + 0x12
PCIE_RTCTL		= PCIE_CAP + 0x1C
PCIE_LNKCTL2		= PCIE_CAP + 0x30
PCIE_VENDOR_REG1	= 0x0188
PCIE_PRIV1_ID_VAL3	= 0x043C
PCIE_MISC_CTRL		= 0x4008
PCIE_WIN0_LO		= 0x400C
PCIE_WIN0_HI		= 0x4010
PCIE_BAR1_LO		= 0x402C
PCIE_BAR2_LO		= 0x4034
PCIE_BAR2_HI		= 0x4038
PCIE_BAR3_LO		= 0x403C
PCIE_STATUS		= 0x4068
PCIE_REVISION		= 0x406C
PCIE_WIN0_BASE_LIMIT	= 0x4070
PCIE_WIN0_BASE_HI	= 0x4080
PCIE_WIN0_LIMIT_HI	= 0x4084
PCIE_HARD_DEBUG		= 0x4204
PCIE_INTR2_CLR		= 0x4308
PCIE_INTR2_MASK_SET	= 0x4310
PCIE_EXT_CFG_DATA	= 0x8000
PCIE_EXT_CFG_INDEX	= 0x9000
PCIE_SW_INIT_1		= 0x9210
PCIE_D_BAR0		= 0x10			; config du peripherique (bus 1)
PCIE_D_BAR1		= 0x14
PCIE_D_INT_PIN		= 0x3D
PCIE_CMD_VALUE		= 0x0146		; MEMORY | MASTER | PARITY | SERR
PCIE_OUT_CPU		= 0x600000000
PCIE_OUT_BUS		= 0xF8000000
PCIE_OUT_SIZE		= 0x4000000
PCIE_IN_LOG2		= 30			; fenetre entrante : 1 Go
PCIE_VL805_BDF		= 0x00100000		; bus 1, dev 0, fonction 0
PCIE_XHCI_CLASS		= 0x0C0330

XH_BAR_PHYS	= PCIE_OUT_CPU
XH_BAR_TAILLE	= 0x10000		; capacites, operationnels, RT, sonnettes

XH_MAX_SLOTS	= 16
XH_RING_TRBS	= 64			; dernier TRB = lien
XH_EVT_TRBS	= 256
XH_DELAI_CMD	= 1000			; ms
XH_DELAI_CTRL	= 500
XH_DELAI_BULK	= 5000
XH_CFG_MAX	= 464
XH_TUY_MAX	= 16			; tuyaux d'interruption
XH_TUY_MPS	= 512			; un tampon de rapport (deux par tuyau)
XH_DMA_MAX	= 0x40000000		; fenetre entrante : 1 Go
XH_SCAN_MS	= 200			; scrutation des ports
XH_PROF_MAX	= 4			; profondeur maximale de l'arbre

XH_E_IO		= -5
XH_E_NOMEM	= -12
XH_E_BUSY	= -16
XH_E_NODEV	= -19
XH_E_INVAL	= -22
XH_E_PIPE	= -32			; STALL
XH_E_DELAI	= -110

; --- zone DMA (2 Mo) ----------------------------------------------------------
XH_Z_TAILLE	= 0x200000
XH_Z_DCBAA	= 0x0000		; (MaxSlotsEn+1) x 8
XH_Z_ERST	= 0x0200		; 1 segment
XH_Z_SPARR	= 0x0400		; tableau des tampons scratchpad (<= 128)
XH_Z_CMD	= 0x1000		; anneau de commandes
XH_Z_EVT	= 0x2000		; anneau d'evenements (256 TRB = 4 Ko)
XH_Z_INCTX	= 0x3000		; contexte d'entree (<= 33 x 64 o)
XH_Z_CBUF	= 0x4000		; tampon des transferts de controle (4 Ko)
XH_Z_REP	= 0x8000		; rapports : XH_TUY_MAX x 2 x 512 o
XH_Z_DEV	= 0x10000		; par slot : contexte peripherique (2 Ko),
XH_Z_DEV_TAILLE	= 0x9000		;   puis anneau du DCI d a +0x800+(d-1)x1 Ko
XH_Z_SCRATCH	= 0x100000		; pages scratchpad
XH_SCRATCH_MAX	= 128

; --- registres -----------------------------------------------------------------
XH_USBCMD	= 0x00			; operationnels
XH_USBSTS	= 0x04
XH_PAGESIZE	= 0x08
XH_CRCR		= 0x18
XH_DCBAAP	= 0x30
XH_CONFIG	= 0x38
XH_PORTSC	= 0x400
XH_IMAN		= 0x00			; interrupteur 0 (RT + 0x20)
XH_ERSTSZ	= 0x08
XH_ERSTBA	= 0x10
XH_ERDP		= 0x18
XH_PORTSC_KEEP	= 0x4E00FFE9		; bits RO + RWS : reecrits tels quels
XH_PORTSC_CHG	= 0x00FE0000		; bits de changement (RW1C)

; --- TRB -------------------------------------------------------------------------
XH_TRB_NORMAL	= 1
XH_TRB_SETUP	= 2
XH_TRB_DATA	= 3
XH_TRB_STATUS	= 4
XH_TRB_LINK	= 6
XH_TRB_ENABLE_SLOT = 9
XH_TRB_DISABLE_SLOT = 10
XH_TRB_ADDR_DEV	= 11
XH_TRB_CONFIG_EP = 12
XH_TRB_EVAL_CTX	= 13
XH_TRB_RESET_EP	= 14
XH_TRB_STOP_EP	= 15
XH_TRB_SET_DEQ	= 16
XH_TRB_NOOP	= 23
XH_TRB_EV_XFER	= 32
XH_TRB_EV_CMD	= 33
XH_TRB_IOC	= 1 shl 5
XH_TRB_ISP	= 1 shl 2
XH_TRB_IDT	= 1 shl 6
XH_CC_SUCCESS	= 1
XH_CC_STALL	= 6
XH_CC_SHORT	= 13
XH_CC_STOPPED	= 26
XH_CC_STOPPED2	= 27			; Stopped - Length Invalid

; --- anneau (producteur) : un par (slot, DCI) ------------------------------------
XH_RG_BASE	= 0
XH_RG_ENQ	= 8
XH_RG_CYC	= 16
XH_RG_PRET	= 24			; extremite configuree
XH_RG_TAILLE	= 32

; --- peripherique (un par slot) ---------------------------------------------------
XH_D_ROUTE	= 0
XH_D_SPEED	= 4
XH_D_RPORT	= 8
XH_D_TTSLOT	= 12
XH_D_TTPORT	= 16
XH_D_MPS0	= 20
XH_D_DEPTH	= 24
XH_D_MAXDCI	= 28
XH_D_ACTIF	= 32			; adresse : present dans l'arbre
XH_D_PARENT	= 36			; arbre : slot du hub parent (0 : port racine)
XH_D_PORT	= 40			;   port racine (profondeur 0) ou port du hub
XH_D_USBD	= 44			;   numero usbd (0 : hub ou non confie)
XH_D_HUB	= 48			;   1 : hub declare (ses ports sont scrutes)
XH_D_NPORTS	= 52			;   nombre de ports du hub
XH_D_TAILLE	= 64

; --- tuyau d'interruption ------------------------------------------------------------
XH_P_SLOT	= 0			; 0 : jamais servi
XH_P_DCI	= 4
XH_P_MPS	= 8
XH_P_OUVERT	= 12
XH_P_ARME	= 16			; un TRB en cours
XH_P_PRET	= 20			; 0 rien, 1 donnees, 2 erreur
XH_P_LONG	= 24
XH_P_CODE	= 28
XH_P_ERR	= 32			; erreurs de suite
XH_P_MORT	= 36
XH_P_COTE	= 40			; tampon arme (0/1)
XH_P_TAILLE	= 48

; vitesses (identifiants PSI par defaut ; usbd : vitesse - 1)
XH_S_FULL	= 1
XH_S_LOW	= 2
XH_S_HIGH	= 3
XH_S_SUPER	= 4

; ============================================================================
;  Point d'entree
; ============================================================================

; -> x0 = 0 (controleur pret, ports enumeres), < 0 si absent ou en echec
xh_init:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	xh_emule			; QEMU : pas de PCIe (une lecture fauterait)
	cbnz	x0,.absent
	movl	x0,XH_PCIE_PHYS
	mov	x1,XH_PCIE_TAILLE
	sys	SYS_DEV_MAP
	tbnz	x0,63,.absent
	adr	x1,xh_pcie
	str	x0,[x1]
	bl	xh_pcie_init
	cbz	x0,.nolink
	adr	x0,xh_m_link
	bl	puts
	movl	x0,XH_BAR_PHYS			; BAR0 du xHCI
	mov	x1,XH_BAR_TAILLE
	sys	SYS_DEV_MAP
	tbnz	x0,63,.fail
	adr	x1,xh_cap
	str	x0,[x1]
	movl	x0,XH_Z_TAILLE			; zone DMA
	bl	dma_zone
	tbnz	x0,63,.fail
	movl	x2,XH_Z_TAILLE			; sous 1 Go (fenetre entrante)
	add	x2,x1,x2
	movl	x3,XH_DMA_MAX
	cmp	x2,x3
	b.hi	.zone
	adr	x2,xh_zone
	str	x0,[x2]
	sub	x1,x1,x0
	adr	x2,xh_delta
	str	x1,[x2]
	bl	xh_hc_init
	cbz	x0,.fail
	bl	xh_scan_root
	adr	x0,xh_m_pret
	bl	puts
	mov	x0,0
	b	.out
.absent:
	adr	x0,xh_m_absent
	bl	puts
	mov	x0,XH_E_NODEV
	b	.out
.nolink:
	adr	x0,xh_m_nolink
	bl	puts
	mov	x0,XH_E_NODEV
	b	.out
.zone:	adr	x0,xh_m_zone
	bl	puts
.fail:	adr	x0,xh_m_fail
	bl	puts
	mov	x0,XH_E_IO
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 sous emulateur (revision du firmware < 0x50000000, comme
; board_is_emulated du noyau), 0 sinon ou inconnu
xh_emule:
	prologue
	adr	x0,xh_fw_msg
	mov	w1,32
	str	w1,[x0]
	str	wzr,[x0,4]
	mov	w1,1				; GET_FIRMWARE_REVISION
	str	w1,[x0,8]
	mov	w1,4
	str	w1,[x0,12]
	str	wzr,[x0,16]
	str	wzr,[x0,20]
	str	wzr,[x0,24]
	str	wzr,[x0,28]
	sys	SYS_FW_CALL
	tbnz	x0,63,.non
	adr	x0,xh_fw_msg
	ldr	w0,[x0,20]
	cbz	w0,.non
	movl	w1,0x50000000
	cmp	w0,w1
	cset	x0,lo
	b	.out
.non:	mov	x0,0
.out:	epilogue

; x0 = microsecondes (attente active sur le compteur generique, lisible en EL0)
xh_udelay:
	mrs	x1,cntfrq_el0
	mul	x0,x0,x1
	movl	x1,1000000
	udiv	x0,x0,x1
	mrs	x1,cntpct_el0
	add	x0,x0,x1
.w:	mrs	x1,cntpct_el0
	cmp	x1,x0
	b.lo	.w
	ret

; ============================================================================
;  Pont PCIe
; ============================================================================

; -> x0 = 1 si le lien est monte et le xHCI du VL805 active, 0 sinon
xh_pcie_init:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,xh_pcie
	ldr	x19,[x19]

	; --- remise a zero du pont et du lien -------------------------------
	movl	x20,PCIE_SW_INIT_1
	ldr	w0,[x19,x20]
	orr	w0,w0,2				; INIT_GENERIC : pont en reset
	str	w0,[x19,x20]
	ldr	w0,[x19,x20]
	orr	w0,w0,1				; PERST# : reset fondamental
	str	w0,[x19,x20]
	ldr	w0,[x19,x20]
	mov	x0,200
	bl	xh_udelay
	ldr	w0,[x19,x20]
	and	w0,w0,not 2			; pont hors reset (PERST# reste actif)
	str	w0,[x19,x20]
	ldr	w0,[x19,x20]
	movl	x20,PCIE_HARD_DEBUG
	ldr	w0,[x19,x20]
	and	w0,w0,not (1 shl 27)		; SERDES_IDDQ = 0 : PHY alimente
	str	w0,[x19,x20]
	ldr	w0,[x19,x20]
	mov	x0,200
	bl	xh_udelay

	; --- configuration du pont ------------------------------------------
	movl	x20,PCIE_MISC_CTRL
	ldr	w0,[x19,x20]
	orr	w0,w0,0x3000			; SCB_ACCESS_EN | CFG_READ_UR_MODE
	and	w0,w0,not 0x300000		; MAX_BURST_SIZE = 128 octets
	and	w0,w0,0x07FFFFFF		; SCB0_SIZE = log2(1 Go) - 15
	movl	w1,(PCIE_IN_LOG2-15) shl 27
	orr	w0,w0,w1
	str	w0,[x19,x20]
	movl	x20,PCIE_BAR2_LO		; fenetre entrante : bus 0, 1 Go
	mov	w0,PCIE_IN_LOG2-15
	str	w0,[x19,x20]
	movl	x20,PCIE_BAR2_HI
	str	wzr,[x19,x20]
	movl	x20,PCIE_BAR1_LO		; fenetres BAR1 / BAR3 coupees
	ldr	w0,[x19,x20]
	and	w0,w0,not 0x1F
	str	w0,[x19,x20]
	movl	x20,PCIE_BAR3_LO
	ldr	w0,[x19,x20]
	and	w0,w0,not 0x1F
	str	w0,[x19,x20]
	mov	w0,-1				; IRQ du pont : acquittees et masquees
	movl	x20,PCIE_INTR2_CLR
	str	w0,[x19,x20]
	ldr	w1,[x19,x20]
	movl	x20,PCIE_INTR2_MASK_SET
	str	w0,[x19,x20]
	ldr	w1,[x19,x20]
	ldr	w0,[x19,PCIE_LNKCAP]		; Gen2 (5 GT/s) maximum
	and	w0,w0,not 0xF
	orr	w0,w0,2
	str	w0,[x19,PCIE_LNKCAP]
	ldrh	w0,[x19,PCIE_LNKCTL2]
	and	w0,w0,not 0xF
	orr	w0,w0,2
	strh	w0,[x19,PCIE_LNKCTL2]

	; --- PERST# relache, attente du lien ---------------------------------
	movl	x20,PCIE_SW_INIT_1
	ldr	w0,[x19,x20]
	and	w0,w0,not 1
	str	w0,[x19,x20]
	mov	x0,100				; 100 ms (spec CEM)
	sys	SYS_SLEEP
	mov	x20,20				; puis jusqu'a 100 ms de plus
.link:	movl	x1,PCIE_STATUS
	ldr	w0,[x19,x1]
	and	w0,w0,0x30			; PHYLINKUP | DL_ACTIVE
	cmp	w0,0x30
	b.eq	.up
	mov	x0,5
	sys	SYS_SLEEP
	subs	x20,x20,1
	b.ne	.link
	b	.fail				; pas de lien (VL805 absent)
.up:	movl	x1,PCIE_STATUS
	ldr	w0,[x19,x1]
	tbz	w0,7,.fail			; PORT = 1 : mode complexe racine

	; --- fenetre sortante 0x6_0000_0000 -> 0xF8000000 --------------------
	movl	x20,PCIE_WIN0_LO
	movl	w0,PCIE_OUT_BUS
	str	w0,[x19,x20]
	movl	x20,PCIE_WIN0_HI
	str	wzr,[x19,x20]
	movl	x20,PCIE_WIN0_BASE_LIMIT
	ldr	w0,[x19,x20]			; BASE[15:4] et LIMIT[31:20] (Mo)
	movl	w1,0xFFF0
	bic	w0,w0,w1
	movl	w1,0xFFF00000
	bic	w0,w0,w1
	movl	w1,(((PCIE_OUT_CPU shr 20) and 0xFFF) shl 4) or ((((PCIE_OUT_CPU + PCIE_OUT_SIZE - 1) shr 20) and 0xFFF) shl 20)
	orr	w0,w0,w1
	str	w0,[x19,x20]
	movl	x20,PCIE_WIN0_BASE_HI
	ldr	w0,[x19,x20]
	and	w0,w0,not 0xFF
	orr	w0,w0,(PCIE_OUT_CPU shr 32) and 0xFF
	str	w0,[x19,x20]
	movl	x20,PCIE_WIN0_LIMIT_HI
	ldr	w0,[x19,x20]
	and	w0,w0,not 0xFF
	orr	w0,w0,((PCIE_OUT_CPU + PCIE_OUT_SIZE - 1) shr 32) and 0xFF
	str	w0,[x19,x20]

	ldr	w0,[x19,PCIE_PRIV1_ID_VAL3]	; le RC se presente en pont PCI-PCI
	and	w0,w0,0xFF000000
	movl	w1,0x060400
	orr	w0,w0,w1
	str	w0,[x19,PCIE_PRIV1_ID_VAL3]
	ldr	w0,[x19,PCIE_PRIV1_ID_VAL3]
	ldr	w0,[x19,PCIE_VENDOR_REG1]	; ENDIAN_MODE_BAR2 = donnees (0)
	and	w0,w0,not 0xC
	str	w0,[x19,PCIE_VENDOR_REG1]
	ldr	w0,[x19,PCIE_VENDOR_REG1]
	movl	x20,PCIE_HARD_DEBUG
	ldr	w0,[x19,x20]
	orr	w0,w0,2				; CLKREQ_DEBUG_ENABLE
	str	w0,[x19,x20]
	ldr	w0,[x19,x20]

	; --- pont (bus 0, dev 0) : registres de config directs ---------------
	ldr	w0,[x19,PCIE_CLASS_REV]
	lsr	w0,w0,8
	movl	w1,0x060400
	cmp	w0,w1
	b.ne	.fail
	ldrb	w0,[x19,PCIE_HDR_TYPE]
	and	w0,w0,0x7F
	cmp	w0,1
	b.ne	.fail
	mov	w0,16				; ligne de cache : 64 octets
	strb	w0,[x19,PCIE_CACHE_LINE]
	mov	w0,1
	strb	w0,[x19,PCIE_SEC_BUS]
	strb	w0,[x19,PCIE_SUB_BUS]
	mov	w0,PCIE_OUT_BUS shr 16		; fenetre memoire du pont : 0xF800xxxx
	strh	w0,[x19,PCIE_MEM_BASE]
	strh	w0,[x19,PCIE_MEM_LIMIT]
	mov	w0,1				; PARITY
	strb	w0,[x19,PCIE_BRIDGE_CTL]
	mov	w0,0x10				; CRS Software Visibility
	strb	w0,[x19,PCIE_RTCTL]
	mov	w0,PCIE_CMD_VALUE
	strh	w0,[x19,PCIE_CMD]

	; --- microcode du VL805 par le VideoCore (NOTIFY_XHCI_RESET) ----------
	adr	x0,xh_fw_msg
	mov	w1,32
	str	w1,[x0]
	str	wzr,[x0,4]
	movl	w1,0x00030058
	str	w1,[x0,8]
	mov	w1,4
	str	w1,[x0,12]
	str	wzr,[x0,16]
	movl	w1,PCIE_VL805_BDF
	str	w1,[x0,20]
	str	wzr,[x0,24]
	str	wzr,[x0,28]
	sys	SYS_FW_CALL			; reponse ignoree (vieux firmwares : EEPROM)
	mov	x0,10
	sys	SYS_SLEEP

	; --- peripherique 01:00.0 = xHCI ---------------------------------------
	movl	x20,PCIE_EXT_CFG_INDEX
	movl	w0,PCIE_VL805_BDF
	str	w0,[x19,x20]
	ldr	w0,[x19,x20]			; index pris en compte
	movl	x20,PCIE_EXT_CFG_DATA
	add	x20,x19,x20			; fenetre de config du peripherique
	ldr	w0,[x20,PCIE_CLASS_REV]
	lsr	w0,w0,8
	movl	w1,PCIE_XHCI_CLASS
	cmp	w0,w1
	b.ne	.fail
	ldrb	w0,[x20,PCIE_HDR_TYPE]
	and	w0,w0,0x7F
	cbnz	w0,.fail
	mov	w0,16
	strb	w0,[x20,PCIE_CACHE_LINE]
	movl	w0,PCIE_OUT_BUS or 4		; BAR0 memoire 64 bits
	str	w0,[x20,PCIE_D_BAR0]
	str	wzr,[x20,PCIE_D_BAR1]
	ldrb	w0,[x20,PCIE_D_INT_PIN]		; INTA (pas utilise : scrutation)
	cmp	w0,1
	b.eq	.pin
	mov	w0,1
	strb	w0,[x20,PCIE_D_INT_PIN]
.pin:	mov	w0,PCIE_CMD_VALUE
	strh	w0,[x20,PCIE_CMD]
	ldrh	w0,[x20,PCIE_CMD]
	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; ============================================================================
;  Controleur
; ============================================================================

; -> x0 = 1 si le controleur tourne
xh_hc_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,xh_cap
	ldr	x19,[x19]
	ldr	w0,[x19]			; CAPLENGTH | HCIVERSION << 16
	cmn	w0,1
	b.eq	.bad
	lsr	w1,w0,16
	cmp	w1,0x96
	b.lo	.bad
	cmp	w1,0x120
	b.hi	.bad
	and	w21,w0,0xFF
	adr	x0,xh_m_ver
	bl	puts
	ldr	w0,[x19]
	lsr	w0,w0,16
	mov	x1,4
	bl	print_hex
	adr	x0,xh_m_nl
	bl	puts
	add	x20,x19,x21			; operationnels
	adr	x1,xh_op
	str	x20,[x1]
	movl	x2,XH_BAR_TAILLE - 0x400	; tout doit tenir dans la fenetre mappee
	ldr	w0,[x19,0x14]			; DBOFF
	and	x0,x0,not 3
	cmp	x0,x2
	b.hs	.bad
	add	x0,x19,x0
	adr	x1,xh_db
	str	x0,[x1]
	ldr	w0,[x19,0x18]			; RTSOFF
	and	x0,x0,not 0x1F
	cmp	x0,x2
	b.hs	.bad
	add	x0,x19,x0
	add	x0,x0,0x20			; interrupteur 0
	adr	x1,xh_ir0
	str	x0,[x1]
	ldr	w0,[x19,4]			; HCSPARAMS1
	lsr	w1,w0,24
	adr	x2,xh_nports
	str	x1,[x2]
	add	x2,x21,XH_PORTSC
	add	x2,x2,x1,lsl 4
	movl	x3,XH_BAR_TAILLE
	cmp	x2,x3
	b.hi	.bad
	and	w0,w0,0xFF
	mov	w1,XH_MAX_SLOTS
	cmp	w0,w1
	csel	w0,w0,w1,lo
	adr	x2,xh_nslots
	str	x0,[x2]
	ldr	w0,[x19,0x10]			; HCCPARAMS1.CSZ
	mov	x1,32
	tbz	w0,2,.csz
	mov	x1,64
.csz:	adr	x2,xh_ctx
	str	x1,[x2]
	ldr	w0,[x19,8]			; HCSPARAMS2 : scratchpad (hi << 5 | lo)
	ubfx	w1,w0,21,5
	ubfx	w2,w0,27,5
	orr	w21,w2,w1,lsl 5

	; --- arret puis remise a zero ------------------------------------------
	ldr	w0,[x20,XH_USBCMD]
	and	w0,w0,not 1
	str	w0,[x20,XH_USBCMD]
	mov	x0,XH_USBSTS
	mov	x1,1				; attendre HCH = 1
	mov	x2,1
	mov	x3,100
	bl	xh_wait_reg
	cbz	x0,.bad
	mov	w0,2				; HCRST
	str	w0,[x20,XH_USBCMD]
	mov	x0,10
	sys	SYS_SLEEP
	mov	x0,XH_USBCMD			; HCRST retombe
	mov	x1,2
	mov	x2,0
	mov	x3,1000
	bl	xh_wait_reg
	cbz	x0,.bad
	mov	x0,XH_USBSTS			; Controller Not Ready = 0
	mov	x1,1 shl 11
	mov	x2,0
	mov	x3,1000
	bl	xh_wait_reg
	cbz	x0,.bad
	ldr	w0,[x20,XH_PAGESIZE]		; pages de 4 Ko obligatoires
	tbz	w0,0,.bad

	; --- slots, DCBAA, scratchpad ------------------------------------------
	adr	x0,xh_nslots
	ldr	w0,[x0]
	str	w0,[x20,XH_CONFIG]
	adr	x22,xh_zone
	ldr	x22,[x22]
	cmp	w21,XH_SCRATCH_MAX
	b.hi	.bad
	add	x0,x22,XH_Z_SPARR
	movl	x1,XH_Z_SCRATCH
	add	x1,x22,x1
	mov	w2,0
.sp:	cmp	w2,w21
	b.hs	.sp_done
	add	x3,x1,x2,lsl 12
	xh_dma	x3,x4
	str	x3,[x0,x2,lsl 3]
	add	w2,w2,1
	b	.sp
.sp_done:
	cbz	w21,.no_sp
	xh_dma	x0,x4
	str	x0,[x22,XH_Z_DCBAA]		; DCBAA[0] = tableau scratchpad
.no_sp:	add	x0,x22,XH_Z_DCBAA
	xh_dma	x0,x4
	dsb	sy
	str	w0,[x20,XH_DCBAAP]
	lsr	x0,x0,32
	str	w0,[x20,XH_DCBAAP+4]

	; --- anneau de commandes -------------------------------------------------
	adr	x0,xh_cmd_ring
	movl	x1,XH_Z_CMD
	add	x1,x22,x1
	bl	xh_ring_init
	movl	x0,XH_Z_CMD
	add	x0,x22,x0
	xh_dma	x0,x4
	dsb	sy
	orr	w1,w0,1				; RCS = 1
	str	w1,[x20,XH_CRCR]
	lsr	x0,x0,32
	str	w0,[x20,XH_CRCR+4]

	; --- anneau d'evenements (interrupteur 0, sans IRQ) ---------------------
	movl	x0,XH_Z_EVT
	add	x0,x22,x0
	xh_dma	x0,x4
	add	x1,x22,XH_Z_ERST
	str	x0,[x1]				; base du segment
	mov	w2,XH_EVT_TRBS
	str	w2,[x1,8]
	str	wzr,[x1,12]
	adr	x2,xh_evt_deq
	str	xzr,[x2]
	mov	x3,1
	str	x3,[x2,8]			; xh_evt_cyc
	adr	x2,xh_ir0
	ldr	x2,[x2]
	dsb	sy
	mov	w3,1
	str	w3,[x2,XH_ERSTSZ]
	str	w0,[x2,XH_ERDP]
	lsr	x3,x0,32
	str	w3,[x2,XH_ERDP+4]
	xh_dma	x1,x4
	str	w1,[x2,XH_ERSTBA]		; ERSTBA en dernier
	lsr	x1,x1,32
	str	w1,[x2,XH_ERSTBA+4]
	mov	w3,1				; IP acquitte, IE = 0
	str	w3,[x2,XH_IMAN]

	; --- marche ----------------------------------------------------------------
	ldr	w0,[x20,XH_USBCMD]
	orr	w0,w0,1
	str	w0,[x20,XH_USBCMD]
	mov	x0,XH_USBSTS			; HCH retombe
	mov	x1,1
	mov	x2,0
	mov	x3,100
	bl	xh_wait_reg
	cbz	x0,.bad
	mov	x1,0				; essai : commande No Op
	mov	w2,0
	mov	w3,XH_TRB_NOOP shl 10
	bl	xh_cmd
	cmp	x0,XH_CC_SUCCESS
	b.ne	.bad
	mov	x0,100				; laisser les ports s'etablir
	sys	SYS_SLEEP
	mov	x0,1
	b	.out
.bad:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = registre operationnel, x1 = masque, x2 = 0 ou 1 (valeur attendue des
; bits : tous a 0 / au moins un a 1), x3 = delai ms -> x0 = 1 si atteint
xh_wait_reg:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
.w:	adr	x0,xh_op
	ldr	x0,[x0]
	ldr	w0,[x0,x19]
	tst	w0,w20
	cset	x0,ne
	cmp	x0,x21
	b.eq	.ok
	subs	x22,x22,1
	b.lo	.no
	mov	x0,1
	sys	SYS_SLEEP
	b	.w
.ok:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse, x1 = octets (multiple de 16) : mise a zero
xh_zero:
	cbz	x1,.f
.b:	stp	xzr,xzr,[x0],16
	subs	x1,x1,16
	b.hi	.b
.f:	ret

; ============================================================================
;  Anneaux
; ============================================================================

; x0 = structure d'anneau, x1 = memoire (1 Ko dans la zone)
xh_ring_init:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x20
	mov	x1,XH_RING_TRBS*16
	bl	xh_zero
	add	x0,x20,(XH_RING_TRBS-1)*16	; TRB de lien vers le debut, TC = 1
	mov	x1,x20
	xh_dma	x1,x2
	str	x1,[x0]
	str	wzr,[x0,8]
	mov	w1,(XH_TRB_LINK shl 10) or 2
	str	w1,[x0,12]
	str	x20,[x19,XH_RG_BASE]
	str	xzr,[x19,XH_RG_ENQ]
	mov	x1,1
	str	x1,[x19,XH_RG_CYC]
	dsb	sy
	ldp	x19,x20,[sp],16
	epilogue

; x0 = anneau, x1 = parametre, w2 = statut, w3 = controle (sans cycle)
; -> x0 = TRB ecrit
xh_ring_push:
	ldr	x4,[x0,XH_RG_BASE]
	ldr	x5,[x0,XH_RG_ENQ]
	ldr	x6,[x0,XH_RG_CYC]
	add	x7,x4,x5,lsl 4
	str	x1,[x7]
	str	w2,[x7,8]
	orr	w8,w3,w6
	dmb	oshst				; parametre/statut visibles avant le cycle
	str	w8,[x7,12]
	add	x5,x5,1
	cmp	x5,XH_RING_TRBS-1
	b.ne	.st
	add	x8,x4,(XH_RING_TRBS-1)*16	; le lien prend le cycle courant
	ldr	w9,[x8,12]
	and	w9,w9,not 1
	orr	w9,w9,w6
	dmb	oshst
	str	w9,[x8,12]
	eor	x6,x6,1				; TC : le cycle producteur bascule
	mov	x5,0
.st:	str	x5,[x0,XH_RG_ENQ]
	str	x6,[x0,XH_RG_CYC]
	mov	x0,x7
	ret

; x0 = numero de sonnette (0 : commandes), w1 = cible
xh_ring_bell:
	adr	x2,xh_db
	ldr	x2,[x2]
	dsb	sy
	str	w1,[x2,x0,lsl 2]
	ret

; w0 = slot, w1 = DCI -> x0 = structure d'anneau
xh_anneau:
	adr	x2,xh_anneaux
	add	x0,x1,x0,lsl 5
	add	x0,x2,x0,lsl 5			; XH_RG_TAILLE = 32
	ret

; ============================================================================
;  Evenements
; ============================================================================

; -> x0 = evenement suivant (copie dans xh_ev), 0 si aucun ; l'anneau avance
xh_evt_take:
	adr	x9,xh_evt_deq
	ldp	x1,x2,[x9]			; index, cycle
	adr	x3,xh_zone
	ldr	x3,[x3]
	movl	x4,XH_Z_EVT
	add	x3,x3,x4
	add	x4,x3,x1,lsl 4
	ldr	w5,[x4,12]
	and	w5,w5,1
	cmp	x5,x2
	b.ne	.none
	dmb	oshld				; le reste du TRB apres le cycle
	ldp	x5,x6,[x4]
	adr	x7,xh_ev
	stp	x5,x6,[x7]
	add	x1,x1,1
	cmp	x1,XH_EVT_TRBS
	b.lo	.keep
	mov	x1,0
	eor	x2,x2,1
.keep:	stp	x1,x2,[x9]
	add	x4,x3,x1,lsl 4			; ERDP = prochain a lire, EHB acquitte
	xh_dma	x4,x6
	orr	x4,x4,8
	adr	x5,xh_ir0
	ldr	x5,[x5]
	str	w4,[x5,XH_ERDP]
	lsr	x4,x4,32
	str	w4,[x5,XH_ERDP+4]
	adr	x0,xh_ev
	ret
.none:	mov	x0,0
	ret

; Traite un evenement deja pris (xh_ev) qui n'etait pas attendu : transferts
; des tuyaux d'interruption ; changements de port ignores ici (PORTSC est
; relu par xh_scan_racines) ; slots disparus ignores
xh_dispatch:
	prologue
	adr	x0,xh_ev
	ldr	w1,[x0,12]
	ubfx	w2,w1,10,6
	cmp	w2,XH_TRB_EV_XFER
	b.ne	.out
	lsr	w0,w1,24			; slot
	sub	w2,w0,1
	cmp	w2,XH_MAX_SLOTS
	b.hs	.out
	adr	x2,xh_devs
	add	x2,x2,x0,lsl 6
	ldr	w2,[x2,XH_D_ACTIF]
	cbz	w2,.out
	ubfx	w1,w1,16,5			; DCI
	adr	x2,xh_ev
	ldr	w3,[x2,8]
	lsr	w2,w3,24			; code
	and	w3,w3,0xFFFFFF			; reste
	bl	xh_tuyau_evt			; (garde w0-w3)
	bl	iso_xh_evt			; tuyaux isochrones (iso.inc)
.out:	epilogue

; w0 = slot, w1 = DCI, w2 = code, w3 = octets non transferes : note dans le
; tuyau arme de cette extremite
xh_tuyau_evt:
	cmp	w2,XH_CC_STOPPED		; (arret demande : rien a noter)
	b.eq	.out
	cmp	w2,XH_CC_STOPPED2
	b.eq	.out
	adr	x4,xh_tuyaux
	mov	x5,0
.t:	cmp	x5,XH_TUY_MAX
	b.hs	.out
	ldr	w6,[x4,XH_P_ARME]
	cbz	w6,.n
	ldr	w6,[x4,XH_P_SLOT]
	cmp	w6,w0
	b.ne	.n
	ldr	w6,[x4,XH_P_DCI]
	cmp	w6,w1
	b.ne	.n
	str	wzr,[x4,XH_P_ARME]
	str	w2,[x4,XH_P_CODE]
	cmp	w2,XH_CC_SUCCESS
	b.eq	.ok
	cmp	w2,XH_CC_SHORT
	b.eq	.ok
	mov	w6,2
	str	w6,[x4,XH_P_PRET]
	ret
.ok:	ldr	w6,[x4,XH_P_MPS]
	subs	w6,w6,w3
	csel	w6,wzr,w6,lo
	str	w6,[x4,XH_P_LONG]
	mov	w6,1
	str	w6,[x4,XH_P_PRET]
	ret
.n:	add	x4,x4,XH_P_TAILLE
	add	x5,x5,1
	b	.t
.out:	ret

; Vide l'anneau d'evenements (appele par la boucle de usbd) ; toutes les
; 200 ms : branchements et retraits (ports racines, puis ports des hubs)
xh_scruter:
	prologue
.e:	bl	xh_evt_take
	cbz	x0,.temps
	bl	xh_dispatch
	b	.e
.temps:	sys	SYS_TIME_MS
	adr	x1,xh_t_scan
	ldr	x2,[x1]
	sub	x3,x0,x2
	cmp	x3,XH_SCAN_MS
	b.lo	.out
	str	x0,[x1]
	bl	xh_scan_racines
	bl	xh_scan_hubs
	sys	SYS_TIME_MS			; (une enumeration a pu durer)
	adr	x1,xh_t_scan
	str	x0,[x1]
.out:	epilogue

; x0 = 0 commande / 1 transfert, x1 = slot, x2 = DCI (transfert), x3 = delai ms
; -> x0 = code de completion (-1 si delai depasse), x1 = slot (commande) ou
; octets non transferes (transfert), x2 = TRB concerne (adresse DMA).
; Les autres evenements sont traites au passage.
xh_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x23,x3
	sys	SYS_TIME_MS
	add	x22,x0,x23
.e:	bl	xh_evt_take
	cbz	x0,.idle
	ldr	w1,[x0,12]
	ubfx	w2,w1,10,6
	cbnz	x19,.xfer
	cmp	w2,XH_TRB_EV_CMD
	b.ne	.other
	ldr	w3,[x0,8]
	lsr	w3,w3,24
	lsr	w1,w1,24
	ldr	x2,[x0]
	mov	x0,x3
	b	.out
.xfer:	cmp	w2,XH_TRB_EV_XFER
	b.ne	.other
	lsr	w3,w1,24
	cmp	w3,w20
	b.ne	.other
	ubfx	w3,w1,16,5
	cmp	w3,w21
	b.ne	.other
	ldr	x2,[x0]
	ldr	w1,[x0,8]
	lsr	w0,w1,24
	and	w1,w1,0xFFFFFF
	b	.out
.other:	bl	xh_dispatch
	b	.e
.idle:	sys	SYS_TIME_MS
	cmp	x0,x22
	b.hs	.late
	mov	x0,1
	sys	SYS_SLEEP
	b	.e
.late:	mov	x0,-1
	mov	x1,0
	mov	x2,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = parametre, w2 = statut, w3 = controle -> x0 = code, x1 = slot
xh_cmd:
	prologue
	adr	x0,xh_cmd_ring
	bl	xh_ring_push
	mov	x0,0
	mov	w1,0
	bl	xh_ring_bell
	mov	x0,0
	mov	x3,XH_DELAI_CMD
	bl	xh_wait
	epilogue

; x0 = code de completion (-1 : delai) -> x0 = erreur (< 0)
xh_errno:
	cmn	x0,1
	b.eq	.delai
	cmp	x0,XH_CC_STALL
	b.eq	.pipe
	mov	x0,XH_E_IO
	ret
.delai:	mov	x0,XH_E_DELAI
	ret
.pipe:	mov	x0,XH_E_PIPE
	ret

; x0 = slot, x1 = DCI, x2 = code de l'echec (-1 : delai) : remet l'extremite
; en etat (Reset Endpoint si arretee sur erreur, sinon Stop Endpoint), puis
; Set TR Dequeue Pointer sur la position courante du producteur
xh_reparer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	lsl	w22,w19,24
	orr	w22,w22,w20,lsl 16
	cmn	x21,1
	b.eq	.stop
	mov	x1,0
	mov	w2,0
	mov	w3,XH_TRB_RESET_EP shl 10
	orr	w3,w3,w22
	bl	xh_cmd
	cmp	x0,XH_CC_SUCCESS
	b.eq	.deq
.stop:	mov	x1,0				; (pas en Halted : on l'arrete)
	mov	w2,0
	mov	w3,XH_TRB_STOP_EP shl 10
	orr	w3,w3,w22
	bl	xh_cmd
.deq:	mov	x0,x19
	mov	x1,x20
	bl	xh_anneau
	ldr	x1,[x0,XH_RG_BASE]
	ldr	x2,[x0,XH_RG_ENQ]
	ldr	x3,[x0,XH_RG_CYC]
	add	x1,x1,x2,lsl 4
	xh_dma	x1,x4
	orr	x1,x1,x3			; DCS = cycle courant
	mov	w2,0
	mov	w3,XH_TRB_SET_DEQ shl 10
	orr	w3,w3,w22
	bl	xh_cmd
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ============================================================================
;  Peripheriques et contextes
; ============================================================================

; x0 = slot -> x0 = structure du peripherique
xh_dev:
	adr	x1,xh_devs
	add	x0,x1,x0,lsl 6			; XH_D_TAILLE = 64
	ret

; x0 = slot -> x0 = 1 si le slot est un peripherique adresse, 0 sinon
xh_slot_ok:
	sub	x1,x0,1
	cmp	x1,XH_MAX_SLOTS
	b.hs	.non
	adr	x1,xh_devs
	add	x1,x1,x0,lsl 6
	ldr	w0,[x1,XH_D_ACTIF]
	ret
.non:	mov	x0,0
	ret

; x0 = slot -> x0 = zone du slot (contexte peripherique en tete)
xh_area:
	sub	x0,x0,1
	mov	x2,XH_Z_DEV_TAILLE
	adr	x1,xh_zone
	ldr	x1,[x1]
	add	x1,x1,XH_Z_DEV
	madd	x0,x0,x2,x1
	ret

; -> x0 = contexte d'entree remis a zero
xh_inctx_clear:
	prologue
	adr	x0,xh_zone
	ldr	x0,[x0]
	add	x0,x0,XH_Z_INCTX
	str	x0,[sp,-16]!
	mov	x1,0x1000
	bl	xh_zero
	ldr	x0,[sp],16
	epilogue

; x0 = slot, x1 = contexte d'entree : y recopie le contexte de slot courant
xh_copy_slot_ctx:
	prologue
	str	x1,[sp,-16]!
	bl	xh_area
	ldr	x1,[sp],16
	adr	x2,xh_ctx
	ldr	x2,[x2]
	ldp	x3,x4,[x0]			; contexte de slot (16 premiers octets)
	add	x1,x1,x2
	stp	x3,x4,[x1]
	epilogue

; w0 = port racine, w1 = route, w2 = vitesse, w3 = slot du hub TT, w4 = port TT,
; w5 = profondeur -> x0 = slot adresse (0 si echec)
xh_new_device:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	w19,w0
	mov	w20,w1
	mov	w21,w2
	mov	w22,w3
	mov	w23,w4
	mov	w24,w5
	mov	x1,0				; Enable Slot
	mov	w2,0
	mov	w3,XH_TRB_ENABLE_SLOT shl 10
	bl	xh_cmd
	cmp	x0,XH_CC_SUCCESS
	b.ne	.fail
	mov	x25,x1				; slot
	cbz	x25,.fail
	cmp	x25,XH_MAX_SLOTS
	b.hi	.fail
	mov	x0,x25
	bl	xh_dev
	mov	x26,x0
	str	w20,[x26,XH_D_ROUTE]
	str	w21,[x26,XH_D_SPEED]
	str	w19,[x26,XH_D_RPORT]
	str	w22,[x26,XH_D_TTSLOT]
	str	w23,[x26,XH_D_TTPORT]
	str	w24,[x26,XH_D_DEPTH]
	str	wzr,[x26,XH_D_ACTIF]
	mov	w0,1
	str	w0,[x26,XH_D_MAXDCI]
	mov	w0,8				; EP0 : 8 (LS/FS), 64 (HS), 512 (SS)
	cmp	w21,XH_S_HIGH
	mov	w1,64
	csel	w0,w1,w0,eq
	cmp	w21,XH_S_SUPER
	mov	w1,512
	csel	w0,w1,w0,eq
	str	w0,[x26,XH_D_MPS0]
	mov	x0,x25				; anneaux du slot : aucun configure
	mov	w1,0
	bl	xh_anneau
	mov	x1,32*XH_RG_TAILLE
	bl	xh_zero
	mov	x0,x25				; contexte peripherique + anneaux
	bl	xh_area
	mov	x19,x0
	mov	x1,XH_Z_DEV_TAILLE
	bl	xh_zero
	adr	x0,xh_zone
	ldr	x0,[x0]
	mov	x1,x19
	xh_dma	x1,x2
	str	x1,[x0,x25,lsl 3]		; DCBAA[slot]
	mov	x0,x25				; anneau EP0 (DCI 1)
	mov	w1,1
	bl	xh_anneau
	add	x1,x19,0x800
	bl	xh_ring_init
	; contexte d'entree : A0 | A1
	bl	xh_inctx_clear
	mov	x1,x0
	mov	w2,3
	str	w2,[x1,4]
	adr	x2,xh_ctx
	ldr	x2,[x2]
	add	x3,x1,x2			; contexte de slot
	ldr	w4,[x26,XH_D_ROUTE]
	ldr	w5,[x26,XH_D_SPEED]
	orr	w4,w4,w5,lsl 20
	orr	w4,w4,1 shl 27			; Context Entries = 1
	str	w4,[x3]
	ldr	w4,[x26,XH_D_RPORT]
	lsl	w4,w4,16
	str	w4,[x3,4]
	ldr	w4,[x26,XH_D_TTSLOT]
	ldr	w5,[x26,XH_D_TTPORT]
	orr	w4,w4,w5,lsl 8
	str	w4,[x3,8]
	add	x3,x3,x2			; contexte EP0
	ldr	w4,[x26,XH_D_MPS0]
	lsl	w4,w4,16
	mov	w5,(4 shl 3) or (3 shl 1)	; type Control, CErr = 3
	orr	w4,w4,w5
	str	w4,[x3,4]
	add	x4,x19,0x800
	xh_dma	x4,x5
	orr	x4,x4,1				; DCS = 1
	str	x4,[x3,8]
	mov	w4,8				; longueur moyenne de TRB
	str	w4,[x3,16]
	dsb	sy
	xh_dma	x1,x2				; Address Device (BSR = 0)
	mov	w2,0
	mov	w3,XH_TRB_ADDR_DEV shl 10
	orr	w3,w3,w25,lsl 24
	bl	xh_cmd
	cmp	x0,XH_CC_SUCCESS
	b.ne	.addr_fail
	mov	w0,1
	str	w0,[x26,XH_D_ACTIF]
	mov	x0,20
	sys	SYS_SLEEP
	mov	x0,x25
	b	.out
.addr_fail:
	mov	x19,x0
	mov	x1,0				; slot rendu (Disable Slot)
	mov	w2,0
	mov	w3,XH_TRB_DISABLE_SLOT shl 10
	orr	w3,w3,w25,lsl 24
	bl	xh_cmd
	adr	x0,xh_zone
	ldr	x0,[x0]
	str	xzr,[x0,x25,lsl 3]		; DCBAA[slot] = 0
	adr	x0,xh_m_addr
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,xh_m_nl
	bl	puts
.fail:	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ============================================================================
;  Transferts de controle (EP0)
; ============================================================================

; x0 = slot, x1 = paquet SETUP (bmRequestType, bRequest, wValue, wIndex,
; wLength) ; donnees dans XH_Z_CBUF -> x0 = octets transferes, < 0 si erreur
xh_ctrl:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w1,1
	bl	xh_anneau
	mov	x21,x0
	lsr	x22,x20,48			; wLength
	mov	x23,0				; TRB de donnees (adresse DMA)
	mov	x25,0				; octets non transferes
	mov	w3,XH_TRB_SETUP shl 10		; SETUP : donnees immediates
	orr	w3,w3,XH_TRB_IDT
	cbz	x22,.trt
	tst	x20,0x80
	mov	w4,3 shl 16			; TRT : 3 IN, 2 OUT
	mov	w5,2 shl 16
	csel	w4,w4,w5,ne
	orr	w3,w3,w4
.trt:	mov	x0,x21
	mov	x1,x20
	mov	w2,8
	bl	xh_ring_push
	cbz	x22,.status
	adr	x1,xh_zone			; DATA
	ldr	x1,[x1]
	add	x1,x1,XH_Z_CBUF
	xh_dma	x1,x2
	mov	w2,w22
	mov	w3,XH_TRB_DATA shl 10
	tst	x20,0x80
	b.eq	.dout
	orr	w3,w3,1 shl 16			; DIR = IN
	orr	w3,w3,XH_TRB_ISP		; paquet court : evenement (longueur)
.dout:	mov	x0,x21
	bl	xh_ring_push
	mov	x23,x0
	xh_dma	x23,x2
.status:
	mov	w3,(XH_TRB_STATUS shl 10) or XH_TRB_IOC
	cbz	x22,.sin			; pas de donnees : statut IN
	tst	x20,0x80
	b.ne	.sgo				; donnees IN : statut OUT
.sin:	orr	w3,w3,1 shl 16
.sgo:	mov	x0,x21
	mov	x1,0
	mov	w2,0
	bl	xh_ring_push
	mov	x24,x0
	xh_dma	x24,x2
	mov	x0,x19
	mov	w1,1				; DCI 1 = EP0
	bl	xh_ring_bell
.att:	mov	x0,1
	mov	x1,x19
	mov	x2,1
	mov	x3,XH_DELAI_CTRL
	bl	xh_wait
	cmp	x0,XH_CC_SUCCESS
	b.eq	.ev
	cmp	x0,XH_CC_SHORT
	b.ne	.err
.ev:	cmp	x2,x24				; statut : fini
	b.eq	.fini
	cmp	x2,x23				; donnees courtes : on note, statut ensuite
	b.ne	.att
	mov	x25,x1
	b	.att
.fini:	subs	x0,x22,x25
	csel	x0,xzr,x0,lo
	b	.out
.err:	mov	x26,x0
	mov	x2,x0
	mov	x0,x19
	mov	x1,1
	bl	xh_reparer
	mov	x0,x26
	bl	xh_errno
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot, w1 = type << 8 | index, w2 = longueur -> x0 = octets, < 0 si erreur
xh_get_desc:
	mov	x3,0x0680			; GET_DESCRIPTOR, peripherique -> hote
	orr	x3,x3,x1,lsl 16
	orr	x1,x3,x2,lsl 48
	b	xh_ctrl

; x0 = slot, x1 = SETUP, x2 = tampon (memoire du process), x3 = longueur
; -> x0 = octets transferes, < 0 si erreur (par le tampon DMA de controle)
xh_control:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	bl	xh_slot_ok
	cbz	x0,.nodev
	lsr	x0,x20,48			; wLength : le tampon DMA fait 4 Ko
	cmp	x0,0x1000
	b.hi	.inval
	tst	x20,0x80
	b.ne	.go
	cmp	x22,x0				; sortie : donnees vers le tampon DMA
	csel	x2,x22,x0,lo
	cbz	x2,.go
	adr	x0,xh_zone
	ldr	x0,[x0]
	add	x0,x0,XH_Z_CBUF
	mov	x1,x21
	bl	mem_move
.go:	mov	x0,x19
	mov	x1,x20
	bl	xh_ctrl
	tbnz	x0,63,.out
	tst	x20,0x80
	b.eq	.out
	cmp	x0,x22				; entree : vers le tampon de l'appelant
	csel	x20,x0,x22,lo
	cbz	x20,.ret
	mov	x0,x21
	adr	x1,xh_zone
	ldr	x1,[x1]
	add	x1,x1,XH_Z_CBUF
	mov	x2,x20
	bl	mem_move
.ret:	mov	x0,x20
	b	.out
.nodev:	mov	x0,XH_E_NODEV
	b	.out
.inval:	mov	x0,XH_E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot, w1 = nouvelle taille max EP0 : Evaluate Context
xh_set_mps0:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	bl	xh_dev
	str	w20,[x0,XH_D_MPS0]
	bl	xh_inctx_clear
	mov	w1,2				; A1
	str	w1,[x0,4]
	adr	x2,xh_ctx
	ldr	x2,[x2]
	add	x3,x0,x2,lsl 1			; EP0 = contexte d'entree n 2
	lsl	w4,w20,16
	mov	w5,(4 shl 3) or (3 shl 1)
	orr	w4,w4,w5
	str	w4,[x3,4]
	dsb	sy
	mov	x1,x0
	xh_dma	x1,x2
	mov	w2,0
	mov	w3,XH_TRB_EVAL_CTX shl 10
	orr	w3,w3,w19,lsl 24
	bl	xh_cmd
	ldp	x19,x20,[sp],16
	epilogue

; ============================================================================
;  Extremites (interruption, bulk)
; ============================================================================

; x0 = slot, w1 = DCI, w2 = MPS, w3 = intervalle (code xHCI), w4 = type
; (2 Bulk OUT, 6 Bulk IN, 7 Interrupt IN), x5 = anneau : Configure Endpoint.
; Seule l'extremite nouvelle est ajoutee (A0 | A(DCI)) : les autres restent.
; -> x0 = code
xh_config_ep:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	mov	w21,w2
	mov	w22,w3
	mov	w23,w4
	mov	x24,x5
	bl	xh_inctx_clear
	mov	x25,x0
	mov	w1,1				; A0 | A(DCI)
	lslv	w1,w1,w20
	orr	w1,w1,1
	str	w1,[x25,4]
	mov	x0,x19
	mov	x1,x25
	bl	xh_copy_slot_ctx
	mov	x0,x19				; Context Entries = DCI max
	bl	xh_dev
	mov	x26,x0
	ldr	w27,[x26,XH_D_MAXDCI]
	cmp	w20,w27
	csel	w27,w20,w27,hi
	adr	x2,xh_ctx
	ldr	x2,[x2]
	add	x3,x25,x2
	ldr	w4,[x3]
	and	w4,w4,0x07FFFFFF
	orr	w4,w4,w27,lsl 27
	str	w4,[x3]
	add	w5,w20,1			; contexte d'entree n DCI+1
	mul	x5,x5,x2
	add	x3,x25,x5
	lsl	w4,w22,16			; intervalle
	str	w4,[x3]
	lsl	w4,w21,16			; MPS, type, CErr 3
	orr	w4,w4,3 shl 1
	orr	w4,w4,w23,lsl 3
	and	w5,w23,3			; (isochrone : CErr 0)
	cmp	w5,1
	b.ne	.cerr
	and	w4,w4,not (3 shl 1)
.cerr:	str	w4,[x3,4]
	ldr	x4,[x24,XH_RG_BASE]
	xh_dma	x4,x6
	orr	x4,x4,1
	str	x4,[x3,8]
	and	w5,w23,3			; interruption, isochrone : ESIT = MPS,
	cmp	w5,1				; TRB moyen 16
	b.eq	.esit
	cmp	w5,3
	b.ne	.bulk
.esit:
	lsl	w4,w21,16
	orr	w4,w4,16
	b	.moy
.bulk:	mov	w4,3072				; bulk : TRB moyen 3 Ko
.moy:	str	w4,[x3,16]
	dsb	sy
	mov	x1,x25
	xh_dma	x1,x6
	mov	w2,0
	mov	w3,XH_TRB_CONFIG_EP shl 10
	orr	w3,w3,w19,lsl 24
	bl	xh_cmd
	cmp	x0,XH_CC_SUCCESS
	b.ne	.out
	str	w27,[x26,XH_D_MAXDCI]
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot, w1 = DCI, w2 = MPS, w3 = intervalle, w4 = type : configure
; l'extremite au premier usage (son anneau, Configure Endpoint)
; -> x0 = 0, ou < 0
xh_ep_preparer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	mov	w21,w2
	mov	w22,w3
	mov	w23,w4
	mov	w1,w20
	bl	xh_anneau
	mov	x24,x0
	ldr	w0,[x24,XH_RG_PRET]
	cbnz	w0,.ok
	mov	x0,x19				; anneau : +0x800 + (DCI-1) Ko
	bl	xh_area
	add	x1,x0,0x800
	sub	w2,w20,1
	add	x1,x1,x2,lsl 10
	mov	x0,x24
	bl	xh_ring_init
	mov	x0,x19
	mov	w1,w20
	mov	w2,w21
	mov	w3,w22
	mov	w4,w23
	mov	x5,x24
	bl	xh_config_ep
	cmp	x0,XH_CC_SUCCESS
	b.ne	.err
	mov	w0,1
	str	w0,[x24,XH_RG_PRET]
.ok:	mov	x0,0
	b	.out
.err:	mov	x19,x0
	adr	x0,xh_m_cfgep
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,xh_m_nl
	bl	puts
	mov	x0,XH_E_IO
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = intervalle (ms), w1 = vitesse -> w0 = code xHCI : log2(ms x 8),
; 3..10 en LS/FS, au plus 15 en HS/SS
xh_intervalle:
	lsl	x0,x0,3
	cmp	x0,8
	mov	x2,8
	csel	x0,x2,x0,lo
	clz	x0,x0
	mov	x2,63
	sub	x0,x2,x0
	cmp	w1,XH_S_HIGH
	b.hs	.hs
	cmp	x0,10
	mov	x2,10
	csel	x0,x2,x0,hi
	ret
.hs:	cmp	x0,15
	mov	x2,15
	csel	x0,x2,x0,hi
	ret

; x0 = tuyau : un transfert Interrupt IN (TRB Normal, IOC, ISP) dans son
; tampon courant
xh_tuyau_armer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x20,xh_tuyaux
	mov	x1,XH_P_TAILLE
	madd	x20,x19,x1,x20
	ldr	w0,[x20,XH_P_SLOT]
	ldr	w1,[x20,XH_P_DCI]
	bl	xh_anneau
	adr	x1,xh_zone
	ldr	x1,[x1]
	add	x1,x1,XH_Z_REP
	add	x1,x1,x19,lsl 10
	ldr	w2,[x20,XH_P_COTE]
	add	x1,x1,x2,lsl 9
	xh_dma	x1,x2
	ldr	w2,[x20,XH_P_MPS]
	mov	w3,(XH_TRB_NORMAL shl 10) or XH_TRB_IOC or XH_TRB_ISP
	bl	xh_ring_push
	mov	w0,1
	str	w0,[x20,XH_P_ARME]
	ldr	w0,[x20,XH_P_SLOT]
	ldr	w1,[x20,XH_P_DCI]
	bl	xh_ring_bell
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot, x1 = extremite (IN), x2 = taille max., x3 = intervalle (ms)
; -> x0 = tuyau >= 0, ou < 0
xh_intr_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	bl	xh_slot_ok
	cbz	x0,.nodev
	tbz	w20,7,.inval			; Interrupt IN seulement
	and	w21,w21,0x7FF
	cbz	w21,.inval
	cmp	w21,XH_TUY_MPS
	mov	w0,XH_TUY_MPS
	csel	w21,w0,w21,hi
	and	w23,w20,0xF			; DCI = 2 x numero + 1
	cbz	w23,.inval
	lsl	w23,w23,1
	add	w23,w23,1
	adr	x24,xh_tuyaux			; le meme (slot, DCI) d'abord
	mov	x25,0
.m:	cmp	x25,XH_TUY_MAX
	b.hs	.libre
	ldr	w0,[x24,XH_P_SLOT]
	cmp	w0,w19
	b.ne	.mn
	ldr	w0,[x24,XH_P_DCI]
	cmp	w0,w23
	b.ne	.mn
	ldr	w0,[x24,XH_P_OUVERT]
	cbnz	w0,.occupe
	b	.trouve
.mn:	add	x24,x24,XH_P_TAILLE
	add	x25,x25,1
	b	.m
.libre:	adr	x24,xh_tuyaux			; sinon un tuyau ferme sans transfert
	mov	x25,0				; en cours (son tampon est libre)
.l:	cmp	x25,XH_TUY_MAX
	b.hs	.nomem
	ldr	w0,[x24,XH_P_OUVERT]
	cbnz	w0,.ln
	ldr	w0,[x24,XH_P_ARME]
	cbz	w0,.trouve
.ln:	add	x24,x24,XH_P_TAILLE
	add	x25,x25,1
	b	.l
.trouve:
	mov	x0,x19
	bl	xh_dev
	ldr	w1,[x0,XH_D_SPEED]
	mov	x0,x22
	bl	xh_intervalle
	mov	w3,w0
	mov	x0,x19
	mov	w1,w23
	mov	w2,w21
	mov	w4,7				; Interrupt IN
	bl	xh_ep_preparer
	tbnz	x0,63,.out
	str	w19,[x24,XH_P_SLOT]
	str	w23,[x24,XH_P_DCI]
	str	w21,[x24,XH_P_MPS]
	str	wzr,[x24,XH_P_PRET]
	str	wzr,[x24,XH_P_ERR]
	str	wzr,[x24,XH_P_MORT]
	mov	w0,1
	str	w0,[x24,XH_P_OUVERT]
	ldr	w0,[x24,XH_P_ARME]		; (un transfert peut rester d'avant)
	cbnz	w0,.arme
	mov	x0,x25
	bl	xh_tuyau_armer
.arme:	mov	x0,x25
	b	.out
.occupe:
	mov	x0,XH_E_BUSY
	b	.out
.nomem:	mov	x0,XH_E_NOMEM
	b	.out
.nodev:	mov	x0,XH_E_NODEV
	b	.out
.inval:	mov	x0,XH_E_INVAL
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tuyau -> x0 = octets recus (> 0), 0 si rien, < 0 erreur ; x1 = donnees
; (valides jusqu'au prochain appel : le transfert suivant va dans l'autre
; tampon). Non bloquant : les evenements sont vides par xh_scruter.
xh_intr_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	cmp	x0,XH_TUY_MAX
	b.hs	.inval
	mov	x19,x0
	adr	x20,xh_tuyaux
	mov	x1,XH_P_TAILLE
	madd	x20,x19,x1,x20
	ldr	w0,[x20,XH_P_OUVERT]
	cbz	w0,.inval
	ldr	w0,[x20,XH_P_MORT]
	cbnz	w0,.io
	ldr	w0,[x20,XH_P_PRET]
	cmp	w0,1
	b.eq	.donnees
	cmp	w0,2
	b.eq	.erreur
	ldr	w0,[x20,XH_P_ARME]
	cbnz	w0,.rien
	mov	x0,x19
	bl	xh_tuyau_armer
.rien:	mov	x0,0
	b	.out
.donnees:
	str	wzr,[x20,XH_P_PRET]
	str	wzr,[x20,XH_P_ERR]
	adr	x21,xh_zone			; tampon rempli
	ldr	x21,[x21]
	add	x21,x21,XH_Z_REP
	add	x21,x21,x19,lsl 10
	ldr	w0,[x20,XH_P_COTE]
	add	x21,x21,x0,lsl 9
	eor	w0,w0,1				; le suivant dans l'autre
	str	w0,[x20,XH_P_COTE]
	ldr	w22,[x20,XH_P_LONG]
	mov	x0,x19
	bl	xh_tuyau_armer
	mov	x0,x22
	mov	x1,x21
	b	.out
.erreur:
	str	wzr,[x20,XH_P_PRET]		; nouvel essai, abandon a 20 de suite
	ldr	w0,[x20,XH_P_ERR]
	add	w0,w0,1
	str	w0,[x20,XH_P_ERR]
	cmp	w0,20
	b.lo	.reprise
	mov	w0,1
	str	w0,[x20,XH_P_MORT]
	adr	x0,xh_m_mort
	bl	puts
	b	.io
.reprise:
	ldr	w0,[x20,XH_P_SLOT]
	ldr	w1,[x20,XH_P_DCI]
	ldr	w2,[x20,XH_P_CODE]
	bl	xh_reparer
	mov	x0,x19
	bl	xh_tuyau_armer
	b	.rien
.io:	mov	x0,XH_E_IO
	b	.out
.inval:	mov	x0,XH_E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tuyau : ferme (plus de nouveau transfert ; celui en cours s'acheve)
xh_intr_fermer:
	cmp	x0,XH_TUY_MAX
	b.hs	.out
	adr	x1,xh_tuyaux
	mov	x2,XH_P_TAILLE
	madd	x1,x0,x2,x1
	str	wzr,[x1,XH_P_OUVERT]
	str	wzr,[x1,XH_P_PRET]
.out:	ret

; x0 = slot, x1 = extremite (bit 7 : IN), x2 = taille max., x3 = tampon DMA
; (virtuel), x4 = son adresse physique, x5 = longueur (<= 64 Ko)
; -> x0 = octets transferes, ou < 0 (bloquant, <= 5 s par morceau)
xh_bulk:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x23,x4
	mov	x24,x5
	bl	xh_slot_ok
	cbz	x0,.nodev
	cbz	x24,.zero
	mov	x0,0x10000
	cmp	x24,x0
	b.hi	.inval
	add	x0,x23,x24			; sous 1 Go (fenetre entrante)
	movl	x1,XH_DMA_MAX
	cmp	x0,x1
	b.hi	.inval
	bl	xh_bulk_dci
	mov	w25,w0
	cmp	w25,2
	b.lo	.inval
	mov	x0,x19
	mov	w1,w25
	and	w2,w21,0x7FF
	mov	w3,0
	mov	w4,2				; Bulk OUT
	tbz	w20,7,.typ
	mov	w4,6				; Bulk IN
.typ:	bl	xh_ep_preparer
	tbnz	x0,63,.out
	mov	x0,x19
	mov	w1,w25
	bl	xh_anneau
	mov	x26,x0
	mov	x27,0				; total
.morceau:
	and	x0,x23,0xFFFF			; un TRB ne franchit pas 64 Ko
	mov	x1,0x10000
	sub	x28,x1,x0
	cmp	x28,x24
	csel	x28,x28,x24,lo
	mov	x0,x26
	mov	x1,x23
	mov	w2,w28
	mov	w3,(XH_TRB_NORMAL shl 10) or XH_TRB_IOC or XH_TRB_ISP
	bl	xh_ring_push
	mov	x0,x19
	mov	w1,w25
	bl	xh_ring_bell
	mov	x0,1
	mov	x1,x19
	mov	x2,x25
	mov	x3,XH_DELAI_BULK
	bl	xh_wait
	cmp	x0,XH_CC_SUCCESS
	b.eq	.ok
	cmp	x0,XH_CC_SHORT
	b.ne	.err
.ok:	subs	x0,x28,x1
	csel	x0,xzr,x0,lo
	add	x27,x27,x0
	cbnz	x1,.fini			; paquet court : fin du transfert
	add	x23,x23,x28
	subs	x24,x24,x28
	b.ne	.morceau
.fini:	mov	x0,x27
	b	.out
.err:	mov	x22,x0				; STALL, erreur ou delai : extremite
	mov	x2,x0				; remise en etat, puis < 0
	mov	x0,x19
	mov	w1,w25
	bl	xh_reparer
	mov	x0,x22
	bl	xh_errno
	b	.out
.zero:	mov	x0,0
	b	.out
.nodev:	mov	x0,XH_E_NODEV
	b	.out
.inval:	mov	x0,XH_E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x20 = extremite -> w0 = DCI (2 x numero, + 1 si IN)
xh_bulk_dci:
	and	w0,w20,0xF
	lsl	w0,w0,1
	ubfx	w1,w20,7,1
	add	w0,w0,w1
	ret

; x0 = slot, x1 = extremite (bit 7 : IN) : apres CLEAR_FEATURE(ENDPOINT_HALT)
; reussi ; si l'extremite est Halted : Reset Endpoint + Set TR Dequeue
; -> x0 = 0, ou < 0
xh_bulk_raz:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	xh_slot_ok
	cbz	x0,.nodev
	bl	xh_bulk_dci
	mov	w21,w0
	cmp	w21,2
	b.lo	.inval
	mov	x0,x19
	mov	w1,w21
	bl	xh_anneau
	ldr	w0,[x0,XH_RG_PRET]
	cbz	w0,.rien			; jamais configuree
	mov	x0,x19				; etat dans le contexte de sortie
	bl	xh_area
	adr	x1,xh_ctx
	ldr	x1,[x1]
	mul	x1,x1,x21
	ldr	w0,[x0,x1]
	and	w0,w0,7
	cmp	w0,2				; Halted
	b.ne	.rien
	mov	x0,x19
	mov	w1,w21
	mov	x2,XH_CC_STALL
	bl	xh_reparer
.rien:	mov	x0,0
	b	.out
.nodev:	mov	x0,XH_E_NODEV
	b	.out
.inval:	mov	x0,XH_E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot, w1 = nombre de ports, w2 = TT think time : declare un hub
xh_config_hub:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	mov	w21,w2
	bl	xh_inctx_clear
	mov	x22,x0
	mov	w1,1				; A0 seulement
	str	w1,[x22,4]
	mov	x0,x19
	mov	x1,x22
	bl	xh_copy_slot_ctx
	adr	x2,xh_ctx
	ldr	x2,[x2]
	add	x3,x22,x2
	ldr	w4,[x3]
	orr	w4,w4,1 shl 26			; Hub = 1
	str	w4,[x3]
	ldr	w4,[x3,4]
	and	w4,w4,0x00FFFFFF
	orr	w4,w4,w20,lsl 24		; nombre de ports
	str	w4,[x3,4]
	ldr	w4,[x3,8]
	and	w4,w4,not (3 shl 16)
	orr	w4,w4,w21,lsl 16		; TTT
	str	w4,[x3,8]
	dsb	sy
	mov	x1,x22
	xh_dma	x1,x2
	mov	w2,0
	mov	w3,XH_TRB_CONFIG_EP shl 10
	orr	w3,w3,w19,lsl 24
	bl	xh_cmd
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ============================================================================
;  Enumeration
; ============================================================================

; Ports racine au demarrage : chacun comme un branchement
xh_scan_root:
	prologue
	str	x19,[sp,-16]!
	mov	x19,1
.port:	adr	x0,xh_nports
	ldr	x0,[x0]
	cmp	x19,x0
	b.hi	.out
	mov	x0,x19
	bl	xh_root_port
	add	x19,x19,1
	b	.port
.out:	ldr	x19,[sp],16
	epilogue

; x0 = port racine -> x0 = adresse de son PORTSC
xh_portsc:
	adr	x1,xh_op
	ldr	x1,[x1]
	add	x1,x1,XH_PORTSC
	sub	x0,x0,1
	add	x0,x1,x0,lsl 4
	ret

; w0 = valeur lue de PORTSC -> w0 = valeur a ecrire : bits conserves (RO,
; RWS) + bits de changement presents (RW1C : acquittes) ; jamais PED ni PR
xh_portsc_acq:
	movl	w1,XH_PORTSC_KEEP
	and	w2,w0,w1
	movl	w1,XH_PORTSC_CHG
	and	w0,w0,w1
	orr	w0,w0,w2
	ret

; x0 = port racine : si connecte, remise a zero puis enumeration (demarrage
; et branchement) ; changements acquittes
xh_root_port:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	xh_portsc
	mov	x20,x0
	ldr	w21,[x20]
	tbnz	w21,0,.reset			; connecte
	mov	w0,w21				; rien : changements acquittes
	bl	xh_portsc_acq
	str	w0,[x20]
	b	.out
.reset:	movl	w1,XH_PORTSC_KEEP		; reset du port
	and	w0,w21,w1
	orr	w0,w0,1 shl 4			; PR
	str	w0,[x20]
	mov	x22,200
.rst:	mov	x0,1
	sys	SYS_SLEEP
	ldr	w21,[x20]
	tbnz	w21,21,.rst_done		; PRC
	subs	x22,x22,1
	b.ne	.rst
	b	.out
.rst_done:
	mov	w0,w21				; acquitter les changements
	bl	xh_portsc_acq
	str	w0,[x20]
	tbz	w21,1,.out			; port non active
	mov	x0,20
	sys	SYS_SLEEP
	adr	x0,xh_m_branche			; branchement port N, vitesse V
	mov	x1,x19
	mov	x2,0
	ubfx	w3,w21,10,4
	bl	xh_log_port
	mov	w0,w19				; port racine, route 0, pas de TT
	mov	w1,0
	ubfx	w2,w21,10,4
	mov	w3,0
	mov	w4,0
	mov	w5,0
	mov	w6,0				; arbre : racine, port N
	mov	w7,w19
	bl	xh_enumerate
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = port racine, w1 = route, w2 = vitesse, w3/w4 = TT (slot, port),
; w5 = profondeur, w6 = slot du hub parent (0 : racine), w7 = port (racine
; ou du hub) : adresse, note dans l'arbre, configure, puis hub ou
; usbd_attacher. Un echec rend le slot.
xh_enumerate:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	w24,w5
	mov	w27,w6
	mov	w28,w7
	cmp	w24,XH_PROF_MAX
	b.hs	.out
	bl	xh_new_device
	cbz	x0,.nodev
	mov	x19,x0				; slot
	bl	xh_dev				; arbre
	str	w27,[x0,XH_D_PARENT]
	str	w28,[x0,XH_D_PORT]
	str	wzr,[x0,XH_D_USBD]
	str	wzr,[x0,XH_D_HUB]
	str	wzr,[x0,XH_D_NPORTS]
	adr	x20,xh_zone
	ldr	x20,[x20]
	add	x20,x20,XH_Z_CBUF		; tampon de controle

	mov	x0,x19				; 8 premiers octets : bMaxPacketSize0
	mov	w1,0x0100
	mov	w2,8
	bl	xh_get_desc
	tbnz	x0,63,.err
	ldrb	w23,[x20,7]
	mov	x0,x19
	bl	xh_dev
	mov	w1,w23
	ldr	w2,[x0,XH_D_SPEED]
	cmp	w2,XH_S_HIGH
	b.hs	.mps_ok				; HS/SS : deja juste
	ldr	w2,[x0,XH_D_MPS0]
	cmp	w1,w2
	b.eq	.mps_ok
	cmp	w1,8				; 8, 16, 32 ou 64 seulement
	b.lo	.mps_ok
	cmp	w1,64
	b.hi	.mps_ok
	mov	x0,x19
	bl	xh_set_mps0
.mps_ok:
	mov	x0,x19				; descripteur complet
	mov	w1,0x0100
	mov	w2,18
	bl	xh_get_desc
	tbnz	x0,63,.err
	adr	x26,xh_desc_copie		; copie : le tampon de controle resservira
	add	x26,x26,x24,lsl 5
	mov	x0,x26
	mov	x1,x20
	mov	x2,18
	bl	mem_move
	ldrb	w25,[x26,4]			; classe du peripherique
	adr	x0,xh_m_dev
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,xh_m_id
	bl	puts
	ldrh	w0,[x26,8]
	mov	x1,4
	bl	print_hex
	adr	x0,xh_m_2p
	bl	puts
	ldrh	w0,[x26,10]
	mov	x1,4
	bl	print_hex
	adr	x0,xh_m_class
	bl	puts
	mov	x0,x25
	mov	x1,2
	bl	print_hex
	adr	x0,xh_m_nl
	bl	puts

	mov	x0,x19				; configuration (en-tete puis complete)
	mov	w1,0x0200
	mov	w2,9
	bl	xh_get_desc
	tbnz	x0,63,.err
	ldrh	w21,[x20,2]			; wTotalLength
	cmp	w21,XH_CFG_MAX
	mov	w0,XH_CFG_MAX
	csel	w21,w0,w21,hi
	mov	x0,x19
	mov	w1,0x0200
	mov	w2,w21
	bl	xh_get_desc
	tbnz	x0,63,.err
	cmp	x0,x21				; ce qui est vraiment arrive
	csel	x21,x0,x21,lo
	cmp	x21,9
	b.lo	.err
	adr	x22,xh_cfg_copie
	mov	x0,XH_CFG_MAX
	madd	x22,x24,x0,x22
	mov	x0,x22
	mov	x1,x20
	mov	x2,x21
	bl	mem_move

	ldrb	w1,[x22,5]			; SET_CONFIGURATION
	mov	x0,x19
	mov	x2,0x0900
	orr	x1,x2,x1,lsl 16
	bl	xh_ctrl
	tbnz	x0,63,.err
	mov	x0,20
	sys	SYS_SLEEP

	cmp	w25,9				; hub ?
	b.eq	.hub
	cmp	x21,9+6
	b.lo	.fonction
	ldrb	w0,[x22,9+5]			; classe de la 1re interface
	cmp	w0,9
	b.eq	.hub
.fonction:					; au coeur : vitesse usbd = PSI - 1
	mov	x0,x19
	bl	xh_dev
	ldr	w5,[x0,XH_D_SPEED]
	sub	w5,w5,1
	cmp	w5,USB_SS
	mov	w0,USB_SS
	csel	w5,w0,w5,hi
	mov	x0,1				; xHCI
	mov	x1,x19
	mov	x2,x26
	mov	x3,x22
	mov	x4,x21
	bl	usbd_attacher
	cmp	x0,0				; arbre : numero usbd (0 : table pleine)
	csel	x20,x0,xzr,gt
	mov	x0,x19
	bl	xh_dev
	str	w20,[x0,XH_D_USBD]
	b	.out
.hub:	mov	x0,x19
	bl	xh_hub
	b	.out
.err:	adr	x0,xh_m_enum
	bl	puts
	mov	x0,x19				; slot rendu
	bl	xh_slot_liberer
	b	.out
.nodev:	adr	x0,xh_m_nodev			; (Enable Slot / Address Device)
	bl	puts
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot du hub (USB2) : declaration, alimentation, puis reset et
; enumeration de chaque port connecte
xh_hub:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	bl	xh_dev
	mov	x26,x0
	ldr	w0,[x26,XH_D_SPEED]
	cmp	w0,XH_S_SUPER			; hubs SuperSpeed : non geres
	b.eq	.out
	mov	x0,x19				; descripteur de hub (classe, 0x29)
	movl	x1,0x06A0 or (0x2900 shl 16)
	movl	x2,8 shl 48
	orr	x1,x1,x2
	bl	xh_ctrl
	tbnz	x0,63,.out
	adr	x20,xh_zone
	ldr	x20,[x20]
	add	x20,x20,XH_Z_CBUF
	ldrb	w21,[x20,2]			; bNbrPorts
	ldrb	w23,[x20,5]			; bPwrOn2PwrGood (x 2 ms)
	ldrh	w25,[x20,3]			; wHubCharacteristics : TTT bits 5-6
	adr	x0,xh_m_hub
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,xh_m_ports
	bl	puts
	mov	x0,x19
	mov	w1,w21
	ubfx	w2,w25,5,2
	bl	xh_config_hub
	str	w21,[x26,XH_D_NPORTS]		; arbre : hub, ports scrutes
	mov	w0,1
	str	w0,[x26,XH_D_HUB]

	mov	x22,1				; alimentation des ports
.pwr:	cmp	x22,x21
	b.hi	.pwr_done
	mov	x0,x19
	mov	x1,x22
	mov	x2,0x0323			; SET_FEATURE(PORT_POWER)
	mov	x3,8
	bl	xh_hub_feature
	add	x22,x22,1
	b	.pwr
.pwr_done:
	lsl	w0,w23,1			; (+ 500 ms : le temps que les
	add	w0,w0,500			; peripheriques se signalent)
	sys	SYS_SLEEP

	mov	x22,1
.port:	cmp	x22,x21
	b.hi	.out
	mov	x0,x19
	mov	x1,x22
	bl	xh_hub_status
	mov	x24,x0
	tbnz	x24,63,.next
	tbz	w24,16,.cc			; C_PORT_CONNECTION : acquitte
	mov	x0,x19
	mov	x1,x22
	mov	x2,0x0123
	mov	x3,16
	bl	xh_hub_feature
.cc:	tbz	w24,0,.next			; PORT_CONNECTION
	mov	x0,x19
	mov	x1,x22
	bl	xh_hub_port
.next:	add	x22,x22,1
	b	.port
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot du hub, x1 = port, x2 = requete (0x0323 SET_FEATURE, 0x0123
; CLEAR_FEATURE), x3 = selecteur -> x0 = resultat de xh_ctrl
xh_hub_feature:
	orr	x2,x2,x3,lsl 16
	orr	x1,x2,x1,lsl 32
	b	xh_ctrl

; x0 = slot du hub, x1 = port (connecte) : PORT_RESET, attente de
; C_PORT_RESET, puis enumeration (vitesse, TT, route) ; demarrage et
; branchement
xh_hub_port:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x22,x1
	bl	xh_dev
	mov	x21,x0
	mov	x0,x19				; SET_FEATURE(PORT_RESET)
	mov	x1,x22
	mov	x2,0x0323
	mov	x3,4
	bl	xh_hub_feature
	mov	x24,25
.rst:	mov	x0,10
	sys	SYS_SLEEP
	mov	x0,x19
	mov	x1,x22
	bl	xh_hub_status
	tbnz	x0,63,.out
	tbnz	w0,20,.rst_done			; C_PORT_RESET
	subs	x24,x24,1
	b.ne	.rst
	b	.out
.rst_done:
	mov	x23,x0
	mov	x0,x19				; CLEAR_FEATURE(C_PORT_RESET)
	mov	x1,x22
	mov	x2,0x0123
	mov	x3,20
	bl	xh_hub_feature
	tbz	w23,1,.out			; port non active
	mov	x0,20
	sys	SYS_SLEEP
	mov	w24,XH_S_FULL			; vitesse : bit 9 LS, bit 10 HS
	tbz	w23,9,.nls
	mov	w24,XH_S_LOW
.nls:	tbz	w23,10,.speed
	mov	w24,XH_S_HIGH
.speed:	adr	x0,xh_m_branche			; branchement port N (hub S), vitesse V
	mov	x1,x22
	mov	x2,x19
	mov	x3,x24
	bl	xh_log_port
	mov	w2,w24
	; TT : hub HS + peripherique LS/FS -> ce hub ; sinon celui du hub
	ldr	w3,[x21,XH_D_TTSLOT]
	ldr	w4,[x21,XH_D_TTPORT]
	ldr	w5,[x21,XH_D_SPEED]
	cmp	w5,XH_S_HIGH
	b.ne	.tt
	cmp	w2,XH_S_HIGH
	b.eq	.tt
	mov	w3,w19
	mov	w4,w22
.tt:	ldr	w5,[x21,XH_D_DEPTH]		; route : port sur le quartet de profondeur
	mov	w6,w22
	cmp	w6,15
	mov	w7,15
	csel	w6,w7,w6,hi
	lsl	w7,w5,2
	lslv	w6,w6,w7
	ldr	w1,[x21,XH_D_ROUTE]
	orr	w1,w1,w6
	ldr	w0,[x21,XH_D_RPORT]
	add	w5,w5,1
	mov	w6,w19				; arbre : ce hub, ce port
	mov	w7,w22
	bl	xh_enumerate
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = message, x1 = port, x2 = slot du hub (0 : port racine), x3 = vitesse
; (0 : non dite) : 'usbd : xhci : branchement port 2 (hub 1), vitesse 3'
xh_log_port:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	bl	puts
	mov	x0,x19
	bl	print_dec
	cbz	x20,.v
	adr	x0,xh_m_lhub
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,xh_m_lpar
	bl	puts
.v:	cbz	x21,.nl
	adr	x0,xh_m_speed
	bl	puts
	mov	x0,x21
	bl	print_dec
.nl:	adr	x0,xh_m_nl
	bl	puts
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot du hub, x1 = port -> x0 = wPortStatus | wPortChange << 16, <0 si erreur
xh_hub_status:
	prologue
	mov	x2,0x00A3			; GET_STATUS(port), 4 octets
	orr	x2,x2,x1,lsl 32
	orr	x1,x2,4 shl 48
	bl	xh_ctrl
	tbnz	x0,63,.out
	adr	x0,xh_zone
	ldr	x0,[x0]
	add	x0,x0,XH_Z_CBUF
	ldr	w0,[x0]
.out:	epilogue

; ============================================================================
;  Branchement a chaud : scrutation et retraits
; ============================================================================

; Ports racines : changement de connexion (CSC) -> retrait du sous-arbre,
; puis enumeration si connecte
xh_scan_racines:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,1
.port:	adr	x0,xh_nports
	ldr	x0,[x0]
	cmp	x19,x0
	b.hi	.out
	mov	x0,x19
	bl	xh_portsc
	mov	x20,x0
	ldr	w21,[x20]
	tbz	w21,17,.next			; CSC
	mov	w0,w21				; acquitte (avec les autres changements)
	bl	xh_portsc_acq
	str	w0,[x20]
	tbz	w21,0,.retrait
	tbz	w21,1,.change			; connecte, active, deja connu : parasite
	mov	x0,0
	mov	x1,x19
	bl	xh_occupe
	cbnz	x0,.next
.change:
	mov	x0,0				; (remplace en un eclair : l'ancien part)
	mov	x1,x19
	bl	xh_detacher
	mov	x0,100				; laisser le contact s'etablir
	sys	SYS_SLEEP
	mov	x0,x19
	bl	xh_root_port
	b	.next
.retrait:
	mov	x0,0
	mov	x1,x19
	bl	xh_detacher
	adr	x0,xh_m_retrait
	mov	x1,x19
	mov	x2,0
	mov	x3,0
	bl	xh_log_port
.next:	add	x19,x19,1
	b	.port
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Ports des hubs connus, par profondeur croissante (un hub retire avec son
; parent n'est plus interroge) : GET_STATUS, C_PORT_CONNECTION
xh_scan_hubs:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,0				; profondeur
.prof:	cmp	x19,XH_PROF_MAX
	b.hs	.out
	mov	x20,1				; slot
.slot:	cmp	x20,XH_MAX_SLOTS
	b.hi	.prof_n
	mov	x0,x20
	bl	xh_dev
	mov	x21,x0
	ldr	w0,[x21,XH_D_ACTIF]
	cbz	w0,.slot_n
	ldr	w0,[x21,XH_D_DEPTH]
	cmp	x0,x19
	b.ne	.slot_n
	mov	x22,1
.port:	ldr	w0,[x21,XH_D_HUB]
	cbz	w0,.slot_n
	ldr	w0,[x21,XH_D_NPORTS]
	cmp	x22,x0
	b.hi	.slot_n
	mov	x0,x20
	mov	x1,x22
	bl	xh_hub_change
	tbnz	x0,63,.slot_n			; hub muet : son parent le retirera
	add	x22,x22,1
	b	.port
.slot_n:
	add	x20,x20,1
	b	.slot
.prof_n:
	add	x19,x19,1
	b	.prof
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot du hub, x1 = port : un changement de connexion ?
; -> x0 = 0, < 0 si le hub ne repond pas
xh_hub_change:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	xh_hub_status
	tbnz	x0,63,.out
	mov	x21,x0
	tbz	w21,16,.rien			; C_PORT_CONNECTION
	mov	x0,x19				; CLEAR_FEATURE(C_PORT_CONNECTION)
	mov	x1,x20
	mov	x2,0x0123
	mov	x3,16
	bl	xh_hub_feature
	tbz	w21,0,.retrait
	tbz	w21,1,.change			; connecte, active, deja connu : parasite
	mov	x0,x19
	mov	x1,x20
	bl	xh_occupe
	cbnz	x0,.rien
.change:
	mov	x0,x19				; (remplace en un eclair : l'ancien part)
	mov	x1,x20
	bl	xh_detacher
	mov	x0,100				; laisser le contact s'etablir
	sys	SYS_SLEEP
	mov	x0,x19
	mov	x1,x20
	bl	xh_hub_port
	b	.rien
.retrait:
	mov	x0,x19
	mov	x1,x20
	bl	xh_detacher
	adr	x0,xh_m_retrait
	mov	x1,x20
	mov	x2,x19
	mov	x3,0
	bl	xh_log_port
.rien:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot du parent (0 : racine), x1 = port -> x0 = slot qui y est (0 : aucun)
xh_occupe:
	adr	x2,xh_devs
	mov	x3,1
.s:	cmp	x3,XH_MAX_SLOTS
	b.hi	.non
	add	x4,x2,x3,lsl 6
	ldr	w5,[x4,XH_D_ACTIF]
	cbz	w5,.n
	ldr	w5,[x4,XH_D_PARENT]
	cmp	x5,x0
	b.ne	.n
	ldr	w5,[x4,XH_D_PORT]
	cmp	x5,x1
	b.ne	.n
	mov	x0,x3
	ret
.n:	add	x3,x3,1
	b	.s
.non:	mov	x0,0
	ret

; x0 = slot du parent (0 : racine), x1 = port : retire ce qui y est branche
; (sous-arbre compris) -> x0 = slots rendus
xh_detacher:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,1
	mov	x22,0
.s:	cmp	x21,XH_MAX_SLOTS
	b.hi	.out
	mov	x0,x21
	bl	xh_dev
	ldr	w1,[x0,XH_D_ACTIF]
	cbz	w1,.n
	ldr	w1,[x0,XH_D_PARENT]
	cmp	x1,x19
	b.ne	.n
	ldr	w1,[x0,XH_D_PORT]
	cmp	x1,x20
	b.ne	.n
	mov	x0,x21
	bl	xh_detacher_slot
	add	x22,x22,x0
.n:	add	x21,x21,1
	b	.s
.out:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot : ses descendants d'abord (recursif, hubs), puis lui
; -> x0 = slots rendus
xh_detacher_slot:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,1				; (lui)
	mov	x21,1
.s:	cmp	x21,XH_MAX_SLOTS
	b.hi	.moi
	cmp	x21,x19
	b.eq	.n
	mov	x0,x21
	bl	xh_dev
	ldr	w1,[x0,XH_D_ACTIF]
	cbz	w1,.n
	ldr	w1,[x0,XH_D_PARENT]
	cmp	x1,x19
	b.ne	.n
	mov	x0,x21
	bl	xh_detacher_slot
	add	x20,x20,x0
.n:	add	x21,x21,1
	b	.s
.moi:	mov	x0,x19
	bl	xh_slot_liberer
	mov	x0,x20
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = slot : hors de l'arbre ; ses tuyaux d'interruption et ses extremites
; (anneaux, bulk) sont oublies, Disable Slot, DCBAA[slot] = 0 (sa zone
; resservira) ; puis usbd_detacher s'il avait ete confie au coeur
xh_slot_liberer:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	bl	xh_dev
	mov	x20,x0
	ldr	w21,[x20,XH_D_USBD]
	str	wzr,[x20,XH_D_ACTIF]		; (ses evenements seront ignores)
	str	wzr,[x20,XH_D_USBD]
	str	wzr,[x20,XH_D_HUB]
	str	wzr,[x20,XH_D_NPORTS]
	str	wzr,[x20,XH_D_PARENT]
	str	wzr,[x20,XH_D_PORT]
	adr	x0,xh_tuyaux			; tuyaux : fermes, desarmes, oublies
	mov	x1,0
.t:	cmp	x1,XH_TUY_MAX
	b.hs	.slot
	ldr	w2,[x0,XH_P_SLOT]
	cmp	w2,w19
	b.ne	.tn
	str	wzr,[x0,XH_P_SLOT]
	str	wzr,[x0,XH_P_OUVERT]
	str	wzr,[x0,XH_P_ARME]
	str	wzr,[x0,XH_P_PRET]
	str	wzr,[x0,XH_P_MORT]
.tn:	add	x0,x0,XH_P_TAILLE
	add	x1,x1,1
	b	.t
.slot:	mov	x1,0				; Disable Slot
	mov	w2,0
	mov	w3,XH_TRB_DISABLE_SLOT shl 10
	orr	w3,w3,w19,lsl 24
	bl	xh_cmd
	adr	x0,xh_zone			; DCBAA[slot] = 0
	ldr	x0,[x0]
	str	xzr,[x0,x19,lsl 3]
	mov	x0,x19				; anneaux du slot (bulk : a reconfigurer)
	mov	w1,0
	bl	xh_anneau
	mov	x1,32*XH_RG_TAILLE
	bl	xh_zero
	cbz	w21,.out
	mov	x0,x21				; au coeur : pilotes prevenus
	bl	usbd_detacher
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ============================================================================
;  Messages
; ============================================================================
xh_m_absent	db 'usbd : xhci : absent (emulateur, pas de PCIe)',10,0
xh_m_branche	db 'usbd : xhci : branchement port ',0
xh_m_retrait	db 'usbd : xhci : retrait port ',0
xh_m_lhub	db ' (hub ',0
xh_m_lpar	db ')',0
xh_m_nodev	db 'usbd : xhci : peripherique sans slot (Enable Slot / Address Device)',10,0
xh_m_nolink	db 'usbd : xhci : pas de lien PCIe',10,0
xh_m_link	db 'usbd : xhci : lien PCIe, VL805 active',10,0
xh_m_fail	db 'usbd : xhci : echec d''initialisation',10,0
xh_m_zone	db 'usbd : xhci : zone DMA au-dela de 1 Go',10,0
xh_m_ver	db 'usbd : xhci : version ',0
xh_m_speed	db ', vitesse ',0
xh_m_addr	db 'usbd : xhci : Address Device refuse, code ',0
xh_m_cfgep	db 'usbd : xhci : Configure Endpoint refuse, code ',0
xh_m_dev	db 'usbd : xhci : slot ',0
xh_m_id		db ', id ',0
xh_m_2p		db ':',0
xh_m_class	db ', classe ',0
xh_m_enum	db 'usbd : xhci : erreur d''enumeration',10,0
xh_m_hub	db 'usbd : xhci : hub, ',0
xh_m_ports	db ' ports',10,0
xh_m_mort	db 'usbd : xhci : tuyau abandonne (erreurs)',10,0
xh_m_pret	db 'usbd : xhci : pret',10,0
xh_m_nl		db 10,0
		align 4

; ============================================================================
;  Donnees (segment inscriptible de usbd)
; ============================================================================
macro xh_donnees {
		align 16
xh_fw_msg	rb 32				; message au firmware (reponse ecrite)
xh_pcie		dd 0				; registres du pont (adresse mappee)
xh_cap		dd 0				; BAR0 du xHCI (adresse mappee)
xh_op		dd 0
xh_db		dd 0
xh_ir0		dd 0
xh_zone		dd 0				; zone DMA (virtuelle)
xh_delta	dd 0				; physique - virtuelle
xh_nports	dd 0
xh_nslots	dd 0
xh_ctx		dd 0
xh_evt_deq	dd 0				; xh_evt_deq / xh_evt_cyc contigus
xh_evt_cyc	dd 1
xh_ev		dd 0, 0				; dernier evenement (16 octets)
xh_t_scan	dd 0				; derniere scrutation des ports (ms)
xh_cmd_ring	rb XH_RG_TAILLE
xh_devs		rb XH_D_TAILLE * (XH_MAX_SLOTS + 1)
xh_anneaux	rb XH_RG_TAILLE * 32 * (XH_MAX_SLOTS + 1)
xh_tuyaux	rb XH_P_TAILLE * XH_TUY_MAX
xh_desc_copie	rb 32 * 4
xh_cfg_copie	rb XH_CFG_MAX * 4
		align 8
}
