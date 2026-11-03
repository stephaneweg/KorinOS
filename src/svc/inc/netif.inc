; ============================================================================
;  svc/inc/netif.inc - cote pilote d'une interface reseau (usbnet, genet,
;  wifi ; docs/RESEAU.md 2.2-2.3) : inscription aupres de netd (service
;  "net"), anneau de trames partage (un producteur, un consommateur par sens,
;  sans verrou ni appel systeme), etat du lien.
;
;	segment executable : include '../inc/netif.inc' (apres netd.inc)
;	segment writeable  : netif_data
;
;  netif_register(x0 type, x1 MAC (octets dans l'ordre, 64 bits petit-
;    boutiste), x2 priorite, x3 nom) -> x0 = 0, < 0 si netd ne repond pas
;  netif_rx_put(x0 trame, x1 longueur) -> x0 = 1 si mise dans l'anneau
;  netif_tx_get(x0 tampon de 1544 o) -> x0 = longueur (0 : rien a emettre)
;  netif_tx_pending -> x0 = trames en attente d'emission
;  netif_link(x0 lien 0/1, x1 debit Mb/s, x2 SSID ou 0)
;  netif_tick : toutes les 2 s, redit l'etat a netd ; s'il a ete relance,
;    se reinscrit (nouvel anneau)
;  nif_wake (variable, avant netif_register) : canal + 1 d'un service du
;    pilote ; netd y leve NIF_SIG_TX quand il met une trame a emettre (0 : le
;    pilote sonde son anneau)
; ============================================================================

; x0 = type, x1 = MAC, x2 = priorite, x3 = nom -> x0 = 0, < 0 si echec
netif_register:
	prologue
	adr	x4,nif_type
	stp	x0,x1,[x4]			; nif_type, nif_mac
	stp	x2,x3,[x4,16]			; nif_prio, nif_name
	movl	x0,60000			; (netd peut demarrer apres le pilote)
	bl	nif_inscrire
	epilogue

; x0 = attente (ms) : (re)inscription : canal de netd, anneau -> x0 = 0
nif_inscrire:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x1,x0
	adr	x0,nif_ring
	str	xzr,[x0]
	adr	x0,nif_s_net
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	adr	x1,nif_canal
	str	x0,[x1]
	adr	x19,nif_msg
	mov	x0,NET_OP_IF_REGISTER
	str	x0,[x19]
	adr	x4,nif_type
	ldp	x0,x1,[x4]
	ldr	x2,[x4,16]
	stp	x0,x1,[x19,WM_A]
	str	x2,[x19,WM_C]
	adr	x1,nif_wake
	ldr	x1,[x1]
	str	x1,[x19,WM_D]
	ldr	x1,[x4,24]			; nom (7 caracteres au plus)
	add	x2,x19,WM_DATA
	str	xzr,[x2]
	mov	x3,0
.n:	ldrb	w4,[x1,x3]
	cbz	w4,.nf
	strb	w4,[x2,x3]
	add	x3,x3,1
	cmp	x3,7
	b.lo	.n
.nf:	mov	w0,8
	str	w0,[x19,WM_LEN]
	adr	x0,nif_canal
	ldr	x0,[x0]
	mov	x1,x19
	sys	SYS_SVC_CALL
	tbnz	x0,63,.out
	adr	x1,nif_index
	str	x0,[x1]
	ldr	x0,[x19,WM_B]			; l'anneau
	sys	SYS_BUF_MAP
	tbnz	x0,63,.out
	adr	x1,nif_ring
	str	x0,[x1]
	sys	SYS_TIME_MS
	adr	x1,nif_next
	str	xzr,[x1]			; etat a redire tout de suite
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = trame, x1 = longueur -> x0 = 1 si mise dans l'anneau de reception
netif_rx_put:
	adr	x2,nif_ring
	ldr	x2,[x2]
	cbz	x2,.non
	cmp	x1,NR_FRAME_MAX
	b.hi	.non
	cmp	x1,14
	b.lo	.non
	ldr	x3,[x2,NR_RX_HEAD]
	add	x4,x2,NR_RX_TAIL
	ldar	x4,[x4]
	sub	x5,x3,x4
	cmp	x5,NR_SLOTS
	b.hs	.plein
	and	x5,x3,NR_SLOTS-1
	add	x6,x2,NR_RX
	add	x6,x6,x5,lsl 11
	str	x1,[x6],8
	mov	x5,x1
.c:	ldrb	w7,[x0],1
	strb	w7,[x6],1
	subs	x5,x5,1
	b.ne	.c
	add	x3,x3,1
	add	x4,x2,NR_RX_HEAD
	stlr	x3,[x4]
	dmb	ish				; (netd : drapeau, puis anneau relu)
	ldr	x3,[x2,NR_SLEEP]
	cbz	x3,.fait
	str	xzr,[x2,NR_SLEEP]
	ldr	x0,[x2,NR_WAKE]
	cbz	x0,.fait
	sub	x0,x0,1
	mov	x1,NR_SIG_RX
	str	x8,[sp,-16]!
	sys	SYS_SVC_SIGNAL
	ldr	x8,[sp],16
