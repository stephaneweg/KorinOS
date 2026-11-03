; ftpd - transfert de fichiers (docs/RESEAU.md 5.2, etape 9), port 21, lance
; par init2 (SD0:/etc/services.txt). Le PC n'a besoin de rien : Python
; ftplib, curl, WinSCP, l'Explorateur Windows. 4 sessions a la fois (un
; thread chacune). Commandes : USER, PASS (tout accepte), SYST, FEAT, OPTS,
; PWD, CWD, CDUP, TYPE, MODE, STRU, NOOP, PASV, EPSV, PORT, LIST, NLST, RETR,
; STOR, SIZE, MKD, RMD, DELE, XCRC (CRC32 d'un fichier : n'envoyer que ce qui
; a change), QUIT. Pas de RNFR/RNTO (le protocole des fichiers n'a pas de
; renommage).
; Chemins : ceux d'ARMPI vus comme une arborescence Unix : "/SD0/sys/svc/netd"
; <-> SD0:/sys/svc/netd ; "/" : les volumes. Ecrire kernel8.img ou un .mod
; sur /SD0 (la partition de demarrage) suffit pour le prochain demarrage.
; Mode passif : port de donnees 2100 + numero de session (redirections sous
; QEMU : 2100-2103).
; Pas de mot de passe (decision de l'utilisateur, l'authentification vient a
; la toute fin du projet) : a garder sur le reseau de la maison.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/netkit/netkit.inc'

PORT		= 21
DATA_PORT	= 2100
SESS_MAX	= 4
TSTACK		= 32768
BLOC		= 8192			; morceau de transfert

; session (FS_* : decalages dans son bloc)
FS_USED		= 0
FS_CTRL		= 8			; connexion de commande
FS_NUM		= 16			; numero (port passif)
FS_PASV		= 24			; socket en ecoute (passif ; -1 : aucune)
FS_ACTIF_IP	= 32			; PORT : adresse, port (0 : aucun)
FS_ACTIF_PORT	= 40
FS_DATA		= 48			; connexion de donnees ouverte
FS_CWD		= 64			; dossier FTP ("/SD0/home"), 256 o
FS_LINE		= 320			; ligne recue, 512 o
FS_LINE_N	= 832
FS_IN		= 840			; octets recus non encore lus, 512 o
FS_IN_I		= 1352
FS_IN_N		= 1360
FS_PATH		= 1368			; chemin ARMPI, 512 o
FS_REP		= 1880			; reponse, 640 o
FS_DIRENT	= 2520			; DIRENT_BYTES
FS_STAT		= 2800			; {taille, attributs}
FS_BUF		= 4096			; tampon de transfert (BLOC)
FS_SIZE		= 4096 + BLOC

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	kits_open
	bl	lang_get			; (dictionnaires lus ici : malloc, avant les threads)
	movl	x0,SESS_MAX * FS_SIZE		; les sessions
	bl	malloc
	cbz	x0,quit
	adr	x1,sess
	str	x0,[x1]
	mov	x1,0
.z:	str	xzr,[x0,x1]
	add	x1,x1,8
	movl	x2,SESS_MAX * FS_SIZE
	cmp	x1,x2
	b.lo	.z
.sock:	mov	x0,SOCK_TCP
	bl	net_socket
	tbnz	x0,63,.attente
	mov	x19,x0
	mov	x1,PORT
	bl	net_bind
	tbnz	x0,63,.ferme
	mov	x0,x19
	mov	x1,SESS_MAX
	bl	net_listen
	tbnz	x0,63,.ferme
	adr	x0,m_pret
	bl	puts
.loop:	mov	x0,x19
	movl	x1,3600000
	bl	net_accept
	cmn	x0,11
	b.eq	.loop
	tbnz	x0,63,.erreur
	bl	nouvelle
	b	.loop
.erreur:
	cmn	x0,32				; netd relance : tout reprendre
	b.ne	.loop
.ferme:	mov	x0,x19
	bl	net_close
.attente:
	mov	x0,2000
	sys	SYS_SLEEP
	b	.sock
quit:	mov	x0,1
	sys	SYS_EXIT

; x0 = connexion : une session (thread), ou refus
nouvelle:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x20,sess
	ldr	x20,[x20]
	mov	x2,0
.p:	cmp	x2,SESS_MAX
	b.hs	.plein
	ldr	x1,[x20,FS_USED]
	cbz	x1,.place
	movl	x3,FS_SIZE
	add	x20,x20,x3
	add	x2,x2,1
	b	.p
