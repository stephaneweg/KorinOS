; wget - un fichier par HTTP (docs/OUTILS.md ; d'apres wget d'Onyx) :
;   wget [-O FICHIER] [-q] [-S] URL
; URL : http://hote[:port][/chemin] (sans « http:// » : HTTP aussi). Sans -O (ou -O -) : le corps
; sur la sortie standard, comme wget d'Onyx ; le bilan va sur la sortie d'erreur. -q : sans bilan,
; -S : les en-tetes de la reponse sur la sortie d'erreur. Les redirections (301, 302, 303, 307,
; 308) sont suivies, 5 au plus. Requete HTTP/1.0 (ni morceaux, ni connexion gardee).
; HTTPS : non. Il n'y a pas de TLS pour les programmes en assembleur (OpenSSL est le seul TLS du
; systeme, pour les programmes C) : une URL https:// est refusee avec ce message.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../kits/netkit/netkit.inc'

REDIR_MAX	= 5
HOTE_MAX	= 128
CHEMIN_MAX	= 1024
CORPS		= 16384

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	t_debut
	mov	x21,0				; fichier de sortie (0 : la sortie standard)
	mov	x22,0				; bit 0 : -q, bit 1 : -S
	mov	x23,0				; URL
	mov	x24,1
.arg:	cmp	x24,x19
	b.hs	.go
	ldr	x25,[x20,x24,lsl 3]
	add	x24,x24,1
	ldrb	w0,[x25]
	cmp	w0,'-'
	b.ne	.url
	ldrb	w0,[x25,1]
	cbz	w0,.usage
	ldrb	w1,[x25,2]
	cbnz	w1,.usage
	cmp	w0,'q'
	b.ne	.o1
	orr	x22,x22,1
	b	.arg
.o1:	cmp	w0,'S'
	b.ne	.o2
	orr	x22,x22,2
	b	.arg
.o2:	cmp	w0,'O'
	b.ne	.usage
	cmp	x24,x19
	b.hs	.usage
	ldr	x21,[x20,x24,lsl 3]
	add	x24,x24,1
	ldrb	w0,[x21]			; « -O - » : la sortie standard
	cmp	w0,'-'
	b.ne	.arg
	ldrb	w0,[x21,1]
	cbnz	w0,.arg
	mov	x21,0
	b	.arg
.url:	cbnz	x23,.usage
	mov	x23,x25
	b	.arg
.go:	cbz	x23,.usage
	mov	x0,x23
	bl	url_lire
	cmn	x0,2
	b.eq	.https
	tbnz	x0,63,.mauvaise
	bl	nt_reseau
	cbz	x0,.nonet
	mov	x24,0				; redirections
.essai:	bl	requete				; -> x0 = code HTTP, < 0 : erreur reseau
	tbnz	x0,63,.echec_net
	mov	x26,x0
	bl	est_redirection
	cbz	x0,.final
	adr	x0,location
	ldrb	w0,[x0]
	cbz	w0,.final
	add	x24,x24,1
	cmp	x24,REDIR_MAX
	b.hi	.trop
	mov	x0,x25
	bl	net_close
	tbnz	x22,0,.r1			; « -> nouvelle adresse »
	adr	x0,m_redir
	bl	t_derr
	adr	x0,location
	bl	t_err
	adr	x0,m_nl
	bl	t_err
.r1:	adr	x0,location
	ldrb	w1,[x0]
	cmp	w1,'/'
	b.eq	.relatif
	bl	url_lire
	cmn	x0,2
	b.eq	.https
	tbnz	x0,63,.mauvaise
	b	.essai
.relatif:					; meme hote, autre chemin
	adr	x0,chemin
	adr	x1,location
	mov	x2,CHEMIN_MAX
	bl	copie_bornee
	b	.essai
.final:	sub	x0,x26,200			; 2xx : le corps
	cmp	x0,100
	b.lo	.corps
	cbnz	x21,.ferme_err			; une erreur HTTP : le corps sur la sortie
	mov	x27,1				; standard seulement (comme Onyx)
	bl	t_flush
	bl	corps
