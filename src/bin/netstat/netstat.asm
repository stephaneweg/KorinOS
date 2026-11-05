; netstat - les interfaces, la configuration IP et les sockets ouverts de tout le systeme
; (docs/OUTILS.md ; d'apres netstat d'Onyx) :
;   netstat [-t] [-u] [-l] [-i]
;   -t : TCP seulement, -u : UDP seulement, -l : les sockets en ecoute seulement,
;   -i : les interfaces et la configuration seulement
; Les sockets viennent de netd (NET_OP_SOCKETS, net_sockets de NetKit) : protocole, etat,
; port local, adresse distante, octets en attente de lecture et d'emission, programme.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../inc/netd.inc'
include '../../kits/netkit/netkit.inc'

SK_MAX		= 128			; sockets lus au plus
IF_MAXE		= 8

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	t_debut
	mov	x21,0				; options : bit 0 -t, 1 -u, 2 -l, 3 -i
	mov	x23,1
.arg:	cmp	x23,x19
	b.hs	.go
	ldr	x24,[x20,x23,lsl 3]
	add	x23,x23,1
	ldrb	w0,[x24]
	cmp	w0,'-'
	b.ne	.usage
	add	x24,x24,1
.o:	ldrb	w0,[x24],1
	cbz	w0,.arg
	cmp	w0,'t'
	b.ne	.o1
	orr	x21,x21,1
	b	.o
.o1:	cmp	w0,'u'
	b.ne	.o2
	orr	x21,x21,2
	b	.o
.o2:	cmp	w0,'l'
	b.ne	.o3
	orr	x21,x21,4
	b	.o
.o3:	cmp	w0,'i'
	b.ne	.o4
	orr	x21,x21,8
	b	.o
.o4:	cmp	w0,'n'				; (-n, -a : sans effet, tout est numerique)
	b.eq	.o
	cmp	w0,'a'
	b.eq	.o
	b	.usage
.go:	tst	x21,3				; ni -t ni -u : les deux
	b.ne	.g1
	orr	x21,x21,3
.g1:	tst	x21,4				; -l seul : pas la configuration
	b.ne	.socks
	bl	config
	tst	x21,8
	b.ne	.fin
	bl	t_nl
.socks:	bl	sockets
.fin:	mov	x0,0
	bl	t_fin
.usage:	adr	x0,m_usage
	bl	t_derr
	mov	x0,2
	bl	t_fin

; --- interfaces et configuration --------------------------------------------------------
config:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,ifs
	mov	x1,IF_MAXE
	bl	net_ifaces
	cmp	x0,0
	b.gt	.if
	adr	x0,m_noif
	bl	t_dire
	b	.ip
.if:	mov	x22,x0
	adr	x23,ifs
.e:	cbz	x22,.ip
	add	x0,x23,NIF_E_NAME		; nom (8 octets, pas toujours termine)
	ldr	x1,[x0]
	str	x1,[sp,-16]!
	strb	wzr,[sp,8]
	mov	x0,sp
	mov	x1,8
	bl	t_col
	add	sp,sp,16
	ldr	w0,[x23,NIF_E_TYPE]
	adr	x1,m_ether
	cmp	w0,NIF_ETHERNET
	b.eq	.ty
	adr	x1,m_wifi
	cmp	w0,NIF_WIFI
	b.eq	.ty
	adr	x1,m_usb
	cmp	w0,NIF_USB
	b.eq	.ty
	adr	x1,m_inconnu
.ty:	mov	x0,x1
	mov	x1,10
	bl	t_col
	ldr	w0,[x23,NIF_E_LINK]
	adr	x1,m_lien_non
	cbz	w0,.li
	adr	x1,m_lien_oui
.li:	mov	x0,x1
	bl	t_dire
	ldr	w0,[x23,NIF_E_LINK]
	cbz	w0,.ac
	ldr	w0,[x23,NIF_E_SPEED]
	cbz	w0,.ac
	adr	x0,m_sep
	bl	t_puts
	ldr	w0,[x23,NIF_E_SPEED]
	bl	t_putn
	adr	x0,m_mbs
	bl	t_puts
.ac:	ldr	w0,[x23,NIF_E_ACTIVE]
	cbz	w0,.ss
	adr	x0,m_active
	bl	t_dire
