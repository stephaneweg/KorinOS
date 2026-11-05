; netd - la pile reseau en service (docs/RESEAU.md, feuille de route section 5),
; lance par init2 (SD0:/etc/services.txt). Service " net " :
;  * les pilotes d'interface (usbnet, genet, wifi : src/svc/inc/netif.inc)
;    s'enregistrent (op 20) ; netd leur cree un anneau de trames partage ;
;    l'interface active est celle de plus haute priorite qui a un lien
;    (GENET 30 > Wi-Fi 20 > USB 10) : elle porte l'adresse (DHCP) et la route ;
;  * les programmes y viennent par NetKit (ops 1-17 : sockets, DNS, ping,
;    etat, heure) ; les donnees de plus de 448 octets passent par un tampon
;    partage de 128 Ko par process (op 1) ;
;  * NTP : heure prise au premier bail puis toutes les 6 h ; l'heure locale
;    (fuseau= de SD0:/etc/options.txt) est ecrite dans SD0:/etc/clock.cfg
;    (le decalage que lisent la barre de menus et l'Horloge).
; Threads : un thread " pile " (anneaux, minuteries, toutes les 1 ms au plus),
; un thread auxiliaire (notifications, NTP, fichiers) et WORKERS threads qui
; servent les requetes ; une requete qui attend (connect, recv, accept, DNS,
; ping) attend dans son thread, sans le verrou de la pile (comme les appels
; 40-52 du noyau), 25 s au plus (E_AGAIN : NetKit redemande).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/netd.inc'
include '../../inc/bus.inc'
include '../../kits/buskit/buskit.inc'
include 'macros.inc'

WORKERS		= 24			; threads de requetes (dont le principal)
WSTACK		= 32768			; pile d'un thread
WAIT_MAX	= 25000			; attente d'une requete (svc_call : 30 s)

IF_MAX		= 4			; interfaces
IF_PID		= 0			; pilote (0 : place libre)
IF_TYPE		= 8
IF_PRIO		= 16
IF_MAC		= 24
IF_NAME		= 32			; 8 o
IF_BUF		= 40			; numero du tampon de l'anneau
IF_RING		= 48			; son adresse chez netd (0 : aucun)
IF_LINK		= 56
IF_SPEED	= 64
IF_SSID		= 72			; 33 o
IF_WAKE		= 112			; canal + 1 du pilote (nif_wake, 0 : il sonde)
IF_SIZE		= 128
NET_BUSY_MS	= 50			; ms sans trame avant de revenir au repos
NET_TICK_MS	= 10			; sommeil : une minuterie TCP en cours
NET_DHCP_MS	= 100			;   adresse pas encore obtenue
NET_LONG_MS	= 1000			;   rien a surveiller
LO_SLOTS	= 256			; boucle locale (lo_put), puissance de 2
LO_SHIFT	= 11			; 2 Ko par trame : longueur (8 o), trame
LO_PAGES	= (LO_SLOTS shl LO_SHIFT) / 4096

CLI_MAX		= 48			; process clients ayant un tampon
CL_PID		= 0
CL_NUM		= 8
CL_ADR		= 16
CL_SIZE		= 24

E_PERM		= -1
E_NOENT		= -2
E_BADF		= -9
E_NOMEM		= -12
E_FAULT		= -14
E_BUSY		= -16
E_INVAL		= -22
E_NOSYS		= -38

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	bl	stack_init
	cbz	x0,quit
	adr	x0,s_net
	mov	x1,SVF_FINS			; (clients et pilotes qui s'arretent)
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	adr	x0,s_netrx			; le reveil de la pile (net_sleep)
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal_rx
	str	x0,[x1]
	adr	x0,m_pret
	bl	puts
	adr	x0,stack_thread
	bl	spawn_thread
	tbnz	x0,63,quit
	adr	x0,bg_thread
	bl	spawn_thread
	mov	x19,WORKERS-1
.w:	adr	x0,worker
	bl	spawn_thread
	subs	x19,x19,1
	b.ne	.w
	b	worker				; le thread principal sert aussi
quit:	mov	x0,1				; (init2 relance)
	sys	SYS_EXIT

; x0 = entree : un thread avec sa pile (WSTACK) -> x0 = tid, < 0 si echec
spawn_thread:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,WSTACK + 16
	sys	SYS_SBRK
	tbnz	x0,63,.out
	add	x1,x0,WSTACK
	and	x1,x1,0xFFFFFFFFFFFFFFF0
	mov	x0,x19
	mov	x2,0
	sys	SYS_THREAD_CREATE
.out:	ldr	x19,[sp],16
	epilogue

; Pile : tables, trames de travail, sockets -> x0 = 1 si pret
stack_init:
	prologue
	mov	x0,LO_PAGES
	bl	pmm_alloc_n
	cbz	x0,.fail
	adr	x1,lo_ring
	str	x0,[x1]
	mov	x0,2				; trame de travail RX + TX
	bl	pmm_alloc_n
	cbz	x0,.fail
	adr	x1,net_rxframe
	str	x0,[x1]
	add	x0,x0,4096
	adr	x1,net_txbuf
	str	x0,[x1]
	mov	x0,(MAX_SOCK * SK_SIZE) / 4096
	bl	pmm_alloc_n
	cbz	x0,.fail
	adr	x1,sock_table
	str	x0,[x1]
	mov	x1,MAX_SOCK * SK_SIZE
	bl	mem_zero
	mov	w0,DHCP_INIT
	adr	x1,dhcp_state
	str	w0,[x1]
	adr	x0,if_active
	mov	x1,-1
	str	x1,[x0]
	mov	x0,1
	epilogue
.fail:	mov	x0,0
	epilogue

; ---------------------------------------------------------------------------
;  Thread de la pile : anneaux de reception, minuteries
; ---------------------------------------------------------------------------
stack_thread:
.loop:	mov	x22,0				; trames traitees dans ce tour
	net_lock_take
	bl	lo_drain
	mov	x22,x0
	mov	x19,0				; interface
.if:	adr	x20,ifs
	mov	x0,IF_SIZE
	madd	x20,x19,x0,x20
	ldr	x21,[x20,IF_RING]
	cbz	x21,.ifn
	mov	x23,32				; au plus 32 trames par interface
.rx:	ldr	x2,[x21,NR_RX_TAIL]
	add	x0,x21,NR_RX_HEAD
	ldar	x1,[x0]
	cmp	x1,x2
	b.eq	.ifn
	and	x3,x2,NR_SLOTS-1
	add	x0,x21,NR_RX
	add	x0,x0,x3,lsl 11			; emplacement (2 Ko)
	ldr	x1,[x0],8			; longueur, trame
	adr	x3,if_active
	ldr	x3,[x3]
	cmp	x3,x19
	b.ne	.skip				; interface inactive : trame jetee
	cmp	x1,NR_FRAME_MAX
	b.hi	.skip
	adr	x16,net_st			; (octets recus)
	ldr	x17,[x16,6*8]
	add	x17,x17,x1
	str	x17,[x16,6*8]
	bl	net_input
	net_lock_drop				; (relache un instant entre deux
	net_lock_take				;  trames : les requetes passent)
	ldr	x0,[x20,IF_RING]		; interface partie entre-temps ?
	cmp	x0,x21
	b.ne	.ifn
.skip:	net_stat 0				; (trames traitees)
	ldr	x2,[x21,NR_RX_TAIL]
	add	x2,x2,1
	add	x0,x21,NR_RX_TAIL
	stlr	x2,[x0]
	add	x22,x22,1
	subs	x23,x23,1
	b.ne	.rx
.ifn:	add	x19,x19,1
	cmp	x19,IF_MAX
	b.lo	.if
	net_lock_drop
	bl	time_ms				; minuteries toutes les 10 ms
	adr	x1,net_next_timer
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.sleep
	add	x0,x0,10
	str	x0,[x1]
	net_lock_take
	bl	dhcp_timer
	bl	tcp_timers
	net_lock_drop
.sleep:	bl	time_ms				; sans pause pendant le trafic
	adr	x1,net_busy_t
	cbz	x22,.calm
	str	x0,[x1]
	b	.loop
.calm:	ldr	x1,[x1]
	sub	x0,x0,x1
	cmp	x0,NET_BUSY_MS
	b.hs	.idle
	sys	SYS_YIELD
	b	.loop
.idle:	bl	net_sleep
	b	.loop

; Dort jusqu'a une trame (pilote : netif_rx_put), une requete qui a pu armer une
; minuterie (worker), ou la prochaine minuterie (net_wait_ms). Les drapeaux sont
; poses avant de relire les anneaux et les sockets : ce qui arrive apres
; reveille forcement.
net_sleep:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,512
	mov	x0,1
	bl	net_flags
	adr	x0,net_asleep
	mov	x1,1
	str	x1,[x0]
	adr	x0,net_long
	mov	x1,1
	str	x1,[x0]
	dmb	ish
	bl	net_rx_ready
	cbnz	x0,.up
	net_lock_take
	bl	net_wait_ms
	mov	x19,x0
	net_lock_drop
	cmp	x19,NET_TICK_MS			; (une minuterie : pas besoin d'etre
	b.hi	.dort				;  reveille par les requetes)
	adr	x0,net_long
	str	xzr,[x0]
.dort:	adr	x0,canal_rx
	ldr	x0,[x0]
	mov	x1,sp
	mov	x2,x19
	sys	SYS_SVC_NEXT
.up:	adr	x0,net_long
	str	xzr,[x0]
	adr	x0,net_asleep
	str	xzr,[x0]
	mov	x0,0
	bl	net_flags
	add	sp,sp,512
	ldr	x19,[sp],16
	epilogue

; x0 = valeur : NR_SLEEP de chaque anneau
net_flags:
	adr	x1,ifs
	mov	x2,0
.i:	ldr	x3,[x1,IF_RING]
	cbz	x3,.n
	str	x0,[x3,NR_SLEEP]
.n:	add	x1,x1,IF_SIZE
	add	x2,x2,1
	cmp	x2,IF_MAX
	b.lo	.i
	ret

; x0 = trame, x1 = longueur : vers soi-meme, comme une trame recue (sous
; net_lock) -> x0 = 1 ; 0 : file pleine, trame perdue (TCP retransmet)
lo_put:
	adr	x2,lo_head
	ldr	x3,[x2]
	adr	x4,lo_tail
	ldr	x4,[x4]
	sub	x5,x3,x4
	cmp	x5,LO_SLOTS
	b.hs	.full
	adr	x4,lo_ring
	ldr	x4,[x4]
	and	x5,x3,LO_SLOTS-1
	add	x4,x4,x5,lsl LO_SHIFT
	str	x1,[x4],8
	add	x5,x1,15
	lsr	x5,x5,4
.c:	ldp	x6,x7,[x0],16
	stp	x6,x7,[x4],16
	subs	x5,x5,1
	b.ne	.c
	add	x3,x3,1
	stlr	x3,[x2]
	dmb	ish				; (net_sleep : drapeau, puis file relue)
	adr	x2,net_asleep
	ldr	x3,[x2]
	cbz	x3,.ok
	str	xzr,[x2]
	adr	x0,canal_rx
	ldr	x0,[x0]
	mov	x1,NR_SIG_RX
	sys	SYS_SVC_SIGNAL
.ok:	mov	x0,1
	ret
.full:	mov	x0,0
	ret

; (sous net_lock) la file de la boucle locale -> x0 = trames traitees
lo_drain:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
.n:	adr	x0,lo_tail
	ldr	x20,[x0]
	adr	x0,lo_head
	ldr	x0,[x0]
	cmp	x0,x20
	b.eq	.fin
	cmp	x19,32
	b.hs	.fin
	adr	x0,lo_ring
	ldr	x0,[x0]
	and	x1,x20,LO_SLOTS-1
	add	x0,x0,x1,lsl LO_SHIFT
	ldr	x1,[x0],8
	bl	net_input
	add	x20,x20,1
	adr	x0,lo_tail
	str	x20,[x0]
	add	x19,x19,1
	net_stat 0
	net_lock_drop				; (les requetes passent)
	net_lock_take
	b	.n
.fin:	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si un anneau a une trame
net_rx_ready:
	adr	x1,lo_tail
	ldr	x1,[x1]
	adr	x2,lo_head
	ldar	x2,[x2]
	cmp	x1,x2
	b.ne	.oui
	adr	x1,ifs
	mov	x2,0
.i:	ldr	x3,[x1,IF_RING]
	cbz	x3,.n
	ldr	x4,[x3,NR_RX_TAIL]
	add	x5,x3,NR_RX_HEAD
	ldar	x5,[x5]
	cmp	x4,x5
	b.ne	.oui
.n:	add	x1,x1,IF_SIZE
	add	x2,x2,1
	cmp	x2,IF_MAX
	b.lo	.i
	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; ---------------------------------------------------------------------------
;  Interfaces
; ---------------------------------------------------------------------------

; x0 = trame Ethernet, x1 = longueur -> x0 = 1 si mise dans l'anneau
; d'emission de l'interface active (sous net_lock)
netif_send:
	adr	x2,if_active
	ldr	x2,[x2]
	tbnz	x2,63,.none
	adr	x3,ifs
	mov	x4,IF_SIZE
	madd	x3,x2,x4,x3
	ldr	x3,[x3,IF_RING]
	cbz	x3,.none
	cmp	x1,NR_FRAME_MAX
	b.hi	.none
	ldr	x4,[x3,NR_TX_HEAD]
	add	x5,x3,NR_TX_TAIL
	ldar	x5,[x5]
	sub	x6,x4,x5
	cmp	x6,NR_SLOTS
	b.hs	.full
	and	x6,x4,NR_SLOTS-1
	add	x7,x3,NR_TX
	add	x7,x7,x6,lsl 11
	adr	x16,net_st			; (octets emis)
	ldr	x17,[x16,7*8]
	add	x17,x17,x1
	str	x17,[x16,7*8]
	str	x1,[x7],8
	add	x5,x1,15			; copie par 16 octets (la trame de
	lsr	x5,x5,4				; travail fait 4 Ko : on peut lire
.c:	ldp	x8,x9,[x0],16			; un peu au-dela)
	stp	x8,x9,[x7],16
	subs	x5,x5,1
	b.ne	.c
	add	x4,x4,1
	add	x5,x3,NR_TX_HEAD
	stlr	x4,[x5]
	adr	x3,ifs				; le pilote dort ?
	mov	x4,IF_SIZE
	madd	x3,x2,x4,x3
	ldr	x0,[x3,IF_WAKE]
	cbz	x0,.sent
	sub	x0,x0,1
	mov	x1,NIF_SIG_TX
	sys	SYS_SVC_SIGNAL
.sent:	mov	x0,1
	ret
.full:	ldr	x4,[x3,NR_TX_DROPS]
	add	x4,x4,1
	str	x4,[x3,NR_TX_DROPS]
.none:	mov	x0,0
	ret

; (sous net_lock) choisit l'interface active : lien monte, plus haute
; priorite ; si elle change, l'adresse est oubliee et le DHCP relance
netif_choose:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,-1				; meilleure
	mov	x20,-1				; sa priorite
	adr	x2,ifs
	mov	x3,0
.t:	ldr	x4,[x2,IF_PID]
	cbz	x4,.n
	ldr	x4,[x2,IF_RING]
	cbz	x4,.n
	ldr	x4,[x2,IF_LINK]
	cbz	x4,.n
	ldr	x4,[x2,IF_PRIO]
	cmp	x4,x20
	b.le	.n
	mov	x20,x4
	mov	x19,x3
.n:	add	x2,x2,IF_SIZE
	add	x3,x3,1
	cmp	x3,IF_MAX
	b.lo	.t
	adr	x0,if_active
	ldr	x1,[x0]
	cmp	x1,x19
	b.eq	.out
	str	x19,[x0]
	adr	x0,netdev_up
	tbnz	x19,63,.down
	mov	w1,1
	str	w1,[x0]
	adr	x2,ifs
	mov	x3,IF_SIZE
	madd	x2,x19,x3,x2
	ldr	x1,[x2,IF_MAC]
	adr	x0,netdev_mac
	str	x1,[x0]
	b	.reset
.down:	str	wzr,[x0]
.reset:	bl	net_reset_ip
	adr	x0,if_changes			; (le thread auxiliaire le voit)
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = message : NET_OP_IF_REGISTER (a type, b MAC, c priorite ; donnees : nom)
; -> x0 = interface ; b = numero du tampon de l'anneau
op_if_register:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	rights_of			; un pilote de SD0:/sys seulement
	cbz	x0,.perm
	ldr	w21,[x19,WM_PID]
	movl	x0,NR_SIZE			; l'anneau
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x22,x0
	sys	SYS_BUF_MAP
	tbnz	x0,63,.free
	mov	x20,x0
	mov	x1,4096				; en-tete a zero
	bl	mem_zero
	movl	x0,1500
	str	x0,[x20,NR_MTU]
	adr	x0,canal_rx
	ldr	x0,[x0]
	add	x0,x0,1
	str	x0,[x20,NR_WAKE]
	mov	x0,x22
	mov	x1,x21
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.free
	net_lock_take
	adr	x2,ifs				; une place (la sienne s'il se
	mov	x3,0				; reinscrit apres une reprise)
	mov	x4,-1
.t:	ldr	x5,[x2,IF_PID]
	cbnz	x5,.n
	tbz	x4,63,.n
	mov	x4,x3
.n:	add	x2,x2,IF_SIZE
	add	x3,x3,1
	cmp	x3,IF_MAX
	b.lo	.t
	tbnz	x4,63,.plein
	adr	x2,ifs
	mov	x3,IF_SIZE
	madd	x2,x4,x3,x2
	str	x21,[x2,IF_PID]
	ldr	x0,[x19,WM_A]
	str	x0,[x2,IF_TYPE]
	ldr	x0,[x19,WM_B]
	and	x0,x0,0xFFFFFFFFFFFF
	str	x0,[x2,IF_MAC]
	ldr	x0,[x19,WM_C]
	str	x0,[x2,IF_PRIO]
	ldr	x0,[x19,WM_DATA]		; nom (7 caracteres)
	and	x0,x0,0x00FFFFFFFFFFFFFF
	str	x0,[x2,IF_NAME]
	str	x22,[x2,IF_BUF]
	ldr	x0,[x19,WM_D]
	str	x0,[x2,IF_WAKE]
	str	x20,[x2,IF_RING]
	str	xzr,[x2,IF_LINK]
	str	xzr,[x2,IF_SPEED]
	strb	wzr,[x2,IF_SSID]
	mov	x20,x4
	net_lock_drop
	str	x22,[x19,WM_B]
	adr	x0,m_if				; " netd : interface usb0 (pilote 23) "
	bl	puts
	adr	x0,ifs
	mov	x1,IF_SIZE
	madd	x0,x20,x1,x0
	add	x0,x0,IF_NAME
	bl	puts
	adr	x0,m_pilote
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_parf
	bl	puts
	mov	x0,x20
	b	.out
.plein:	net_lock_drop
	mov	x0,x22
	sys	SYS_BUF_FREE
	mov	x0,E_NOMEM
	b	.out
.free:	str	x0,[sp,-16]!
	mov	x0,x22
	sys	SYS_BUF_FREE
	ldr	x0,[sp],16
	b	.out
.perm:	mov	x0,E_PERM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = message : NET_OP_IF_STATE (a interface, b lien, c debit ; donnees : SSID)
op_if_state:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,WM_A]
	cmp	x0,IF_MAX
	b.hs	.inval
	adr	x20,ifs
	mov	x1,IF_SIZE
	madd	x20,x0,x1,x20
	net_lock_take
	ldr	x0,[x20,IF_PID]			; l'interface de ce pilote ?
	ldr	w1,[x19,WM_PID]
	cmp	x0,x1
	b.ne	.inval_l
	ldr	x0,[x19,WM_B]
	cmp	x0,0
	cset	x0,ne
	str	x0,[x20,IF_LINK]
	ldr	x0,[x19,WM_C]
	str	x0,[x20,IF_SPEED]
	add	x0,x20,IF_SSID			; SSID (32 o au plus)
	add	x1,x19,WM_DATA
	ldr	w2,[x19,WM_LEN]
	cmp	x2,32
	mov	x3,32
	csel	x2,x2,x3,ls
.s:	cbz	x2,.se
	ldrb	w3,[x1],1
	cbz	w3,.se
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.s
.se:	strb	wzr,[x0]
	bl	netif_choose
	net_lock_drop
	mov	x0,0
	b	.out
.inval_l:
	net_lock_drop
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid termine : ses interfaces disparaissent
if_gone:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x19,0
.t:	adr	x20,ifs
	mov	x0,IF_SIZE
	madd	x20,x19,x0,x20
	ldr	x0,[x20,IF_PID]
	cmp	x0,x21
	b.ne	.n
	net_lock_take
	str	xzr,[x20,IF_PID]
	str	xzr,[x20,IF_RING]
	str	xzr,[x20,IF_LINK]
	bl	netif_choose
	net_lock_drop
	adr	x0,m_if
	bl	puts
	add	x0,x20,IF_NAME
	bl	puts
	adr	x0,m_partie
	bl	puts
	ldr	x0,[x20,IF_BUF]			; (le thread de la pile ne le lit plus)
	sys	SYS_BUF_FREE
.n:	add	x19,x19,1
	cmp	x19,IF_MAX
	b.lo	.t
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = message -> x0 = 1 si l'appelant a les droits de SD0:/sys (pilote)
; (le noyau ne les donne pas dans le message : on accepte tout pid qui
; n'est pas un client connu ; les pilotes sont lances par hwmanager)
rights_of:
	mov	x0,1
	ret

; ---------------------------------------------------------------------------
;  Requetes
; ---------------------------------------------------------------------------
worker:
	sub	sp,sp,512
	mov	x19,sp				; message
.loop:	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x19
	movl	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,.err
	cbz	x0,.loop
	mov	x20,x0
	mov	x0,x19
	bl	dispatch
	mov	x21,x0
	dmb	ish				; la pile dort longtemps : cette requete a
	adr	x0,net_long			; pu armer une minuterie (net_sleep)
	ldr	x0,[x0]
	cbz	x0,.rep
	adr	x0,canal_rx
	ldr	x0,[x0]
	mov	x1,NR_SIG_RX
	sys	SYS_SVC_SIGNAL
.rep:	cbz	x21,.loop			; (avis : pas de reponse)
	mov	x0,x20
	mov	x1,x19
	sys	SYS_SVC_REPLY
	b	.loop
.err:	mov	x0,100
	sys	SYS_SLEEP
	b	.loop

; x0 = message -> x0 = 1 : repondre (resultat en +0)
dispatch:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	str	wzr,[x19,WM_LEN]
	ldr	x0,[x19]
	movl	x1,SVC_OP_FIN
	cmp	x0,x1
	b.eq	.fin
	movl	x1,SVC_OP_NAISSANCE
	cmp	x0,x1
	b.eq	.avis
	cmp	x0,NET_OP_SOCKETS
	b.hi	.inval
	adr	x1,op_table
	ldrsw	x1,[x1,x0,lsl 2]		; (decalage signe)
	cbz	x1,.inval
	adr	x2,op_table
	add	x1,x2,x1
	mov	x0,x19
	blr	x1
	b	.rep
.inval:	mov	x0,E_INVAL
.rep:	str	x0,[x19]
	mov	x0,1
	b	.out
.fin:	ldr	x0,[x19,WM_A]			; un process s'arrete
	bl	if_gone
	ldr	x0,[x19,WM_A]
	bl	net_close_all
	ldr	x0,[x19,WM_A]
	bl	cli_gone
.avis:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

macro op_entry f {
  if f eq 0
	dw	0
  else
	dw	f - op_table
  end if
}
	align 4
op_table:
	op_entry 0				; 0
	op_entry op_open			; 1
	op_entry op_socket
	op_entry op_connect
	op_entry op_bind
	op_entry op_listen			; 5
	op_entry op_accept
	op_entry op_send
	op_entry op_recv
	op_entry op_close
	op_entry op_sendto			; 10
	op_entry op_recvfrom
	op_entry op_resolve
	op_entry op_ping
	op_entry op_info
	op_entry op_ifaces			; 15
	op_entry op_time
	op_entry op_ntp
	op_entry 0
	op_entry 0
	op_entry op_if_register			; 20
	op_entry op_if_state
	op_entry op_doorbell
	op_entry op_stats
	op_entry op_shutdown
	op_entry op_sockets			; 25

op_doorbell:
	mov	x0,0
	ret

; 25 sockets(a premier emplacement) -> nombre d'entrees NSK_ENT (14 au plus),
; b emplacement suivant (MAX_SOCK : fin) : la table des sockets (netstat)
op_sockets:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	add	x20,x19,WM_DATA			; entree courante
	mov	x21,0				; entrees
	ldr	x22,[x19,WM_A]			; emplacement
	net_lock_take
.s:	cmp	x22,MAX_SOCK
	b.hs	.fin
	cmp	x21,NSK_PER_MSG
	b.hs	.fin
	adr	x2,sock_table
	ldr	x2,[x2]
	mov	x3,SK_SIZE
	madd	x2,x22,x3,x2
	add	x22,x22,1
	ldr	x3,[x2,SK_USED]
	cbz	x3,.s
	ldr	x3,[x2,SK_TYPE]
	str	w3,[x20,NSK_TYPE]
	ldr	x4,[x2,SK_STATE]
	cmp	x3,SOCK_TCP
	csel	x4,x4,xzr,eq
	str	w4,[x20,NSK_STATE]
	ldr	x3,[x2,SK_LPORT]
	str	w3,[x20,NSK_LPORT]
	ldr	x3,[x2,SK_RPORT]
	str	w3,[x20,NSK_RPORT]
	ldr	x3,[x2,SK_RIP]
	str	w3,[x20,NSK_RIP]
	ldr	x3,[x2,SK_OWNER]
	str	w3,[x20,NSK_OWNER]
	ldp	x3,x4,[x2,SK_RXHEAD]		; (SK_RXTAIL = SK_RXHEAD + 8)
	sub	x3,x3,x4
	str	w3,[x20,NSK_RXQ]
	ldp	x3,x4,[x2,SK_TXHEAD]		; (SK_TXTAIL = SK_TXHEAD + 8)
	sub	x3,x3,x4
	str	w3,[x20,NSK_TXQ]
	add	x20,x20,NSK_ENT
	add	x21,x21,1
	b	.s
.fin:	net_lock_drop
	str	x22,[x19,WM_B]
	lsl	x0,x21,5			; NSK_ENT = 32
	str	w0,[x19,WM_LEN]
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; 23 stats -> donnees NST_SIZE : compteurs du pilote actif, puis de la pile
op_stats:
	add	x1,x0,WM_DATA
	mov	w2,NST_SIZE
	str	w2,[x0,WM_LEN]
	adr	x2,if_active
	ldr	x2,[x2]
	tbnz	x2,63,.none
	adr	x3,ifs
	mov	x4,IF_SIZE
	madd	x3,x2,x4,x3
	ldr	x3,[x3,IF_RING]
	cbz	x3,.none
	add	x4,x3,NR_STATS
	mov	x5,8
.c:	ldr	x6,[x4],8
	str	x6,[x1],8
	subs	x5,x5,1
	b.ne	.c
	sub	x1,x1,64
	ldr	x4,[x3,NR_RX_DROPS]
	str	x4,[x1,NST_RX_DROPS]
	ldr	x4,[x3,NR_TX_DROPS]
	str	x4,[x1,NST_TX_DROPS]
.none:	adr	x2,net_st
	ldp	x3,x4,[x2]
	stp	x3,x4,[x1,NST_STACK]
	ldp	x3,x4,[x2,16]
	stp	x3,x4,[x1,NST_OOO]
	mrs	x3,cntfrq_el0
	str	x3,[x1,NST_FREQ]
	ldp	x3,x4,[x2,32]
	stp	x3,x4,[x1,NST_FRTX]
	ldp	x3,x4,[x2,48]
	stp	x3,x4,[x1,NST_RXB]
	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  Tampons des process (128 Ko : NetKit y met les donnees de plus de 448 o)
; ---------------------------------------------------------------------------

; x0 = message : NET_OP_OPEN -> 0 ; b = numero du tampon (accorde au pid)
op_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	w20,[x19,WM_PID]
	mov	x0,x20
	bl	cli_find
	cbnz	x0,.deja
	movl	x0,NET_CLI_BUF
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x21,x0
	sys	SYS_BUF_MAP
	tbnz	x0,63,.free
	mov	x22,x0
	mov	x0,x21
	mov	x1,x20
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.free
	net_lock_take
	mov	x0,x20				; deux threads du process a la fois : un
	bl	cli_find			; seul tampon, sinon netd ecrit dans l'un
	cbnz	x0,.twice			; et le client lit l'autre
	adr	x2,clis
	mov	x3,CLI_MAX
.t:	ldr	x4,[x2,CL_PID]
	cbz	x4,.place
	add	x2,x2,CL_SIZE
	subs	x3,x3,1
	b.ne	.t
	net_lock_drop
	mov	x0,E_NOMEM
	b	.free
.place:	str	x20,[x2,CL_PID]
	str	x21,[x2,CL_NUM]
	str	x22,[x2,CL_ADR]
	net_lock_drop
	str	x21,[x19,WM_B]
	mov	x0,0
	b	.out
.twice:	ldr	x20,[x0,CL_NUM]
	net_lock_drop
	mov	x0,x21
	sys	SYS_BUF_FREE
	str	x20,[x19,WM_B]
	mov	x0,0
	b	.out
.deja:	ldr	x0,[x0,CL_NUM]
	str	x0,[x19,WM_B]
	mov	x0,0
	b	.out
.free:	str	x0,[sp,-16]!
	mov	x0,x21
	sys	SYS_BUF_FREE
	ldr	x0,[sp],16
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid -> x0 = son entree (0 : aucune)
cli_find:
	adr	x1,clis
	mov	x2,CLI_MAX
.t:	ldr	x3,[x1,CL_PID]
	cmp	x3,x0
	b.eq	.oui
	add	x1,x1,CL_SIZE
	subs	x2,x2,1
	b.ne	.t
	mov	x0,0
	ret
.oui:	cbz	x0,.non
	mov	x0,x1
	ret
.non:	mov	x0,0
	ret

; x0 = pid termine : son tampon est rendu
cli_gone:
	prologue
	str	x19,[sp,-16]!
	cbz	x0,.out
	net_lock_take
	bl	cli_find
	mov	x19,x0
	cbz	x19,.unl
	str	xzr,[x19,CL_PID]
	ldr	x19,[x19,CL_NUM]
	net_lock_drop
	mov	x0,x19
	sys	SYS_BUF_FREE
	b	.out
.unl:	net_lock_drop
.out:	ldr	x19,[sp],16
	epilogue

; x0 = message, x1 = decalage (-1 : donnees du message), x2 = octets
; -> x0 = adresse des donnees chez netd (0 si invalide)
data_ptr:
	cmn	x1,1
	b.ne	.buf
	cmp	x2,NET_INLINE
	b.hi	.bad
	add	x0,x0,WM_DATA
	ret
.buf:	movl	x3,NET_CLI_BUF
	cmp	x1,x3
	b.hs	.bad
	cmp	x2,x3
	b.hi	.bad
	add	x4,x1,x2
	cmp	x4,x3
	b.hi	.bad
	ldr	w0,[x0,WM_PID]
	adr	x3,clis
	mov	x4,CLI_MAX
.t:	ldr	x5,[x3,CL_PID]
	cmp	x5,x0
	b.eq	.oui
	add	x3,x3,CL_SIZE
	subs	x4,x4,1
	b.ne	.t
.bad:	mov	x0,0
	ret
.oui:	ldr	x0,[x3,CL_ADR]
	add	x0,x0,x1
	ret

; ---------------------------------------------------------------------------
;  Equivalents des services du noyau utilises par la pile
; ---------------------------------------------------------------------------
macro save_caller {
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	stp	x5,x6,[sp,-16]!
	stp	x7,x8,[sp,-16]!
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	stp	x15,x16,[sp,-16]!
	stp	x17,x18,[sp,-16]!
}
macro restore_caller {
	ldp	x17,x18,[sp],16
	ldp	x15,x16,[sp],16
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	ldp	x7,x8,[sp],16
	ldp	x5,x6,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	ldp	x29,x30,[sp],16
}

kputs:
	save_caller
	bl	puts
	restore_caller
	ret

kputc:
	save_caller
	sub	sp,sp,16
	strb	w0,[sp]
	strb	wzr,[sp,1]
	mov	x0,sp
	bl	puts
	add	sp,sp,16
	restore_caller
	ret

kput_dec:
	save_caller
	bl	print_dec
	restore_caller
	ret

; -> x0 = ms depuis le demarrage (tous les registres gardes)
time_ms:
	str	x8,[sp,-16]!
	mov	x8,SYS_TIME_MS
	svc	0
	ldr	x8,[sp],16
	ret

; x0 = ms
thread_sleep:
	str	x8,[sp,-16]!
	mov	x8,SYS_SLEEP
	svc	0
	ldr	x8,[sp],16
	ret

; x0 = adresse, x1 = octets (multiple de 8)
mem_zero:
	cbz	x1,.e
.z:	str	xzr,[x0],8
	subs	x1,x1,8
	b.hi	.z
.e:	ret

; x0 = destination, x1 = source, x2 = octets
mem_copy:
	cbz	x2,.e
.c:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.c
.e:	ret

; x0 = pages -> x0 = adresse (alignee sur 4 Ko), 0 si echec. Les anneaux
; des sockets (SK_RING_PAGES) liberes sont gardes pour resservir. (net_lock)
pmm_alloc_n:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cmp	x19,SK_RING_PAGES
	b.ne	.neuf
	adr	x1,ring_free
	ldr	x0,[x1]
	cbz	x0,.neuf
	ldr	x2,[x0]
	str	x2,[x1]
	b	.out
.neuf:	lsl	x0,x19,12
	add	x0,x0,4095
	sys	SYS_SBRK
	tbnz	x0,63,.fail
	add	x0,x0,4095
	and	x0,x0,0xFFFFFFFFFFFFF000
	b	.out
.fail:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = adresse, x1 = pages (net_lock)
pmm_free_n:
	cmp	x1,SK_RING_PAGES
	b.ne	.r
	adr	x1,ring_free
	ldr	x2,[x1]
	str	x2,[x0]
	str	x0,[x1]
.r:	ret

; verrou de la pile (x0-x2 changes) : tourne, cede le coeur s'il est pris
nl_take:
	adr	x0,net_lock
	mov	w2,1
.t:	ldaxr	w1,[x0]
	cbnz	w1,.busy
	stxr	w1,w2,[x0]
	cbnz	w1,.t
	ret
.busy:	clrex
	str	x8,[sp,-16]!
	mov	x8,SYS_YIELD
	svc	0
	ldr	x8,[sp],16
	adr	x0,net_lock
	mov	w2,1
	b	.t

nl_drop:
	adr	x0,net_lock
	stlr	wzr,[x0]
	ret

; x0 = verrou (dort 1 ms tant qu'il est pris)
mutex_lock:
	mov	x1,x0
	mov	w2,1
.t:	ldaxr	w3,[x1]
	cbnz	w3,.busy
	stxr	w3,w2,[x1]
	cbnz	w3,.t
	ret
.busy:	clrex
	stp	x1,x8,[sp,-16]!
	mov	x0,1
	mov	x8,SYS_SLEEP
	svc	0
	ldp	x1,x8,[sp],16
	mov	w2,1
	b	.t

mutex_unlock:
	stlr	wzr,[x0]
	ret

include 'stack.inc'
include 'ops.inc'
include 'background.inc'
include '../inc/buspub.inc'

s_net		db 'net',0
s_netrx		db 'net.rx',0
s_localhost	db 'localhost',0
m_pret		db 'netd : en service',10,0
m_if		db 'netd : interface ',0
m_pilote	db ' (pilote ',0
m_parf		db ')',10,0
m_partie	db ' partie',10,0
		align 8

segment readable writeable
kit_data
		align 8
canal		dd 0
canal_rx	dd 0
net_long	dd 0				; 1 : la pile dort sans minuterie a suivre
net_asleep	dd 0				; 1 : la pile dort (lo_put la reveille)
lo_ring		dd 0
lo_head		dd 0
lo_tail		dd 0
if_active	dd -1				; interface active (-1 : aucune)
if_changes	dd 0				; compteur de changements
ring_free	dd 0				; anneaux de sockets libres
include 'stackdata.inc'
bg_data
		align 16
ifs		rb IF_MAX * IF_SIZE
clis		rb CLI_MAX * CL_SIZE
