; wifi - etat, scan et connexion du Wi-Fi (service "wifi", par NetKit)
;   wifi                        etat de la connexion
;   wifi scan                   reseaux visibles
;   wifi join SSID [cle]        connexion, memorisee (sans cle : reseau ouvert)
;   wifi leave                  deconnexion (plus de reconnexion automatique)
;   wifi known                  reseaux connus (SD0:/etc/wifi.txt, sans les cles)
;   wifi forget SSID            oublie un reseau
;   wifi priority SSID N        priorite d'un reseau (le plus grand d'abord)
;   wifi country XX             pays (reglementation radio), tout de suite
; (anciens mots : connecter, quitter)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/netkit/netkit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	mov	x19,x0				; ligne d'arguments (ou 0)
	cbz	x19,do_status
	mov	x0,x19				; mots separes par des 0
	bl	split
	mov	x20,x0				; nombre de mots
	cbz	x20,do_status
	ldrb	w0,[x19]			; espaces en tete
	cbnz	w0,.first
	mov	x0,x19
	bl	next_word
	mov	x19,x0
.first:
	mov	x0,x19
	adr	x1,w_scan
	bl	same_word
	cbnz	x0,do_scan
	mov	x0,x19
	adr	x1,w_join
	bl	same_word
	cbnz	x0,do_join
	mov	x0,x19
	adr	x1,w_leave
	bl	same_word
	cbnz	x0,do_leave
	mov	x0,x19
	adr	x1,w_join_en
	bl	same_word
	cbnz	x0,do_join
	mov	x0,x19
	adr	x1,w_leave_en
	bl	same_word
	cbnz	x0,do_leave
	mov	x0,x19
	adr	x1,w_known
	bl	same_word
	cbnz	x0,do_known
	mov	x0,x19
	adr	x1,w_forget
	bl	same_word
	cbnz	x0,do_forget
	mov	x0,x19
	adr	x1,w_priority
	bl	same_word
	cbnz	x0,do_priority
	mov	x0,x19
	adr	x1,w_country
	bl	same_word
	cbnz	x0,do_country
	adr	x0,m_usage
	bl	lang_puts
	mov	x0,2
	sys	SYS_EXIT

; --- etat --------------------------------------------------------------------
do_status:
	adr	x0,wst
	netcall	wifi_status
	tbnz	x0,63,do_absent
	adr	x19,wst
	ldr	w0,[x19]
	cbz	w0,do_absent
	cmp	w0,1
	b.eq	.starting
	adr	x0,m_mac
	bl	lang_puts
	add	x0,x19,8
	bl	print_mac
	adr	x0,m_nl
	bl	puts
	ldr	w0,[x19]
	cmp	w0,4
	b.eq	.conn
	adr	x0,m_ready
	cmp	w0,3
	b.ne	.p
	adr	x0,m_joining
.p:	bl	lang_puts
	b	do_ok
.conn:	adr	x0,m_conn
	bl	lang_puts
	add	x0,x19,16
	bl	puts
	adr	x0,m_rssi
	bl	puts
	ldr	w0,[x19,4]
	sxtw	x0,w0
	bl	print_dec
	adr	x0,m_dbm
	bl	puts
	adr	x0,ni				; adresse IP (DHCP)
	netcall	net_info
	adr	x0,ni
	ldr	w0,[x0]
	cbz	w0,do_ok
	adr	x0,m_ip
	bl	lang_puts
	adr	x0,ni
	ldr	w0,[x0]
	bl	put_ip
	adr	x0,m_nl
	bl	puts
	b	do_ok
.starting:
	adr	x0,m_start
	bl	lang_puts
	b	do_ok

; --- scan -------------------------------------------------------------------------
do_scan:
	adr	x0,m_scanning
	bl	lang_puts
	adr	x0,tab
	mov	x1,64
	netcall	wifi_scan
	tbnz	x0,63,do_err
	mov	x20,x0
	cbz	x20,.none
	adr	x0,m_head
	bl	lang_puts
	adr	x19,tab
