; ftp - client FTP en ligne de commande (docs/OUTILS.md ; d'apres ftp d'Onyx) :
;   ftp [-u UTILISATEUR] [-p MOT_DE_PASSE] [HOTE [PORT]]
; puis une commande par ligne sur l'entree standard (invite « ftp> ») :
;   open hote [port]    user nom [mot de passe]    close    bye / quit / exit
;   ls [chemin]    dir [chemin]    cd chemin    cdup    pwd
;   get distant [local]    put local [distant]    mget noms...    mput noms...
;   delete nom    mkdir nom    rmdir nom    rename de vers    size nom
;   lcd [dossier]    lpwd    quote COMMANDE    help / ?
; Le protocole est parle ici meme, sur NetKit : connexion de controle, mode passif (PASV ; les
; donnees vont a l'adresse de la connexion de controle), binaire (TYPE I). Sans -u, l'utilisateur
; est demande ; vide : anonymous. Les reponses du serveur sont affichees telles quelles.
; Pas de FTPS : il n'y a pas de TLS pour les programmes en assembleur (OpenSSL ne sert que les
; programmes C). Le mot de passe se tape en clair.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../kits/netkit/netkit.inc'

MOTS_MAX	= 16
DONNEES		= 16384
NOM_MAX		= 128

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	t_debut
	mov	x0,-1
	adr	x1,ctl
	str	x0,[x1]
	mov	x21,0				; hote
	mov	x22,21				; port
	mov	x23,1
.arg:	cmp	x23,x19
	b.hs	.go
	ldr	x24,[x20,x23,lsl 3]
	add	x23,x23,1
	ldrb	w0,[x24]
	cmp	w0,'-'
	b.ne	.mot
	ldrb	w0,[x24,1]
	ldrb	w1,[x24,2]
	cbnz	w1,.usage
	cmp	x23,x19
	b.hs	.usage
	ldr	x1,[x20,x23,lsl 3]
	add	x23,x23,1
	cmp	w0,'u'
	b.ne	.op
	adr	x0,opt_user
	str	x1,[x0]
	b	.arg
.op:	cmp	w0,'p'
	b.ne	.usage
	adr	x0,opt_pass
	str	x1,[x0]
	b	.arg
.mot:	cbnz	x21,.port
	mov	x21,x24
	b	.arg
.port:	mov	x0,x24
	bl	parse_dec
	cbz	x0,.usage
	mov	x22,x0
	b	.arg
.go:	mov	x0,0				; les commandes : l'entree standard
	bl	t_lire_debut
	cbz	x21,.boucle
	mov	x0,x21
	mov	x1,x22
	bl	ouvrir
.boucle:
	adr	x0,m_invite
	bl	t_puts
	bl	t_flush
	bl	t_lire_ligne
	cbz	x0,.eof
	bl	couper
	cbz	x0,.boucle
	mov	x21,x0				; mots
	adr	x22,cmds			; la commande
.c:	ldr	x0,[x22]
	cbz	x0,.inconnue
	ldr	x1,[x22,8]
	adr	x2,mots
	ldr	x2,[x2]
	mov	x23,x1
	mov	x1,x2
	bl	str_eq
	cbnz	x0,.trouve
	add	x22,x22,16
	b	.c
.trouve:
	mov	x0,x21
	adr	x1,mots
	blr	x23
	adr	x0,fini
	ldr	x0,[x0]
	cbz	x0,.boucle
	b	.fin
.inconnue:
	adr	x0,m_inconnue
	bl	t_dire
	adr	x0,mots
	ldr	x0,[x0]
	bl	t_puts
	bl	t_nl
	b	.boucle
.eof:	bl	t_nl
	bl	quitter_ctl
.fin:	mov	x0,0
	bl	t_fin
.usage:	adr	x0,m_usage
	bl	t_derr
	mov	x0,2
	bl	t_fin

; --- outils -----------------------------------------------------------------------------

; x0 = ligne : coupee en mots (mots[]) -> x0 = nombre
couper:
	mov	x1,0
	adr	x2,mots