.ss:	ldrb	w0,[x23,NIF_E_SSID]
	cbz	w0,.nl
	adr	x0,m_ssid
	bl	t_puts
	add	x0,x23,NIF_E_SSID
	strb	wzr,[x0,31]
	bl	t_puts
	adr	x0,m_guill
	bl	t_puts
.nl:	bl	t_nl
	add	x23,x23,NIF_ENT
	sub	x22,x22,1
	b	.e
.ip:	sub	sp,sp,48			; netinfo
	mov	x0,sp
	bl	net_info
	bl	t_nl
	adr	x0,m_adresse
	ldr	w1,[sp,NI_IP]
	bl	ligne_ip
	adr	x0,m_masque
	ldr	w1,[sp,NI_MASK]
	bl	ligne_ip
	adr	x0,m_passerelle
	ldr	w1,[sp,NI_GW]
	bl	ligne_ip
	adr	x0,m_dns
	ldr	w1,[sp,NI_DNS]
	bl	ligne_ip
	adr	x0,m_mac			; MAC
	bl	dire12
	mov	x22,0
.m:	add	x0,sp,NI_MAC
	ldrb	w0,[x0,x22]
	bl	hex2
	add	x22,x22,1
	cmp	x22,6
	b.hs	.mf
	mov	w0,':'
	bl	t_putc
	b	.m
.mf:	bl	t_nl
	adr	x0,m_dhcp
	bl	dire12
	ldr	w0,[sp,NI_DHCP]
	adr	x1,m_bail
	cmp	w0,4
	b.eq	.dh
	adr	x1,m_attente
	ldr	w0,[sp,NI_UP]
	cbnz	w0,.dh
	adr	x1,m_pas_lien
.dh:	mov	x0,x1
	bl	t_dire
	bl	t_nl
	add	sp,sp,48
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = libelle, w1 = adresse IPv4 (0 : aucune) : une ligne
ligne_ip:
	prologue
	str	x19,[sp,-16]!
	mov	w19,w1
	bl	dire12
	cbz	w19,.aucune
	mov	w0,w19
	bl	nt_put_ip
	b	.nl
.aucune:
	adr	x0,m_aucune
	bl	t_dire
.nl:	bl	t_nl
	ldr	x19,[sp],16
	epilogue

; x0 = libelle : traduit, colonne de 12
dire12:
	prologue
	bl	lang_tr
	mov	x1,12
	bl	t_col
	epilogue

; w0 = octet : deux chiffres hexadecimaux
hex2:
	prologue
	str	x19,[sp,-16]!
	and	w19,w0,0xFF
	lsr	w0,w19,4
	bl	.c
	and	w0,w19,15
	bl	.c
	ldr	x19,[sp],16
	epilogue
.c:	cmp	w0,10
	add	w1,w0,'0'
	add	w2,w0,'a'-10
	csel	w0,w1,w2,lo
	b	t_putc

; --- sockets ----------------------------------------------------------------------------
sockets:
	prologue
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	sub	sp,sp,96			; nom de programme (SYS_PROC_INFO), adresse
	adr	x0,socks
	mov	x1,SK_MAX
	bl	net_sockets
	tbnz	x0,63,.vide
	mov	x22,x0
	adr	x23,socks
	mov	x25,0				; lignes affichees
.s:	cbz	x22,.fin
	ldr	w24,[x23,NSK_TYPE]		; filtres
	cmp	w24,SOCK_TCP
	b.ne	.udp
	tbz	x21,0,.n
	b	.l
.udp:	tbz	x21,1,.n
.l:	tbz	x21,2,.aff
	ldr	w0,[x23,NSK_STATE]		; -l : en ecoute (TCP), UDP sans pair
	cmp	w24,SOCK_TCP
	b.ne	.lu
	cmp	w0,1				; TCP_LISTEN
	b.ne	.n
	b	.aff
.lu:	ldr	w0,[x23,NSK_RIP]
	cbnz	w0,.n
.aff:	cbnz	x25,.ligne
	adr	x0,m_entete
	bl	t_dire
.ligne:	add	x25,x25,1
	adr	x0,m_tcp
	cmp	w24,SOCK_TCP
	b.eq	.p
	adr	x0,m_udp
