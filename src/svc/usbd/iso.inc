; ============================================================================
;  svc/usbd/iso.inc - transferts isochrones (docs/SON.md 5.1 : cartes son USB)
;
;  Un tuyau isochrone (op USB_OP_ISO_OPEN) a un anneau partage avec son pilote
;  (UI_* de inc/usb.inc) : OUT, le pilote y ecrit le PCM, usbd en envoie un
;  paquet par milliseconde (octets par ms en 16.16, arrondis a la trame) ;
;  IN, usbd y ecrit ce qu'il recoit.
;  * DWC2 (port USB-C ; QEMU usb-audio) : un second thread d'usbd, seul
;    maitre des canaux 3 (OUT) et 4 (IN), un paquet par transfert, cadence
;    sur l'horloge avec ISO_LEAD ms d'avance, scrute toutes les ms (QEMU
;    fait le paquet des l'activation du canal : on enchaine alors les paquets
;    dus). Registres du canal seulement ; HFNUM en lecture.
;  * xHCI (ports USB-A) : TRB Isoch (SIA : au plus tot) mis en file ISO_XQ ms
;    d'avance par la boucle d'usbd (iso_xh_service, a chaque tour), recharges
;    a chaque evenement de fin (iso_xh_evt). Non teste (pas de xHCI sous QEMU).
;  Limites : pas de retour de synchronisation (sound corrige), pas de SPLIT.
; ============================================================================

ISO_MAX		= 4
IS_ACTIVE	= 0			; 1 : ouvert
IS_PID		= 8
IS_CTRL		= 16			; 0 DWC2, 1 xHCI
IS_DEV		= 24			; numero usbd
IS_HANDLE	= 32			; poignee du controleur
IS_EP		= 40			; extremite (bit 7 : IN)
IS_MPS		= 48
IS_BPMS		= 56			; octets par ms (16.16)
IS_FRAME	= 64			; octets par trame
IS_BUF		= 72			; anneau : numero, adresse
IS_RING		= 80
IS_ACC		= 88			; cumul (16.16)
IS_T0		= 96			; DWC2 : depart (ms), paquets envoyes
IS_SENT		= 104
IS_ARMED	= 112			; DWC2 : un paquet en cours
IS_DMA		= 120			; DWC2 : tampon DMA (adresse, physique)
IS_DMAPA	= 128
IS_CHAN		= 136			; DWC2 : decalage du canal (n x 0x20)
IS_DCI		= 144			; xHCI
IS_OUTST	= 152			; xHCI : TRB en file
IS_PUSH		= 160			;   prochaine case (0..15)
IS_DONE		= 168			;   prochaine case finie
IS_LAST		= 176			; DWC2 IN : dernier paquet arme (ms)
IS_LEN		= 184			; DWC2 : octets du paquet arme
IS_SIZE		= 256
ISO_LEAD	= 4			; ms d'avance (DWC2)
ISO_XQ		= 12			; TRB en file (xHCI)
ISO_SLOTS	= 16			; cases de 1 Ko par tuyau (xHCI)
XH_Z_ISO	= 0xA0000		; zone DMA du xHCI : ISO_MAX x 16 Ko
DW_ISO_OUT	= 3 * 0x20		; canaux DWC2
DW_ISO_IN	= 4 * 0x20
DW_EPTYPE_ISO	= 1
E_BUSY		= -16
ISO_STACK	= 16384

; x0 = pipe -> x0 = son entree
iso_entry:
	adr	x1,iso_tab
	add	x0,x1,x0,lsl 8
	ret

; (les deux gardent tous les registres)
iso_lock_take:
	stp	x0,x1,[sp,-32]!
	stp	x2,x8,[sp,16]
	adr	x0,iso_lock
	mov	w2,1
.t:	ldaxr	w1,[x0]
	cbnz	w1,.busy
	stxr	w1,w2,[x0]
	cbnz	w1,.t
	ldp	x2,x8,[sp,16]
	ldp	x0,x1,[sp],32
	ret
