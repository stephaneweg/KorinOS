; ============================================================================
;  elegant/present.inc - presentation : les zones recomposees (cmp_list) du
;  tampon arriere vers le framebuffer
;
;  Comme Onyx (CCompositorTask::Present, option dispdma ; Circle :
;  CDMAChannel::SetupMemCopy2D) : un canal DMA du BCM2711 en mode 2D lit
;  chaque zone EN PLACE dans le tampon arriere (pas source = largeur de
;  l'ecran) et l'ecrit dans le framebuffer (pas = fb_pitch). Les zones d'une
;  recomposition forment une chaine de blocs de controle (un par zone),
;  lancee d'un coup ; Elegant repart aussitot (requetes, entrees). Avant de
;  redessiner, compose attend la fin du transfert (present_wait) : endormi
;  sur l'interruption du canal (dernier bloc de la chaine, dma_irq), ou le
;  canal interroge entre deux SYS_YIELD si le noyau n'en donne pas (no_irq).
;
;  * tampon arriere : dma_alloc DMA_CACHED (contigu, cache : le dessin reste
;    rapide) ; chaque ligne d'une zone est nettoyee (DC CVAC, permis en EL0
;    par SCTLR_EL1.UCI) avant le lancement ;
;  * blocs de controle et essai : une page dma_alloc non cachee ;
;  * canal : le plus haut des canaux complets (0-6) que le firmware laisse a
;    l'ARM (GET_DMA_CHANNELS) : 6 sur le Pi 4 (comme le framebuffer de
;    Circle), 5 sous QEMU. Le son du noyau prend les canaux « lite » (7 et
;    plus : audio.asm), les autres pilotes (sd, usbd, genet) ont leur propre
;    DMA ; rien d'autre ne touche aux canaux 0-6 ;
;  * adresses de bus : physique | 0xC0000000 (premier Go, que le noyau gere
;    seul) ;
;  * essai au demarrage : 2 lignes de 16 octets (YLENGTH = 1) d'une zone a
;    une autre de la page non cachee. Le vrai BCM2711 copie YLENGTH + 1
;    lignes (Circle : nBlockCount - 1), QEMU en copie YLENGTH : l'essai donne
;    le decalage (dma_ybias). En 128 bits (comme Circle) : QEMU ne l'emule
;    pas (erreur du canal), d'ou la copie processeur sous QEMU ; l'option
;    « dispdma_qemu » y essaie alors 32 bits (bon pour verifier la chaine de
;    blocs, mais lent : QEMU copie mot a mot, ~100 ms par ecran). Echec
;    (rien de copie, erreur, pas de reponse en 50 ms) -> copie par le
;    processeur ;
;  * en marche : un transfert sans fin apres DMA_TIMEOUT ms, ou une erreur
;    du canal -> canal remis a zero, retour definitif a la copie processeur,
;    tout l'ecran recompose ;
;  * SD0:/etc/options.txt « no_dispdma » : copie par le processeur ;
;    « dispdma_qemu » : le DMA en 32 bits sous QEMU (essai).
;  Le journal dit le chemin pris (« elegant : copie vers l'ecran par ... »).
; ============================================================================

DMA_PHYS	= 0xFE007000		; DMA du BCM2711 (canaux 0-14), adresse ARM
DMA_ENABLE_R	= 0xFF0
DMA_CS		= 0x00
DMA_CONBLK	= 0x04
DMA_DEBUG	= 0x20
CS_RESET	= 1 shl 31
CS_ERROR	= 1 shl 8
CS_ACTIVE	= 1
CS_GO		= (1 shl 28) or (15 shl 20) or (1 shl 16) or (1 shl 1) or 1
					; WAIT_WRITES, PANIC 15, PRIO 1, END (efface), ACTIVE
TI_2D		= (2 shl 12) or (1 shl 9) or (1 shl 8) or (1 shl 5) or (1 shl 4) or (1 shl 1)
					; rafale 2, 128 bits, SRC_INC, DEST_INC, TDMODE
TI_2D_32	= TI_2D and not ((1 shl 9) or (1 shl 5))
					; (QEMU : 128 bits non emules, erreur du canal)
DEBUG_LITE	= 28			; (bit) canal « lite » : pas de mode 2D
BUS_ALIAS	= 0xC0000000
DMA_TIMEOUT	= 100			; ms
DMA_IRQ_BASE	= 112			; ligne du GIC du canal 0 (canaux 0-10 : une par canal)
TI_INTEN	= 1			; interruption a la fin du bloc
CS_INT		= 4
DMA_CHANNELS_DEF = 0x71F5		; masque par defaut (Circle, Pi 4)

; x0 = ecran : largeur * hauteur * 4 -> le tampon arriere (bb, bb_pitch) et
; le chemin de presentation ; x0 = 0 si memoire insuffisante
present_init:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	adr	x0,scr_w
	ldr	x0,[x0]
	lsl	x0,x0,2
	adr	x1,bb_pitch
	str	x0,[x1]
	adr	x20,m_r_opt
	adr	x0,s_no_dispdma
	bl	option_word
	cbnz	x0,.cpu
	adr	x20,m_r_board
	bl	board_pi4
	cbz	x0,.cpu
	adr	x20,m_r_size			; limites du mode 2D : pas < 32 Ko,
	adr	x0,bb_pitch			; hauteur < 16384
	ldr	x0,[x0]
	cmp	x0,0x8000
	b.hs	.cpu
	adr	x0,fb_pitch
	ldr	x0,[x0]
	cmp	x0,0x8000
	b.hs	.cpu
	adr	x0,scr_h
	ldr	x0,[x0]
	cmp	x0,0x4000
	b.hs	.cpu
	adr	x20,m_r_mem			; tampon arriere : contigu, cache
	orr	x0,x19,DMA_CACHED
	adr	x1,pres_phys
	sys	SYS_DMA_ALLOC
	cmp	x0,0
	b.le	.cpu
	adr	x1,bb
	str	x0,[x1]
	adr	x0,pres_phys
	ldr	x0,[x0]
	bl	phys_bus
	cbz	x0,.cpu
	adr	x1,bb_bus
	str	x0,[x1]
	adr	x20,m_r_fb
	adr	x0,info				; (ws_display +32)
	ldr	x0,[x0,32]
	bl	phys_bus
	cbz	x0,.cpu
	adr	x1,fb_bus
	str	x0,[x1]
	adr	x20,m_r_chan
	bl	dma_pick
	tbnz	x0,63,.cpu
	adr	x1,dma_chan
	str	x0,[x1]
	adr	x20,m_r_map
	movl	x0,DMA_PHYS
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	cmp	x0,0
	b.le	.cpu
	adr	x1,dma_base
	str	x0,[x1]
	adr	x1,dma_chan
	ldr	x1,[x1]
	add	x0,x0,x1,lsl 8
	adr	x1,dma_regs
	str	x0,[x1]
	adr	x20,m_r_mem			; blocs de controle : page non cachee
	mov	x0,0x1000
	adr	x1,pres_phys
	sys	SYS_DMA_ALLOC
	cmp	x0,0
	b.le	.cpu
	adr	x1,dma_cb
	str	x0,[x1]
	adr	x0,pres_phys
	ldr	x0,[x0]
	bl	phys_bus
	cbz	x0,.cpu
	adr	x1,dma_cb_bus
	str	x0,[x1]
	adr	x20,m_r_lite
	adr	x0,dma_regs
	ldr	x0,[x0]
	ldr	w0,[x0,DMA_DEBUG]
	tbnz	w0,DEBUG_LITE,.cpu
	adr	x20,m_r_reset
	bl	dma_reset
	cbz	x0,.cpu
	adr	x20,m_r_trial
	movl	x0,TI_2D			; largeur 128 bits (BCM2711),
	bl	dma_trial
	cbnz	x0,.go
	adr	x20,m_r_emul			; sinon : pas un vrai BCM2711 (QEMU) ;
	adr	x0,s_dispdma_qemu		; 32 bits seulement sur demande
	bl	option_word
	cbz	x0,.cpu
	adr	x20,m_r_trial
	bl	dma_reset
	cbz	x0,.cpu
	movl	x0,TI_2D_32
	bl	dma_trial
	cbz	x0,.cpu
.go:
	bl	dma_irq_init
	adr	x0,pres_mode			; --- DMA 2D
	mov	x1,1
	str	x1,[x0]
	adr	x0,pres_line			; (une ligne du journal, d'un seul puts)
	adr	x1,m_dma
	bl	str_append
	adr	x1,dma_chan			; canal 0-6 : un chiffre
	ldr	x1,[x1]
	add	w1,w1,'0'
	strb	w1,[x0],1
	adr	x1,dma_ti			; (QEMU : 32 bits)
	ldr	x1,[x1]
	tbnz	x1,9,.w128
	adr	x1,m_dma_32
	bl	str_append
.w128:	adr	x1,dma_ybias
	ldr	x1,[x1]
	cbnz	x1,.y1
	adr	x1,m_dma_ylen
	bl	str_append
.y1:	adr	x1,m_nl
	bl	str_append
	b	.log
.cpu:	adr	x0,pres_line			; --- processeur (x20 : la raison)
	adr	x1,m_cpu
	bl	str_append
	mov	x1,x20
	bl	str_append
	adr	x0,pres_line
	bl	puts
	adr	x0,bb				; (tampon DMA deja pris : il sert)
	ldr	x0,[x0]
	cbnz	x0,.ok
	mov	x0,x19
	bl	tas_alloc
	cbz	x0,.out
	adr	x1,bb
	str	x0,[x1]
	b	.ok
.log:	adr	x0,pres_line
	bl	puts
.ok:	mov	x0,1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fin de chaine, x1 = chaine : recopiee (0 final compris) -> x0 = sur le 0
str_append:
	ldrb	w2,[x1],1
	strb	w2,[x0]
	cbz	w2,.ret
	add	x0,x0,1
	b	str_append
.ret:	ret

; x0 = adresse physique -> adresse de bus du DMA (0 : hors du premier Go)
phys_bus:
	lsr	x1,x0,30
	cbnz	x1,.non
	cbz	x0,.non
	orr	x0,x0,BUS_ALIAS
	ret
.non:	mov	x0,0
	ret

; x0 = etiquette « property tags » (une valeur de 32 bits en reponse) ->
; x0 = la valeur, -1 si echec
fw_get:
	prologue
	adr	x1,pres_fw
	mov	w2,28
	str	w2,[x1]				; taille
	str	wzr,[x1,4]			; requete
	str	w0,[x1,8]			; etiquette
	mov	w2,4
	str	w2,[x1,12]			; tampon de valeur
	str	wzr,[x1,16]
	str	wzr,[x1,20]			; valeur
	str	wzr,[x1,24]			; fin
	mov	x0,x1
	sys	SYS_FW_CALL
	cbnz	x0,.fail
	adr	x1,pres_fw
	ldr	w2,[x1,4]
	movl	w3,0x80000000
	cmp	w2,w3
	b.ne	.fail
	ldr	w0,[x1,20]
	epilogue
.fail:	mov	x0,-1
	epilogue

; -> x0 = 1 sur un Pi 4 (4 B, 400, CM4 ; QEMU raspi4b), 0 sinon (le DMA du
; Pi 5 est ailleurs)
board_pi4:
	prologue
	movl	x0,0x00010002			; GET_BOARD_REVISION
	bl	fw_get
	tbnz	x0,63,.non
	tbz	x0,23,.non			; nouveau format
	ubfx	x0,x0,4,8			; type
	cmp	x0,0x11
	b.eq	.oui
	cmp	x0,0x13
	b.eq	.oui
	cmp	x0,0x14
	b.eq	.oui
.non:	mov	x0,0
	epilogue
.oui:	mov	x0,1
	epilogue

; -> x0 = canal : le plus haut des canaux complets (6..0) laisses par le
; firmware ; -1 si aucun
dma_pick:
	prologue
	movl	x0,0x00060001			; GET_DMA_CHANNELS
	bl	fw_get
	tbnz	x0,63,.def
	cbnz	x0,.m
.def:	movl	x0,DMA_CHANNELS_DEF
.m:	mov	x1,6
.c:	lsrv	x2,x0,x1
	tbnz	x2,0,.ok
	cbz	x1,.none
	sub	x1,x1,1
	b	.c
.ok:	mov	x0,x1
	epilogue
.none:	mov	x0,-1
	epilogue

; active le canal (DMA_ENABLE) et le remet a zero -> x0 = 1, 0 si le canal
; ne sort pas de sa remise a zero
dma_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,dma_base
	ldr	x0,[x0]
	adr	x1,dma_chan
	ldr	x1,[x1]
	ldr	w2,[x0,DMA_ENABLE_R]
	lsrv	w3,w2,w1
	tbnz	w3,0,.en
	mov	w3,1
	lslv	w3,w3,w1
	orr	w2,w2,w3
	str	w2,[x0,DMA_ENABLE_R]
.en:	adr	x19,dma_regs
	ldr	x19,[x19]
	mov	w0,CS_RESET
	str	w0,[x19,DMA_CS]
	sys	SYS_TIME_MS
	mov	x20,x0
.w:	ldr	w0,[x19,DMA_CS]
	tbz	w0,31,.oui
	sys	SYS_YIELD
	sys	SYS_TIME_MS
	sub	x0,x0,x20
	cmp	x0,20
	b.lo	.w
	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse de bus du premier bloc de controle : lance la chaine
dma_start:
	dsb	sy				; blocs (non caches) et lignes nettoyees
	adr	x1,dma_regs			; en memoire avant le lancement
	ldr	x1,[x1]
	str	w0,[x1,DMA_CONBLK]
	movl	w0,CS_GO
	str	w0,[x1,DMA_CS]
	ret

; x0 = attente maximale (ms) -> x0 = 1 fini sans erreur, 0 erreur ou pas de
; reponse (le canal est alors remis a zero)
dma_finish:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x19,dma_regs
	ldr	x19,[x19]
	sys	SYS_TIME_MS
	mov	x20,x0
.w:	ldr	w0,[x19,DMA_CS]
	tbnz	w0,8,.err			; ERROR
	tbz	w0,0,.oui			; plus ACTIVE
	adr	x0,dma_irq
	ldr	x0,[x0]
	cbz	x0,.spin
	mov	x1,x21				; (la borne, puis l'echeance ci-dessous)
	sys	SYS_IRQ_WAIT
	cmp	x0,1				; (pas pendant le transfert)
	b.ne	.time
	adr	x0,dma_irqs
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	mov	w0,CS_INT
	str	w0,[x19,DMA_CS]
	b	.time
.spin:	sys	SYS_YIELD			; (le coeur reste aux autres)
.time:	sys	SYS_TIME_MS
	sub	x0,x0,x20
	cmp	x0,x21
	b.lo	.w
.err:	mov	w0,CS_RESET
	str	w0,[x19,DMA_CS]
	mov	x0,0
	b	.out
.oui:	mov	w0,CS_INT
	str	w0,[x19,DMA_CS]
	mov	x0,1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; apres l'essai du canal : son interruption, si le noyau la donne
dma_irq_init:
	prologue
	adr	x0,dma_chan
	ldr	x0,[x0]
	add	x0,x0,DMA_IRQ_BASE
	str	x0,[sp,-16]!
	mov	x1,-1
	mov	x2,0
	sys	SYS_IRQ_BIND
	ldr	x1,[sp],16
	tbnz	x0,63,.non
	adr	x0,dma_irq
	str	x1,[x0]
.non:	epilogue

; essai : 2 lignes de 16 octets (YLENGTH = 1) de +0x800 vers +0xC00 de la
; page des blocs (pas 64 des deux cotes), x0 = TI -> x0 = 1, dma_ti et
; dma_ybias ; 0 si echec
dma_trial:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x1,dma_ti
	str	x0,[x1]
	adr	x19,dma_cb
	ldr	x19,[x19]
	adr	x20,dma_cb_bus
	ldr	x20,[x20]
	mov	x0,0				; source : 3 lignes reconnaissables ;
	movl	w1,0xA5C30000			; destination : a zero
	add	x2,x19,0x800
	add	x3,x19,0xC00
.f:	add	w4,w1,w0
	str	w4,[x2,x0,lsl 2]
	str	wzr,[x3,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,48				; 3 x 64 octets
	b.lo	.f
	adr	x0,dma_ti
	ldr	x0,[x0]
	str	w0,[x19]
	add	w0,w20,0x800
	str	w0,[x19,4]			; SOURCE_AD
	add	w0,w20,0xC00
	str	w0,[x19,8]			; DEST_AD
	movl	w0,(1 shl 16) or 16
	str	w0,[x19,12]			; TXFR_LEN : YLENGTH 1, XLENGTH 16
	movl	w0,(48 shl 16) or 48
	str	w0,[x19,16]			; STRIDE : 64 - 16 des deux cotes
	str	wzr,[x19,20]			; NEXTCONBK
	str	xzr,[x19,24]
	mov	x0,x20
	bl	dma_start
	mov	x0,50
	bl	dma_finish
	cbz	x0,.non
	add	x2,x19,0x800
	add	x3,x19,0xC00
	ldp	x4,x5,[x2]			; ligne 0 : copiee
	ldp	x6,x7,[x3]
	cmp	x4,x6
	b.ne	.non
	cmp	x5,x7
	b.ne	.non
	ldp	x6,x7,[x3,128]			; ligne 2 : intacte
	orr	x6,x6,x7
	cbnz	x6,.non
	ldp	x6,x7,[x3,64]			; ligne 1 : copiee (BCM2711) ou
	orr	x0,x6,x7			; intacte (QEMU)
	cbz	x0,.emul
	ldp	x4,x5,[x2,64]
	cmp	x4,x6
	b.ne	.non
	cmp	x5,x7
	b.ne	.non
	mov	x0,1
	b	.bias
.emul:	mov	x0,0
.bias:	adr	x1,dma_ybias
	str	x0,[x1]
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; avant de redessiner : le transfert precedent fini. Sans reponse en
; DMA_TIMEOUT ms, ou en erreur : retour a la copie processeur, tout l'ecran
; a recomposer.
present_wait:
	prologue
	adr	x0,pres_busy
	ldr	x1,[x0]
	cbz	x1,.out
	str	xzr,[x0]
	adr	x0,pres_t0			; (le temps deja passe depuis le lancement
	ldr	x1,[x0]				; compte)
	str	x1,[sp,-16]!
	sys	SYS_TIME_MS
	ldr	x1,[sp],16
	sub	x0,x0,x1
	mov	x1,DMA_TIMEOUT
	subs	x0,x1,x0
	csel	x0,xzr,x0,lt
	add	x0,x0,1
	bl	dma_finish
	cbnz	x0,.out
	adr	x0,pres_mode			; le DMA ne repond plus
	str	xzr,[x0]
	adr	x0,m_dma_fail
	bl	puts
	bl	dirty_all
.out:	epilogue

; cmp_list (cmp_n zones) -> ecran
present:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,pres_mode
	ldr	x0,[x0]
	cbnz	x0,.dma
	adr	x0,cmp_n			; --- processeur, zone par zone
	ldr	x19,[x0]
	adr	x20,cmp_list
.r:	cbz	x19,.out
	ldp	x0,x1,[x20]
	ldp	x2,x3,[x20,16]
	add	x20,x20,32
	bl	flush_rect
	sub	x19,x19,1
	b	.r
.dma:	bl	present_dma
.out:	ldp	x19,x20,[sp],16
	epilogue

; une chaine de blocs de controle (un par zone, lue en place dans le tampon
; arriere), lignes nettoyees, lancee ; present_wait en attendra la fin
present_dma:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	adr	x0,cmp_n
	ldr	x19,[x0]
	cbz	x19,.out
	adr	x20,cmp_list
	adr	x0,dma_cb
	ldr	x21,[x0]
	adr	x0,dma_cb_bus
	ldr	x22,[x0]
	adr	x0,bb
	ldp	x23,x24,[x0]			; bb, bb_pitch
	adr	x0,bb_bus
	ldr	x25,[x0]
	adr	x0,fb_bus
	ldr	x26,[x0]
	adr	x0,fb_pitch
	ldr	x27,[x0]
	adr	x0,dma_ybias
	ldr	x28,[x0]
	mov	x16,x21				; bloc courant
.z:	ldp	x0,x1,[x20]			; x0, y0, x1, y1
	ldp	x2,x3,[x20,16]
	add	x20,x20,32
	sub	x9,x2,x0			; largeur
	sub	x10,x3,x1			; hauteur
	lsl	x11,x9,2			; octets par ligne
	madd	x12,x1,x24,xzr			; decalage dans le tampon arriere
	add	x12,x12,x0,lsl 2
	madd	x13,x1,x27,xzr			; dans le framebuffer
	add	x13,x13,x0,lsl 2
	add	x14,x23,x12			; nettoyer les lignes de la zone
	mov	x15,x10
.l:	bic	x6,x14,63
	add	x7,x14,x11
.dc:	dc	cvac,x6
	add	x6,x6,64
	cmp	x6,x7
	b.lo	.dc
	add	x14,x14,x24
	subs	x15,x15,1
	b.ne	.l
	adr	x4,dma_ti			; le bloc
	ldr	x4,[x4]
	str	w4,[x16]
	add	w4,w25,w12
	str	w4,[x16,4]			; SOURCE_AD
	add	w4,w26,w13
	str	w4,[x16,8]			; DEST_AD
	sub	x4,x10,x28			; YLENGTH : lignes - 1 (BCM2711),
	orr	x4,x11,x4,lsl 16		; lignes (QEMU)
	str	w4,[x16,12]			; TXFR_LEN
	sub	x4,x27,x11
	sub	x5,x24,x11
	orr	x4,x5,x4,lsl 16
	str	w4,[x16,16]			; STRIDE : destination, source
	sub	x19,x19,1
	cbnz	x19,.tiok			; le dernier bloc : interruption
	ldr	w4,[x16]
	orr	w4,w4,TI_INTEN
	str	w4,[x16]
.tiok:	add	x4,x16,32			; suivant (0 : fin de la chaine)
	sub	x4,x4,x21
	add	x4,x4,x22
	cmp	x19,0
	csel	x4,xzr,x4,eq
	str	w4,[x16,20]			; NEXTCONBK
	str	xzr,[x16,24]
	add	x16,x16,32
	cbnz	x19,.z
	mov	x0,x22
	bl	dma_start
	sys	SYS_TIME_MS
	adr	x1,pres_t0
	str	x0,[x1]
	adr	x1,pres_busy
	mov	x0,1
	str	x0,[x1]
	adr	x0,dma_n			; (diagnostic F11)
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (F11) presentation : chemin, transferts DMA lances, listes de zones pleines
present_diag:
	prologue
	adr	x0,dg_pres
	bl	puts
	adr	x0,pres_mode
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,dg_dman
	bl	puts
	adr	x0,dma_n
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,dg_irq
	bl	puts
	adr	x0,dma_irqs
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,dg_ovf
	bl	puts
	adr	x0,dirty_overflow
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	epilogue

; x0 = mot -> x0 = 1 s'il commence une ligne de SD0:/etc/options.txt
; (comme option_mot de hwmanager)
option_word:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x0,path_options
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	mov	x19,x0
	adr	x1,ini_buf
	mov	x2,INI_MAX-1
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.non
	adr	x0,ini_buf
	strb	wzr,[x0,x20]
.line:	mov	x1,x21				; le mot en debut de ligne
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	cbz	w3,.end
	ldrb	w4,[x0,x2]
	cmp	w3,w4
	b.ne	.skip
	add	x2,x2,1
	b	.c
.end:	ldrb	w4,[x0,x2]			; suivi d'une fin de ligne
	cmp	w4,' '
	b.ls	.oui
.skip:	ldrb	w3,[x0],1			; ligne suivante
	cbz	w3,.non
	cmp	w3,10
	b.ne	.skip
	b	.line
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

dg_pres		db 'presentation (1 DMA 2D, 0 processeur) : ',0
dg_irq		db ', par interruption ',0
dg_dman		db ' ; transferts DMA : ',0
dg_ovf		db ' ; listes de zones pleines : ',0
path_options	db 'SD0:/etc/options.txt',0
s_no_dispdma	db 'no_dispdma',0
s_dispdma_qemu	db 'dispdma_qemu',0
m_cpu		db 'elegant : copie vers l',0x27,0xC3,0xA9,'cran par le processeur (',0
m_r_opt		db 'no_dispdma)',10,0
m_r_board	db 'pas un Pi 4)',10,0
m_r_size	db 0xC3,0xA9,'cran trop grand pour le mode 2D)',10,0
m_r_mem		db 'pas de m',0xC3,0xA9,'moire DMA)',10,0
m_r_fb		db 'adresse de l',0x27,0xC3,0xA9,'cran inconnue)',10,0
m_r_chan	db 'aucun canal DMA libre)',10,0
m_r_map		db 'registres du DMA refus',0xC3,0xA9,'s)',10,0
m_r_lite	db 'canal DMA ',0xC2,0xAB,' lite ',0xC2,0xBB,')',10,0
m_r_reset	db 'canal DMA bloqu',0xC3,0xA9,')',10,0
m_r_emul	db 'DMA 128 bits absent : ',0xC3,0xA9,'mulateur ; dispdma_qemu pour l',0x27,'essayer)',10,0
m_r_trial	db 'essai du DMA 2D rat',0xC3,0xA9,')',10,0
m_dma		db 'elegant : copie vers l',0x27,0xC3,0xA9,'cran par DMA 2D asynchrone, canal ',0
m_dma_32	db ', 32 bits (128 non ',0xC3,0xA9,'mul',0xC3,0xA9,'s)',0
m_dma_ylen	db ', YLENGTH = lignes',0
m_nl		db 10,0
m_dma_fail	db 'elegant : le DMA de l',0x27,0xC3,0xA9,'cran ne r',0xC3,0xA9,'pond plus : copie par le processeur',10,0
		align 4