.b:	ldrb	w3,[x0]
	cbz	w3,.f
	cmp	w3,' '
	b.eq	.z
	cmp	w3,9
	b.ne	.m
.z:	strb	wzr,[x0],1
	b	.b
.m:	cmp	x1,MOTS_MAX
	b.hs	.f
	str	x0,[x2,x1,lsl 3]
	add	x1,x1,1
.t:	ldrb	w3,[x0]
	cbz	w3,.f
	cmp	w3,' '
	b.eq	.b
	cmp	w3,9
	b.eq	.b
	add	x0,x0,1
	b	.t
.f:	str	xzr,[x2,x1,lsl 3]
	mov	x0,x1
	ret

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

; -> x0 = 1 si connecte (sinon « Pas connecte. »)
connecte:
	prologue
	adr	x0,ctl
	ldr	x0,[x0]
	tbz	x0,63,.oui
	adr	x0,m_pas_connecte
	bl	t_dire
	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	epilogue

; x0 = invite (traduite) -> x0 = ligne lue sur l'entree standard (0 a la fin)
demander:
	prologue
	bl	t_dire
	bl	t_flush
	bl	t_lire_ligne
	epilogue

; -> x0 = code de la reponse du serveur (affichee), -1 si la connexion est perdue ; derniere
; = sa derniere ligne
reponse:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	nt_ligne
	cbz	x0,.perdue
	mov	x19,x0
	bl	t_puts
	bl	t_nl
	mov	x0,x19
	bl	code_de
	mov	x20,x0
	ldrb	w0,[x19,3]
	cmp	w0,'-'
	b.ne	.der
.suite:	bl	nt_ligne			; reponse sur plusieurs lignes : jusqu'a
	cbz	x0,.perdue			; « NNN » suivi d'une espace
	mov	x19,x0
	bl	t_puts
	bl	t_nl
	ldrb	w0,[x19,3]
	cmp	w0,' '
	b.ne	.suite
	mov	x0,x19
	bl	code_de
	cmp	x0,x20
	b.ne	.suite
.der:	adr	x0,derniere
	mov	x1,x19
	mov	x2,NT_LIGNE
	bl	copie_bornee
	mov	x0,x20
	b	.out
.perdue:
	bl	perdre_ctl
	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne -> x0 = son code a trois chiffres (0 s'il n'y en a pas)
code_de:
	mov	x1,0
	mov	x2,3
.c:	ldrb	w3,[x0],1
	sub	w3,w3,'0'
	cmp	w3,9
	b.hi	.non
	mov	x4,10
	madd	x1,x1,x4,x3
	subs	x2,x2,1
	b.ne	.c
	mov	x0,x1
	ret
.non:	mov	x0,0
	ret

; la connexion de controle est perdue
perdre_ctl:
	prologue
	adr	x1,ctl
	ldr	x0,[x1]
	tbnz	x0,63,.out
	mov	x2,-1
	str	x2,[x1]
	bl	net_close
	adr	x0,m_perdue
	bl	t_dire
.out:	epilogue

; x0 = commande, x1 = argument (0 : aucun) : envoyee -> x0 = code de la reponse, -1
envoyer_cmd:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x1,x0
	adr	x0,cmdbuf
	bl	str_cat
	cbz	x19,.crlf
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x19
	bl	str_cat
.crlf:	adr	x1,s_crlf
	bl	str_cat
	adr	x0,ctl
	ldr	x0,[x0]
	tbnz	x0,63,.perdue
	adr	x1,cmdbuf
	bl	nt_envoyer
	tbnz	x0,63,.perdue
	bl	reponse
	b	.out
.perdue:
	bl	perdre_ctl
	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; QUIT, puis la connexion de controle fermee (rien si pas connecte)
quitter_ctl:
	prologue
	adr	x0,ctl
	ldr	x0,[x0]
	tbnz	x0,63,.out
	adr	x0,s_quit
	mov	x1,0
	bl	envoyer_cmd
	adr	x1,ctl
	ldr	x0,[x1]
	tbnz	x0,63,.out
	mov	x2,-1
	str	x2,[x1]
	bl	net_close