.place:	mov	x1,1
	str	x1,[x20,FS_USED]
	str	x19,[x20,FS_CTRL]
	str	x2,[x20,FS_NUM]
	mov	x0,TSTACK
	bl	malloc
	cbz	x0,.ko
	add	x1,x0,TSTACK
	and	x1,x1,0xFFFFFFFFFFFFFFF0
	adr	x0,session
	mov	x2,x20
	sys	SYS_THREAD_CREATE
	tbz	x0,63,.out
.ko:	str	xzr,[x20,FS_USED]
.plein:	mov	x0,x19
	adr	x1,r_plein
	bl	dire_sur
	mov	x0,x19
	bl	net_close
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = socket, x1 = chaine : envoyee
send_str:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x1
	bl	str_len
	mov	x2,x0
	mov	x0,x19
	mov	x1,x20
	bl	net_send
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Une session (thread) : x0 = son bloc ; x19 = session dans tout ce qui suit
; ---------------------------------------------------------------------------
session:
	mov	x19,x0
	mov	x0,-1
	str	x0,[x19,FS_PASV]
	str	x0,[x19,FS_DATA]
	str	xzr,[x19,FS_ACTIF_IP]
	str	xzr,[x19,FS_IN_N]
	str	xzr,[x19,FS_IN_I]
	mov	w0,'/'				; dossier : la racine
	strh	w0,[x19,FS_CWD]
	adr	x0,m_session
	bl	puts
	adr	x0,r_bonjour
	bl	dire
.loop:	bl	read_line
	tbnz	x0,63,.fin
	bl	commande
	cbz	x0,.loop
.fin:	bl	data_close
	ldr	x0,[x19,FS_PASV]
	tbnz	x0,63,.f2
	bl	net_close
.f2:	ldr	x0,[x19,FS_CTRL]
	bl	net_close
	adr	x0,m_finie
	bl	puts
	str	xzr,[x19,FS_USED]
	sys	SYS_THREAD_EXIT

; x0 = reponse (chaine avec CR LF) sur la connexion de commande
reply:
	mov	x1,x0
	ldr	x0,[x19,FS_CTRL]
	b	send_str

; x0 = reponse fixe : comme reply, traduite
dire:
	mov	x1,x0
	ldr	x0,[x19,FS_CTRL]

; x0 = socket, x1 = reponse fixe (une ligne, CR LF compris) : traduite sans son CR LF
; (le code au debut en fait partie : la cle du dictionnaire est « 220 Bonjour »)
dire_sur:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,256
	mov	x19,x0
	mov	x0,sp
.c:	ldrb	w2,[x1],1
	cmp	w2,13
	b.eq	.f
	strb	w2,[x0],1
	cbnz	w2,.c
.f:	strb	wzr,[x0]
	mov	x0,sp
	bl	lang_tr
	mov	x1,x0
	mov	x0,sp
	bl	str_cpy
	adr	x1,s_crlf
	bl	str_cpy
	mov	x0,x19
	mov	x1,sp
	bl	send_str
	add	sp,sp,256
	ldr	x19,[sp],16
	epilogue

; -> FS_LINE : une ligne de commande (sans CR LF) ; x0 = 0, < 0 si fin
read_line:
	prologue
	stp	x20,x21,[sp,-16]!
	mov	x20,0				; longueur
.c:	ldr	x1,[x19,FS_IN_I]
	ldr	x2,[x19,FS_IN_N]
	cmp	x1,x2
	b.lo	.octet
	ldr	x0,[x19,FS_CTRL]		; recevoir (5 min de silence : fin)
	add	x1,x19,FS_IN
	mov	x2,512
	movl	x3,300000
	bl	net_recv
	cmp	x0,0
	b.le	.fin
	str	x0,[x19,FS_IN_N]
	str	xzr,[x19,FS_IN_I]
	b	.c
.octet:	add	x3,x19,FS_IN
	ldrb	w21,[x3,x1]
	add	x1,x1,1
	str	x1,[x19,FS_IN_I]
	cmp	w21,10
	b.eq	.eol
	cmp	w21,13
	b.eq	.c
	cmp	x20,500
	b.hs	.c
	add	x3,x19,FS_LINE
	strb	w21,[x3,x20]
	add	x20,x20,1
	b	.c
.eol:	add	x3,x19,FS_LINE
	strb	wzr,[x3,x20]
	mov	x0,0
	b	.out
.fin:	mov	x0,-1
.out:	ldp	x20,x21,[sp],16
	epilogue

; FS_LINE : une commande -> x0 = 1 pour finir la session
commande:
	prologue
	stp	x20,x21,[sp,-16]!
	add	x20,x19,FS_LINE			; verbe en majuscules, argument
	mov	x21,x20