.p:	mov	x1,6
	bl	t_col
	adr	x0,m_tiret			; etat
	cmp	w24,SOCK_TCP
	b.ne	.et
	ldr	w0,[x23,NSK_STATE]
	cmp	w0,10
	b.hi	.et
	adr	x1,etats
	ldrh	w0,[x1,x0,lsl 1]
	add	x0,x1,x0
.et:	mov	x1,12
	bl	t_col
	ldr	w0,[x23,NSK_LPORT]		; port local
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	mov	x1,8
	bl	t_col
	ldr	w0,[x23,NSK_RIP]		; adresse distante
	cbnz	w0,.ra
	adr	x0,m_etoile
	b	.rc
.ra:	mov	x1,sp
	bl	fmt_ip
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,x0
	ldr	w0,[x23,NSK_RPORT]
	bl	fmt_dec
	mov	x0,sp
.rc:	mov	x1,22
	bl	t_col
	ldr	w0,[x23,NSK_RXQ]		; files
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	mov	x1,8
	bl	t_col
	ldr	w0,[x23,NSK_TXQ]
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	mov	x1,8
	bl	t_col
	ldr	w0,[x23,NSK_OWNER]		; programme
	cbz	w0,.netd
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	bl	t_puts
	ldr	w0,[x23,NSK_OWNER]
	add	x1,sp,32
	sys	SYS_PROC_INFO
	tbnz	x0,63,.nl
	mov	w0,' '
	bl	t_putc
	add	x0,sp,32+24
	strb	wzr,[x0,31]
	bl	t_puts
	b	.nl
.netd:	adr	x0,m_netd
	bl	t_puts
.nl:	bl	t_nl
.n:	add	x23,x23,NSK_ENT
	sub	x22,x22,1
	b	.s
.vide:	mov	x25,0
.fin:	cbnz	x25,.out
	adr	x0,m_aucun
	bl	t_dire
.out:	add	sp,sp,96
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	epilogue

include '../../inc/tool.inc'
include '../../inc/nettool.inc'

m_usage		db 'usage : netstat [-t] [-u] [-l] [-i]',10,0
m_noif		db '(aucune interface : netd ne r',0xC3,0xA9,'pond pas)',10,0
m_ether		db 'ethernet',0
m_wifi		db 'wifi',0
m_usb		db 'usb',0
m_inconnu	db '?',0
m_lien_non	db 'pas de lien',0
m_lien_oui	db 'lien',0
m_sep		db ', ',0
m_mbs		db ' Mb/s',0
m_active	db ', active',0
m_ssid		db ', « ',0
m_guill		db ' »',0
m_adresse	db 'Adresse',0
m_masque	db 'Masque',0
m_passerelle	db 'Passerelle',0
m_dns		db 'DNS',0
m_mac		db 'MAC',0
m_dhcp		db 'DHCP',0
m_aucune	db '(aucune)',0
m_bail		db 'bail obtenu',0
m_attente	db 'en attente',0
m_pas_lien	db 'pas de lien',0
m_entete	db 'Proto ',0xC3,0x89,'tat        Local   Distant               Re',0xC3,0xA7,'us   ',0xC3,0x89,'mis    Programme',10,0
m_aucun		db '(aucun socket ouvert)',10,0
m_tcp		db 'tcp',0
m_udp		db 'udp',0
m_tiret		db '-',0
m_etoile	db '*',0
m_netd		db '- (netd)',0
		align 2
etats:		dh e0-etats, e1-etats, e2-etats, e3-etats, e4-etats, e5-etats
		dh e6-etats, e7-etats, e8-etats, e9-etats, e10-etats
e0		db 'CLOSED',0
e1		db 'LISTEN',0
e2		db 'SYN_SENT',0
e3		db 'SYN_RCVD',0
e4		db 'ESTABLISHED',0
e5		db 'FIN_WAIT1',0
e6		db 'FIN_WAIT2',0
e7		db 'CLOSE_WAIT',0
e8		db 'CLOSING',0
e9		db 'LAST_ACK',0
e10		db 'TIME_WAIT',0
		align 4

segment readable writeable
kit_data
tool_data
nettool_data
		align 8
ifs		rb IF_MAXE * NIF_ENT
socks		rb SK_MAX * NSK_ENT
