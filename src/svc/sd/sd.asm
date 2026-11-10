; sd - pilote de la carte SD en service (docs/MICRO-NOYAU.md, section 4,
; etape C) : il pilote lui-meme le controleur SDHCI (registres mappes par
; dev_map, horloge demandee au firmware) et sert blk.sd0 et ses partitions
; (svc/inc/blkserv.inc). Le pilote est celui du noyau (src/kernel/arch/common/
; sdhci.asm, inclus tel quel), en lecture et ecriture (toutes les
; partitions : la personnelle aussi, choix de l'utilisateur du 28/10/2026 ;
; blk_type_ro de blkserv.inc permet d'en proteger un type). Module de
; demarrage (initramfs, init.txt) ;
; en mode noyau, le noyau garde la carte et ce service n'est pas lance.
; Argument : « <nom> » (« sd0 »). Pi 4 (EMMC2 ; QEMU : Arasan) ; Pi 5
; (SDHCI du BCM2712, non teste : docs/PI5.md). dev_map refuse les adresses
; de l'autre carte : chaque controleur s'essaie sans connaitre la carte.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../inc/blk.inc'

SD_BASE_A	= 0xFE340000		; EMMC2 (vrai Pi 4)
SD_BASE_B	= 0xFE300000		; Arasan (QEMU raspi4b)
SD_BASE_C	= 0x1000FFF000		; SDHCI du BCM2712 (Pi 5)
SD_CLOCK_PI5	= 200000000		; horloge de base du Pi 5 (si le firmware se tait)
SD_IRQ_PI5	= 32 + 273		; sa ligne du GIC (mmc@fff000 du DTB : SPI 273)

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; AppKit (initramfs en mode services)

start:
	mov	x19,x0				; « sd0 »
	bl	kits_open
	cbz	x19,quit
	movl	x0,SD_BASE_A			; EMMC2, sinon Arasan
	bl	essayer
	cbnz	x0,.pret
	movl	x0,SD_BASE_B
	bl	essayer
	cbnz	x0,.pret
	movl	x0,SD_BASE_C
	bl	essayer
	cbz	x0,.absente
.pret:	adr	x0,m_25				; vitesse du bus (journal)
	adr	x1,m_50
	adr	x2,sd_hs
	ldr	x2,[x2]
	cmp	x2,0
	csel	x0,x0,x1,eq
	bl	puts
	adr	x0,lire				; le pilote
	adr	x1,blk_lire_fn
	str	x0,[x1]
	adr	x0,ecrire
	adr	x1,blk_ecrire_fn
	str	x0,[x1]
	adr	x0,sd_dma_start
	adr	x1,blk_dma_fn
	str	x0,[x1]
	adr	x0,sd_read_sg
	adr	x1,blk_sg_fn
	str	x0,[x1]
	adr	x0,sd_stats
	adr	x1,blk_stat_fn
	str	x0,[x1]
	mrs	x0,cntfrq_el0			; attente active par defaut
	mov	x1,SD_SPIN_US
	mul	x0,x0,x1
	movl	x1,1000000
	udiv	x0,x0,x1
	adr	x1,sd_spin
	str	x0,[x1]
	adr	x0,uhs
	adr	x1,blk_uhs_fn
	str	x0,[x1]
	mov	x0,x19
	mov	x1,0				; lecture et ecriture
	adr	x2,sd_secteurs			; (CSD, lu a l'initialisation)
	ldr	x2,[x2]
	b	blk_demarrer
.absente:
	adr	x0,m_absente
	bl	puts
quit:	mov	x0,1				; (sur une erreur : init2 relance)
	sys	SYS_EXIT

; x0 = adresse physique du controleur -> x0 = 1 si une carte y repond
essayer:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x1,base_phys
	str	x0,[x1]
	mov	x1,0x100
	sys	SYS_DEV_MAP
	tbnz	x0,63,.non
	mov	x1,SD_IRQ			; ligne d'interruption de ce controleur
	movl	x2,SD_BASE_C
	cmp	x19,x2
	mov	x2,SD_IRQ_PI5
	csel	x1,x2,x1,eq
	adr	x2,sd_irq
	str	x1,[x2]
	bl	sd_init_at
	b	.out
.non:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; Passe en 1,8 V sur EMMC2 ; ailleurs, la carte est seulement reprise a zero
; (QEMU : ce chemin s'y essaie, sans changer de tension)
uhs:
	adr	x0,base_phys
	ldr	x0,[x0]
	movl	x1,SD_BASE_A
	cmp	x0,x1
	cset	x0,eq
	b	sd_uhs_start

; x0 = secteur, x1 = nombre, x2 = adresse, x3 = physique -> 0 ou E_IO
lire:
	mov	x4,x3
	mov	x3,0
	b	transfert
ecrire:
	mov	x4,x3
	mov	x3,1
transfert:
	prologue
	bl	sd_transfer
	cmp	x0,0
	mov	x0,E_IO
	csel	x0,x0,xzr,eq
	epilogue

; ---------------------------------------------------------------------------
;  Ce que sdhci.asm attend du noyau
; ---------------------------------------------------------------------------

; x0 = base (mappee) -> x0 = horloge de base en Hz (firmware : EMMC2 id 12,
; Arasan et Pi 5 id 1 ; sur le Pi 5, 200 MHz si le firmware ne dit rien)
board_sd_clock:
	prologue
	adr	x0,base_phys
	ldr	x0,[x0]
	movl	x1,SD_BASE_A
	cmp	x0,x1
	mov	x0,12
	mov	x1,1
	csel	x0,x0,x1,eq
	adr	x1,fw_horloge
	str	w0,[x1,20]			; identifiant
	str	wzr,[x1,24]
	str	wzr,[x1,4]
	adr	x0,fw_horloge
	sys	SYS_FW_CALL
	adr	x1,fw_horloge
	ldr	w0,[x1,24]
	cbnz	w0,.out
	adr	x1,base_phys
	ldr	x1,[x1]
	movl	x2,SD_BASE_C
	cmp	x1,x2
	b.ne	.out
	movl	x0,SD_CLOCK_PI5
.out:	epilogue

delay_ms:
	sys	SYS_SLEEP
	ret
time_ms:
	sys	SYS_TIME_MS
	ret
spin_lock_irqsave:				; (blkserv serialise deja les appels)
	mov	x0,0
	ret
spin_unlock_irqrestore:
	ret

include 'sdhci.asm'
include '../inc/blkserv.inc'

m_absente db 'sd : aucune carte',10,0
m_25	db 'sd : bus 4 bits, 25 MHz, plusieurs blocs par commande',10,0
m_50	db 'sd : bus 4 bits, 50 MHz (haute vitesse), plusieurs blocs par commande',10,0
	align 8

segment readable writeable
	align 16
fw_horloge	dw 32, 0, 0x00030002, 8, 0, 0, 0, 0	; GET_CLOCK_RATE
		align 16
fw_gpio		dw 32, 0, 0x00038041, 8, 0, 0, 0, 0	; SET_GPIO_STATE (expandeur)
base_phys	dd 0
sd_irq		dd 0				; ligne du GIC du controleur (sdhci.asm)
kit_data
blkserv_data
	align 8
sd_base		rd 1
sd_rca		rd 1
sd_hc		rd 1
sd_ready	rd 1
sd_hs		rd 1			; 1 : haute vitesse (50 MHz)
sd_secteurs	rd 1			; capacite de la carte (CSD ; 0 : inconnue)
	align 8
sd_swbuf	rb 64			; statut de CMD6
sd_dma_ok	rd 1			; 1 : DMA en service
sd_dma_va	rd 1			; table de descripteurs, tampon d'essai
sd_dma_pa	rd 1
sd_phys		rd 1			; physique du morceau en cours (0 : inconnue)
sd_chunk_dma	rd 1			; 1 : ce morceau passe par le DMA
sd_sg		rd 1			; pages de la lecture directe (0 : aucune)
sd_sg_off	rd 1			; octets depuis le debut de la premiere page
sd_base_clock	rd 1
		align 8
sd_s18r		dd 0			; 1 : demander le 1,8 V a l'identification
sd_v18		dd 0			; 1 : passage en 1,8 V commence
sd_spin		dd 0			; attente active avant de dormir (ticks)
st_t0		dd 0			; (compteurs : sd_stats)
st_cmds		dd 0
st_setup	dd 0
st_cmd		dd 0
st_data		dd 0
st_sleeps	dd 0
sd_lock		rd 1
		align 8
sd_test		rb SD_TEST_BYTES
sd_test2	rb SD_TEST_BYTES
