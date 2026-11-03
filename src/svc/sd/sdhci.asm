; ============================================================================
;  arch/common/sdhci.asm - carte SD par un controleur SDHCI
;
;  Transferts par le registre DATA (PIO, attente active bornee), ou par ADMA2
;  directement dans le tampon du client, fin signalee par interruption : voir
;  sd_dma_start (apres essai), retour au PIO sur une erreur.
;  Lecture/ecriture de blocs de 512 octets, plusieurs par commande, carte SDSC (adresse en octets)
;  ou SDHC/SDXC (adresse en blocs). Bus 1 bit puis 4 bits, 25 MHz, puis
;  50 MHz (haute vitesse, CMD6) si la carte l'accepte.
;  Protege par un verrou : un seul transfert a la fois, depuis tout coeur.
;
;  Pi 4 reel : le firmware a deja alimente la carte en 3,3 V pour le boot ; on
;  y reste, sauf demande (sd_uhs_start) : carte reprise a zero, 1,8 V, DDR50
;  (50 MHz sur les deux fronts) si elle le peut, sinon SDR25.
; ============================================================================

; Controleurs essayes dans l'ordre : SD_BASE_A puis SD_BASE_B (0 : aucun),
; horloge de base donnee par la carte (board_sd_clock) : board.inc / board.asm

SD_ARG2		= 0x00
SD_BLKSIZECNT	= 0x04
SD_ARG1		= 0x08
SD_CMDTM	= 0x0C
SD_RESP0	= 0x10
SD_RESP1	= 0x14
SD_RESP2	= 0x18
SD_RESP3	= 0x1C
SD_DATA		= 0x20
SD_STATUS	= 0x24
SD_CONTROL0	= 0x28
SD_CONTROL1	= 0x2C
SD_INTERRUPT	= 0x30
SD_IRPT_MASK	= 0x34
SD_IRPT_EN	= 0x38
SD_CONTROL2	= 0x3C
SD_CAPS		= 0x40
SD_ADMA_ADDR	= 0x58
CAPS_ADMA2	= 19			; (bit de SD_CAPS ; QEMU ne l'a pas)
SD_VERSION	= 0xFC

; CMDTM
CMD_RSP_NONE	= 0
CMD_RSP_136	= 1 shl 16
CMD_RSP_48	= 2 shl 16
CMD_RSP_48B	= 3 shl 16
CMD_CRCCHK	= 1 shl 19
CMD_IXCHK	= 1 shl 20
CMD_DATA	= 1 shl 21
CMD_READ	= 1 shl 4
CMD_MULTI	= 1 shl 5
CMD_BLKCNT	= 1 shl 1
CMD_AUTO12	= 1 shl 2
CMD_DMA		= 1 shl 0

RSP_R1		= CMD_RSP_48 or CMD_CRCCHK or CMD_IXCHK
RSP_R1B		= CMD_RSP_48B or CMD_CRCCHK or CMD_IXCHK
RSP_R2		= CMD_RSP_136 or CMD_CRCCHK
RSP_R3		= CMD_RSP_48
RSP_R6		= CMD_RSP_48 or CMD_CRCCHK or CMD_IXCHK
RSP_R7		= CMD_RSP_48 or CMD_CRCCHK or CMD_IXCHK

; INTERRUPT
INT_CMD_DONE	= 1 shl 0
INT_DATA_DONE	= 1 shl 1
INT_WRITE_RDY	= 1 shl 4
INT_READ_RDY	= 1 shl 5
INT_ERR		= 1 shl 15

SD_TIMEOUT_MS	= 500

CTL0_ADMA2	= 2 shl 3		; selection du DMA (CONTROL0)
SD_IRQ		= 158			; ligne du GIC (EMMC2 du Pi 4, Arasan de QEMU)
SD_IRQ_BITS	= 0xFFFF0000 or INT_DATA_DONE	; fin de donnees, erreurs
ADMA_TRAN	= 0x21			; descripteur valide, transfert
ADMA_END	= 2			; dernier de la table
ADMA_MAX	= 0xFC00		; octets par descripteur (multiple de 512)
SD_DMA_LIMIT	= 0x9C000000		; essaye jusqu'a 2,5 Go (dmareach), attendu jusqu'a 3,9
SD_TEST_BLOCKS	= 256			; essai : un morceau entier, plusieurs descripteurs
SD_TEST_BYTES	= SD_TEST_BLOCKS * 512
SD_TABLE_BYTES	= 4096
PAGE_BYTES	= 4096
SD_SPIN_US	= 8000			; attente active d'un DMA : tout un morceau (128 Ko a
					; 25 Mo/s) ; le sommeil coutait 2 a 9 ms par commande
CTL2_V18	= 1 shl 19		; signaux en 1,8 V (CONTROL2)
CTL2_UHS_SHIFT	= 16			; mode UHS (CONTROL2) : 1 SDR25, 4 DDR50
OCR_S18		= 1 shl 24		; ACMD41 : 1,8 V demande, accepte
EXP_SD_1V8	= 128 + 4		; expandeur de GPIO du firmware : tension des signaux
EXP_SD_PWR	= 128 + 6		; alimentation de la carte
UHS_DDR50	= 4
UHS_SDR25	= 1

; --- variables (BSS du noyau) : sd_base, sd_rca, sd_hc, sd_ready, sd_lock -----

; -> x0 = 1 si une carte est prete. Essaie EMMC2 (vrai Pi 4) puis le
; controleur Arasan (ou QEMU raspi4b branche la carte).
sd_init:
	prologue
	movl	x0,SD_BASE_A
	bl	sd_init_at
	cbnz	x0,.ok
  if SD_BASE_B
	movl	x0,SD_BASE_B
	bl	sd_init_at
  end if
