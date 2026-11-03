; genet - l'Ethernet integre du Raspberry Pi 4 (MAC GENET v5 + PHY BCM54213PE,
; RGMII, MDIO ; port RJ45) en service (docs/RESEAU.md etape 4), lance par
; hwmanager (cle soc:genet de drivers.txt) sur un Pi 4 / Pi 400 / CM4.
; Repris de src/kernel/arch/pi4/genet.asm (d'apres la documentation du bloc,
; docs/etude/03-reseau-materiel.md, section 1) :
;  * scrutation (pas d'IRQ) toutes les 1 ms : reception, emission ; lien PHY
;    sonde toutes les 2 s ; sans cable, le pilote reste et attend ;
;  * un seul anneau dans chaque sens (16) : RX 64 descripteurs, TX 32,
;    tampons de 2 Ko en memoire DMA non cachee (dma_alloc : adresse physique
;    vue telle quelle par le GENET) ;
;  * interface eth0 de netd (priorite 30 : passe avant le Wi-Fi et l'USB des
;    que le lien monte), anneau de trames (netif.inc) ;
;  * fin sans erreur (pas de relance) sous QEMU (firmware < 0x50000000 : bloc
;    non emule, ne rien toucher) ou si SYS_REV n'est pas un GENET v5.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/netd.inc'

GENET_BASE	= 0xFD580000

; --- bloc SYS / EXT -----------------------------------------------------------
GE_SYS_REV	= 0x0000
GE_PORT_CTRL	= 0x0004
GE_RBUF_FLUSH	= 0x0008
GE_RGMII_OOB	= 0x008C
OOB_RGMII_LINK	= 1 shl 4
OOB_DISABLE	= 1 shl 5
OOB_MODE_EN	= 1 shl 6
OOB_ID_MODE_DIS	= 1 shl 16
PORT_EXT_GPHY	= 3

; --- interruptions : reception seulement, l'emission est sondee au reveil -------
GE_INTRL2_0	= 0x0200
GE_INTRL2_1	= 0x0240
INTRL2_CLEAR	= 0x08
INTRL2_MASK_SET	= 0x10
INTRL2_MASK_CLR	= 0x14
INT_RX_DONE	= 1 shl 13
GE_IRQ		= 189			; ligne du GIC (SPI 157)
GE_SIG_RX	= 1			; bit de son service ; 2 : NIF_SIG_TX
GE_WAIT_POLL	= 1			; ms : tant qu'aucune interruption n'a prouve
GE_WAIT_IRQ	= 50			;      qu'elle marche ; ensuite (lien, netif_tick)

; --- tampons RBUF / UMAC -----------------------------------------------------
GE_RBUF_CTRL	= 0x0300
RBUF_ALIGN_2B	= 1 shl 1
GE_TBUF_SIZE	= 0x03B4
GE_UMAC_CMD	= 0x0808
GE_UMAC_MAC0	= 0x080C
GE_UMAC_MAC1	= 0x0810
GE_UMAC_MAXLEN	= 0x0814
GE_UMAC_TXFLUSH	= 0x0B34
GE_UMAC_MIB	= 0x0D80
GE_MDIO_CMD	= 0x0E14

CMD_TX_EN	= 1 shl 0
CMD_RX_EN	= 1 shl 1
CMD_SPEED_SHIFT	= 2			; 0 = 10, 1 = 100, 2 = 1000 Mb/s
CMD_PROMISC	= 1 shl 4
CMD_CRC_FWD	= 1 shl 6
CMD_RX_PAUSE_IGN = 1 shl 8
CMD_HD_EN	= 1 shl 10
CMD_SW_RESET	= 1 shl 13
CMD_LCL_LOOP	= 1 shl 15
CMD_TX_PAUSE_IGN = 1 shl 28

MDIO_START	= 1 shl 29
MDIO_RD_FAIL	= 1 shl 28
MDIO_OP_RD	= 2 shl 26
MDIO_OP_WR	= 1 shl 26
GE_PHY_ADDR	= 1

; registres PHY (IEEE 802.3 clause 22)
MII_BMCR	= 0
MII_BMSR	= 1
MII_ADVERTISE	= 4
MII_LPA		= 5
MII_CTRL1000	= 9
MII_STAT1000	= 10
BMCR_ANENABLE	= 1 shl 12
BMCR_ANRESTART	= 1 shl 9
BMSR_LSTATUS	= 1 shl 2

; --- DMA -----------------------------------------------------------------------
GE_RDMA_DESC	= 0x2000		; descripteurs RX : 12 octets chacun
GE_RDMA_R16	= 0x3000		; registres de l'anneau RX 16
GE_RDMA_CFG	= 0x3040
GE_RDMA_CTRL	= 0x3044
GE_RDMA_BURST	= 0x304C
GE_RDMA_IDX2RING = 0x30B0		; 8 mots
GE_TDMA_DESC	= 0x4000
GE_TDMA_R16	= 0x5000
GE_TDMA_CFG	= 0x5040
GE_TDMA_CTRL	= 0x5044
GE_TDMA_BURST	= 0x504C
GE_TDMA_ARB	= 0x506C
GE_HFB_CTRL	= 0xFC00
GE_HFB_FLT_EN	= 0xFC04

; registres d'un anneau (decalages dans son bloc de 0x40 octets)
RING_PTR0	= 0x00			; RX : WRITE_PTR / TX : READ_PTR
RING_PTR0_HI	= 0x04
RING_HW_INDEX	= 0x08			; RX : PROD_INDEX / TX : CONS_INDEX (materiel)
RING_SW_INDEX	= 0x0C			; RX : CONS_INDEX / TX : PROD_INDEX (logiciel)
RING_BUF_SIZE	= 0x10
RING_START	= 0x14
RING_START_HI	= 0x18
RING_END	= 0x1C
RING_END_HI	= 0x20
RING_DONE_THR	= 0x24
RING_FLOW	= 0x28			; RX : XON/XOFF / TX : FLOW_PERIOD
RING_PTR1	= 0x2C			; RX : READ_PTR / TX : WRITE_PTR
RING_PTR1_HI	= 0x30

DMA_EN		= 1 shl 0
DMA_R16_BUF_EN	= 1 shl 17		; bit (anneau + 1)
DMA_R16_CFG	= 1 shl 16

; descripteur : +0 LENGTH_STATUS, +4 ADDRESS_LO, +8 ADDRESS_HI
DESC_SIZE	= 12
DS_SOP		= 1 shl 13
DS_EOP		= 1 shl 14
DS_TX_CRC	= 1 shl 6
DS_TX_QTAG	= 0x3F shl 7
DS_RX_ERRS	= 0x1F			; LG NO RXER CRC OV

GE_RX_DESCS	= 64
GE_TX_DESCS	= 32
GE_BUF		= 2048
GE_RX_PAGES	= GE_RX_DESCS * GE_BUF / 4096
GE_TX_PAGES	= GE_TX_DESCS * GE_BUF / 4096
GE_LINK_MS	= 2000

; Mode de reception : promiscuite (aucun filtre MDF a programmer, robuste
; pour une premiere mise en route ; net.asm ignore ce qui ne le concerne pas)
GE_PROMISC	= 1


; --- etat ------------------------------------------------------------------------
GS_LINK		= 4			; 1 si lien
GS_SPEED	= 8			; Mb/s
GS_FDX		= 12
GS_CRC_FWD	= 16
GS_RX_CONS	= 24			; compteur 16 bits
GS_TX_PROD	= 28
GS_TX_CONS	= 32
GS_NEXT_LINK	= 40			; prochaine verification du lien (ms)
GS_RX_ERR	= 80
GS_RX_OK	= 84
GS_TX_OK	= 88

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,fw_rev			; QEMU : bloc absent, ne rien toucher
	sys	SYS_FW_CALL
	tbnz	x0,63,.absent
	adr	x0,fw_rev
	ldr	w0,[x0,20]
	movl	w1,0x50000000
	cmp	w0,w1
	b.lo	.absent
	movl	x0,GENET_BASE			; registres
	mov	x1,0x10000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.absent
	adr	x1,ge_regs
	str	x0,[x1]
	mov	x19,x0
	ldr	w0,[x19,GE_SYS_REV]		; SYS_REV_CTRL[27:24] = 6 : GENET v5
	ubfx	w0,w0,24,4
	cmp	w0,6
	b.ne	.absent
	movl	x0,GE_RX_DESCS * GE_BUF		; tampons DMA (non caches)
	adr	x1,rx_phys
	sys	SYS_DMA_ALLOC
	tbnz	x0,63,.nomem
	adr	x1,rx_va
	str	x0,[x1]
	movl	x0,GE_TX_DESCS * GE_BUF
	adr	x1,tx_phys
	sys	SYS_DMA_ALLOC
	tbnz	x0,63,.nomem
	adr	x1,tx_va
	str	x0,[x1]
	bl	genet_read_mac
	bl	genet_init
	adr	x0,ge_msg_ok			; "genet : GENET v5 pret, MAC ..."
	bl	puts
	bl	print_mac
	bl	genet_irq_init
	mov	x0,NIF_ETHERNET			; interface eth0 de netd
	adr	x1,genet_mac
	ldr	x1,[x1]
	mov	x2,NIF_PRIO_GENET
	adr	x3,s_eth0
	bl	netif_register
	tbnz	x0,63,.sans_netd
	adr	x19,genet_state
.loop:	mov	x20,0				; travail fait
	bl	genet_rx_poll
	add	x20,x20,x0
	bl	genet_tx_poll
	add	x20,x20,x0
	sys	SYS_TIME_MS			; lien toutes les 2 s
	ldr	x1,[x19,GS_NEXT_LINK]
	cmp	x0,x1
	b.lo	.tick
	add	x0,x0,GE_LINK_MS
	str	x0,[x19,GS_NEXT_LINK]
	bl	genet_phy_status
	ldr	w1,[x19,GS_LINK]
	cmp	w0,w1
	b.eq	.tick
	cbz	w0,.down
	bl	genet_link_up
	b	.tick
.down:	bl	genet_link_down
.tick:	bl	netif_tick
	mov	x0,x20
	bl	genet_wait
	b	.loop
.absent:
	adr	x0,ge_msg_absent
	bl	puts
	mov	x0,0				; (pas de relance)
	sys	SYS_EXIT
.nomem:	adr	x0,ge_msg_nomem
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
.sans_netd:
	adr	x0,ge_msg_netd
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; un service (le reveil de netd) et l'interruption, sinon : on sonde
genet_irq_init:
	prologue
	adr	x0,s_genet
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,.out
	adr	x1,ge_canal
	str	x0,[x1]
	add	x0,x0,1
	adr	x1,nif_wake
	str	x0,[x1]
	mov	x0,GE_IRQ
	adr	x1,ge_canal
	ldr	x1,[x1]
	mov	x2,GE_SIG_RX
	sys	SYS_IRQ_BIND
	tbnz	x0,63,.out
	adr	x1,ge_regs
	ldr	x1,[x1]
	add	x1,x1,GE_INTRL2_0
	mov	w0,INT_RX_DONE
	str	w0,[x1,INTRL2_CLEAR]
	str	w0,[x1,INTRL2_MASK_CLR]
.out:	epilogue

; x0 = travail fait a ce tour (0 : attendre)
genet_wait:
	prologue
	cbz	x0,.idle
	adr	x1,ge_timed			; du travail apres un delai : une
	ldr	x2,[x1]				; interruption a manque, on resonde
	str	xzr,[x1]
	cbz	x2,.out
	adr	x1,ge_wait
	mov	x2,GE_WAIT_POLL
	str	x2,[x1]
	b	.out
.idle:	adr	x1,ge_canal
	ldr	x0,[x1]
	tbnz	x0,63,.sleep
	adr	x1,ge_wait
	ldr	x2,[x1]
	adr	x1,ge_msg
	sys	SYS_SVC_NEXT
	cbz	x0,.delay
	tbnz	x0,63,.out
	adr	x1,ge_msg
	ldp	x0,x1,[x1]
	movl	x2,SVC_OP_SIGNAL
	cmp	x0,x2
	b.ne	.out
	tbz	x1,0,.out			; (NIF_SIG_TX seul : genet_tx_poll)
	adr	x1,ge_regs
	ldr	x1,[x1]
	add	x1,x1,GE_INTRL2_0
	mov	w0,INT_RX_DONE
	str	w0,[x1,INTRL2_CLEAR]
	mov	x0,GE_IRQ
	sys	SYS_IRQ_ACK
	adr	x1,ge_wait
	mov	x2,GE_WAIT_IRQ
	str	x2,[x1]
	b	.out
.delay:	adr	x1,ge_wait			; (au tour d'apres : genet_wait)
	ldr	x2,[x1]
	cmp	x2,GE_WAIT_IRQ
	b.ne	.out
	adr	x1,ge_timed
	mov	x2,1
	str	x2,[x1]
	b	.out
.sleep:	mov	x0,1
	sys	SYS_SLEEP
.out:	epilogue

; x0 = ms
delay_ms:
	sys	SYS_SLEEP
	ret

; ---------------------------------------------------------------------------
;  Initialisation (registres en ge_regs, tampons alloues)
; ---------------------------------------------------------------------------
genet_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,ge_regs
	ldr	x19,[x19]
	adr	x20,genet_state
	add	x21,x19,0x4000			; registres TDMA (hors portee de x19)

	; --- remise a zero de l'UniMAC ------------------------------------------
	str	wzr,[x19,GE_RBUF_FLUSH]
	mov	x0,1
	bl	delay_ms
	str	wzr,[x19,GE_UMAC_CMD]
	mov	w0,CMD_SW_RESET or CMD_LCL_LOOP
	str	w0,[x19,GE_UMAC_CMD]
	mov	x0,1
	bl	delay_ms
	str	wzr,[x19,GE_UMAC_CMD]
	ldr	w0,[x19,GE_RBUF_FLUSH]		; vidage des tampons de reception
	orr	w0,w0,2
	str	w0,[x19,GE_RBUF_FLUSH]
	mov	x0,1
	bl	delay_ms
	ldr	w0,[x19,GE_RBUF_FLUSH]
	and	w0,w0,0xFFFFFFFD
	str	w0,[x19,GE_RBUF_FLUSH]
	mov	x0,1
	bl	delay_ms

	mov	w0,7				; compteurs MIB a zero
	str	w0,[x19,GE_UMAC_MIB]
	str	wzr,[x19,GE_UMAC_MIB]
	mov	w0,1536
	str	w0,[x19,GE_UMAC_MAXLEN]
	ldr	w0,[x19,GE_RBUF_CTRL]		; trames RX decalees de 2 octets
	orr	w0,w0,RBUF_ALIGN_2B
	str	w0,[x19,GE_RBUF_CTRL]
	mov	w0,1
	str	w0,[x19,GE_TBUF_SIZE]
	mov	w0,-1				; toutes les IRQ masquees et acquittees
	add	x1,x19,GE_INTRL2_0
	str	w0,[x1,INTRL2_MASK_SET]
	str	w0,[x1,INTRL2_CLEAR]
	add	x1,x19,GE_INTRL2_1
	str	w0,[x1,INTRL2_MASK_SET]
	str	w0,[x1,INTRL2_CLEAR]

	ldr	w0,[x19,GE_UMAC_CMD]		; la FCS est-elle transmise en RX ?
	ubfx	w0,w0,6,1
	str	w0,[x20,GS_CRC_FWD]

	; --- adresse MAC de la carte (firmware) ------------------------------------
	adr	x0,genet_mac
	ldrb	w1,[x0]
	ldrb	w2,[x0,1]
	ldrb	w3,[x0,2]
	ldrb	w4,[x0,3]
	lsl	w1,w1,24
	orr	w1,w1,w2,lsl 16
	orr	w1,w1,w3,lsl 8
	orr	w1,w1,w4
	str	w1,[x19,GE_UMAC_MAC0]
	ldrb	w1,[x0,4]
	ldrb	w2,[x0,5]
	lsl	w1,w1,8
	orr	w1,w1,w2
	str	w1,[x19,GE_UMAC_MAC1]

	; --- DMA arrete, file d'emission videe ------------------------------------
	movl	w2,(not (DMA_EN or DMA_R16_BUF_EN)) and 0xFFFFFFFF
	ldr	w0,[x19,GE_RDMA_CTRL]
	and	w0,w0,w2
	str	w0,[x19,GE_RDMA_CTRL]
	ldr	w0,[x21,GE_TDMA_CTRL-0x4000]
	and	w0,w0,w2
	str	w0,[x21,GE_TDMA_CTRL-0x4000]
	mov	w0,1
	str	w0,[x19,GE_UMAC_TXFLUSH]
	mov	x0,1
	bl	delay_ms
	str	wzr,[x19,GE_UMAC_TXFLUSH]

	; --- anneau RX 16 : descripteurs 0..63 -----------------------------------
	mov	w0,8
	str	w0,[x19,GE_RDMA_BURST]
	adr	x2,rx_phys
	ldr	x2,[x2]
	add	x3,x19,GE_RDMA_DESC
	mov	x4,0
.rxd:	str	wzr,[x3]			; LENGTH_STATUS
	str	w2,[x3,4]			; ADDRESS_LO (adresse physique ARM)
	lsr	x5,x2,32
	str	w5,[x3,8]			; ADDRESS_HI
	add	x2,x2,GE_BUF
	add	x3,x3,DESC_SIZE
	add	x4,x4,1
	cmp	x4,GE_RX_DESCS
	b.lo	.rxd
	add	x3,x19,GE_RDMA_R16
	str	wzr,[x3,RING_HW_INDEX]
	str	wzr,[x3,RING_SW_INDEX]
	movl	w0,(GE_RX_DESCS shl 16) or GE_BUF
	str	w0,[x3,RING_BUF_SIZE]
	movl	w0,(5 shl 16) or 16		; seuils XON/XOFF
	str	w0,[x3,RING_FLOW]
	mov	w0,1				; une interruption par trame recue
	str	w0,[x3,RING_DONE_THR]
	str	wzr,[x3,RING_START]
	str	wzr,[x3,RING_START_HI]
	str	wzr,[x3,RING_PTR0]
	str	wzr,[x3,RING_PTR0_HI]
	str	wzr,[x3,RING_PTR1]
	str	wzr,[x3,RING_PTR1_HI]
	mov	w0,GE_RX_DESCS * 3 - 1		; fin, en mots
	str	w0,[x3,RING_END]
	str	wzr,[x3,RING_END_HI]
	mov	x4,0				; aucun filtre vers d'autres anneaux
	movl	x5,GE_RDMA_IDX2RING
	add	x5,x19,x5
.i2r:	str	wzr,[x5,x4,lsl 2]
	add	x4,x4,1
	cmp	x4,8
	b.lo	.i2r
	mov	w0,DMA_R16_CFG
	str	w0,[x19,GE_RDMA_CFG]
	mov	w0,DMA_R16_BUF_EN
	str	w0,[x19,GE_RDMA_CTRL]

	; --- anneau TX 16 : descripteurs 0..31 -----------------------------------
	mov	w0,8
	str	w0,[x21,GE_TDMA_BURST-0x4000]
	mov	w0,2				; arbitrage : priorite stricte
	str	w0,[x21,GE_TDMA_ARB-0x4000]
	adr	x2,tx_phys
	ldr	x2,[x2]
	add	x3,x19,GE_TDMA_DESC
	mov	x4,0
.txd:	str	wzr,[x3]
	str	w2,[x3,4]
	lsr	x5,x2,32
	str	w5,[x3,8]
	add	x2,x2,GE_BUF
	add	x3,x3,DESC_SIZE
	add	x4,x4,1
	cmp	x4,GE_TX_DESCS
	b.lo	.txd
	add	x3,x19,GE_TDMA_R16
	str	wzr,[x3,RING_HW_INDEX]
	str	wzr,[x3,RING_SW_INDEX]
	mov	w0,10
	str	w0,[x3,RING_DONE_THR]
	str	wzr,[x3,RING_FLOW]
	movl	w0,(GE_TX_DESCS shl 16) or GE_BUF
	str	w0,[x3,RING_BUF_SIZE]
	str	wzr,[x3,RING_START]
	str	wzr,[x3,RING_START_HI]
	str	wzr,[x3,RING_PTR0]
	str	wzr,[x3,RING_PTR0_HI]
	str	wzr,[x3,RING_PTR1]
	str	wzr,[x3,RING_PTR1_HI]
	mov	w0,GE_TX_DESCS * 3 - 1
	str	w0,[x3,RING_END]
	str	wzr,[x3,RING_END_HI]
	mov	w0,DMA_R16_CFG
	str	w0,[x21,GE_TDMA_CFG-0x4000]
	mov	w0,DMA_R16_BUF_EN
	str	w0,[x21,GE_TDMA_CTRL-0x4000]

	; --- filtres materiels (HFB) desactives, DMA en marche ---------------------
	movl	x9,GE_HFB_CTRL
	add	x9,x19,x9
	str	wzr,[x9]
	str	wzr,[x9,GE_HFB_FLT_EN-GE_HFB_CTRL]
	dsb	sy
	ldr	w0,[x19,GE_RDMA_CTRL]
	orr	w0,w0,DMA_EN
	str	w0,[x19,GE_RDMA_CTRL]
	ldr	w0,[x21,GE_TDMA_CTRL-0x4000]
	orr	w0,w0,DMA_EN
	str	w0,[x21,GE_TDMA_CTRL-0x4000]

	; --- PHY (RGMII) -------------------------------------------------------------
	mov	w0,PORT_EXT_GPHY
	str	w0,[x19,GE_PORT_CTRL]
	ldr	w0,[x19,GE_RGMII_OOB]
	movl	w1,OOB_MODE_EN or OOB_ID_MODE_DIS
	orr	w0,w0,w1
	str	w0,[x19,GE_RGMII_OOB]
	mov	w0,MII_BMSR			; 1re lecture factice (MDIO)
	bl	genet_mdio_read
	mov	w0,MII_BMCR			; autonegociation (normalement deja
	bl	genet_mdio_read			; lancee par le firmware VideoCore)
	tbnz	w0,31,.mac_on
	tbnz	w0,12,.mac_on
	mov	w0,MII_BMCR
	mov	w1,BMCR_ANENABLE or BMCR_ANRESTART
	bl	genet_mdio_write

	; --- emetteur / recepteur MAC --------------------------------------------
.mac_on:
	ldr	w0,[x19,GE_UMAC_CMD]
	movl	w1,CMD_TX_EN or CMD_RX_EN or CMD_RX_PAUSE_IGN or CMD_TX_PAUSE_IGN
	orr	w0,w0,w1
if GE_PROMISC
	orr	w0,w0,CMD_PROMISC
end if
	str	w0,[x19,GE_UMAC_CMD]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  MDIO (clause 22) : PHY d'adresse GE_PHY_ADDR
; ---------------------------------------------------------------------------

; w0 = registre -> w0 = valeur (16 bits), bit 31 a 1 si echec
genet_mdio_read:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,ge_regs
	ldr	x19,[x19]
	and	w0,w0,0x1F
	lsl	w0,w0,16
	movl	w1,MDIO_OP_RD or (GE_PHY_ADDR shl 21)
	orr	w0,w0,w1
	str	w0,[x19,GE_MDIO_CMD]
	bl	genet_mdio_go
	cbz	x0,.fail
	ldr	w0,[x19,GE_MDIO_CMD]
	tst	w0,MDIO_RD_FAIL
	b.ne	.fail
	and	w0,w0,0xFFFF
	b	.out
.fail:	mov	w0,0x80000000
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = registre, w1 = valeur
genet_mdio_write:
	prologue
	str	x19,[sp,-16]!
	adr	x19,ge_regs
	ldr	x19,[x19]
	and	w0,w0,0x1F
	lsl	w0,w0,16
	and	w1,w1,0xFFFF
	orr	w0,w0,w1
	movl	w1,MDIO_OP_WR or (GE_PHY_ADDR shl 21)
	orr	w0,w0,w1
	str	w0,[x19,GE_MDIO_CMD]
	bl	genet_mdio_go
	ldr	x19,[sp],16
	epilogue

; lance la commande MDIO deja ecrite et attend sa fin (10 ms max) -> x0 = 1 si ok
genet_mdio_go:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,ge_regs
	ldr	x19,[x19]
	ldr	w0,[x19,GE_MDIO_CMD]
	orr	w0,w0,MDIO_START
	str	w0,[x19,GE_MDIO_CMD]
	sys	SYS_TIME_MS
	add	x20,x0,10
.w:	ldr	w0,[x19,GE_MDIO_CMD]
	tbz	w0,29,.ok
	sys	SYS_TIME_MS
	cmp	x0,x20
	b.lo	.w
	mov	x0,0
	b	.out
.ok:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Lien
; ---------------------------------------------------------------------------

; Lit l'etat du PHY -> x0 = 1 si lien ; GS_SPEED / GS_FDX renseignes
genet_phy_status:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,genet_state
	mov	w0,MII_BMSR			; BMSR : bit de lien "verrouille bas",
	bl	genet_mdio_read			; donc deux lectures
	mov	w0,MII_BMSR
	bl	genet_mdio_read
	tbnz	w0,31,.down
	tbz	w0,2,.down
	; 1000 Mb/s : STAT1000 & (CTRL1000 << 2), bits 11 (FD) / 10 (HD)
	mov	w0,MII_CTRL1000
	bl	genet_mdio_read
	mov	w20,w0
	mov	w0,MII_STAT1000
	bl	genet_mdio_read
	tbnz	w0,31,.mii100
	tbnz	w20,31,.mii100
	and	w0,w0,w20,lsl 2
	tbz	w0,11,.g_hd
	mov	w1,1000
	mov	w2,1
	b	.set
.g_hd:	tbz	w0,10,.mii100
	mov	w1,1000
	mov	w2,0
	b	.set
.mii100:					; LPA & ADVERTISE
	mov	w0,MII_ADVERTISE
	bl	genet_mdio_read
	mov	w20,w0
	mov	w0,MII_LPA
	bl	genet_mdio_read
	and	w0,w0,w20
	mov	w1,100
	mov	w2,1
	tbnz	w0,8,.set			; 100 FD
	mov	w2,0
	tbnz	w0,7,.set			; 100 HD
	mov	w1,10
	mov	w2,1
	tbnz	w0,6,.set			; 10 FD
	mov	w2,0				; 10 HD (ou inconnu)
.set:	str	w1,[x19,GS_SPEED]
	str	w2,[x19,GS_FDX]
	mov	x0,1
	b	.out
.down:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Lien monte : vitesse/duplex dans l'UniMAC, RGMII actif, netd prevenu
genet_link_up:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,ge_regs
	ldr	x19,[x19]
	adr	x20,genet_state
	ldr	w0,[x19,GE_RGMII_OOB]
	and	w0,w0,(not OOB_DISABLE) and 0xFFFFFFFF
	orr	w0,w0,OOB_RGMII_LINK
	str	w0,[x19,GE_RGMII_OOB]
	ldr	w1,[x20,GS_SPEED]		; 0 = 10, 1 = 100, 2 = 1000
	mov	w2,2
	cmp	w1,1000
	b.eq	.spd
	mov	w2,1
	cmp	w1,100
	b.eq	.spd
	mov	w2,0
.spd:	ldr	w0,[x19,GE_UMAC_CMD]
	and	w0,w0,(not (3 shl CMD_SPEED_SHIFT)) and 0xFFFFFFFF
	orr	w0,w0,w2,lsl CMD_SPEED_SHIFT
	and	w0,w0,(not CMD_HD_EN) and 0xFFFFFFFF
	ldr	w1,[x20,GS_FDX]
	cbnz	w1,.fdx
	orr	w0,w0,CMD_HD_EN
.fdx:	str	w0,[x19,GE_UMAC_CMD]
	mov	w0,1
	str	w0,[x20,GS_LINK]
	adr	x0,ge_msg_link
	bl	puts
	ldr	w0,[x20,GS_SPEED]
	bl	print_dec
	adr	x0,ge_msg_mbs
	bl	puts
	adr	x0,ge_msg_fdx
	ldr	w1,[x20,GS_FDX]
	cbnz	w1,.dx
	adr	x0,ge_msg_hdx
.dx:	bl	puts
	mov	x0,1				; netd : lien (priorite sur Wi-Fi, USB)
	ldr	w1,[x20,GS_SPEED]
	mov	x2,0
	bl	netif_link
	ldp	x19,x20,[sp],16
	epilogue

; Lien tombe : netd passe a l'interface suivante
genet_link_down:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,ge_regs
	ldr	x19,[x19]
	adr	x20,genet_state
	str	wzr,[x20,GS_LINK]
	ldr	w0,[x19,GE_RGMII_OOB]
	and	w0,w0,(not OOB_RGMII_LINK) and 0xFFFFFFFF
	str	w0,[x19,GE_RGMII_OOB]
	adr	x0,ge_msg_down
	bl	puts
	mov	x0,0
	mov	x1,0
	mov	x2,0
	bl	netif_link
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Chemin des donnees
; ---------------------------------------------------------------------------

; Trames recues : anneau RX 16 -> anneau de netd (si le lien est monte)
; -> x0 = trames traitees
genet_rx_poll:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	mov	x25,0
	adr	x19,genet_state
	adr	x20,ge_regs
	ldr	x20,[x20]
	add	x21,x20,GE_RDMA_R16
.next:	cmp	x25,32				; au plus 32 par tour
	b.hs	.out
	ldr	w0,[x21,RING_HW_INDEX]		; [15:0] producteur, [31:16] rejets
	lsr	w1,w0,16
	cmp	w1,0xC000			; compteur de rejets : remis a zero
	b.lo	.prod
	and	w2,w0,0xFFFF
	str	w2,[x21,RING_HW_INDEX]
.prod:	and	w0,w0,0xFFFF
	ldr	w22,[x19,GS_RX_CONS]
	sub	w1,w0,w22
	tst	w1,0xFFFF
	b.eq	.out				; rien de neuf
	add	x25,x25,1
	and	w23,w22,GE_RX_DESCS-1		; descripteur
	mov	w0,DESC_SIZE
	mul	w0,w23,w0
	add	x0,x0,GE_RDMA_DESC
	ldr	w24,[x20,x0]			; LENGTH_STATUS
	adr	x0,rx_va
	ldr	x0,[x0]
	add	x0,x0,x23,lsl 11		; tampon (2 Ko)
	mov	x23,x0
	and	w1,w24,DS_SOP or DS_EOP		; trame entiere et sans erreur ?
	movl	w2,DS_SOP or DS_EOP
	cmp	w1,w2
	b.ne	.bad
	tst	w24,DS_RX_ERRS
	b.ne	.bad
	lsr	w24,w24,16			; longueur (avec 2 octets d'alignement)
	ldr	w1,[x19,GS_CRC_FWD]
	sub	w24,w24,2
	cbz	w1,.len
	sub	w24,w24,4			; FCS transmise : la retirer
.len:	cmp	w24,14
	b.lt	.bad
	cmp	w24,1518
	b.gt	.bad
	ldr	w0,[x19,GS_LINK]
	cbz	w0,.done
	add	x0,x23,2
	mov	x1,x24
	bl	netif_rx_put
	ldr	w0,[x19,GS_RX_OK]
	add	w0,w0,1
	str	w0,[x19,GS_RX_OK]
	b	.done
.bad:	ldr	w0,[x19,GS_RX_ERR]
	add	w0,w0,1
	str	w0,[x19,GS_RX_ERR]
.done:	add	w22,w22,1
	and	w22,w22,0xFFFF
	str	w22,[x19,GS_RX_CONS]
	dsb	sy
	str	w22,[x21,RING_SW_INDEX]		; CONS_INDEX : descripteur rendu
	b	.next
.out:	mov	x0,x25
	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Recupere les descripteurs emis, puis vide l'anneau d'emission de netd dans
; l'anneau TX 16 -> x0 = trames emises
genet_tx_poll:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	mov	x25,0
	adr	x19,genet_state
	adr	x20,ge_regs
	ldr	x20,[x20]
	movl	x21,GE_TDMA_R16
	add	x21,x20,x21
.next:	ldr	w0,[x21,RING_HW_INDEX]		; CONS_INDEX materiel
	and	w0,w0,0xFFFF
	str	w0,[x19,GS_TX_CONS]
	ldr	w22,[x19,GS_TX_PROD]
	sub	w1,w22,w0
	and	w1,w1,0xFFFF
	cmp	w1,GE_TX_DESCS
	b.hs	.out				; anneau plein : attendre
	and	w23,w22,GE_TX_DESCS-1
	adr	x0,tx_va			; directement dans le tampon du
	ldr	x0,[x0]				; descripteur
	add	x0,x0,x23,lsl 11
	mov	x3,x0
	str	x3,[sp,-16]!
	bl	netif_tx_get			; -> x0 = longueur (0 : rien)
	ldr	x3,[sp],16
	cbz	x0,.out
	mov	x24,x0
	ldr	w0,[x19,GS_LINK]		; sans lien : trame jetee
	cbz	w0,.next
	add	x25,x25,1
	cmp	x24,60				; trame minimale : bourrage a zero
	b.hs	.padded
	add	x0,x3,x24
.pad:	strb	wzr,[x0],1
	add	x24,x24,1
	cmp	x24,60
	b.lo	.pad
.padded:
	mov	w0,DESC_SIZE
	mul	w0,w23,w0
	add	x0,x0,GE_TDMA_DESC
	add	x0,x20,x0
	lsl	w1,w24,16
	movl	w2,DS_SOP or DS_EOP or DS_TX_CRC or DS_TX_QTAG
	orr	w1,w1,w2
	dsb	sy
	str	w1,[x0]				; LENGTH_STATUS (adresse deja en place)
	add	w22,w22,1
	and	w22,w22,0xFFFF
	str	w22,[x19,GS_TX_PROD]
	dsb	sy
	str	w22,[x21,RING_SW_INDEX]		; PROD_INDEX : la trame part
	ldr	w0,[x19,GS_TX_OK]
	add	w0,w0,1
	str	w0,[x19,GS_TX_OK]
	b	.next
.out:	mov	x0,x25
	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Outils
; ---------------------------------------------------------------------------

; Adresse MAC de la carte (firmware) -> genet_mac ; a defaut, adresse
; administree localement 02:41:52:4D:50:49
genet_read_mac:
	prologue
	adr	x0,fw_mac
	sys	SYS_FW_CALL
	tbnz	x0,63,.local
	adr	x2,fw_mac
	ldr	w0,[x2,20]
	ldrh	w1,[x2,24]
	orr	x0,x0,x1,lsl 32
	cbz	x0,.local
	b	.st
.local:	movl	x0,0x49504D524102
.st:	adr	x1,genet_mac
	str	x0,[x1]
	epilogue

print_mac:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,genet_mac
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

s_eth0		db 'eth0',0
ge_msg_ok	db 'genet : Ethernet GENET v5 pr',0xC3,0xAA,'t, MAC ',0
ge_msg_absent	db 'genet : pas de GENET v5 (QEMU, ou autre carte) : rien a faire',10,0
ge_msg_nomem	db 'genet : plus de m',0xC3,0xA9,'moire DMA',10,0
ge_msg_netd	db 'genet : netd ne r',0xC3,0xA9,'pond pas',10,0
ge_msg_link	db 'genet : lien ',0
ge_msg_mbs	db ' Mb/s',0
ge_msg_fdx	db ' duplex int',0xC3,0xA9,'gral',10,0
ge_msg_hdx	db ' semi-duplex',10,0
ge_msg_down	db 'genet : lien perdu',10,0
m_2p		db ':',0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 16
fw_rev		dw 28, 0, 0x00000001, 4, 0, 0, 0	; GET_FIRMWARE_REVISION
		align 16
fw_mac		dw 32, 0, 0x00010003, 8, 0, 0, 0, 0	; GET_BOARD_MAC_ADDRESS
		align 8
ge_regs		dd 0
ge_canal	dd -1				; son service (< 0 : aucun)
ge_wait		dd GE_WAIT_POLL			; attente en ms
ge_timed	dd 0				; 1 : le dernier reveil etait un delai
ge_msg		rb 512
s_genet		db 'genet',0
		align 8
rx_va		dd 0
rx_phys		dd 0
tx_va		dd 0
tx_phys		dd 0
genet_mac	dd 0
genet_state	rb 96
netif_data