.busy:	clrex
	mov	x8,SYS_YIELD
	svc	0
	adr	x0,iso_lock
	mov	w2,1
	b	.t

iso_lock_drop:
	str	x0,[sp,-16]!
	adr	x0,iso_lock
	stlr	wzr,[x0]
	ldr	x0,[sp],16
	ret

; (requete) x20 = message : USB_OP_ISO_OPEN -> x0 = tuyau ; b = tampon
iso_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x20				; le message
	ldr	x0,[x19,8]
	bl	dev_de
	cbz	x0,.nodev
	mov	x21,x0				; entree du peripherique
	ldr	x0,[x19,24]			; taille max. (1..1023)
	and	x0,x0,0x7FF
	cbz	x0,.inval
	cmp	x0,1023
	b.hi	.inval
	adr	x22,iso_tab			; une place libre
	mov	x23,0
.t:	cmp	x23,ISO_MAX
	b.hs	.nomem
	ldr	x0,[x22,IS_ACTIVE]
	cbz	x0,.place
	add	x22,x22,IS_SIZE
	add	x23,x23,1
	b	.t
.place:	ldr	x0,[x21,DV_CTRL]		; DWC2 : un seul OUT, un seul IN
	cbnz	x0,.ring
	ldr	x1,[x19,16]
	and	x1,x1,0x80
	adr	x2,iso_tab
	mov	x3,ISO_MAX
.u:	ldr	x4,[x2,IS_ACTIVE]
	cbz	x4,.un
	ldr	x4,[x2,IS_CTRL]
	cbnz	x4,.un
	ldr	x4,[x2,IS_EP]
	and	x4,x4,0x80
	cmp	x4,x1
	b.eq	.busy
.un:	add	x2,x2,IS_SIZE
	subs	x3,x3,1
	b.ne	.u
.ring:	movl	x0,UI_DATA + UI_BYTES		; l'anneau, pour le pilote
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x24,x0
	sys	SYS_BUF_MAP
	tbnz	x0,63,.free
	str	x0,[x22,IS_RING]
	mov	x1,UI_DATA/8
.z:	str	xzr,[x0],8
	subs	x1,x1,1
	b.ne	.z
	mov	x0,x24
	ldr	w1,[x19,44]
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.free
	str	x24,[x22,IS_BUF]
	ldr	w0,[x19,44]
	str	x0,[x22,IS_PID]
	ldr	x0,[x21,DV_CTRL]
	str	x0,[x22,IS_CTRL]
	ldr	x0,[x19,8]
	str	x0,[x22,IS_DEV]
	ldr	x0,[x21,DV_POIGNEE]
	str	x0,[x22,IS_HANDLE]
	ldr	x0,[x19,16]
	and	x1,x0,0x80
	and	x0,x0,0xF
	orr	x0,x0,x1
	str	x0,[x22,IS_EP]
	ldr	x0,[x19,24]
	and	x0,x0,0x7FF
	str	x0,[x22,IS_MPS]
	ldr	x0,[x19,32]			; octets par ms, par trame
	mov	w1,w0
	str	x1,[x22,IS_BPMS]
	lsr	x0,x0,32
	cmp	x0,1
	mov	x1,4
	csel	x0,x1,x0,lo
	str	x0,[x22,IS_FRAME]
	str	xzr,[x22,IS_ACC]
	str	xzr,[x22,IS_T0]
	str	xzr,[x22,IS_SENT]
	str	xzr,[x22,IS_ARMED]
	str	xzr,[x22,IS_OUTST]
	str	xzr,[x22,IS_PUSH]
	str	xzr,[x22,IS_DONE]
	str	xzr,[x22,IS_LAST]
	ldr	x0,[x22,IS_CTRL]
	cbnz	x0,.xh
	ldr	x0,[x22,IS_DMA]			; DWC2 : un tampon DMA (gardee)
	cbnz	x0,.chan
	mov	x0,4096
	bl	dma_zone
	tbnz	x0,63,.free
	str	x0,[x22,IS_DMA]
	str	x1,[x22,IS_DMAPA]