.fait:	mov	x0,1
	ret
.plein:	ldr	x3,[x2,NR_RX_DROPS]
	add	x3,x3,1
	str	x3,[x2,NR_RX_DROPS]
.non:	mov	x0,0
	ret

; -> x0 = trames en attente d'emission
netif_tx_pending:
	adr	x2,nif_ring
	ldr	x2,[x2]
	mov	x0,0
	cbz	x2,.ret
	ldr	x0,[x2,NR_TX_TAIL]
	add	x1,x2,NR_TX_HEAD
	ldar	x1,[x1]
	sub	x0,x1,x0
.ret:	ret

; x0 = tampon -> x0 = longueur d'une trame a emettre (0 : rien)
netif_tx_get:
	adr	x2,nif_ring
	ldr	x2,[x2]
	cbz	x2,.rien
	ldr	x3,[x2,NR_TX_TAIL]
	add	x4,x2,NR_TX_HEAD
	ldar	x4,[x4]
	cmp	x3,x4
	b.eq	.rien
	and	x5,x3,NR_SLOTS-1
	add	x6,x2,NR_TX
	add	x6,x6,x5,lsl 11
	ldr	x1,[x6],8
	cmp	x1,NR_FRAME_MAX
	b.hi	.saute
	mov	x5,x1
	cbz	x5,.saute
.c:	ldrb	w7,[x6],1
	strb	w7,[x0],1
	subs	x5,x5,1
	b.ne	.c
	add	x3,x3,1
	add	x4,x2,NR_TX_TAIL
	stlr	x3,[x4]
	mov	x0,x1
	ret
.saute:	add	x3,x3,1
	add	x4,x2,NR_TX_TAIL
	stlr	x3,[x4]
.rien:	mov	x0,0
	ret

; x0 = lien (0/1), x1 = debit (Mb/s), x2 = SSID (0 : aucun) : dit a netd
netif_link:
	prologue
	adr	x3,nif_link
	stp	x0,x1,[x3]
	add	x3,x3,16			; nif_ssid
	strb	wzr,[x3]
	cbz	x2,.envoi
	mov	x4,0
.s:	ldrb	w5,[x2,x4]
	strb	w5,[x3,x4]
	cbz	w5,.envoi
	add	x4,x4,1
	cmp	x4,32
	b.lo	.s
	strb	wzr,[x3,32]
.envoi:	bl	nif_etat
	epilogue

; etat a netd -> x0 = resultat (< 0 : netd parti ou interface inconnue)
nif_etat:
	prologue
	str	x19,[sp,-16]!
	adr	x0,nif_ring
	ldr	x0,[x0]
	cbz	x0,.non
	adr	x19,nif_msg
	mov	x0,NET_OP_IF_STATE
	str	x0,[x19]
	adr	x0,nif_index
	ldr	x0,[x0]
	str	x0,[x19,WM_A]
	adr	x2,nif_link
	ldp	x0,x1,[x2]
	stp	x0,x1,[x19,WM_B]
	add	x2,x2,16
	add	x3,x19,WM_DATA
	mov	x4,0
.s:	ldrb	w5,[x2,x4]
	strb	w5,[x3,x4]
	add	x4,x4,1
	cbz	w5,.f
	cmp	x4,33
	b.lo	.s
.f:	str	w4,[x19,WM_LEN]
	adr	x0,nif_canal
	ldr	x0,[x0]
	mov	x1,x19
	sys	SYS_SVC_CALL
	b	.out
.non:	mov	x0,-1
.out:	ldr	x19,[sp],16
	epilogue

; A appeler dans la boucle du pilote : toutes les 2 s, l'etat a netd ; s'il
; ne repond plus (relance), nouvelle inscription
netif_tick:
	prologue
	sys	SYS_TIME_MS
	adr	x1,nif_next
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.out
	add	x0,x0,2000
	str	x0,[x1]
	bl	nif_etat
	tbz	x0,63,.out
	mov	x0,0				; netd relance : l'anneau a change
	bl	nif_inscrire
	tbnz	x0,63,.out
	bl	nif_etat
.out:	epilogue

nif_s_net	db 'net',0
		align 4

macro netif_data {
		align 8
nif_type	dd 0
nif_mac		dd 0
nif_prio	dd 0
nif_name	dd 0
nif_canal	dd 0
nif_index	dd 0
nif_ring	dd 0				; anneau mappe (0 : pas inscrit)
nif_wake	dd 0				; canal + 1 de son service (0 : aucun)
nif_next	dd 0				; prochain rappel de l'etat (ms)
nif_link	dd 0				; lien, debit, SSID
nif_speed	dd 0
nif_ssid	rb 40
nif_msg		rb 512
}
