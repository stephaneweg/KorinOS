; ============================================================================
;  ops.inc - requetes des programmes (NetKit) : sockets, DNS, ping, etat.
;  Reprise des appels 40-52 de src/kernel/net.asm : x0 = message (donnees du
;  message, ou tampon du process : data_ptr) -> x0 = resultat. Une attente
;  se fait sans le verrou, 25 s au plus (E_AGAIN : NetKit redemande).
; ============================================================================

; x0 = descripteur, x1 = pid -> x0 = socket de ce process (0 si invalide)
sk_get:
	cmp	x0,MAX_SOCK
	b.hs	.bad
	cbz	x1,.bad
	adr	x2,sock_table
	ldr	x2,[x2]
	mov	x3,SK_SIZE
	madd	x0,x0,x3,x2
	ldr	x2,[x0,SK_USED]
	cbz	x2,.bad
	ldr	x2,[x0,SK_OWNER]
	cmp	x1,x2
	b.ne	.bad
	ret
.bad:	mov	x0,0
	ret

; x0 = message, x1 = delai demande -> x0 = min(delai, WAIT_MAX)
wait_cap:
	movl	x0,WAIT_MAX
	cmp	x1,x0
	csel	x0,x1,x0,lo
	ret

; --- 2 socket(a type) -> descripteur ------------------------------------------
op_socket:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	w21,[x0,WM_PID]
	ldr	x19,[x0,WM_A]
	cmp	x19,SOCK_TCP
	b.eq	.ok
	cmp	x19,SOCK_UDP
	b.ne	.inval
.ok:	net_lock_take
	bl	sock_alloc
	mov	x20,x0
	cbz	x20,.full
	str	x19,[x20,SK_TYPE]
	str	x21,[x20,SK_OWNER]
	mov	x0,1000
	str	x0,[x20,SK_RTO]
	mov	x0,536
	str	x0,[x20,SK_MSS]
	mov	x0,x20
	cmp	x19,SOCK_TCP
	cset	x1,eq
	bl	sk_alloc_rings
	cbnz	x0,.done
	mov	x0,x20
	bl	sock_free
.full:	net_lock_drop
	mov	x0,E_NOMEM
	b	.out
.done:	net_lock_drop
	mov	x0,x20
	bl	sk_index
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 3 connect(a sd, b ip, c port) ------------------------------------------------
op_connect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x9,x0
	ldr	w20,[x9,WM_B]
	ldr	w21,[x9,WM_C]
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	bl	sk_get
	cbz	x0,.badf
	mov	x19,x0
	ldr	x0,[x19,SK_TYPE]
	cmp	x0,SOCK_TCP
	b.ne	.udp
	ldr	x0,[x19,SK_STATE]
	cbnz	x0,.inval
	mov	w0,w20
	bl	arp_resolve_wait
	cbz	x0,.unreach
	net_lock_take
	str	x20,[x19,SK_RIP]
	str	x21,[x19,SK_RPORT]
	ldr	x0,[x19,SK_LPORT]
	cbnz	x0,.port
	bl	sock_ephemeral
	str	x0,[x19,SK_LPORT]
.port:	mov	w0,w21
	bl	tcp_iss
	str	x0,[x19,SK_ISS]
	str	x0,[x19,SK_SND_UNA]
	add	w1,w0,1
	str	x1,[x19,SK_SND_NXT]
	mov	x0,TCP_SYN_SENT
	str	x0,[x19,SK_STATE]
	mov	x0,x19
	mov	w1,TH_SYN
	ldr	x2,[x19,SK_ISS]
	mov	x3,0
	bl	tcp_send
	mov	x0,x19
	bl	tcp_arm_rtx
	net_lock_drop
	bl	time_ms
	movl	x16,20000
	add	x22,x0,x16
.wait:	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_ESTAB
	b.hs	.connected
	ldr	x1,[x19,SK_FLAGS]
	tbnz	x1,SKF_REFUSED,.refused
	tbnz	x1,SKF_TIMEOUT,.timeout
	tbnz	x1,SKF_RESET,.refused
	bl	time_ms
	cmp	x0,x22
	b.hs	.timeout
	mov	x0,2
	bl	thread_sleep
	b	.wait
.connected:
	mov	x0,0
	b	.out