.v:	ldrb	w0,[x21]
	cbz	w0,.sans
	cmp	w0,' '
	b.eq	.coupe
	cmp	w0,'a'
	b.lo	.vn
	cmp	w0,'z'
	b.hi	.vn
	sub	w0,w0,0x20
	strb	w0,[x21]
.vn:	add	x21,x21,1
	b	.v
.coupe:	strb	wzr,[x21],1
	b	.arg
.sans:	adr	x21,s_vide
.arg:	adr	x2,verbes			; (verbe, fonction)
.t:	ldr	w0,[x2]
	cbz	w0,.inconnu
	adr	x1,verbes
	add	x0,x1,x0
	stp	x2,x2,[sp,-16]!
	mov	x1,x20
	bl	str_eq
	ldp	x2,x3,[sp],16
	cbnz	x0,.appel
	add	x2,x2,8
	b	.t
.appel:	ldrsw	x1,[x2,4]
	adr	x2,verbes
	add	x1,x2,x1
	mov	x0,x21				; x0 = argument
	blr	x1
	b	.out
.inconnu:
	adr	x0,r_inconnu
	bl	dire
	mov	x0,0
.out:	ldp	x20,x21,[sp],16
	epilogue


; --- commandes (x0 = argument) -> x0 = 0 (1 : fin) ---------------------------
c_user:
	adr	x0,r_user
	b	rep0
c_pass:
	adr	x0,r_pass
	b	rep0
c_syst:
	adr	x0,r_syst
	b	rep0
c_feat:
	prologue
	adr	x0,r_feat			; (plusieurs lignes, technique : telle quelle)
	bl	reply
	mov	x0,0
	epilogue
c_ok:
	adr	x0,r_ok
	b	rep0
c_noop:
	adr	x0,r_ok
	b	rep0
c_quit:
	prologue
	adr	x0,r_quit
	bl	dire
	mov	x0,1
	epilogue
c_non:
	adr	x0,r_non
	b	rep0
; x0 = reponse -> 0
rep0:
	prologue
	bl	dire
	mov	x0,0
	epilogue

c_pwd:
	prologue
	add	x0,x19,FS_REP			; 257 "/SD0/..." est le dossier
	adr	x1,r_257
	bl	str_cpy
	add	x1,x19,FS_CWD
	bl	str_cpy
	str	x0,[sp,-16]!
	adr	x0,r_257b
	bl	lang_tr
	mov	x1,x0
	ldr	x0,[sp],16
	bl	str_cpy
	adr	x1,s_crlf
	bl	str_cpy
	add	x0,x19,FS_REP
	bl	reply
	mov	x0,0
	epilogue

c_cdup:
	adr	x0,s_parent
c_cwd:
	prologue
	str	x20,[sp,-16]!
	bl	resoudre			; -> FS_PATH (ARMPI), FS_REP (FTP)
	tbnz	x0,63,.non
	cbz	x0,.racine			; "/" : toujours la
	add	x0,x19,FS_PATH			; existe ?
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	sys	SYS_CLOSE
	add	x0,x19,FS_PATH			; un fichier : refuse (la racine
	add	x1,x19,FS_STAT			;  d'un volume n'a pas de stat)
	sys	SYS_STAT
	tbnz	x0,63,.racine
	ldr	x0,[x19,FS_STAT+8]
	tst	x0,ATTR_DIR
	b.eq	.non
.racine:
	add	x0,x19,FS_CWD			; nouveau dossier
	add	x1,x19,FS_REP
	bl	str_cpy
	adr	x0,r_cwd
	bl	dire
	b	.out
.non:	adr	x0,r_550
	bl	dire
.out:	mov	x0,0
	ldr	x20,[sp],16
	epilogue

c_type:
	adr	x0,r_type
	b	rep0

c_size:
	prologue
	bl	resoudre
	cmp	x0,0
	b.le	.non
	add	x0,x19,FS_PATH
	add	x1,x19,FS_STAT
	sys	SYS_STAT
	tbnz	x0,63,.non
	add	x0,x19,FS_REP
	adr	x1,r_213
	bl	str_cpy
	mov	x1,x0
	ldr	x0,[x19,FS_STAT]
	bl	fmt_dec
	adr	x1,s_crlf
	bl	str_cpy
	add	x0,x19,FS_REP
	bl	reply
	b	.out
.non:	adr	x0,r_550
	bl	dire
.out:	mov	x0,0
	epilogue

c_mkd:
	prologue
	bl	resoudre
	cmp	x0,0
	b.le	.non
	add	x0,x19,FS_PATH
	sys	SYS_MKDIR
	tbnz	x0,63,.non
	adr	x0,r_mkd
	bl	dire
	b	.out
