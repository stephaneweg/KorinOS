; ============================================================================
;  stack.inc - pile TCP/IP de netd (docs/RESEAU.md) : reprise de
;  src/kernel/net.asm (le noyau l'a gardee jusqu'a l'elagage, etape 6).
;
;  Reimplementee d'apres les RFC : 826 (ARP), 791 (IPv4), 792 (ICMP),
;  768 (UDP), 2131/2132 (DHCP), 1035 (DNS), 793/9293 (TCP), 6298 (RTO, simplifie).
;
;  * Thread 'pile' de netd : vide les anneaux des interfaces, traite les
;    trames, fait tourner les minuteries (DHCP, retransmissions TCP) toutes
;    les 10 ms.
;  * Tout l'etat est protege par net_lock (verrou tournant de netd). Les
;    requetes bloquantes (threads 'requetes') attendent SANS le verrou.
;  * Adresses IPv4 et ports en ordre "hote" dans les variables et l'API
;    (10.0.2.15 = 0x0A00020F) ; ordre reseau seulement dans les trames.
;
;  Limites : pas de fragmentation IP (ni emission ni reassemblage). TCP :
;  controle de congestion NewReno (RFC 5681, 6582 : tcp_cc_*), accuses
;  retardes, options MSS et window scale, sondes de fin de rafale (tcp_probe_*).
; ============================================================================

ETH_HDR		= 14
IP_HDR		= 20
PAYLOAD		= ETH_HDR + IP_HDR		; 34 : donnees apres l'en-tete IP (emission)
ETH_ARP		= 0x0806
ETH_IP		= 0x0800
PROTO_ICMP	= 1
PROTO_TCP	= 6
PROTO_UDP	= 17

ARP_ENTRIES	= 16

; files de trames (pilotes -> pile, pile -> pilotes)
NETQ_SLOTS	= 32			; trames par file
NETQ_SLOT	= 1552			; 8 (longueur) + 1544
NETQ_PAGES	= (NETQ_SLOTS * NETQ_SLOT + 4095) / 4096

MAX_SOCK	= 64
SK_RING		= 262144		; tampons des sockets (puissance de 2)
SK_RING_PAGES	= SK_RING / 4096
TCP_MSS		= 1460
TCP_RTO_MIN	= 1000			; ms
TCP_WS_SHIFT	= 3			; window scale (RFC 7323) : le notre
TCP_IW		= 10			; fenetre de congestion initiale (segments)
TCP_SSTHRESH	= 0x100000		; seuil initial
TCP_ACK_EVERY	= 8			; un accuse pour tant de segments pleins
TCP_ACK_MS	= 20			; au plus tard (ms)
TCP_ACK_FULL	= 1000			; octets : un segment plein
TCP_DUPACK_MAX	= 3			; accuses dupliques avant retransmission rapide
TCP_PROBE_MS	= 60			; sonde de fin de rafale : premier delai (ms)
TCP_PROBE_MAX	= 4			; sondes avant le RTO (delai double a chacune)
EPHEMERAL	= 49152

; --- socket (SK_SIZE octets) --------------------------------------------------
SK_USED		= 0
SK_TYPE		= 8			; 1 TCP, 2 UDP
SK_STATE	= 16
SK_OWNER	= 24			; pid proprietaire (0 : interne ou orphelin)
SK_LPORT	= 40
SK_RIP		= 48
SK_RPORT	= 56
SK_RXBUF	= 64
SK_RXHEAD	= 72			; compteurs monotones (index = compteur & masque)
SK_RXTAIL	= 80
SK_TXBUF	= 88
SK_TXHEAD	= 96
SK_TXTAIL	= 104			; correspond a SND_UNA
SK_SND_UNA	= 112
SK_SND_NXT	= 120
SK_SND_WND	= 128
SK_RCV_NXT	= 136
SK_ISS		= 144
SK_MSS		= 152
SK_RTO		= 160
SK_RTX_AT	= 168			; echeance de retransmission (ms), 0 = desarmee
SK_RETRIES	= 176
SK_SRTT		= 184
SK_RTT_SEQ	= 192			; sequence chronometree (0 : aucune)
SK_RTT_START	= 200
SK_FLAGS	= 208
SK_FIN_SEQ	= 216
SK_PARENT	= 224			; socket en ecoute (fils pas encore accepte)
SK_BACKLOG	= 232
SK_DEADLINE	= 240			; fin de TIME_WAIT / delai d'un orphelin
SK_ACKNOW	= 248
SK_OOO_N	= 256			; plages recues hors ordre (deja dans l'anneau)
SK_OOO		= 264			; OOO_MAX x (debut, fin) en numeros de sequence
OOO_MAX		= 4
SK_CWND		= 296			; fenetre de congestion (octets)
SK_SSTHRESH	= 304
SK_DUPACKS	= 312			; accuses dupliques de suite
SK_RECOVER	= 320			; recuperation : fin (SND_NXT au moment de la perte)
SK_INREC	= 328			; 1 : en recuperation
SK_ACK_CNT	= 336			; accuses retardes : segments pas encore acquittes
SK_ACK_AT	= 344			;   echeance (ms), 0 : aucun
SK_RX_LEN	= 352			; (tcp_input) octets du segment en ordre
SK_WS_OK	= 360			; 1 : le pair a l'option window scale
SK_SND_SHIFT	= 368			; son decalage (0 sans l'option)
SK_RCV_SHIFT	= 376			; le notre
SK_PROBE_AT	= 384			; prochaine sonde de fin de rafale (ms)
SK_PROBE_N	= 392			; sondes deja parties depuis le dernier accuse
SK_SIZE		= 512

SKF_FIN_PENDING	= 0			; l'application a ferme : FIN a envoyer
SKF_FIN_SENT	= 1
SKF_RX_FIN	= 2			; le pair a ferme
SKF_RESET	= 3
SKF_TIMEOUT	= 4
SKF_REFUSED	= 5
SKF_ACCEPTED	= 6
SKF_ORPHAN	= 7

TCP_CLOSED	= 0
TCP_LISTEN	= 1
TCP_SYN_SENT	= 2
TCP_SYN_RCVD	= 3
TCP_ESTAB	= 4
TCP_FIN_WAIT1	= 5
TCP_FIN_WAIT2	= 6
TCP_CLOSE_WAIT	= 7
TCP_CLOSING	= 8
TCP_LAST_ACK	= 9
TCP_TIME_WAIT	= 10

TH_FIN		= 0x01
TH_SYN		= 0x02
TH_RST		= 0x04
TH_PSH		= 0x08
TH_ACK		= 0x10

SOCK_TCP	= 1
SOCK_UDP	= 2

E_TIMEDOUT	= -110
E_CONNREFUSED	= -111
E_CONNRESET	= -104
E_ADDRINUSE	= -98
E_NOTCONN	= -107
E_HOSTUNREACH	= -113

DHCP_IDLE	= 0
DHCP_INIT	= 1
DHCP_SELECTING	= 2
DHCP_REQUESTING	= 3
DHCP_BOUND	= 4

; ============================================================================
;  Initialisation et thread
; ============================================================================

; A appeler apres usb_init : files, tables, thread "net"
csum_add:
.l:	cmp	x1,2
	b.lo	.odd
	ldrb	w3,[x0],1
	ldrb	w4,[x0],1
	orr	w3,w4,w3,lsl 8
	add	x2,x2,x3
	sub	x1,x1,2
	b	.l
.odd:	cbz	x1,.d
	ldrb	w3,[x0]
	add	x2,x2,x3,lsl 8
.d:	mov	x0,x2
	ret

; x0 = accumulateur -> w0 = complement a un replie sur 16 bits
csum_fold:
.f:	lsr	x1,x0,16
	cbz	x1,.e
	and	x0,x0,0xFFFF
	add	x0,x0,x1
	b	.f
.e:	mvn	w0,w0
	and	w0,w0,0xFFFF
	ret

; w0 = source, w1 = destination, w2 = protocole, w3 = longueur
; -> x0 = accumulateur du pseudo-en-tete TCP/UDP
csum_pseudo:
	lsr	w4,w0,16
	and	w5,w0,0xFFFF
	add	x6,x4,x5
	lsr	w4,w1,16
	and	w5,w1,0xFFFF
	add	x6,x6,x4
	add	x6,x6,x5
	add	x6,x6,x2
	add	x0,x6,x3
	ret

; x0 = destination, x1 = source, x2 = octets
net_copy:
	cbz	x2,.e
.c:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.c
.e:	ret

; w0 = adresse IPv4 -> console "a.b.c.d"
kput_ip:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	mov	x20,24
.o:	lsrv	w0,w19,w20
	and	x0,x0,0xFF
	bl	kput_dec
	cbz	x20,.e
	mov	w0,'.'
	bl	kputc
	sub	x20,x20,8
	b	.o
.e:	ldp	x19,x20,[sp],16
	epilogue

; x0 = trame Ethernet, x1 = longueur -> x0 = 1 si mise en file d'emission
; Carte active : netdev_tx_fn si renseigne (GENET, WiFi), sinon la carte de
; base (netbase_tx : l'ECM USB), sinon rien.
net_reset_ip:
	adr	x0,net_ip
	str	wzr,[x0]
	adr	x0,net_gw
	str	wzr,[x0]
	adr	x0,dhcp_state
	mov	w1,DHCP_INIT
	str	w1,[x0]
	adr	x0,dhcp_next_ms
	str	xzr,[x0]
	adr	x0,dhcp_tries
	str	xzr,[x0]
	adr	x0,arp_table			; les voisins ont pu changer
	mov	x1,ARP_ENTRIES * 16
	b	mem_zero

; ============================================================================
;  Reception
; ============================================================================

; x0 = trame, x1 = longueur (sous net_lock)
net_input:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x20,ETH_HDR
	b.lo	.out
	ld16be	w2,x19,12
	cmp	w2,ETH_ARP
	b.eq	.arp
	cmp	w2,ETH_IP
	b.ne	.out
	mov	x0,x19
	mov	x1,x20
	bl	ip_input
	b	.out
.arp:	mov	x0,x19
	mov	x1,x20
	bl	arp_input
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- ARP ----------------------------------------------------------------------

; x0 = trame, x1 = longueur
arp_input:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x1,ETH_HDR+28
	b.lo	.out
	add	x19,x19,ETH_HDR
	ld16be	w0,x19,0			; materiel Ethernet, protocole IPv4
	cmp	w0,1
	b.ne	.out
	ld16be	w0,x19,2
	cmp	w0,ETH_IP
	b.ne	.out
	ld32be	w0,x19,14			; IP emetteur -> cache
	add	x1,x19,8
	bl	arp_update
	ld16be	w0,x19,6			; operation
	cmp	w0,1
	b.ne	.out				; reponse : deja prise en compte
	ld32be	w0,x19,24			; demande pour nous ?
	adr	x1,net_ip
	ldr	w1,[x1]
	cbz	w1,.out
	cmp	w0,w1
	b.ne	.out
	; reponse : emetteur = nous, cible = le demandeur
	adr	x20,net_arpbuf
	add	x0,x20,0			; Ethernet : destination = demandeur
	add	x1,x19,8
	mov	x2,6
	bl	net_copy
	add	x0,x20,6
	adr	x1,netdev_mac
	mov	x2,6
	bl	net_copy
	mov	w0,ETH_ARP
	st16be	w0,x20,12
	mov	w0,1
	st16be	w0,x20,14
	mov	w0,ETH_IP
	st16be	w0,x20,16
	mov	w0,0x0604			; hlen 6, plen 4 (st16be : 06 puis 04)
	st16be	w0,x20,18
	mov	w0,2
	st16be	w0,x20,20
	add	x0,x20,22
	adr	x1,netdev_mac
	mov	x2,6
	bl	net_copy
	adr	x0,net_ip
	ldr	w0,[x0]
	st32be	w0,x20,28
	add	x0,x20,32
	add	x1,x19,8
	mov	x2,10				; MAC + IP du demandeur
	bl	net_copy
	mov	x0,x20
	mov	x1,60
	bl	netif_send
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = IP, x1 = MAC (6 octets) : ajoute / met a jour le cache ARP
arp_update:
	cbz	w0,.ret
	adr	x2,arp_table
	mov	x3,0
.f:	add	x4,x2,x3,lsl 4
	ldr	w4,[x4]
	cmp	w4,w0
	b.eq	.set
	add	x3,x3,1
	cmp	x3,ARP_ENTRIES
	b.lo	.f
	adr	x4,arp_next			; nouvelle entree (remplacement circulaire)
	ldr	w3,[x4]
	add	w5,w3,1
	and	w5,w5,ARP_ENTRIES-1
	str	w5,[x4]
.set:	add	x4,x2,x3,lsl 4
	str	w0,[x4]
	ldr	w5,[x1]
	str	w5,[x4,4]
	ldrh	w5,[x1,4]
	strh	w5,[x4,8]
	mov	w5,1
	strh	w5,[x4,10]
.ret:	ret

; w0 = IP -> x0 = MAC (dans le cache), 0 si inconnue
arp_lookup:
	lsr	w2,w0,28			; multidiffusion (mDNS) : la MAC vient de l'adresse
	cmp	w2,0xE
	b.eq	.multi
	adr	x2,arp_table
	mov	x3,0
.f:	add	x4,x2,x3,lsl 4
	ldr	w5,[x4]
	cmp	w5,w0
	b.ne	.n
	ldrh	w5,[x4,10]
	cbz	w5,.n
	add	x0,x4,4
	ret
.n:	add	x3,x3,1
	cmp	x3,ARP_ENTRIES
	b.lo	.f
	mov	x0,0
	ret
.multi:	adr	x2,net_mcmac			; 01:00:5E, puis les 23 bits du bas
	mov	w3,0x01
	strb	w3,[x2]
	strb	wzr,[x2,1]
	mov	w3,0x5E
	strb	w3,[x2,2]
	ubfx	w3,w0,16,7
	strb	w3,[x2,3]
	ubfx	w3,w0,8,8
	strb	w3,[x2,4]
	strb	w0,[x2,5]
	mov	x0,x2
	ret

; w0 = IP -> envoie une demande ARP
arp_request:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	adr	x20,net_arpbuf
	mov	x0,x20
	mov	x1,64
	bl	mem_zero
	mov	w0,-1				; diffusion
	str	w0,[x20]
	strh	w0,[x20,4]
	add	x0,x20,6
	adr	x1,netdev_mac
	mov	x2,6
	bl	net_copy
	mov	w0,ETH_ARP
	st16be	w0,x20,12
	mov	w0,1
	st16be	w0,x20,14
	mov	w0,ETH_IP
	st16be	w0,x20,16
	mov	w0,0x0604			; hlen 6, plen 4 (st16be : 06 puis 04)
	st16be	w0,x20,18
	mov	w0,1				; demande
	st16be	w0,x20,20
	add	x0,x20,22
	adr	x1,netdev_mac
	mov	x2,6
	bl	net_copy
	adr	x0,net_ip
	ldr	w0,[x0]
	st32be	w0,x20,28
	st32be	w19,x20,38
	mov	x0,x20
	mov	x1,60
	bl	netif_send
	ldp	x19,x20,[sp],16
	epilogue

; w0 = destination -> w0 = prochain saut (destination ou passerelle)
ip_next_hop:
	lsr	w1,w0,28			; (multidiffusion : directement sur le lien)
	cmp	w1,0xE
	b.eq	.local
	adr	x1,net_ip
	ldr	w2,[x1]
	adr	x1,net_mask
	ldr	w3,[x1]
	eor	w4,w0,w2
	tst	w4,w3
	b.eq	.local
	adr	x1,net_gw
	ldr	w0,[x1]
.local:	ret

; w0 = destination : resout l'adresse MAC du prochain saut, en attendant
; jusqu'a 1 s (contexte d'appel systeme, SANS net_lock) -> x0 = 1 si connue
arp_resolve_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	cmn	w0,1
	b.eq	.yes				; diffusion
	mov	w19,w0
	bl	ip_is_local
	cbnz	x0,.yes				; nous : la boucle locale
	mov	w0,w19
	bl	ip_next_hop
	mov	w19,w0
	bl	time_ms
	add	x20,x0,1000
	mov	x21,0				; prochaine demande
.try:	net_lock_take
	mov	w0,w19
	bl	arp_lookup
	mov	x22,x0
	cbnz	x22,.unlock
	bl	time_ms
	cmp	x0,x21
	b.lo	.unlock
	add	x21,x0,200
	mov	w0,w19
	bl	arp_request
.unlock:
	net_lock_drop
	cbnz	x22,.yes
	bl	time_ms
	cmp	x0,x20
	b.hs	.no
	mov	x0,5
	bl	thread_sleep
	b	.try
.yes:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- IPv4 ---------------------------------------------------------------------

; x0 = trame, x1 = longueur
ip_input:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x23,x0				; trame
	add	x19,x0,ETH_HDR			; en-tete IP
	sub	x20,x1,ETH_HDR
	cmp	x20,IP_HDR
	b.lo	.out
	ldrb	w0,[x19]
	lsr	w1,w0,4
	cmp	w1,4
	b.ne	.out
	and	w21,w0,0xF
	lsl	w21,w21,2			; longueur de l'en-tete
	cmp	w21,IP_HDR
	b.lo	.out
	cmp	x21,x20
	b.hi	.out
	ld16be	w22,x19,2			; longueur totale
	cmp	x22,x20
	b.hi	.out
	cmp	x22,x21
	b.lo	.out
	mov	x0,x19				; somme de controle de l'en-tete
	mov	x1,x21
	mov	x2,0
	bl	csum_add
	bl	csum_fold
	cbnz	w0,.out
	ld16be	w0,x19,6			; fragments : non geres
	and	w0,w0,0x3FFF
	cbnz	w0,.out
	ld32be	w24,x19,12			; source
	ld32be	w0,x19,16			; destination : nous, diffusion, boucle
	adr	x1,net_ip
	ldr	w1,[x1]
	cmp	w0,w1
	b.eq	.ours
	cmn	w0,1
	b.eq	.ours
	lsr	w2,w0,24
	cmp	w2,127
	b.ne	.out
.ours:	cbz	w1,.no_learn			; apprendre l'adresse MAC du voisin
	adr	x2,net_mask
	ldr	w2,[x2]
	eor	w3,w24,w1
	tst	w3,w2
	b.ne	.no_learn
	mov	w0,w24
	add	x1,x23,6
	bl	arp_update
.no_learn:
	ldrb	w0,[x19,9]			; protocole
	add	x1,x19,x21			; charge utile
	sub	x2,x22,x21			; sa longueur
	mov	w3,w24				; IP source
	ld32be	w4,x19,16
	cmp	w0,PROTO_ICMP
	b.eq	.icmp
	cmp	w0,PROTO_UDP
	b.eq	.udp
	cmp	w0,PROTO_TCP
	b.ne	.out
	mov	x0,x1
	mov	x1,x2
	mov	w2,w3
	mov	w3,w4
	bl	tcp_input
	b	.out
.udp:	mov	x0,x1
	mov	x1,x2
	mov	w2,w3
	mov	w3,w4
	bl	udp_input
	b	.out
.icmp:	mov	x0,x1
	mov	x1,x2
	mov	w2,w3
	bl	icmp_input
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = destination, w1 = protocole, x2 = longueur de la charge utile (deja
; ecrite a net_txbuf + PAYLOAD) -> x0 = 1 si la trame est partie
; (0 : adresse MAC inconnue, demande ARP envoyee, trame perdue)
ip_output:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w20,w0
	mov	w21,w1
	mov	x22,x2
	adr	x19,net_txbuf
	ldr	x19,[x19]
	add	x0,x19,ETH_HDR			; en-tete IP
	mov	w1,0x45
	strb	w1,[x0]
	strb	wzr,[x0,1]
	add	w1,w22,IP_HDR
	st16be	w1,x0,2
	adr	x2,net_ip_id
	ldr	w1,[x2]
	add	w3,w1,1
	str	w3,[x2]
	st16be	w1,x0,4
	mov	w1,0x4000			; ne pas fragmenter
	st16be	w1,x0,6
	mov	w1,64
	strb	w1,[x0,8]
	strb	w21,[x0,9]
	strh	wzr,[x0,10]
	mov	x3,x0
	mov	w0,w20
	bl	ip_src
	mov	w1,w0
	mov	x0,x3
	st32be	w1,x0,12
	st32be	w20,x0,16
	mov	x1,IP_HDR
	mov	x2,0
	bl	csum_add
	bl	csum_fold
	add	x1,x19,ETH_HDR
	st16be	w0,x1,10

	mov	w0,w20				; vers nous : pas de reseau
	bl	ip_is_local
	cbz	x0,.eth
	mov	w0,ETH_IP
	st16be	w0,x19,12
	mov	x0,x19
	add	x1,x22,PAYLOAD
	bl	lo_put
	b	.out
.eth:	cmn	w20,1				; Ethernet : destination
	b.ne	.unicast
	mov	w0,-1
	str	w0,[x19]
	strh	w0,[x19,4]
	b	.src
.unicast:
	mov	w0,w20
	bl	ip_next_hop
	mov	w21,w0
	bl	arp_lookup
	cbnz	x0,.known
	mov	w0,w21
	bl	arp_request
	mov	x0,0
	b	.out
.known:	ldr	w1,[x0]
	str	w1,[x19]
	ldrh	w1,[x0,4]
	strh	w1,[x19,4]
.src:	add	x0,x19,6
	adr	x1,netdev_mac
	mov	x2,6
	bl	net_copy
	mov	w0,ETH_IP
	st16be	w0,x19,12
	add	x1,x22,PAYLOAD			; longueur de trame (minimum 60)
	cmp	x1,60
	b.hs	.send
	add	x0,x19,x1			; bourrage a zero
.pad:	strb	wzr,[x0],1
	add	x1,x1,1
	cmp	x1,60
	b.lo	.pad
.send:	mov	x0,x19
	bl	netif_send
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- ICMP ---------------------------------------------------------------------

; x0 = message, x1 = longueur, w2 = source
icmp_input:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	cmp	x20,8
	b.lo	.out
	movl	x0,1400
	cmp	x20,x0
	b.hi	.out
	mov	x0,x19
	mov	x1,x20
	mov	x2,0
	bl	csum_add
	bl	csum_fold
	cbnz	w0,.out
	ldrb	w0,[x19]
	cmp	w0,8				; demande d'echo -> reponse
	b.eq	.echo
	cbnz	w0,.out				; 0 : reponse d'echo
	ld16be	w0,x19,4			; identifiant / sequence attendus ?
	adr	x1,ping_id
	ldr	w1,[x1]
	cmp	w0,w1
	b.ne	.out
	ld16be	w0,x19,6
	adr	x1,ping_seq
	ldr	w1,[x1]
	cmp	w0,w1
	b.ne	.out
	bl	time_ms
	adr	x1,ping_reply_ms
	str	x0,[x1]
	b	.out
.echo:	adr	x22,net_txbuf
	ldr	x22,[x22]
	add	x0,x22,PAYLOAD
	mov	x1,x19
	mov	x2,x20
	bl	net_copy
	add	x0,x22,PAYLOAD
	strb	wzr,[x0]			; type 0 : reponse
	strh	wzr,[x0,2]
	mov	x1,x20
	mov	x2,0
	bl	csum_add
	bl	csum_fold
	add	x1,x22,PAYLOAD
	st16be	w0,x1,2
	mov	w0,w21
	mov	w1,PROTO_ICMP
	mov	x2,x20
	bl	ip_output
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- UDP ----------------------------------------------------------------------

; x0 = datagramme, x1 = longueur, w2 = source, w3 = destination
udp_input:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	mov	w22,w3
	cmp	x20,8
	b.lo	.out
	ld16be	w0,x19,4			; longueur UDP
	cmp	x0,x20
	b.hi	.out
	cmp	x0,8
	b.lo	.out
	mov	x20,x0
	ldrh	w0,[x19,6]			; somme de controle (0 : absente)
	cbz	w0,.ok
	mov	w0,w21
	mov	w1,w22
	mov	w2,PROTO_UDP
	mov	w3,w20
	bl	csum_pseudo
	mov	x2,x0
	mov	x0,x19
	mov	x1,x20
	bl	csum_add
	bl	csum_fold
	cbnz	w0,.out
.ok:	ld16be	w23,x19,2			; port de destination
	cmp	w23,68				; client DHCP
	b.ne	.sock
	add	x0,x19,8
	sub	x1,x20,8
	bl	dhcp_input
	b	.out
.sock:	mov	w0,SOCK_UDP
	mov	w1,w23
	bl	sock_find_port
	cbz	x0,.out
	mov	x24,x0
	; enregistrement : longueur(2) port(2) ip(4) donnees, si la place suffit
	ldr	x1,[x24,SK_RXHEAD]
	ldr	x2,[x24,SK_RXTAIL]
	sub	x3,x1,x2
	mov	x4,SK_RING
	sub	x3,x4,x3			; place libre
	sub	x5,x20,8			; donnees
	add	x6,x5,8
	cmp	x6,x3
	b.hi	.out
	sub	sp,sp,16
	strh	w5,[sp]
	ld16be	w0,x19,0
	strh	w0,[sp,2]
	str	w21,[sp,4]
	mov	x0,x24
	mov	x1,sp
	mov	x2,8
	bl	sk_rx_write
	add	sp,sp,16
	mov	x0,x24
	add	x1,x19,8
	sub	x2,x20,8
	bl	sk_rx_write
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = port source, w1 = destination, w2 = port destination, x3 = donnees,
; x4 = longueur (x3 = 0 : donnees deja a net_txbuf + PAYLOAD + 8)
; -> x0 = 1 si envoye
udp_output:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	w20,w0
	mov	w21,w1
	mov	w22,w2
	mov	x23,x4
	adr	x19,net_txbuf
	ldr	x19,[x19]
	add	x19,x19,PAYLOAD
	cbz	x3,.hdr
	add	x0,x19,8
	mov	x1,x3
	mov	x2,x23
	bl	net_copy
.hdr:	st16be	w20,x19,0
	st16be	w22,x19,2
	add	w24,w23,8
	st16be	w24,x19,4
	strh	wzr,[x19,6]
	mov	w0,w21
	bl	ip_src
	mov	w1,w21
	mov	w2,PROTO_UDP
	mov	w3,w24
	bl	csum_pseudo
	mov	x2,x0
	mov	x0,x19
	mov	x1,x24
	bl	csum_add
	bl	csum_fold
	cbnz	w0,.store
	mov	w0,0xFFFF
.store:	st16be	w0,x19,6
	mov	w0,w21
	mov	w1,PROTO_UDP
	mov	x2,x24
	bl	ip_output
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- DHCP ---------------------------------------------------------------------

; Minuterie (sous net_lock)
dhcp_timer:
	prologue
	adr	x0,netdev_up
	ldr	w0,[x0]
	cbz	w0,.out
	adr	x0,dhcp_state
	ldr	w0,[x0]
	cmp	w0,DHCP_INIT
	b.lo	.out
	cmp	w0,DHCP_BOUND
	b.hs	.out
	bl	time_ms
	adr	x1,dhcp_next_ms
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.out
	adr	x3,dhcp_tries			; nouvel essai dans 0,5 s, 1 s, puis
	ldr	x4,[x3]				; toutes les 2 s : la premiere demande
	add	x5,x4,1				; part souvent avant que le lien Wi-Fi
	str	x5,[x3]				; passe les trames (docs/BOOT.md)
	mov	x5,500
	lsl	x5,x5,x4
	cmp	x4,2
	mov	x6,2000
	csel	x5,x5,x6,lo
	add	x0,x0,x5
	str	x0,[x1]
	cbnz	x4,.xid				; (le meme pour les renvois : une offre
	bl	time_ms				; lente reste bonne) identifiant de
	adr	x1,dhcp_xid			; transaction
	movl	w2,0x41524D00			; "ARM."
	eor	w0,w0,w2
	str	w0,[x1]
.xid:	mov	w0,1				; DISCOVER
	bl	dhcp_send
	adr	x0,dhcp_state
	mov	w1,DHCP_SELECTING
	str	w1,[x0]
.out:	epilogue

; w0 = type de message (1 DISCOVER, 3 REQUEST)
dhcp_send:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w20,w0
	adr	x19,net_txbuf
	ldr	x19,[x19]
	add	x19,x19,PAYLOAD+8
	mov	x0,x19
	mov	x1,304
	bl	mem_zero
	mov	w0,0x01				; op BOOTREQUEST, htype 1, hlen 6, hops 0
	strb	w0,[x19]
	strb	w0,[x19,1]
	mov	w0,6
	strb	w0,[x19,2]
	adr	x0,dhcp_xid
	ldr	w0,[x0]
	st32be	w0,x19,4
	mov	w0,0x8000			; reponse en diffusion
	st16be	w0,x19,10
	add	x0,x19,28			; chaddr
	adr	x1,netdev_mac
	mov	x2,6
	bl	net_copy
	add	x0,x19,236			; options
	movl	w1,0x63538263			; cookie magique 63 82 53 63
	str	w1,[x0],4
	mov	w1,53				; type de message
	strb	w1,[x0],1
	mov	w1,1
	strb	w1,[x0],1
	strb	w20,[x0],1
	cmp	w20,3
	b.ne	.params
	mov	w1,50				; adresse demandee
	strb	w1,[x0],1
	mov	w1,4
	strb	w1,[x0],1
	adr	x1,dhcp_offer_ip
	ldr	w1,[x1]
	rev	w1,w1
	str	w1,[x0],4
	mov	w1,54				; serveur
	strb	w1,[x0],1
	mov	w1,4
	strb	w1,[x0],1
	adr	x1,dhcp_server
	ldr	w1,[x1]
	rev	w1,w1
	str	w1,[x0],4
.params:
	mov	w1,55				; parametres voulus : masque, routeur, DNS
	strb	w1,[x0],1
	mov	w1,3
	strb	w1,[x0],1
	mov	w1,1
	strb	w1,[x0],1
	mov	w1,3
	strb	w1,[x0],1
	mov	w1,6
	strb	w1,[x0],1
	mov	w1,255
	strb	w1,[x0],1
	mov	w0,68
	mov	w1,-1
	mov	w2,67
	mov	x3,0
	mov	x4,300
	bl	udp_output
	ldp	x19,x20,[sp],16
	epilogue

; x0 = message DHCP, x1 = longueur
dhcp_input:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x20,240
	b.lo	.out
	ldrb	w0,[x19]
	cmp	w0,2				; BOOTREPLY
	b.ne	.out
	ld32be	w0,x19,4
	adr	x1,dhcp_xid
	ldr	w1,[x1]
	cmp	w0,w1
	b.ne	.out
	ld32be	w21,x19,16			; yiaddr
	; options
	mov	w22,0				; type
	mov	w23,0				; masque
	mov	w24,0				; routeur
	mov	x9,0				; DNS
	mov	x10,0				; serveur
	add	x2,x19,240
	add	x3,x19,x20
.opt:	cmp	x2,x3
	b.hs	.parsed
	ldrb	w4,[x2]
	cmp	w4,255
	b.eq	.parsed
	cbz	w4,.pad
	ldrb	w5,[x2,1]
	add	x6,x2,2
	cmp	w4,53
	b.ne	.o1
	ldrb	w22,[x6]
	b	.onext
.o1:	cmp	w4,1
	b.ne	.o3
	ldr	w23,[x6]
	rev	w23,w23
	b	.onext
.o3:	cmp	w4,3
	b.ne	.o6
	ldr	w24,[x6]
	rev	w24,w24
	b	.onext
.o6:	cmp	w4,6
	b.ne	.o54
	ldr	w9,[x6]
	rev	w9,w9
	b	.onext
.o54:	cmp	w4,54
	b.ne	.onext
	ldr	w10,[x6]
	rev	w10,w10
.onext:	add	x2,x6,x5
	b	.opt
.pad:	add	x2,x2,1
	b	.opt
.parsed:
	adr	x0,dhcp_state
	ldr	w1,[x0]
	cmp	w22,2				; OFFER
	b.ne	.ack
	cmp	w1,DHCP_SELECTING
	b.ne	.out
	adr	x1,dhcp_offer_ip
	str	w21,[x1]
	adr	x1,dhcp_server
	str	w10,[x1]
	mov	w1,DHCP_REQUESTING
	str	w1,[x0]
	bl	time_ms				; l'accuse a 2 s pour venir avant
	add	x0,x0,2000			; qu'un renvoi ne reparte de zero
	adr	x1,dhcp_next_ms
	str	x0,[x1]
	mov	w0,3
	bl	dhcp_send
	b	.out
.ack:	cmp	w22,5
	b.ne	.nak
	cmp	w1,DHCP_REQUESTING
	b.ne	.out
	mov	w1,DHCP_BOUND
	str	w1,[x0]
	adr	x0,net_ip
	str	w21,[x0]
	adr	x0,net_mask
	str	w23,[x0]
	adr	x0,net_gw
	str	w24,[x0]
	adr	x0,net_dns
	str	w9,[x0]
	adr	x0,net_msg_dhcp
	bl	kputs
	mov	w0,w21
	bl	kput_ip
	adr	x0,net_msg_gw
	bl	kputs
	adr	x0,net_gw
	ldr	w0,[x0]
	bl	kput_ip
	adr	x0,net_msg_dns
	bl	kputs
	adr	x0,net_dns
	ldr	w0,[x0]
	bl	kput_ip
	mov	w0,10
	bl	kputc
	b	.out
.nak:	cmp	w22,6
	b.ne	.out
	mov	w1,DHCP_INIT			; NAK : on recommence
	str	w1,[x0]
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ============================================================================
;  Sockets : outils
; ============================================================================

; -> x0 = socket libre (mis a zero, USED=1), 0 si table pleine
sock_alloc:
	adr	x0,sock_table
	ldr	x0,[x0]
	mov	x1,MAX_SOCK
.f:	ldr	x2,[x0,SK_USED]
	cbz	x2,.got
	add	x0,x0,SK_SIZE
	subs	x1,x1,1
	b.ne	.f
	mov	x0,0
	ret
.got:	mov	x1,0
.z:	str	xzr,[x0,x1]
	add	x1,x1,8
	cmp	x1,SK_SIZE
	b.lo	.z
	mov	x1,1
	str	x1,[x0,SK_USED]
	ret

; x0 = socket, x1 = 1 : aussi un tampon d'emission -> x0 = 1 si ok
sk_alloc_rings:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,SK_RING_PAGES
	bl	pmm_alloc_n
	cbz	x0,.fail
	str	x0,[x19,SK_RXBUF]
	cbz	x20,.ok
	mov	x0,SK_RING_PAGES
	bl	pmm_alloc_n
	cbz	x0,.fail
	str	x0,[x19,SK_TXBUF]
.ok:	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = socket : libere ses tampons et son emplacement
sock_free:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,SK_RXBUF]
	cbz	x0,.tx
	mov	x1,SK_RING_PAGES
	bl	pmm_free_n
.tx:	ldr	x0,[x19,SK_TXBUF]
	cbz	x0,.z
	mov	x1,SK_RING_PAGES
	bl	pmm_free_n
.z:	str	xzr,[x19,SK_RXBUF]
	str	xzr,[x19,SK_TXBUF]
	str	xzr,[x19,SK_USED]
	ldr	x19,[sp],16
	epilogue

; w0 = type, w1 = port local -> x0 = socket (UDP, ou TCP en ecoute), 0 sinon
sock_find_port:
	adr	x2,sock_table
	ldr	x2,[x2]
	mov	x3,MAX_SOCK
.f:	ldr	x4,[x2,SK_USED]
	cbz	x4,.n
	ldr	x4,[x2,SK_TYPE]
	cmp	w4,w0
	b.ne	.n
	ldr	x4,[x2,SK_LPORT]
	cmp	w4,w1
	b.ne	.n
	cmp	w0,SOCK_TCP
	b.ne	.got
	ldr	x4,[x2,SK_STATE]
	cmp	x4,TCP_LISTEN
	b.eq	.got
.n:	add	x2,x2,SK_SIZE
	subs	x3,x3,1
	b.ne	.f
	mov	x0,0
	ret
.got:	mov	x0,x2
	ret

; -> w0 = port local ephemere inutilise
sock_ephemeral:
	adr	x1,net_next_port
	ldr	w0,[x1]
	add	w2,w0,1
	movl	w3,0x3FFF
	and	w2,w2,w3
	str	w2,[x1]
	movl	w1,EPHEMERAL
	add	w0,w0,w1
	ret

; x0 = socket, x1 = source, x2 = octets : ecrit dans l'anneau de reception
sk_rx_write:
	ldr	x3,[x0,SK_RXBUF]
	ldr	x4,[x0,SK_RXHEAD]
	cbz	x2,.e
.c:	ldrb	w5,[x1],1
	and	x6,x4,SK_RING-1
	strb	w5,[x3,x6]
	add	x4,x4,1
	subs	x2,x2,1
	b.ne	.c
.e:	str	x4,[x0,SK_RXHEAD]
	ret

; x0 = socket, x1 = destination, x2 = octets : lit et consomme l'anneau de reception
sk_rx_read:
	ldr	x3,[x0,SK_RXBUF]
	ldr	x4,[x0,SK_RXTAIL]
	cbz	x2,.e
.c:	and	x6,x4,SK_RING-1
	ldrb	w5,[x3,x6]
	strb	w5,[x1],1
	add	x4,x4,1
	subs	x2,x2,1
	b.ne	.c
.e:	str	x4,[x0,SK_RXTAIL]
	ret

; x0 = socket -> x0 = place libre dans l'anneau de reception
sk_rx_free:
	ldr	x1,[x0,SK_RXHEAD]
	ldr	x2,[x0,SK_RXTAIL]
	sub	x1,x1,x2
	mov	x0,SK_RING
	sub	x0,x0,x1
	ret

; ============================================================================
;  TCP
; ============================================================================

; x0 = socket, w1 = drapeaux, w2 = sequence, x3 = octets de donnees a prendre
; dans l'anneau d'emission a partir de SK_TXTAIL + (sequence - SND_UNA)
; -> x0 = 1 si envoye
tcp_send:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	mov	w21,w2
	mov	x22,x3
	tst	w20,TH_ACK			; tout segment porte l'accuse
	b.eq	.nacc
	str	xzr,[x19,SK_ACK_CNT]
	str	xzr,[x19,SK_ACK_AT]
.nacc:	adr	x23,net_txbuf
	ldr	x23,[x23]
	add	x23,x23,PAYLOAD			; en-tete TCP
	mov	x24,20				; longueur de l'en-tete
	tst	w20,TH_SYN
	b.eq	.hdr
	mov	x24,24				; option MSS
	movl	w0,0x020405B4			; kind 2, len 4, 1460
	rev	w0,w0
	str	w0,[x23,20]
	tst	w20,TH_ACK			; window scale : toujours dans notre
	b.eq	.ws				; SYN, dans le SYN-ACK si le pair l'a
	ldr	x0,[x19,SK_WS_OK]
	cbz	x0,.hdr
.ws:	mov	x24,28				; NOP, kind 3, len 3, decalage
	movl	w0,0x01 or (3 shl 8) or (3 shl 16) or (TCP_WS_SHIFT shl 24)
	str	w0,[x23,24]
.hdr:	ldr	x0,[x19,SK_LPORT]
	st16be	w0,x23,0
	ldr	x0,[x19,SK_RPORT]
	st16be	w0,x23,2
	st32be	w21,x23,4
	mov	w0,0				; acquittement
	tst	w20,TH_ACK
	b.eq	.noack
	ldr	x0,[x19,SK_RCV_NXT]
.noack:	st32be	w0,x23,8
	lsl	w0,w24,2			; (longueur/4) << 4 dans l'octet 12
	strb	w0,[x23,12]
	strb	w20,[x23,13]
	ldr	x0,[x19,SK_RXBUF]		; fenetre = place libre en reception
	mov	x1,0
	cbz	x0,.win
	mov	x0,x19
	bl	sk_rx_free
	tst	w20,TH_SYN			; (jamais decalee dans un SYN)
	b.ne	.clamp
	ldr	x1,[x19,SK_RCV_SHIFT]
	lsr	x0,x0,x1
.clamp:	movl	x1,0xFFFF
	cmp	x0,x1
	csel	x1,x0,x1,lo
.win:	st16be	w1,x23,14
	strh	wzr,[x23,16]			; somme de controle
	strh	wzr,[x23,18]
	; donnees depuis l'anneau d'emission
	cbz	x22,.sum
	ldr	x0,[x19,SK_SND_UNA]
	sub	w0,w21,w0			; ecart depuis SND_UNA
	ldr	x1,[x19,SK_TXTAIL]
	add	x1,x1,x0
	ldr	x2,[x19,SK_TXBUF]
	add	x3,x23,x24
	mov	x4,x22
.cp:	and	x5,x1,SK_RING-1
	ldrb	w6,[x2,x5]
	strb	w6,[x3],1
	add	x1,x1,1
	subs	x4,x4,1
	b.ne	.cp
.sum:	ldr	x0,[x19,SK_RIP]
	bl	ip_src
	ldr	x1,[x19,SK_RIP]
	mov	w2,PROTO_TCP
	add	x3,x24,x22
	bl	csum_pseudo
	mov	x2,x0
	mov	x0,x23
	add	x1,x24,x22
	bl	csum_add
	bl	csum_fold
	st16be	w0,x23,16
	ldr	x0,[x19,SK_RIP]
	mov	w1,PROTO_TCP
	add	x2,x24,x22
	bl	ip_output
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = socket : ACK seul
tcp_send_ack:
	mov	w1,TH_ACK
	ldr	x2,[x0,SK_SND_NXT]
	mov	x3,0
	b	tcp_send

; Reponse RST a un segment sans socket : w0 = IP distante, w1 = port distant,
; w2 = port local, w3 = sequence, w4 = acquittement, w5 = drapeaux (RST|ACK...)
tcp_send_rst:
	prologue
	sub	sp,sp,SK_SIZE			; socket temporaire sur la pile
	stp	x19,x20,[sp,-16]!
	add	x19,sp,16
	str	x0,[x19,SK_RIP]
	str	x1,[x19,SK_RPORT]
	str	x2,[x19,SK_LPORT]
	str	x4,[x19,SK_RCV_NXT]
	str	xzr,[x19,SK_RXBUF]
	mov	x0,x19
	mov	w1,w5
	mov	w2,w3
	mov	x3,0
	bl	tcp_send
	ldp	x19,x20,[sp],16
	add	sp,sp,SK_SIZE
	epilogue

; w0 = graine -> w0 = numero de sequence initial
tcp_iss:
	mrs	x1,cntpct_el0
	eor	x0,x0,x1
	movl	w1,0x9E3779B1
	mul	w0,w0,w1
	ret

; x0 = segment, x1 = longueur, w2 = IP source, w3 = IP destination (net_lock)
tcp_input:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0				; segment
	mov	x20,x1				; longueur
	mov	w21,w2				; IP source
	cmp	x20,20
	b.lo	.out
	mov	w0,w2				; somme de controle
	mov	w1,w3
	mov	w2,PROTO_TCP
	mov	w3,w20
	bl	csum_pseudo
	mov	x2,x0
	mov	x0,x19
	mov	x1,x20
	bl	csum_add
	bl	csum_fold
	cbnz	w0,.out
	ldrb	w0,[x19,12]
	lsr	w0,w0,4
	lsl	w22,w0,2			; longueur de l'en-tete
	cmp	w22,20
	b.lo	.out
	cmp	x22,x20
	b.hi	.out
	ldrb	w23,[x19,13]			; drapeaux
	ld32be	w24,x19,4			; sequence
	ld32be	w25,x19,8			; acquittement
	add	x26,x19,x22			; donnees
	sub	x27,x20,x22			; longueur des donnees
	ld16be	w0,x19,0			; port distant
	ld16be	w1,x19,2			; port local
	stp	x0,x1,[sp,-16]!			; [sp] = port distant, [sp+8] = port local

	; --- recherche de la connexion -------------------------------------------
	adr	x28,sock_table
	ldr	x28,[x28]
	mov	x2,MAX_SOCK
.find:	ldr	x3,[x28,SK_USED]
	cbz	x3,.fnext
	ldr	x3,[x28,SK_TYPE]
	cmp	x3,SOCK_TCP
	b.ne	.fnext
	ldr	x3,[x28,SK_STATE]
	cmp	x3,TCP_SYN_SENT
	b.lo	.fnext
	ldr	x3,[x28,SK_LPORT]
	cmp	w3,w1
	b.ne	.fnext
	ldr	x3,[x28,SK_RPORT]
	cmp	w3,w0
	b.ne	.fnext
	ldr	x3,[x28,SK_RIP]
	cmp	w3,w21
	b.eq	.found
.fnext:	add	x28,x28,SK_SIZE
	subs	x2,x2,1
	b.ne	.find
	mov	w0,SOCK_TCP			; socket en ecoute ?
	bl	sock_find_port
	cbz	x0,.no_socket
	mov	x28,x0
	b	.listen

.no_socket:					; RST (sauf a un RST)
	tst	w23,TH_RST
	b.ne	.drop
	ldp	x1,x2,[sp]
	mov	w0,w21
	tst	w23,TH_ACK
	b.eq	.rst_noack
	mov	w3,w25				; seq = leur ACK
	mov	w4,0
	mov	w5,TH_RST
	bl	tcp_send_rst
	b	.drop
.rst_noack:
	mov	w3,0
	add	w4,w24,w27			; ack = seq + longueur (+1 si SYN/FIN)
	tst	w23,TH_SYN
	cinc	w4,w4,ne
	tst	w23,TH_FIN
	cinc	w4,w4,ne
	mov	w5,TH_RST or TH_ACK
	bl	tcp_send_rst
	b	.drop

	; --- LISTEN : nouvelle connexion entrante --------------------------------
.listen:
	tst	w23,TH_RST
	b.ne	.drop
	tst	w23,TH_ACK
	b.ne	.no_socket
	tst	w23,TH_SYN
	b.eq	.drop
	mov	x0,x28				; place dans la file d'attente ?
	bl	tcp_pending_count
	ldr	x1,[x28,SK_BACKLOG]
	cmp	x0,x1
	b.hs	.drop
	bl	sock_alloc
	cbz	x0,.drop
	mov	x2,x0
	stp	x2,xzr,[sp,-16]!
	mov	x1,1
	bl	sk_alloc_rings
	ldp	x2,x3,[sp],16
	cbnz	x0,.child_ok
	mov	x0,x2
	bl	sock_free
	b	.drop
.child_ok:
	mov	x0,x2
	mov	x1,SOCK_TCP
	str	x1,[x0,SK_TYPE]
	mov	x1,TCP_SYN_RCVD
	str	x1,[x0,SK_STATE]
	ldr	x1,[x28,SK_LPORT]
	str	x1,[x0,SK_LPORT]
	ldr	x1,[sp]
	str	x1,[x0,SK_RPORT]
	str	x21,[x0,SK_RIP]
	str	x28,[x0,SK_PARENT]
	add	w1,w24,1
	str	x1,[x0,SK_RCV_NXT]
	mov	x28,x0
	mov	w0,w24
	bl	tcp_iss
	str	x0,[x28,SK_ISS]
	str	x0,[x28,SK_SND_UNA]
	add	w1,w0,1
	str	x1,[x28,SK_SND_NXT]
	mov	x0,x28
	bl	tcp_parse_mss
	ld16be	w0,x19,14
	str	x0,[x28,SK_SND_WND]
	mov	x0,1000
	str	x0,[x28,SK_RTO]
	mov	x0,x28				; SYN-ACK
	mov	w1,TH_SYN or TH_ACK
	ldr	x2,[x28,SK_ISS]
	mov	x3,0
	bl	tcp_send
	mov	x0,x28
	bl	tcp_arm_rtx
	b	.drop

	; --- connexion existante ---------------------------------------------------
.found:	ldr	x0,[x28,SK_STATE]
	cmp	x0,TCP_SYN_SENT
	b.ne	.synced
	tst	w23,TH_ACK			; SYN-SENT
	b.eq	.ss_noack
	ldr	x1,[x28,SK_SND_NXT]
	cmp	w25,w1
	b.eq	.ss_ackok
	tst	w23,TH_RST
	b.ne	.drop
	b	.no_socket			; ACK inacceptable : RST
.ss_ackok:
	tst	w23,TH_RST
	b.eq	.ss_syn
	ldr	x0,[x28,SK_FLAGS]		; connexion refusee
	orr	x0,x0,1 shl SKF_REFUSED
	str	x0,[x28,SK_FLAGS]
	str	xzr,[x28,SK_STATE]
	str	xzr,[x28,SK_RTX_AT]
	b	.drop
.ss_noack:
	tst	w23,TH_RST
	b.ne	.drop
.ss_syn:
	tst	w23,TH_SYN
	b.eq	.drop
	add	w0,w24,1
	str	x0,[x28,SK_RCV_NXT]
	mov	x0,x28
	bl	tcp_parse_mss
	ld16be	w0,x19,14
	str	x0,[x28,SK_SND_WND]
	tst	w23,TH_ACK
	b.eq	.drop				; ouverture simultanee : non geree
	str	x25,[x28,SK_SND_UNA]
	mov	x0,TCP_ESTAB
	str	x0,[x28,SK_STATE]
	str	xzr,[x28,SK_RTX_AT]
	str	xzr,[x28,SK_RETRIES]
	mov	x0,x28
	bl	tcp_send_ack
	b	.drop

.synced:
	tst	w23,TH_RST			; RST : connexion reinitialisee
	b.eq	.no_rst
	ldr	x1,[x28,SK_RCV_NXT]
	sub	w1,w24,w1
	movl	w2,0x10000
	cmp	w1,w2				; numero dans la fenetre (approximation)
	b.hs	.drop
	ldr	x0,[x28,SK_STATE]
	cmp	x0,TCP_SYN_RCVD
	b.ne	.rst_conn
	mov	x0,x28				; fils pas encore accepte : supprime
	bl	sock_free
	b	.drop
.rst_conn:
	ldr	x0,[x28,SK_FLAGS]
	orr	x0,x0,1 shl SKF_RESET
	str	x0,[x28,SK_FLAGS]
	str	xzr,[x28,SK_STATE]
	str	xzr,[x28,SK_RTX_AT]
	mov	x0,x28
	bl	tcp_maybe_free
	b	.drop
.no_rst:
	tst	w23,TH_SYN			; SYN dans une connexion etablie : ignore
	b.ne	.ack_only
	tst	w23,TH_ACK
	b.eq	.drop

	; --- traitement de l'acquittement ---------------------------------------
	ldr	x0,[x28,SK_STATE]
	cmp	x0,TCP_SYN_RCVD
	b.ne	.ack_proc
	ldr	x1,[x28,SK_SND_NXT]
	cmp	w25,w1
	b.ne	.no_socket
	str	x25,[x28,SK_SND_UNA]
	mov	x0,TCP_ESTAB
	str	x0,[x28,SK_STATE]
	str	xzr,[x28,SK_RTX_AT]
	str	xzr,[x28,SK_RETRIES]
	ld16be	w0,x19,14
	ldr	x1,[x28,SK_SND_SHIFT]
	lsl	x0,x0,x1
	str	x0,[x28,SK_SND_WND]
	b	.data
.ack_proc:
	ldr	x1,[x28,SK_SND_UNA]
	ldr	x2,[x28,SK_SND_NXT]
	sub	w3,w25,w1			; acquitte = ack - SND_UNA
	sub	w4,w2,w1			; en vol
	cbz	w3,.same
	cmp	w3,w4
	b.hi	.ack_only			; acquitte des donnees jamais envoyees
	tbnz	w3,31,.win			; ancien ACK
	mov	w5,w3				; octets de donnees acquittes
	ldr	x6,[x28,SK_FLAGS]
	tbz	x6,SKF_FIN_SENT,.ack_data
	ldr	x7,[x28,SK_FIN_SEQ]
	add	w7,w7,1
	cmp	w25,w7
	b.ne	.ack_data
	sub	w5,w5,1				; notre FIN est acquitte
	orr	x6,x6,1 shl 16			; (marqueur temporaire)
.ack_data:
	ldr	x7,[x28,SK_TXTAIL]
	add	x7,x7,x5
	str	x7,[x28,SK_TXTAIL]
	str	x25,[x28,SK_SND_UNA]
	str	xzr,[x28,SK_RETRIES]
	stp	x5,x6,[sp,-16]!
	mov	x0,x28
	mov	x1,x5
	mov	w2,w25
	bl	tcp_cc_ack
	ldp	x5,x6,[sp],16
	; mesure du RTT (Karn : seulement si pas de retransmission)
	ldr	x7,[x28,SK_RTT_SEQ]
	cbz	x7,.no_rtt
	sub	w8,w25,w7
	tbnz	w8,31,.no_rtt
	cbz	w8,.no_rtt
	str	xzr,[x28,SK_RTT_SEQ]
	stp	x6,xzr,[sp,-16]!
	bl	time_ms
	ldp	x6,x7,[sp],16
	ldr	x8,[x28,SK_RTT_START]
	sub	x0,x0,x8			; RTT mesure
	ldr	x8,[x28,SK_SRTT]
	cbz	x8,.first_rtt
	mov	x9,7
	mul	x8,x8,x9
	add	x8,x8,x0
	lsr	x0,x8,3
.first_rtt:
	str	x0,[x28,SK_SRTT]
	lsl	x0,x0,1				; RTO = 2 x SRTT + 50, borne 1000..16000
	add	x0,x0,50			; (200 ms : fausses expirations avec les
	cmp	x0,TCP_RTO_MIN			;  accuses retardes de Windows)
	mov	x8,TCP_RTO_MIN
	csel	x0,x8,x0,lo
	movl	x8,16000
	cmp	x0,x8
	csel	x0,x8,x0,hi
	str	x0,[x28,SK_RTO]
.no_rtt:
	ldr	x0,[x28,SK_SND_UNA]
	ldr	x1,[x28,SK_SND_NXT]
	cmp	w0,w1
	b.ne	.rearm
	str	xzr,[x28,SK_RTX_AT]
	b	.fin_acked
.rearm:	mov	x0,x28
	bl	tcp_arm_rtx_force
.fin_acked:
	tbz	x6,16,.win
	ldr	x0,[x28,SK_STATE]		; transitions sur l'acquittement du FIN
	cmp	x0,TCP_FIN_WAIT1
	b.ne	.fa2
	mov	x0,TCP_FIN_WAIT2
	str	x0,[x28,SK_STATE]
	bl	time_ms				; orphelin : 10 s maximum en FIN-WAIT-2
	movl	x16,10000
	add	x0,x0,x16
	str	x0,[x28,SK_DEADLINE]
	b	.win
.fa2:	cmp	x0,TCP_CLOSING
	b.ne	.fa3
	mov	x0,x28
	bl	tcp_enter_time_wait
	b	.win
.fa3:	cmp	x0,TCP_LAST_ACK
	b.ne	.win
	str	xzr,[x28,SK_STATE]
	mov	x0,x28
	bl	tcp_maybe_free
	b	.drop
	b	.win
.same:	cbnz	x27,.win			; accuse duplique (RFC 5681) : sans
	tst	w23,TH_SYN or TH_FIN		; donnees, ni SYN, ni FIN, meme fenetre,
	b.ne	.win				; des donnees en vol
	cbz	w4,.win
	ld16be	w0,x19,14
	ldr	x1,[x28,SK_SND_SHIFT]
	lsl	x0,x0,x1
	ldr	x1,[x28,SK_SND_WND]
	cmp	x0,x1
	b.ne	.win
	mov	x0,x28
	bl	tcp_cc_dup
.win:	ld16be	w0,x19,14
	ldr	x1,[x28,SK_SND_SHIFT]
	lsl	x0,x0,x1
	str	x0,[x28,SK_SND_WND]

	; --- donnees -------------------------------------------------------------
.data:	ldr	x0,[x28,SK_STATE]
	cmp	x0,TCP_ESTAB
	b.lo	.output
	cmp	x0,TCP_FIN_WAIT2
	b.hi	.fin				; plus de donnees acceptees apres leur FIN
	cbz	x27,.fin
	ldr	x1,[x28,SK_RCV_NXT]
	sub	w2,w1,w24			; deja recu en tete du segment ?
	tbnz	w2,31,.ooo			; segment en avance : hors ordre
	cmp	x2,x27
	b.hs	.dup
	add	x26,x26,x2
	sub	x27,x27,x2
	add	w24,w24,w2
	mov	x0,x28
	bl	sk_rx_free
	cmp	x27,x0
	csel	x3,x27,x0,ls			; ce qui tient dans l'anneau
	cbz	x3,.ack_now
	mov	x0,x28
	mov	x1,x26
	mov	x2,x3
	stp	x3,xzr,[sp,-16]!
	bl	sk_rx_write
	ldp	x3,x4,[sp],16
	str	x3,[x28,SK_RX_LEN]
	ldr	x1,[x28,SK_RCV_NXT]
	add	w1,w1,w3
	str	x1,[x28,SK_RCV_NXT]
	add	w24,w24,w3
	sub	x27,x27,x3
	mov	x0,x28				; plages hors ordre devenues contigues
	bl	tcp_ooo_merge
	ldr	x1,[x28,SK_RCV_NXT]		; (le FIN eventuel suit la derniere plage)
	cbnz	x27,.ack_now
	mov	w24,w1
	ldr	x0,[x28,SK_RX_LEN]		; accuse retarde : segment plein, en
	cmp	x0,TCP_ACK_FULL			; ordre, sans PSH ni FIN, aucun trou
	b.lo	.ack_now
	tst	w23,TH_PSH
	b.ne	.ack_now
	tst	w23,TH_FIN
	b.ne	.ack_now
	ldr	x0,[x28,SK_OOO_N]
	cbnz	x0,.ack_now
	ldr	x0,[x28,SK_ACK_CNT]
	add	x0,x0,1
	cmp	x0,TCP_ACK_EVERY
	b.hs	.ack_now
	str	x0,[x28,SK_ACK_CNT]
	ldr	x0,[x28,SK_ACK_AT]
	cbnz	x0,.fin
	bl	time_ms
	add	x0,x0,TCP_ACK_MS
	str	x0,[x28,SK_ACK_AT]
	b	.fin
.dup:	mov	x27,0
	add	w24,w24,w2
	b	.ack_now
.ooo:	neg	w2,w2				; hors ordre : rangement a sa place si la
	mov	x0,x28				; fenetre le permet, puis ACK duplique
	mov	x1,x2
	mov	x2,x26
	mov	x3,x27
	mov	w4,w24
	bl	tcp_ooo_store
	mov	x0,x28
	bl	tcp_send_ack
	b	.output
.ack_now:
	mov	x0,1
	str	x0,[x28,SK_ACKNOW]

	; --- FIN du pair ---------------------------------------------------------
.fin:	tst	w23,TH_FIN
	b.eq	.send_ack
	cbnz	x27,.send_ack			; donnees non acceptees : FIN plus tard
	ldr	x1,[x28,SK_RCV_NXT]
	cmp	w24,w1
	b.ne	.send_ack
	ldr	x0,[x28,SK_FLAGS]
	tbnz	x0,SKF_RX_FIN,.fin_dup
	orr	x0,x0,1 shl SKF_RX_FIN
	str	x0,[x28,SK_FLAGS]
	add	w1,w1,1
	str	x1,[x28,SK_RCV_NXT]
	ldr	x0,[x28,SK_STATE]
	cmp	x0,TCP_ESTAB
	b.ne	.f1
	mov	x0,TCP_CLOSE_WAIT
	str	x0,[x28,SK_STATE]
	b	.fin_ack
.f1:	cmp	x0,TCP_FIN_WAIT1
	b.ne	.f2
	mov	x0,TCP_CLOSING
	str	x0,[x28,SK_STATE]
	b	.fin_ack
.f2:	cmp	x0,TCP_FIN_WAIT2
	b.ne	.fin_ack
	mov	x0,x28
	bl	tcp_enter_time_wait
.fin_ack:
.fin_dup:
	mov	x0,1
	str	x0,[x28,SK_ACKNOW]
.send_ack:
	ldr	x0,[x28,SK_ACKNOW]
	cbz	x0,.output
	str	xzr,[x28,SK_ACKNOW]
	mov	x0,x28
	bl	tcp_send_ack
.output:
	mov	x0,x28
	bl	tcp_output
	b	.drop
.ack_only:
	mov	x0,x28
	bl	tcp_send_ack
.drop:	add	sp,sp,16
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = socket, x1 = ecart (sequence - RCV_NXT > 0), x2 = donnees, x3 = longueur,
; w4 = sequence : ecrit les donnees a leur place dans l'anneau (sans avancer
; RXHEAD) et memorise la plage
tcp_ooo_store:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	cbz	x3,.out
	add	x5,x1,x3			; doit tenir dans la place libre
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	str	x5,[sp,-16]!
	bl	sk_rx_free
	ldr	x5,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	cmp	x5,x0
	b.hi	.out
	ldr	x6,[x19,SK_RXBUF]		; copie a RXHEAD + ecart
	ldr	x7,[x19,SK_RXHEAD]
	add	x7,x7,x1
	mov	x8,x3
.cp:	ldrb	w9,[x2],1
	and	x10,x7,SK_RING-1
	strb	w9,[x6,x10]
	add	x7,x7,1
	subs	x8,x8,1
	b.ne	.cp
	add	w5,w4,w3			; plage [w4, w5[
	ldr	x20,[x19,SK_OOO_N]		; fusion avec une plage qui touche
	add	x21,x19,SK_OOO
	mov	x22,0
.m:	cmp	x22,x20
	b.hs	.new
	add	x9,x21,x22,lsl 3
	ldr	w10,[x9]			; debut
	ldr	w11,[x9,4]			; fin
	sub	w12,w5,w10			; nouvelle fin < debut existant ?
	tbnz	w12,31,.mn
	sub	w12,w11,w4			; fin existante < nouveau debut ?
	tbnz	w12,31,.mn
	sub	w12,w4,w10			; union
	tbz	w12,31,.s_ok
	str	w4,[x9]
.s_ok:	sub	w12,w5,w11
	tbnz	w12,31,.out
	str	w5,[x9,4]
	b	.out
.mn:	add	x22,x22,1
	b	.m
.new:	net_stat 2
	cmp	x20,OOO_MAX
	b.lo	.room
	net_stat 3				; plus de place : donnees non suivies
	b	.out
.room:
	add	x9,x21,x20,lsl 3
	str	w4,[x9]
	str	w5,[x9,4]
	add	x20,x20,1
	str	x20,[x19,SK_OOO_N]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = socket : integre les plages hors ordre que RCV_NXT a rejointes
tcp_ooo_merge:
.again:	ldr	x1,[x0,SK_OOO_N]
	cbz	x1,.done
	add	x2,x0,SK_OOO
	mov	x3,0
.r:	cmp	x3,x1
	b.hs	.done
	add	x4,x2,x3,lsl 3
	ldr	w5,[x4]
	ldr	w6,[x4,4]
	ldr	x7,[x0,SK_RCV_NXT]
	sub	w8,w5,w7			; debut <= RCV_NXT ?
	cmp	w8,0
	b.gt	.rn
	sub	w8,w6,w7			; fin > RCV_NXT : avancer
	cmp	w8,0
	b.le	.drop
	ldr	x9,[x0,SK_RXHEAD]
	add	x9,x9,x8
	str	x9,[x0,SK_RXHEAD]
	str	x6,[x0,SK_RCV_NXT]
.drop:	sub	x1,x1,1				; retirer la plage (la derniere prend sa place)
	str	x1,[x0,SK_OOO_N]
	add	x9,x2,x1,lsl 3
	ldr	x10,[x9]
	str	x10,[x4]
	b	.again
.rn:	add	x3,x3,1
	b	.r
.done:	ret

; x0 = socket, x19 = segment (en-tete TCP) : lit l'option MSS (defaut 536)
tcp_parse_mss:
	str	xzr,[x0,SK_WS_OK]
	str	xzr,[x0,SK_SND_SHIFT]
	str	xzr,[x0,SK_RCV_SHIFT]
	mov	x1,536
	ldrb	w2,[x19,12]
	lsr	w2,w2,4
	lsl	w2,w2,2				; longueur de l'en-tete
	add	x3,x19,20
	add	x4,x19,x2
.o:	cmp	x3,x4
	b.hs	.e
	ldrb	w5,[x3]
	cbz	w5,.e
	cmp	w5,1
	b.ne	.kind
	add	x3,x3,1
	b	.o
.kind:	ldrb	w6,[x3,1]
	cmp	w6,2
	b.lo	.e
	cmp	w5,3
	b.ne	.mss
	ldrb	w7,[x3,2]			; window scale du pair (14 au plus)
	cmp	w7,14
	mov	w8,14
	csel	w7,w8,w7,hi
	str	x7,[x0,SK_SND_SHIFT]
	mov	x7,1
	str	x7,[x0,SK_WS_OK]
	mov	x7,TCP_WS_SHIFT
	str	x7,[x0,SK_RCV_SHIFT]
	b	.skip
.mss:	cmp	w5,2
	b.ne	.skip
	ldrb	w7,[x3,2]
	ldrb	w8,[x3,3]
	orr	w1,w8,w7,lsl 8
.skip:	add	x3,x3,x6
	b	.o
.e:	movl	x2,TCP_MSS
	cmp	x1,x2
	csel	x1,x2,x1,hi
	cmp	x1,64
	mov	x2,64
	csel	x1,x2,x1,lo
	str	x1,[x0,SK_MSS]
	mov	x2,TCP_IW			; (controle de congestion : depart)
	mul	x2,x1,x2
	str	x2,[x0,SK_CWND]
	movl	x2,TCP_SSTHRESH
	str	x2,[x0,SK_SSTHRESH]
	str	xzr,[x0,SK_DUPACKS]
	str	xzr,[x0,SK_INREC]
	ret

; x0 = socket en ecoute -> x0 = fils en attente d'accept
tcp_pending_count:
	adr	x1,sock_table
	ldr	x1,[x1]
	mov	x2,MAX_SOCK
	mov	x3,0
.f:	ldr	x4,[x1,SK_USED]
	cbz	x4,.n
	ldr	x4,[x1,SK_PARENT]
	cmp	x4,x0
	cinc	x3,x3,eq
.n:	add	x1,x1,SK_SIZE
	subs	x2,x2,1
	b.ne	.f
	mov	x0,x3
	ret

; x0 = socket, x1 = octets acquittes, w2 = acquittement (SND_UNA deja avance) :
; fin de recuperation, nouveau trou (accuse partiel), ou fenetre qui grandit
tcp_cc_ack:
	str	xzr,[x0,SK_DUPACKS]
	ldr	x3,[x0,SK_INREC]
	cbz	x3,.grow
	ldr	x3,[x0,SK_RECOVER]
	sub	w3,w2,w3
	tbnz	w3,31,.part
	str	xzr,[x0,SK_INREC]		; tout est recupere
	ldr	x3,[x0,SK_SSTHRESH]
	str	x3,[x0,SK_CWND]
	ret
.part:	ldr	x3,[x0,SK_CWND]			; accuse partiel : le trou suivant
	ldr	x4,[x0,SK_MSS]
	sub	x3,x3,x1
	add	x3,x3,x4
	cmp	x3,x4
	csel	x3,x4,x3,lo
	str	x3,[x0,SK_CWND]
	b	tcp_rtx_head
.grow:	ldr	x3,[x0,SK_CWND]
	ldr	x4,[x0,SK_MSS]
	ldr	x5,[x0,SK_SSTHRESH]
	cmp	x3,x5
	b.hs	.avoid
	cmp	x1,x4				; demarrage lent
	csel	x1,x1,x4,ls
	add	x3,x3,x1
	b	.set
.avoid:	mul	x5,x4,x4			; evitement de congestion
	udiv	x5,x5,x3
	cmp	x5,0
	cinc	x5,x5,eq
	add	x3,x3,x5
.set:	movl	x5,0x1000000
	cmp	x3,x5
	csel	x3,x5,x3,hi
	str	x3,[x0,SK_CWND]
	ret

; x0 = socket : un accuse duplique ; au troisieme, retransmission rapide
tcp_cc_dup:
	ldr	x1,[x0,SK_DUPACKS]
	add	x1,x1,1
	str	x1,[x0,SK_DUPACKS]
	ldr	x2,[x0,SK_MSS]
	ldr	x3,[x0,SK_INREC]
	cbz	x3,.first
	ldr	x3,[x0,SK_CWND]			; en recuperation : une trame est sortie
	add	x3,x3,x2
	str	x3,[x0,SK_CWND]
	ret
.first:	cmp	x1,TCP_DUPACK_MAX
	b.ne	.ret
	net_stat 4
	prologue
	bl	tcp_cc_loss
	mov	x3,TCP_DUPACK_MAX
	mul	x3,x2,x3
	add	x3,x3,x1
	str	x3,[x0,SK_CWND]
	bl	tcp_rtx_head
	epilogue
.ret:	ret

; x0 = socket : une perte : seuil a la moitie du vol, recuperation jusqu'a
; SND_NXT -> x1 = seuil, x2 = MSS (ne touche pas la pile : appelable en feuille)
tcp_cc_loss:
	ldr	x1,[x0,SK_SND_UNA]
	ldr	x3,[x0,SK_SND_NXT]
	sub	w1,w3,w1
	lsr	x1,x1,1
	ldr	x2,[x0,SK_MSS]
	lsl	x4,x2,1
	cmp	x1,x4
	csel	x1,x4,x1,lo
	str	x1,[x0,SK_SSTHRESH]
	str	x3,[x0,SK_RECOVER]
	mov	x4,1
	str	x4,[x0,SK_INREC]
	str	xzr,[x0,SK_RTT_SEQ]		; Karn
	ret

; x0 = socket : renvoie le plus ancien segment non acquitte
tcp_rtx_head:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x2,[x19,SK_SND_UNA]
	ldr	x3,[x19,SK_SND_NXT]
	sub	w3,w3,w2			; en vol
	ldr	x4,[x19,SK_FLAGS]
	tbz	x4,SKF_FIN_SENT,.d
	sub	w3,w3,1				; le FIN n'est pas une donnee
.d:	cbz	w3,.out
	ldr	x5,[x19,SK_MSS]
	cmp	x3,x5
	csel	x3,x3,x5,ls
	mov	w1,TH_ACK or TH_PSH
	bl	tcp_send
	mov	x0,x19
	bl	tcp_arm_rtx_force
.out:	ldr	x19,[sp],16
	epilogue

; x0 = socket : arme la retransmission si elle ne l'est pas
tcp_arm_rtx:
	ldr	x1,[x0,SK_RTX_AT]
	cbz	x1,tcp_arm_rtx_force
	ret
; x0 = socket : (re)arme la retransmission a maintenant + RTO
tcp_arm_rtx_force:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	time_ms
	ldr	x1,[x19,SK_RTO]
	add	x1,x0,x1
	str	x1,[x19,SK_RTX_AT]
	str	xzr,[x19,SK_PROBE_N]
	mov	x1,x0
	mov	x0,x19
	bl	tcp_probe_arm
	ldr	x19,[sp],16
	epilogue

; x0 = socket, x1 = maintenant : prochaine sonde a max(2 x SRTT, TCP_PROBE_MS),
; double a chaque sonde
tcp_probe_arm:
	ldr	x2,[x0,SK_SRTT]
	lsl	x2,x2,1
	cmp	x2,TCP_PROBE_MS
	mov	x3,TCP_PROBE_MS
	csel	x2,x3,x2,lo
	ldr	x3,[x0,SK_PROBE_N]
	lsl	x2,x2,x3
	add	x1,x1,x2
	str	x1,[x0,SK_PROBE_AT]
	ret

; x0 = socket : TIME-WAIT pendant 2 s
tcp_enter_time_wait:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,TCP_TIME_WAIT
	str	x0,[x19,SK_STATE]
	str	xzr,[x19,SK_RTX_AT]
	bl	time_ms
	add	x0,x0,2000
	str	x0,[x19,SK_DEADLINE]
	ldr	x19,[sp],16
	epilogue

; x0 = socket ferme : libere si l'application ne le tient plus
tcp_maybe_free:
	ldr	x1,[x0,SK_FLAGS]
	tbz	x1,SKF_ORPHAN,.keep
	b	sock_free
.keep:	ret

; x0 = socket : envoie les donnees en attente que la fenetre permet, puis FIN
tcp_output:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_ESTAB
	b.eq	.go
	cmp	x0,TCP_CLOSE_WAIT
	b.ne	.out
.go:	ldr	x0,[x19,SK_FLAGS]
	tbnz	x0,SKF_FIN_SENT,.out
.seg:	ldr	x1,[x19,SK_SND_UNA]
	ldr	x2,[x19,SK_SND_NXT]
	sub	w20,w2,w1			; en vol
	ldr	x3,[x19,SK_TXHEAD]
	ldr	x4,[x19,SK_TXTAIL]
	sub	x3,x3,x4
	sub	x21,x3,x20			; pas encore envoye
	cbz	x21,.fin
	ldr	x5,[x19,SK_SND_WND]
	cbnz	x5,.wnd
	cbnz	x20,.out			; fenetre nulle : sonde d'un octet si rien en vol
	mov	x5,1
.wnd:	ldr	x6,[x19,SK_CWND]		; fenetre du pair, de congestion
	cmp	x5,x6
	b.ls	.cw
	cbz	x6,.cw
	mov	x5,x6
.cw:	subs	x22,x5,x20			; utilisable
	b.ls	.out
	cmp	x21,x22
	csel	x21,x21,x22,ls
	ldr	x5,[x19,SK_MSS]
	cmp	x21,x5
	csel	x21,x21,x5,ls
	ldr	x0,[x19,SK_RTT_SEQ]		; chronometrer ce segment
	cbnz	x0,.send
	add	w0,w2,w21
	str	x0,[x19,SK_RTT_SEQ]
	bl	time_ms
	str	x0,[x19,SK_RTT_START]
.send:	mov	x0,x19
	mov	w1,TH_ACK or TH_PSH
	ldr	x2,[x19,SK_SND_NXT]
	mov	x3,x21
	bl	tcp_send
	cbz	x0,.out				; adresse MAC perdue : la minuterie reessaiera
	ldr	x2,[x19,SK_SND_NXT]
	add	w2,w2,w21
	str	x2,[x19,SK_SND_NXT]
	mov	x0,x19
	bl	tcp_arm_rtx
	b	.seg
.fin:	ldr	x0,[x19,SK_FLAGS]		; tout est envoye : FIN demande ?
	tbz	x0,SKF_FIN_PENDING,.out
	orr	x0,x0,1 shl SKF_FIN_SENT
	str	x0,[x19,SK_FLAGS]
	ldr	x2,[x19,SK_SND_NXT]
	str	x2,[x19,SK_FIN_SEQ]
	add	w3,w2,1
	str	x3,[x19,SK_SND_NXT]
	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_ESTAB
	mov	x1,TCP_FIN_WAIT1
	mov	x3,TCP_LAST_ACK
	csel	x0,x1,x3,eq
	str	x0,[x19,SK_STATE]
	mov	x0,x19
	mov	w1,TH_FIN or TH_ACK
	mov	x3,0
	bl	tcp_send
	mov	x0,x19
	bl	tcp_arm_rtx
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Minuteries TCP (sous net_lock, toutes les 10 ms)
; w0 = adresse -> x0 = 1 si c'est nous : 127.0.0.0/8, ou notre adresse (x1, x2)
ip_is_local:
	lsr	w1,w0,24
	cmp	w1,127
	b.eq	.oui
	adr	x1,net_ip
	ldr	w1,[x1]
	cbz	w1,.non
	cmp	w0,w1
	b.eq	.oui
.non:	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; w0 = destination -> w0 = notre adresse source pour elle (x1) : en boucle
; locale, sans interface, net_ip peut etre nulle
ip_src:
	lsr	w1,w0,24
	cmp	w1,127
	b.eq	.ret
	adr	x1,net_ip
	ldr	w0,[x1]
.ret:	ret

; (sous net_lock) -> x0 = ms avant la prochaine verification (net_sleep)
net_wait_ms:
	adr	x0,netdev_up
	ldr	w0,[x0]
	cbz	w0,.sock
	adr	x0,dhcp_state
	ldr	w0,[x0]
	cmp	w0,DHCP_BOUND
	b.hs	.sock
	mov	x4,NET_DHCP_MS
	b	.s0
.sock:	movl	x4,NET_LONG_MS
.s0:	adr	x1,sock_table
	ldr	x1,[x1]
	mov	x2,MAX_SOCK
.s:	ldr	x0,[x1,SK_USED]
	cbz	x0,.n
	ldr	x0,[x1,SK_TYPE]
	cmp	x0,SOCK_TCP
	b.ne	.n
	ldr	x0,[x1,SK_ACK_AT]
	cbnz	x0,.tick
	ldr	x0,[x1,SK_RTX_AT]
	cbnz	x0,.tick
	ldr	x0,[x1,SK_PROBE_AT]
	cbnz	x0,.tick
	ldr	x0,[x1,SK_STATE]
	cmp	x0,TCP_TIME_WAIT
	b.eq	.tick
	cmp	x0,TCP_FIN_WAIT2		; (orphelin : son delai ; apres un
	b.ne	.n				; shutdown, le pair decide : rien a voir)
	ldr	x0,[x1,SK_FLAGS]
	tbnz	x0,SKF_ORPHAN,.tick
.n:	add	x1,x1,SK_SIZE
	subs	x2,x2,1
	b.ne	.s
	mov	x0,x4
	ret
.tick:	mov	x0,NET_TICK_MS
	ret

tcp_timers:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	time_ms
	mov	x21,x0
	adr	x19,sock_table
	ldr	x19,[x19]
	mov	x20,MAX_SOCK
.s:	ldr	x0,[x19,SK_USED]
	cbz	x0,.next
	ldr	x0,[x19,SK_TYPE]
	cmp	x0,SOCK_TCP
	b.ne	.next
	ldr	x1,[x19,SK_ACK_AT]		; accuse retarde arrive a echeance
	cbz	x1,.noack
	cmp	x21,x1
	b.lo	.noack
	mov	x0,x19
	bl	tcp_send_ack
.noack:	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_TIME_WAIT		; fin de TIME-WAIT
	b.eq	.deadline
	cmp	x0,TCP_FIN_WAIT2
	b.ne	.rtx
	ldr	x1,[x19,SK_FLAGS]		; orphelin bloque en FIN-WAIT-2
	tbz	x1,SKF_ORPHAN,.rtx
.deadline:
	ldr	x1,[x19,SK_DEADLINE]
	cmp	x21,x1
	b.lo	.next
	str	xzr,[x19,SK_STATE]
	mov	x0,x19
	bl	tcp_maybe_free
	b	.next
.rtx:	ldr	x1,[x19,SK_RTX_AT]
	cbz	x1,.next
	cmp	x21,x1
	b.lo	.probe
	ldr	x1,[x19,SK_RETRIES]
	add	x1,x1,1
	str	x1,[x19,SK_RETRIES]
	cmp	x1,8
	b.ls	.retry
	ldr	x1,[x19,SK_FLAGS]		; trop d'essais : abandon
	orr	x1,x1,1 shl SKF_TIMEOUT
	str	x1,[x19,SK_FLAGS]
	mov	x0,x19
	mov	w1,TH_RST or TH_ACK
	ldr	x2,[x19,SK_SND_NXT]
	mov	x3,0
	bl	tcp_send
	str	xzr,[x19,SK_STATE]
	str	xzr,[x19,SK_RTX_AT]
	ldr	x0,[x19,SK_PARENT]
	cbz	x0,.orph
	mov	x0,x19				; fils non accepte
	bl	sock_free
	b	.next
.orph:	mov	x0,x19
	bl	tcp_maybe_free
	b	.next
.retry:	net_stat 1
	str	xzr,[x19,SK_RTT_SEQ]		; Karn : pas de mesure apres retransmission
	ldr	x1,[x19,SK_RTO]			; RTO double, 16 s maximum
	lsl	x1,x1,1
	movl	x2,16000
	cmp	x1,x2
	csel	x1,x2,x1,hi
	str	x1,[x19,SK_RTO]
	mov	x0,x19
	bl	tcp_arm_rtx_force
	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_SYN_SENT
	b.ne	.r1
	mov	x0,x19
	mov	w1,TH_SYN
	ldr	x2,[x19,SK_ISS]
	mov	x3,0
	bl	tcp_send
	b	.next
.r1:	cmp	x0,TCP_SYN_RCVD
	b.ne	.r2
	mov	x0,x19
	mov	w1,TH_SYN or TH_ACK
	ldr	x2,[x19,SK_ISS]
	mov	x3,0
	bl	tcp_send
	b	.next
.r2:	mov	x0,x19				; expiration : on repart d'un segment,
	bl	tcp_cc_loss			; les accuses partiels rendent les trous
	str	x2,[x19,SK_CWND]
	str	xzr,[x19,SK_DUPACKS]
	ldr	x1,[x19,SK_SND_UNA]		; plus ancien segment non acquitte
	ldr	x2,[x19,SK_SND_NXT]
	sub	w3,w2,w1			; en vol
	cbz	w3,.next
	ldr	x4,[x19,SK_FLAGS]
	tbz	x4,SKF_FIN_SENT,.r_data
	sub	w3,w3,1				; le FIN n'est pas une donnee
	cbnz	w3,.r_data
	mov	x0,x19				; seul le FIN reste : le renvoyer
	mov	w1,TH_FIN or TH_ACK
	ldr	x2,[x19,SK_FIN_SEQ]
	mov	x3,0
	bl	tcp_send
	b	.next
.r_data:
	ldr	x5,[x19,SK_MSS]
	cmp	x3,x5
	csel	x3,x3,x5,ls
	mov	x0,x19
	mov	w1,TH_ACK or TH_PSH
	ldr	x2,[x19,SK_SND_UNA]
	bl	tcp_send
	b	.next
.probe:	ldr	x1,[x19,SK_PROBE_AT]		; plus d'accuse depuis un moment : le
	cmp	x21,x1				; dernier segment envoye, renvoye seul,
	b.lo	.next				; en reclame un tout de suite. Si les
	ldr	x1,[x19,SK_PROBE_N]		; accuses se sont perdus (le pair a tout
	cmp	x1,TCP_PROBE_MAX		; recu : plus rien ne les ferait repartir),
	b.hs	.next				; le debit reprend sans RTO ni baisse de
	ldr	x0,[x19,SK_INREC]		; la fenetre de congestion
	cbnz	x0,.next
	ldr	x0,[x19,SK_FLAGS]
	tbnz	x0,SKF_FIN_SENT,.next
	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_ESTAB
	b.eq	.pr
	cmp	x0,TCP_CLOSE_WAIT
	b.ne	.next
.pr:	add	x1,x1,1
	str	x1,[x19,SK_PROBE_N]
	mov	x0,x19
	mov	x1,x21
	bl	tcp_probe_arm
	str	xzr,[x19,SK_RTT_SEQ]		; Karn
	net_stat 5
	ldr	x1,[x19,SK_SND_UNA]
	ldr	x2,[x19,SK_SND_NXT]
	sub	w3,w2,w1			; en vol
	cbz	w3,.next
	ldr	x5,[x19,SK_MSS]
	cmp	x3,x5
	csel	x3,x3,x5,ls
	sub	w2,w2,w3
	mov	x0,x19
	mov	w1,TH_ACK or TH_PSH
	bl	tcp_send
.next:	add	x19,x19,SK_SIZE
	subs	x20,x20,1
	b.ne	.s
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ============================================================================
;  Appels systeme reseau (40 a 52)
; ============================================================================

; x0 = descripteur -> x0 = socket du process courant (0 si invalide)
sk_index:
	adr	x1,sock_table
	ldr	x1,[x1]
	sub	x0,x0,x1
	lsr	x0,x0,9				; SK_SIZE = 512
	ret

; --- 40 socket(type 1 TCP / 2 UDP) -> descripteur ------------------------------
sock_close_locked:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	str	xzr,[x19,SK_OWNER]
	ldr	x0,[x19,SK_FLAGS]
	orr	x0,x0,1 shl SKF_ORPHAN
	str	x0,[x19,SK_FLAGS]
	ldr	x0,[x19,SK_TYPE]
	cmp	x0,SOCK_TCP
	b.ne	.free
	ldr	x0,[x19,SK_STATE]
	cmp	x0,TCP_LISTEN
	b.ne	.conn
	adr	x20,sock_table			; fils non acceptes : RST + liberation
	ldr	x20,[x20]
	mov	x1,MAX_SOCK
.ch:	ldr	x2,[x20,SK_USED]
	cbz	x2,.chn
	ldr	x2,[x20,SK_PARENT]
	cmp	x2,x19
	b.ne	.chn
	str	x1,[sp,-16]!
	mov	x0,x20
	mov	w1,TH_RST or TH_ACK
	ldr	x2,[x20,SK_SND_NXT]
	mov	x3,0
	bl	tcp_send
	mov	x0,x20
	bl	sock_free
	ldr	x1,[sp],16
.chn:	add	x20,x20,SK_SIZE
	subs	x1,x1,1
	b.ne	.ch
	b	.free
.conn:	cmp	x0,TCP_SYN_RCVD
	b.lo	.free				; ferme ou SYN-SENT : liberation immediate
	cmp	x0,TCP_ESTAB
	b.eq	.fin
	cmp	x0,TCP_CLOSE_WAIT
	b.eq	.fin
	cmp	x0,TCP_SYN_RCVD
	b.eq	.fin
	b	.out				; deja en fermeture : le protocole finira
.fin:	ldr	x0,[x19,SK_FLAGS]
	orr	x0,x0,1 shl SKF_FIN_PENDING
	str	x0,[x19,SK_FLAGS]
	mov	x0,x19
	bl	tcp_output
	b	.out
.free:	mov	x0,x19
	bl	sock_free
.out:	ldp	x19,x20,[sp],16
	epilogue

; Fermeture des sockets d'un process termine (x0 = PCB)
udp_sendto_user:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	mov	w22,w3
	mov	x23,x4
	movl	x0,1472
	cmp	x20,x0
	b.hi	.big
	mov	w0,w21
	bl	arp_resolve_wait
	cbz	x0,.unreach
	net_lock_take
	ldr	x0,[x23,SK_LPORT]
	cbnz	x0,.port
	bl	sock_ephemeral
	str	x0,[x23,SK_LPORT]
.port:	ldr	x0,[x23,SK_LPORT]
	mov	w1,w21
	mov	w2,w22
	mov	x3,x19
	mov	x4,x20
	bl	udp_output
	mov	x24,x0
	net_lock_drop
	mov	x0,x20
	cbnz	x24,.out
.unreach:
	mov	x0,E_HOSTUNREACH
	b	.out
.big:	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 50 resolve(nom) -> adresse IPv4 (ordre hote) ----------------------------------
dns_resolve:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	bl	parse_dotted
	tbz	x0,63,.done			; deja une adresse numerique
	mov	x0,x19				; localhost : la boucle locale
	adr	x1,s_localhost
.lh:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cbz	w3,.lhf
	orr	w2,w2,0x20
	cmp	w2,w3
	b.ne	.dns
	b	.lh
.lhf:	cbnz	w2,.dns
	movl	x0,0x7F000001
	b	.done
.dns:	adr	x0,net_dns
	ldr	w20,[x0]
	cbz	w20,.unreach
	mov	w0,w20
	bl	arp_resolve_wait
	cbz	x0,.unreach
	; requete : en-tete 12 octets + nom en etiquettes + type A + classe IN
	mov	x0,sp
	mov	x1,512
	bl	mem_zero
	bl	time_ms
	and	w21,w0,0xFFFF			; identifiant
	st16be	w21,sp,0
	mov	w0,0x0100			; recursion demandee
	st16be	w0,sp,2
	mov	w0,1
	st16be	w0,sp,4
	add	x1,sp,12			; etiquettes
	mov	x2,x19
.label:	mov	x3,x1				; octet de longueur
	add	x1,x1,1
	mov	x4,0
.lc:	ldrb	w5,[x2]
	cbz	w5,.lend
	add	x2,x2,1
	cmp	w5,'.'
	b.eq	.lend
	strb	w5,[x1],1
	add	x4,x4,1
	cmp	x4,63
	b.hi	.inval
	b	.lc
.lend:	strb	w4,[x3]
	cbz	x4,.qdone
	ldrb	w5,[x2,-1]
	cmp	w5,'.'
	b.eq	.label
.qdone:	cbz	x4,.term
	strb	wzr,[x1],1			; etiquette vide finale
.term:	mov	w0,1				; type A
	st16be	w0,x1,0
	st16be	w0,x1,2				; classe IN
	add	x1,x1,4
	mov	x0,sp
	sub	x22,x1,x0			; longueur de la requete
	net_lock_take				; socket UDP interne
	bl	sock_alloc
	mov	x23,x0
	cbz	x23,.nomem_l
	mov	x0,SOCK_UDP
	str	x0,[x23,SK_TYPE]
	mov	x0,x23
	mov	x1,0
	bl	sk_alloc_rings
	cbnz	x0,.sk_ok
	mov	x0,x23
	bl	sock_free
.nomem_l:
	net_lock_drop
	mov	x0,E_NOMEM
	b	.out
.sk_ok:	bl	sock_ephemeral
	str	x0,[x23,SK_LPORT]
	net_lock_drop
	mov	x24,3				; essais
.send:	net_lock_take
	ldr	x0,[x23,SK_LPORT]
	mov	w1,w20
	mov	w2,53
	mov	x3,sp
	mov	x4,x22
	bl	udp_output
	net_lock_drop
	bl	time_ms
	add	x21,x0,2000			; (identifiant ecrase : relu dans la requete)
.wait:	net_lock_take
	ldr	x1,[x23,SK_RXHEAD]
	ldr	x2,[x23,SK_RXTAIL]
	cmp	x1,x2
	b.ne	.reply
	net_lock_drop
	bl	time_ms
	cmp	x0,x21
	b.hs	.retry
	mov	x0,5
	bl	thread_sleep
	b	.wait
.retry:	subs	x24,x24,1
	b.ne	.send
	mov	x24,E_TIMEDOUT
	b	.free
.reply:	add	x1,sp,256			; reponse dans la 2e moitie du tampon
	mov	x0,x23
	mov	x2,8
	bl	sk_rx_read
	add	x1,sp,256
	ldrh	w2,[x1]				; longueur
	mov	w3,248
	cmp	w2,w3
	csel	w2,w2,w3,ls
	ldrh	w4,[x1]
	sub	x3,x4,x2			; reste a jeter
	str	x3,[sp,-16]!
	mov	x0,x23
	add	x1,sp,256+16
	bl	sk_rx_read
	ldr	x3,[sp],16
	ldr	x1,[x23,SK_RXTAIL]
	add	x1,x1,x3
	str	x1,[x23,SK_RXTAIL]
	net_lock_drop
	add	x0,sp,256			; analyse
	ldrh	w1,[sp]				; meme identifiant ?
	ldrh	w2,[x0]
	cmp	w1,w2
	b.ne	.wait
	mov	x1,248
	bl	dns_parse_a
	mov	x24,x0
.free:	net_lock_take
	mov	x0,x23
	bl	sock_free
	net_lock_drop
	mov	x0,x24
	b	.out
.done:	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.unreach:
	mov	x0,E_HOSTUNREACH
.out:	add	sp,sp,512
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = reponse DNS, x1 = longueur -> x0 = premiere adresse A, ou erreur
dns_parse_a:
	prologue
	mov	x9,x0
	add	x10,x0,x1			; fin
	ldrb	w2,[x9,3]			; RCODE
	and	w2,w2,0xF
	cbnz	w2,.noent
	ldrb	w2,[x9,6]			; ANCOUNT
	ldrb	w3,[x9,7]
	orr	w11,w3,w2,lsl 8
	cbz	w11,.noent
	add	x2,x9,12			; sauter la question
	bl	.skip_name
	add	x2,x2,4
.ans:	cbz	w11,.noent
	sub	w11,w11,1
	bl	.skip_name
	add	x3,x2,10
	cmp	x3,x10
	b.hi	.noent
	ldrb	w4,[x2]				; type
	ldrb	w5,[x2,1]
	orr	w4,w5,w4,lsl 8
	ldrb	w5,[x2,8]			; longueur des donnees
	ldrb	w6,[x2,9]
	orr	w5,w6,w5,lsl 8
	add	x2,x2,10
	cmp	w4,1
	b.ne	.next
	cmp	w5,4
	b.ne	.next
	ldrb	w0,[x2]
	ldrb	w1,[x2,1]
	orr	w0,w1,w0,lsl 8
	ldrb	w1,[x2,2]
	orr	w0,w1,w0,lsl 8
	ldrb	w1,[x2,3]
	orr	w0,w1,w0,lsl 8
	epilogue
.next:	add	x2,x2,x5
	b	.ans
.noent:	mov	x0,E_NOENT
	epilogue
; (interne) x2 = nom -> x2 apres le nom (etiquettes ou pointeur de compression)
.skip_name:
.sn:	cmp	x2,x10
	b.hs	.sn_end
	ldrb	w3,[x2]
	cbz	w3,.sn_zero
	and	w4,w3,0xC0
	cmp	w4,0xC0
	b.eq	.sn_ptr
	add	x2,x2,x3
	add	x2,x2,1
	b	.sn
.sn_zero:
	add	x2,x2,1
	ret
.sn_ptr:
	add	x2,x2,2
.sn_end:
	ret

; x0 = chaine "a.b.c.d" -> x0 = adresse, -1 (negatif) si ce n'en est pas une
parse_dotted:
	mov	x1,0				; resultat
	mov	x2,0				; octets lus
.oct:	mov	x3,0
	mov	x4,0				; chiffres
.dig:	ldrb	w5,[x0]
	sub	w6,w5,'0'
	cmp	w6,9
	b.hi	.end_oct
	mov	x7,10
	madd	x3,x3,x7,x6
	add	x0,x0,1
	add	x4,x4,1
	b	.dig
.end_oct:
	cbz	x4,.bad
	cmp	x3,255
	b.hi	.bad
	orr	x1,x3,x1,lsl 8
	add	x2,x2,1
	cmp	x2,4
	b.eq	.last
	cmp	w5,'.'
	b.ne	.bad
	add	x0,x0,1
	b	.oct
.last:	cbnz	w5,.bad
	mov	x0,x1
	ret
.bad:	mov	x0,-1
	ret

; --- 51 netinfo(tampon 32 o) : ip, masque, passerelle, dns, mac[6], etat ----------
NETINFO_SIZE	= 32