.udp:	str	x20,[x19,SK_RIP]		; UDP : destination par defaut
	str	x21,[x19,SK_RPORT]
	mov	x0,0
	b	.out
.refused:
	mov	x0,E_CONNREFUSED
	b	.out
.timeout:
	net_lock_take				; abandon : la socket redevient fermee
	str	xzr,[x19,SK_STATE]
	str	xzr,[x19,SK_RTX_AT]
	net_lock_drop
	mov	x0,E_TIMEDOUT
	b	.out
.unreach:
	mov	x0,E_HOSTUNREACH
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.badf:	mov	x0,E_BADF
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 4 bind(a sd, b port) -----------------------------------------------------------
op_bind:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x9,x0
	ldr	x20,[x9,WM_B]
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	bl	sk_get
	cbz	x0,.badf
	mov	x19,x0
	cbz	x20,.inval
	movl	x0,0xFFFF
	cmp	x20,x0
	b.hi	.inval
	net_lock_take
	adr	x1,sock_table			; port deja pris pour ce type ?
	ldr	x1,[x1]
	mov	x2,MAX_SOCK
	ldr	x3,[x19,SK_TYPE]
.f:	ldr	x4,[x1,SK_USED]
	cbz	x4,.n
	ldr	x4,[x1,SK_TYPE]
	cmp	x4,x3
	b.ne	.n
	ldr	x4,[x1,SK_LPORT]
	cmp	x4,x20
	b.ne	.n
	ldr	x4,[x1,SK_PARENT]		; (connexion acceptee : meme port)
	cbnz	x4,.n
	ldr	x4,[x1,SK_RIP]
	cbz	x4,.inuse
.n:	add	x1,x1,SK_SIZE
	subs	x2,x2,1
	b.ne	.f
	str	x20,[x19,SK_LPORT]
	net_lock_drop
	mov	x0,0
	b	.out
