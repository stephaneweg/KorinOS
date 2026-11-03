; ============================================================================
;  sdio.inc - hote SDIO (repris de src/kernel/arch/pi4/sdio.asm) du Raspberry Pi 4 pour la puce WiFi :
;  controleur Arasan (EMMC1) en PIO, GPIO 34-39 en ALT3
;
;  Une attente apres chaque ecriture de registre (errata Arasan : deux
;  ecritures trop proches peuvent se perdre). CMD52 (octet), CMD53 (blocs de
;  64 o pour F1, 512 o pour F2). Le protocole de la puce est dans wifi.asm.
; ============================================================================

WL_HC		= 0xFE300000		; controleur Arasan (EMMC1), mappe en wl_hc_va

; ---------------------------------------------------------------------------
;  Interface pour wifi.asm :
;    sdio_host_init  -> x0 = 0 carte SDIO selectionnee (25 MHz, bus 1 bit),
;                       1 controleur muet, 2 pas de carte
;    sdio_host_bus4  bus 4 bits cote hote (apres CCCR_BUSCTL cote carte)
;    sdio_host_fast  haute vitesse : 50 MHz demandes, 25 si la carte ne suit pas
;    sdio_cmd52 / sdio_cmd53, sdio_f0r / f1r / f0w / f1w
; ---------------------------------------------------------------------------
sdio_host_init:
	prologue
	str	x19,[sp,-16]!
	bl	wl_chip_reset
	; --- broches : GPIO 34-39 -> ALT3 (Arasan), 48-53 -> ALT0 -------------------
	mov	x19,34
.pin:	mov	w0,w19
	mov	w1,7				; ALT3
	bl	gpio_fsel
	mov	w0,w19
	cmp	w19,34				; 34 (horloge) sans tirage, 35-39 tirage haut
	cset	w1,ne
	bl	gpio_pull
	add	x19,x19,1
	cmp	x19,39
	b.ls	.pin
	mov	x19,48
.pin0:	mov	w0,w19
	mov	w1,4				; ALT0 : l'ancien routage SD est deconnecte
	bl	gpio_fsel
	add	x19,x19,1
	cmp	x19,53
	b.ls	.pin0

	; --- controleur ------------------------------------------------------------
	mrs	x0,cntfrq_el0			; 20 us d'attente tant que l'horloge est lente
	movl	x1,50000
	udiv	x0,x0,x1
	adr	x1,wl_wdelay
	str	x0,[x1]
	mov	w0,1 shl 24
	bl	wl_hc_reset
	mov	x0,10
	bl	delay_ms
	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w1,[x1,SD_CONTROL0]		; alimentation 3,3 V
	orr	w1,w1,0xF00
	mov	x0,SD_CONTROL0
	bl	wl_wr
	movl	x0,400000
	bl	wl_hc_clock
	cbz	x0,.nohc
	mov	x0,SD_IRPT_EN			; scrutation, pas d'IRQ
	mov	w1,0
	bl	wl_wr
	mov	x0,SD_IRPT_MASK
	mov	w1,-1
	bl	wl_wr
	mov	x0,SD_INTERRUPT
	mov	w1,-1
	bl	wl_wr

	; --- enumeration SDIO ---------------------------------------------------------
	mov	w0,0				; CMD0
	mov	w1,0
	mov	w2,CMD_RSP_NONE
	bl	wl_cmd
	mov	x0,2
	bl	delay_ms
	mov	w0,5				; CMD5 : interrogation (sans tension)
	mov	w1,0
	movl	w2,RSP_R3
	bl	wl_cmd
	mov	x19,20
.cmd5:	mov	w0,5				; CMD5 : 3,2-3,4 V jusqu'a " pret "
	movl	w1,0x300000
	movl	w2,RSP_R3
	bl	wl_cmd
	cbz	x0,.cmd5_next
	tbnz	w1,31,.ready
.cmd5_next:
	mov	x0,100
	bl	delay_ms
	subs	x19,x19,1
	b.ne	.cmd5
	b	.nocard
.ready:	mov	w0,3				; CMD3 : adresse relative
	mov	w1,0
	movl	w2,RSP_R6
	bl	wl_cmd
	cbz	x0,.nocard
	and	w19,w1,0xFFFF0000
	mov	w0,7				; CMD7 : selection
	mov	w1,w19
	movl	w2,RSP_R1B
	bl	wl_cmd
	cbz	x0,.nocard
	movl	x0,25000000
	bl	wl_hc_clock
	cbz	x0,.nocard
	mov	x0,0
	b	.out
.nohc:	mov	x0,1
	b	.out
.nocard:
	mov	x0,2
.out:	ldr	x19,[sp],16
	epilogue