.out:	epilogue

; x0 = hote, x1 = port : connexion, accueil, identification
ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x1
	mov	x1,x0
	adr	x0,hote
	mov	x2,NOM_MAX
	bl	copie_bornee
	bl	quitter_ctl
	adr	x0,m_connexion
	bl	t_dire
	adr	x0,hote
	bl	t_puts
	adr	x0,m_points
	bl	t_puts
	bl	t_flush
	bl	nt_reseau
	cbz	x0,.nonet
	adr	x0,hote
	mov	x1,x20
	bl	nt_tcp
	tbnz	x0,63,.echec
	adr	x2,ctl
	str	x0,[x2]
	adr	x2,ctl_ip
	str	w1,[x2]
	bl	nt_lire_debut
	bl	reponse				; 220 : bienvenue
	cmp	x0,220
	b.ne	.refus
	adr	x0,opt_user
	ldr	x0,[x0]
	adr	x1,opt_pass
	ldr	x1,[x1]
	bl	identifier
	b	.out
.refus:	bl	quitter_ctl
	b	.out
.echec:	mov	x19,x0
	adr	x0,m_echec
	bl	t_derr
	mov	x0,x19
	bl	nt_err_texte
	adr	x0,m_nl
	bl	t_err
	b	.out
.nonet:	adr	x0,m_nonet
	bl	t_derr
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = utilisateur (0 : demande), x1 = mot de passe (0 : demande) : USER, PASS, TYPE I
identifier:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x1
	cbnz	x0,.u
	adr	x0,m_utilisateur		; « Utilisateur (hote) : »
	bl	t_dire
	adr	x0,hote
	bl	t_puts
	adr	x0,m_fin_util
	bl	demander
	cbz	x0,.anon
	ldrb	w1,[x0]
	cbz	w1,.anon
.u:	mov	x1,x0
	adr	x0,util
	mov	x2,NOM_MAX
	bl	copie_bornee
	b	.user
.anon:	adr	x0,util
	adr	x1,s_anonymous
	mov	x2,NOM_MAX
	bl	copie_bornee
.user:	adr	x0,s_user
	adr	x1,util
	bl	envoyer_cmd
	cmp	x0,230
	b.eq	.type
	cmp	x0,331
	b.eq	.pass
	cmp	x0,332
	b.ne	.refus
.pass:	mov	x1,x20
	cbnz	x1,.p
	adr	x0,util				; anonymous : une adresse pour mot de passe
	adr	x1,s_anonymous
	bl	str_eq
	adr	x1,s_anon_pass
	cbnz	x0,.p
	adr	x0,m_mot_de_passe
	bl	demander
	mov	x1,x0
	cbnz	x1,.p
	adr	x1,s_vide
.p:	adr	x0,mdp
	mov	x2,NOM_MAX
	bl	copie_bornee
	adr	x0,s_pass
	adr	x1,mdp
	bl	envoyer_cmd
	cmp	x0,230
	b.eq	.type
	cmp	x0,202
	b.ne	.refus
.type:	adr	x0,s_type
	adr	x1,s_i
	bl	envoyer_cmd
	b	.out
.refus:	tbnz	x0,63,.out
	adr	x0,m_refus
	bl	t_dire
.out:	ldp	x19,x20,[sp],16
	epilogue

; PASV -> x0 = socket de donnees connecte, < 0 si impossible
passif:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,s_pasv
	mov	x1,0
	bl	envoyer_cmd
	cmp	x0,227
	b.ne	.non
	adr	x19,derniere			; « 227 ... (h1,h2,h3,h4,p1,p2) »
	add	x19,x19,4
.cherche:
	ldrb	w0,[x19]
	cbz	w0,.non
	sub	w0,w0,'0'
	cmp	w0,9
	b.ls	.nombres
	add	x19,x19,1
	b	.cherche
.nombres:
	mov	x20,0				; nombres lus
	mov	x21,0				; les deux derniers (port)
.n:	mov	x22,0
.d:	ldrb	w0,[x19]
	sub	w1,w0,'0'
	cmp	w1,9
	b.hi	.nf
	mov	x2,10
	madd	x22,x22,x2,x1
	add	x19,x19,1
	b	.d
