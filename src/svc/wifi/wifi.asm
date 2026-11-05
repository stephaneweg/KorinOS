; wifi - le Wi-Fi du Raspberry Pi 4 (puce CYW43455 sur SDIO, hote Arasan
; EMMC1) en service (docs/RESEAU.md etape 5, docs/WIFI.md), lance par
; hwmanager (cle soc:wifi de drivers.txt) sur un Pi 4 / Pi 400 / CM4.
;  * mise en route de la puce (chip.inc, sdio.inc : repris du noyau) ;
;    firmware lu dans SD0:/firmware/ par AppKit ;
;  * interface wlan0 de netd (priorite 20), anneau de trames (netif.inc) ;
;  * reseaux connus : SD0:/etc/wifi.txt (ssid=, key=, priority=, proto=,
;    key_mgmt= ; country= ; anciennes cles reseau=, cle=, pays= lues aussi),
;    16 au plus ; le pays vient d'abord de pays= de SD0:/etc/options.txt ;
;  * connexion automatique : au demarrage, le reseau connu prefere sans
;    balayage (docs/BOOT.md) ; puis balayage, reseaux connus presents tries par
;    priorite puis signal, essayes l'un apres l'autre ; lien perdu : le meme
;    reseau apres 2 s, puis le cycle ; ensuite un balayage toutes les 5 s,
;    10 s... jusqu'a 60 s ;
;  * service "wifi" (ops de inc/netd.inc, docs/RESEAU.md 4.3) : etat,
;    balayage, connexion (memorisee), deconnexion, oubli, reseaux connus,
;    priorite, pays ;
;  * l'etat publie sur le bus (docs/BUS.md) : korin.net.wifi, « <etat>
;    <barres> <ssid> », quand il change ; le signal est relu toutes les 5 s
;    au plus, par ce thread seul (les clients ne demandent plus l'etat) ;
;  * jamais de cle au journal : seulement les SSID.
; Fin 0 (pas de relance) sous QEMU, ou si le controleur, la puce ou le
; firmware manquent ; fin 1 (relance par hwmanager) sur un echec peut-etre
; passager.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/netd.inc'
include '../../inc/bus.inc'
include '../../kits/buskit/buskit.inc'
include 'sdhci.inc'

GPIO_BASE	= 0xFE200000
GP_FSEL0	= 0x00
GP_PUPPDN0	= 0xE4			; BCM2711 : 2 bits par broche
SRV_THREADS	= 3			; threads du service
WL_ON_GPIO	= 129			; WL_ON : ligne 1 de l'extenseur (128 + 1)
WL_BUSY_MS	= 50			; ms sans trame avant de revenir au repos
WL_IRQ		= 158			; Arasan (ligne partagee avec l'EMMC2)
WL_SIG_IRQ	= 1			; bit de wifi.io ; 2 : NIF_SIG_TX
WL_WAIT_POLL	= 1			; ms : tant qu'aucune interruption n'est venue
WL_WAIT_IRQ	= 200			; ms : au repos, en cas d'oubli
SD_INT_CARD	= 1 shl 8		; interruption de la carte SDIO
TSTACK		= 32768
FILE_MAX	= 1048576		; plus gros fichier lu (firmware : ~550 Ko)
CFG_MAX		= 8192			; wifi.txt
NET_MAX		= 16			; reseaux connus
KN_SSID		= 0			; 33 o
KN_KEY		= 36			; 65 o
KN_PRIO		= 104
KN_SEEN		= 108			; (balayage) RSSI + 1000, 0 : pas vu
KN_SIZE		= 128
E_NOENT		= -2
E_IO		= -5
E_NOMEM		= -12
E_ACCES		= -13
E_INVAL		= -22
E_TIMEDOUT	= -110

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0				; argument "test" : sans la puce (essai
	bl	kits_open			; des reseaux connus et du service, QEMU)
	cbz	x19,.materiel
	mov	x0,x19
	adr	x1,s_test
	bl	prefix
	cbz	x0,.materiel
	adr	x0,m_test
	bl	puts
	adr	x0,sim
	mov	w1,1
	str	w1,[x0]
	mov	w0,WS_READY			; (une puce simulee, pour les essais
	adr	x1,wl_state			;  de l'applet et de wifimenu)
	str	w0,[x1]
	movl	x0,8192
	sys	SYS_SBRK
	adr	x1,wl_scan
	str	x0,[x1]
	bl	cfg_load
	adr	x0,s_wifi
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,.absent
	adr	x1,canal
	str	x0,[x1]
	adr	x0,srv_thread
	bl	spawn_thread
	adr	x0,srv_thread
	bl	spawn_thread
	b	pub_thread			; ce thread publie l'etat
.materiel:
	adr	x0,fw_rev			; QEMU : puce absente, et la carte SD y
	sys	SYS_FW_CALL			; est sur l'Arasan : ne rien toucher
	tbnz	x0,63,.absent
	adr	x0,fw_rev
	ldr	w0,[x0,20]
	movl	w1,0x50000000
	cmp	w0,w1
	b.lo	.absent
	movl	x0,GPIO_BASE			; registres : GPIO, Arasan
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.absent
	adr	x1,gpio_va
	str	x0,[x1]
	movl	x0,WL_HC
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.absent
	adr	x1,wl_hc_va
	str	x0,[x1]
	movl	x0,40960 + 4096			; tampons de la puce
	sys	SYS_SBRK
	tbnz	x0,63,.nomem
	add	x0,x0,4095
	and	x0,x0,0xFFFFFFFFFFFFF000
	adr	x1,wl_txbuf
	str	x0,[x1]
	add	x0,x0,8192
	adr	x1,wl_rxbuf
	str	x0,[x1]
	add	x0,x0,8192
	adr	x1,wl_iobuf
	str	x0,[x1]
	add	x0,x0,4096
	adr	x1,wl_scan
	str	x0,[x1]
	add	x0,x0,4096
	adr	x1,wl_work
	str	x0,[x1]
	add	x0,x0,8192
	adr	x1,wl_txframe
	str	x0,[x1]
	mov	w0,WS_STARTING
	adr	x1,wl_state
	str	w0,[x1]
	bl	cfg_load			; reseaux connus, pays
	adr	x0,s_wifi			; service "wifi" (etat : demarrage)
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,.absent
	adr	x1,canal
	str	x0,[x1]
	mov	x19,SRV_THREADS
.t:	adr	x0,srv_thread
	bl	spawn_thread
	subs	x19,x19,1
	b.ne	.t
	bl	wl_bringup			; la puce
	cbz	x0,.echec
	bl	wl_setup
	cbz	x0,.echec_io
	mov	w0,WS_READY
	adr	x1,wl_state
	str	w0,[x1]
	bl	wifi_irq_open
	mov	x0,NIF_WIFI			; interface wlan0 de netd
	adr	x1,wl_mac
	ldr	x1,[x1]
	mov	x2,NIF_PRIO_WIFI
	adr	x3,s_wlan0
	bl	netif_register
	adr	x0,conn_thread			; connexion automatique
	bl	spawn_thread
	adr	x0,pub_thread			; l'etat sur le bus
	bl	spawn_thread
	b	wl_io_loop			; ce thread sert la puce
.echec_io:
	adr	x0,wl_retry
	mov	w1,1
	str	w1,[x0]
.echec:	adr	x0,wl_msg_fail
	bl	puts
	adr	x1,wl_state
	str	wzr,[x1]
	adr	x0,wl_retry
	ldr	w0,[x0]
	sys	SYS_EXIT			; (0 : materiel absent, pas de relance)
.nomem:	mov	x0,1
	sys	SYS_EXIT
.absent:
	adr	x0,m_absent
	bl	puts
	mov	x0,0
	sys	SYS_EXIT

; la boucle de la puce
wl_io_loop:
.loop:	adr	x0,wl_mutex
	bl	mutex_lock
	bl	wl_poll
	mov	x19,x0
	bl	netif_tick
	bl	wifi_stats
	adr	x0,wl_mutex
	bl	mutex_unlock
	mov	x0,x19
	bl	wifi_pace
	b	.loop

; x0 = interruptions vues : sans pause pendant le trafic, 1 ms au repos
wifi_pace:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x2,wl_timed			; la puce a du travail apres un delai :
	ldr	x3,[x2]				; une interruption a manque, on resonde
	str	xzr,[x2]
	cbz	x3,.tx
	cbz	x19,.tx
	adr	x2,wl_wait
	mov	x3,WL_WAIT_POLL
	str	x3,[x2]
.tx:	bl	netif_tx_pending
	orr	x19,x19,x0
	sys	SYS_TIME_MS
	adr	x1,wl_busy_t
	cbz	x19,.calm
	str	x0,[x1]
.calm:	ldr	x1,[x1]
	sub	x0,x0,x1
	cmp	x0,WL_BUSY_MS
	b.hs	.idle
	sys	SYS_YIELD
	b	.out
.idle:	adr	x0,wl_io
	ldr	x0,[x0]
	cbnz	x0,.irq
	mov	x0,1
	sys	SYS_SLEEP
	b	.out
.irq:	bl	wifi_wait
.out:	ldr	x19,[sp],16
	epilogue

; Le service wifi.io et l'interruption de la carte (avant netif_register : nif_wake)
wifi_irq_open:
	prologue
	adr	x0,s_wifi_io
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,.out
	add	x3,x0,1
	mov	x1,x0
	mov	x0,WL_IRQ
	movl	x2,WL_SIG_IRQ or IRQ_SHARED
	str	x3,[sp,-16]!
	sys	SYS_IRQ_BIND
	ldr	x3,[sp],16
	tbnz	x0,63,.out
	adr	x1,wl_io
	str	x3,[x1]
	adr	x1,nif_wake
	str	x3,[x1]
	adr	x1,wl_wait
	mov	x2,WL_WAIT_POLL
	str	x2,[x1]
.out:	epilogue

; Au repos : la carte peut interrompre, on dort sur wifi.io (interruption, ou
; trame a emettre). Le bit CARD reste leve lignes DAT hautes : on l'efface en
; coupant son activation. Il ne monte que sur un front : ce qui attendait deja
; avant d'armer ne dira plus rien, on relit donc la puce apres avoir arme. Tant
; qu'aucune interruption n'est venue, 1 ms, comme un sondage ; si la puce a du
; travail apres un delai, l'interruption a manque (wifi_pace).
wifi_wait:
	prologue
	str	x19,[sp,-16]!
	mov	x0,SD_IRPT_MASK
	mov	w1,-1
	and	w1,w1,not SD_INT_CARD
	bl	wl_wr
	mov	x0,SD_IRPT_MASK
	mov	w1,-1
	bl	wl_wr
	mov	x0,SD_INTERRUPT
	mov	w1,SD_INT_CARD
	bl	wl_wr
	mov	x0,SD_IRPT_EN
	mov	w1,SD_INT_CARD
	bl	wl_wr
	adr	x0,wl_mutex
	bl	mutex_lock
	mov	w0,CCCR_INTPEND
	bl	sdio_f0r
	mov	x19,x0
	adr	x0,wl_mutex
	bl	mutex_unlock
	cbnz	x19,.coupe
	adr	x0,wl_io
	ldr	x0,[x0]
	sub	x0,x0,1
	adr	x1,wl_iomsg
	adr	x2,wl_wait
	ldr	x2,[x2]
	sys	SYS_SVC_NEXT
	mov	x19,x0
.coupe:	adr	x9,wl_hc_va			; la carte, ou l'autre pilote de la ligne ?
	ldr	x9,[x9]
	ldr	w0,[x9,SD_INTERRUPT]
	str	x0,[sp,-16]!
	mov	x0,SD_IRPT_EN
	mov	w1,0
	bl	wl_wr
	ldr	x3,[sp],16
	cmp	x19,0
	b.eq	.delai
	b.lt	.out
	adr	x1,wl_iomsg
	ldp	x0,x1,[x1]
	movl	x2,SVC_OP_SIGNAL
	cmp	x0,x2
	b.ne	.out
	tbz	x1,0,.out			; (NIF_SIG_TX seul)
	str	x3,[sp,-16]!
	mov	x0,WL_IRQ
	sys	SYS_IRQ_ACK
	ldr	x3,[sp],16
	tbz	x3,8,.out			; (l'autre pilote : la carte SD)
	adr	x1,wl_wait
	mov	x2,WL_WAIT_IRQ
	str	x2,[x1]
	b	.out
.delai:	adr	x1,wl_timed
	mov	x2,1
	str	x2,[x1]
.out:	ldr	x19,[sp],16
	epilogue

; (diagnostic) des erreurs du controleur depuis le dernier passage ?
wifi_stats:
	prologue
	sys	SYS_TIME_MS			; l'etat de la liaison, toutes les 2 s
	adr	x1,wl_link_t
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.keep
	add	x0,x0,2000
	str	x0,[x1]
	adr	x0,wl_net_on
	ldr	w0,[x0]
	cbz	w0,.keep
	bl	wl_link_read
.keep:	adr	x0,nif_ring			; les compteurs, pour NET_OP_STATS
	ldr	x0,[x0]
	cbz	x0,.nor
	add	x0,x0,NR_STATS
	adr	x1,wl_st
	mov	x2,8
.cp:	ldr	x3,[x1],8
	str	x3,[x0],8
	subs	x2,x2,1
	b.ne	.cp
.nor:	adr	x0,wl_errs
	ldr	x1,[x0]
	adr	x2,wl_errshown
	ldr	x3,[x2]
	cmp	x1,x3
	b.eq	.out
	mov	x4,x1
	sub	x3,x1,x3
	cmp	x3,8
	b.lo	.out
	str	x4,[x2]
	adr	x0,m_err1
	bl	puts
	adr	x0,wl_errs
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_err2
	bl	puts
	adr	x0,wl_lasterr
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_err3
	bl	puts
	adr	x0,wl_clk_base
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_err4
	bl	puts
.out:	epilogue

; x0 = entree : un thread avec sa pile -> x0 = tid
spawn_thread:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,TSTACK + 16
	sys	SYS_SBRK
	tbnz	x0,63,.out
	add	x1,x0,TSTACK
	and	x1,x1,0xFFFFFFFFFFFFFFF0
	mov	x0,x19
	mov	x2,0
	sys	SYS_THREAD_CREATE
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Service "wifi"
; ---------------------------------------------------------------------------
srv_thread:
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
	str	wzr,[x19,WM_LEN]
	ldr	x0,[x19]
	cmp	x0,WIFI_OP_COUNTRY
	b.hi	.inval
	cbz	x0,.inval
	adr	x1,srv_table
	ldrsw	x1,[x1,x0,lsl 2]
	adr	x2,srv_table
	add	x1,x2,x1
	mov	x0,x19
	blr	x1
	b	.rep
.inval:	mov	x0,E_INVAL
.rep:	str	x0,[x19]
	mov	x0,x20
	mov	x1,x19
	sys	SYS_SVC_REPLY
	b	.loop
.err:	mov	x0,100
	sys	SYS_SLEEP
	b	.loop

	align 4
srv_table:
	dw	0
	dw	op_status - srv_table		; 1
	dw	op_scan - srv_table
	dw	op_join - srv_table
	dw	op_leave - srv_table
	dw	op_forget - srv_table		; 5
	dw	op_known - srv_table
	dw	op_priority - srv_table
	dw	op_country - srv_table		; 8

; -> x0 = 0 si la puce est utilisable, E_NODEV sinon
wl_check_ready:
	adr	x0,wl_state
	ldr	w0,[x0]
	cmp	w0,WS_READY
	b.lo	.no
	mov	x0,0
	ret
.no:	mov	x0,E_NODEV
	ret

; --- 1 etat -> donnees 64 o : etat, RSSI (+4), MAC (+8), SSID (+16), long. (+48)
op_status:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,16
	add	x19,x0,WM_DATA
	mov	w1,64
	str	w1,[x0,WM_LEN]
	mov	x0,x19
	mov	x1,64
	bl	mem_zero
	adr	x0,wl_state
	ldr	w20,[x0]
	str	w20,[x19]
	cmp	w20,WS_READY
	b.lo	.done
	adr	x0,wl_mac
	ldr	x0,[x0]
	str	w0,[x19,8]
	lsr	x0,x0,32
	strh	w0,[x19,12]
	cmp	w20,WS_CONNECTED
	b.ne	.done
	adr	x0,sim
	ldr	w0,[x0]
	cbz	w0,.ssid
	mov	w0,-52				; (simulation)
	str	w0,[x19,4]
	adr	x0,wl_ssid
	mov	x1,0
	b	.s
.ssid:	adr	x0,wl_ssid
	mov	x1,0
.s:	ldrb	w2,[x0,x1]
	cbz	w2,.slen
	add	x3,x19,16
	strb	w2,[x3,x1]
	add	x1,x1,1
	cmp	x1,32
	b.lo	.s
.slen:	str	w1,[x19,48]
	adr	x0,sim
	ldr	w0,[x0]
	cbnz	w0,.done
	adr	x0,wl_mutex			; force du signal
	bl	mutex_lock
	str	wzr,[sp]
	mov	w0,WLC_GET_RSSI
	mov	x1,sp
	mov	x2,4
	mov	x3,4
	mov	w4,0
	bl	wl_ioctl
	adr	x0,wl_mutex
	bl	mutex_unlock
	ldr	w0,[sp]
	str	w0,[x19,4]
.done:	mov	x0,0
	add	sp,sp,16
	ldp	x19,x20,[sp],16
	epilogue

; --- 2 balayage (a premiere entree, b 1 : sans refaire) -> a, x0 = nombre
op_scan:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	wl_check_ready
	cbnz	x0,.out
	ldr	x0,[x19,WM_B]
	cbnz	x0,.copie
	adr	x0,sim				; essai sans la puce : simulation
	ldr	w0,[x0]
	cbz	w0,.reel
	bl	sim_scan
	b	.copie
.reel:	adr	x0,wl_opmutex
	bl	mutex_lock
	bl	wl_do_scan
	mov	x20,x0
	adr	x0,wl_opmutex
	bl	mutex_unlock
	tbnz	x20,63,.err
.copie:	adr	x0,wl_scan_count
	ldr	w20,[x0]
	ldr	x1,[x19,WM_A]			; entrees [a, a + 9)
	cmp	x1,x20
	b.hs	.fin
	sub	x2,x20,x1
	cmp	x2,WIFI_PER_MSG
	mov	x3,WIFI_PER_MSG
	csel	x2,x2,x3,ls
	mov	x3,SE_SIZE
	mul	x4,x2,x3
	str	w4,[x19,WM_LEN]
	adr	x0,wl_scan
	ldr	x0,[x0]
	madd	x1,x1,x3,x0
	add	x0,x19,WM_DATA
	mov	x2,x4
	bl	mem_copy
.fin:	str	x20,[x19,WM_A]
	mov	x0,x20
	b	.out
.err:	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- 3 connexion (donnees : SSID, 0, cle, 0 ; a memoriser, b priorite) ------
op_join:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,112
	mov	x19,x0
	bl	wl_check_ready
	cbnz	x0,.out
	add	x1,x19,WM_DATA			; SSID (32), cle (64)
	mov	x0,sp
	mov	x2,33
	bl	copy_z
	mov	x1,x0				; (apres le 0 du SSID)
	add	x0,sp,40
	mov	x2,65
	bl	copy_z
	ldrb	w0,[sp,40]			; sans cle, un reseau connu : sa cle
	cbnz	w0,.cle
	mov	x0,sp
	bl	known_find
	cbz	x0,.cle
	add	x1,x0,KN_KEY
	add	x0,sp,40
	mov	x2,65
	bl	copy_z
	mov	x0,sp				; (sa priorite gardee)
	bl	known_find
	ldr	w0,[x0,KN_PRIO]
	str	x0,[x19,WM_B]
.cle:	adr	x0,sim				; essai sans la puce : simulation
	ldr	w0,[x0]
	cbz	w0,.reel
	mov	x0,sp
	add	x1,sp,40
	bl	sim_join
	mov	x20,x0
	b	.apres
.reel:
	adr	x0,user_join			; l'essai automatique laisse la place
	mov	w1,1
	str	w1,[x0]
	adr	x0,auto_off
	str	wzr,[x0]
	adr	x0,m_join			; "wifi : connexion a <SSID>"
	bl	puts
	mov	x0,sp
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x0,wl_opmutex
	bl	mutex_lock
	mov	x0,sp
	add	x1,sp,40
	bl	wl_join
	mov	x20,x0
	adr	x0,wl_opmutex
	bl	mutex_unlock
.apres:	adr	x0,user_join
	str	wzr,[x0]
	mov	x0,x20
	bl	log_result
	cbnz	x20,.res
	adr	x0,last_ssid			; (pour reprendre apres une perte)
	mov	x1,sp
	bl	str_copy_n
	ldr	x0,[x19,WM_A]			; memoriser
	cbz	x0,.res
	mov	x0,sp
	add	x1,sp,40
	ldr	x2,[x19,WM_B]
	bl	known_set
	bl	cfg_save
.res:	mov	x0,x20
.out:	add	sp,sp,112
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 4 deconnexion (pas de reconnexion automatique jusqu'au prochain JOIN)
op_leave:
	prologue
	bl	wl_check_ready
	cbnz	x0,.out
	adr	x0,auto_off
	mov	w1,1
	str	w1,[x0]
	adr	x0,sim				; essai sans la puce
	ldr	w0,[x0]
	cbz	w0,.reel
	adr	x0,wl_net_on
	str	wzr,[x0]
	mov	w0,WS_READY
	adr	x1,wl_state
	str	w0,[x1]
	mov	x0,0
	b	.out
.reel:	adr	x0,wl_opmutex
	bl	mutex_lock
	bl	wl_leave
	adr	x0,wl_opmutex
	bl	mutex_unlock
	mov	x0,0
.out:	epilogue

; --- 5 oubli (donnees : SSID) -----------------------------------------------
op_forget:
	prologue
	str	x19,[sp,-16]!
	add	x0,x0,WM_DATA
	strb	wzr,[x0,32]
	bl	known_find
	cbz	x0,.noent
	mov	x19,x0				; les suivants avancent d'une place
	adr	x1,known_n
	ldr	x2,[x1]
	sub	x2,x2,1
	str	x2,[x1]
	adr	x3,known
	mov	x4,KN_SIZE
	madd	x3,x2,x4,x3			; (fin de la table)
.m:	cmp	x19,x3
	b.hs	.save
	mov	x0,x19
	add	x1,x19,KN_SIZE
	mov	x2,KN_SIZE
	bl	mem_copy
	add	x19,x19,KN_SIZE
	b	.m
.save:	bl	cfg_save
	mov	x0,0
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldr	x19,[sp],16
	epilogue

; --- 6 reseaux connus (a premiere) -> a, x0 = nombre ; donnees : WK_* -------
op_known:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,known_n
	ldr	x20,[x0]
	ldr	x21,[x19,WM_A]			; premiere
	add	x22,x19,WM_DATA
	mov	x9,0				; entrees envoyees
.e:	cmp	x21,x20
	b.hs	.fin
	cmp	x9,WIFI_PER_MSG
	b.hs	.fin
	mov	x0,x22
	mov	x1,WK_ENT
	bl	mem_zero
	adr	x1,known
	mov	x2,KN_SIZE
	madd	x1,x21,x2,x1
	mov	x0,x22				; SSID (jamais la cle)
	mov	x3,0
.s:	ldrb	w4,[x1,x3]
	strb	w4,[x0,x3]
	cbz	w4,.sf
	add	x3,x3,1
	cmp	x3,32
	b.lo	.s
.sf:	ldrb	w4,[x1,KN_KEY]			; securite : 0 ouvert, 3 WPA2
	cmp	w4,0
	mov	w4,3
	csel	w4,wzr,w4,eq
	str	w4,[x22,WK_SECU]
	ldr	w4,[x1,KN_PRIO]
	str	w4,[x22,WK_PRIO]
	add	x22,x22,WK_ENT
	add	x21,x21,1
	add	x9,x9,1
	b	.e
.fin:	mov	x0,WK_ENT
	mul	x0,x9,x0
	str	w0,[x19,WM_LEN]
	str	x20,[x19,WM_A]
	mov	x0,x20
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 7 priorite (donnees : SSID ; a priorite) ------------------------------
op_priority:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	add	x0,x19,WM_DATA
	strb	wzr,[x0,32]
	bl	known_find
	cbz	x0,.noent
	ldr	x1,[x19,WM_A]
	str	w1,[x0,KN_PRIO]
	bl	cfg_save
	mov	x0,0
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldr	x19,[sp],16
	epilogue

; --- 8 pays (donnees : "FR") : applique tout de suite ------------------------
op_country:
	prologue
	sub	sp,sp,16
	ldrb	w1,[x0,WM_DATA]
	ldrb	w2,[x0,WM_DATA+1]
	cbz	w1,.inval
	cbz	w2,.inval
	bic	w1,w1,0x20			; majuscules
	bic	w2,w2,0x20
	adr	x3,wl_cfg_cc
	strb	w1,[x3]
	strb	w2,[x3,1]
	strb	wzr,[x3,2]
	bl	wl_check_ready
	cbnz	x0,.out				; (applique a la mise en route)
	bl	apply_country
	b	.out
.inval:	mov	x0,E_INVAL
.out:	add	sp,sp,16
	epilogue

; Pays de wl_cfg_cc a la puce -> x0 = statut
apply_country:
	prologue
	sub	sp,sp,16
	adr	x0,wl_mutex
	bl	mutex_lock
	str	xzr,[sp]			; { pays, revision -1, pays }
	str	wzr,[sp,8]
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
	str	x0,[sp,-16]!
	adr	x0,wl_mutex
	bl	mutex_unlock
	ldr	x0,[sp],16
	add	sp,sp,16
	epilogue

; x0 = resultat d'une connexion : au journal (et une notification si la cle
; est refusee)
log_result:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	adr	x0,m_echec
	bl	puts
	neg	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	cmn	x19,13				; E_ACCES : "Cle refusee pour << X >>"
	b.ne	.out
	adr	x0,nbuf
	strb	wzr,[x0]
	adr	x1,t_cle
	bl	str_cat
	adr	x1,wl_ssid
	bl	str_cat
	adr	x1,t_guil
	bl	str_cat
	adr	x0,t_wifi
	adr	x1,nbuf
	bl	notifier
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  L'etat sur le bus : korin.net.wifi
; ---------------------------------------------------------------------------
pub_thread:
.loop:	mov	x0,250
	sys	SYS_SLEEP
	bl	wifi_publish
	b	.loop

; « <etat> <barres> <ssid> » (barres 0..4, ssid si connecte), publie (gardee)
; s'il a change ; rien tant que busd n'est pas la
wifi_publish:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,16
	mov	x0,0
	bl	bp_open
	cbz	x0,.out
	adr	x0,wl_state
	ldr	w19,[x0]
	mov	x20,0				; barres
	cmp	w19,WS_CONNECTED
	b.eq	.co
	adr	x0,pub_rssi_t			; (connexion suivante : signal relu
	str	xzr,[x0]			;  tout de suite)
	b	.txt
.co:	mov	w21,-52				; (simulation)
	adr	x0,sim
	ldr	w0,[x0]
	cbnz	w0,.lvl
	sys	SYS_TIME_MS
	adr	x1,pub_rssi_t
	ldr	x2,[x1]
	adr	x3,pub_rssi
	ldr	w21,[x3]
	cmp	x0,x2
	b.lo	.lvl
	mov	x2,5000
	add	x0,x0,x2
	str	x0,[x1]
	adr	x0,wl_mutex
	bl	mutex_lock
	str	wzr,[sp]
	mov	w0,WLC_GET_RSSI
	mov	x1,sp
	mov	x2,4
	mov	x3,4
	mov	w4,0
	bl	wl_ioctl
	adr	x0,wl_mutex
	bl	mutex_unlock
	ldr	w21,[sp]
	adr	x3,pub_rssi
	str	w21,[x3]
.lvl:	mov	x20,4				; comme wd_level (inc/wifidraw.inc)
	cmn	w21,55
	b.ge	.txt
	mov	x20,3
	cmn	w21,65
	b.ge	.txt
	mov	x20,2
	cmn	w21,75
	b.ge	.txt
	mov	x20,1
.txt:	adr	x22,pub_val
	mov	x0,x22
	adr	x1,t_st_absent
	cmp	w19,WS_STARTING
	adr	x2,t_st_starting
	csel	x1,x2,x1,eq
	cmp	w19,WS_READY
	adr	x2,t_st_ready
	csel	x1,x2,x1,eq
	cmp	w19,WS_JOINING
	adr	x2,t_st_joining
	csel	x1,x2,x1,eq
	cmp	w19,WS_CONNECTED
	adr	x2,t_st_connected
	csel	x1,x2,x1,eq
	bl	bp_cat
	mov	w1,' '
	strb	w1,[x0],1
	add	w1,w20,'0'
	strb	w1,[x0],1
	cmp	w19,WS_CONNECTED
	b.ne	.end
	mov	w1,' '
	strb	w1,[x0],1
	adr	x1,wl_ssid
	mov	x2,32
.ss:	ldrb	w3,[x1],1
	cbz	w3,.end
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.ss
.end:	strb	wzr,[x0]
	adr	x1,pub_last			; deja publie ?
	mov	x2,0
.cmp:	ldrb	w3,[x22,x2]
	ldrb	w4,[x1,x2]
	cmp	w3,w4
	b.ne	.pub
	add	x2,x2,1
	cbnz	w3,.cmp
	b	.out
.pub:	adr	x0,s_topic_wifi
	mov	x1,x22
	mov	x2,BUS_RETAIN
	bl	bp_publish
	tbnz	x0,63,.out			; (rate : au prochain tour)
	adr	x1,pub_last
	mov	x2,0
.cp:	ldrb	w3,[x22,x2]
	strb	w3,[x1,x2]
	add	x2,x2,1
	cbnz	w3,.cp
.out:	add	sp,sp,16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Connexion automatique (docs/RESEAU.md 4.2)
; ---------------------------------------------------------------------------
conn_thread:
	mov	x28,5000			; attente entre deux tours (ms)
	sys	SYS_TIME_MS			; premier tour 0,5 s apres la mise en
	add	x27,x0,500			; route de la puce (prochain tour, ms)
	mov	x24,1				; premier tour : sans balayage
	mov	x26,0				; connecte au tour precedent
	mov	x25,0				; "aucun reseau connu" deja dit
.loop:	mov	x0,250
	sys	SYS_SLEEP
	adr	x0,wl_net_on
	ldr	w0,[x0]
	cbz	w0,.pas_co
	mov	x26,1				; connecte : rien a faire
	mov	x28,5000
	b	.loop
.pas_co:
	cbz	x26,.essai
	mov	x26,0				; lien perdu : le meme reseau apres 2 s
	adr	x0,m_perdu
	bl	puts
	mov	x0,2000
	sys	SYS_SLEEP
	adr	x0,auto_off
	ldr	w0,[x0]
	cbnz	w0,.loop
	adr	x0,last_ssid
	ldrb	w1,[x0]
	cbz	w1,.essai
	bl	known_find
	cbz	x0,.essai
	bl	try_known
	cbz	x0,.loop
.essai:	adr	x0,auto_off			; deconnexion voulue
	ldr	w0,[x0]
	cbnz	w0,.loop
	adr	x0,user_join
	ldr	w0,[x0]
	cbnz	w0,.loop
	adr	x0,known_n
	ldr	x0,[x0]
	cbz	x0,.loop			; aucun reseau connu
	sys	SYS_TIME_MS
	cmp	x0,x27
	b.lo	.loop
	cbz	x24,.tour
	mov	x24,0
	bl	quick_round			; le reseau prefere, sans balayage
	cbz	x0,.ok
.tour:	bl	round				; un tour : balayage, essais
	cbz	x0,.ok
	cmp	x0,1				; aucun reseau connu visible
	b.ne	.attente
	cbnz	x25,.attente
	mov	x25,1
	adr	x0,t_wifi
	adr	x1,t_aucun
	bl	notifier
	adr	x0,m_aucun
	bl	puts
.attente:
	sys	SYS_TIME_MS
	add	x27,x0,x28
	lsl	x28,x28,1			; 5 s, 10 s... 60 s
	movl	x0,60000
	cmp	x28,x0
	csel	x28,x28,x0,ls
	b	.loop
.ok:	mov	x28,5000
	b	.loop

; Premier tour, au demarrage : le reseau connu de plus haute priorite,
; directement, sans balayer tous les canaux (2,5 s) : la puce le cherche
; elle-meme -> x0 = 0 connecte (sinon le tour normal suit tout de suite)
quick_round:
	prologue
	bl	mark_all
	bl	best_seen
	cbz	x0,.rate
	bl	try_known
	b	.out
.rate:	mov	x0,2
.out:	epilogue

; Un tour de connexion -> x0 = 0 connecte, 1 aucun reseau connu visible,
; 2 essais sans succes
round:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,wl_opmutex			; balayage
	bl	mutex_lock
	bl	wl_do_scan
	mov	x21,x0
	bl	mark_seen			; reseaux connus presents (RSSI), releves
	mov	x22,x0				; avant de rendre le verrou : un autre
						; balayage (menu, panneau) reecrirait
						; la table pendant la lecture
	adr	x0,wl_opmutex
	bl	mutex_unlock
	cbnz	x22,.essais
	cmp	x21,0				; balayage vide ou en erreur : on
	b.gt	.aucun				; essaie quand meme tous les connus
	bl	mark_all			; (reseau cache, balayage rate)
.essais:
	bl	best_seen			; le meilleur restant
	cbz	x0,.rate
	mov	x19,x0
	str	wzr,[x19,KN_SEEN]		; (essaye)
	adr	x0,user_join
	ldr	w0,[x0]
	cbnz	w0,.rate
	adr	x0,auto_off
	ldr	w0,[x0]
	cbnz	w0,.rate
	mov	x0,x19
	bl	try_known
	cbz	x0,.ok
	b	.essais
.ok:	mov	x0,0
	b	.out
.aucun:	mov	x0,1
	b	.out
.rate:	mov	x0,2
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = reseau connu : essai de connexion -> x0 = 0 si connecte
try_known:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,m_auto			; "wifi : connexion automatique a X"
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x0,wl_opmutex
	bl	mutex_lock
	add	x1,x19,KN_KEY
	mov	x0,x19
	bl	wl_join
	mov	x20,x0
	adr	x0,wl_opmutex
	bl	mutex_unlock
	mov	x0,x20
	bl	log_result
	cbnz	x20,.out
	adr	x0,last_ssid
	mov	x1,x19
	bl	str_copy_n
.out:	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = nombre de reseaux connus vus au dernier balayage (KN_SEEN = RSSI +
; 1000 du plus fort de leurs points d'acces)
mark_seen:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x22,0
	adr	x19,known
	adr	x0,known_n
	ldr	x20,[x0]
.k:	cbz	x20,.fin
	str	wzr,[x19,KN_SEEN]
	adr	x0,wl_scan			; ses points d'acces
	ldr	x9,[x0]
	adr	x0,wl_scan_count
	ldr	w10,[x0]
.e:	cbz	w10,.kn
	ldrb	w1,[x9,SE_SSIDLEN]		; meme SSID ?
	mov	x2,0
.c:	ldrb	w3,[x19,x2]
	cmp	x2,x1
	b.eq	.fin_nom
	ldrb	w4,[x9,x2]
	cmp	w3,w4
	b.ne	.en
	add	x2,x2,1
	b	.c
.fin_nom:
	cbnz	w3,.en
	ldrsh	w1,[x9,SE_RSSI]
	add	w1,w1,1000
	ldr	w2,[x19,KN_SEEN]
	cbnz	w2,.max
	add	x22,x22,1
.max:	cmp	w1,w2
	csel	w1,w1,w2,gt
	str	w1,[x19,KN_SEEN]
.en:	add	x9,x9,SE_SIZE
	sub	w10,w10,1
	b	.e
.kn:	add	x19,x19,KN_SIZE
	sub	x20,x20,1
	b	.k
.fin:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; tous les reseaux connus a essayer (signal inconnu : 1)
mark_all:
	adr	x0,known
	adr	x1,known_n
	ldr	x1,[x1]
	mov	w2,1
.k:	cbz	x1,.r
	str	w2,[x0,KN_SEEN]
	add	x0,x0,KN_SIZE
	sub	x1,x1,1
	b	.k
.r:	ret

; -> x0 = reseau vu non encore essaye : plus grande priorite, puis meilleur
; signal (0 : aucun)
best_seen:
	adr	x2,known
	adr	x3,known_n
	ldr	x3,[x3]
	mov	x0,0
	mov	w5,0				; sa priorite, son signal
	mov	w6,0
.k:	cbz	x3,.r
	ldr	w7,[x2,KN_SEEN]
	cbz	w7,.n
	ldr	w8,[x2,KN_PRIO]
	cbz	x0,.pris
	cmp	w8,w5
	b.gt	.pris
	b.lt	.n
	cmp	w7,w6
	b.le	.n
.pris:	mov	x0,x2
	mov	w5,w8
	mov	w6,w7
.n:	add	x2,x2,KN_SIZE
	sub	x3,x3,1
	b	.k
.r:	ret

; ---------------------------------------------------------------------------
;  Reseaux connus : SD0:/etc/wifi.txt
; ---------------------------------------------------------------------------

; x0 = SSID -> x0 = son entree (0 : inconnu)
known_find:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x19,known
	adr	x0,known_n
	ldr	x20,[x0]
.k:	cbz	x20,.non
	mov	x0,x19
	mov	x1,x21
	bl	str_eq
	cbnz	x0,.oui
	add	x19,x19,KN_SIZE
	sub	x20,x20,1
	b	.k
.oui:	mov	x0,x19
	b	.out
.non:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = SSID, x1 = cle, x2 = priorite : ajoute ou met a jour un reseau connu
known_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	known_find
	cbnz	x0,.maj
	adr	x1,known_n
	ldr	x2,[x1]
	cmp	x2,NET_MAX
	b.hs	.out
	add	x3,x2,1
	str	x3,[x1]
	adr	x0,known
	mov	x3,KN_SIZE
	madd	x0,x2,x3,x0
	mov	x22,x0
	mov	x1,KN_SIZE
	bl	mem_zero
	mov	x0,x22
.maj:	mov	x22,x0
	mov	x1,x19
	mov	x2,33
	bl	copy_z
	add	x0,x22,KN_KEY
	mov	x1,x20
	mov	x2,65
	bl	copy_z
	str	w21,[x22,KN_PRIO]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Lit SD0:/etc/wifi.txt (a defaut SD0:/etc/wifi.bak) ; puis pays= de
; SD0:/etc/options.txt (prioritaire sur country=)
cfg_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,known_n
	str	xzr,[x0]
	adr	x0,p_cfg
	bl	cfg_read
	adr	x0,known_n
	ldr	x0,[x0]
	cbnz	x0,.opts
	adr	x0,p_bak
	bl	cfg_read
.opts:	adr	x0,p_options			; pays= (prioritaire)
	adr	x1,cfgbuf
	mov	x2,CFG_MAX - 1
	bl	read_file
	cmp	x0,0
	b.le	.fin
	adr	x19,cfgbuf
	strb	wzr,[x19,x0]
.ligne:	ldrb	w0,[x19]
	cbz	w0,.fin
	mov	x0,x19
	adr	x1,k_pays
	bl	prefix
	cbz	x0,.suiv
	add	x19,x19,5
	ldrb	w1,[x19]
	ldrb	w2,[x19,1]
	cmp	w1,'A'
	b.lo	.suiv
	cmp	w2,'A'
	b.lo	.suiv
	bic	w1,w1,0x20
	bic	w2,w2,0x20
	adr	x3,wl_cfg_cc
	strb	w1,[x3]
	strb	w2,[x3,1]
	strb	wzr,[x3,2]
.suiv:	ldrb	w0,[x19],1
	cbz	w0,.fin
	cmp	w0,10
	b.ne	.suiv
	b	.ligne
.fin:	adr	x0,m_connus			; "wifi : 2 reseaux connus, pays FR"
	bl	puts
	adr	x0,known_n
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_connus2
	bl	puts
	adr	x0,wl_cfg_cc
	bl	puts
	adr	x0,m_nl
	bl	puts
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin : lignes "cle=valeur" -> table known, wl_cfg_cc
cfg_read:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x1,cfgbuf
	mov	x2,CFG_MAX - 1
	bl	read_file
	cmp	x0,0
	b.le	.out
	adr	x19,cfgbuf
	strb	wzr,[x19,x0]
	mov	x23,0				; reseau en cours (0 : aucun)
.ligne:	ldrb	w0,[x19]
	cbz	w0,.out
	mov	x20,x19				; fin de ligne -> 0
.eol:	ldrb	w0,[x20]
	cbz	w0,.coupe
	cmp	w0,10
	b.eq	.coupe
	cmp	w0,13
	b.eq	.coupe
	add	x20,x20,1
	b	.eol
.coupe:	ldrb	w24,[x20]
	strb	wzr,[x20]
.esp:	ldrb	w0,[x19]			; espaces de tete
	cmp	w0,' '
	b.eq	.e1
	cmp	w0,9
	b.ne	.cle
.e1:	add	x19,x19,1
	b	.esp
.cle:	cmp	w0,'#'
	b.eq	.suiv
	mov	x21,x19				; "cle=" ?
.eq:	ldrb	w0,[x21]
	cbz	w0,.suiv
	cmp	w0,'='
	b.eq	.val
	orr	w0,w0,0x20			; (minuscules)
	strb	w0,[x21]
	add	x21,x21,1
	b	.eq
.val:	strb	wzr,[x21],1
	mov	x0,x21				; valeur : guillemets, espaces
	bl	cfg_value			; -> x0 = valeur (x1 = 1 : entre guillemets)
	mov	x22,x0
	mov	x21,x1
	mov	x0,x19
	adr	x1,k_ssid
	bl	str_eq
	cbnz	x0,.ssid
	mov	x0,x19
	adr	x1,k_reseau
	bl	str_eq
	cbnz	x0,.ssid
	mov	x0,x19
	adr	x1,k_key
	bl	str_eq
	cbnz	x0,.key
	mov	x0,x19
	adr	x1,k_cle
	bl	str_eq
	cbnz	x0,.key
	mov	x0,x19
	adr	x1,k_psk
	bl	str_eq
	cbnz	x0,.key
	cbnz	x21,.autres			; (commentaire en fin de ligne : pas
	mov	x0,x22				;  dans une cle)
	bl	cut_comment
.autres:
	mov	x0,x19
	adr	x1,k_priority
	bl	str_eq
	cbnz	x0,.prio
	mov	x0,x19
	adr	x1,k_country
	bl	str_eq
	cbnz	x0,.cc
	mov	x0,x19
	adr	x1,k_pays2
	bl	str_eq
	cbnz	x0,.cc
	b	.suiv				; (proto=, key_mgmt= : la puce choisit)
.ssid:	adr	x1,known_n
	ldr	x2,[x1]
	cmp	x2,NET_MAX
	b.hs	.plein
	add	x3,x2,1
	str	x3,[x1]
	adr	x23,known
	mov	x3,KN_SIZE
	madd	x23,x2,x3,x23
	mov	x0,x23
	mov	x1,KN_SIZE
	bl	mem_zero
	mov	x0,x23
	mov	x1,x22
	mov	x2,33
	bl	copy_z
	b	.suiv
.plein:	mov	x23,0
	b	.suiv
.key:	cbz	x23,.suiv
	add	x0,x23,KN_KEY
	mov	x1,x22
	mov	x2,65
	bl	copy_z
	b	.suiv
.prio:	cbz	x23,.suiv
	mov	x0,x22
	bl	parse_dec
	str	w0,[x23,KN_PRIO]
	b	.suiv
.cc:	ldrb	w1,[x22]
	ldrb	w2,[x22,1]
	cbz	w1,.suiv
	cbz	w2,.suiv
	bic	w1,w1,0x20
	bic	w2,w2,0x20
	adr	x3,wl_cfg_cc
	strb	w1,[x3]
	strb	w2,[x3,1]
	strb	wzr,[x3,2]
.suiv:	mov	x19,x20				; ligne suivante
	cbz	w24,.out
	add	x19,x19,1
	b	.ligne
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur brute -> x0 = valeur (sans espaces autour ; "..." : le texte
; entre le premier et le dernier guillemet), x1 = 1 si entre guillemets
cfg_value:
.esp:	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.e1
	cmp	w1,9
	b.ne	.q
.e1:	add	x0,x0,1
	b	.esp
.q:	cmp	w1,'"'
	b.ne	.brut
	add	x0,x0,1
	mov	x2,x0				; dernier guillemet
	mov	x3,0
.d:	ldrb	w1,[x2]
	cbz	w1,.df
	cmp	w1,'"'
	csel	x3,x2,x3,eq
	add	x2,x2,1
	b	.d
.df:	cbz	x3,.qf
	strb	wzr,[x3]
.qf:	mov	x1,1
	ret
.brut:	mov	x2,x0				; espaces de fin
	mov	x3,x0
.f:	ldrb	w1,[x2]
	cbz	w1,.ff
	add	x2,x2,1
	cmp	w1,' '
	b.eq	.f
	cmp	w1,9
	b.eq	.f
	mov	x3,x2
	b	.f
.ff:	strb	wzr,[x3]
	mov	x1,0
	ret

; x0 = valeur : coupe un commentaire " #..." (et les espaces avant)
cut_comment:
	mov	x1,x0
.c:	ldrb	w2,[x1]
	cbz	w2,.r
	cmp	w2,'#'
	b.ne	.n
	cmp	x1,x0
	b.eq	.n
	ldrb	w3,[x1,-1]
	cmp	w3,' '
	b.eq	.coupe
	cmp	w3,9
	b.ne	.n
.coupe:	sub	x1,x1,1
	strb	wzr,[x1]
	cmp	x1,x0
	b.eq	.r
	ldrb	w3,[x1,-1]
	cmp	w3,' '
	b.eq	.coupe
	cmp	w3,9
	b.eq	.coupe
	ret
.n:	add	x1,x1,1
	b	.c
.r:	ret

; x0 = texte, x1 = prefixe -> x0 = 1 si le texte commence par le prefixe
prefix:
.c:	ldrb	w2,[x1],1
	cbz	w2,.oui
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; Ecrit SD0:/etc/wifi.bak puis SD0:/etc/wifi.txt (pas de renommage dans le
; protocole des fichiers : la copie sert si wifi.txt est abime)
cfg_save:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,cfgbuf
	mov	x0,x19
	adr	x1,t_entete
	bl	str_cpy
	adr	x1,wl_cfg_cc
	ldrb	w2,[x1]
	cbz	w2,.res
	adr	x1,t_country
	bl	str_cpy
	adr	x1,wl_cfg_cc
	bl	str_cpy
	mov	w1,10
	strb	w1,[x0],1
.res:	adr	x20,known
	adr	x1,known_n
	ldr	x21,[x1]
.k:	cbz	x21,.ecrit
	adr	x1,t_ssid
	bl	str_cpy
	mov	x1,x20
	bl	str_cpy
	adr	x1,t_guil_nl
	bl	str_cpy
	ldrb	w1,[x20,KN_KEY]
	cbz	w1,.p
	adr	x1,t_key
	bl	str_cpy
	add	x1,x20,KN_KEY
	bl	str_cpy
	adr	x1,t_guil_nl
	bl	str_cpy
.p:	ldr	w1,[x20,KN_PRIO]
	cbz	w1,.n
	adr	x1,t_prio
	bl	str_cpy
	ldr	w1,[x20,KN_PRIO]
	mov	x22,x0
	mov	x0,x1
	mov	x1,x22
	bl	fmt_dec
	mov	w1,10
	strb	w1,[x0],1
.n:	add	x20,x20,KN_SIZE
	sub	x21,x21,1
	b	.k
.ecrit:	sub	x20,x0,x19			; longueur
	adr	x0,p_bak
	mov	x1,x19
	mov	x2,x20
	bl	write_file
	adr	x0,p_cfg
	mov	x1,x19
	mov	x2,x20
	bl	write_file
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = chaine -> x0 = fin (sur le 0 ecrit)
str_cpy:
	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,str_cpy
	sub	x0,x0,1
	ret

; x0 = destination, x1 = source, x2 = taille (avec le 0) -> x0 = source
; apres son 0 (copie tronquee, toujours terminee)
copy_z:
	sub	x2,x2,1
.c:	ldrb	w3,[x1],1
	cbz	w3,.fin
	cbz	x2,.c				; (trop long : la suite est sautee)
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.fin:	strb	wzr,[x0]
	mov	x0,x1
	ret

; x0 = chemin, x1 = tampon, x2 = max -> x0 = octets lus, < 0 erreur
read_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x22,0
.r:	sub	x2,x21,x22
	cbz	x2,.fin
	mov	x0,x19
	add	x1,x20,x22
	sys	SYS_READ
	cmp	x0,0
	b.le	.fin
	add	x22,x22,x0
	b	.r
.fin:	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,x22
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin, x1 = donnees, x2 = octets -> x0 = 0, < 0 erreur
write_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x1,x20
	mov	x2,x21
	sys	SYS_WRITE
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Equivalents des services du noyau (chip.inc, sdio.inc)
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

; w0 = valeur -> en hexadecimal (4 chiffres au moins)
kput_hex:
	save_caller
	sub	sp,sp,32
	add	x1,sp,17
	strb	wzr,[x1]
	mov	w2,w0
	mov	x4,0
.c:	and	w3,w2,15
	cmp	w3,10
	add	w5,w3,'0'
	add	w6,w3,'A'-10
	csel	w3,w5,w6,lo
	sub	x1,x1,1
	strb	w3,[x1]
	lsr	w2,w2,4
	add	x4,x4,1
	cmp	x4,4
	b.lo	.c
	cbnz	w2,.c
	mov	x0,x1
	bl	puts
	add	sp,sp,32
	restore_caller
	ret

; x0 = adresse de 6 octets -> "aa:bb:cc:dd:ee:ff"
put_mac:
	save_caller
	sub	sp,sp,32
	mov	x1,sp
	mov	x2,0
.o:	ldrb	w3,[x0,x2]
	lsr	w4,w3,4
	and	w3,w3,15
	cmp	w4,10
	add	w5,w4,'0'
	add	w6,w4,'a'-10
	csel	w4,w5,w6,lo
	strb	w4,[x1],1
	cmp	w3,10
	add	w5,w3,'0'
	add	w6,w3,'a'-10
	csel	w3,w5,w6,lo
	strb	w3,[x1],1
	add	x2,x2,1
	cmp	x2,6
	b.hs	.f
	mov	w3,':'
	strb	w3,[x1],1
	b	.o
.f:	strb	wzr,[x1]
	mov	x0,sp
	bl	puts
	add	sp,sp,32
	restore_caller
	ret

time_ms:
	str	x8,[sp,-16]!
	mov	x8,SYS_TIME_MS
	svc	0
	ldr	x8,[sp],16
	ret

thread_sleep:
delay_ms:
	str	x8,[sp,-16]!
	mov	x8,SYS_SLEEP
	svc	0
	ldr	x8,[sp],16
	ret

; x0 = microsecondes (attente active)
udelay:
	mrs	x1,cntfrq_el0
	mul	x0,x0,x1
	movl	x1,1000000
	udiv	x0,x0,x1
	mrs	x1,cntpct_el0
	add	x0,x0,x1
.w:	isb
	mrs	x1,cntpct_el0
	cmp	x1,x0
	b.lo	.w
	ret

; x0 = adresse, x1 = octets
mem_zero:
	cbz	x1,.e
.z:	strb	wzr,[x0],1
	subs	x1,x1,1
	b.ne	.z
.e:	ret

; x0 = destination, x1 = source, x2 = octets
mem_copy:
	cbz	x2,.e
.c:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.c
.e:	ret

; x0 = destination, x1 = source -> x0 = fin (sur le 0) ; 63 caracteres au plus
str_copy_n:
	mov	x2,63
.c:	ldrb	w3,[x1],1
	cbz	x2,.end
	cbz	w3,.end
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.end:	strb	wzr,[x0]
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

; w0 = broche, w1 = fonction (0 entree, 1 sortie, 4 ALT0, 7 ALT3...)
gpio_fsel:
	adr	x9,gpio_va
	ldr	x9,[x9]
	mov	w10,10				; registre GPFSELn = broche / 10
	udiv	w11,w0,w10
	msub	w12,w11,w10,w0			; broche % 10
	add	x9,x9,x11,lsl 2
	mov	w10,3
	mul	w12,w12,w10			; position : 3 bits par broche
	ldr	w10,[x9,GP_FSEL0]
	mov	w11,7
	lslv	w11,w11,w12
	bic	w10,w10,w11
	and	w11,w1,7
	lslv	w11,w11,w12
	orr	w10,w10,w11
	str	w10,[x9,GP_FSEL0]
	dsb	sy
	ret

; w0 = broche, w1 = tirage (0 aucun, 1 haut, 2 bas)
gpio_pull:
	adr	x9,gpio_va
	ldr	x9,[x9]
	lsr	w10,w0,4			; 16 broches par registre
	add	x9,x9,x10,lsl 2
	and	w12,w0,15
	lsl	w12,w12,1
	ldr	w10,[x9,GP_PUPPDN0]
	mov	w11,3
	lslv	w11,w11,w12
	bic	w10,w10,w11
	and	w11,w1,3
	lslv	w11,w11,w12
	orr	w10,w10,w11
	str	w10,[x9,GP_PUPPDN0]
	ret

; x0 = horloge (1 : EMMC) -> x0 = Hz (0 si inconnu)
mbox_get_clock:
	prologue
	adr	x1,fw_clk
	str	w0,[x1,20]
	str	wzr,[x1,24]
	mov	w2,8
	str	w2,[x1,12]
	str	wzr,[x1,16]
	mov	x0,x1
	sys	SYS_FW_CALL
	tbnz	x0,63,.zero
	adr	x1,fw_clk
	ldr	w0,[x1,24]
	epilogue
.zero:	mov	x0,0
	epilogue

; -> x0 = adresse MAC de la carte (6 octets) ; a defaut 02:41:52:4D:50:49
mbox_get_board_mac:
	prologue
	adr	x0,fw_mac
	sys	SYS_FW_CALL
	tbnz	x0,63,.local
	adr	x2,fw_mac
	ldr	w0,[x2,20]
	ldrh	w1,[x2,24]
	orr	x0,x0,x1,lsl 32
	cbz	x0,.local
	epilogue
.local:	movl	x0,0x49504D524102
	epilogue

; x0 = chemin -> x0 = contenu (tampon de 1 Mo, un fichier a la fois), x1 =
; taille ; x0 = 0 si absent
boot_file_take:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,file_buf
	ldr	x0,[x0]
	cbnz	x0,.lire
	movl	x0,FILE_MAX + 16
	sys	SYS_SBRK
	tbnz	x0,63,.non
	add	x0,x0,15
	and	x0,x0,0xFFFFFFFFFFFFFFF0
	adr	x1,file_buf
	str	x0,[x1]
.lire:	mov	x1,x0
	mov	x0,x19
	movl	x2,FILE_MAX
	bl	read_file
	cmp	x0,0
	b.le	.non
	mov	x1,x0
	adr	x0,file_buf
	ldr	x0,[x0]
	b	.out
.non:	mov	x0,0
	mov	x1,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = contenu rendu (le tampon resert)
file_free:
	ret

; (wl_setup) les reseaux connus et le pays sont deja lus (cfg_load)
wl_cfg_load:
	ret

; ---------------------------------------------------------------------------
;  Simulation (argument test, QEMU) : quatre reseaux visibles ; une connexion
;  reussit en 1 s, sauf avec la cle "faux" (cle refusee) ou un SSID inconnu
; ---------------------------------------------------------------------------
sim_scan:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,1500
	sys	SYS_SLEEP
	adr	x0,wl_scan
	ldr	x19,[x0]
	mov	x0,x19
	mov	x1,4 * SE_SIZE
	bl	mem_zero
	adr	x20,sim_bss
	mov	x2,0
.e:	cmp	x2,4
	b.hs	.fin
	mov	x0,x19
	ldr	x1,[x20],8			; SSID
	mov	x3,0
.c:	ldrb	w4,[x1,x3]
	cbz	w4,.cf
	strb	w4,[x0,x3]
	add	x3,x3,1
	b	.c
.cf:	strb	w3,[x19,SE_SSIDLEN]
	ldr	x4,[x20],8			; canal, securite, RSSI
	strb	w4,[x19,SE_CHAN]
	lsr	x5,x4,8
	strb	w5,[x19,SE_SEC]
	lsr	x5,x4,16
	strh	w5,[x19,SE_RSSI]
	add	x19,x19,SE_SIZE
	add	x2,x2,1
	b	.e
.fin:	mov	w0,4
	adr	x1,wl_scan_count
	str	w0,[x1]
	mov	x0,4
	ldp	x19,x20,[sp],16
	epilogue

; x0 = SSID, x1 = cle -> x0 = 0, E_ACCES, E_NOENT
sim_join:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w0,WS_JOINING
	adr	x1,wl_state
	str	w0,[x1]
	mov	x0,1000
	sys	SYS_SLEEP
	mov	x0,x20
	adr	x1,sim_faux
	bl	str_eq
	cbnz	x0,.acces
	adr	x0,sim_bss			; un des reseaux simules ?
	mov	x2,0
.e:	cmp	x2,4
	b.hs	.noent
	add	x1,x0,x2,lsl 4
	ldr	x1,[x1]
	stp	x0,x2,[sp,-16]!
	mov	x0,x19
	bl	str_eq
	mov	x3,x0
	ldp	x0,x2,[sp],16
	cbnz	x3,.ok
	add	x2,x2,1
	b	.e
.ok:	adr	x0,wl_ssid
	mov	x1,x19
	bl	str_copy_n
	mov	w0,WS_CONNECTED
	adr	x1,wl_state
	str	w0,[x1]
	adr	x0,wl_net_on
	mov	w1,1
	str	w1,[x0]
	mov	x0,0
	b	.out
.acces:	adr	x0,wl_ssid
	mov	x1,x19
	bl	str_copy_n
	mov	x0,E_ACCES
	b	.ko
.noent:	mov	x0,E_NOENT
.ko:	mov	w1,WS_READY
	adr	x2,wl_state
	str	w1,[x2]
	adr	x2,wl_net_on
	str	wzr,[x2]
.out:	ldp	x19,x20,[sp],16
	epilogue

include 'sdio.inc'
include 'chip.inc'
include '../inc/netif.inc'
include '../inc/buspub.inc'
include '../../inc/notify.inc'

s_wifi		db 'wifi',0
s_topic_wifi	db BUS_TOPIC_WIFI,0
t_st_absent	db 'absent',0
t_st_starting	db 'starting',0
t_st_ready	db 'ready',0
t_st_joining	db 'joining',0
t_st_connected	db 'connected',0
s_wifi_io	db 'wifi.io',0
s_wlan0		db 'wlan0',0
s_test		db 'test',0
sim_faux	db 'faux',0
sim_s1		db 'Maison',0
sim_s2		db 'Telephone',0
sim_s3		db 'Cafe ouvert',0
sim_s4		db 'Voisin',0
		align 8
sim_bss		dd sim_s1, 6 or (3 shl 8) or ((-48 and 0xFFFF) shl 16)
		dd sim_s2, 1 or (3 shl 8) or ((-67 and 0xFFFF) shl 16)
		dd sim_s3, 11 or (0 shl 8) or ((-74 and 0xFFFF) shl 16)
		dd sim_s4, 36 or (2 shl 8) or ((-86 and 0xFFFF) shl 16)
m_test		db 'wifi : essai sans la puce (argument test)',10,0
p_cfg		db 'SD0:/etc/wifi.txt',0
p_bak		db 'SD0:/etc/wifi.bak',0
p_options	db 'SD0:/etc/options.txt',0
m_err1		db 'wifi : erreurs du bus ',0
m_err2		db ', INTERRUPT ',0
m_err3		db ', base ',0
m_err4		db ' Hz',10,0
k_ssid		db 'ssid',0
k_reseau	db 'reseau',0
k_key		db 'key',0
k_cle		db 'cle',0
k_psk		db 'psk',0
k_priority	db 'priority',0
k_country	db 'country',0
k_pays		db 'pays=',0
k_pays2		db 'pays',0
t_entete	db '# R',0xC3,0xA9,'seaux Wi-Fi connus (',0xC3,0xA9,'crit par le service wifi ;'
		db ' docs/RESEAU.md 4.1)',10,0
t_country	db 'country=',0
t_ssid		db 'ssid="',0
t_key		db 'key="',0
t_guil_nl	db '"',10,0
t_prio		db 'priority=',0
t_wifi		db 'Wi-Fi',0
t_cle		db 'Cl',0xC3,0xA9,' refus',0xC3,0xA9,'e pour ',0xC2,0xAB,' ',0
t_guil		db ' ',0xC2,0xBB,0
t_aucun		db 'Aucun r',0xC3,0xA9,'seau connu',0
m_absent	db 'wifi : pas de puce Wi-Fi du Pi 4 ici (QEMU) : rien a faire',10,0
m_connus	db 'wifi : ',0
m_connus2	db ' r',0xC3,0xA9,'seau(x) connu(s), pays ',0
m_join		db 'wifi : connexion ',0xC3,0xA0,' ',0
m_auto		db 'wifi : connexion automatique ',0xC3,0xA0,' ',0
m_echec		db 'wifi : connexion impossible, erreur -',0
m_perdu		db 'wifi : lien perdu',10,0
m_aucun		db 'wifi : aucun r',0xC3,0xA9,'seau connu visible',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 16
fw_rev		dw 28, 0, 0x00000001, 4, 0, 0, 0	; GET_FIRMWARE_REVISION
		align 16
fw_mac		dw 32, 0, 0x00010003, 8, 0, 0, 0, 0	; GET_BOARD_MAC_ADDRESS
		align 16
fw_clk		dw 32, 0, 0x00030002, 8, 0, 0, 0, 0	; GET_CLOCK_RATE
		align 8
canal		dd 0
gpio_va		dd 0
file_buf	dd 0
known_n		dd 0
auto_off	dw 0				; 1 : deconnexion voulue (LEAVE)
sim		dw 0				; 1 : essai sans la puce (simulation)
user_join	dw 0				; 1 : une connexion demandee est en cours
last_ssid	rb 68
		align 8
bp_canal	dd 0				; canal de busd + 1 (buspub.inc)
pub_rssi_t	dd 0				; prochaine lecture du signal (ms)
pub_rssi	dd 0
pub_val		rb 64				; korin.net.wifi : a publier, publie
pub_last	rb 64
sdio_data
chip_data
		align 8
wl_link_t	dd 0				; (wifi_stats) prochaine lecture de la liaison, ms
wl_busy_t	dd 0				; (wifi_pace) derniere activite, ms
wl_io		dd 0				; canal + 1 de wifi.io (0 : on sonde)
wl_wait		dd 0				; ms d'attente au repos
wl_timed	dd 0				; 1 : la derniere attente a expire
wl_iomsg	rb 512
		align 16
fw_wlon		dw 0, 0, 0, 0, 0, 0, 0, 0		; (wl_on_set) message du firmware
netif_data
nbuf		rb 160
known		rb NET_MAX * KN_SIZE
cfgbuf		rb CFG_MAX