.chan:	ldr	x0,[x22,IS_EP]
	mov	x1,DW_ISO_OUT
	mov	x2,DW_ISO_IN
	tst	x0,0x80
	csel	x1,x2,x1,ne
	str	x1,[x22,IS_CHAN]
	adr	x0,iso_thread_on		; le thread des canaux isochrones
	ldr	x1,[x0]
	cbnz	x1,.act
	mov	x1,1
	str	x1,[x0]
	adr	x0,iso_thread
	bl	iso_spawn
	b	.act
.xh:	ldr	x0,[x22,IS_HANDLE]		; xHCI : l'extremite configuree
	ldr	x1,[x22,IS_EP]
	ldr	x2,[x22,IS_MPS]
	bl	iso_xh_open
	tbnz	x0,63,.free
	str	x0,[x22,IS_DCI]
.act:	bl	iso_lock_take
	mov	x0,1
	str	x0,[x22,IS_ACTIVE]
	bl	iso_lock_drop
	str	x24,[x19,16]			; b = l'anneau
	mov	x0,x23
	b	.out
.free:	str	x0,[sp,-16]!
	mov	x0,x24
	sys	SYS_BUF_FREE
	ldr	x0,[sp],16
	b	.out
.busy:	mov	x0,E_BUSY
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.nodev:	mov	x0,E_NODEV
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree : fermeture (canal DWC2 arrete ; anneau rendu)
iso_close_entry:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	iso_lock_take
	ldr	x0,[x19,IS_ACTIVE]
	str	xzr,[x19,IS_ACTIVE]
	cbz	x0,.unl
	ldr	x0,[x19,IS_CTRL]
	cbnz	x0,.unl
	ldr	x0,[x19,IS_ARMED]		; DWC2 : canal arrete
	cbz	x0,.unl
	str	xzr,[x19,IS_ARMED]
	adr	x0,dw_regs
	ldr	x0,[x0]
	ldr	x1,[x19,IS_CHAN]
	add	x0,x0,x1
	ldr	w1,[x0,DWC_HCCHAR]
	orr	w1,w1,(1 shl 30) or DW_HCCHAR_CHENA
	str	w1,[x0,DWC_HCCHAR]
.unl:	bl	iso_lock_drop
	ldr	x0,[x19,IS_BUF]
	sys	SYS_BUF_FREE
	ldr	x19,[sp],16
	epilogue

; (requete) x20 = message : USB_OP_ISO_CLOSE -> x0 = 0
iso_close:
	prologue
	ldr	x0,[x20,8]
	cmp	x0,ISO_MAX
	b.hs	.inval
	bl	iso_entry
	ldr	x1,[x0,IS_ACTIVE]
	cbz	x1,.inval
	ldr	x1,[x0,IS_PID]
	ldr	w2,[x20,44]
	cmp	x1,x2
	b.ne	.inval
	bl	iso_close_entry
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	epilogue

; x0 = pid (fin de process) : ses tuyaux isochrones fermes
iso_close_pid:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x19,iso_tab
	mov	x20,ISO_MAX
.t:	ldr	x0,[x19,IS_ACTIVE]
	cbz	x0,.n
	ldr	x0,[x19,IS_PID]
	cmp	x0,x21
	b.ne	.n
	mov	x0,x19
	bl	iso_close_entry
.n:	add	x19,x19,IS_SIZE
	subs	x20,x20,1
	b.ne	.t
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = peripherique retire : ses tuyaux fermes (UI_STATE = 1 : le pilote voit)
iso_close_dev:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x19,iso_tab
	mov	x20,ISO_MAX
.t:	ldr	x0,[x19,IS_ACTIVE]
	cbz	x0,.n
	ldr	x0,[x19,IS_DEV]
	cmp	x0,x21
	b.ne	.n
	ldr	x0,[x19,IS_RING]
	mov	x1,1
	str	x1,[x0,UI_STATE]
	mov	x0,x19
	bl	iso_close_entry