.non:	adr	x0,r_550
	bl	dire
.out:	mov	x0,0
	epilogue

c_dele:
	prologue
	bl	resoudre
	cmp	x0,0
	b.le	.non
	add	x0,x19,FS_PATH
	sys	SYS_UNLINK
	tbnz	x0,63,.non
	adr	x0,r_250
	bl	dire
	b	.out
.non:	adr	x0,r_550
	bl	dire
.out:	mov	x0,0
	epilogue

; XCRC chemin -> 250 XXXXXXXX (CRC32 du fichier)
c_xcrc:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	bl	resoudre
	cmp	x0,0
	b.le	.non
	add	x0,x19,FS_PATH
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	mov	x20,x0
	mvn	w21,wzr				; crc = ~0
.r:	mov	x0,x20
	add	x1,x19,FS_BUF
	mov	x2,BLOC
	sys	SYS_READ
	cmp	x0,0
	b.le	.fini
	add	x1,x19,FS_BUF
	mov	x2,x0
	mov	w0,w21
	bl	crc32_maj
	mov	w21,w0
	b	.r
.fini:	mov	x0,x20
	sys	SYS_CLOSE
	mvn	w21,w21
	add	x0,x19,FS_REP
	adr	x1,r_250x
	bl	str_cpy
	mov	x22,28				; 8 chiffres hexadecimaux
.h:	lsr	w1,w21,w22
	and	w1,w1,15
	cmp	w1,10
	add	w2,w1,'0'
	add	w3,w1,'A'-10
	csel	w1,w2,w3,lo
	strb	w1,[x0],1
	subs	x22,x22,4
	b.pl	.h
	adr	x1,s_crlf
	bl	str_cpy
	add	x0,x19,FS_REP
	bl	reply
	b	.out
.non:	adr	x0,r_550
	bl	dire
.out:	mov	x0,0
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; w0 = crc courant, x1 = donnees, x2 = octets -> w0 (CRC32, polynome
; 0xEDB88320, bit a bit)
crc32_maj:
	movl	w5,0xEDB88320
.o:	cbz	x2,.r
	ldrb	w3,[x1],1
	eor	w0,w0,w3
	mov	x4,8
.b:	tst	w0,1
	lsr	w0,w0,1
	b.eq	.nb
	eor	w0,w0,w5
.nb:	subs	x4,x4,1
	b.ne	.b
	sub	x2,x2,1
	b	.o
.r:	ret

; --- connexion de donnees ---------------------------------------------------------

; PASV : ecoute sur DATA_PORT + numero -> 227 (a,b,c,d,p1,p2)
c_pasv:
	mov	x9,0
	b	pasv_commun
c_epsv:
	mov	x9,1
pasv_commun:
	prologue
	stp	x20,x21,[sp,-16]!
	str	x22,[sp,-16]!
	mov	x22,x9
	bl	data_close
	ldr	x0,[x19,FS_PASV]		; ecoute deja ouverte : resert
	tbz	x0,63,.rep
	mov	x0,SOCK_TCP
	bl	net_socket
	tbnz	x0,63,.non
	mov	x20,x0
	ldr	x1,[x19,FS_NUM]
	add	x1,x1,DATA_PORT
	bl	net_bind
	tbnz	x0,63,.non_f
	mov	x0,x20
	mov	x1,1
	bl	net_listen
	tbnz	x0,63,.non_f
	str	x20,[x19,FS_PASV]
.rep:	str	xzr,[x19,FS_ACTIF_IP]
	ldr	x21,[x19,FS_NUM]
	add	x21,x21,DATA_PORT
	add	x0,x19,FS_REP
	cbnz	x22,.epsv
	adr	x1,r_227			; 227 Entering Passive Mode (a,b,c,d,p1,p2)
	bl	str_cpy
	mov	x20,x0
	add	x0,x19,FS_STAT			; notre adresse
	bl	net_info
	ldr	w9,[x19,FS_STAT]
	mov	x10,24
.o:	lsr	w0,w9,w10
	and	x0,x0,0xFF
	mov	x1,x20
	bl	fmt_dec
	mov	w1,','
	strb	w1,[x0],1
	mov	x20,x0
	subs	x10,x10,8
	b.pl	.o
	lsr	x0,x21,8
	mov	x1,x20
	bl	fmt_dec
	mov	w1,','
	strb	w1,[x0],1
	mov	x1,x0
	and	x0,x21,0xFF
	bl	fmt_dec
	adr	x1,r_227b
	bl	str_cpy
	b	.envoi
