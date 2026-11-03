; ============================================================================
;  chip.inc - puce Wi-Fi CYW43455 (protocole) : reprise de src/kernel/wifi.asm
;  (d'apres docs/etude/03-reseau-materiel.md, section 2 ; Circle et Linux
;  n'ont servi que de carte). L'hote SDIO est dans sdio.inc.
;
;  * Bus interne de la puce ("backplane") par fenetre de 32 Ko (F1) ;
;    table EROM pour trouver les coeurs ARM CR4, D11 et SDIO.
;  * Firmware, nvram et CLM lus dans SD0:/firmware/ (AppKit), charges dans
;    la RAM de la puce, puis le CPU de la puce est relache.
;  * Trames SDPCM sur F2 : canal 0 controle (ioctl BCDC), 1 evenements,
;    2 donnees (en-tete BDC + trame Ethernet) -> anneau de netd (netif.inc).
;  * WPA2-PSK : supplicant interne du firmware ("idsup") ; l'hote ne fait
;    que donner la phrase secrete (WLC_SET_WSEC_PMK).
;  * wl_mutex protege la puce ; wl_poll est appele par le thread principal,
;    sans pause sous trafic (wifi_pace), et pendant l'attente d'un ioctl.
; ============================================================================

macro wl_count f* {			; compteur f de wl_st (x16, x17 changes)
	adr	x16,wl_st
	ldr	x17,[x16,f]
	add	x17,x17,1
	str	x17,[x16,f]
}

; --- registres SDIO de la fonction 0 (CCCR / FBR) ------------------------------
CCCR_IOEN	= 0x02
CCCR_IORDY	= 0x03
CCCR_INTEN	= 0x04
CCCR_INTPEND	= 0x05
CCCR_BUSCTL	= 0x07
CCCR_SPEED	= 0x13
FBR1_BLKSIZE	= 0x110
FBR2_BLKSIZE	= 0x210

; --- registres de la fonction 1 -------------------------------------------------
SB_WINLO	= 0x1000A
SB_WINMID	= 0x1000B
SB_WINHI	= 0x1000C
SB_FRAMECTL	= 0x1000D
SB_CLKCSR	= 0x1000E
SB_PULLUP	= 0x1000F
SB_WATERMARK	= 0x10008
SB_RFRMCNT_LO	= 0x1001B
SB_RFRMCNT_HI	= 0x1001C

CLK_FORCE_ALP	= 0x01
CLK_FORCE_HT	= 0x02
CLK_REQ_ALP	= 0x08
CLK_REQ_HT	= 0x10
CLK_NOHWREQ	= 0x20
CLK_ALP_AVAIL	= 0x40
CLK_HT_AVAIL	= 0x80

; --- coeurs du backplane --------------------------------------------------------
CC_BASE		= 0x18000000		; ChipCommon
WR_IOCTL	= 0x408			; registres du " wrapper " d'un coeur
WR_RESETCTL	= 0x800
CR4_CAP		= 0x04
CR4_BANKIDX	= 0x40
CR4_BANKINFO	= 0x44
CR4_CPUHALT	= 0x20
D11_PHYCLK	= 0x04
D11_PHYRESET	= 0x08
SDR_INTSTATUS	= 0x20			; coeur SDIO de la puce
SDR_HOSTINTMASK	= 0x24
SDR_TOSBMB	= 0x40
SDR_TOSBMBDATA	= 0x48
SDR_TOHOSTMBDATA = 0x4C
I_FC_CHANGE	= 0x20
I_FRAME_IND	= 0x40
I_HOST_INT	= 0x80
WL_RAMBASE	= 0x198000		; RAM du CR4 sur le 43455

; --- ioctls du firmware ---------------------------------------------------------
WLC_UP		= 2
WLC_SET_INFRA	= 20
WLC_SET_AUTH	= 22
WLC_SET_SSID	= 26
WLC_SET_PASSIVE	= 49
WLC_DISASSOC	= 52
WLC_GET_PM	= 85
WLC_SET_PM	= 86
WLC_GET_RATE	= 12
WLC_SET_FRAMEBURST = 219
WLC_SET_BAND	= 142
WLC_BAND_AUTO	= 0
WLC_BAND_5G	= 1
WLC_GET_RSSI	= 127
WLC_SET_WSEC	= 134
WLC_SET_WPA_AUTH = 165
WLC_GET_VAR	= 262
WLC_SET_VAR	= 263
WLC_SET_WSEC_PMK = 268

; --- evenements -------------------------------------------------------------------
EV_SET_SSID	= 0
EV_DEAUTH	= 5
EV_DEAUTH_IND	= 6
EV_DISASSOC	= 11
EV_DISASSOC_IND	= 12
EV_LINK		= 16
EV_PSK_SUP	= 46
EV_ESCAN	= 69

; drapeaux de connexion (wl_jf)
JF_SSID		= 1
JF_LINK		= 2
JF_KEYED	= 4
JF_FAIL		= 8
JF_NOTFOUND	= 16
JF_BADKEY	= 32

; etats (wifi_status)
WS_ABSENT	= 0
WS_STARTING	= 1
WS_READY	= 2
WS_JOINING	= 3
WS_CONNECTED	= 4

; resultats de scan (SE_SIZE octets)
SE_SSID		= 0
SE_SSIDLEN	= 32
SE_CHAN		= 33
SE_SEC		= 34			; 0 ouvert, 1 WEP, 2 WPA, 3 WPA2
SE_RSSI		= 36			; dBm (16 bits signes)
SE_BSSID	= 38
SE_SIZE		= 48
SE_MAX		= 64

WL_RXMAX	= 7600			; plus grande trame acceptee
WL_IO_MS	= 2000			; delai d'un ioctl
E_NODEV		= -19
E_IO		= -5
E_ACCES		= -13

; ---------------------------------------------------------------------------
;  Initialisation (au demarrage) : cree le thread "wifi" sur le vrai Pi 4
; ---------------------------------------------------------------------------
wl_sb_window:
	prologue
	stp	x19,x20,[sp,-16]!
	and	w19,w0,0xFFFF8000
	adr	x1,wl_window
	ldr	w2,[x1]
	cmp	w2,w19
	b.eq	.out
	mov	w20,1				; fenetre invalide tant que non ecrite
	str	w20,[x1]
	movl	w0,SB_WINLO
	ubfx	w1,w19,8,8
	bl	sdio_f1w
	tbnz	x0,63,.out
	movl	w0,SB_WINMID
	ubfx	w1,w19,16,8
	bl	sdio_f1w
	tbnz	x0,63,.out
	movl	w0,SB_WINHI
	lsr	w1,w19,24
	bl	sdio_f1w
	tbnz	x0,63,.out
	adr	x1,wl_window
	str	w19,[x1]
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = adresse -> w0 = mot de 32 bits
wl_sb_r32:
	prologue
	str	x19,[sp,-16]!
	mov	w19,w0
	bl	wl_sb_window
	mov	w0,0
	mov	w1,1
	and	w2,w19,0x7FFF
	orr	w2,w2,0x8000			; acces 32 bits
	adr	x3,wl_tmp4
	mov	x4,4
	mov	w5,1
	bl	sdio_cmd53
	adr	x0,wl_tmp4
	ldr	w0,[x0]
	ldr	x19,[sp],16
	epilogue

; w0 = adresse, w1 = valeur
wl_sb_w32:
	prologue
	str	x19,[sp,-16]!
	mov	w19,w0
	adr	x2,wl_tmp4
	str	w1,[x2]
	bl	wl_sb_window
	mov	w0,1
	mov	w1,1
	and	w2,w19,0x7FFF
	orr	w2,w2,0x8000
	adr	x3,wl_tmp4
	mov	x4,4
	mov	w5,1
	bl	sdio_cmd53
	ldr	x19,[sp],16
	epilogue

; w0 = adresse, x1 = tampon, x2 = longueur (multiple de 4), w3 = 1 ecriture
; -> x0 = 1 si ok
wl_sb_mem:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	w19,w0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
.next:	cbz	x21,.ok
	and	w0,w19,0x7FFF			; sans franchir la fenetre, 2 Ko au plus
	mov	w1,0x8000
	sub	w23,w1,w0
	mov	x0,2048
	cmp	x23,x0
	csel	x23,x23,x0,ls
	cmp	x23,x21
	csel	x23,x23,x21,ls
	mov	w0,w19
	bl	wl_sb_window
	mov	w0,w22
	mov	w1,1
	and	w2,w19,0x7FFF
	orr	w2,w2,0x8000
	mov	x3,x20
	mov	x4,x23
	mov	w5,1
	bl	sdio_cmd53
	cbz	x0,.out
	add	w19,w19,w23
	add	x20,x20,x23
	sub	x21,x21,x23
	b	.next
.ok:	mov	x0,1
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = wrapper du coeur, w1 = ioctl avant reset, w2 = ioctl pendant le reset
wl_core_disable:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w19,w0
	mov	w20,w1
	mov	w21,w2
	add	w0,w19,WR_RESETCTL
	bl	wl_sb_r32
	tbnz	w0,0,.inreset			; deja en reset
	add	w0,w19,WR_IOCTL			; horloge forcee + ioctl
	orr	w1,w20,3
	bl	wl_sb_w32
	add	w0,w19,WR_IOCTL
	bl	wl_sb_r32
	add	w0,w19,WR_RESETCTL
	mov	w1,1
	bl	wl_sb_w32
	mov	x0,10
	bl	udelay
	mov	x22,30
.w:	add	w0,w19,WR_RESETCTL
	bl	wl_sb_r32
	tbnz	w0,0,.inreset
	mov	x0,10
	bl	udelay
	subs	x22,x22,1
	b.ne	.w
.inreset:
	add	w0,w19,WR_IOCTL
	orr	w1,w21,3
	bl	wl_sb_w32
	add	w0,w19,WR_IOCTL
	bl	wl_sb_r32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = wrapper, w1 = ioctl avant, w2 = ioctl pendant, w3 = ioctl apres le reset
wl_core_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	w19,w0
	mov	w20,w3
	bl	wl_core_disable
	mov	x21,50
.r:	add	w0,w19,WR_RESETCTL		; sortir du reset
	bl	wl_sb_r32
	tbz	w0,0,.out_reset
	add	w0,w19,WR_RESETCTL
	mov	w1,0
	bl	wl_sb_w32
	mov	x0,40
	bl	udelay
	subs	x21,x21,1
	b.ne	.r
.out_reset:
	add	w0,w19,WR_IOCTL
	orr	w1,w20,1			; horloge du coeur
	bl	wl_sb_w32
	add	w0,w19,WR_IOCTL
	bl	wl_sb_r32
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Table EROM : adresses des coeurs D11, SDIO et ARM CR4 -> x0 = 1 si complets
wl_scan_cores:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	movl	w0,CC_BASE + 0xFC		; pointeur EROM
	bl	wl_sb_r32
	mov	w19,w0
	mov	w20,0				; coeur courant
	mov	x21,0				; index du mot
.n:	cmp	x21,512
	b.hs	.done
	add	w0,w19,w21,lsl 2
	bl	wl_sb_r32
	add	x21,x21,1
	mov	w22,w0
	and	w1,w22,0xF
	cmp	w1,0xF				; fin de table
	b.eq	.done
	cmp	w1,1				; description de coeur (2 mots)
	b.ne	.notci
	ubfx	w20,w22,8,12
	add	x21,x21,1
	b	.n
.notci:	and	w1,w22,7
	cmp	w1,5				; descripteur d'adresse ?
	b.ne	.n
	tbz	w22,3,.nohi			; adresse sur 64 bits : mot haut
	add	x21,x21,1
.nohi:	ubfx	w1,w22,4,2
	cmp	w1,3				; taille decrite par un mot a part
	b.ne	.nosz
	add	w0,w19,w21,lsl 2
	bl	wl_sb_r32
	add	x21,x21,1
	tbz	w0,3,.nosz
	add	x21,x21,1
.nosz:	and	w23,w22,0xFFFFF000
	ubfx	w24,w22,6,2			; 0 registres, 1 pont, 2-3 wrapper
	cmp	w24,1
	b.eq	.n
	mov	x0,0
.id:	adr	x1,wl_coreids
	ldrh	w2,[x1,x0,lsl 1]
	cbz	w2,.n
	cmp	w2,w20
	b.eq	.match
	add	x0,x0,1
	b	.id
.match:	adr	x1,wl_core_regs
	cbz	w24,.slot
	adr	x1,wl_core_wrap
.slot:	ldr	w2,[x1,x0,lsl 2]		; premiere adresse seulement
	cbnz	w2,.n
	str	w23,[x1,x0,lsl 2]
	b	.n
.done:	mov	x0,0
	adr	x1,wl_core_wrap
	ldr	w2,[x1]				; D11
	cbz	w2,.out
	ldr	w2,[x1,8]			; ARM CR4
	cbz	w2,.out
	adr	x1,wl_core_regs
	ldr	w2,[x1,4]			; SDIO
	cbz	w2,.out
	ldr	w2,[x1,8]
	cbz	w2,.out
	mov	x0,1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = taille de la RAM du CR4 (banques)
wl_ram_size:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,wl_core_regs
	ldr	w19,[x0,8]
	add	w0,w19,CR4_CAP
	bl	wl_sb_r32
	and	w1,w0,0xF
	ubfx	w2,w0,4,4
	add	w20,w1,w2			; nombre de banques
	mov	x21,0				; total
	mov	w22,0
.b:	cmp	w22,w20
	b.hs	.end
	add	w0,w19,CR4_BANKIDX
	mov	w1,w22
	bl	wl_sb_w32
	add	w0,w19,CR4_BANKINFO
	bl	wl_sb_r32
	and	w0,w0,0x3F
	add	w0,w0,1
	add	x21,x21,x0,lsl 13		; banques de 8 Ko
	add	w22,w22,1
	b	.b
.end:	mov	x0,x21
	cbz	x0,.def
	cmp	x0,0x200000
	b.ls	.out
.def:	movl	x0,0xC8000			; valeur connue du 43455
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = adresse F1, w1 = masque, x2 = delai ms -> x0 = 1 si un bit du masque est leve
wl_f1_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w19,w0
	mov	w20,w1
	mov	x22,x2
	bl	time_ms
	add	x21,x0,x22
.w:	mov	w0,w19
	bl	sdio_f1r
	tbnz	x0,63,.again
	tst	w0,w20
	b.ne	.ok
.again:	bl	time_ms
	cmp	x0,x21
	b.hs	.ko
	mov	x0,50
	bl	udelay
	b	.w
.ok:	mov	x0,1
	b	.out
.ko:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = adresse F0, w1 = masque, x2 = delai ms -> x0 = 1 si un bit est leve
wl_f0_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w19,w0
	mov	w20,w1
	mov	x22,x2
	bl	time_ms
	add	x21,x0,x22
.w:	mov	w0,w19
	bl	sdio_f0r
	tbnz	x0,63,.again
	tst	w0,w20
	b.ne	.ok
.again:	bl	time_ms
	cmp	x0,x21
	b.hs	.ko
	mov	x0,1
	bl	delay_ms
	b	.w
.ok:	mov	x0,1
	b	.out
.ko:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Mise en route : SDIO, puce, firmware -> x0 = 1 si le firmware tourne
; ---------------------------------------------------------------------------
wl_bringup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!

	; --- hote SDIO de la carte : broches, controleur, carte selectionnee --------
	bl	sdio_host_init			; -> 0 ok, 1 controleur muet, 2 pas de carte
	cmp	x0,1
	b.eq	.fail_hc
	cbnz	x0,.fail_card

	mov	w0,CCCR_BUSCTL			; bus 4 bits, des deux cotes
	bl	sdio_f0r
	tbnz	x0,63,.fail_card
	and	w1,w0,0xFC
	orr	w1,w1,2
	mov	w0,CCCR_BUSCTL
	bl	sdio_f0w
	bl	sdio_host_bus4
	bl	sdio_host_fast
	mov	w0,FBR1_BLKSIZE			; blocs F1 : 64, F2 : 512
	mov	w1,64
	bl	sdio_f0w
	mov	w0,FBR1_BLKSIZE+1
	mov	w1,0
	bl	sdio_f0w
	mov	w0,FBR2_BLKSIZE
	mov	w1,0
	bl	sdio_f0w
	mov	w0,FBR2_BLKSIZE+1
	mov	w1,2
	bl	sdio_f0w
	mov	w0,CCCR_IOEN			; fonction 1
	mov	w1,2
	bl	sdio_f0w
	mov	w0,CCCR_IORDY
	mov	w1,2
	mov	x2,1000
	bl	wl_f0_wait
	cbz	x0,.fail_card

	; --- horloge ALP, identification de la puce -----------------------------------
	movl	w0,SB_CLKCSR
	mov	w1,CLK_NOHWREQ or CLK_REQ_ALP
	bl	sdio_f1w
	movl	w0,SB_CLKCSR
	mov	w1,CLK_ALP_AVAIL
	mov	x2,100
	bl	wl_f1_wait
	cbz	x0,.fail_chip
	movl	w0,SB_CLKCSR
	mov	w1,CLK_NOHWREQ or CLK_FORCE_ALP
	bl	sdio_f1w
	mov	x0,65
	bl	udelay
	adr	x0,wl_window
	mov	w1,1
	str	w1,[x0]
	movl	w0,CC_BASE
	bl	wl_sb_r32
	mov	w19,w0
	adr	x0,wl_msg_chip
	bl	kputs
	and	w0,w19,0xFFFF
	bl	kput_hex
	adr	x0,wl_msg_rev
	bl	kputs
	ubfx	w0,w19,16,4
	bl	kput_dec
	mov	w0,10
	bl	kputc
	and	w0,w19,0xFFFF
	mov	w1,0x4345
	cmp	w0,w1
	b.ne	.fail_chip
	movl	w0,SB_PULLUP
	mov	w1,0
	bl	sdio_f1w
	bl	wl_scan_cores
	cbz	x0,.fail_chip
	bl	wl_ram_size
	adr	x1,wl_ramsize
	str	w0,[x1]

	; --- CPU de la puce a l'arret, MAC 802.11 remise a zero --------------------------
	adr	x0,wl_core_wrap
	ldr	w20,[x0,8]
	add	w0,w20,WR_IOCTL
	bl	wl_sb_r32
	and	w1,w0,CR4_CPUHALT
	mov	w0,w20
	mov	w2,CR4_CPUHALT
	mov	w3,CR4_CPUHALT
	bl	wl_core_reset
	adr	x0,wl_core_wrap
	ldr	w0,[x0]
	mov	w1,D11_PHYRESET or D11_PHYCLK
	mov	w2,D11_PHYCLK
	mov	w3,D11_PHYCLK
	bl	wl_core_reset

	; --- firmware ------------------------------------------------------------------
	adr	x0,wl_path_bin
	bl	boot_file_take
	cbz	x0,.fail_fw
	mov	x21,x0				; pages
	mov	x22,x1				; taille
	adr	x0,wl_msg_fw
	bl	kputs
	mov	x0,x22
	bl	kput_dec
	adr	x0,wl_msg_bytes
	bl	kputs
	adr	x0,wl_ramsize			; dernier mot de la RAM a zero
	ldr	w0,[x0]
	movl	w1,WL_RAMBASE - 4
	add	w0,w0,w1
	mov	w1,0
	bl	wl_sb_w32
	ldr	w23,[x21]			; vecteur de reset
	add	x2,x22,3
	and	x2,x2,0xFFFFFFFFFFFFFFFC
	movl	w0,WL_RAMBASE
	mov	x1,x21
	mov	w3,1
	bl	wl_sb_mem
	mov	x24,x0
	movl	w0,WL_RAMBASE			; verification du debut
	adr	x1,wl_work
	ldr	x1,[x1]
	mov	x2,2048
	mov	w3,0
	bl	wl_sb_mem
	cbz	x0,.fw_bad
	cbz	x24,.fw_bad
	adr	x0,wl_work
	ldr	x0,[x0]
	mov	x1,0
.cmp:	ldr	w2,[x0,x1]
	ldr	w3,[x21,x1]
	cmp	w2,w3
	b.ne	.fw_bad
	add	x1,x1,4
	cmp	x1,2048
	b.lo	.cmp
	mov	x0,x21
	bl	file_free

	; --- nvram : lignes " cle=valeur\0 ", double zero final, longueur en tete ------
	adr	x0,wl_path_txt
	bl	boot_file_take
	cbz	x0,.fail_fw
	mov	x21,x0
	mov	x22,x1
	mov	x0,x21
	mov	x1,x22
	adr	x2,wl_work
	ldr	x2,[x2]
	bl	wl_nvram_condense
	mov	x24,x0				; longueur (multiple de 4)
	mov	x0,x21
	bl	file_free
	adr	x0,wl_ramsize
	ldr	w0,[x0]
	movl	w1,WL_RAMBASE - 4
	add	w0,w0,w1
	sub	w0,w0,w24
	adr	x1,wl_work
	ldr	x1,[x1]
	mov	x2,x24
	mov	w3,1
	bl	wl_sb_mem
	cbz	x0,.fail_fw
	lsr	w1,w24,2			; mot de longueur : n | ~n << 16
	mvn	w2,w1
	and	w1,w1,0xFFFF
	orr	w1,w1,w2,lsl 16
	adr	x0,wl_ramsize
	ldr	w0,[x0]
	movl	w2,WL_RAMBASE - 4
	add	w0,w0,w2
	bl	wl_sb_w32

	; --- demarrage du CPU de la puce ------------------------------------------------
	adr	x0,wl_core_regs
	ldr	w0,[x0,4]
	add	w0,w0,SDR_INTSTATUS
	mov	w1,-1
	bl	wl_sb_w32
	mov	w0,0
	mov	w1,w23
	bl	wl_sb_w32
	adr	x0,wl_core_wrap
	ldr	w0,[x0,8]
	mov	w1,CR4_CPUHALT
	mov	w2,0
	mov	w3,0
	bl	wl_core_reset

	; --- horloge HT, fonction 2 (trames) --------------------------------------------
	movl	w0,SB_CLKCSR
	mov	w1,CLK_REQ_HT
	bl	sdio_f1w
	movl	w0,SB_CLKCSR
	mov	w1,CLK_HT_AVAIL
	mov	x2,2000
	bl	wl_f1_wait
	cbz	x0,.fail_ht
	movl	w0,SB_CLKCSR
	bl	sdio_f1r
	orr	w1,w0,CLK_FORCE_HT
	movl	w0,SB_CLKCSR
	bl	sdio_f1w
	adr	x0,wl_core_regs
	ldr	w0,[x0,4]
	add	w0,w0,SDR_TOSBMBDATA		; version 4 du protocole SDPCM
	mov	w1,4 shl 16
	bl	wl_sb_w32
	mov	w0,CCCR_IOEN
	mov	w1,2 or 4
	bl	sdio_f0w
	mov	w0,CCCR_IORDY
	mov	w1,4
	mov	x2,3000
	bl	wl_f0_wait
	cbz	x0,.fail_ht
	adr	x0,wl_core_regs
	ldr	w0,[x0,4]
	add	w0,w0,SDR_HOSTINTMASK
	mov	w1,0xF0
	bl	wl_sb_w32
	movl	w0,SB_WATERMARK
	mov	w1,8
	bl	sdio_f1w
	mov	w0,CCCR_INTEN
	mov	w1,7
	bl	sdio_f0w

	; --- attendre " firmware pret " (boite aux lettres), 2 s au plus -----------------
	bl	time_ms
	movl	x1,2000
	add	x19,x0,x1
.rdy:	bl	wl_poll
	adr	x0,wl_fwready
	ldr	w0,[x0]
	cbnz	w0,.up
	bl	time_ms
	cmp	x0,x19
	b.hs	.up				; on tente quand meme les ioctls
	mov	x0,2
	bl	thread_sleep
	b	.rdy
.up:	mov	x0,1
	b	.out
.fw_bad:
	mov	x0,x21
	bl	file_free
	adr	x0,wl_retry			; (peut-etre passager : relance)
	mov	w1,1
	str	w1,[x0]
	adr	x0,wl_msg_fwbad
	b	.msg
.fail_hc:
	adr	x0,wl_msg_hc
	b	.msg
.fail_card:
	adr	x0,wl_msg_card
	b	.msg
.fail_chip:
	adr	x0,wl_msg_chipko
	b	.msg
.fail_fw:
	adr	x0,wl_msg_nofw
	b	.msg
.fail_ht:
	adr	x0,wl_retry			; (peut-etre passager : relance)
	mov	w1,1
	str	w1,[x0]
	adr	x0,wl_msg_ht
.msg:	bl	kputs
	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte nvram, x1 = taille, x2 = destination -> x0 = longueur (multiple de 4)
; Supprime commentaires, CR, lignes vides et espaces de tete ; " \n " -> " \0 ".
wl_nvram_condense:
	mov	x3,0				; lecture
	mov	x4,0				; ecriture
	mov	x5,1				; debut de ligne
	mov	x6,0				; dans un commentaire
.c:	cmp	x3,x1
	b.hs	.end
	ldrb	w7,[x0,x3]
	add	x3,x3,1
	cmp	w7,10
	b.eq	.nl
	cmp	w7,13
	b.eq	.c
	cbnz	x6,.c
	cbz	x5,.put
	cmp	w7,'#'
	b.eq	.comment
	cmp	w7,' '
	b.eq	.c
	cmp	w7,9
	b.eq	.c
.put:	strb	w7,[x2,x4]
	add	x4,x4,1
	mov	x5,0
	movl	x8,4000				; une page de travail
	cmp	x4,x8
	b.hs	.end
	b	.c
.comment:
	mov	x6,1
	b	.c
.nl:	mov	x6,0
	cbnz	x5,.c				; ligne vide
	strb	wzr,[x2,x4]
	add	x4,x4,1
	mov	x5,1
	b	.c
.end:	cbnz	x5,.z
	strb	wzr,[x2,x4]			; derniere ligne sans fin de ligne
	add	x4,x4,1
.z:	strb	wzr,[x2,x4]			; zero final
	add	x4,x4,1
.pad:	tst	x4,3
	b.eq	.done
	strb	wzr,[x2,x4]
	add	x4,x4,1
	b	.pad
.done:	mov	x0,x4
	ret

; ---------------------------------------------------------------------------
;  Trames SDPCM
; ---------------------------------------------------------------------------

; w0 = canal, x1 = longueur de la charge (deja a wl_txbuf + 12) -> x0 = 1 si ok
wl_tx_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x2,wl_txbuf
	ldr	x19,[x2]
	add	x20,x1,12
	strh	w20,[x19]			; longueur et son complement
	mvn	w2,w20
	strh	w2,[x19,2]
	adr	x2,wl_txseq
	ldr	w3,[x2]
	strb	w3,[x19,4]
	add	w3,w3,1
	and	w3,w3,0xFF
	str	w3,[x2]
	strb	w0,[x19,5]
	strb	wzr,[x19,6]
	mov	w2,12				; decalage des donnees
	strb	w2,[x19,7]
	str	wzr,[x19,8]
	add	x4,x20,3			; arrondi a 4, puis au bloc si > 512
	and	x4,x4,0xFFFFFFFFFFFFFFFC
	cmp	x4,512
	b.ls	.go
	add	x4,x4,511
	and	x4,x4,0xFFFFFFFFFFFFFE00
.go:	mov	w0,1
	mov	w1,2
	mov	w2,0
	mov	x3,x19
	mov	w5,0
	bl	sdio_cmd53
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = longueur de la trame lue dans wl_rxbuf (0 : aucune, -1 : erreur)
wl_rx_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,wl_rxbuf
	ldr	x19,[x0]
	mov	w0,0
	mov	w1,2
	mov	w2,0
	mov	x3,x19
	mov	x4,12
	mov	w5,0
	bl	sdio_cmd53
	cbz	x0,.err
	ldrh	w20,[x19]
	ldrh	w1,[x19,2]
	orr	w2,w20,w1
	cbz	w2,.none
	eor	w2,w20,w1
	movl	w3,0xFFFF
	cmp	w2,w3
	b.ne	.bad
	cmp	w20,12
	b.lo	.bad
	movl	w3,WL_RXMAX
	cmp	w20,w3
	b.hi	.bad
	sub	x4,x20,12
	cbz	x4,.ok
	add	x4,x4,3
	and	x4,x4,0xFFFFFFFFFFFFFFFC
	cmp	x4,512
	b.ls	.rd
	add	x4,x4,511
	and	x4,x4,0xFFFFFFFFFFFFFE00
.rd:	mov	w0,0
	mov	w1,2
	mov	w2,0
	add	x3,x19,12
	mov	w5,0
	bl	sdio_cmd53
	cbz	x0,.err
.ok:	mov	x0,x20
	b	.out
.none:	mov	x0,0
	b	.out
.bad:	movl	w0,SB_FRAMECTL			; trame incoherente : l'abandonner
	mov	w1,1
	bl	sdio_f1w
	mov	x20,100
.drain:	movl	w0,SB_RFRMCNT_LO
	bl	sdio_f1r
	mov	x19,x0
	movl	w0,SB_RFRMCNT_HI
	bl	sdio_f1r
	orr	x0,x0,x19
	cbz	x0,.err
	subs	x20,x20,1
	b.ne	.drain
.err:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; Scrutation (sous wl_mutex) : interruptions de la puce, trames recues,
; emission de la file -> x0 = interruptions vues (0 : rien de la puce)
wl_poll:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w0,CCCR_INTPEND			; rien en attente : le bus interne de la
	bl	sdio_f0r			; puce n'est pas sollicite
	mov	w20,0
	wl_count NST_POLLS
	cbnz	x0,.chip
	wl_count NST_EMPTY
	b	.tx
.chip:	adr	x0,wl_core_regs
	ldr	w19,[x0,4]
	add	w0,w19,SDR_INTSTATUS
	bl	wl_sb_r32
	and	w20,w0,0xF0
	cbz	w20,.tx
	add	w0,w19,SDR_INTSTATUS		; acquitter
	mov	w1,w20
	bl	wl_sb_w32
	tbz	w20,7,.frames			; boite aux lettres
	add	w0,w19,SDR_TOHOSTMBDATA
	bl	wl_sb_r32
	mov	w19,w0
	adr	x0,wl_core_regs
	ldr	w0,[x0,4]
	add	w0,w0,SDR_TOSBMB
	mov	w1,2				; accuse de reception
	bl	wl_sb_w32
	tbz	w19,3,.frames
	adr	x0,wl_fwready
	mov	w1,1
	str	w1,[x0]
.frames:
	tst	w20,I_FRAME_IND
	b.eq	.tx
	mov	x19,32				; au plus 32 trames par passage
.rx:	mrs	x0,cntpct_el0
	str	x0,[sp,-16]!
	bl	wl_rx_frame
	ldr	x1,[sp],16
	mrs	x2,cntpct_el0
	sub	x2,x2,x1
	adr	x1,wl_st
	ldr	x3,[x1,NST_RX_T]
	add	x3,x3,x2
	str	x3,[x1,NST_RX_T]
	cmp	x0,0
	b.le	.nof
	wl_count NST_RX_N
.nof:	cmp	x0,0
	b.le	.tx
	bl	wl_dispatch
	subs	x19,x19,1
	b.ne	.rx
.tx:	bl	wl_tx_pump
	mov	w0,w20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = longueur de la trame dans wl_rxbuf : controle, evenement ou donnees
wl_dispatch:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x1,wl_rxbuf
	ldr	x19,[x1]
	mov	x20,x0
	ldrb	w1,[x19,9]			; credit d'emission
	adr	x2,wl_txmax
	str	w1,[x2]
	ldrb	w21,[x19,7]			; decalage des donnees
	cmp	x20,x21
	b.ls	.out
	ldrb	w1,[x19,5]
	and	w8,w1,0xF			; canal
	cbz	w8,.ctl
	add	x22,x19,x21			; en-tete BDC
	sub	x20,x20,x21
	cmp	x20,4
	b.lo	.out
	ldrb	w2,[x22,3]
	lsl	w2,w2,2
	add	w2,w2,4
	cmp	x20,x2
	b.ls	.out
	add	x0,x22,x2			; trame Ethernet
	sub	x1,x20,x2
	cmp	w8,1
	b.eq	.event
	cmp	w1,14
	b.lo	.out
	ldrh	w2,[x0,12]
	movl	w3,0x6C88			; 0x886C (gros-boutiste) : evenement
	cmp	w2,w3
	b.eq	.event
	adr	x2,wl_net_on
	ldr	w2,[x2]
	cbz	w2,.out
	bl	netif_rx_put
	b	.out
.event:	bl	wl_event
	b	.out
.ctl:	add	x22,x19,x21			; reponse d'ioctl (BCDC)
	sub	x20,x20,x21
	cmp	x20,16
	b.lo	.out
	adr	x0,wl_iowait
	ldr	w0,[x0]
	cbz	w0,.out
	ldr	w0,[x22,8]			; drapeaux : identifiant << 16
	lsr	w1,w0,16
	adr	x2,wl_ioid
	ldr	w2,[x2]
	cmp	w1,w2
	b.ne	.out
	mov	x1,0
	tbz	w0,0,.st			; erreur signalee
	ldr	w1,[x22,12]
	cbnz	w1,.st
	mov	w1,-1
.st:	sxtw	x1,w1
	adr	x2,wl_iostatus
	str	x1,[x2]
	sub	x2,x20,16			; donnees de la reponse
	adr	x3,wl_iomax
	ldr	x3,[x3]
	cmp	x2,x3
	csel	x2,x2,x3,ls
	adr	x0,wl_iodst
	ldr	x0,[x0]
	add	x1,x22,16
	cbz	x0,.done
	bl	mem_copy
.done:	adr	x0,wl_iodone
	mov	w1,1
	str	w1,[x0]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Vide la file d'emission tant que la puce donne du credit
wl_tx_pump:
	prologue
	stp	x19,x20,[sp,-16]!
.next:	adr	x0,wl_txmax
	ldr	w0,[x0]
	adr	x1,wl_txseq
	ldr	w1,[x1]
	sub	w0,w0,w1
	and	w0,w0,0xFF
	cbz	w0,.nocred
	tbnz	w0,7,.nocred
	adr	x0,wl_net_on			; (sans lien : rien ne part)
	ldr	w0,[x0]
	cbz	w0,.out
	adr	x0,wl_txframe
	ldr	x0,[x0]
	bl	netif_tx_get
	cbz	x0,.out
	mov	x19,x0
	adr	x0,wl_txbuf
	ldr	x20,[x0]
	add	x0,x20,16			; BDC (4 o) puis la trame
	adr	x1,wl_txframe
	ldr	x1,[x1]
	mov	x2,x19
	bl	mem_copy
	mov	w0,0x20				; BDC version 2, priorite 0
	str	w0,[x20,12]
	mrs	x0,cntpct_el0
	str	x0,[sp,-16]!
	mov	w0,2
	add	x1,x19,4
	bl	wl_tx_frame
	ldr	x1,[sp],16
	mrs	x2,cntpct_el0
	sub	x2,x2,x1
	adr	x1,wl_st
	ldr	x3,[x1,NST_TX_T]
	add	x3,x3,x2
	str	x3,[x1,NST_TX_T]
	wl_count NST_TX_N
	b	.next
.nocred:
	bl	netif_tx_pending		; (une trame retenue, pas un tour a vide)
	cbz	x0,.out
	wl_count NST_NOCRED
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = trame Ethernet, x1 = longueur -> x0 = 1 si mise en file (net.asm,
; sous net_lock)
wl_ioctl:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	w19,w0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	cmp	x21,x22				; charge = max(entree, sortie)
	csel	x23,x21,x22,hs
	adr	x0,wl_ioid
	ldr	w1,[x0]
	add	w1,w1,1
	and	w1,w1,0xFFFF
	cbnz	w1,.id
	mov	w1,1
.id:	str	w1,[x0]
	adr	x0,wl_txbuf
	ldr	x24,[x0]
	str	w19,[x24,12]			; en-tete BCDC
	str	w23,[x24,16]
	lsl	w1,w1,16
	cbz	w4,.get
	orr	w1,w1,2
.get:	str	w1,[x24,20]
	str	wzr,[x24,24]
	add	x0,x24,28
	mov	x1,0
.z:	cmp	x1,x23
	b.hs	.copy
	strb	wzr,[x0,x1]
	add	x1,x1,1
	b	.z
.copy:	add	x0,x24,28
	mov	x1,x20
	mov	x2,x21
	bl	mem_copy
	adr	x0,wl_iodst
	str	x20,[x0]
	adr	x0,wl_iomax
	str	x22,[x0]
	adr	x0,wl_iodone
	str	wzr,[x0]
	adr	x0,wl_iowait
	mov	w1,1
	str	w1,[x0]
	mov	w0,0
	add	x1,x23,16
	bl	wl_tx_frame
	cbz	x0,.ioerr
	bl	time_ms
	movl	x1,WL_IO_MS
	add	x19,x0,x1
.w:	bl	wl_poll
	adr	x0,wl_iodone
	ldr	w0,[x0]
	cbnz	w0,.done
	bl	time_ms
	cmp	x0,x19
	b.hs	.ioerr
	mov	x0,1
	bl	thread_sleep
	b	.w
.done:	adr	x0,wl_iostatus
	ldr	x0,[x0]
	b	.out
.ioerr:	mov	x0,E_IO
.out:	adr	x1,wl_iowait
	str	wzr,[x1]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = commande, w1 = valeur 32 bits (ecriture) -> x0 = statut
wl_ioctl_u32:
	prologue
	sub	sp,sp,16
	str	w1,[sp]
	mov	x1,sp
	mov	x2,4
	mov	x3,0
	mov	w4,1
	bl	wl_ioctl
	add	sp,sp,16
	epilogue

; x0 = nom, x1 = donnees, x2 = longueur -> x0 = statut (" nom\0 " + donnees)
wl_iovar_set:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	adr	x1,wl_iobuf
	ldr	x19,[x1]
	mov	x1,x0
	mov	x0,x19
	bl	str_copy_n
	sub	x0,x0,x19			; longueur du nom + 1
	add	x0,x0,1
	mov	x19,x0
	adr	x1,wl_iobuf
	ldr	x1,[x1]
	add	x0,x1,x19
	mov	x1,x20
	mov	x2,x21
	bl	mem_copy
	mov	w0,WLC_SET_VAR
	adr	x1,wl_iobuf
	ldr	x1,[x1]
	add	x2,x19,x21
	mov	x3,0
	mov	w4,1
	bl	wl_ioctl
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom, x1 = destination, x2 = longueur voulue -> x0 = statut
wl_iovar_get:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	adr	x1,wl_iobuf
	ldr	x19,[x1]
	mov	x1,x0
	mov	x0,x19
	bl	str_copy_n
	sub	x2,x0,x19
	add	x2,x2,1
	mov	w0,WLC_GET_VAR
	mov	x1,x19
	mov	x3,x21
	mov	w4,0
	bl	wl_ioctl
	mov	x19,x0
	cbnz	x0,.out
	mov	x0,x20
	adr	x1,wl_iobuf
	ldr	x1,[x1]
	mov	x2,x21
	bl	mem_copy
.out:	mov	x0,x19
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> compteur NST_LINK : debit radio (500 kb/s) | mode d'economie d'energie << 16
wl_link_read:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,16
	str	wzr,[sp]
	mov	w0,WLC_GET_RATE
	mov	x1,sp
	mov	x2,4
	mov	x3,4
	mov	w4,0
	bl	wl_ioctl
	ldr	w19,[sp]
	str	wzr,[sp]
	mov	w0,WLC_GET_PM
	mov	x1,sp
	mov	x2,4
	mov	x3,4
	mov	w4,0
	bl	wl_ioctl
	ldr	w0,[sp]
	orr	w19,w19,w0,lsl 16
	adr	x0,wl_st
	str	x19,[x0,NST_LINK]
	add	sp,sp,16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom, w1 = valeur -> x0 = statut
wl_iovar_u32:
	prologue
	sub	sp,sp,16
	str	w1,[sp]
	mov	x1,sp
	mov	x2,4
	bl	wl_iovar_set
	add	sp,sp,16
	epilogue

; x0 = nom (" bsscfg:... "), w1 = interface, w2 = valeur -> x0 = statut
wl_iovar_u32x2:
	prologue
	sub	sp,sp,16
	str	w1,[sp]
	str	w2,[sp,4]
	mov	x1,sp
	mov	x2,8
	bl	wl_iovar_set
	add	sp,sp,16
	epilogue

; ---------------------------------------------------------------------------
;  Configuration logique (apres le demarrage du firmware) -> x0 = 1 si ok
; ---------------------------------------------------------------------------
wl_setup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,32
	bl	wl_cfg_load
	adr	x0,wl_mutex
	bl	mutex_lock

	; --- CLM (tables reglementaires) par paquets de 1400 octets ---------------------
	adr	x0,wl_path_clm
	bl	boot_file_take
	cbz	x0,.noclm
	mov	x19,x0				; pages
	mov	x20,x1				; taille
	mov	x21,0				; position
	mov	w22,0x1002			; version 1, premier paquet
.clm:	sub	x2,x20,x21
	mov	x3,1400
	cmp	x2,x3
	csel	x2,x2,x3,ls
	add	x3,x21,x2
	cmp	x3,x20
	b.lo	.mid
	orr	w22,w22,4			; dernier paquet
.mid:	adr	x0,wl_work
	ldr	x0,[x0]
	strh	w22,[x0]
	mov	w1,2				; type CLM
	strh	w1,[x0,2]
	str	w2,[x0,4]
	str	wzr,[x0,8]
	str	x2,[sp]
	add	x0,x0,12
	add	x1,x19,x21
	bl	mem_copy
	ldr	x2,[sp]
	add	x21,x21,x2
	adr	x0,wl_str_clmload
	adr	x1,wl_work
	ldr	x1,[x1]
	add	x2,x2,12
	bl	wl_iovar_set
	cbnz	x0,.clm_ko
	mov	w22,0x1000
	cmp	x21,x20
	b.lo	.clm
	b	.clm_free
.clm_ko:
	adr	x0,wl_msg_clm
	bl	kputs
.clm_free:
	mov	x0,x19
	bl	file_free
	b	.mac
.noclm:	adr	x0,wl_msg_noclm
	bl	kputs

	; --- adresse MAC : celle de la puce, sauf l'adresse d'exemple du nvram ----------
.mac:	adr	x0,wl_str_mac
	adr	x1,wl_mac
	mov	x2,8
	bl	wl_iovar_get
	cbnz	x0,.fail
	adr	x0,wl_mac
	ldr	x1,[x0]
	and	x1,x1,0xFFFFFFFFFFFF
	str	x1,[x0]
	movl	x2,0x6CF274EB27B8		; b8:27:eb:74:f2:6c
	cmp	x1,x2
	b.ne	.macok
	bl	mbox_get_board_mac		; adresse de la carte + 1
	mov	x1,x0
	movl	x2,0x10000000000
	add	x1,x1,x2
	and	x1,x1,0xFFFFFFFFFFFF
	adr	x0,wl_mac
	str	x1,[x0]
	adr	x0,wl_str_mac
	adr	x1,wl_mac
	mov	x2,6
	bl	wl_iovar_set
.macok:
	adr	x0,wl_cfg_cc			; pays (optionnel)
	ldrb	w0,[x0]
	cbz	w0,.nocc
	str	xzr,[sp]
	str	xzr,[sp,8]
	adr	x0,wl_cfg_cc
	ldrh	w0,[x0]
	strh	w0,[sp]
	strh	w0,[sp,8]
	mov	w0,-1
	str	w0,[sp,4]
	adr	x0,wl_str_country
	mov	x1,sp
	mov	x2,12
	bl	wl_iovar_set
.nocc:	adr	x0,wl_str_txglom
	mov	w1,0
	bl	wl_iovar_u32
	str	xzr,[sp]			; evenements utiles
	str	xzr,[sp,8]
	mov	w0,0x61				; 0 SET_SSID, 5 DEAUTH, 6 DEAUTH_IND
	strb	w0,[sp]
	mov	w0,0x18				; 11 DISASSOC, 12 DISASSOC_IND
	strb	w0,[sp,1]
	mov	w0,0x01				; 16 LINK
	strb	w0,[sp,2]
	mov	w0,0x40				; 46 PSK_SUP
	strb	w0,[sp,5]
	mov	w0,0x20				; 69 ESCAN_RESULT
	strb	w0,[sp,8]
	adr	x0,wl_str_events
	mov	x1,sp
	mov	x2,16
	bl	wl_iovar_set
	mov	w0,WLC_SET_PM			; pas d'economie d'energie
	mov	w1,0
	bl	wl_ioctl_u32
	adr	x0,wl_str_ampdu_tx		; emission sans agregation : sinon des
	mov	w1,0				; trames se perdent apres l'accuse de
	bl	wl_iovar_u32			; reception (refuse une fois la puce active)
	mov	w0,WLC_SET_FRAMEBURST		; plusieurs trames par occasion d'emettre
	mov	w1,1
	bl	wl_ioctl_u32
	mov	w0,WLC_UP
	mov	x1,sp
	mov	x2,0
	mov	x3,0
	mov	w4,1
	bl	wl_ioctl
	cbnz	x0,.fail

	adr	x0,wl_msg_up			; version et adresse
	bl	kputs
	adr	x0,wl_str_ver
	adr	x1,wl_work
	ldr	x1,[x1]
	mov	x2,128
	bl	wl_iovar_get
	cbnz	x0,.nover
	adr	x0,wl_work
	ldr	x0,[x0]
	strb	wzr,[x0,100]
	mov	x1,0				; jusqu'a la fin de ligne
.vl:	ldrb	w2,[x0,x1]
	cbz	w2,.vp
	cmp	w2,10
	b.eq	.vz
	add	x1,x1,1
	b	.vl
.vz:	strb	wzr,[x0,x1]
.vp:	bl	kputs
.nover:	adr	x0,wl_msg_mac
	bl	kputs
	adr	x0,wl_mac
	bl	put_mac
	mov	w0,10
	bl	kputc
	mov	x19,1
	b	.out
.fail:	adr	x0,wl_msg_ioctl
	bl	kputs
	mov	x19,0
.out:	adr	x0,wl_mutex
	bl	mutex_unlock
	mov	x0,x19
	add	sp,sp,32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Lit /sd/etc/wifi.txt : " ssid=... ", " key=... ", " country=FR " (une par ligne)
wl_event:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	cmp	x1,14 + 10 + 48
	b.lo	.out
	add	x19,x0,24			; message (gros-boutiste)
	sub	x22,x1,24 + 48			; donnees disponibles
	ldr	w20,[x19,4]
	rev	w20,w20				; type
	ldr	w21,[x19,8]
	rev	w21,w21				; statut
	adr	x3,wl_jf
	ldr	w4,[x3]
	cmp	w20,EV_SET_SSID
	b.eq	.ssid
	cmp	w20,EV_LINK
	b.eq	.link
	cmp	w20,EV_PSK_SUP
	b.eq	.psk
	cmp	w20,EV_ESCAN
	b.eq	.escan
	cmp	w20,EV_DEAUTH
	b.eq	.lost
	cmp	w20,EV_DEAUTH_IND
	b.eq	.lost
	cmp	w20,EV_DISASSOC
	b.eq	.lost
	cmp	w20,EV_DISASSOC_IND
	b.eq	.lost
	b	.out
.ssid:	cbnz	w21,.ssid_ko
	orr	w4,w4,JF_SSID
	b	.store
.ssid_ko:
	orr	w4,w4,JF_FAIL
	cmp	w21,3				; aucun reseau de ce nom
	b.ne	.store
	orr	w4,w4,JF_NOTFOUND
	b	.store
.link:	ldrh	w0,[x19,2]
	rev16	w0,w0
	tbz	w0,0,.lost
	orr	w4,w4,JF_LINK
	b	.store
.lost:	mov	w5,JF_LINK or JF_KEYED
	bic	w4,w4,w5
	b	.store
.psk:	cmp	w21,6				; WLC_SUP_KEYED
	b.eq	.keyed
	cmp	w21,4				; etapes intermediaires
	b.eq	.out
	cmp	w21,8
	b.eq	.out
	cmp	w21,11
	b.eq	.out
	orr	w4,w4,JF_BADKEY
	b	.store
.keyed:	orr	w4,w4,JF_KEYED
.store:	str	w4,[x3]
	bl	wl_update_net
	b	.out
.escan:	cmp	w21,8				; resultat partiel : un BSS
	b.ne	.scan_end
	add	x0,x19,48
	mov	x1,x22
	bl	wl_scan_add
	b	.out
.scan_end:
	adr	x0,wl_scan_done
	mov	w1,1
	str	w1,[x0]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Etat de la connexion -> carte de net.asm (lien + cle si WPA)
wl_update_net:
	prologue
	adr	x0,wl_jf
	ldr	w0,[x0]
	mov	w1,0
	tbz	w0,0,.set			; JF_SSID
	tbz	w0,1,.set			; JF_LINK
	adr	x2,wl_open
	ldr	w2,[x2]
	cbnz	w2,.on
	tbz	w0,2,.set			; JF_KEYED
.on:	mov	w1,1
.set:	adr	x0,wl_net_on
	ldr	w2,[x0]
	cmp	w1,w2
	b.eq	.out
	str	w1,[x0]
	mov	w0,WS_READY
	cbz	w1,.st
	mov	w0,WS_CONNECTED
.st:	adr	x2,wl_state
	str	w0,[x2]
	str	w1,[sp,-16]!
	adr	x0,wl_msg_link
	cbnz	w1,.msg
	adr	x0,wl_msg_unlink
.msg:	bl	kputs
	ldr	w0,[sp]				; netd : lien de wlan0 (et le SSID)
	mov	x1,0
	adr	x2,wl_ssid
	bl	netif_link
	add	sp,sp,16
.out:	epilogue

; x0 = resultat d'escan, x1 = longueur : ajoute ou met a jour le BSS
wl_scan_add:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	cmp	x1,12 + 128
	b.lo	.out
	add	x19,x0,12			; wl_bss_info
	sub	x24,x1,12			; octets disponibles
	ldr	w0,[x19,4]			; longueur declaree
	cmp	x0,x24
	csel	x24,x0,x24,lo
	ldrb	w20,[x19,18]			; SSID
	cbz	w20,.out
	cmp	w20,32
	b.hi	.out
	; --- securite : IE RSN (48) -> WPA2, IE WPA (221, 00:50:F2:01) -> WPA ------------
	mov	w23,0
	ldrh	w0,[x19,16]			; capacites : bit 4 confidentialite
	tbz	w0,4,.ies
	mov	w23,1
.ies:	ldrh	w21,[x19,116]			; decalage des IE
	ldr	w22,[x19,120]			; longueur des IE
	add	x22,x21,x22
	cmp	x22,x24
	csel	x22,x22,x24,ls
.ie:	add	x0,x21,2
	cmp	x0,x22
	b.hi	.find
	ldrb	w0,[x19,x21]
	add	x1,x19,x21
	ldrb	w2,[x1,1]
	add	x5,x21,x2
	add	x5,x5,2
	cmp	x5,x22				; element entier dans la zone ?
	b.hi	.find
	cmp	w0,48
	b.ne	.wpa
	mov	w23,3
	b	.ie_next
.wpa:	cmp	w0,221
	b.ne	.ie_next
	cmp	w2,4
	b.lo	.ie_next
	ldr	w3,[x1,2]
	movl	w4,0x01F25000
	cmp	w3,w4
	b.ne	.ie_next
	cmp	w23,3
	b.eq	.ie_next
	mov	w23,2
.ie_next:
	add	x21,x21,2
	add	x21,x21,x2
	b	.ie
	; --- deja vu ? (meme SSID : garder le plus fort) ----------------------------------
.find:	adr	x0,wl_scan
	ldr	x21,[x0]
	adr	x0,wl_scan_count
	ldr	w22,[x0]
	mov	x0,0
.e:	cmp	w0,w22
	b.hs	.new
	mov	x1,SE_SIZE
	madd	x1,x0,x1,x21
	ldrb	w2,[x1,SE_SSIDLEN]
	cmp	w2,w20
	b.ne	.e_next
	mov	x3,0
.sc:	cmp	x3,x20
	b.hs	.same
	ldrb	w4,[x1,x3]
	add	x5,x19,19
	ldrb	w5,[x5,x3]
	cmp	w4,w5
	b.ne	.e_next
	add	x3,x3,1
	b	.sc
.same:	ldrsh	w2,[x1,SE_RSSI]
	ldrsh	w3,[x19,78]
	cmp	w3,w2
	b.le	.out
	mov	x0,x1
	b	.fill
.e_next:
	add	x0,x0,1
	b	.e
.new:	cmp	w22,SE_MAX
	b.hs	.out
	mov	x1,SE_SIZE
	madd	x0,x22,x1,x21
	add	w22,w22,1
	adr	x1,wl_scan_count
	str	w22,[x1]
.fill:	mov	x1,0				; SSID
.f:	add	x2,x19,19
	ldrb	w2,[x2,x1]
	strb	w2,[x0,x1]
	add	x1,x1,1
	cmp	x1,x20
	b.lo	.f
	strb	w20,[x0,SE_SSIDLEN]
	ldrb	w1,[x19,72]			; canal (chanspec, octet bas)
	strb	w1,[x0,SE_CHAN]
	strb	w23,[x0,SE_SEC]
	ldrh	w1,[x19,78]
	strh	w1,[x0,SE_RSSI]
	mov	x1,0
.b:	add	x2,x19,8
	ldrb	w2,[x2,x1]
	add	x3,x0,SE_BSSID
	strb	w2,[x3,x1]
	add	x1,x1,1
	cmp	x1,6
	b.lo	.b
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Operations : scan, connexion, deconnexion
; ---------------------------------------------------------------------------

; -> x0 = nombre de reseaux trouves (table wl_scan), < 0 si erreur
wl_do_scan:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,80
	adr	x0,wl_mutex
	bl	mutex_lock
	adr	x0,wl_scan_count
	str	wzr,[x0]
	adr	x0,wl_scan_done
	str	wzr,[x0]
	mov	w0,WLC_SET_PASSIVE		; scan actif
	mov	w1,0
	bl	wl_ioctl_u32
	mov	x0,sp				; parametres escan (72 octets)
	mov	x1,80
	bl	mem_zero
	mov	w0,1				; version
	str	w0,[sp]
	strh	w0,[sp,4]			; action : debut
	movl	w0,0x4152
	strh	w0,[sp,6]			; identifiant
	mov	w0,-1
	str	w0,[sp,44]			; BSSID ff:ff:ff:ff:ff:ff
	strh	w0,[sp,48]
	mov	w1,2				; tout type de BSS
	strb	w1,[sp,50]
	str	w0,[sp,52]			; sondes et durees par defaut
	str	w0,[sp,56]
	str	w0,[sp,60]
	str	w0,[sp,64]
	adr	x0,wl_str_escan			; 0 canal : tous
	mov	x1,sp
	mov	x2,72
	bl	wl_iovar_set
	mov	x19,x0
	adr	x0,wl_mutex
	bl	mutex_unlock
	cbnz	x19,.err
	bl	time_ms
	movl	x1,10000
	add	x19,x0,x1
.w:	adr	x0,wl_scan_done
	ldr	w0,[x0]
	cbnz	w0,.done
	bl	time_ms
	cmp	x0,x19
	b.hs	.done
	mov	x0,20
	bl	thread_sleep
	b	.w
.done:	adr	x0,wl_scan_count
	ldr	w0,[x0]
	b	.out
.err:	mov	x0,E_IO
.out:	add	sp,sp,80
	ldp	x19,x20,[sp],16
	epilogue

; x0 = SSID (chaine), x1 = phrase secrete (chaine, vide : reseau ouvert)
; -> x0 = 0 si connecte, < 0 sinon
wl_join:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,80
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; longueurs
.l1:	ldrb	w0,[x19,x21]
	cbz	w0,.l2
	add	x21,x21,1
	b	.l1
.l2:	mov	x22,0
.l3:	ldrb	w0,[x20,x22]
	cbz	w0,.lens
	add	x22,x22,1
	b	.l3
.lens:	cbz	x21,.inval
	cmp	x21,32
	b.hi	.inval
	cbz	x22,.lok
	cmp	x22,8
	b.lo	.inval
	cmp	x22,63
	b.hi	.inval
.lok:	adr	x0,wl_mutex
	bl	mutex_lock
	adr	x0,wl_jf			; repartir de zero
	str	wzr,[x0]
	bl	wl_update_net
	mov	w0,WLC_DISASSOC
	mov	x1,sp
	mov	x2,0
	mov	x3,0
	mov	w4,1
	bl	wl_ioctl
	adr	x0,wl_jf
	str	wzr,[x0]
	mov	w0,WS_JOINING
	adr	x1,wl_state
	str	w0,[x1]
	cmp	x22,0
	cset	w0,eq
	adr	x1,wl_open
	str	w0,[x1]
	adr	x0,wl_ssid			; nom du reseau courant
	mov	x1,x19
	bl	str_copy_n
	cbz	x22,.open

	; --- WPA/WPA2-PSK par le supplicant de la puce -----------------------------------
	mov	w0,WLC_SET_WSEC			; AES + TKIP (cles de groupe des AP mixtes)
	mov	w1,6
	bl	wl_ioctl_u32
	adr	x0,wl_str_supwpa
	mov	w1,0
	mov	w2,1
	bl	wl_iovar_u32x2
	adr	x0,wl_str_eapver
	mov	w1,0
	mov	w2,-1
	bl	wl_iovar_u32x2
	adr	x0,wl_str_suptmo
	mov	w1,0
	mov	w2,2500
	bl	wl_iovar_u32x2
	mov	x0,2
	bl	delay_ms
	mov	x0,sp				; { longueur, drapeau phrase, phrase[64] }
	mov	x1,80
	bl	mem_zero
	strh	w22,[sp]
	mov	w0,1
	strh	w0,[sp,2]
	add	x0,sp,4
	mov	x1,x20
	mov	x2,x22
	bl	mem_copy
	mov	w0,WLC_SET_WSEC_PMK
	mov	x1,sp
	mov	x2,68
	mov	x3,0
	mov	w4,1
	bl	wl_ioctl
	mov	x0,x19				; WPA seul d'apres le scan ? sinon WPA2
	bl	wl_scan_sec
	mov	w1,0x80
	cmp	w0,2
	b.ne	.auth
	mov	w1,0x04
	b	.auth
.open:	mov	w0,WLC_SET_WSEC
	mov	w1,0
	bl	wl_ioctl_u32
	adr	x0,wl_str_supwpa
	mov	w1,0
	mov	w2,0
	bl	wl_iovar_u32x2
	mov	w1,0
.auth:	str	w1,[sp,72]
	mov	w0,WLC_SET_BAND			; 5 GHz d'abord, si un reseau a les deux
	adr	x1,wl_band
	ldr	w1,[x1]
	bl	wl_ioctl_u32
	mov	w0,WLC_SET_INFRA
	mov	w1,1
	bl	wl_ioctl_u32
	mov	w0,WLC_SET_AUTH			; systeme ouvert
	mov	w1,0
	bl	wl_ioctl_u32
	mov	w0,WLC_SET_WPA_AUTH
	ldr	w1,[sp,72]
	bl	wl_ioctl_u32
	mov	x0,sp				; { longueur, SSID[32] }
	mov	x1,48
	bl	mem_zero
	str	w21,[sp]
	add	x0,sp,4
	mov	x1,x19
	mov	x2,x21
	bl	mem_copy
	mov	w0,WLC_SET_SSID
	mov	x1,sp
	mov	x2,36
	mov	x3,0
	mov	w4,1
	bl	wl_ioctl
	mov	x21,x0
	adr	x0,wl_mutex
	bl	mutex_unlock
	cbnz	x21,.ioerr

	; --- attente du resultat (le thread "wifi" traite les evenements) ----------------
	bl	time_ms
	movl	x1,20000
	add	x21,x0,x1
.w:	adr	x0,wl_net_on
	ldr	w0,[x0]
	cbnz	w0,.ok
	adr	x0,wl_jf
	ldr	w0,[x0]
	tbnz	w0,5,.badkey			; JF_BADKEY
	tbnz	w0,4,.notfound			; JF_NOTFOUND
	tbnz	w0,3,.failed			; JF_FAIL
	bl	time_ms
	cmp	x0,x21
	b.hs	.timeout
	mov	x0,20
	bl	thread_sleep
	b	.w
.ok:	adr	x0,wl_band			; (la bande qui a marche)
	mov	w1,WLC_BAND_5G
	str	w1,[x0]
	adr	x0,wl_mutex
	bl	mutex_lock
	mov	w0,WLC_SET_PM			; (redit : la connexion peut la remettre)
	mov	w1,0
	bl	wl_ioctl_u32
	adr	x0,wl_str_chanspec
	mov	x1,sp
	mov	x2,4
	bl	wl_iovar_get
	adr	x0,wl_mutex
	bl	mutex_unlock
	adr	x0,wl_msg_chan
	bl	puts
	ldrb	w0,[sp]
	bl	print_dec
	adr	x0,wl_msg_chan2
	bl	puts
	mov	x0,0
	b	.out
.badkey:
	mov	x21,E_ACCES
	b	.abort
.notfound:
	mov	x21,E_NOENT
	b	.abort
.failed:
.ioerr:	mov	x21,E_IO
	b	.abort
.timeout:
	mov	x21,E_TIMEDOUT
.abort:	bl	wl_leave
	adr	x0,wl_band			; echec : la bande au choix de la puce
	str	wzr,[x0]
	mov	x0,x21
	b	.out
.inval:	mov	x0,E_INVAL
.out:	add	sp,sp,80
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = SSID -> x0 = securite vue au dernier scan (SE_SEC), -1 si inconnu
wl_scan_sec:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,wl_scan
	ldr	x20,[x0]
	adr	x0,wl_scan_count
	ldr	w3,[x0]
	mov	x4,0
.e:	cmp	w4,w3
	b.hs	.none
	mov	x5,SE_SIZE
	madd	x5,x4,x5,x20
	ldrb	w6,[x5,SE_SSIDLEN]
	mov	x7,0
.c:	ldrb	w8,[x19,x7]
	cmp	x7,x6
	b.eq	.end
	ldrb	w9,[x5,x7]
	cmp	w8,w9
	b.ne	.next
	add	x7,x7,1
	b	.c
.end:	cbnz	w8,.next			; le nom demande est plus long
	ldrb	w0,[x5,SE_SEC]
	b	.out
.next:	add	x4,x4,1
	b	.e
.none:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; Deconnexion
wl_leave:
	prologue
	sub	sp,sp,16
	adr	x0,wl_mutex
	bl	mutex_lock
	adr	x0,wl_jf
	str	wzr,[x0]
	bl	wl_update_net
	mov	w0,WLC_DISASSOC
	mov	x1,sp
	mov	x2,0
	mov	x3,0
	mov	w4,1
	bl	wl_ioctl
	mov	w0,WS_READY
	adr	x1,wl_state
	str	w0,[x1]
	adr	x0,wl_mutex
	bl	mutex_unlock
	add	sp,sp,16
	epilogue

; ---------------------------------------------------------------------------
;  Appels systeme
; ---------------------------------------------------------------------------

; -> x0 = 0 si le WiFi est utilisable, E_NODEV sinon

wl_name		db 'wifi',0
wl_path_bin	db 'SD0:/firmware/brcmfmac43455-sdio.bin',0	; (bootfiles.asm :
wl_path_txt	db 'SD0:/firmware/brcmfmac43455-sdio.txt',0	; donnes par init)
wl_path_clm	db 'SD0:/firmware/brcmfmac43455-sdio.clm_blob',0
wl_str_clmload	db 'clmload',0
wl_str_mac	db 'cur_etheraddr',0
wl_str_country	db 'country',0
wl_str_txglom	db 'bus:txglom',0
wl_str_chanspec	db 'chanspec',0
wl_msg_chan	db 'WIFI : canal ',0
wl_msg_chan2	db 10,0
wl_str_ampdu_tx	db 'ampdu_tx',0
wl_str_events	db 'event_msgs',0
wl_str_ver	db 'ver',0
wl_str_escan	db 'escan',0
wl_str_supwpa	db 'bsscfg:sup_wpa',0
wl_str_eapver	db 'bsscfg:sup_wpa2_eapver',0
wl_str_suptmo	db 'bsscfg:sup_wpa_tmo',0
wl_msg_chip	db 'WIFI : puce ',0
wl_msg_rev	db ' r',0xC3,0xA9,'vision ',0
wl_msg_fw	db 'WIFI : firmware ',0
wl_msg_bytes	db ' octets',10,0
wl_msg_up	db 'WIFI : ',0
wl_msg_mac	db 10,'WIFI : MAC ',0
wl_msg_link	db 'WIFI : connect',0xC3,0xA9,10,0
wl_msg_unlink	db 'WIFI : d',0xC3,0xA9,'connect',0xC3,0xA9,10,0
wl_msg_fail	db 'WIFI : indisponible',10,0
wl_msg_hc	db 'WIFI : contr',0xC3,0xB4,'leur SDIO muet',10,0
wl_msg_card	db 'WIFI : pas de r',0xC3,0xA9,'ponse SDIO',10,0
wl_msg_chipko	db 'WIFI : puce inattendue',10,0
wl_msg_nofw	db 'WIFI : firmware absent de SD0:/firmware',10,0
wl_msg_fwbad	db 'WIFI : chargement du firmware incorrect',10,0
wl_msg_ht	db 'WIFI : le firmware ne d',0xC3,0xA9,'marre pas',10,0
wl_msg_clm	db 'WIFI : CLM refus',0xC3,0xA9,10,0
wl_msg_noclm	db 'WIFI : CLM absent',10,0
wl_msg_ioctl	db 'WIFI : le firmware ne r',0xC3,0xA9,'pond pas',10,0
		align 8
wl_coreids	dh 0x812, 0x829, 0x83E, 0	; D11, SDIO, ARM CR4
		align 8

macro chip_data {
wl_txbuf	dd 0
wl_rxbuf	dd 0
wl_iobuf	dd 0
wl_scan		dd 0
wl_work		dd 0
wl_txframe	dd 0
wl_iodst	dd 0
wl_iomax	dd 0
wl_iostatus	dd 0
wl_mac		dd 0
wl_window	dw 1
wl_ramsize	dw 0
wl_txseq	dw 0
wl_txmax	dw 1
wl_ioid		dw 0
wl_iowait	dw 0
wl_iodone	dw 0
wl_fwready	dw 0
wl_state	dw 0
wl_jf		dw 0
wl_open		dw 0
wl_net_on	dw 0
wl_scan_count	dw 0
wl_scan_done	dw 0
wl_mutex	dw 0
		align 8
wl_st		rb 64				; compteurs NST_* (copies dans l'anneau : wifi_stats)
		align 8
wl_band		dd WLC_BAND_5G			; bande demandee a la prochaine connexion
wl_opmutex	dw 0
wl_tmp4		dw 0
wl_core_regs	dw 0, 0, 0
wl_core_wrap	dw 0, 0, 0
wl_ssid		rb 68
wl_cfg_cc	rb 4
wl_retry	dw 0				; 1 : echec a relancer (code 1)
		align 8
}
