; ============================================================================
;  mailbox.asm - interface "property tags" vers le firmware VideoCore (canal 8)
;
;  Le tampon est en RAM cacheable une fois la MMU active : on le nettoie avant
;  l'envoi (le VC lit la RAM) et on l'invalide apres la reponse (le VC y a ecrit).
;
;  Un seul echange a la fois (mbox_lock) : plusieurs pilotes, dans leurs threads
;  et sur plusieurs coeurs, l'appellent au demarrage ; sans verrou, un coeur
;  pouvait lire la reponse d'un autre, qui l'attendait alors pour toujours
;  (demarrage fige, une fois sur quelques-unes). Les attentes sont bornees
;  (largement : certains appels du vrai firmware sont lents).
; ============================================================================

MBOX_DELAI_MS	= 10000			; filet de securite seulement : allumer l'USB
					; ou recharger le VL805 prend du temps
; x0 = adresse du tampon (aligne sur 16)  ->  x0 = 1 si succes, 0 sinon
mbox_call:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	mbox_verrou
	mov	x20,x0
	mov	x0,x19
	bl	mbox_xfer
	mov	x19,x0
	mov	x0,x20
	bl	mbox_deverrou
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; (sous mbox_lock) x0 = tampon -> x0 = 1 si succes, 0 sinon (ou firmware muet)
mbox_xfer:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mrs	x0,cntfrq_el0			; echeance
	mov	x1,MBOX_DELAI_MS
	mul	x0,x0,x1
	mov	x1,1000
	udiv	x0,x0,x1
	mrs	x1,cntpct_el0
	add	x21,x0,x1
	str	wzr,[x19,4]			; code : requete (le message peut resservir)
	ldr	w20,[x19]			; taille totale du message
	mov	x0,x19				; (x0 : ecrase par l'echeance)
	mov	x1,x20
	bl	dcache_flush_range

	movl	x1,MBOX_BASE
	orr	w2,w19,MBOX_CH_PROP
	orr	w2,w2,GPU_BUS_ALIAS		; adresse vue par le VideoCore
.full:	ldr	w3,[x1,MBOX_STATUS1]
	tbz	w3,31,.send
	mrs	x4,cntpct_el0
	cmp	x4,x21
	b.hs	.mute
	b	.full
.send:	str	w2,[x1,MBOX_WRITE]
.empty:	ldr	w3,[x1,MBOX_STATUS]
	tbz	w3,30,.read
	mrs	x4,cntpct_el0
	cmp	x4,x21
	b.hs	.mute
	b	.empty
.read:	ldr	w3,[x1,MBOX_READ]
	cmp	w3,w2
	b.ne	.empty				; reponse d'un autre canal : on ignore

	mov	x0,x19
	mov	x1,x20
	bl	dcache_flush_range
	ldr	w3,[x19,4]			; code de reponse global
	movl	w4,0x80000000
	cmp	w3,w4
	cset	x0,eq
	b	.out
.mute:	adr	x0,mbox_muet			; firmware muet : echec (compte)
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = DAIF (verrou pris), ou -1 avant la MMU (un seul coeur, et les acces
; exclusifs du verrou ne sont pas surs sans elle)
mbox_verrou:
	mrs	x0,sctlr_el1
	tbz	x0,0,.sans
	adr	x0,mbox_lock
	b	spin_lock_irqsave
.sans:	mov	x0,-1
	ret

; x0 = ce que mbox_verrou a rendu
mbox_deverrou:
	cmn	x0,1
	b.eq	.out
	mov	x1,x0
	adr	x0,mbox_lock
	b	spin_unlock_irqrestore
.out:	ret

; helpers a tampon commun (mbox_power_on, mbox_get_clock) : remplir, echanger
; et lire la reponse sous le verrou
macro mbox_prendre {			; -> x20 (x20 sauve par l'appelant)
	bl	mbox_verrou
	mov	x20,x0
}
macro mbox_rendre {
	mov	x0,x20
	bl	mbox_deverrou
}

; -> x0 = fin de la RAM ARM (base + taille), arrondie au multiple de 2 Mo inferieur
mbox_get_arm_memory:
	prologue
	adr	x0,mem_msg
	bl	mbox_call
	cbz	x0,.fail
	adr	x1,mem_msg_base
	ldp	w2,w3,[x1]
	add	x0,x2,x3
	and	x0,x0,not 0x1FFFFF
	epilogue
.fail:	movl	x0,0x3C000000			; valeur sure si le firmware ne repond pas
	epilogue

		align 16
mem_msg:	dw mem_msg_end - mem_msg, 0
		dw 0x00010005, 8, 0		; GET_ARM_MEMORY
mem_msg_base	dw 0, 0				;  -> base, taille
		dw 0
mem_msg_end:

; x0 = peripherique (3 = USB HCD) -> x0 = etat rendu (bit 0 : alimente), -1 si echec
; Alimente le peripherique et attend qu'il soit pret.
mbox_power_on:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mbox_prendre
	adr	x1,pwr_msg_dev
	str	w19,[x1]
	mov	w0,3				; allume + attendre
	str	w0,[x1,4]
	adr	x0,pwr_msg
	bl	mbox_xfer
	mov	x19,-1
	cbz	x0,.fail
	adr	x0,pwr_msg_dev
	ldr	w19,[x0,4]
.fail:	mbox_rendre
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

		align 16
pwr_msg:	dw pwr_msg_end - pwr_msg, 0
		dw 0x00028001, 8, 0		; SET_POWER_STATE
pwr_msg_dev	dw 0, 0				; peripherique, etat
		dw 0
pwr_msg_end:

; x0 = identifiant d'horloge -> x0 = frequence en Hz (0 si echec)
mbox_get_clock:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mbox_prendre
	adr	x1,clk_msg_id
	str	w19,[x1]
	adr	x0,clk_msg
	bl	mbox_xfer
	mov	x19,0
	cbz	x0,.out
	adr	x0,clk_msg_rate
	ldr	w19,[x0]
.out:	mbox_rendre
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

		align 16
clk_msg:	dw clk_msg_end - clk_msg, 0
		dw 0x00030002, 8, 0		; GET_CLOCK_RATE
clk_msg_id	dw 0
clk_msg_rate	dw 0
		dw 0
clk_msg_end:

; -> x0 = frequence actuelle du processeur en Hz (0 si echec)
mbox_arm_rate:
	prologue
	adr	x0,armcur_msg
	bl	mbox_call
	cbz	x0,.out
	adr	x0,armcur_rate
	ldr	w0,[x0]
.out:	epilogue

		align 16
armcur_msg:	dw armcur_msg_end - armcur_msg, 0
		dw 0x00030002, 8, 0		; GET_CLOCK_RATE
		dw 3				; ARM
armcur_rate	dw 0
		dw 0
armcur_msg_end:

; Le processeur a sa frequence maximale (le firmware le laisse a sa frequence
; de repos, 600 MHz sur Pi 4, tant que personne ne la change ; il reprend la
; main seul si la puce chauffe) -> x0 = frequence en Hz (0 si echec)
mbox_arm_max:
	prologue
	adr	x0,armmax_msg
	bl	mbox_call
	cbz	x0,.non
	adr	x0,armmax_rate
	ldr	w1,[x0]
	cbz	w1,.non
	adr	x0,armset_rate
	str	w1,[x0]
	adr	x0,armset_msg
	bl	mbox_call
	cbz	x0,.non
	adr	x0,armset_rate
	ldr	w0,[x0]
	epilogue
.non:	mov	x0,0
	epilogue

		align 16
armmax_msg:	dw armmax_msg_end - armmax_msg, 0
		dw 0x00030004, 8, 0		; GET_MAX_CLOCK_RATE
		dw 3				; ARM
armmax_rate	dw 0
		dw 0
armmax_msg_end:
		align 16
armset_msg:	dw armset_msg_end - armset_msg, 0
		dw 0x00038002, 12, 0		; SET_CLOCK_RATE
		dw 3				; ARM
armset_rate	dw 0
		dw 0				; (ne pas ignorer le turbo)
		dw 0
armset_msg_end:

; -> x0 = adresse MAC de la carte (6 octets, firmware) ; a defaut, adresse
; administree localement 02:41:52:4D:50:49 (« ARMPI »)
mbox_get_board_mac:
	prologue
	adr	x0,gmac_msg
	bl	mbox_call
	cbz	x0,.local
	adr	x0,gmac_val
	ldr	x0,[x0]
	and	x0,x0,0xFFFFFFFFFFFF
	cbz	x0,.local
	epilogue
.local:	movl	x0,0x49504D524102
	epilogue

		align 16
gmac_msg:	dw gmac_msg_end - gmac_msg, 0
		dw 0x00010003, 6, 0		; GET_BOARD_MAC_ADDRESS
gmac_val	dw 0, 0				; 6 octets (+ remplissage)
		dw 0
gmac_msg_end:

; -> x0 = revision du firmware VideoCore, lue une fois puis gardee. Echec :
; 0x7FFFFFFF (« inconnue », que les pilotes prennent pour un vrai Pi : un
; firmware lent ne doit pas faire croire a QEMU, ce qui coupait l'USB et le
; tactile)
mbox_get_fw_rev:
	prologue
	adr	x0,fwrev_cache
	ldr	w0,[x0]
	cbnz	w0,.out
	adr	x0,fwrev_msg
	bl	mbox_call
	cbz	x0,.inconnue
	adr	x0,fwrev_val
	ldr	w0,[x0]
	cbz	w0,.inconnue
	adr	x1,fwrev_cache
	str	w0,[x1]
	b	.out
.inconnue:
	movl	w0,0x7FFFFFFF
.out:	epilogue

		align 4
fwrev_cache	dw 0

		align 16
fwrev_msg:	dw fwrev_msg_end - fwrev_msg, 0
		dw 0x00000001, 4, 0		; GET_FIRMWARE_REVISION
fwrev_val	dw 0
		dw 0
fwrev_msg_end:
		align 4

		align 8
mbox_lock	dw 0
		align 8
mbox_muet	dd 0				; echanges sans reponse (diagnostic F12)