.e:	ldrsh	x0,[x19,36]			; RSSI
	bl	print_dec
	adr	x0,m_sep
	bl	puts
	ldrb	w0,[x19,33]			; canal
	bl	print_dec
	adr	x0,m_sep
	bl	puts
	ldrb	w0,[x19,34]			; securite
	adr	x1,sec_names
	ldr	x0,[x1,x0,lsl 3]
	bl	lang_puts
	ldrb	w0,[x19,32]			; SSID (pas forcement termine)
	add	x1,x19,x0
	strb	wzr,[x1]
	mov	x0,x19
	bl	puts
	adr	x0,m_nl
	bl	puts
	add	x19,x19,48
	subs	x20,x20,1
	b.ne	.e
	b	do_ok
.none:	adr	x0,m_none
	bl	lang_puts
	b	do_ok

; --- connexion -------------------------------------------------------------------
do_join:
	cmp	x20,2
	b.lo	do_usage
	mov	x0,x19				; 2e mot : SSID
	bl	next_word
	mov	x21,x0
	mov	x22,0				; 3e mot : cle (facultative)
	cmp	x20,3
	b.lo	.go
	bl	next_word
	mov	x22,x0
.go:	adr	x0,m_joining2
	bl	lang_puts
	mov	x0,x21
	bl	puts
	adr	x0,m_dots
	bl	puts
	mov	x0,x21
	mov	x1,x22
	netcall	wifi_join
	tbnz	x0,63,do_err
	adr	x0,m_joined
	bl	lang_puts
	b	do_ok

do_leave:
	netcall	wifi_leave
	tbnz	x0,63,do_err
	adr	x0,m_left
	bl	lang_puts
	b	do_ok

; --- reseaux connus ----------------------------------------------------------------
do_known:
	adr	x0,tab
	mov	x1,16
	netcall	wifi_known
	tbnz	x0,63,do_err
	mov	x20,x0
	cbz	x20,.none
	adr	x19,tab
.e:	ldr	w0,[x19,40]			; priorite
	bl	print_dec
	adr	x0,m_sep
	bl	puts
	ldr	w0,[x19,36]			; securite
	adr	x1,sec_names
	ldr	x0,[x1,x0,lsl 3]
	bl	lang_puts
	mov	x0,x19
	bl	puts
	adr	x0,m_nl
	bl	puts
	add	x19,x19,48
	subs	x20,x20,1
	b.ne	.e
	b	do_ok
.none:	adr	x0,m_noknown
	bl	lang_puts
	b	do_ok

do_forget:
	cmp	x20,2
	b.lo	do_usage
	mov	x0,x19
	bl	next_word
	netcall	wifi_forget
	tbnz	x0,63,do_err
	b	do_ok

do_priority:
	cmp	x20,3
	b.lo	do_usage
	mov	x0,x19
	bl	next_word
	mov	x21,x0
	bl	next_word
	bl	parse_dec
	mov	x1,x0
	mov	x0,x21
	netcall	wifi_priority
	tbnz	x0,63,do_err
	b	do_ok

do_country:
	cmp	x20,2
	b.lo	do_usage
	mov	x0,x19
	bl	next_word
	netcall	wifi_country
	tbnz	x0,63,do_err
	b	do_ok

do_usage:	adr	x0,m_usage
	bl	lang_puts
	mov	x0,2
	sys	SYS_EXIT

do_absent:	adr	x0,m_absent
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = code d'erreur
do_err:	mov	x19,x0
	adr	x1,errs				; (code, message)
.f:	ldr	x2,[x1]
	cbz	x2,.gen
	cmp	x2,x19
	b.eq	.msg
	add	x1,x1,16
	b	.f
.msg:	ldr	x0,[x1,8]
	bl	lang_puts
	b	.out
.gen:	adr	x0,m_err
	bl	lang_puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.out:	mov	x0,1
	sys	SYS_EXIT

do_ok:	mov	x0,0
	sys	SYS_EXIT

; x0 = chaine : remplace les espaces par des 0 -> x0 = nombre de mots
split:
	mov	x1,0				; mots
	mov	x2,0				; dans un mot
.c:	ldrb	w3,[x0]
	cbz	w3,.e
	cmp	w3,' '
	b.ne	.w
	strb	wzr,[x0]
	mov	x2,0
	b	.n
.w:	cbnz	x2,.n
	mov	x2,1
	add	x1,x1,1
.n:	add	x0,x0,1
	b	.c
.e:	strb	wzr,[x0,1]			; double 0 final : fin de liste
	mov	x0,x1
	ret

; x0 = mot -> x0 = mot suivant (apres le(s) 0)
next_word:
.a:	ldrb	w1,[x0],1
	cbnz	w1,.a