.n:	add	x19,x19,IS_SIZE
	subs	x20,x20,1
	b.ne	.t
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree : -> x0 = octets du prochain paquet OUT (cumul 16.16 arrondi
; a la trame, au plus la taille max.)
iso_packet_bytes:
	ldr	x1,[x0,IS_ACC]
	ldr	x2,[x0,IS_BPMS]
	add	x1,x1,x2
	lsr	x3,x1,16			; octets dus
	ldr	x4,[x0,IS_FRAME]
	udiv	x5,x3,x4
	mul	x5,x5,x4			; en trames entieres
	ldr	x6,[x0,IS_MPS]
	cmp	x5,x6
	b.ls	.ok
	udiv	x5,x6,x4
	mul	x5,x5,x4
.ok:	sub	x1,x1,x5,lsl 16
	str	x1,[x0,IS_ACC]
	mov	x0,x5
	ret

; x0 = entree, x1 = destination, x2 = octets : depuis l'anneau OUT (silence
; s'il manque, compte dans UI_LOST)
iso_take:
	ldr	x3,[x0,IS_RING]
	add	x4,x3,UI_HEAD
	ldar	x4,[x4]
	ldr	x5,[x3,UI_TAIL]
	sub	x6,x4,x5			; disponibles
	cmp	x6,x2
	csel	x6,x6,x2,lo
	add	x7,x3,UI_DATA
	movl	x8,UI_BYTES-1
	mov	x9,0
.c:	cmp	x9,x6
	b.hs	.z
	and	x10,x5,x8
	ldrb	w11,[x7,x10]
	strb	w11,[x1,x9]
	add	x5,x5,1
	add	x9,x9,1
	b	.c
.z:	cmp	x9,x2
	b.hs	.done
	strb	wzr,[x1,x9]
	add	x9,x9,1
	b	.z
.done:	add	x7,x3,UI_TAIL
	stlr	x5,[x7]
	subs	x9,x2,x6
	b.eq	.ret
	ldr	x10,[x3,UI_LOST]
	add	x10,x10,x9
	str	x10,[x3,UI_LOST]
.ret:	ret

; x0 = entree, x1 = source, x2 = octets : dans l'anneau IN (le reste perdu)
iso_put:
	ldr	x3,[x0,IS_RING]
	ldr	x4,[x3,UI_HEAD]
	add	x5,x3,UI_TAIL
	ldar	x5,[x5]
	sub	x6,x4,x5
	movl	x7,UI_BYTES
	sub	x6,x7,x6			; place
	cmp	x6,x2
	csel	x6,x6,x2,lo
	add	x7,x3,UI_DATA
	movl	x8,UI_BYTES-1
	mov	x9,0
.c:	cmp	x9,x6
	b.hs	.done
	ldrb	w11,[x1,x9]
	and	x10,x4,x8
	strb	w11,[x7,x10]
	add	x4,x4,1
	add	x9,x9,1
	b	.c
.done:	add	x7,x3,UI_HEAD
	stlr	x4,[x7]
	subs	x9,x2,x6
	b.eq	.ret
	ldr	x10,[x3,UI_LOST]
	add	x10,x10,x9
	str	x10,[x3,UI_LOST]
.ret:	ret

; x0 = entree du thread -> x0 = tid (pile de ISO_STACK)
iso_spawn:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,ISO_STACK + 16
	sys	SYS_SBRK
	tbnz	x0,63,.out
	add	x1,x0,ISO_STACK
	and	x1,x1,0xFFFFFFFFFFFFFFF0
	mov	x0,x19
	mov	x2,0
	sys	SYS_THREAD_CREATE
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  DWC2 : le thread des canaux isochrones (toutes les ms)
; ---------------------------------------------------------------------------
iso_thread:
.l:	bl	iso_lock_take
	adr	x19,iso_tab
	mov	x20,ISO_MAX
.t:	ldr	x0,[x19,IS_ACTIVE]
	cbz	x0,.n
	ldr	x0,[x19,IS_CTRL]
	cbnz	x0,.n
	mov	x0,x19
	bl	dw_iso_service
.n:	add	x19,x19,IS_SIZE
	subs	x20,x20,1
	b.ne	.t
	bl	iso_lock_drop
	mov	x0,1
	sys	SYS_SLEEP
	b	.l