.ok:	epilogue

; x0 = base du controleur -> x0 = 1 si une carte est prete
sd_init_at:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,sd_base
	str	x19,[x0]
	adr	x0,sd_base_clock
	str	xzr,[x0]
	adr	x0,sd_v18
	str	xzr,[x0]

	; reset du controleur
	ldr	w0,[x19,SD_CONTROL1]
	orr	w0,w0,1 shl 24			; SRST_HC
	str	w0,[x19,SD_CONTROL1]
	mov	x0,1 shl 24
	bl	sd_wait_clear_c1
	cbz	x0,.fail

	ldr	w0,[x19,SD_CONTROL0]		; alimentation du bus 3,3 V
	orr	w0,w0,0xF00
	str	w0,[x19,SD_CONTROL0]
	mov	x0,10
	bl	delay_ms

	movl	x0,400000			; horloge d'identification
	bl	sd_set_clock
	cbz	x0,.fail

	str	wzr,[x19,SD_IRPT_EN]		; pas d'IRQ, scrutation
	mov	w0,-1
	str	w0,[x19,SD_IRPT_MASK]
	str	w0,[x19,SD_INTERRUPT]

	; --- identification -------------------------------------------------
	mov	w0,0				; CMD0 GO_IDLE
	mov	w1,0
	mov	w2,CMD_RSP_NONE
	bl	sd_cmd
	movl	w0,8				; CMD8 SEND_IF_COND : 2,7-3,6 V, motif AA
	movl	w1,0x1AA
	movl	w2,RSP_R7
	bl	sd_cmd
	adr	x1,sd_hc
	cbz	x0,.v1				; pas de reponse : carte SD v1
	mov	x0,1
	str	x0,[x1]
	b	.acmd41
.v1:	str	xzr,[x1]
	bl	sd_reset_cmd_line

.acmd41:
	mov	x20,200				; ~2 s
.op:	mov	w0,55				; CMD55 APP_CMD
	mov	w1,0
	movl	w2,RSP_R1
	bl	sd_cmd
	movl	w0,41				; ACMD41 SD_SEND_OP_COND
	movl	w1,0x00FF8000			; plage de tension
	adr	x2,sd_hc
	ldr	x2,[x2]
	cbz	x2,.no_hcs
	movl	w1,0x40FF8000			; HCS : on accepte SDHC/SDXC
	adr	x2,sd_s18r
	ldr	x2,[x2]
	cbz	x2,.no_hcs
	orr	w1,w1,OCR_S18
.no_hcs:
	movl	w2,RSP_R3
	bl	sd_cmd
	ldr	w0,[x19,SD_RESP0]
	tbnz	w0,31,.ready
	mov	x0,10
	bl	delay_ms
	subs	x20,x20,1
	b.ne	.op
	b	.fail
.ready:	mov	w20,w0
	ubfx	w0,w0,30,1			; CCS : adressage par blocs
	adr	x1,sd_hc
	str	x0,[x1]
	adr	x0,sd_s18r
	ldr	x0,[x0]
	cbz	x0,.id
	tbz	w20,24,.id			; (OCR_S18 : accepte ?)
	bl	sd_volt_switch
	cbz	x0,.fail
.id:
	movl	w0,2				; CMD2 ALL_SEND_CID
	mov	w1,0
	movl	w2,RSP_R2
	bl	sd_cmd
	cbz	x0,.fail
	movl	w0,3				; CMD3 SEND_RELATIVE_ADDR
	mov	w1,0
	movl	w2,RSP_R6
	bl	sd_cmd
	cbz	x0,.fail
	ldr	w0,[x19,SD_RESP0]
	lsr	w0,w0,16
	lsl	w0,w0,16
	adr	x1,sd_rca
	str	x0,[x1]
	movl	w0,9				; CMD9 SEND_CSD : la capacite
	adr	x1,sd_rca			; (Setup : l'espace libre de la
	ldr	w1,[x1]				; carte)
	movl	w2,RSP_R2
	bl	sd_cmd
	cbz	x0,.sel
	bl	sd_capacite
.sel:	movl	w0,7				; CMD7 SELECT_CARD
	adr	x1,sd_rca
	ldr	w1,[x1]
	movl	w2,RSP_R1B
	bl	sd_cmd
	cbz	x0,.fail
	movl	w0,16				; CMD16 SET_BLOCKLEN 512 (SDSC)
	mov	w1,512
	movl	w2,RSP_R1
	bl	sd_cmd

	; bus 4 bits : ACMD6
	mov	w0,55
	adr	x1,sd_rca
	ldr	w1,[x1]
	movl	w2,RSP_R1
	bl	sd_cmd
	cbz	x0,.fast
	mov	w0,6
	mov	w1,2
	movl	w2,RSP_R1
	bl	sd_cmd
	cbz	x0,.fast
	ldr	w0,[x19,SD_CONTROL0]
	orr	w0,w0,2				; HCTL_DWIDTH 4 bits
	str	w0,[x19,SD_CONTROL0]
.fast:	movl	x0,25000000
	bl	sd_set_clock
	adr	x0,sd_v18
	ldr	x0,[x0]
	cbz	x0,.hs
	bl	sd_uhs_mode
	b	.hs_done
