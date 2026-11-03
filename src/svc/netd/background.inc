; ============================================================================
;  background.inc - thread auxiliaire de netd : ce qui peut bloquer (fichiers,
;  notifyd) ; notifications " Reseau connecte / perdu ", heure par NTP
;  (SNTP, RFC 4330 : au premier bail puis toutes les 6 h ; serveur ntp= de
;  SD0:/etc/options.txt, pool.ntp.org par defaut, ntp=non : jamais), heure
;  locale par fuseau= (Europe/Paris par defaut) ecrite dans
;  SD0:/etc/clock.cfg (decalage lu par la barre de menus et l'Horloge).
; ============================================================================

NTP_PERIOD	= 6 * 3600 * 1000	; ms entre deux synchronisations
NTP_RETRY	= 60000			; apres un echec
NTP_1970	= 2208988800		; secondes entre 1900 et 1970

bg_thread:
	bl	opts_load
	mov	x19,0				; bail vu au dernier tour
.loop:	mov	x0,500
	sys	SYS_SLEEP
	adr	x0,dhcp_state			; adresse obtenue ?
	ldr	w0,[x0]
	cmp	w0,DHCP_BOUND
	cset	x20,eq
	adr	x0,net_ip
	ldr	w0,[x0]
	cmp	w0,0
	csel	x20,xzr,x20,eq
	cmp	x20,x19
	b.eq	.ntp
	mov	x19,x20
	mov	x0,x20
	bl	notify_net
	cbz	x20,.ntp
	adr	x0,ntp_next			; nouveau bail : l'heure tout de suite
	str	xzr,[x0]
.ntp:	adr	x0,ntp_force
	ldr	x0,[x0]
	cbnz	x0,.sync
	cbz	x20,.loop
	sys	SYS_TIME_MS
	adr	x1,ntp_next
	ldr	x1,[x1]
	cmp	x0,x1
	b.lo	.loop
.sync:	bl	opts_load
	bl	ntp_sync
	adr	x1,ntp_force
	str	xzr,[x1]
	adr	x1,ntp_gen
	ldr	x2,[x1]
	add	x2,x2,1
	str	x2,[x1]
	adr	x1,ntp_last
	str	x0,[x1]
	cmp	x0,0
	movl	x2,NTP_PERIOD
	mov	x3,NTP_RETRY
	csel	x2,x2,x3,eq
	adr	x4,ntp_off			; (ntp=non : rien, pas de nouvel essai)
	ldr	x4,[x4]
	cbz	x4,.next
	movl	x2,0x7FFFFFFF
.next:	sys	SYS_TIME_MS
	add	x0,x0,x2
	adr	x1,ntp_next
	str	x0,[x1]
	b	.loop

; x0 = 1 connecte, 0 perdu : notification (" Ethernet - 10.0.0.5 "...)
notify_net:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x20,nbuf
	strb	wzr,[x20]
	adr	x0,if_active			; le genre de l'interface active
	ldr	x0,[x0]
	tbnz	x0,63,.texte
	adr	x1,ifs
	mov	x2,IF_SIZE
	madd	x1,x0,x2,x1
	ldr	x0,[x1,IF_TYPE]
	cmp	x0,NIF_WIFI
	b.eq	.wifi
	adr	x2,t_eth
	cmp	x0,NIF_ETHERNET
	b.eq	.k
	adr	x2,t_usb
.k:	mov	x0,x20
	mov	x1,x2
	bl	str_cat
	b	.ip
.wifi:	str	x1,[sp,-16]!
	mov	x0,x20
	adr	x1,t_wifi
	bl	str_cat
	ldr	x1,[sp],16
	add	x1,x1,IF_SSID
	bl	str_cat
	adr	x1,t_guil
	bl	str_cat
.ip:	cbz	x19,.texte
	adr	x1,t_tiret
	bl	str_cat
	mov	x1,x0
	adr	x0,net_ip
	ldr	w0,[x0]
	bl	fmt_ip4
.texte:	adr	x0,t_connecte
	adr	x1,t_perdu
	cmp	x19,0
	csel	x0,x0,x1,ne
	mov	x1,x20
	bl	notifier
	adr	x0,m_ntf			; (et au journal)
	bl	puts
	mov	x0,x20
	bl	puts
	adr	x0,m_nl
	bl	puts
	ldp	x19,x20,[sp],16
	epilogue

; w0 = adresse, x1 = tampon -> x0 = fin (" a.b.c.d ")
fmt_ip4:
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

; options.txt : fuseau=, ntp=
opts_load:
	prologue
	adr	x0,tz_name
	strb	wzr,[x0]
	adr	x0,ntp_name
	strb	wzr,[x0]
	adr	x0,ntp_off
	str	xzr,[x0]
	adr	x0,p_options
	adr	x1,opt_line
	bl	ini_read
	epilogue

; (ini_read) x0 = cle, x1 = valeur
opt_line:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x1,k_fuseau
	bl	str_eq
	cbz	x0,.ntp
	adr	x0,tz_name
	mov	x1,x20
	mov	x2,32
	bl	str_copy_n
	b	.out
.ntp:	mov	x0,x19
	adr	x1,k_ntp
	bl	str_eq
	cbz	x0,.out
	mov	x0,x20
	adr	x1,v_non
	bl	str_eq
	adr	x1,ntp_off
	str	x0,[x1]
	cbnz	x0,.out
	adr	x0,ntp_name
	mov	x1,x20
	mov	x2,64
	bl	str_copy_n
.out:	ldp	x19,x20,[sp],16
	epilogue

; Une synchronisation -> x0 = 0, ou < 0
ntp_sync:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,ntp_off
	ldr	x0,[x0]
	cbnz	x0,.off
	adr	x0,ntp_name			; le serveur
	ldrb	w1,[x0]
	cbnz	w1,.nom
	adr	x0,s_pool
.nom:	bl	dns_resolve
	tbnz	x0,63,.out
	bl	ntp_query
	tbnz	x0,63,.out
	mov	x19,x0				; secondes depuis 1970
	mov	x20,x1				; instant (ms du demarrage)
	adr	x0,ntp_unix
	stp	x19,x20,[x0]
	adr	x0,ntp_ok
	mov	x1,1
	str	x1,[x0]
	mov	x0,x19				; heure locale
	bl	tz_offset
	add	x21,x19,x0
	mov	x1,x0				; l'horloge du systeme
	mov	x0,x19
	sys	SYS_CLOCK_SET
	mov	x0,1000
	udiv	x1,x20,x0			; - secondes depuis le demarrage
	sub	x21,x21,x1
	movl	x1,86400
	udiv	x2,x21,x1
	msub	x21,x2,x1,x21
	adr	x0,cfgbuf			; SD0:/etc/clock.cfg
	mov	x1,x0
	mov	x0,x21
	bl	fmt_dec
	mov	w1,10
	strb	w1,[x0],1
	adr	x1,cfgbuf
	sub	x22,x0,x1
	adr	x0,p_clock
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.journal
	mov	x21,x0
	adr	x1,cfgbuf
	mov	x2,x22
	sys	SYS_WRITE
	mov	x0,x21
	sys	SYS_CLOSE
.journal:
	adr	x0,m_ntp			; " netd : NTP 1730000000 (UTC) "
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_utc
	bl	puts
	mov	x0,0
	b	.out
.off:	mov	x0,E_NOENT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = serveur -> x0 = secondes depuis 1970 (< 0 : erreur), x1 = instant
; de la reponse (ms du demarrage). Socket UDP interne, 3 essais de 2 s.
ntp_query:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,128
	mov	w20,w0
	bl	arp_resolve_wait
	cbz	x0,.unreach
	mov	x0,sp				; requete : 48 octets, LI 0, VN 4, mode 3
	mov	x1,128
	bl	mem_zero
	mov	w0,0x23
	strb	w0,[sp]
	net_lock_take
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
	mov	w2,123
	mov	x3,sp
	mov	x4,48
	bl	udp_output
	net_lock_drop
	bl	time_ms
	add	x21,x0,2000
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
	mov	x19,E_TIMEDOUT
	b	.free
.reply:	add	x1,sp,64			; en-tete de l'enregistrement
	mov	x0,x23
	mov	x2,8
	bl	sk_rx_read
	add	x1,sp,64
	ldrh	w22,[x1]			; longueur
	cmp	x22,48
	mov	x2,48
	csel	x2,x22,x2,ls
	sub	x3,x22,x2			; reste a jeter
	str	x3,[sp,-16]!
	mov	x0,x23
	add	x1,sp,64+16
	bl	sk_rx_read
	ldr	x3,[sp],16
	ldr	x1,[x23,SK_RXTAIL]
	add	x1,x1,x3
	str	x1,[x23,SK_RXTAIL]
	net_lock_drop
	cmp	x22,48
	b.lo	.wait
	ldrb	w0,[sp,64]			; mode 4 (serveur), strate non nulle
	and	w0,w0,7
	cmp	w0,4
	b.ne	.wait
	ldrb	w0,[sp,64+1]
	cbz	w0,.wait
	ld32be	w19,sp,64+40			; horodatage d'emission (secondes)
	movl	x0,NTP_1970
	sub	x19,x19,x0
	bl	time_ms
	mov	x20,x0
.free:	net_lock_take
	mov	x0,x23
	bl	sock_free
	net_lock_drop
	mov	x0,x19
	mov	x1,x20
	b	.out
.unreach:
	mov	x0,E_HOSTUNREACH
.out:	add	sp,sp,128
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = heure UTC (s depuis 1970) -> x0 = decalage de l'heure locale (s)
; d'apres tz_name : UTC/GMT[+-h], fuseaux d'Europe (heure d'ete de l'UE :
; du dernier dimanche de mars au dernier dimanche d'octobre, 01:00 UTC) ;
; inconnu ou absent : Europe/Paris
tz_offset:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x20,tz_name
	ldrb	w0,[x20]			; " UTC+2 ", " GMT-5 "
	cmp	w0,'U'
	b.eq	.utc
	cmp	w0,'G'
	b.ne	.table
.utc:	ldrb	w0,[x20,3]
	cbz	w0,.zero
	cmp	w0,'+'
	b.eq	.fixe
	cmp	w0,'-'
	b.ne	.table
.fixe:	mov	x21,x0
	add	x0,x20,4
	bl	parse_dec
	mov	x1,3600
	mul	x0,x0,x1
	cmp	x21,'-'
	cneg	x0,x0,eq
	b	.out
.zero:	mov	x0,0
	b	.out
.table:	adr	x21,tz_table			; (nom, base en heures, ete)
.t:	ldr	w0,[x21]
	cbz	w0,.defaut
	adr	x1,tz_table
	add	x1,x1,x0
	mov	x0,x20
	bl	str_eq
	cbnz	x0,.trouve
	add	x21,x21,12
	b	.t
.defaut:
	mov	x0,3600				; Europe/Paris
	mov	x22,1
	b	.calc
.trouve:
	ldrsw	x0,[x21,4]
	mov	x1,3600
	mul	x0,x0,x1
	ldr	w22,[x21,8]
.calc:	mov	x20,x0
	cbz	x22,.base
	mov	x0,x19				; heure d'ete ?
	bl	eu_dst
	cbz	x0,.base
	add	x20,x20,3600
.base:	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = heure UTC -> x0 = 1 pendant l'heure d'ete de l'Union europeenne
eu_dst:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	movl	x1,86400			; annee
	udiv	x0,x19,x1
	bl	year_of
	mov	x20,x0
	mov	x1,3				; dernier dimanche de mars
	bl	last_sunday
	movl	x1,86400
	mul	x21,x0,x1
	add	x21,x21,3600
	mov	x0,x20				; ... d'octobre
	mov	x1,10
	bl	last_sunday
	movl	x1,86400
	mul	x22,x0,x1
	add	x22,x22,3600
	mov	x0,0
	cmp	x19,x21
	b.lo	.out
	cmp	x19,x22
	b.hs	.out
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = annee, x1 = mois (3..12, 31 jours) -> x0 = jour (depuis 1970) du
; dernier dimanche du mois
last_sunday:
	sub	x1,x1,3				; jours depuis 1970 du 31 (H. Hinnant)
	mov	x2,153
	mul	x2,x1,x2
	add	x2,x2,2
	mov	x3,5
	udiv	x2,x2,x3
	add	x2,x2,30			; jour de l'annee (depuis le 1er mars)
	mov	x3,400
	udiv	x4,x0,x3			; ere
	msub	x5,x4,x3,x0			; annee de l'ere
	mov	x6,365
	mul	x7,x5,x6
	add	x7,x7,x5,lsr 2
	mov	x6,100
	udiv	x8,x5,x6
	sub	x7,x7,x8
	add	x7,x7,x2			; jour de l'ere
	movl	x6,146097
	madd	x0,x4,x6,x7
	movl	x6,719468
	sub	x0,x0,x6
	add	x1,x0,4				; jour de la semaine (0 : dimanche)
	mov	x2,7
	udiv	x3,x1,x2
	msub	x1,x3,x2,x1
	sub	x0,x0,x1
	ret

; x0 = jours depuis 1970 -> x0 = annee
year_of:
	movl	x1,719468
	add	x0,x0,x1
	movl	x1,146097
	udiv	x2,x0,x1			; ere
	msub	x3,x2,x1,x0			; jour de l'ere
	movl	x4,1460
	udiv	x5,x3,x4
	sub	x6,x3,x5
	movl	x4,36524
	udiv	x5,x3,x4
	add	x6,x6,x5
	movl	x4,146096
	udiv	x5,x3,x4
	sub	x6,x6,x5
	mov	x4,365
	udiv	x6,x6,x4			; annee de l'ere
	mov	x4,400
	madd	x0,x2,x4,x6
	mov	x4,365				; jour de l'annee (depuis mars)
	mul	x5,x6,x4
	add	x5,x5,x6,lsr 2
	mov	x4,100
	udiv	x7,x6,x4
	sub	x5,x5,x7
	sub	x5,x3,x5
	mov	x4,5
	mul	x5,x5,x4
	add	x5,x5,2
	mov	x4,153
	udiv	x5,x5,x4			; mois depuis mars
	cmp	x5,10				; janvier, fevrier : annee suivante
	cinc	x0,x0,hs
	ret

; --- 16 time -> a heure UTC (0 : inconnue), b 1 si NTP, c instant de la synchro
op_time:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,ntp_ok
	ldr	x0,[x0]
	str	x0,[x19,WM_B]
	cbz	x0,.non
	adr	x0,ntp_unix
	ldp	x1,x20,[x0]
	str	x20,[x19,WM_C]
	bl	time_ms
	sub	x0,x0,x20
	mov	x2,1000
	udiv	x0,x0,x2
	add	x0,x0,x1
	str	x0,[x19,WM_A]
	mov	x0,0
	b	.out
.non:	str	xzr,[x19,WM_A]
	str	xzr,[x19,WM_C]
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- 17 ntp -> 0 apres une synchronisation reussie (10 s au plus) ---------------
op_ntp:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x20,[x0,WM_A]			; a = 0 : demande seulement
	adr	x0,ntp_gen
	ldr	x19,[x0]
	adr	x0,ntp_force
	mov	x1,1
	str	x1,[x0]
	mov	x0,0
	cbz	x20,.out
	bl	time_ms
	movl	x1,20000
	add	x20,x0,x1
.w:	adr	x0,ntp_gen
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.fait
	bl	time_ms
	cmp	x0,x20
	b.hs	.delai
	mov	x0,50
	bl	thread_sleep
	b	.w
.fait:	adr	x0,ntp_last
	ldr	x0,[x0]
	b	.out
.delai:	mov	x0,E_TIMEDOUT
.out:	ldp	x19,x20,[sp],16
	epilogue

include '../../inc/notify.inc'
include '../../inc/applist.inc'			; ini_read, str_copy_n

p_options	db 'SD0:/etc/options.txt',0
p_clock		db 'SD0:/etc/clock.cfg',0
k_fuseau	db 'fuseau',0
k_ntp		db 'ntp',0
v_non		db 'non',0
s_pool		db 'pool.ntp.org',0
m_ntp		db 'netd : NTP ',0
m_utc		db ' (UTC)',10,0
m_ntf		db 'netd : ',0
m_nl		db 10,0
t_connecte	db 'R',0xC3,0xA9,'seau connect',0xC3,0xA9,0
t_perdu		db 'R',0xC3,0xA9,'seau perdu',0
t_eth		db 'Ethernet',0
t_usb		db 'USB',0
t_wifi		db 'Wi-Fi ',0xC2,0xAB,' ',0
t_guil		db ' ',0xC2,0xBB,0
t_tiret		db ' ',0xE2,0x80,0x94,' ',0
		align 4
; fuseaux : decalage du nom depuis tz_table, base (heures), heure d'ete de l'UE
tz_table:
	dw	tz_london - tz_table, 0, 1
	dw	tz_lisbon - tz_table, 0, 1
	dw	tz_dublin - tz_table, 0, 1
	dw	tz_canary - tz_table, 0, 1
	dw	tz_athens - tz_table, 2, 1
	dw	tz_helsinki - tz_table, 2, 1
	dw	tz_kiev - tz_table, 2, 1
	dw	tz_kyiv - tz_table, 2, 1
	dw	tz_bucarest - tz_table, 2, 1
	dw	tz_sofia - tz_table, 2, 1
	dw	tz_riga - tz_table, 2, 1
	dw	tz_tallinn - tz_table, 2, 1
	dw	tz_vilnius - tz_table, 2, 1
	dw	tz_moscow - tz_table, 3, 0
	dw	tz_istanbul - tz_table, 3, 0
	dw	tz_minsk - tz_table, 3, 0
	dw	tz_ny - tz_table, -5, 0
	dw	tz_tokyo - tz_table, 9, 0
	dw	0, 0, 0
tz_london	db 'Europe/London',0
tz_lisbon	db 'Europe/Lisbon',0
tz_dublin	db 'Europe/Dublin',0
tz_canary	db 'Atlantic/Canary',0
tz_athens	db 'Europe/Athens',0
tz_helsinki	db 'Europe/Helsinki',0
tz_kiev		db 'Europe/Kiev',0
tz_kyiv		db 'Europe/Kyiv',0
tz_bucarest	db 'Europe/Bucharest',0
tz_sofia	db 'Europe/Sofia',0
tz_riga		db 'Europe/Riga',0
tz_tallinn	db 'Europe/Tallinn',0
tz_vilnius	db 'Europe/Vilnius',0
tz_moscow	db 'Europe/Moscow',0
tz_istanbul	db 'Europe/Istanbul',0
tz_minsk	db 'Europe/Minsk',0
tz_ny		db 'America/New_York',0
tz_tokyo	db 'Asia/Tokyo',0
		align 8

macro bg_data {
		align 8
ntp_next	dd 0				; prochaine synchronisation (ms)
ntp_force	dd 0				; demandee (op 17)
ntp_gen		dd 0				; synchronisations faites
ntp_last	dd 0				; resultat de la derniere
ntp_unix	dd 0				; heure UTC a la synchro, instant (ms)
ntp_ms		dd 0
ntp_ok		dd 0
ntp_off		dd 0				; ntp=non
tz_name		rb 32
ntp_name	rb 64
nbuf		rb 160
cfgbuf		rb 32
applis_data
}