; x0 = entree (DWC2) : paquets dus
dw_iso_service:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	adr	x20,dw_regs
	ldr	x20,[x20]
	ldr	x0,[x19,IS_CHAN]
	add	x21,x20,x0			; registres du canal
	ldr	x0,[x19,IS_EP]
	tbnz	x0,7,.in
.o:	ldr	x0,[x19,IS_ARMED]		; --- OUT ---
	cbz	x0,.due
	ldr	w0,[x21,DWC_HCINT]
	tbz	w0,1,.out			; paquet en cours
	str	xzr,[x19,IS_ARMED]
	ldr	x1,[x19,IS_RING]
	ldr	x2,[x1,UI_PACKETS]
	add	x2,x2,1
	str	x2,[x1,UI_PACKETS]
.due:	sys	SYS_TIME_MS
	mov	x22,x0
	ldr	x1,[x19,IS_T0]
	cbnz	x1,.paced
	str	x22,[x19,IS_T0]
	str	xzr,[x19,IS_SENT]
	mov	x1,x22
.paced:	sub	x2,x22,x1
	add	x2,x2,ISO_LEAD
	ldr	x3,[x19,IS_SENT]
	subs	x23,x2,x3			; paquets dus
	b.le	.out
	cmp	x23,50				; tres en retard : on repart (un
	b.lo	.send				; seul paquet du)
	add	x1,x22,ISO_LEAD
	sub	x1,x1,x3
	sub	x1,x1,1
	str	x1,[x19,IS_T0]
.send:	mov	x0,x19
	bl	iso_packet_bytes
	mov	x24,x0
	mov	x0,x19
	ldr	x1,[x19,IS_DMA]
	mov	x2,x24
	bl	iso_take
	mov	w0,-1
	str	w0,[x21,DWC_HCINT]
	mov	w0,0x7FF
	str	w0,[x21,DWC_HCINTMSK]
	str	wzr,[x21,DWC_HCSPLT]
	ldr	x0,[x19,IS_DMAPA]
	orr	w0,w0,DW_BUS
	str	w0,[x21,DWC_HCDMA]
	orr	w0,w24,1 shl 19			; un paquet, DATA0
	str	w0,[x21,DWC_HCTSIZ]
	mov	x0,x19
	mov	w1,0
	bl	dw_iso_hcchar
	orr	w0,w0,DW_HCCHAR_CHENA
	str	w0,[x21,DWC_HCCHAR]
	mov	x0,1
	str	x0,[x19,IS_ARMED]
	ldr	x0,[x19,IS_SENT]
	add	x0,x0,1
	str	x0,[x19,IS_SENT]
	b	.o				; (QEMU : deja fait, le suivant)
.in:	ldr	x0,[x19,IS_ARMED]		; --- IN ---
	cbz	x0,.arm
	ldr	w22,[x21,DWC_HCINT]
	tbz	w22,1,.out			; pas encore recu
	str	xzr,[x19,IS_ARMED]
	ldr	w23,[x21,DWC_HCTSIZ]
	mov	w0,w22
	bl	dw_xfer_ok
	cbz	x0,.arm
	ldr	x2,[x19,IS_MPS]
	and	w23,w23,0x7FFFF
	sub	x2,x2,x23			; octets recus
	cbz	x2,.cnt
	mov	x0,x19
	ldr	x1,[x19,IS_DMA]
	bl	iso_put
.cnt:	ldr	x1,[x19,IS_RING]
	ldr	x2,[x1,UI_PACKETS]
	add	x2,x2,1
	str	x2,[x1,UI_PACKETS]