.hs:	adr	x0,sd_hc			; haute vitesse (50 MHz) : SDHC/SDXC
	ldr	x0,[x0]				; (CMD6 depuis la version 1.10)
	cbz	x0,.hs_done
	movl	x0,0x80FFFFF1			; mode 1 (bascule), groupe 1 -> 1
	mov	x1,1
	bl	sd_switch
	cbz	x0,.hs_done
	ldr	w0,[x19,SD_CONTROL0]
	orr	w0,w0,4				; HCTL_HS_EN
	str	w0,[x19,SD_CONTROL0]
	movl	x0,50000000
	bl	sd_set_clock
	adr	x1,sd_hs
	mov	x0,1
	str	x0,[x1]
.hs_done:

	mov	x0,1
	adr	x1,sd_ready
	str	x0,[x1]
	b	.out
.fail:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; CMD6 SWITCH_FUNC : w0 = argument, x1 = fonction attendue du groupe 1 (-1 :
; question seule) -> x0 = 1 si la carte l'a prise (sd_swbuf, statut de 64 octets :
; groupe 1 en bits 379:376, l'octet 16 ; ses fonctions en bits 415:400, l'octet 13)
sd_switch:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w22,w0
	mov	x21,x1
	adr	x0,sd_base
	ldr	x19,[x0]
	mov	w0,64
	orr	w0,w0,1 shl 16			; 1 bloc de 64 octets
	str	w0,[x19,SD_BLKSIZECNT]
	mov	w0,6
	mov	w1,w22
	movl	w2,RSP_R1 or CMD_DATA or CMD_READ
	bl	sd_cmd
	cbz	x0,.no
	mov	x0,INT_READ_RDY
	bl	sd_wait_int
	cbz	x0,.no
	adr	x20,sd_swbuf
	mov	x1,0