.inuse:	net_lock_drop
	mov	x0,E_ADDRINUSE
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.badf:	mov	x0,E_BADF
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- 5 listen(a sd, b file d'attente) -------------------------------------------------
op_listen:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x9,x0
	ldr	x20,[x9,WM_B]
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	bl	sk_get
	cbz	x0,.badf
	ldr	x1,[x0,SK_TYPE]
	cmp	x1,SOCK_TCP
	b.ne	.inval
	ldr	x1,[x0,SK_LPORT]
	cbz	x1,.inval
	cmp	x20,1
	mov	x1,1
	csel	x20,x1,x20,lo
	cmp	x20,16
	mov	x1,16
	csel	x20,x1,x20,hi
	str	x20,[x0,SK_BACKLOG]
	mov	x1,TCP_LISTEN
	str	x1,[x0,SK_STATE]
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.badf:	mov	x0,E_BADF
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- 6 accept(a sd, b delai ms) -> descripteur de la connexion ------------------------
op_accept:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x9,x0
	ldr	x1,[x9,WM_B]
	bl	wait_cap
	mov	x20,x0
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	bl	sk_get
	cbz	x0,.badf
	mov	x19,x0
	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_LISTEN
	b.ne	.inval
	bl	time_ms
	add	x22,x0,x20
.try:	net_lock_take
	ldr	x0,[x19,SK_STATE]		; (fermee pendant l'attente)
	cmp	x0,TCP_LISTEN
	b.ne	.inval_l
	adr	x21,sock_table
	ldr	x21,[x21]
	mov	x2,MAX_SOCK
.f:	ldr	x3,[x21,SK_USED]
	cbz	x3,.n
	ldr	x3,[x21,SK_PARENT]
	cmp	x3,x19
	b.ne	.n
	ldr	x3,[x21,SK_STATE]
	cmp	x3,TCP_ESTAB
	b.hs	.got
.n:	add	x21,x21,SK_SIZE
	subs	x2,x2,1
	b.ne	.f
	net_lock_drop
	bl	time_ms
	cmp	x0,x22
	b.hs	.again
	mov	x0,2
	bl	thread_sleep
	b	.try
.got:	str	xzr,[x21,SK_PARENT]
	ldr	x0,[x19,SK_OWNER]
	str	x0,[x21,SK_OWNER]
	net_lock_drop
	mov	x0,x21
	bl	sk_index
	b	.out
.again:	mov	x0,E_AGAIN
	b	.out
.inval_l:
	net_lock_drop
.inval:	mov	x0,E_INVAL
	b	.out
.badf:	mov	x0,E_BADF
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 7 send(a sd, b decalage, c octets) -> octets mis en file ---------------------------
op_send:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x9,x0
	ldr	x21,[x9,WM_C]
	ldr	x1,[x9,WM_B]
	mov	x2,x21
	bl	data_ptr
	cbz	x0,.fault
	mov	x20,x0
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	bl	sk_get
	cbz	x0,.badf
	mov	x19,x0
	ldr	x0,[x19,SK_TYPE]
	cmp	x0,SOCK_UDP
	b.ne	.tcp
	ldr	x3,[x19,SK_RIP]			; UDP connecte
	cbz	x3,.notconn
	mov	x0,x20
	mov	x1,x21
	ldr	x2,[x19,SK_RIP]
	ldr	x3,[x19,SK_RPORT]
	mov	x4,x19
	bl	udp_sendto_user
	b	.out
.tcp:	mov	x22,0				; deja mis en file
	bl	time_ms
	movl	x16,20000
	add	x24,x0,x16
.loop:	cmp	x22,x21
	b.hs	.done
	net_lock_take
	ldr	x0,[x19,SK_USED]		; (fermee pendant l'attente)
	cbz	x0,.st_bad
	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_ESTAB
	b.eq	.st_ok
	cmp	x0,TCP_CLOSE_WAIT
	b.ne	.st_bad
.st_ok:	ldr	x1,[x19,SK_TXHEAD]
	ldr	x2,[x19,SK_TXTAIL]
	sub	x3,x1,x2
	mov	x4,SK_RING
	sub	x23,x4,x3			; place libre
	sub	x5,x21,x22
	cmp	x23,x5
	csel	x23,x23,x5,ls
	cbz	x23,.full
	ldr	x2,[x19,SK_TXBUF]
	add	x3,x20,x22
	mov	x4,x23
.cp:	ldrb	w5,[x3],1
	and	x6,x1,SK_RING-1
	strb	w5,[x2,x6]
	add	x1,x1,1
	subs	x4,x4,1
	b.ne	.cp
	str	x1,[x19,SK_TXHEAD]
	add	x22,x22,x23
	mov	x0,x19
	bl	tcp_output
	net_lock_drop
	b	.loop
.full:	net_lock_drop
	bl	time_ms
	cmp	x0,x24
	b.hs	.done
	mov	x0,2
	bl	thread_sleep
	b	.loop
.st_bad:
	net_lock_drop
	cbnz	x22,.done
	mov	x0,E_NOTCONN
	ldr	x1,[x19,SK_FLAGS]
	tst	x1,(1 shl SKF_RESET) or (1 shl SKF_TIMEOUT)
	mov	x2,E_CONNRESET
	csel	x0,x2,x0,ne
	b	.out
.done:	mov	x0,x22
	b	.out
.notconn:
	mov	x0,E_NOTCONN
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 8 recv(a sd, b decalage, c octets, d delai) -> octets (0 : fermee) -----------------
; --- 11 recvfrom : idem -> c ip, d port de la source -----------------------------------
op_recv:
op_recvfrom:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,16			; source {ip, port}
	mov	x25,x0
	str	xzr,[sp]
	mov	x23,sp
	ldr	x21,[x25,WM_C]
	ldr	x1,[x25,WM_B]
	mov	x2,x21
	bl	data_ptr
	cbz	x0,.fault
	mov	x20,x0
	ldr	x1,[x25,WM_D]
	bl	wait_cap
	mov	x22,x0
	ldr	x0,[x25,WM_A]
	ldr	w1,[x25,WM_PID]
	bl	sk_get
	cbz	x0,.badf
	mov	x19,x0
	bl	time_ms
	add	x22,x0,x22			; echeance
.try:	net_lock_take
	ldr	x0,[x19,SK_USED]		; (fermee pendant l'attente, par un
	cbz	x0,.closed			;  autre thread du process)
	ldr	x0,[x19,SK_OWNER]
	ldr	w1,[x25,WM_PID]
	cmp	x0,x1
	b.ne	.closed
	ldr	x1,[x19,SK_RXHEAD]
	ldr	x2,[x19,SK_RXTAIL]
	cmp	x1,x2
	b.ne	.data
	ldr	x0,[x19,SK_TYPE]
	cmp	x0,SOCK_TCP
	b.ne	.wait
	ldr	x0,[x19,SK_FLAGS]
	tbnz	x0,SKF_RX_FIN,.eof
	tst	x0,(1 shl SKF_RESET) or (1 shl SKF_TIMEOUT)
	b.ne	.reset
	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_SYN_RCVD
	b.hi	.wait
	cmp	x0,TCP_SYN_SENT
	b.hs	.wait
	net_lock_drop
	mov	x0,E_NOTCONN
	b	.out
.wait:	net_lock_drop
	bl	time_ms
	cmp	x0,x22
	b.hs	.again
	mov	x0,2
	bl	thread_sleep
	b	.try
.data:	ldr	x0,[x19,SK_TYPE]
	cmp	x0,SOCK_UDP
	b.eq	.dgram
	sub	x24,x1,x2			; disponible
	cmp	x24,x21
	csel	x24,x24,x21,ls
	mov	x0,x19				; fenetre etait-elle presque fermee ?
	bl	sk_rx_free
	str	x0,[sp,-16]!
	mov	x0,x19
	mov	x1,x20
	mov	x2,x24
	bl	sk_rx_read
	ldr	x0,[sp],16
	ldr	x1,[x19,SK_MSS]
	cmp	x0,x1,lsl 1
	b.hs	.unlock_ret
	mov	x0,x19				; mise a jour de fenetre
	bl	tcp_send_ack
.unlock_ret:
	net_lock_drop
	mov	x0,x24
	b	.out
.dgram:	sub	sp,sp,16			; enregistrement UDP
	mov	x0,x19
	mov	x1,sp
	mov	x2,8
	bl	sk_rx_read
	ldrh	w24,[sp]			; longueur du datagramme
	ldr	w0,[sp,4]			; source : ip, port
	str	w0,[x23]
	ldrh	w0,[sp,2]
	str	w0,[x23,4]
	add	sp,sp,16
	cmp	x24,x21
	csel	x2,x24,x21,ls
	sub	x3,x24,x2			; reste a jeter
	str	x2,[sp,-16]!
	str	x3,[sp,8]
	mov	x0,x19
	mov	x1,x20
	bl	sk_rx_read
	ldr	x3,[sp,8]
	ldr	x24,[sp],16
	ldr	x1,[x19,SK_RXTAIL]
	add	x1,x1,x3
	str	x1,[x19,SK_RXTAIL]
	b	.unlock_ret
.closed:
	net_lock_drop
	mov	x0,E_BADF
	b	.out
.eof:	net_lock_drop
	mov	x0,0
	b	.out
.reset:	net_lock_drop
	mov	x0,E_CONNRESET
	b	.out
.again:	mov	x0,E_AGAIN
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.fault:	mov	x0,E_FAULT
.out:	cmp	x0,0				; donnees du message : leur longueur
	b.le	.src
	ldr	x1,[x25,WM_B]
	cmn	x1,1
	b.ne	.src
	str	w0,[x25,WM_LEN]
.src:	ldr	w1,[x23]			; source (recvfrom)
	str	x1,[x25,WM_C]
	ldr	w1,[x23,4]
	str	x1,[x25,WM_D]
	add	sp,sp,16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 9 close(a sd) -----------------------------------------------------------------
op_close:
	prologue
	str	x19,[sp,-16]!
	mov	x9,x0
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	bl	sk_get
	cbz	x0,.badf
	mov	x19,x0
	net_lock_take
	ldr	x0,[x19,SK_USED]
	cbz	x0,.unl
	mov	x0,x19
	bl	sock_close_locked
.unl:	net_lock_drop
	mov	x0,0
	b	.out
.badf:	mov	x0,E_BADF
.out:	ldr	x19,[sp],16
	epilogue

; --- 24 shutdown(a sd) : FIN apres ce qui reste en file ; le socket reste au
; process et recoit toujours (ni orphelin, ni delai en FIN-WAIT-2)
op_shutdown:
	prologue
	str	x19,[sp,-16]!
	mov	x9,x0
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	bl	sk_get
	cbz	x0,.badf
	mov	x19,x0
	net_lock_take
	ldr	x0,[x19,SK_USED]
	cbz	x0,.unl
	ldr	x0,[x19,SK_TYPE]
	cmp	x0,SOCK_TCP
	b.ne	.unl
	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_ESTAB
	b.eq	.fin
	cmp	x0,TCP_CLOSE_WAIT
	b.ne	.unl				; (deja en fermeture : rien de plus)
.fin:	ldr	x0,[x19,SK_FLAGS]
	orr	x0,x0,1 shl SKF_FIN_PENDING
	str	x0,[x19,SK_FLAGS]
	mov	x0,x19
	bl	tcp_output
.unl:	net_lock_drop
	mov	x0,0
	b	.out
.badf:	mov	x0,E_BADF
.out:	ldr	x19,[sp],16
	epilogue

; Fermeture des sockets d'un process termine (x0 = pid)
net_close_all:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	cbz	x21,.ret
	net_lock_take
	adr	x19,sock_table
	ldr	x19,[x19]
	mov	x20,MAX_SOCK
.s:	ldr	x0,[x19,SK_USED]
	cbz	x0,.n
	ldr	x0,[x19,SK_OWNER]
	cmp	x0,x21
	b.ne	.n
	mov	x0,x19
	bl	sock_close_locked
.n:	add	x19,x19,SK_SIZE
	subs	x20,x20,1
	b.ne	.s
	net_lock_drop
.ret:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 10 sendto(a sd, b decalage, c octets, d ip | port << 32) (UDP) -----------------
op_sendto:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x9,x0
	ldr	x21,[x9,WM_C]
	ldr	x1,[x9,WM_B]
	mov	x2,x21
	bl	data_ptr
	cbz	x0,.fault
	mov	x20,x0
	ldr	x22,[x9,WM_D]
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	bl	sk_get
	cbz	x0,.badf
	mov	x4,x0
	ldr	x0,[x4,SK_TYPE]
	cmp	x0,SOCK_UDP
	b.ne	.inval
	mov	x0,x20
	mov	x1,x21
	mov	w2,w22
	lsr	x3,x22,32
	and	x3,x3,0xFFFF
	bl	udp_sendto_user
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 12 resolve(donnees : nom) -> adresse IPv4 ----------------------------------------
op_resolve:
	prologue
	sub	sp,sp,256
	add	x1,x0,WM_DATA
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	strb	w3,[sp,x2]
	cbz	w3,.go
	add	x2,x2,1
	cmp	x2,255
	b.lo	.c
	strb	wzr,[sp,x2]
.go:	mov	x0,sp
	bl	dns_resolve
	add	sp,sp,256
	epilogue

; --- 13 ping(a ip, b delai ms) -> temps aller-retour en ms ---------------------------
op_ping:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x9,x0
	ldr	x1,[x9,WM_B]
	bl	wait_cap
	mov	x20,x0
	ldr	w19,[x9,WM_A]
	mov	w0,w19				; (vers nous : pas besoin d'adresse)
	bl	ip_is_local
	cbnz	x0,.res
	adr	x0,net_ip
	ldr	w0,[x0]
	cbz	w0,.unreach
.res:	mov	w0,w19
	bl	arp_resolve_wait
	cbz	x0,.unreach
	adr	x0,ping_mutex			; un ping a la fois
	bl	mutex_lock
	net_lock_take
	adr	x0,ping_seq
	ldr	w1,[x0]
	add	w1,w1,1
	and	w1,w1,0xFFFF
	str	w1,[x0]
	adr	x0,ping_id
	movl	w2,0x4152			; "AR"
	str	w2,[x0]
	adr	x0,ping_reply_ms
	str	xzr,[x0]
	adr	x21,net_txbuf
	ldr	x21,[x21]
	add	x21,x21,PAYLOAD
	mov	w0,8				; demande d'echo, 32 octets de donnees
	strb	w0,[x21]
	strb	wzr,[x21,1]
	strh	wzr,[x21,2]
	st16be	w2,x21,4
	st16be	w1,x21,6
	mov	x0,8
.fill:	add	w1,w0,0x20
	strb	w1,[x21,x0]
	add	x0,x0,1
	cmp	x0,40
	b.lo	.fill
	mov	x0,x21
	mov	x1,40
	mov	x2,0
	bl	csum_add
	bl	csum_fold
	st16be	w0,x21,2
	bl	time_ms
	mov	x22,x0				; instant d'envoi
	mov	w0,w19
	mov	w1,PROTO_ICMP
	mov	x2,40
	bl	ip_output
	net_lock_drop
	add	x20,x20,x22			; echeance
.wait:	adr	x0,ping_reply_ms
	ldr	x0,[x0]
	cbnz	x0,.reply
	bl	time_ms
	cmp	x0,x20
	b.hs	.timeout
	mov	x0,1
	bl	thread_sleep
	b	.wait
.reply:	sub	x19,x0,x22
	b	.unlock
.timeout:
	mov	x19,E_TIMEDOUT
.unlock:
	adr	x0,ping_mutex
	bl	mutex_unlock
	mov	x0,x19
	b	.out
.unreach:
	mov	x0,E_HOSTUNREACH
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 14 info -> donnees : netinfo (32 o : ip, masque, passerelle, dns, mac,
; lien, etat DHCP), nom de l'interface active (+32, 8 o), son type (+40) -----
op_info:
	prologue
	str	x19,[sp,-16]!
	add	x19,x0,WM_DATA
	mov	w1,48
	str	w1,[x0,WM_LEN]
	net_lock_take
	adr	x0,net_ip
	ldr	w0,[x0]
	str	w0,[x19]
	adr	x0,net_mask
	ldr	w0,[x0]
	str	w0,[x19,4]
	adr	x0,net_gw
	ldr	w0,[x0]
	str	w0,[x19,8]
	adr	x0,net_dns
	ldr	w0,[x0]
	str	w0,[x19,12]
	adr	x1,netdev_mac
	ldr	w0,[x1]
	str	w0,[x19,16]
	ldrh	w0,[x1,4]
	strh	w0,[x19,20]
	strh	wzr,[x19,22]
	adr	x0,netdev_up
	ldr	w0,[x0]
	str	w0,[x19,24]
	adr	x0,dhcp_state
	ldr	w0,[x0]
	str	w0,[x19,28]
	str	xzr,[x19,32]
	str	xzr,[x19,40]
	adr	x0,if_active
	ldr	x0,[x0]
	tbnz	x0,63,.fin
	adr	x1,ifs
	mov	x2,IF_SIZE
	madd	x1,x0,x2,x1
	ldr	x0,[x1,IF_NAME]
	str	x0,[x19,32]
	ldr	x0,[x1,IF_TYPE]
	str	x0,[x19,40]
.fin:	net_lock_drop
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; --- 15 ifaces -> a nombre ; donnees : NIF_ENT o par interface ------------------------
op_ifaces:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	add	x20,x19,WM_DATA
	mov	x21,0				; entrees
	net_lock_take
	adr	x2,ifs
	mov	x3,0
.t:	ldr	x4,[x2,IF_PID]
	cbz	x4,.n
	mov	x0,x20				; entree a zero
	mov	x1,NIF_ENT
.z:	str	xzr,[x0],8
	subs	x1,x1,8
	b.ne	.z
	ldr	x4,[x2,IF_NAME]
	str	x4,[x20,NIF_E_NAME]
	ldr	x4,[x2,IF_TYPE]
	str	w4,[x20,NIF_E_TYPE]
	ldr	x4,[x2,IF_LINK]
	str	w4,[x20,NIF_E_LINK]
	ldr	x4,[x2,IF_PRIO]
	str	w4,[x20,NIF_E_PRIO]
	ldr	x4,[x2,IF_SPEED]
	str	w4,[x20,NIF_E_SPEED]
	adr	x4,if_active
	ldr	x4,[x4]
	cmp	x4,x3
	b.ne	.ssid
	mov	w4,1
	str	w4,[x20,NIF_E_ACTIVE]
	adr	x4,net_ip
	ldr	w4,[x4]
	str	w4,[x20,NIF_E_IP]
.ssid:	add	x4,x2,IF_SSID
	add	x5,x20,NIF_E_SSID
	mov	x6,31
.s:	ldrb	w7,[x4],1
	strb	w7,[x5],1
	cbz	w7,.se
	subs	x6,x6,1
	b.ne	.s
.se:	add	x20,x20,NIF_ENT
	add	x21,x21,1
.n:	add	x2,x2,IF_SIZE
	add	x3,x3,1
	cmp	x3,IF_MAX
	b.lo	.t
	net_lock_drop
	str	x21,[x19,WM_A]
	lsl	x0,x21,6
	str	w0,[x19,WM_LEN]
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