.arm:	sys	SYS_TIME_MS			; un paquet par ms
	ldr	x1,[x19,IS_LAST]
	cmp	x0,x1
	b.eq	.out
	str	x0,[x19,IS_LAST]
	mov	w0,-1
	str	w0,[x21,DWC_HCINT]
	mov	w0,0x7FF
	str	w0,[x21,DWC_HCINTMSK]
	str	wzr,[x21,DWC_HCSPLT]
	ldr	x0,[x19,IS_DMAPA]
	orr	w0,w0,DW_BUS
	str	w0,[x21,DWC_HCDMA]
	ldr	x0,[x19,IS_MPS]
	orr	w0,w0,1 shl 19
	str	w0,[x21,DWC_HCTSIZ]
	mov	x0,x19
	mov	w1,DW_HCCHAR_EPIN
	bl	dw_iso_hcchar
	orr	w0,w0,DW_HCCHAR_CHENA
	str	w0,[x21,DWC_HCCHAR]
	mov	x0,1
	str	x0,[x19,IS_ARMED]
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree, w1 = EPIN ou 0 -> w0 = HCCHAR (sans CHENA) : taille max.,
; extremite, isochrone, un paquet par trame, adresse, vitesse basse, trame
; paire / impaire suivante
dw_iso_hcchar:
	ldr	x2,[x0,IS_MPS]
	ldr	x3,[x0,IS_EP]
	and	x3,x3,0xF
	orr	w2,w2,w3,lsl 11
	orr	w2,w2,w1
	orr	w2,w2,DW_EPTYPE_ISO shl 18
	orr	w2,w2,DW_HCCHAR_EC1
	ldr	x3,[x0,IS_HANDLE]
	and	x4,x3,0x7F
	orr	w2,w2,w4,lsl 22
	ubfx	x4,x3,8,8
	cmp	x4,DW_SPEED_LOW
	b.ne	.fs
	orr	w2,w2,DW_HCCHAR_LSPD
.fs:	adr	x3,dw_regs
	ldr	x3,[x3]
	ldr	w4,[x3,DWC_HFNUM]
	tbnz	w4,0,.even			; trame impaire : la suivante est paire
	orr	w2,w2,DW_HCCHAR_ODDFRM
.even:	mov	w0,w2
	ret

; ---------------------------------------------------------------------------
;  xHCI : TRB Isoch (non teste : pas de xHCI sous QEMU)
; ---------------------------------------------------------------------------

; x0 = slot, x1 = extremite (bit 7 : IN), x2 = taille max. -> x0 = DCI, < 0
iso_xh_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	xh_slot_ok
	cbz	x0,.nodev
	and	w22,w20,0xF			; DCI = 2 x numero (+ 1 si IN)
	cbz	w22,.inval
	lsl	w22,w22,1
	tst	w20,0x80
	cinc	w22,w22,ne
	mov	x0,x19
	bl	xh_dev
	ldr	w1,[x0,XH_D_SPEED]
	mov	x0,1				; une fois par ms
	bl	xh_intervalle
	mov	w3,w0
	mov	x0,x19
	mov	w1,w22
	mov	w2,w21
	mov	w4,1				; Isoch OUT
	tst	w20,0x80
	b.eq	.t
	mov	w4,5				; Isoch IN
.t:	bl	xh_ep_preparer
	tbnz	x0,63,.out
	mov	x0,x22
	b	.out
.nodev:	mov	x0,XH_E_NODEV
	b	.out
.inval:	mov	x0,XH_E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree, x1 = case -> x0 = adresse de la case (zone xHCI)
iso_xh_slot:
	adr	x2,iso_tab
	sub	x2,x0,x2
	lsr	x2,x2,8				; tuyau
	adr	x3,xh_zone
	ldr	x3,[x3]
	movl	x4,XH_Z_ISO
	add	x3,x3,x4
	add	x3,x3,x2,lsl 14			; 16 Ko par tuyau
	add	x0,x3,x1,lsl 10			; 1 Ko par case
	ret

; Boucle d'usbd : chaque tuyau xHCI garde ISO_XQ TRB en file
iso_xh_service:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x19,iso_tab
	mov	x20,ISO_MAX
.t:	ldr	x0,[x19,IS_ACTIVE]
	cbz	x0,.n
	ldr	x0,[x19,IS_CTRL]
	cbz	x0,.n
	mov	x23,0				; TRB ajoutes