sdio_host_bus4:
	prologue
	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w1,[x1,SD_CONTROL0]
	orr	w1,w1,2				; bus 4 bits
	mov	x0,SD_CONTROL0
	bl	wl_wr
	epilogue

; la puce remise a zero (WL_ON, sur l'extenseur du firmware) : un redemarrage
; de l'ARM ne l'atteint pas, elle gardait son etat
wl_chip_reset:
	prologue
	mov	w0,0
	bl	wl_on_set
	mov	x0,20
	bl	delay_ms
	mov	w0,1
	bl	wl_on_set
	mov	x0,150
	bl	delay_ms
	epilogue

; w0 = etat de WL_ON
wl_on_set:
	adr	x1,fw_wlon
	str	w0,[x1,24]
	mov	w2,32
	str	w2,[x1]
	str	wzr,[x1,4]
	movl	w2,0x00038041			; SET_GPIO_STATE
	str	w2,[x1,8]
	mov	w2,8
	str	w2,[x1,12]
	str	wzr,[x1,16]
	mov	w2,WL_ON_GPIO
	str	w2,[x1,20]
	str	wzr,[x1,28]
	mov	x0,x1
	sys	SYS_FW_CALL
	ret

; carte en haute vitesse (CCCR 0x13, si elle le permet), hote en haute vitesse
; a 50 MHz ; la carte ne repond plus : retour a 25 MHz
sdio_host_fast:
	prologue
	mov	w0,CCCR_SPEED
	bl	sdio_f0r
	tbnz	x0,63,.out
	tbz	w0,0,.out			; SHS : pas de haute vitesse
	orr	w1,w0,2				; EHS
	mov	w0,CCCR_SPEED
	bl	sdio_f0w
	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w1,[x1,SD_CONTROL0]
	orr	w1,w1,4				; hote : haute vitesse
	mov	x0,SD_CONTROL0
	bl	wl_wr
	movl	x0,50000000
	bl	wl_hc_clock
	mov	w0,CCCR_SPEED			; encore la ?
	bl	sdio_f0r
	tbnz	x0,63,.back
	tbnz	w0,1,.out
.back:	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w1,[x1,SD_CONTROL0]
	and	w1,w1,0xFFFFFFFB
	mov	x0,SD_CONTROL0
	bl	wl_wr
	movl	x0,25000000
	bl	wl_hc_clock
	mov	w0,CCCR_SPEED
	mov	w1,0
	bl	sdio_f0w
	adr	x0,m_sdio_slow
	bl	puts
.out:	epilogue

; ---------------------------------------------------------------------------
;  Hote SDHCI (Arasan)
; ---------------------------------------------------------------------------

; x0 = registre, w1 = valeur (ecriture puis attente de l'errata)
wl_wr:
	adr	x9,wl_hc_va
	ldr	x9,[x9]
	str	w1,[x9,x0]
; attente apres une ecriture (x9, x10 seulement)
wl_wdelay_wait:
	mrs	x9,cntpct_el0
	adr	x10,wl_wdelay
	ldr	x10,[x10]
	add	x10,x9,x10
.w:	isb
	mrs	x9,cntpct_el0
	cmp	x9,x10
	b.lo	.w
	ret

; w0 = bits de INTERRUPT attendus, x1 = delai ms -> x0 = 1 si ok
; (les bits trouves sont acquittes ; erreur ou delai : x0 = 0)
wl_hc_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	mov	x20,x1
	bl	time_ms
	add	x20,x0,x20
.w:	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w0,[x1,SD_INTERRUPT]
	tst	w0,INT_ERR
	b.ne	.err
	tst	w0,w19
	b.ne	.ok
	bl	time_ms
	cmp	x0,x20
	b.lo	.w
	mov	x0,0
	b	.out
.err:	and	w1,w0,0xFFFFFEFF		; tout sauf l'interruption carte
	adr	x0,wl_errs			; (diagnostic : wifi_stats)
	ldr	x2,[x0]
	add	x2,x2,1
	str	x2,[x0]
	str	w1,[x0,8]
	mov	x0,SD_INTERRUPT
	bl	wl_wr
	mov	x0,0
	b	.out
.ok:	and	w1,w0,w19
	mov	x0,SD_INTERRUPT
	bl	wl_wr
	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = bits de remise a zero de CONTROL1 (24 tout, 25 commande, 26 donnees)
wl_hc_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w1,[x1,SD_CONTROL1]
	orr	w1,w1,w19
	mov	x0,SD_CONTROL1
	bl	wl_wr
	bl	time_ms
	add	x20,x0,100
.w:	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w0,[x1,SD_CONTROL1]
	tst	w0,w19
	b.eq	.out
	bl	time_ms
	cmp	x0,x20
	b.lo	.w
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = frequence (Hz) -> x0 = 1 si horloge stable
wl_hc_clock:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x0
	adr	x1,wl_hc_va			; couper l'horloge carte
	ldr	x1,[x1]
	ldr	w1,[x1,SD_CONTROL1]
	and	w1,w1,0xFFFFFFFB
	mov	x0,SD_CONTROL1
	bl	wl_wr
	adr	x0,wl_clk_base
	ldr	x0,[x0]
	cbnz	x0,.have
	mov	x0,1				; horloge EMMC (id 1)
	bl	mbox_get_clock
	cbnz	x0,.store
	movl	x0,100000000
.store:	adr	x1,wl_clk_base
	str	x0,[x1]
.have:	lsl	x1,x20,1			; diviseur 10 bits : f = base / (2 div)
	add	x0,x0,x1
	sub	x0,x0,1
	udiv	x0,x0,x1
	cmp	x0,0x3FF
	mov	x1,0x3FF
	csel	x0,x0,x1,ls
	and	w1,w0,0xFF
	lsl	w1,w1,8
	ubfx	w2,w0,8,2
	orr	w1,w1,w2,lsl 6
	orr	w1,w1,0xE0000			; delai de donnees maximal
	orr	w1,w1,1				; horloge interne
	mov	x0,SD_CONTROL1
	bl	wl_wr
	bl	time_ms
	add	x19,x0,100
.w:	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w0,[x1,SD_CONTROL1]
	tbnz	w0,1,.stable
	bl	time_ms
	cmp	x0,x19
	b.lo	.w
	mov	x0,0
	b	.out
.stable:
	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w1,[x1,SD_CONTROL1]
	orr	w1,w1,4				; horloge carte
	mov	x0,SD_CONTROL1
	bl	wl_wr
	mrs	x0,cntfrq_el0			; attente d'ecriture : 2 cycles SD
	lsl	x0,x0,1
	udiv	x0,x0,x20
	add	x0,x0,1
	adr	x1,wl_wdelay
	str	x0,[x1]
	mov	x0,2
	bl	delay_ms
	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = index, w1 = argument, w2 = drapeaux CMDTM -> x0 = 1 si ok, w1 = RESP0
wl_cmd:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w19,w0
	mov	w20,w1
	mov	w21,w2
	bl	time_ms
	add	x22,x0,200
.busy:	adr	x1,wl_hc_va			; ligne commande libre ?
	ldr	x1,[x1]
	ldr	w0,[x1,SD_STATUS]
	tst	w0,1
	b.eq	.free
	bl	time_ms
	cmp	x0,x22
	b.lo	.busy
	mov	w0,1 shl 25
	bl	wl_hc_reset
.free:	adr	x1,wl_hc_va			; interruptions pendantes (sauf carte)
	ldr	x1,[x1]
	ldr	w1,[x1,SD_INTERRUPT]
	and	w1,w1,0xFFFFFEFF
	cbz	w1,.arg
	mov	x0,SD_INTERRUPT
	bl	wl_wr
.arg:	mov	x0,SD_ARG1
	mov	w1,w20
	bl	wl_wr
	mov	x0,SD_CMDTM
	orr	w1,w21,w19,lsl 24
	bl	wl_wr
	mov	w0,INT_CMD_DONE
	mov	x1,500
	bl	wl_hc_wait
	cbz	x0,.fail
	ubfx	w0,w21,16,2			; reponse avec occupation (R1b)
	cmp	w0,3
	b.ne	.ok
	mov	w0,INT_DATA_DONE
	mov	x1,500
	bl	wl_hc_wait
.ok:	adr	x1,wl_hc_va
	ldr	x1,[x1]
	ldr	w1,[x1,SD_RESP0]
	mov	x0,1
	b	.out
.fail:	movl	w0,(1 shl 25) or (1 shl 26)
	bl	wl_hc_reset
	mov	x0,0
	mov	w1,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  SDIO
; ---------------------------------------------------------------------------

; w0 = 1 ecriture / 0 lecture, w1 = fonction, w2 = adresse, w3 = donnee
; -> x0 = octet (lu, ou relu apres ecriture), -1 si erreur
sdio_cmd52:
	prologue
	stp	x19,x20,[sp,-16]!
	lsl	w4,w0,31
	orr	w4,w4,w1,lsl 28
	and	w2,w2,0x1FFFF
	orr	w4,w4,w2,lsl 9
	and	w3,w3,0xFF
	orr	w19,w4,w3
	mov	x20,3				; essais
.try:	mov	w0,52
	mov	w1,w19
	movl	w2,RSP_R1
	bl	wl_cmd
	cbz	x0,.again
	movl	w2,0xCB00			; R5 : CRC, commande, erreur, fonction, plage
	tst	w1,w2
	b.ne	.again
	and	x0,x1,0xFF
	b	.out
.again:	subs	x20,x20,1
	b.ne	.try
	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = adresse -> x0 = octet (fonction 0 / fonction 1)
sdio_f0r:
	mov	w2,w0
	mov	w0,0
	mov	w1,0
	mov	w3,0
	b	sdio_cmd52
sdio_f1r:
	mov	w2,w0
	mov	w0,0
	mov	w1,1
	mov	w3,0
	b	sdio_cmd52
; w0 = adresse, w1 = valeur
sdio_f0w:
	mov	w3,w1
	mov	w2,w0
	mov	w0,1
	mov	w1,0
	b	sdio_cmd52
sdio_f1w:
	mov	w3,w1
	mov	w2,w0
	mov	w0,1
	mov	w1,1
	b	sdio_cmd52

; w0 = 1 ecriture, w1 = fonction (1 ou 2), w2 = adresse, x3 = tampon (aligne 4),
; x4 = longueur (multiple de 4), w5 = 1 adresse incrementee -> x0 = 1 si ok
sdio_cmd53:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	w19,w0
	mov	w20,w1
	mov	w21,w2
	mov	x22,x3
	mov	x23,x4
	mov	w24,w5
	mov	x25,64				; taille de bloc de la fonction
	cmp	w20,2
	b.ne	.chunk
	mov	x25,512
.chunk:	cbz	x23,.ok
	cmp	x23,x25
	b.lo	.bytes
	udiv	x26,x23,x25			; mode bloc : au plus 511 blocs
	mov	x0,511
	cmp	x26,x0
	csel	x26,x26,x0,ls
	mul	x27,x26,x25
	orr	w1,w25,w26,lsl 16
	mov	w28,1
	b	.setup
.bytes:	mov	x26,1				; mode octet : une seule commande
	mov	x27,x23
	orr	w1,w27,0x10000
	mov	w28,0
.setup:	mov	x0,SD_BLKSIZECNT
	bl	wl_wr
	lsl	w2,w19,31			; argument CMD53
	orr	w2,w2,w20,lsl 28
	orr	w2,w2,w28,lsl 27
	orr	w2,w2,w24,lsl 26
	and	w3,w21,0x1FFFF
	orr	w2,w2,w3,lsl 9
	cbz	w28,.cnt_b
	orr	w2,w2,w26
	b	.cnt_ok
.cnt_b:	and	w3,w27,0x1FF			; 512 octets : code 0
	orr	w2,w2,w3
.cnt_ok:
	mov	w1,w2
	movl	w2,RSP_R1 or CMD_DATA
	cbnz	w19,.dir
	orr	w2,w2,CMD_READ
.dir:	cmp	x26,1
	b.ls	.single
	orr	w2,w2,CMD_MULTI
	orr	w2,w2,CMD_BLKCNT
.single:
	mov	w0,53
	bl	wl_cmd
	cbz	x0,.fail
.blk:	mov	w0,INT_READ_RDY
	cbz	w19,.rdy
	mov	w0,INT_WRITE_RDY
.rdy:	mov	x1,500
	bl	wl_hc_wait
	cbz	x0,.fail
	mov	x0,x27				; mots de ce bloc
	cbz	w28,.sz
	mov	x0,x25
.sz:	lsr	x0,x0,2
	adr	x1,wl_hc_va
	ldr	x1,[x1]
	cbnz	w19,.pw
.pr:	ldr	w2,[x1,SD_DATA]
	str	w2,[x22],4
	subs	x0,x0,1
	b.ne	.pr
	b	.nb
.pw:	ldr	w2,[x22],4
	str	w2,[x1,SD_DATA]
	bl	wl_wdelay_wait
	subs	x0,x0,1
	b.ne	.pw
.nb:	subs	x26,x26,1
	b.ne	.blk
	mov	w0,INT_DATA_DONE
	mov	x1,500
	bl	wl_hc_wait
	cbz	x0,.fail
	sub	x23,x23,x27
	cbz	w24,.chunk
	add	w21,w21,w27
	b	.chunk
.ok:	mov	x0,1
	b	.out
.fail:	movl	w0,(1 shl 25) or (1 shl 26)
	bl	wl_hc_reset
	mov	x0,0
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

m_sdio_slow	db 'wifi : haute vitesse refusee, bus a 25 MHz',10,0
		align 4

macro sdio_data {
		align 8
wl_hc_va	dd 0
wl_wdelay	dd 0
wl_clk_base	dd 0
wl_errs		dd 0				; erreurs du controleur
wl_lasterr	dd 0				; son registre INTERRUPT a la derniere
wl_errshown	dd 0
}
