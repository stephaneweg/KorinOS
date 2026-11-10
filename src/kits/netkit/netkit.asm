; ============================================================================
;  kits/netkit/netkit.asm - NetKit : le reseau (docs/KITS.md, docs/RESEAU.md 3)
;  -> SD0:/lib/netkit.so
;  Sockets, DNS, ping, etat : requetes au service "net" (netd) ; Wi-Fi :
;  service "wifi" (le pilote). Donnees de plus de 448 octets : par le tampon
;  du process (128 Ko, 16 zones de 8 Ko, donne par netd a la premiere
;  utilisation ; une zone par appel, plusieurs threads a la fois).
;  Une attente de plus de 25 s est decoupee (netd rend E_AGAIN, on
;  redemande tant que le delai n'est pas ecoule).
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
include 'netkit.inc'
include '../../inc/netd.inc'

E_NOENT		= -2
E_NODEV		= -19
E_PIPE		= -32
E_INVAL		= -22
WAIT_SLICE	= 25000

segment readable executable
include 'netkit.abi'

; enveloppe : garde x1-x15 et x18 (comme un appel systeme), appelle f
macro keep_regs f {
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	stp	x5,x6,[sp,-16]!
	stp	x7,x8,[sp,-16]!
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	stp	x15,x18,[sp,-16]!
	bl	f
	ldp	x15,x18,[sp],16
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	ldp	x7,x8,[sp],16
	ldp	x5,x6,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	ldp	x29,x30,[sp],16
	ret
}

; w0 = adresse IPv4 (ordre hote), x1 = tampon -> x0 = fin ("a.b.c.d")
fmt_ip:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w19,w0
	mov	x21,x1
	mov	x20,24
.o:	lsrv	w0,w19,w20
	and	x0,x0,0xFF
	mov	x1,x21
	bl	fmt_dec
	mov	x21,x0
	cbz	x20,.e
	mov	w1,'.'
	strb	w1,[x21],1
	strb	wzr,[x21]
	sub	x20,x20,8
	b	.o
.e:	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = adresse -> console
print_ip:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_ip
	mov	x0,sp
	bl	puts
	add	sp,sp,32
	epilogue

; x0 = tampon netinfo (32 o) : attend une adresse DHCP (20 s max)
; -> x0 = 1 si obtenue
wait_dhcp:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,48
	mov	x19,x0
	mov	x20,100
.w:	mov	x0,x19
	bl	net_info_i
	ldr	w0,[x19,NI_DHCP]
	cmp	w0,4
	b.eq	.ok
	mov	x0,200
	sys	SYS_SLEEP
	subs	x20,x20,1
	b.ne	.w
	mov	x0,0
	b	.out
.ok:	mov	x0,1
.out:	add	sp,sp,48
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Liaison avec netd
; ---------------------------------------------------------------------------

; x0 = message -> x0 = resultat (le service "net" ouvert au premier appel ;
; repris s'il a ete relance)
ncall:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,nk_canal
	ldr	x0,[x0]
	cbnz	x0,.go
	adr	x0,s_net
	mov	x1,0				; (sans attendre : pas de netd, pas de reseau)
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	add	x0,x0,1
	adr	x1,nk_canal
	str	x0,[x1]
.go:	sub	x0,x0,1
	mov	x1,x19
	sys	SYS_SVC_CALL
	cmn	x0,32				; E_PIPE : netd s'est arrete
	b.ne	.out
	adr	x1,nk_canal
	str	xzr,[x1]
	adr	x1,nk_buf
	str	xzr,[x1]
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = adresse du tampon du process (0 si impossible)
nbuf:
	prologue
	sub	sp,sp,512
	adr	x0,nk_buf
	ldr	x0,[x0]
	cbnz	x0,.out
	mov	x0,NET_OP_OPEN
	str	x0,[sp]
	mov	x0,sp
	bl	ncall
	tbnz	x0,63,.non
	ldr	x0,[sp,WM_B]
	sys	SYS_BUF_MAP
	tbnz	x0,63,.non
	adr	x1,nk_buf
	str	x0,[x1]
	b	.out
.non:	mov	x0,0
.out:	add	sp,sp,512
	epilogue

; -> x0 = numero de zone pris (0..15)
zone_take:
	adr	x1,nk_zones
.t:	ldaxr	x2,[x1]
	mvn	x3,x2
	cbz	x3,.plein
	rbit	x3,x3
	clz	x0,x3				; premier bit libre
	cmp	x0,NET_ZONES
	b.hs	.plein
	mov	x3,1
	lsl	x3,x3,x0
	orr	x2,x2,x3
	stlxr	w3,x2,[x1]
	cbnz	w3,.t
	ret
.plein:	clrex
	str	x0,[sp,-16]!
	mov	x0,1
	sys	SYS_SLEEP
	ldr	x0,[sp],16
	adr	x1,nk_zones
	b	.t

; x0 = zone rendue
zone_give:
	adr	x1,nk_zones
	mov	x3,1
	lsl	x3,x3,x0
.t:	ldaxr	x2,[x1]
	bic	x2,x2,x3
	stlxr	w4,x2,[x1]
	cbnz	w4,.t
	ret

; x0 = destination, x1 = source, x2 = octets
copy:
	cbz	x2,.e
	orr	x3,x0,x1
	orr	x3,x3,x2
	tst	x3,7
	b.ne	.b
.q:	ldr	x3,[x1],8
	str	x3,[x0],8
	subs	x2,x2,8
	b.ne	.q
	ret
.b:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.b
.e:	ret

; x0 = delai (ms) -> x0 = echeance (sature)
deadline_of:
	mov	x1,x0
	sys	SYS_TIME_MS
	adds	x0,x0,x1
	b.cc	.r
	mov	x0,-1
.r:	ret

; x0 = echeance -> x0 = attente pour une requete (<= 25 s, 0 si passee)
slice_of:
	mov	x1,x0
	sys	SYS_TIME_MS
	subs	x0,x1,x0
	csel	x0,xzr,x0,lo
	movl	x1,WAIT_SLICE
	cmp	x0,x1
	csel	x0,x0,x1,lo
	ret

; ---------------------------------------------------------------------------
;  Sockets
; ---------------------------------------------------------------------------
net_socket:	keep_regs net_socket_i
net_connect:	keep_regs net_connect_i
net_bind:	keep_regs net_bind_i
net_listen:	keep_regs net_listen_i
net_accept:	keep_regs net_accept_i
net_send:	keep_regs net_send_i
net_recv:	keep_regs net_recv_i
net_close:	keep_regs net_close_i
net_shutdown:	keep_regs net_shutdown_i
net_sendto:	keep_regs net_sendto_i
net_recvfrom:	keep_regs net_recvfrom_i
net_resolve:	keep_regs net_resolve_i
net_info:	keep_regs net_info_i
net_active:	keep_regs net_active_i
net_ping:	keep_regs net_ping_i
net_ifaces:	keep_regs net_ifaces_i
net_time:	keep_regs net_time_i
net_ntp_sync:	keep_regs net_ntp_i
net_stats:	keep_regs net_stats_i
net_sockets:	keep_regs net_sockets_i
net_fork_child:	keep_regs net_fork_child_i

; Dans le fils d'un fork, juste apres : le tampon du process est celui du
; pere (netd l'a accorde a son pid) : demappe ici, un nouveau sera demande
; au premier besoin (op 1) ; zones liberees (des threads du pere les
; tenaient). Les sockets restent : netd les donne aussi au fils (avis de
; fork). -> x0 = 0
net_fork_child_i:
	prologue
	adr	x1,nk_zones
	str	xzr,[x1]
	adr	x1,nk_buf
	ldr	x0,[x1]
	str	xzr,[x1]
	cbz	x0,.out
	movl	x1,BUF_VA_BASE			; numero du tampon d'apres son adresse
	sub	x0,x0,x1
	lsr	x0,x0,26			; (BUF_SLOT = 64 Mo)
	sys	SYS_BUF_FREE
.out:	mov	x0,0
	epilogue

; x0 = op, x1 = a, x2 = b, x3 = c, x4 = d : requete simple -> x0
simple:
	prologue
	sub	sp,sp,512
	stp	x0,x1,[sp]
	stp	x2,x3,[sp,16]
	str	x4,[sp,32]
	str	wzr,[sp,WM_LEN]
	mov	x0,sp
	bl	ncall
	add	sp,sp,512
	epilogue

net_socket_i:
	mov	x1,x0
	mov	x0,NET_OP_SOCKET
	b	simple

net_connect_i:
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,NET_OP_CONNECT
	b	simple

net_bind_i:
	mov	x2,x1
	mov	x1,x0
	mov	x0,NET_OP_BIND
	b	simple

net_listen_i:
	mov	x2,x1
	mov	x1,x0
	mov	x0,NET_OP_LISTEN
	b	simple

net_close_i:
	mov	x1,x0
	mov	x0,NET_OP_CLOSE
	b	simple

net_shutdown_i:
	mov	x1,x0
	mov	x0,NET_OP_SHUTDOWN
	b	simple

; x0 = sd, x1 = delai
net_accept_i:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,x1
	bl	deadline_of
	mov	x20,x0
.r:	mov	x0,x20
	bl	slice_of
	mov	x2,x0
	mov	x1,x19
	mov	x0,NET_OP_ACCEPT
	bl	simple
	cmn	x0,11				; E_AGAIN : encore du temps ?
	b.ne	.out
	sys	SYS_TIME_MS
	cmp	x0,x20
	mov	x0,E_AGAIN
	b.lo	.r
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = ip, x1 = delai
net_ping_i:
	mov	x2,x1
	mov	x1,x0
	mov	x0,NET_OP_PING
	b	simple

; x0 = sd, x1 = tampon, x2 = octets -> octets envoyes
net_send_i:
	mov	x3,0
	mov	x4,0
	mov	x5,NET_OP_SEND
	b	send_common

; x0 = sd, x1 = tampon, x2 = octets, x3 = ip, x4 = port
net_sendto_i:
	and	x3,x3,0xFFFFFFFF
	and	x4,x4,0xFFFF
	orr	x3,x3,x4,lsl 32
	mov	x5,NET_OP_SENDTO
	b	send_common

; x0 = sd, x1 = tampon, x2 = octets, x3 = d (sendto : ip | port << 32),
; x5 = op
send_common:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x26,x5
	cmp	x21,NET_INLINE
	b.hi	.gros
	stp	x26,x19,[sp]			; dans le message
	mov	x0,-1
	stp	x0,x21,[sp,16]
	str	x22,[sp,32]
	str	w21,[sp,WM_LEN]
	add	x0,sp,WM_DATA
	mov	x1,x20
	mov	x2,x21
	bl	copy
	mov	x0,sp
	bl	ncall
	b	.out
.gros:	bl	nbuf				; par une zone du tampon du process
	cbz	x0,.nomem
	mov	x23,x0
	bl	zone_take
	mov	x24,x0
	add	x23,x23,x24,lsl 13		; adresse de la zone
	mov	x25,0				; envoye
.morceau:
	sub	x2,x21,x25
	mov	x0,NET_ZONE
	cmp	x2,x0
	csel	x2,x2,x0,ls
	str	x2,[sp,24]			; c = octets
	mov	x0,x23
	add	x1,x20,x25
	bl	copy
	stp	x26,x19,[sp]
	lsl	x0,x24,13			; b = decalage
	str	x0,[sp,16]
	str	x22,[sp,32]
	str	wzr,[sp,WM_LEN]
	mov	x0,sp
	bl	ncall
	tbnz	x0,63,.err
	add	x25,x25,x0
	ldr	x1,[sp,24]			; (envoi incomplet : delai)
	cmp	x0,x1
	b.lo	.fini
	cmp	x25,x21
	b.lo	.morceau
.fini:	mov	x0,x24
	bl	zone_give
	mov	x0,x25
	b	.out
.err:	mov	x1,x0
	mov	x0,x24
	str	x1,[sp,-16]!
	bl	zone_give
	ldr	x1,[sp],16
	mov	x0,x1
	cbz	x25,.out
	mov	x0,x25
	b	.out
.nomem:	mov	x0,-12
.out:	add	sp,sp,512
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sd, x1 = tampon, x2 = max, x3 = delai -> octets
net_recv_i:
	mov	x4,0
	mov	x5,NET_OP_RECV
	b	recv_common

; x0 = sd, x1 = tampon, x2 = max, x3 = delai, x4 = &{ip, port}
net_recvfrom_i:
	mov	x5,NET_OP_RECVFROM
	b	recv_common

recv_common:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x23,x4
	mov	x26,x5
	mov	x0,x3
	bl	deadline_of
	mov	x22,x0
	mov	x24,-1				; zone (-1 : donnees du message)
	cmp	x21,NET_INLINE
	b.ls	.r
	mov	x0,NET_ZONE			; une zone : 8 Ko au plus par appel
	cmp	x21,x0
	csel	x21,x21,x0,ls
	bl	nbuf
	cbz	x0,.nomem
	mov	x25,x0
	bl	zone_take
	mov	x24,x0
.r:	mov	x0,x22
	bl	slice_of
	str	x0,[sp,32]			; d = attente
	stp	x26,x19,[sp]
	mov	x0,-1
	tbnz	x24,63,.b
	lsl	x0,x24,13
.b:	stp	x0,x21,[sp,16]
	str	wzr,[sp,WM_LEN]
	mov	x0,sp
	bl	ncall
	cmn	x0,11				; E_AGAIN : encore du temps ?
	b.ne	.res
	sys	SYS_TIME_MS
	cmp	x0,x22
	mov	x0,E_AGAIN
	b.lo	.r
.res:	mov	x19,x0
	cmp	x19,0
	b.le	.src
	mov	x0,x20				; les donnees
	mov	x2,x19
	add	x1,sp,WM_DATA
	tbnz	x24,63,.cp
	add	x1,x25,x24,lsl 13
.cp:	bl	copy
.src:	cbz	x23,.zone			; source (recvfrom)
	ldr	x0,[sp,24]
	str	w0,[x23]
	ldr	x0,[sp,32]
	str	w0,[x23,4]
.zone:	tbnz	x24,63,.ret
	mov	x0,x24
	bl	zone_give
.ret:	mov	x0,x19
	b	.out
.nomem:	mov	x0,-12
.out:	add	sp,sp,512
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom -> IPv4
net_resolve_i:
	prologue
	sub	sp,sp,512
	mov	x1,NET_OP_RESOLVE
	str	x1,[sp]
	add	x2,sp,WM_DATA
	mov	x3,0
.c:	ldrb	w4,[x0,x3]
	strb	w4,[x2,x3]
	cbz	w4,.go
	add	x3,x3,1
	cmp	x3,255
	b.lo	.c
	strb	wzr,[x2,x3]
.go:	add	x3,x3,1
	str	w3,[sp,WM_LEN]
	mov	x0,sp
	bl	ncall
	add	sp,sp,512
	epilogue

; x0 = tampon 32 o (netinfo) -> 0
net_info_i:
	mov	x1,0
	mov	x2,32
	b	info_common

; x0 = tampon 16 o : nom, type de l'interface active -> 0
net_active_i:
	mov	x1,32
	mov	x2,16
	b	info_common

; x0 = tampon, x1 = decalage dans la reponse, x2 = octets
info_common:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,NET_OP_INFO
	str	x0,[sp]
	str	wzr,[sp,WM_LEN]
	mov	x0,sp
	bl	ncall
	tbnz	x0,63,.vide
	mov	x0,x19
	add	x1,sp,WM_DATA
	add	x1,x1,x20
	mov	x2,x21
	bl	copy
	mov	x0,0
	b	.out
.vide:	mov	x1,0				; pas de netd : rien
.z:	str	xzr,[x19,x1]
	add	x1,x1,8
	cmp	x1,x21
	b.lo	.z
.out:	add	sp,sp,512
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon, x1 = max -> nombre d'interfaces copiees
net_ifaces_i:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x0,NET_OP_IFACES
	str	x0,[sp]
	str	wzr,[sp,WM_LEN]
	mov	x0,sp
	bl	ncall
	tbnz	x0,63,.out
	cmp	x0,x20
	csel	x20,x0,x20,lo
	mov	x0,x19
	add	x1,sp,WM_DATA
	lsl	x2,x20,6
	bl	copy
	mov	x0,x20
.out:	add	sp,sp,512
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon de NST_SIZE octets -> 0 : compteurs de netd
net_stats_i:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x0,NET_OP_STATS
	str	x0,[sp]
	str	wzr,[sp,WM_LEN]
	mov	x0,sp
	bl	ncall
	tbnz	x0,63,.out
	mov	x0,x19
	add	x1,sp,WM_DATA
	mov	x2,NST_SIZE
	bl	copy
	mov	x0,0
.out:	add	sp,sp,512
	ldr	x19,[sp],16
	epilogue

; x0 = tampon, x1 = max -> nombre d'entrees NSK_ENT copiees : les sockets
; ouverts de tout le systeme (netstat), une requete par 14 entrees
net_sockets_i:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; entrees copiees
	mov	x22,0				; emplacement de netd
.req:	cmp	x21,x20
	b.hs	.fin
	mov	x0,NET_OP_SOCKETS
	stp	x0,x22,[sp]
	str	wzr,[sp,WM_LEN]
	mov	x0,sp
	bl	ncall
	tbnz	x0,63,.err
	ldr	x22,[sp,WM_B]
	sub	x1,x20,x21			; place qui reste
	cmp	x0,x1
	csel	x2,x0,x1,lo
	str	x0,[sp,WM_A]			; (entrees de cette reponse)
	add	x0,x19,x21,lsl 5		; NSK_ENT = 32
	add	x21,x21,x2
	lsl	x2,x2,5
	add	x1,sp,WM_DATA
	bl	copy
	ldr	x0,[sp,WM_A]			; moins qu'un plein message : la fin
	cmp	x0,NSK_PER_MSG
	b.eq	.req
.fin:	mov	x0,x21
	b	.out
.err:	cbnz	x21,.fin
.out:	add	sp,sp,512
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = &{utc, ntp, ms} ou 0 -> heure UTC (0 : inconnue)
net_time_i:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x0,NET_OP_TIME
	str	x0,[sp]
	str	xzr,[sp,8]
	str	xzr,[sp,16]
	str	xzr,[sp,24]
	mov	x0,sp
	bl	ncall
	tbnz	x0,63,.zero
	ldr	x0,[sp,WM_A]
	cbz	x19,.out
	ldp	x1,x2,[sp,WM_A]
	stp	x1,x2,[x19]
	ldr	x1,[sp,WM_C]
	str	x1,[x19,16]
	b	.out
.zero:	mov	x0,0
	cbz	x19,.out
	stp	xzr,xzr,[x19]
	str	xzr,[x19,16]
.out:	add	sp,sp,512
	ldr	x19,[sp],16
	epilogue

; x0 = 1 : attendre la synchronisation (20 s au plus), 0 : la demander
net_ntp_i:
	mov	x1,x0
	mov	x0,NET_OP_NTP
	b	simple

; ---------------------------------------------------------------------------
;  Wi-Fi : service "wifi" (pilote wifi). Sans lui : E_NODEV
; ---------------------------------------------------------------------------
wifi_scan:	keep_regs wifi_scan_i
wifi_join:	keep_regs wifi_join_i
wifi_status:	keep_regs wifi_status_i
wifi_leave:	keep_regs wifi_leave_i
wifi_forget:	keep_regs wifi_forget_i
wifi_known:	keep_regs wifi_known_i
wifi_priority:	keep_regs wifi_priority_i
wifi_country:	keep_regs wifi_country_i
wifi_join_ex:	keep_regs wifi_join_ex_i

; x0 = message -> x0 = resultat (E_NODEV sans service "wifi")
wcall:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,nk_wifi
	ldr	x0,[x0]
	cbnz	x0,.go
	adr	x0,s_wifi
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.nodev
	add	x0,x0,1
	adr	x1,nk_wifi
	str	x0,[x1]
.go:	sub	x0,x0,1
	mov	x1,x19
	sys	SYS_SVC_CALL
	cmn	x0,32				; E_PIPE : le pilote s'est arrete
	b.ne	.out
	adr	x1,nk_wifi
	str	xzr,[x1]
.nodev:	mov	x0,E_NODEV
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = op, x1 = a, x2 = b, x3 = chaine 1 (0 : aucune), x4 = chaine 2 (0)
; -> x0 ; x1 = message (512 o sur la pile de l'appelant : x5)
wsimple:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x5
	stp	x0,x1,[x19]
	str	x2,[x19,16]
	add	x6,x19,WM_DATA			; donnees : chaine 1, 0, chaine 2, 0
	add	x7,x19,WM_DATA + 460
	cbz	x3,.s2
.c1:	ldrb	w8,[x3],1
	strb	w8,[x6],1
	cbz	w8,.s2
	cmp	x6,x7
	b.lo	.c1
	strb	wzr,[x6],1
.s2:	cbz	x4,.fin
.c2:	ldrb	w8,[x4],1
	strb	w8,[x6],1
	cbz	w8,.fin
	cmp	x6,x7
	b.lo	.c2
	strb	wzr,[x6],1
.fin:	add	x0,x19,WM_DATA
	sub	x0,x6,x0
	str	w0,[x19,WM_LEN]
	mov	x0,x19
	bl	wcall
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon 64 o -> 0
wifi_status_i:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x0,WIFI_OP_STATUS
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,0
	mov	x5,sp
	bl	wsimple
	tbnz	x0,63,.vide
	mov	x0,x19
	add	x1,sp,WM_DATA
	mov	x2,64
	bl	copy
	mov	x0,0
	b	.out
.vide:	mov	x1,0				; etat 0 : absent
.z:	str	xzr,[x19,x1]
	add	x1,x1,8
	cmp	x1,64
	b.lo	.z
.out:	add	sp,sp,512
	ldr	x19,[sp],16
	epilogue

; x0 = tampon, x1 = max -> n entrees de 48 o (balayage : environ 3 s)
wifi_scan_i:
	mov	x2,WIFI_OP_SCAN
	b	list_common

; x0 = tampon, x1 = max -> n reseaux connus (WK_ENT o)
wifi_known_i:
	mov	x2,WIFI_OP_KNOWN
	b	list_common

; x0 = tampon, x1 = max, x2 = op : pages de 9 entrees de 48 o
list_common:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x23,x2
	mov	x21,0				; entrees copiees
.page:	mov	x0,x23
	mov	x1,x21				; a = premiere
	cmp	x21,0				; b = 1 : pages suivantes du meme
	cset	x2,ne				;   resultat
	mov	x3,0
	mov	x4,0
	mov	x5,sp
	bl	wsimple
	tbnz	x0,63,.err
	mov	x22,x0				; total
	cmp	x22,x20
	csel	x22,x22,x20,ls
	sub	x2,x22,x21			; dans cette page
	cmp	x2,WIFI_PER_MSG
	mov	x3,WIFI_PER_MSG
	csel	x2,x2,x3,ls
	cmp	x2,0
	b.le	.fini
	mov	x3,48
	madd	x0,x21,x3,x19
	add	x21,x21,x2
	mul	x2,x2,x3
	add	x1,sp,WM_DATA
	bl	copy
	cmp	x21,x22
	b.lo	.page
.fini:	mov	x0,x21
	b	.out
.err:	cbz	x21,.out
	mov	x0,x21
.out:	add	sp,sp,512
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = SSID, x1 = cle (0 ou vide : ouvert) -> 0 (memorise dans wifi.txt)
wifi_join_i:
	mov	x2,1
	mov	x3,0
; x0 = SSID, x1 = cle, x2 = memoriser, x3 = priorite
wifi_join_ex_i:
	prologue
	sub	sp,sp,512
	mov	x4,x1
	adr	x5,s_vide
	cmp	x4,0
	csel	x4,x5,x4,eq
	mov	x5,x3
	mov	x1,x2
	mov	x2,x5
	mov	x3,x0
	mov	x0,WIFI_OP_JOIN
	mov	x5,sp
	bl	wsimple
	add	sp,sp,512
	epilogue

wifi_leave_i:
	prologue
	sub	sp,sp,512
	mov	x0,WIFI_OP_LEAVE
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,0
	mov	x5,sp
	bl	wsimple
	add	sp,sp,512
	epilogue

; x0 = SSID
wifi_forget_i:
	prologue
	sub	sp,sp,512
	mov	x3,x0
	mov	x0,WIFI_OP_FORGET
	mov	x1,0
	mov	x2,0
	mov	x4,0
	mov	x5,sp
	bl	wsimple
	add	sp,sp,512
	epilogue

; x0 = SSID, x1 = priorite
wifi_priority_i:
	prologue
	sub	sp,sp,512
	mov	x3,x0
	mov	x0,WIFI_OP_PRIORITY
	mov	x2,0
	mov	x4,0
	mov	x5,sp
	bl	wsimple
	add	sp,sp,512
	epilogue

; x0 = pays (" FR ")
wifi_country_i:
	prologue
	sub	sp,sp,512
	mov	x3,x0
	mov	x0,WIFI_OP_COUNTRY
	mov	x1,0
	mov	x2,0
	mov	x4,0
	mov	x5,sp
	bl	wsimple
	add	sp,sp,512
	epilogue

; copies privees (un kit ne depend pas encore d'un autre kit)
include '../../inc/lib.inc'

s_net		db 'net',0
s_wifi		db 'wifi',0
s_vide		db 0
		align 8

segment readable writeable
		align 8
nk_canal	dd 0				; canal de netd + 1 (0 : a ouvrir)
nk_wifi		dd 0				; canal du service wifi + 1
nk_buf		dd 0				; tampon du process (0 : pas encore ;
						; oublie dans le fils d'un fork)
nk_zones	dd 0				; zones prises (bits)