.q:	ldr	x0,[x19,IS_OUTST]
	cmp	x0,ISO_XQ
	b.hs	.bell
	ldr	x24,[x19,IS_PUSH]
	mov	x0,x19
	mov	x1,x24
	bl	iso_xh_slot
	mov	x21,x0				; la case
	ldr	x0,[x19,IS_EP]
	tbnz	x0,7,.in
	mov	x0,x19				; OUT : le paquet dans la case
	bl	iso_packet_bytes
	mov	x22,x0
	mov	x0,x19
	mov	x1,x21
	mov	x2,x22
	bl	iso_take
	mov	w3,(5 shl 10) or XH_TRB_IOC	; Isoch, IOC
	b	.push
.in:	ldr	x22,[x19,IS_MPS]
	mov	w3,(5 shl 10) or XH_TRB_IOC or XH_TRB_ISP
.push:	orr	w3,w3,1 shl 31			; SIA : au plus tot
	ldr	w0,[x19,IS_HANDLE]
	ldr	w1,[x19,IS_DCI]
	str	w3,[sp,-16]!
	bl	xh_anneau
	ldr	w3,[sp],16
	mov	x1,x21
	xh_dma	x1,x2
	mov	w2,w22
	bl	xh_ring_push
	add	x24,x24,1
	and	x24,x24,ISO_SLOTS-1
	str	x24,[x19,IS_PUSH]
	ldr	x0,[x19,IS_OUTST]
	add	x0,x0,1
	str	x0,[x19,IS_OUTST]
	add	x23,x23,1
	b	.q
.bell:	cbz	x23,.n
	ldr	w0,[x19,IS_HANDLE]
	ldr	w1,[x19,IS_DCI]
	bl	xh_ring_bell
.n:	add	x19,x19,IS_SIZE
	subs	x20,x20,1
	b.ne	.t
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = slot, w1 = DCI, w2 = code, w3 = octets non transferes (evenement de
; fin d'un TRB) : un TRB de moins en file ; IN : les donnees a l'anneau
iso_xh_evt:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w21,w2
	mov	w22,w3
	adr	x19,iso_tab
	mov	x20,ISO_MAX
.t:	ldr	x4,[x19,IS_ACTIVE]
	cbz	x4,.n
	ldr	x4,[x19,IS_CTRL]
	cbz	x4,.n
	ldr	x4,[x19,IS_HANDLE]
	cmp	w4,w0
	b.ne	.n
	ldr	x4,[x19,IS_DCI]
	cmp	w4,w1
	b.eq	.got
.n:	add	x19,x19,IS_SIZE
	subs	x20,x20,1
	b.ne	.t
	b	.out
.got:	cmp	w21,XH_CC_STOPPED
	b.eq	.out
	cmp	w21,XH_CC_STOPPED2
	b.eq	.out
	ldr	x0,[x19,IS_OUTST]
	subs	x0,x0,1
	csel	x0,xzr,x0,lo
	str	x0,[x19,IS_OUTST]
	ldr	x1,[x19,IS_RING]
	ldr	x2,[x1,UI_PACKETS]
	add	x2,x2,1
	str	x2,[x1,UI_PACKETS]
	ldr	x20,[x19,IS_DONE]		; la case finie (dans l'ordre)
	add	x0,x20,1
	and	x0,x0,ISO_SLOTS-1
	str	x0,[x19,IS_DONE]
	ldr	x0,[x19,IS_EP]
	tbz	x0,7,.out
	cmp	w21,XH_CC_SUCCESS		; IN : recu
	b.eq	.data
	cmp	w21,XH_CC_SHORT
	b.ne	.out
.data:	ldr	x2,[x19,IS_MPS]
	subs	x2,x2,x22
	b.ls	.out
	str	x2,[sp,-16]!
	mov	x0,x19
	mov	x1,x20
	bl	iso_xh_slot
	mov	x1,x0
	ldr	x2,[sp],16
	mov	x0,x19
	bl	iso_put
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

macro iso_data {
		align 8
iso_lock	dw 0, 0
iso_thread_on	dd 0
iso_tab		rb ISO_MAX * IS_SIZE
}