.epsv:	adr	x1,r_229			; 229 Entering Extended Passive Mode (|||p|)
	bl	str_cpy
	mov	x1,x0
	mov	x0,x21
	bl	fmt_dec
	adr	x1,r_229b
	bl	str_cpy
.envoi:	add	x0,x19,FS_REP
	bl	reply
	b	.out
.non_f:	mov	x0,x20
	bl	net_close
.non:	adr	x0,r_425
	bl	dire
.out:	mov	x0,0
	ldr	x22,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; PORT a,b,c,d,p1,p2 : mode actif (le serveur se connecte au client)
c_port:
	prologue
	stp	x20,x21,[sp,-16]!
	mov	x20,x0
	mov	x21,0				; six nombres
	mov	x9,0
	mov	x10,0
.n:	mov	x0,x20
	bl	parse_dec
	lsl	x10,x10,8
	orr	x10,x10,x0
	add	x21,x21,1
.s:	ldrb	w0,[x20],1
	cbz	w0,.fin
	cmp	w0,','
	b.ne	.s
	b	.n
.fin:	cmp	x21,6
	b.ne	.non
	lsr	x0,x10,16			; adresse
	and	x0,x0,0xFFFFFFFF
	str	x0,[x19,FS_ACTIF_IP]
	and	x0,x10,0xFFFF
	str	x0,[x19,FS_ACTIF_PORT]
	adr	x0,r_port
	bl	dire
	b	.out
.non:	adr	x0,r_501
	bl	dire
.out:	mov	x0,0
	ldp	x20,x21,[sp],16
	epilogue

; -> x0 = connexion de donnees (FS_DATA), < 0 si aucune (reponse 425 faite)
data_open:
	prologue
	str	x20,[sp,-16]!
	ldr	x0,[x19,FS_ACTIF_IP]		; actif : on se connecte
	cbz	x0,.passif
	mov	x0,SOCK_TCP
	bl	net_socket
	tbnz	x0,63,.non
	mov	x20,x0
	ldr	x1,[x19,FS_ACTIF_IP]
	ldr	x2,[x19,FS_ACTIF_PORT]
	bl	net_connect
	tbnz	x0,63,.non_f
	mov	x0,x20
	b	.ok
.passif:
	ldr	x0,[x19,FS_PASV]
	tbnz	x0,63,.non
	movl	x1,20000
	bl	net_accept
	tbnz	x0,63,.non
.ok:	str	x0,[x19,FS_DATA]
	b	.out
.non_f:	mov	x0,x20
	bl	net_close
.non:	adr	x0,r_425
	bl	dire
	mov	x0,-1
.out:	ldr	x20,[sp],16
	epilogue