.nf:	add	x20,x20,1
	lsl	x21,x21,8
	and	x22,x22,0xFF
	orr	x21,x21,x22
	cmp	x20,6
	b.hs	.port
	cmp	w0,','
	b.ne	.non
	add	x19,x19,1
	b	.n
.port:	and	x1,x21,0xFFFF
	adr	x0,ctl_ip			; (l'adresse de la connexion de controle :
	ldr	w0,[x0]				; celle du 227 peut etre privee, ou NAT)
	bl	nt_tcp_ip
	tbz	x0,63,.out
.non:	adr	x0,m_pas_donnees
	bl	t_dire
	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = code -> x0 = 1 si 1xx (le transfert commence)
debut_ok:
	sub	x0,x0,100
	cmp	x0,100
	cset	x0,lo
	ret

; x0 = octets, x1 = ms : « N octets en M ms (K Ko/s) »
bilan:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	t_putn
	adr	x0,m_octets_en
	bl	t_dire
	mov	x0,x20
	bl	t_putn
	adr	x0,m_ms
	bl	t_puts
	cbz	x20,.nl
	adr	x0,m_paren
	bl	t_puts
	udiv	x0,x19,x20
	bl	t_putn
	adr	x0,m_kos
	bl	t_dire
.nl:	bl	t_nl
	ldp	x19,x20,[sp],16
	epilogue

; --- commandes : x0 = nombre de mots, x1 = mots[] -----------------------------------------

cmd_bye:
	prologue
	bl	quitter_ctl
	adr	x0,fini
	mov	x1,1
	str	x1,[x0]
	epilogue

cmd_close:
	prologue
	bl	connecte
	cbz	x0,.out
	bl	quitter_ctl
	adr	x0,m_ferme
	bl	t_dire
.out:	epilogue

cmd_help:
	prologue
	adr	x0,m_aide
	bl	t_dire
	epilogue

cmd_mode:
	prologue
	adr	x0,m_mode
	bl	t_dire
	epilogue

cmd_open:
	prologue
	cmp	x0,2
	b.lo	.usage
	mov	x2,x0
	ldr	x0,[x1,8]
	mov	x3,21
	cmp	x2,3
	b.lo	.go
	stp	x0,x1,[sp,-16]!
	ldr	x0,[x1,16]
	bl	parse_dec
	mov	x3,x0
	ldp	x0,x1,[sp],16
.go:	mov	x1,x3
	bl	ouvrir
	b	.out
.usage:	adr	x0,m_u_open
	bl	t_dire
.out:	epilogue

cmd_user:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	connecte
	cbz	x0,.out
	cmp	x19,2
	b.lo	.usage
	ldr	x0,[x20,8]
	mov	x1,0
	cmp	x19,3
	b.lo	.go
	ldr	x1,[x20,16]
.go:	bl	identifier
	b	.out
.usage:	adr	x0,m_u_user
	bl	t_dire
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = n, x1 = mots, x2 = commande FTP : une commande simple, son argument facultatif
simple_opt:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	str	x2,[sp,-16]!
	bl	connecte
	ldr	x2,[sp],16
	cbz	x0,.out
	mov	x0,x2
	mov	x1,0
	cmp	x19,2
	b.lo	.go
	ldr	x1,[x20,8]
.go:	bl	envoyer_cmd
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = n, x1 = mots, x2 = commande FTP, x3 = usage : une commande simple a argument
simple_arg:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	stp	x2,x3,[sp,-16]!
	bl	connecte
	ldp	x2,x3,[sp],16
	cbz	x0,.out
	cmp	x19,2
	b.lo	.usage
	mov	x0,x2
	ldr	x1,[x20,8]
	bl	envoyer_cmd
	b	.out
.usage:	mov	x0,x3
	bl	t_dire
.out:	ldp	x19,x20,[sp],16
	epilogue

cmd_pwd:
	adr	x2,s_pwd
	mov	x0,1
	b	simple_opt