.rd:	ldr	w0,[x19,SD_DATA]
	str	w0,[x20,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,16
	b.lo	.rd
	mov	x0,INT_DATA_DONE
	bl	sd_wait_int
	cbz	x0,.no
	tbnz	x21,63,.vu
	ldrb	w0,[x20,16]
	and	w0,w0,0xF
	cmp	w0,w21
	b.ne	.no
.vu:
	mov	x0,1				; (8 cycles au moins avant la
	bl	delay_ms			;  nouvelle horloge)
	mov	x0,1
	b	.out
.no:	ldr	w0,[x19,SD_CONTROL1]		; lignes remises, rien de change
	orr	w0,w0,3 shl 25
	str	w0,[x19,SD_CONTROL1]
	mov	x0,3 shl 25
	bl	sd_wait_clear_c1
	mov	w0,-1
	str	w0,[x19,SD_INTERRUPT]
	mov	x0,0
.out:	mov	w1,512				; blocs de 512 ensuite
	str	w1,[x19,SD_BLKSIZECNT]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = GPIO de l'expandeur, x1 = etat
sd_exp_set:
	prologue
	adr	x2,fw_gpio
	str	wzr,[x2,4]
	str	w0,[x2,20]
	str	w1,[x2,24]
	mov	x0,x2
	sys	SYS_FW_CALL
	epilogue

; Apres ACMD41 accepte : CMD11, puis le passage de la spec, horloge coupee
; (x19 = controleur) -> x0 = 1 si les lignes DAT remontent en 1,8 V
sd_volt_switch:
	prologue
	adr	x0,sd_v18
	mov	x1,1
	str	x1,[x0]
	mov	w0,11				; CMD11 VOLTAGE_SWITCH
	mov	w1,0
	movl	w2,RSP_R1
	bl	sd_cmd
	cbz	x0,.out
	ldr	w0,[x19,SD_CONTROL1]		; horloge carte coupee
	and	w0,w0,not 4
	str	w0,[x19,SD_CONTROL1]
	ldr	w0,[x19,SD_STATUS]		; DAT0-3 tenues basses par la carte
	ubfx	w0,w0,20,4
	cbnz	w0,.non
	ldr	w0,[x19,SD_CONTROL2]
	orr	w0,w0,CTL2_V18
	str	w0,[x19,SD_CONTROL2]
	mov	x0,EXP_SD_1V8
	mov	x1,1
	bl	sd_exp_set
	mov	x0,10
	bl	delay_ms
	ldr	w0,[x19,SD_CONTROL2]
	tbz	w0,19,.non
	ldr	w0,[x19,SD_CONTROL1]		; horloge rendue : la carte relache DAT
	orr	w0,w0,4
	str	w0,[x19,SD_CONTROL1]
	mov	x0,2
	bl	delay_ms
	ldr	w0,[x19,SD_STATUS]
	ubfx	w0,w0,20,4
	cmp	w0,0xF
	b.ne	.non
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	epilogue

; En 1,8 V, bus 4 bits a 25 MHz : DDR50 si la carte le connait, sinon SDR25 ;
; refuse : elle reste a 25 MHz (x19 = controleur)
sd_uhs_mode:
	prologue
	str	x20,[sp,-16]!
	movl	x0,0x00FFFFFF			; mode 0 : question
	mov	x1,-1
	bl	sd_switch
	cbz	x0,.out
	adr	x0,sd_swbuf
	ldrb	w0,[x0,13]
	mov	x20,UHS_SDR25
	tbz	w0,UHS_DDR50,.sw
	mov	x20,UHS_DDR50
.sw:	movl	x0,0x80FFFFF0
	orr	x0,x0,x20
	mov	x1,x20
	bl	sd_switch
	cbz	x0,.out
	ldr	w0,[x19,SD_CONTROL1]		; (le mode change, horloge coupee)
	and	w0,w0,not 4
	str	w0,[x19,SD_CONTROL1]
	ldr	w0,[x19,SD_CONTROL2]
	and	w0,w0,not (7 shl CTL2_UHS_SHIFT)
	orr	w0,w0,w20,lsl CTL2_UHS_SHIFT
	str	w0,[x19,SD_CONTROL2]
	ldr	w0,[x19,SD_CONTROL0]
	orr	w0,w0,4				; HCTL_HS_EN
	str	w0,[x19,SD_CONTROL0]
	movl	x0,50000000
	bl	sd_set_clock
	adr	x1,sd_hs
	mov	x0,1
	str	x0,[x1]
	adr	x0,m_ddr
	cmp	x20,UHS_DDR50
	b.eq	.dit
	adr	x0,m_sdr
.dit:	bl	puts
.out:	ldr	x20,[sp],16
	epilogue

; (BLK_UHS, avant le DMA, en PIO) x0 = 1 : demander le 1,8 V : la carte est
; reprise a zero, puis son debut relu doit etre celui d'avant ; sinon elle est
; rendue au mode d'origine, alimentation coupee si la tension a change -> 0
sd_uhs_start:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x0,sd_base
	ldr	x19,[x0]
	mov	x0,0
	mov	x1,SD_TEST_BLOCKS
	adr	x2,sd_test
	mov	x3,0
	mov	x4,0
	bl	sd_transfer
	cbz	x0,.out
	adr	x0,sd_s18r
	str	x21,[x0]
	adr	x0,sd_ready
	str	xzr,[x0]
	mov	x0,x19
	bl	sd_init_at
	cbz	x0,.bad
	mov	x0,0
	mov	x1,SD_TEST_BLOCKS
	adr	x2,sd_test2
	mov	x3,0
	mov	x4,0
	bl	sd_transfer
	cbz	x0,.bad
	adr	x0,sd_test
	adr	x1,sd_test2
	movl	x2,SD_TEST_BYTES
.cmp:	ldr	x3,[x0],8
	ldr	x4,[x1],8
	cmp	x3,x4
	b.ne	.bad
	subs	x2,x2,8
	b.ne	.cmp
	adr	x0,m_uhs_ok
	bl	puts
	b	.out
.bad:	adr	x0,m_uhs_bad
	bl	puts
	adr	x0,sd_v18
	ldr	x0,[x0]
	cbz	x0,.init
	mov	x0,EXP_SD_PWR
	mov	x1,0
	bl	sd_exp_set
	mov	x0,EXP_SD_1V8
	mov	x1,0
	bl	sd_exp_set
	mov	x0,300
	bl	delay_ms
	mov	x0,EXP_SD_PWR
	mov	x1,1
	bl	sd_exp_set
	mov	x0,100
	bl	delay_ms
.init:	adr	x0,sd_s18r
	str	xzr,[x0]
	mov	x0,x19
	bl	sd_init_at
.out:	mov	x0,0
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

m_ddr		db 'sd : 1,8 V, DDR50',10,0
m_sdr		db 'sd : 1,8 V, SDR25',10,0
m_uhs_ok	db 'sd : carte reprise, debut relu identique',10,0
m_uhs_bad	db 'sd : essai du 1,8 V rate, retour au mode d origine',10,0
		align 4

; (x19 = controleur, CMD9 repondu) la capacite d'apres le CSD -> sd_secteurs.
; La reponse R2 sans son CRC : le bit n du CSD est le bit n-8 de RESP0..3.
sd_capacite:
	ldr	w0,[x19,SD_RESP3]
	ubfx	w0,w0,22,2			; CSD_STRUCTURE
	ldr	w1,[x19,SD_RESP1]
	cbz	w0,.v1
	ubfx	w1,w1,8,22			; v2 (SDHC, SDXC) : C_SIZE, en
	add	x1,x1,1				; unites de 512 Ko
	lsl	x1,x1,10
	b	.fin
.v1:	ubfx	w2,w1,22,10			; v1 (SDSC) : C_SIZE sur 12 bits,
	ldr	w3,[x19,SD_RESP2]		; C_SIZE_MULT, READ_BL_LEN
	and	w4,w3,3
	orr	w2,w2,w4,lsl 10
	ubfx	w4,w1,7,3
	ubfx	w3,w3,8,4
	add	x2,x2,1
	add	w4,w4,2
	lslv	x2,x2,x4
	lslv	x2,x2,x3
	lsr	x1,x2,9
.fin:	adr	x0,sd_secteurs
	str	x1,[x0]
	ret

; x0 = frequence voulue (Hz) -> x0 = 1 si horloge stable
sd_set_clock:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x0
	adr	x0,sd_base
	ldr	x19,[x0]
	ldr	w0,[x19,SD_CONTROL1]		; couper l'horloge carte
	and	w0,w0,not 4
	str	w0,[x19,SD_CONTROL1]
	adr	x0,sd_base_clock
	ldr	x0,[x0]
	cbnz	x0,.have
	mov	x0,x19				; horloge de base de ce controleur
	bl	board_sd_clock
	cbnz	x0,.store
	movl	x0,100000000			; defaut raisonnable
.store:	adr	x1,sd_base_clock
	str	x0,[x1]
.have:	; diviseur SDHCI v3 : f = base / (2 * div), div sur 10 bits
	lsl	x1,x20,1
	add	x0,x0,x1
	sub	x0,x0,1
	udiv	x0,x0,x1			; arrondi superieur
	cmp	x0,0x3FF
	mov	x1,0x3FF
	csel	x0,x0,x1,ls
	and	w1,w0,0xFF			; bits 8-15 : 8 bits bas
	lsl	w1,w1,8
	ubfx	w2,w0,8,2			; bits 6-7 : 2 bits hauts
	orr	w1,w1,w2,lsl 6
	orr	w1,w1,0xE0000			; timeout de donnees max
	orr	w1,w1,1				; horloge interne
	str	w1,[x19,SD_CONTROL1]
	mov	x0,2				; attendre "stable"
	bl	sd_wait_set_c1
	cbz	x0,.out
	ldr	w0,[x19,SD_CONTROL1]
	orr	w0,w0,4				; horloge carte
	str	w0,[x19,SD_CONTROL1]
	mov	x0,2
	bl	delay_ms
	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = masque : attend que ces bits de CONTROL1 tombent -> x0 = 1 si ok
sd_wait_clear_c1:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	time_ms
	add	x20,x0,SD_TIMEOUT_MS
.w:	adr	x0,sd_base
	ldr	x0,[x0]
	ldr	w0,[x0,SD_CONTROL1]
	tst	w0,w19
	b.eq	.ok
	bl	time_ms
	cmp	x0,x20
	b.lo	.w
	mov	x0,0
	b	.out
.ok:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = masque : attend que ces bits de CONTROL1 montent -> x0 = 1 si ok
sd_wait_set_c1:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	time_ms
	add	x20,x0,SD_TIMEOUT_MS
.w:	adr	x0,sd_base
	ldr	x0,[x0]
	ldr	w0,[x0,SD_CONTROL1]
	bics	wzr,w19,w0
	b.eq	.ok
	bl	time_ms
	cmp	x0,x20
	b.lo	.w
	mov	x0,0
	b	.out
.ok:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = masque : attend un bit de INTERRUPT (ou une erreur) -> x0 = 1 si ok
; (le bit attendu est acquitte ; en cas d'erreur, tout est acquitte)
sd_wait_int:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	time_ms
	add	x20,x0,SD_TIMEOUT_MS
.w:	adr	x0,sd_base
	ldr	x1,[x0]
	ldr	w0,[x1,SD_INTERRUPT]
	tst	w0,INT_ERR
	b.ne	.err
	tst	w0,w19
	b.ne	.ok
	bl	time_ms
	cmp	x0,x20
	b.lo	.w
.err:	mov	w0,-1
	str	w0,[x1,SD_INTERRUPT]
	mov	x0,0
	b	.out
.ok:	str	w19,[x1,SD_INTERRUPT]
	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = index, w1 = argument, w2 = drapeaux CMDTM -> x0 = 1 si reponse correcte
sd_cmd:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w20,w0
	mov	w21,w1
	mov	w22,w2
	adr	x0,sd_base
	ldr	x19,[x0]
	bl	time_ms				; attendre que la ligne commande soit libre
	add	x9,x0,SD_TIMEOUT_MS
.busy:	ldr	w0,[x19,SD_STATUS]
	tst	w0,1				; CMD_INHIBIT
	b.eq	.free
	str	x9,[sp,-16]!
	bl	time_ms
	ldr	x9,[sp],16
	cmp	x0,x9
	b.lo	.busy
.free:	mov	w0,-1
	str	w0,[x19,SD_INTERRUPT]
	str	w21,[x19,SD_ARG1]
	orr	w0,w22,w20,lsl 24
	str	w0,[x19,SD_CMDTM]
	mov	x0,INT_CMD_DONE
	bl	sd_wait_int
	cbz	x0,.fail
	ubfx	w0,w22,16,2			; R1b : attendre la fin d'occupation
	cmp	w0,3
	b.ne	.ok
	mov	x0,INT_DATA_DONE
	bl	sd_wait_int
.ok:	mov	x0,1
	b	.out
.fail:	bl	sd_reset_cmd_line
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

sd_reset_cmd_line:
	prologue
	adr	x0,sd_base
	ldr	x1,[x0]
	ldr	w0,[x1,SD_CONTROL1]
	orr	w0,w0,1 shl 25			; SRST_CMD
	str	w0,[x1,SD_CONTROL1]
	mov	x0,1 shl 25
	bl	sd_wait_clear_c1
	epilogue

; Passe en DMA si le controleur l'annonce, si le noyau le permet (ni no_irq ni
; no_sd_dma : IRQ_BIND refuse) et si l'essai est bon : le debut de la carte, lu en PIO puis en DMA,
; doit etre identique.
sd_dma_start:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,sd_base
	ldr	x19,[x0]
	ldr	w0,[x19,SD_CAPS]
	tbz	w0,CAPS_ADMA2,.off
	mov	x0,SD_IRQ
	mov	x1,-1
	movl	x2,IRQ_SHARED			; (avec l'Arasan du Wi-Fi)
	sys	SYS_IRQ_BIND
	tbnz	x0,63,.off
	movl	x0,SD_TABLE_BYTES + SD_TEST_BYTES
	adr	x1,sd_dma_pa
	sys	SYS_DMA_ALLOC
	cmp	x0,0
	b.le	.bad
	adr	x1,sd_dma_va
	str	x0,[x1]
	mov	x0,0				; la reference, en PIO
	mov	x1,SD_TEST_BLOCKS
	adr	x2,sd_test
	mov	x3,0
	mov	x4,0
	bl	sd_transfer
	cbz	x0,.bad
	ldr	w0,[x19,SD_CONTROL0]
	orr	w0,w0,CTL0_ADMA2
	str	w0,[x19,SD_CONTROL0]
	adr	x0,sd_dma_ok
	mov	x1,1
	str	x1,[x0]
	adr	x0,sd_dma_va
	ldr	x2,[x0]
	add	x2,x2,SD_TABLE_BYTES
	adr	x0,sd_dma_pa
	ldr	x4,[x0]
	add	x4,x4,SD_TABLE_BYTES
	mov	x0,0
	mov	x1,SD_TEST_BLOCKS
	mov	x3,0
	bl	sd_transfer
	cbz	x0,.bad
	adr	x0,sd_dma_ok			; (une erreur l'a remis a 0)
	ldr	x0,[x0]
	cbz	x0,.bad
	adr	x0,sd_dma_va
	ldr	x0,[x0]
	add	x0,x0,SD_TABLE_BYTES
	adr	x1,sd_test
	movl	x2,SD_TEST_BYTES
.cmp:	ldr	x3,[x0],8
	ldr	x4,[x1],8
	cmp	x3,x4
	b.ne	.diff
	subs	x2,x2,8
	b.ne	.cmp
	adr	x0,m_dma_on
	bl	puts
	b	.out
.diff:	adr	x0,sd_dma_ok
	str	xzr,[x0]
.bad:	adr	x0,m_dma_bad
	bl	puts
	b	.out
.off:	adr	x0,m_dma_off
	bl	puts
.out:	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

m_dma_on	db 'sd : DMA (ADMA2) en service',10,0
m_dma_bad	db 'sd : essai du DMA rate, PIO',10,0
m_dma_off	db 'sd : PIO (pas d''ADMA2, no_sd_dma ou no_irq)',10,0
m_dma_lost	db 'sd : erreur de DMA, retour au PIO',10,0
		align 4

; x0 = adresse, x1 = octets : vide et invalide le cache (avant et apres un
; DMA vers la memoire)
sd_cache_flush:
	add	x1,x1,x0
	and	x0,x0,not 63
.l:	dc	civac,x0
	add	x0,x0,64
	cmp	x0,x1
	b.lo	.l
	dsb	sy
	ret

; x0 = adresse, x1 = octets : vide le cache (avant un DMA depuis la memoire)
sd_cache_clean:
	add	x1,x1,x0
	and	x0,x0,not 63
.l:	dc	cvac,x0
	add	x0,x0,64
	cmp	x0,x1
	b.lo	.l
	dsb	sy
	ret

; x0 = physique, x1 = virtuelle, x2 = octets, x3 = 1 ecrire : caches, table de
; descripteurs, interruption de fin (x19 = controleur)
sd_dma_prepare:
	prologue
	stp	x20,x21,[sp,-16]!
	mov	x20,x0
	mov	x21,x2
	mov	x0,x1
	mov	x1,x2
	cbnz	x3,.clean
	bl	sd_cache_flush
	b	.tab
.clean:	bl	sd_cache_clean
.tab:	adr	x0,sd_dma_va
	ldr	x0,[x0]
.d:	mov	x1,ADMA_MAX
	cmp	x21,x1
	csel	x1,x21,x1,lo
	mov	w2,ADMA_TRAN
	subs	x21,x21,x1
	b.ne	.more
	orr	w2,w2,ADMA_END
.more:	orr	w2,w2,w1,lsl 16
	stp	w2,w20,[x0],8
	add	x20,x20,x1
	cbnz	x21,.d
	bl	sd_dma_arm
	ldp	x20,x21,[sp],16
	epilogue

; table prete : la donne au controleur (x19 = controleur)
sd_dma_arm:
	dsb	sy
	adr	x0,sd_dma_pa
	ldr	x0,[x0]
	str	w0,[x19,SD_ADMA_ADDR]
	ret

; x0 = pages (adresses physiques), x1 = virtuelle, x2 = octets : comme
; sd_dma_prepare, en lecture, un descripteur par page (sd_sg_off avance)
sd_dma_prepare_sg:
	prologue
	stp	x20,x21,[sp,-16]!
	mov	x20,x0
	mov	x21,x2
	mov	x0,x1
	mov	x1,x2
	bl	sd_cache_flush
	adr	x0,sd_sg_off
	ldr	x3,[x0]
	adr	x0,sd_dma_va
	ldr	x0,[x0]
.d:	lsr	x4,x3,12
	and	x5,x3,0xFFF
	ldr	x4,[x20,x4,lsl 3]
	add	x4,x4,x5
	mov	x6,PAGE_BYTES
	sub	x6,x6,x5
	cmp	x21,x6
	csel	x6,x21,x6,lo
	mov	w2,ADMA_TRAN
	subs	x21,x21,x6
	b.ne	.more
	orr	w2,w2,ADMA_END
.more:	orr	w2,w2,w6,lsl 16
	stp	w2,w4,[x0],8
	add	x3,x3,x6
	cbnz	x21,.d
	adr	x0,sd_sg_off
	str	x3,[x0]
	bl	sd_dma_arm
	ldp	x20,x21,[sp],16
	epilogue

; x0 = premier bloc, x1 = nombre, x2 = fenetre sur l'appelant, x3 = adresses
; physiques de ses pages -> 0, E_IO, ou E_AGAIN si le DMA n'y va pas
sd_read_sg:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x4,sd_dma_ok
	ldr	x4,[x4]
	cbz	x4,.again
	and	x4,x2,0xFFF			; pages touchees
	add	x5,x4,x1,lsl 9
	add	x5,x5,0xFFF
	lsr	x5,x5,12
	movl	x6,SD_DMA_LIMIT
	sub	x6,x6,PAGE_BYTES
	mov	x7,0
.p:	ldr	x8,[x3,x7,lsl 3]
	cmp	x8,x6
	b.hi	.again
	add	x7,x7,1
	cmp	x7,x5
	b.lo	.p
	adr	x5,sd_sg
	str	x3,[x5]
	adr	x5,sd_sg_off
	str	x4,[x5]
	mov	x3,0
	mov	x4,0
	bl	sd_transfer
	adr	x1,sd_sg
	str	xzr,[x1]
	cmp	x0,0
	mov	x0,E_IO
	csel	x0,x0,xzr,eq
	b	.out
.again:	mov	x0,-11				; E_AGAIN
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = compteur : y ajoute le temps depuis st_t0, qui repart de maintenant
sd_st_add:
	mrs	x1,cntpct_el0
	adr	x2,st_t0
	ldr	x3,[x2]
	str	x1,[x2]
	sub	x1,x1,x3
	ldr	x3,[x0]
	add	x3,x3,x1
	str	x3,[x0]
	ret

; x0 = ticks -> x0 = us
sd_st_us:
	mov	x1,1000
	mul	x0,x0,x1
	mul	x0,x0,x1
	mrs	x1,cntfrq_el0
	udiv	x0,x0,x1
	ret

; x0 = attente active en us (-1 : inchangee), x1 = message -> x0 = sommeils
; (compteurs lus et remis a zero)
sd_stats:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	tbnz	x0,63,.lire
	mrs	x1,cntfrq_el0
	mul	x0,x0,x1
	movl	x1,1000000
	udiv	x0,x0,x1
	adr	x1,sd_spin
	str	x0,[x1]
.lire:	adr	x20,st_cmds
	ldr	x0,[x20]
	str	x0,[x19,MSG_A]
	ldr	x0,[x20,8]
	bl	sd_st_us
	str	x0,[x19,MSG_B]
	ldr	x0,[x20,16]
	bl	sd_st_us
	str	x0,[x19,MSG_C]
	ldr	x0,[x20,24]
	bl	sd_st_us
	str	x0,[x19,MSG_D]
	ldr	x0,[x20,32]
	stp	xzr,xzr,[x20]
	stp	xzr,xzr,[x20,16]
	str	xzr,[x20,32]
	ldp	x19,x20,[sp],16
	epilogue

; Attend la fin du DMA, d'abord en sondant (sd_spin), puis en dormant ;
; l'interruption n'est armee que pour dormir : sa ligne est partagee avec le
; Wi-Fi, qu'elle reveille a chaque fois, et sur le meme coeur il nous retarde
; de plusieurs ms (x19 = controleur) -> x0 = 1 si ok
sd_dma_wait:
	prologue
	str	x20,[sp,-16]!
	adr	x0,sd_spin
	ldr	x0,[x0]
	cbz	x0,.dort
	mrs	x1,cntpct_el0
	add	x1,x1,x0
.sp:	ldr	w0,[x19,SD_INTERRUPT]
	tst	w0,INT_ERR
	b.ne	.err
	tst	w0,INT_DATA_DONE
	b.ne	.ok
	mrs	x0,cntpct_el0
	cmp	x0,x1
	b.lo	.sp
.dort:	movl	w0,SD_IRQ_BITS
	str	w0,[x19,SD_IRPT_EN]
	bl	time_ms
	add	x20,x0,SD_TIMEOUT_MS
.w:	ldr	w0,[x19,SD_INTERRUPT]
	tst	w0,INT_ERR
	b.ne	.err
	tst	w0,INT_DATA_DONE
	b.ne	.ok
	adr	x0,st_sleeps
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	mov	x0,SD_IRQ
	mov	x1,SD_TIMEOUT_MS
	sys	SYS_IRQ_WAIT
	bl	time_ms
	cmp	x0,x20
	b.lo	.w
	ldr	w0,[x19,SD_INTERRUPT]		; (derniere chance)
	tst	w0,INT_DATA_DONE
	b.ne	.ok
.err:	mov	w0,-1
	str	w0,[x19,SD_INTERRUPT]
	mov	x0,0
	b	.out
.ok:	mov	w0,INT_DATA_DONE
	str	w0,[x19,SD_INTERRUPT]
	mov	x0,1
.out:	str	wzr,[x19,SD_IRPT_EN]
	ldr	x20,[sp],16
	epilogue

; x0 = premier bloc, x1 = nombre de blocs, x2 = tampon, x3 = 0 lire / 1 ecrire,
; x4 = adresse physique du tampon (0 : inconnue, PIO) -> x0 = 1 si succes
; Plusieurs blocs par commande (CMD18 / CMD25, CMD12 envoye par le
; controleur a la fin : AUTO12) : la carte ne paie son delai d'acces qu'une
; fois par morceau de SD_MULTI_MAX blocs (un seul bloc : CMD17 / CMD24).
SD_MULTI_MAX	= 256
sd_transfer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,x3
	adr	x0,sd_phys
	str	x4,[x0]
	adr	x0,sd_ready
	ldr	x0,[x0]
	cbz	x0,.fail0
	adr	x0,sd_base
	ldr	x19,[x0]
.next:	cbz	x21,.ok0
	adr	x0,sd_lock
	bl	spin_lock_irqsave
	mov	x26,x0
	mrs	x0,cntpct_el0
	adr	x1,st_t0
	str	x0,[x1]
	adr	x0,st_cmds
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	mov	x27,SD_MULTI_MAX		; blocs de ce morceau
	cmp	x21,x27
	csel	x27,x21,x27,lo
	bl	sd_chunk_setup
	mov	w0,512
	orr	w0,w0,w27,lsl 16		; n blocs de 512
	str	w0,[x19,SD_BLKSIZECNT]
	mov	x1,x20
	adr	x0,sd_hc
	ldr	x0,[x0]
	cbnz	x0,.blk
	lsl	x1,x20,9			; SDSC : adresse en octets
.blk:	cmp	x27,1
	b.eq	.single
	cbnz	x23,.wmulti
	mov	w0,18				; READ_MULTIPLE_BLOCK
	movl	w2,RSP_R1 or CMD_DATA or CMD_READ or CMD_MULTI or CMD_BLKCNT or CMD_AUTO12
	b	.cmd
.wmulti:
	mov	w0,25				; WRITE_MULTIPLE_BLOCK
	movl	w2,RSP_R1 or CMD_DATA or CMD_MULTI or CMD_BLKCNT or CMD_AUTO12
	b	.cmd
.single:
	cbnz	x23,.wsingle
	mov	w0,17				; READ_SINGLE_BLOCK
	movl	w2,RSP_R1 or CMD_DATA or CMD_READ
	b	.cmd
.wsingle:
	mov	w0,24				; WRITE_BLOCK
	movl	w2,RSP_R1 or CMD_DATA
.cmd:	adr	x3,sd_chunk_dma
	ldr	x3,[x3]
	cbz	x3,.pio
	orr	w2,w2,CMD_DMA
.pio:	stp	w0,w1,[sp,-16]!
	str	w2,[sp,8]
	adr	x0,st_setup
	bl	sd_st_add
	ldp	w0,w1,[sp]
	ldr	w2,[sp,8]
	add	sp,sp,16
	bl	sd_cmd
	cbz	x0,.fail
	adr	x0,st_cmd
	bl	sd_st_add
	adr	x0,sd_chunk_dma
	ldr	x0,[x0]
	cbz	x0,.piod
	bl	sd_dma_wait
	cbz	x0,.fail
	cbnz	x23,.dmadn
	mov	x0,x22				; (lectures speculatives pendant le DMA)
	lsl	x1,x27,9
	bl	sd_cache_flush
.dmadn:	add	x22,x22,x27,lsl 9
	b	.chunk
.piod:	mov	x28,x27				; blocs restant dans le morceau
.bloc:	cbnz	x23,.write
	mov	x0,INT_READ_RDY
	bl	sd_wait_int
	cbz	x0,.fail
	mov	x24,32
.rd:	ldr	w0,[x19,SD_DATA]		; (16 octets par tour)
	ldr	w1,[x19,SD_DATA]
	ldr	w2,[x19,SD_DATA]
	ldr	w3,[x19,SD_DATA]
	stp	w0,w1,[x22],8
	stp	w2,w3,[x22],8
	subs	x24,x24,1
	b.ne	.rd
	b	.bdone
.write:	mov	x0,INT_WRITE_RDY
	bl	sd_wait_int
	cbz	x0,.fail
	mov	x24,32
.wr:	ldp	w0,w1,[x22],8
	ldp	w2,w3,[x22],8
	str	w0,[x19,SD_DATA]
	str	w1,[x19,SD_DATA]
	str	w2,[x19,SD_DATA]
	str	w3,[x19,SD_DATA]
	subs	x24,x24,1
	b.ne	.wr
.bdone:	subs	x28,x28,1
	b.ne	.bloc
	mov	x0,INT_DATA_DONE		; (apres CMD12 automatique)
	bl	sd_wait_int
	cbz	x0,.fail
.chunk:	adr	x0,st_data
	bl	sd_st_add
	adr	x0,sd_lock
	mov	x1,x26
	bl	spin_unlock_irqrestore
	adr	x0,sd_phys
	ldr	x1,[x0]
	cbz	x1,.nop
	add	x1,x1,x27,lsl 9
	str	x1,[x0]
.nop:	add	x20,x20,x27
	sub	x21,x21,x27
	b	.next
.fail:	str	wzr,[x19,SD_IRPT_EN]
	cmp	x27,1				; morceau rate : CMD12 a la main,
	b.eq	.fail1				; lignes commande et donnees remises
	mov	w0,12				; STOP_TRANSMISSION
	mov	w1,0
	movl	w2,RSP_R1B
	bl	sd_cmd
.fail1:	ldr	w0,[x19,SD_CONTROL1]
	orr	w0,w0,3 shl 25			; SRST_CMD | SRST_DATA
	str	w0,[x19,SD_CONTROL1]
	mov	x0,3 shl 25
	bl	sd_wait_clear_c1
	mov	w0,-1
	str	w0,[x19,SD_INTERRUPT]
	adr	x0,sd_lock
	mov	x1,x26
	bl	spin_unlock_irqrestore
	adr	x0,sd_chunk_dma			; DMA rate : ce morceau, puis la
	ldr	x1,[x0]				; suite, en PIO
	cbz	x1,.fail0
	str	xzr,[x0]
	adr	x0,sd_dma_ok
	str	xzr,[x0]
	adr	x0,m_dma_lost
	bl	puts
	b	.next
.fail0:	mov	x25,0
	b	.out
.ok0:	mov	x25,1
.out:	mov	x0,x25
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sd_transfer) le morceau de x27 blocs depuis x22, a ecrire si x23 : DMA ou
; PIO ? sd_chunk_dma
sd_chunk_setup:
	prologue
	adr	x1,sd_chunk_dma
	str	xzr,[x1]
	adr	x0,sd_dma_ok
	ldr	x0,[x0]
	cbz	x0,.out
	adr	x0,sd_sg
	ldr	x0,[x0]
	cbz	x0,.lin
	mov	x3,1
	str	x3,[x1]
	mov	x1,x22
	lsl	x2,x27,9
	bl	sd_dma_prepare_sg
	b	.out
.lin:	adr	x0,sd_phys
	ldr	x0,[x0]
	cbz	x0,.out
	lsl	x2,x27,9
	add	x3,x0,x2
	movl	x4,SD_DMA_LIMIT
	cmp	x3,x4
	b.hi	.out
	mov	x3,1
	str	x3,[x1]
	mov	x1,x22
	mov	x3,x23
	bl	sd_dma_prepare
.out:	epilogue