.b:	ldrb	w1,[x0]
	cbnz	w1,.e
	add	x0,x0,1
	b	.b
.e:	ret

; x0, x1 = chaines -> x0 = 1 si egales
same_word:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.no
	cbnz	w2,.c
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = 6 octets -> aa:bb:cc:dd:ee:ff
print_mac:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x20,0
	mov	x3,sp
.b:	ldrb	w0,[x19,x20]
	adr	x2,hex
	lsr	w1,w0,4
	ldrb	w1,[x2,x1]
	strb	w1,[x3],1
	and	w0,w0,0xF
	ldrb	w1,[x2,x0]
	strb	w1,[x3],1
	add	x20,x20,1
	cmp	x20,6
	b.hs	.e
	mov	w1,':'
	strb	w1,[x3],1
	b	.b
.e:	strb	wzr,[x3]
	mov	x0,sp
	bl	puts
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue


; w0 = adresse IPv4 -> sortie standard (le print_ip de NetKit ecrit sur la
; console du noyau, donc dans le journal, pas dans le tube du terminal)
put_ip:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_ip
	mov	x0,sp
	bl	puts
	add	sp,sp,32
	epilogue

segment readable writeable
kit_data

w_scan		db 'scan',0
w_join		db 'connecter',0
w_leave		db 'quitter',0
w_join_en	db 'join',0
w_leave_en	db 'leave',0
w_known		db 'known',0
w_forget	db 'forget',0
w_priority	db 'priority',0
w_country	db 'country',0
m_noknown	db 'wifi : aucun r',0xC3,0xA9,'seau connu',10,0
hex		db '0123456789abcdef'
m_usage		db 'usage : wifi [scan | join SSID [cle] | leave | known | forget SSID |'
		db ' priority SSID N | country XX]',10,0
m_absent	db 'wifi : pas de WiFi (puce absente, firmware manquant ou QEMU)',10,0
m_start		db 'wifi : d',0xC3,0xA9,'marrage en cours',10,0
m_mac		db 'wifi : MAC ',0
m_ready		db 'wifi : non connect',0xC3,0xA9,10,0
m_joining	db 'wifi : connexion en cours',10,0
m_conn		db 'wifi : connect',0xC3,0xA9,' ',0xC3,0xA0,' ',0
m_rssi		db ', signal ',0
m_dbm		db ' dBm',10,0
m_ip		db 'wifi : adresse ',0
m_scanning	db 'wifi : recherche des r',0xC3,0xA9,'seaux...',10,0
m_head		db 'dBm | canal | s',0xC3,0xA9,'curit',0xC3,0xA9,' | r',0xC3,0xA9,'seau',10,0
m_sep		db ' | ',0
m_none		db 'wifi : aucun r',0xC3,0xA9,'seau visible',10,0
m_joining2	db 'wifi : connexion ',0xC3,0xA0,' ',0
m_dots		db '...',10,0
m_joined	db 'wifi : connect',0xC3,0xA9,' (adresse IP par DHCP)',10,0
m_left		db 'wifi : d',0xC3,0xA9,'connect',0xC3,0xA9,10,0
m_err		db 'wifi : erreur ',0
m_e_nodev	db 'wifi : pas de WiFi',10,0
m_e_noent	db 'wifi : r',0xC3,0xA9,'seau introuvable',10,0
m_e_acces	db 'wifi : cl',0xC3,0xA9,' refus',0xC3,0xA9,'e',10,0
m_e_time	db 'wifi : d',0xC3,0xA9,'lai d',0x27,'attente d',0xC3,0xA9,'pass',0xC3,0xA9,10,0
m_e_inval	db 'wifi : SSID (1-32) ou cl',0xC3,0xA9,' (8-63 caract',0xC3,0xA8,'res) invalide',10,0
m_nl		db 10,0
s_open		db 'ouvert | ',0
s_wep		db 'WEP    | ',0
s_wpa		db 'WPA    | ',0
s_wpa2		db 'WPA2   | ',0
		align 8
sec_names	dd s_open, s_wep, s_wpa, s_wpa2
errs		dd -19, m_e_nodev, -2, m_e_noent, -13, m_e_acces
		dd -110, m_e_time, -22, m_e_inval, 0
		align 8
wst		rb 64
ni		rb 32
tab		rb 64 * 48