cmd_cdup:
	adr	x2,s_cdup
	mov	x0,1
	b	simple_opt
cmd_size:
	adr	x2,s_size
	adr	x3,m_u_size
	b	simple_arg
cmd_cd:
	adr	x2,s_cwd
	adr	x3,m_u_cd
	b	simple_arg
cmd_delete:
	adr	x2,s_dele
	adr	x3,m_u_delete
	b	simple_arg
cmd_mkdir:
	adr	x2,s_mkd
	adr	x3,m_u_mkdir
	b	simple_arg
cmd_rmdir:
	adr	x2,s_rmd
	adr	x3,m_u_rmdir
	b	simple_arg

cmd_rename:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	connecte
	cbz	x0,.out
	cmp	x19,3
	b.lo	.usage
	adr	x0,s_rnfr
	ldr	x1,[x20,8]
	bl	envoyer_cmd
	cmp	x0,350
	b.ne	.out
	adr	x0,s_rnto
	ldr	x1,[x20,16]
	bl	envoyer_cmd
	b	.out
.usage:	adr	x0,m_u_rename
	bl	t_dire
.out:	ldp	x19,x20,[sp],16
	epilogue

; quote : la ligne telle quelle (ses mots rejoints)
cmd_quote:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	connecte
	cbz	x0,.out
	cmp	x19,2
	b.lo	.usage
	mov	x1,2