.ferme_err:
	mov	x0,x25
	bl	net_close
	adr	x0,m_http_err
	bl	t_derr
	mov	x0,x26
	bl	put_err_dec
	adr	x0,m_nl
	bl	t_err
	mov	x0,1
	bl	t_fin
.corps:	mov	x27,1
	cbz	x21,.sortie
	mov	x0,x21
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fic
	mov	x27,x0
.sortie:
	bl	t_flush
	bl	corps				; -> x0 = octets, x1 = 1 si complet
	mov	x28,x0
	mov	x26,x1
	mov	x0,x25
	bl	net_close
	cmp	x27,1
	b.eq	.bilan
	mov	x0,x27
	sys	SYS_CLOSE
.bilan:	tbnz	x22,0,.fin
	adr	x0,m_bilan
	bl	t_derr
	mov	x0,x28
	bl	put_err_dec
	adr	x0,m_octets
	bl	t_derr
	cbz	x21,.b1
	adr	x0,m_vers
	bl	t_err
	mov	x0,x21
	bl	t_err
.b1:	adr	x0,m_nl
	bl	t_err
.fin:	cbz	x26,.incomplet
	mov	x0,0
	bl	t_fin
.incomplet:
	adr	x0,m_incomplet
	bl	t_derr
	mov	x0,1
	bl	t_fin
.fic:	mov	x0,x21
	bl	t_err_fic
	mov	x0,x25
	bl	net_close
	mov	x0,1
	bl	t_fin
.trop:	adr	x0,m_trop
	bl	t_derr
	mov	x0,1
	bl	t_fin
.echec_net:
	mov	x25,x0
	adr	x0,m_echec
	bl	t_derr
	adr	x0,hote
	bl	t_err
	adr	x0,m_deux_points
	bl	t_err
	mov	x0,x25
	bl	nt_err_texte
	adr	x0,m_nl
	bl	t_err
	mov	x0,1
	bl	t_fin
.https:	adr	x0,m_https
	bl	t_derr
	mov	x0,1
	bl	t_fin
.mauvaise:
	adr	x0,m_mauvaise
	bl	t_derr
	mov	x0,1
	bl	t_fin
.nonet:	adr	x0,m_nonet
	bl	t_derr
	mov	x0,1
	bl	t_fin
.usage:	adr	x0,m_usage
	bl	t_derr
	mov	x0,2
	bl	t_fin

; x0 = valeur : en decimal sur la sortie d'erreur
put_err_dec:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	bl	t_err
	add	sp,sp,32
	epilogue

; x0 = URL -> hote, port, chemin ; x0 = 0, -1 si mauvaise, -2 si https://
url_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,s_https
	bl	prefixe_i
	cbnz	x0,.https
	mov	x0,x19
	adr	x1,s_http
	bl	prefixe_i
	cbz	x0,.hote
	mov	x19,x0
.hote:	adr	x2,hote				; jusqu'a « : », « / » ou la fin
	mov	x3,0
.h:	ldrb	w4,[x19]
	cbz	w4,.hf
	cmp	w4,':'
	b.eq	.hf
	cmp	w4,'/'
	b.eq	.hf
	cmp	w4,'?'
	b.eq	.hf
	cmp	x3,HOTE_MAX-1
	b.hs	.bad
	strb	w4,[x2,x3]
	add	x3,x3,1
	add	x19,x19,1
	b	.h
.hf:	strb	wzr,[x2,x3]
	cbz	x3,.bad
	mov	x20,80
	ldrb	w4,[x19]
	cmp	w4,':'
	b.ne	.ch
	add	x19,x19,1
	mov	x20,0
.p:	ldrb	w4,[x19]
	sub	w5,w4,'0'
	cmp	w5,9
	b.hi	.pf
	mov	x6,10
	madd	x20,x20,x6,x5
	add	x19,x19,1
	b	.p