; ferme la connexion de donnees (et l'ecoute passive, une fois servie)
data_close:
	prologue
	ldr	x0,[x19,FS_DATA]
	tbnz	x0,63,.p
	bl	net_close
	mov	x0,-1
	str	x0,[x19,FS_DATA]
.p:	ldr	x0,[x19,FS_PASV]
	tbnz	x0,63,.out
	bl	net_close
	mov	x0,-1
	str	x0,[x19,FS_PASV]
.out:	epilogue

; x0 = donnees, x1 = octets : sur la connexion de donnees -> x0 < 0 si echec
data_send:
	prologue
	mov	x2,x1
	mov	x1,x0
	ldr	x0,[x19,FS_DATA]
	bl	net_send
	epilogue

; --- LIST, NLST, RETR, STOR ------------------------------------------------------
c_nlst:
	mov	x9,1
	b	liste
c_list:
	mov	x9,0
liste:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x22,x9
	ldrb	w1,[x0]				; options "-la" : ignorees
	cmp	w1,'-'
	b.ne	.chemin
.opt:	ldrb	w1,[x0]
	cbz	w1,.chemin
	add	x0,x0,1
	cmp	w1,' '
	b.ne	.opt
.chemin:
	bl	resoudre
	tbnz	x0,63,.non
	mov	x23,x0				; 0 : la racine (les volumes)
	cbz	x23,.ouvre
	add	x0,x19,FS_PATH
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	mov	x20,x0
.ouvre:	adr	x0,r_150
	bl	dire
	bl	data_open
	tbnz	x0,63,.ferme
	cbz	x23,.volumes
.e:	mov	x0,x20
	add	x1,x19,FS_DIRENT
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.fini
	add	x0,x19,FS_DIRENT
	ldr	x1,[x0,DIRENT_ATTR]
	ldr	x2,[x0,DIRENT_SIZE]
	mov	x3,x22
	bl	ligne_liste
	tbnz	x0,63,.fini
	b	.e
.volumes:
	adr	x21,volumes			; ceux qui repondent
.v:	ldrb	w0,[x21]
	cbz	w0,.fini
	add	x0,x19,FS_PATH
	mov	x1,x21
	bl	str_cpy
	adr	x1,s_2p
	bl	str_cpy
	add	x0,x19,FS_PATH
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.vn
	sys	SYS_CLOSE
	mov	x0,x21
	mov	x1,ATTR_DIR
	mov	x2,0
	mov	x3,x22
	bl	ligne_liste
.vn:	ldrb	w0,[x21],1
	cbnz	w0,.vn
	b	.v
.fini:	bl	data_close
	adr	x0,r_226
	bl	dire
.ferme:	cbz	x23,.out
	mov	x0,x20
	sys	SYS_CLOSE
	b	.out
.non:	adr	x0,r_550
	bl	dire
.out:	mov	x0,0
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; x0 = nom, x1 = attributs, x2 = taille, x3 = 1 nom seul : une ligne de liste
; ("drwxr-xr-x 1 armpi armpi 0 Jan 01 2026 nom") -> x0 < 0 si echec
ligne_liste:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	add	x0,x19,FS_REP
	cbnz	x3,.nom
	adr	x1,s_fichier
	tst	x21,ATTR_DIR
	b.eq	.f
	adr	x1,s_dossier
.f:	bl	str_cpy
	mov	x1,x0
	mov	x0,x22
	bl	fmt_dec
	adr	x1,s_date
	bl	str_cpy
.nom:	mov	x1,x20
	bl	str_cpy
	adr	x1,s_crlf
	bl	str_cpy
	add	x1,x19,FS_REP
	sub	x1,x0,x1
	add	x0,x19,FS_REP
	bl	data_send
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

c_retr:
	prologue
	stp	x20,x21,[sp,-16]!
	bl	resoudre
	cmp	x0,0
	b.le	.non
	add	x0,x19,FS_PATH
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	mov	x20,x0
	adr	x0,r_150
	bl	dire
	bl	data_open
	tbnz	x0,63,.ferme
.r:	mov	x0,x20
	add	x1,x19,FS_BUF
	mov	x2,BLOC
	sys	SYS_READ
	cmp	x0,0
	b.le	.fini
	mov	x1,x0
	add	x0,x19,FS_BUF
	bl	data_send
	tbz	x0,63,.r
	bl	data_close
	adr	x0,r_426
	bl	dire
	b	.ferme
.fini:	bl	data_close
	adr	x0,r_226
	bl	dire
.ferme:	mov	x0,x20
	sys	SYS_CLOSE
	b	.out
.non:	adr	x0,r_550
	bl	dire
.out:	mov	x0,0
	ldp	x20,x21,[sp],16
	epilogue

c_stor:
	prologue
	stp	x20,x21,[sp,-16]!
	bl	resoudre
	cmp	x0,0
	b.le	.non
	add	x0,x19,FS_PATH
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.non
	mov	x20,x0
	adr	x0,r_150
	bl	dire
	bl	data_open
	tbnz	x0,63,.ferme
.r:	ldr	x0,[x19,FS_DATA]
	add	x1,x19,FS_BUF
	mov	x2,BLOC
	movl	x3,60000
	bl	net_recv
	cmp	x0,0
	b.le	.fini				; 0 : le client a tout envoye
	mov	x2,x0
	mov	x0,x20
	add	x1,x19,FS_BUF
	sys	SYS_WRITE
	tbz	x0,63,.r
	bl	data_close
	adr	x0,r_452
	bl	dire
	b	.ferme
.fini:	bl	data_close
	adr	x0,r_226
	bl	dire
.ferme:	mov	x0,x20
	sys	SYS_CLOSE
	b	.out
.non:	adr	x0,r_553
	bl	dire
.out:	mov	x0,0
	ldp	x20,x21,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Chemins
; ---------------------------------------------------------------------------

; x0 = argument (chemin FTP, relatif ou absolu ; vide : le dossier) ->
; FS_REP = chemin FTP normalise ("/SD0/a/b"), FS_PATH = chemin ARMPI
; ("SD0:/a/b") ; x0 = 0 : la racine (pas de chemin ARMPI), 1 : un chemin,
; < 0 : invalide. Accepte aussi "SD0:/a/b".
resoudre:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x20,x0
	add	x21,x19,FS_REP			; construction du chemin FTP
	ldrb	w0,[x20]
	cmp	w0,'/'
	b.eq	.absolu
	mov	x0,x20				; "SD0:/..." ?
	mov	w1,':'
	bl	contient_avant_slash
	cbnz	x0,.armpi
	mov	x0,x21				; relatif : dossier + "/" + argument
	add	x1,x19,FS_CWD
	bl	str_cpy
	mov	w1,'/'
	strb	w1,[x0],1
	mov	x1,x20
	bl	str_cpy
	b	.norm
.armpi:	mov	w0,'/'				; "SD0:/a" -> "/SD0/a"
	strb	w0,[x21]
	add	x0,x21,1
	mov	x1,x20
.ac:	ldrb	w2,[x1],1
	cmp	w2,':'
	b.eq	.ac
	strb	w2,[x0],1
	cbnz	w2,.ac
	b	.norm
.absolu:
	mov	x0,x21
	mov	x1,x20
	bl	str_cpy
.norm:	mov	x0,x21				; ".", "..", "//"
	bl	normaliser
	; FS_REP = "/" ou "/VOL[/...]" -> FS_PATH = "VOL:/..."
	ldrb	w0,[x21,1]
	cbz	w0,.racine
	add	x22,x19,FS_PATH
	add	x1,x21,1
.vol:	ldrb	w2,[x1],1
	cbz	w2,.fv
	cmp	w2,'/'
	b.eq	.fv
	strb	w2,[x22],1
	b	.vol
.fv:	mov	w3,':'
	strb	w3,[x22],1
	mov	w3,'/'
	strb	w3,[x22],1
	cbz	w2,.term
.reste:	ldrb	w2,[x1],1
	strb	w2,[x22],1
	cbnz	w2,.reste
	b	.ok
.term:	strb	wzr,[x22]
.ok:	mov	x0,1
	b	.out
.racine:
	mov	x0,0
.out:	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; x0 = chaine, w1 = caractere -> x0 = 1 s'il y est avant le premier "/"
contient_avant_slash:
.c:	ldrb	w2,[x0],1
	cbz	w2,.non
	cmp	w2,'/'
	b.eq	.non
	cmp	w2,w1
	b.ne	.c
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = chemin absolu "/a/./b/../c//" -> "/a/c" (sur place : l'ecriture,
; "/" puis le segment, ne depasse jamais la lecture)
normaliser:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x20,x0				; lecture
	mov	x21,x0				; debut
	mov	x22,x0				; ecriture
.seg:	ldrb	w2,[x20]			; "/" multiples
	cmp	w2,'/'
	b.ne	.s1
	add	x20,x20,1
	b	.seg
.s1:	cbz	w2,.fin
	mov	x23,x20				; un segment
.s2:	ldrb	w2,[x20]
	cbz	w2,.s3
	cmp	w2,'/'
	b.eq	.s3
	add	x20,x20,1
	b	.s2
.s3:	sub	x3,x20,x23			; longueur
	cmp	x3,1
	b.ne	.pp
	ldrb	w4,[x23]
	cmp	w4,'.'
	b.eq	.seg				; "."
.pp:	cmp	x3,2
	b.ne	.copie
	ldrb	w4,[x23]
	ldrb	w5,[x23,1]
	cmp	w4,'.'
	b.ne	.copie
	cmp	w5,'.'
	b.ne	.copie
.rec:	cmp	x22,x21				; ".." : segment precedent retire
	b.ls	.seg
	sub	x22,x22,1
	ldrb	w4,[x22]
	cmp	w4,'/'
	b.ne	.rec
	b	.seg
.copie:	mov	w4,'/'
	strb	w4,[x22],1
.c:	ldrb	w4,[x23],1
	strb	w4,[x22],1
	cmp	x23,x20
	b.lo	.c
	b	.seg
.fin:	cmp	x22,x21				; rien : la racine
	b.hi	.z
	mov	w4,'/'
	strb	w4,[x22],1
.z:	strb	wzr,[x22]
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; x0 = destination, x1 = chaine -> x0 = fin (sur le 0)
str_cpy:
	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,str_cpy
	sub	x0,x0,1
	ret

	align 4
verbes:
	dw	v_user - verbes, c_user - verbes
	dw	v_pass - verbes, c_pass - verbes
	dw	v_syst - verbes, c_syst - verbes
	dw	v_feat - verbes, c_feat - verbes
	dw	v_opts - verbes, c_ok - verbes
	dw	v_mode - verbes, c_ok - verbes
	dw	v_stru - verbes, c_ok - verbes
	dw	v_noop - verbes, c_noop - verbes
	dw	v_quit - verbes, c_quit - verbes
	dw	v_pwd - verbes, c_pwd - verbes
	dw	v_xpwd - verbes, c_pwd - verbes
	dw	v_cwd - verbes, c_cwd - verbes
	dw	v_cdup - verbes, c_cdup - verbes
	dw	v_type - verbes, c_type - verbes
	dw	v_pasv - verbes, c_pasv - verbes
	dw	v_epsv - verbes, c_epsv - verbes
	dw	v_port - verbes, c_port - verbes
	dw	v_list - verbes, c_list - verbes
	dw	v_nlst - verbes, c_nlst - verbes
	dw	v_retr - verbes, c_retr - verbes
	dw	v_stor - verbes, c_stor - verbes
	dw	v_size - verbes, c_size - verbes
	dw	v_mkd - verbes, c_mkd - verbes
	dw	v_xmkd - verbes, c_mkd - verbes
	dw	v_rmd - verbes, c_dele - verbes
	dw	v_xrmd - verbes, c_dele - verbes
	dw	v_dele - verbes, c_dele - verbes
	dw	v_xcrc - verbes, c_xcrc - verbes
	dw	v_rnfr - verbes, c_non - verbes
	dw	v_rnto - verbes, c_non - verbes
	dw	0, 0
v_user	db 'USER',0
v_pass	db 'PASS',0
v_syst	db 'SYST',0
v_feat	db 'FEAT',0
v_opts	db 'OPTS',0
v_mode	db 'MODE',0
v_stru	db 'STRU',0
v_noop	db 'NOOP',0
v_quit	db 'QUIT',0
v_pwd	db 'PWD',0
v_xpwd	db 'XPWD',0
v_cwd	db 'CWD',0
v_cdup	db 'CDUP',0
v_type	db 'TYPE',0
v_pasv	db 'PASV',0
v_epsv	db 'EPSV',0
v_port	db 'PORT',0
v_list	db 'LIST',0
v_nlst	db 'NLST',0
v_retr	db 'RETR',0
v_stor	db 'STOR',0
v_size	db 'SIZE',0
v_mkd	db 'MKD',0
v_xmkd	db 'XMKD',0
v_rmd	db 'RMD',0
v_xrmd	db 'XRMD',0
v_dele	db 'DELE',0
v_xcrc	db 'XCRC',0
v_rnfr	db 'RNFR',0
v_rnto	db 'RNTO',0

volumes		db 'SD0',0,'SD1',0,'RAM0',0,'USB0',0,'USB1',0,'IMG0',0,'IMG1',0,0
s_vide		db 0
s_parent	db '..',0
s_2p		db ':/',0
s_crlf		db 13,10,0
s_dossier	db 'drwxr-xr-x 1 korin korin ',0
s_fichier	db '-rw-r--r-- 1 korin korin ',0
s_date		db ' Jan 01 2026 ',0
r_bonjour	db '220 Korin OS ftpd (sans mot de passe)',13,10,0
r_plein		db '421 Trop de sessions',13,10,0
r_user		db '331 Mot de passe (non verifie)',13,10,0
r_pass		db '230 Bienvenue',13,10,0
r_syst		db '215 UNIX Type: L8',13,10,0
r_feat		db '211-Extensions',13,10,' SIZE',13,10,' XCRC',13,10,' EPSV',13,10
		db ' PASV',13,10,' UTF8',13,10,'211 Fin',13,10,0
r_ok		db '200 OK',13,10,0
r_type		db '200 Binaire',13,10,0
r_quit		db '221 Au revoir',13,10,0
r_non		db '502 Non disponible',13,10,0
r_inconnu	db '500 Commande inconnue',13,10,0
r_257		db '257 "',0
r_257b		db '" est le dossier',0
r_cwd		db '250 Dossier change',13,10,0
r_250		db '250 Fait',13,10,0
r_250x		db '250 ',0
r_213		db '213 ',0
r_mkd		db '257 Dossier cree',13,10,0
r_550		db '550 Introuvable ou refuse',13,10,0
r_553		db '553 Ecriture refusee',13,10,0
r_452		db '452 Ecriture interrompue',13,10,0
r_501		db '501 Argument invalide',13,10,0
r_227		db '227 Entering Passive Mode (',0
r_227b		db ')',13,10,0
r_229		db '229 Entering Extended Passive Mode (|||',0
r_229b		db '|)',13,10,0
r_port		db '200 PORT accepte',13,10,0
r_150		db '150 Ouverture de la connexion de donnees',13,10,0
r_226		db '226 Transfert fini',13,10,0
r_425		db '425 Pas de connexion de donnees',13,10,0
r_426		db '426 Transfert interrompu',13,10,0
m_pret		db 'ftpd : en ',0xC3,0xA9,'coute sur le port 21 (sans mot de passe)',10,0
m_session	db 'ftpd : session',10,0
m_finie		db 'ftpd : session finie',10,0
		align 8

segment readable writeable
kit_data
		align 8
sess		dd 0