.j:	cmp	x1,x19				; mots 2... rejoints par des espaces
	b.hs	.go
	ldr	x2,[x20,x1,lsl 3]
	sub	x3,x2,1				; (l'espace remise a la place du 0)
	mov	w4,' '
	strb	w4,[x3]
	add	x1,x1,1
	b	.j
.go:	ldr	x0,[x20,8]
	mov	x1,0
	bl	envoyer_cmd
	b	.out
.usage:	adr	x0,m_u_quote
	bl	t_dire
.out:	ldp	x19,x20,[sp],16
	epilogue

cmd_ls:
	adr	x2,s_nlst
	b	lister
cmd_dir:
	adr	x2,s_list
; x0 = n, x1 = mots, x2 = NLST / LIST : la liste par une connexion de donnees
lister:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	connecte
	cbz	x0,.out
	bl	passif
	tbnz	x0,63,.out
	mov	x22,x0
	mov	x0,x21
	mov	x1,0
	cmp	x19,2
	b.lo	.go
	ldr	x1,[x20,8]
.go:	bl	envoyer_cmd
	bl	debut_ok
	cbz	x0,.ferme
.r:	mov	x0,x22
	adr	x1,donnees
	mov	x2,DONNEES
	movl	x3,30000
	bl	net_recv
	cmp	x0,0
	b.le	.fin
	adr	x19,donnees			; (sans les CR)
	add	x20,x19,x0
.c:	cmp	x19,x20
	b.hs	.r
	ldrb	w0,[x19],1
	cmp	w0,13
	b.eq	.c
	bl	t_putc
	b	.c
.fin:	mov	x0,x22
	bl	net_close
	bl	reponse				; 226
	b	.out
.ferme:	mov	x0,x22
	bl	net_close
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

cmd_get:
	prologue
	cmp	x0,2
	b.lo	.usage
	mov	x2,x0
	ldr	x0,[x1,8]
	mov	x3,x1
	mov	x1,0
	cmp	x2,3
	b.lo	.go
	ldr	x1,[x3,16]
.go:	bl	recevoir
	b	.out
.usage:	adr	x0,m_u_get
	bl	t_dire
.out:	epilogue

cmd_mget:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,1
.n:	cmp	x21,x19
	b.hs	.out
	ldr	x0,[x20,x21,lsl 3]
	mov	x1,0
	bl	recevoir
	add	x21,x21,1
	b	.n
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fichier distant, x1 = local (0 : le dernier composant du distant) : RETR
recevoir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	connecte
	cbz	x0,.out
	cbnz	x20,.loc
	mov	x0,x19
	bl	t_base
	mov	x20,x0
.loc:	mov	x0,x20
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fic
	mov	x21,x0
	bl	passif
	tbnz	x0,63,.sans
	mov	x22,x0
	sys	SYS_TIME_MS
	mov	x24,x0
	adr	x0,s_retr
	mov	x1,x19
	bl	envoyer_cmd
	bl	debut_ok
	cbz	x0,.refus
	mov	x23,0				; octets
.r:	mov	x0,x22
	adr	x1,donnees
	mov	x2,DONNEES
	movl	x3,30000
	bl	net_recv
	cmp	x0,0
	b.le	.fin
	mov	x2,x0
	add	x23,x23,x0
	mov	x0,x21
	adr	x1,donnees
	sys	SYS_WRITE
	tbz	x0,63,.r
	adr	x0,m_ecriture			; le disque refuse : on arrete
	bl	t_dire
.fin:	mov	x0,x22
	bl	net_close
	mov	x0,x21
	sys	SYS_CLOSE
	bl	reponse				; 226
	sys	SYS_TIME_MS
	sub	x24,x0,x24
	mov	x0,x20
	bl	t_puts
	adr	x0,m_deux_points
	bl	t_puts
	mov	x0,x23
	mov	x1,x24
	bl	bilan
	b	.out
.refus:	mov	x0,x22
	bl	net_close
.sans:	mov	x0,x21				; rien recu : le fichier vide est retire
	sys	SYS_CLOSE
	mov	x0,x20
	sys	SYS_UNLINK
	b	.out
.fic:	mov	x0,x20
	bl	t_err_fic
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

cmd_put:
	prologue
	cmp	x0,2
	b.lo	.usage
	mov	x2,x0
	ldr	x0,[x1,8]
	mov	x3,x1
	mov	x1,0
	cmp	x2,3
	b.lo	.go
	ldr	x1,[x3,16]
.go:	bl	envoyer_fic
	b	.out
.usage:	adr	x0,m_u_put
	bl	t_dire
.out:	epilogue

cmd_mput:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,1
.n:	cmp	x21,x19
	b.hs	.out
	ldr	x0,[x20,x21,lsl 3]
	mov	x1,0
	bl	envoyer_fic
	add	x21,x21,1
	b	.n
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fichier local, x1 = distant (0 : le dernier composant du local) : STOR
envoyer_fic:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	connecte
	cbz	x0,.out
	cbnz	x20,.loc
	mov	x0,x19
	bl	t_base
	mov	x20,x0
.loc:	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.fic
	mov	x21,x0
	bl	passif
	tbnz	x0,63,.sans
	mov	x22,x0
	sys	SYS_TIME_MS
	mov	x24,x0
	adr	x0,s_stor
	mov	x1,x20
	bl	envoyer_cmd
	bl	debut_ok
	cbz	x0,.refus
	mov	x23,0
.r:	mov	x0,x21
	adr	x1,donnees
	mov	x2,DONNEES
	sys	SYS_READ
	cmp	x0,0
	b.le	.fin
	add	x23,x23,x0
	mov	x2,x0				; tout envoyer
	adr	x1,donnees
.s:	mov	x0,x22
	stp	x1,x2,[sp,-16]!
	bl	net_send
	ldp	x1,x2,[sp],16
	cmp	x0,0
	b.le	.coupe
	add	x1,x1,x0
	subs	x2,x2,x0
	b.hi	.s
	b	.r
.coupe:	adr	x0,m_coupe
	bl	t_dire
.fin:	mov	x0,x21
	sys	SYS_CLOSE
	mov	x0,x22				; fin des donnees : FIN, puis la reponse
	bl	net_shutdown
	bl	reponse				; 226
	mov	x0,x22
	bl	net_close
	sys	SYS_TIME_MS
	sub	x24,x0,x24
	mov	x0,x20
	bl	t_puts
	adr	x0,m_deux_points
	bl	t_puts
	mov	x0,x23
	mov	x1,x24
	bl	bilan
	b	.out
.refus:	mov	x0,x22
	bl	net_close
.sans:	mov	x0,x21
	sys	SYS_CLOSE
	b	.out
.fic:	mov	x0,x19
	bl	t_err_fic
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

cmd_lcd:
	prologue
	stp	x19,x20,[sp,-16]!
	cmp	x0,2
	b.lo	.pwd
	ldr	x19,[x1,8]
	mov	x0,x19
	sys	SYS_CHDIR
	tbz	x0,63,.pwd
	adr	x0,m_lcd_non
	bl	t_dire
	mov	x0,x19
	bl	t_puts
	bl	t_nl
.pwd:	adr	x0,m_local
	bl	t_dire
	bl	lpwd
	ldp	x19,x20,[sp],16
	epilogue

cmd_lpwd:
lpwd:
	prologue
	adr	x0,cwd
	mov	x1,256
	sys	SYS_GETCWD
	adr	x0,cwd
	bl	t_puts
	bl	t_nl
	epilogue

include '../../inc/tool.inc'
include '../../inc/nettool.inc'

		align 8
cmds:	dd s_c_open, cmd_open,   s_c_user, cmd_user,   s_c_close, cmd_close
	dd s_c_disconnect, cmd_close,   s_c_bye, cmd_bye,   s_c_quit, cmd_bye
	dd s_c_exit, cmd_bye,   s_c_help, cmd_help,   s_c_aide, cmd_help
	dd s_c_ls, cmd_ls,   s_c_dir, cmd_dir,   s_c_pwd, cmd_pwd
	dd s_c_cd, cmd_cd,   s_c_cdup, cmd_cdup,   s_c_get, cmd_get
	dd s_c_recv, cmd_get,   s_c_put, cmd_put,   s_c_send, cmd_put
	dd s_c_mget, cmd_mget,   s_c_mput, cmd_mput,   s_c_size, cmd_size
	dd s_c_delete, cmd_delete,   s_c_del, cmd_delete,   s_c_rm, cmd_delete
	dd s_c_mkdir, cmd_mkdir,   s_c_md, cmd_mkdir,   s_c_rmdir, cmd_rmdir
	dd s_c_rd, cmd_rmdir,   s_c_rename, cmd_rename,   s_c_ren, cmd_rename
	dd s_c_lcd, cmd_lcd,   s_c_lpwd, cmd_lpwd,   s_c_quote, cmd_quote
	dd s_c_binary, cmd_mode,   s_c_bin, cmd_mode,   s_c_ascii, cmd_mode
	dd s_c_passive, cmd_mode,   s_c_prompt, cmd_mode
	dd 0, 0

s_c_open	db 'open',0
s_c_user	db 'user',0
s_c_close	db 'close',0
s_c_disconnect	db 'disconnect',0
s_c_bye		db 'bye',0
s_c_quit	db 'quit',0
s_c_exit	db 'exit',0
s_c_help	db 'help',0
s_c_aide	db '?',0
s_c_ls		db 'ls',0
s_c_dir		db 'dir',0
s_c_pwd		db 'pwd',0
s_c_cd		db 'cd',0
s_c_cdup	db 'cdup',0
s_c_get		db 'get',0
s_c_recv	db 'recv',0
s_c_put		db 'put',0
s_c_send	db 'send',0
s_c_mget	db 'mget',0
s_c_mput	db 'mput',0
s_c_size	db 'size',0
s_c_delete	db 'delete',0
s_c_del		db 'del',0
s_c_rm		db 'rm',0
s_c_mkdir	db 'mkdir',0
s_c_md		db 'md',0
s_c_rmdir	db 'rmdir',0
s_c_rd		db 'rd',0
s_c_rename	db 'rename',0
s_c_ren		db 'ren',0
s_c_lcd		db 'lcd',0
s_c_lpwd	db 'lpwd',0
s_c_quote	db 'quote',0
s_c_binary	db 'binary',0
s_c_bin		db 'bin',0
s_c_ascii	db 'ascii',0
s_c_passive	db 'passive',0
s_c_prompt	db 'prompt',0

s_user		db 'USER',0
s_pass		db 'PASS',0
s_type		db 'TYPE',0
s_i		db 'I',0
s_pasv		db 'PASV',0
s_quit		db 'QUIT',0
s_pwd		db 'PWD',0
s_cdup		db 'CDUP',0
s_cwd		db 'CWD',0
s_size		db 'SIZE',0
s_dele		db 'DELE',0
s_mkd		db 'MKD',0
s_rmd		db 'RMD',0
s_rnfr		db 'RNFR',0
s_rnto		db 'RNTO',0
s_nlst		db 'NLST',0
s_list		db 'LIST',0
s_retr		db 'RETR',0
s_stor		db 'STOR',0
s_crlf		db 13,10,0
s_anonymous	db 'anonymous',0
s_anon_pass	db 'korin@',0
s_vide		db 0

m_usage		db 'usage : ftp [-u utilisateur] [-p mot de passe] [h',0xC3,0xB4,'te [port]]',10,0
m_invite	db 'ftp> ',0
m_inconnue	db '?Commande inconnue : ',0
m_pas_connecte	db 'Pas connect',0xC3,0xA9,'.',10,0
m_perdue	db 'ftp : connexion perdue',10,0
m_connexion	db 'Connexion ',0xC3,0xA0,' ',0
m_points	db '...',10,0
m_echec		db 'ftp : connexion impossible : ',0
m_nonet		db 'ftp : pas de r',0xC3,0xA9,'seau',10,0
m_nl		db 10,0
m_utilisateur	db 'Utilisateur (',0
m_fin_util	db ') : ',0
m_mot_de_passe	db 'Mot de passe : ',0
m_refus		db 'Identification refus',0xC3,0xA9,'e.',10,0
m_pas_donnees	db 'ftp : pas de connexion de donn',0xC3,0xA9,'es',10,0
m_octets_en	db ' octets en ',0
m_ms		db ' ms',0
m_paren		db ' (',0
m_kos		db ' Ko/s)',0
m_deux_points	db ' : ',0
m_ferme		db 'D',0xC3,0xA9,'connect',0xC3,0xA9,'.',10,0
m_mode		db '(toujours binaire, passif)',10,0
m_ecriture	db 'ftp : ',0xC3,0xA9,'criture impossible, transfert arr',0xC3,0xAA,'t',0xC3,0xA9,10,0
m_coupe		db 'ftp : envoi interrompu',10,0
m_lcd_non	db 'lcd : dossier introuvable : ',0
m_local		db 'Dossier local : ',0
m_u_open	db 'usage : open h',0xC3,0xB4,'te [port]',10,0
m_u_user	db 'usage : user nom [mot de passe]',10,0
m_u_size	db 'usage : size nom',10,0
m_u_cd		db 'usage : cd chemin',10,0
m_u_delete	db 'usage : delete nom',10,0
m_u_mkdir	db 'usage : mkdir nom',10,0
m_u_rmdir	db 'usage : rmdir nom',10,0
m_u_rename	db 'usage : rename de vers',10,0
m_u_quote	db 'usage : quote COMMANDE [arguments]',10,0
m_u_get		db 'usage : get distant [local]',10,0
m_u_put		db 'usage : put local [distant]',10,0
m_aide		db 'open h',0xC3,0xB4,'te [port]   user nom [mot de passe]   close   bye / quit',10
		db 'ls [chemin]   dir [chemin]   cd chemin   cdup   pwd',10
		db 'get distant [local]   put local [distant]   mget noms...   mput noms...',10
		db 'delete nom   mkdir nom   rmdir nom   rename de vers   size nom',10
		db 'lcd [dossier]   lpwd   quote COMMANDE   (binaire, passif ; pas de FTPS)',10,0
		align 4

segment readable writeable
kit_data
tool_data
nettool_data
		align 8
ctl		rb 8				; connexion de controle (< 0 : aucune)
ctl_ip		rb 8
opt_user	rb 8
opt_pass	rb 8
fini		rb 8
mots		rb (MOTS_MAX + 1) * 8
hote		rb NOM_MAX
util		rb NOM_MAX
mdp		rb NOM_MAX
cwd		rb 256
cmdbuf		rb NT_LIGNE + 32
derniere	rb NT_LIGNE
donnees		rb DONNEES