.pf:	cbz	x20,.bad
	movl	x6,65535
	cmp	x20,x6
	b.hi	.bad
.ch:	adr	x0,port
	str	x20,[x0]
	adr	x0,chemin			; le chemin (« / » s'il manque)
	ldrb	w4,[x19]
	cmp	w4,'/'
	b.eq	.c
	mov	w5,'/'
	strb	w5,[x0],1
.c:	mov	x1,x19
	mov	x2,CHEMIN_MAX-1
	bl	copie_bornee
	mov	x0,0
	b	.out
.https:	mov	x0,-2
	b	.out
.bad:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source, x2 = taille de la destination (0 compris)
copie_bornee:
	subs	x2,x2,1
	b.ls	.f
.c:	ldrb	w3,[x1],1
	cbz	w3,.f
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.c
.f:	strb	wzr,[x0]
	ret

; x0 = texte, x1 = prefixe en minuscules -> x0 = apres le prefixe, 0 s'il n'y est pas (casse ASCII)
prefixe_i:
	ldrb	w2,[x1],1
	cbz	w2,.oui
	ldrb	w3,[x0],1
	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.m
	orr	w3,w3,0x20
.m:	cmp	w2,w3
	b.eq	prefixe_i
	mov	x0,0
.oui:	ret

; x0 = code HTTP (x26) -> x0 = 1 si c'est une redirection
est_redirection:
	cmp	x0,301
	b.lo	.non
	cmp	x0,303
	b.ls	.oui
	cmp	x0,307
	b.lo	.non
	cmp	x0,308
	b.hi	.non
.oui:	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; la requete a hote:port, chemin ; ligne d'etat et en-tetes lus -> x0 = code HTTP, x25 = socket
; (ouvert), location, longueur (-1 : inconnue) ; < 0 : erreur reseau
requete:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,location
	strb	wzr,[x0]
	adr	x0,longueur
	mov	x1,-1
	str	x1,[x0]
	adr	x0,hote
	adr	x1,port
	ldr	x1,[x1]
	bl	nt_tcp
	tbnz	x0,63,.out
	mov	x25,x0
	adr	x0,req				; GET chemin HTTP/1.0
	adr	x1,s_get
	bl	str_cat
	adr	x1,chemin
	bl	str_cat
	adr	x1,s_get2
	bl	str_cat
	adr	x1,hote
	bl	str_cat
	adr	x1,port
	ldr	x1,[x1]
	cmp	x1,80
	b.eq	.p80
	mov	w2,':'
	strb	w2,[x0],1
	mov	x2,x0
	mov	x0,x1
	mov	x1,x2
	bl	fmt_dec
.p80:	adr	x1,s_get3
	bl	str_cat
	mov	x0,x25
	adr	x1,req
	bl	nt_envoyer
	tbnz	x0,63,.ferme
	mov	x0,x25
	bl	nt_lire_debut
	bl	nt_ligne			; « HTTP/1.x 200 OK »
	cbz	x0,.vide
	mov	x19,x0
	tbz	x22,1,.st
	bl	t_err
	adr	x0,m_nl
	bl	t_err
.st:	ldrb	w1,[x19],1			; le code : apres la premiere espace
	cbz	w1,.vide
	cmp	w1,' '
	b.ne	.st
	mov	x0,x19
	bl	parse_dec
	mov	x20,x0
.h:	bl	nt_ligne			; les en-tetes, jusqu'a la ligne vide
	cbz	x0,.fin
	ldrb	w1,[x0]
	cbz	w1,.fin
	mov	x19,x0
	tbz	x22,1,.loc
	bl	t_err
	adr	x0,m_nl
	bl	t_err
.loc:	mov	x0,x19
	adr	x1,s_location
	bl	prefixe_i
	cbz	x0,.len
	bl	blancs
	mov	x1,x0
	adr	x0,location
	mov	x2,CHEMIN_MAX
	bl	copie_bornee
	b	.h
.len:	mov	x0,x19
	adr	x1,s_longueur
	bl	prefixe_i
	cbz	x0,.h
	bl	blancs
	bl	parse_dec
	adr	x1,longueur
	str	x0,[x1]
	b	.h
.fin:	mov	x0,x20
	b	.out
.vide:	mov	x0,E_NT_CONNRESET
.ferme:	mov	x19,x0
	mov	x0,x25
	bl	net_close
	mov	x0,x19
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> x0 apres ses blancs
blancs:
	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.s
	cmp	w1,9
	b.ne	.r
.s:	add	x0,x0,1
	b	blancs
.r:	ret

; le corps de la reponse (socket x25) vers x27 -> x0 = octets, x1 = 1 si complet (longueur
; atteinte, ou inconnue et connexion fermee)
corps:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0				; octets
	adr	x0,nt_rx_i			; deja recus avec les en-tetes
	ldr	x1,[x0]
	adr	x0,nt_rx_n
	ldr	x2,[x0]
	subs	x2,x2,x1
	b.ls	.r
	adr	x0,nt_rx
	add	x1,x0,x1
	mov	x0,x27
	add	x19,x19,x2
	sys	SYS_WRITE
	tbnz	x0,63,.err
.r:	adr	x0,longueur
	ldr	x0,[x0]
	tbnz	x0,63,.recv
	cmp	x19,x0
	b.hs	.complet
.recv:	mov	x0,x25
	adr	x1,tampon
	mov	x2,CORPS
	movl	x3,30000
	bl	net_recv
	cmp	x0,0
	b.le	.fin
	mov	x2,x0
	add	x19,x19,x2
	mov	x0,x27
	adr	x1,tampon
	sys	SYS_WRITE
	tbz	x0,63,.r
.err:	mov	x1,0
	b	.out
.fin:	adr	x0,longueur			; fermee : complete si la longueur
	ldr	x0,[x0]				; etait inconnue
	tbnz	x0,63,.complet
	cmp	x19,x0
	cset	x1,hs
	b	.out
.complet:
	mov	x1,1
.out:	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

include '../../inc/tool.inc'
include '../../inc/nettool.inc'

m_usage		db 'usage : wget [-O fichier] [-q] [-S] http://h',0xC3,0xB4,'te[:port]/chemin',10,0
m_nonet		db 'wget : pas de r',0xC3,0xA9,'seau',10,0
m_https		db 'wget : HTTPS non pris en charge (pas de TLS pour les outils en assembleur ; OpenSSL ne sert que les programmes C)',10,0
m_mauvaise	db 'wget : URL invalide',10,0
m_echec		db 'wget : connexion impossible ',0xC3,0xA0,' ',0
m_deux_points	db ' : ',0
m_redir		db 'wget : redirig',0xC3,0xA9,' vers ',0
m_trop		db 'wget : trop de redirections',10,0
m_http_err	db 'wget : erreur HTTP ',0
m_bilan		db 'wget : ',0
m_octets	db ' octets re',0xC3,0xA7,'us',0
m_vers		db ' -> ',0
m_incomplet	db 'wget : transfert incomplet',10,0
m_nl		db 10,0
s_https		db 'https://',0
s_http		db 'http://',0
s_get		db 'GET ',0
s_get2		db ' HTTP/1.0',13,10,'Host: ',0
s_get3		db 13,10,'User-Agent: Korin-wget/1.0',13,10,'Accept: */*',13,10,'Connection: close',13,10,13,10,0
s_location	db 'location:',0
s_longueur	db 'content-length:',0
		align 4

segment readable writeable
kit_data
tool_data
nettool_data
		align 8
port		rb 8
longueur	rb 8
hote		rb HOTE_MAX
chemin		rb CHEMIN_MAX
location	rb CHEMIN_MAX
req		rb CHEMIN_MAX + HOTE_MAX + 256
tampon		rb CORPS
