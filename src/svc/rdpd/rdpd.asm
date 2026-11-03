; rdpd - le bureau a distance d'Onyx (docs/RESEAU.md 5.4, etape 12), port 3390,
; lance par init2 (SD0:/etc/services.txt). Client : Korin Remote (tools/remote,
; .NET ; copie d'Onyx Remote, pc/OnyxRemote d'Onyx) : chaque fenetre d'ARMPI
; devient une fenetre du PC, son contenu et (au choix du client) son cadre, au
; lieu d'une image de l'ecran.
; Protocole d'Onyx (user/BinUtils/rdpd.c), petit-boutiste, plus un mot de passe
; (celui de l'acces a distance, SD0:/etc/remote.txt, inc/remotepw.inc, relu a
; chaque connexion), un client a la fois (un nouveau client prend la place de
; l'ancien : le PC qui se reconnecte apres une coupure) :
;  * salut : "ONYXRDP2", largeur, hauteur, 96 (u16 ; le client y lit la
;    version de l'ABI d'Onyx et veut au moins 56), puis un defi de 16 octets
;    (SYS_GETRANDOM) ; le client repond "ONYXRDP2" + options (1 : 16 bits,
;    2 : sans cadres, 4 : rondes en pipeline -> CAPS, 8 : COPY, ignore ici)
;    + HMAC-SHA256 (cle : le mot de passe en UTF-8, message : le defi ; 32
;    octets, sha256.inc) ; le serveur repond un octet : 0 accepte, 1 mot de
;    passe faux, 2 aucun mot de passe sur ARMPI (tout est refuse) ; sauf 0, il
;    ferme. "ONYXRDP2" et non "ONYXRDP1" : un ancien client (Onyx Remote) dit
;    « not an Onyx rdpd server » et s'arrete, sans boucler sur les reconnexions ;
;    Korin Remote parle encore "ONYXRDP1" (sans mot de passe) au rdpd d'Onyx ;
;  * serveur -> client : type (u8), longueur (u32), charge : 1 WIN, 2 GONE,
;    3 ZORDER, 4 PIXELS (LZ4 si plus court, sinon brut ; 32 bits, ou 16 bits
;    RGB565 si demande, sauf les cadres et les fenetres WF_ALPHA : 32 bits,
;    octet haut = transparence), 5 END, 8 SCREEN, 9 CAPS, 10 PING, 11 CURSOR ;
;  * client -> serveur : 1 READY, 2 PTR, 3 KEY, 4 RAISE, 5 CLOSE, 6 CHAR,
;    7 DESKTOP, 8 PONG, 9 MOVE.
; Une ronde par credit (un READY en donne un, 3 en vol au plus en pipeline),
; seulement si quelque chose a change : WS_OP_WIN_LIST d'Elegant ; pour chaque
; fenetre dont la generation du contenu a bouge, WS_OP_WIN_READ par bandes,
; comparaison par carres de 64 x 64 avec la copie qu'a le client, envoi des
; carres changes (cote a cote : un rectangle) ; le cadre quand sa generation
; bouge. Les fenetres du fond (WF_BACK : le fond d'ecran) ne sont pas dites :
; elles sont dans la pseudo-fenetre du bureau (0xFFFFFFFF, si le client la
; demande : message DESKTOP). Entrees : pilote virtuel de HIDManager (comme
; vncd) ; le pointeur arrive en coordonnees de la fenetre et repart a sa place
; sur l'ecran (la fenetre passe devant au clic).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/netkit/netkit.inc'

PORT		= 3390
HID_OP_PILOTE	= 3
ANNEAU_EV	= 64
ANNEAU_N	= 64
EV_KEY		= 1
EV_MOUSE_ABS	= 3
EV_WHEEL	= 4
IN_MAX		= 4096
OUT_MAX		= 65536			; messages : envoyes par paquets de 64 Ko
TILE		= 64
PACK_MAX	= 2304 * TILE * 4	; une bande de 64 lignes (cadre de 2048 + 8)
LZ_MAX		= PACK_MAX + PACK_MAX / 255 + 64
HT_BYTES	= 4096 * 4		; LZ4 : table de hachage (12 bits)
MAXWIN		= 34			; 32 fenetres d'Elegant + le bureau
PIPE_ROUNDS	= 3
MIN_ROUND_MS	= 20			; 50 rondes par seconde au plus
CURSOR_POLL_MS	= 50			; la forme du curseur
IDLE_PING_MS	= 2000			; client en pipeline silencieux : un PING
SILENT_MS	= 12000			; ... sans rien entendre : la session finit
POLL_MS		= 500			; un nouveau client ?
DPI		= 96
DESK		= 0xFFFFFFFF		; pseudo-fenetre du bureau
AUTH_LEN	= 8 + 1 + 32		; la reponse au salut : "ONYXRDP2", options, HMAC
AUTH_NONE	= 2			; verdicts : 0 accepte, 1 faux, 2 aucun mot de passe
; une fenetre telle que le client l'a
SL_ID		= 0			; 0 : place libre
SL_X		= 8
SL_Y		= 16
SL_W		= 24
SL_H		= 32
SL_FLAGS	= 40			; WF_*
SL_STATE	= 48			; WES_*
SL_GEN		= 56
SL_CGEN		= 64
SL_SENT		= 72			; son WIN est parti
SL_STALE	= 80			; pixels a envoyer en entier
SL_ALIVE	= 88
SL_BUF		= 96			; copie du contenu qu'a le client
SL_CAP		= 104			; (octets ; garde quand la place change de fenetre)
SL_BW		= 112
SL_BH		= 120
SL_TITLE	= 128			; 32 octets
SL_SIZE		= 160

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	kits_open
	movl	x0,OUT_MAX
	bl	malloc
	cbz	x0,quit
	adr	x1,outbuf
	str	x0,[x1]
	movl	x0,PACK_MAX
	bl	malloc
	cbz	x0,quit
	adr	x1,packbuf
	str	x0,[x1]
	movl	x0,LZ_MAX
	bl	malloc
	cbz	x0,quit
	adr	x1,lzbuf
	str	x0,[x1]
	mov	x0,HT_BYTES
	bl	malloc
	cbz	x0,quit
	adr	x1,htab
	str	x0,[x1]
	adr	x0,next_sock
	mov	x1,-1
	str	x1,[x0]
	adr	x0,s_hid			; pilote virtuel de HIDManager
	movl	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.sock
	adr	x1,msg
	mov	x2,HID_OP_PILOTE
	str	x2,[x1]
	str	wzr,[x1,WM_LEN]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.sock
	sys	SYS_BUF_MAP
	tbnz	x0,63,.sock
	adr	x1,anneau
	str	x0,[x1]
.sock:	mov	x0,SOCK_TCP
	bl	net_socket
	tbnz	x0,63,.attente
	mov	x19,x0
	adr	x1,lsock
	str	x19,[x1]
	movl	x1,PORT
	bl	net_bind
	tbnz	x0,63,.ferme
	mov	x0,x19
	mov	x1,2
	bl	net_listen
	tbnz	x0,63,.ferme
	adr	x0,m_pret
	bl	puts
.loop:	adr	x1,next_sock			; un client arrive pendant la session
	ldr	x0,[x1]
	tbz	x0,63,.got
	mov	x0,x19
	movl	x1,3600000
	bl	net_accept
	cmn	x0,11
	b.eq	.loop
	tbnz	x0,63,.erreur
.got:	mov	x2,-1
	adr	x1,next_sock
	str	x2,[x1]
	adr	x1,sock
	str	x0,[x1]
	adr	x0,m_client
	bl	puts
	bl	session
	adr	x0,sock
	ldr	x0,[x0]
	bl	net_close
	adr	x0,m_fin
	bl	puts
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
quit:	adr	x0,m_nomem
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Petits outils
; ---------------------------------------------------------------------------

; -> x0 = horloge (ms)
now_ms:
	sys	SYS_TIME_MS
	ret

; x0 = op, x1 = a, x2 = b, x3 = c, x4 = d : requete a Elegant -> x0
; (la reponse dans msg)
wscall:
	adr	x9,msg
	stp	x0,x1,[x9,WM_OP]
	stp	x2,x3,[x9,WM_B]
	str	x4,[x9,WM_D]
	str	wzr,[x9,WM_LEN]
	mov	x0,x9
	sys	SYS_WS_CALL
	ret

; x0 = destination, x1 = source, x2 = octets (sans recouvrement)
copy_bytes:
.c16:	cmp	x2,16
	b.lo	.c1
	ldp	x3,x4,[x1],16
	stp	x3,x4,[x0],16
	sub	x2,x2,16
	b	.c16
.c1:	cbz	x2,.ret
	ldrb	w3,[x1],1
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c1
.ret:	ret

; x0 = adresse -> x0 = 32 bits petit-boutistes
get32:
	ldrb	w1,[x0]
	ldrb	w2,[x0,1]
	orr	w1,w1,w2,lsl 8
	ldrb	w2,[x0,2]
	orr	w1,w1,w2,lsl 16
	ldrb	w2,[x0,3]
	orr	w0,w1,w2,lsl 24
	ret

; x0 = adresse -> x0 = 16 bits signes
get16s:
	ldrb	w1,[x0]
	ldrb	w2,[x0,1]
	orr	w1,w1,w2,lsl 8
	sxth	x0,w1
	ret

; ---------------------------------------------------------------------------
;  Sortie : les messages s'accumulent dans outbuf, envoyes par flush_out
; ---------------------------------------------------------------------------

; x0 = donnees, x1 = octets
put:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.l:	cbz	x20,.out
	adr	x0,outlen
	ldr	x21,[x0]
	movl	x2,OUT_MAX
	sub	x22,x2,x21			; place
	cbnz	x22,.c
	bl	flush_out
	b	.l
.c:	cmp	x22,x20
	csel	x22,x20,x22,hi
	adr	x0,outbuf
	ldr	x0,[x0]
	add	x0,x0,x21
	mov	x1,x19
	mov	x2,x22
	bl	copy_bytes
	add	x21,x21,x22
	adr	x0,outlen
	str	x21,[x0]
	add	x19,x19,x22
	sub	x20,x20,x22
	b	.l
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur : 4, 2 ou 1 octet(s) petit-boutiste(s)
put32:	mov	x1,4
	b	putn
put16:	mov	x1,2
	b	putn
put8:	mov	x1,1
putn:	prologue
	str	x0,[sp,-16]!
	mov	x0,sp
	bl	put
	add	sp,sp,16
	epilogue

; x0 = type, x1 = longueur de la charge : en-tete d'un message (ceux de la
; ronde comptes, sauf END, PING, CURSOR)
msg_head:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x0,5
	b.eq	.n
	cmp	x0,10
	b.eq	.n
	cmp	x0,11
	b.eq	.n
	adr	x2,roundmsgs
	ldr	x3,[x2]
	add	x3,x3,1
	str	x3,[x2]
.n:	mov	x0,x19
	bl	put8
	mov	x0,x20
	bl	put32
	ldp	x19,x20,[sp],16
	epilogue

; envoie outbuf (en entier : net_send attend la place) ; echec -> dead
flush_out:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,outbuf
	ldr	x19,[x0]
	adr	x0,outlen
	ldr	x20,[x0]
.l:	cbz	x20,.done
	adr	x0,dead
	ldr	x0,[x0]
	cbnz	x0,.done
	adr	x0,sock
	ldr	x0,[x0]
	mov	x1,x19
	mov	x2,x20
	bl	net_send
	cmp	x0,0
	b.le	.ko
	add	x19,x19,x0
	sub	x20,x20,x0
	b	.l
.ko:	adr	x0,dead
	mov	x1,1
	str	x1,[x0]
	adr	x0,m_send
	bl	puts
.done:	adr	x0,outlen
	str	xzr,[x0]
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Entree
; ---------------------------------------------------------------------------

; x0 = attente (ms) : ce qui est arrive -> inbuf ; x0 = 0 si la connexion est
; fermee (ou en erreur), 1 sinon
fill_in:
	prologue
	str	x19,[sp,-16]!
	mov	x3,x0
	adr	x0,in_n
	ldr	x19,[x0]
	cmp	x19,IN_MAX
	b.hs	.heard				; (plein : le client parle)
	adr	x1,inbuf
	add	x1,x1,x19
	mov	x2,IN_MAX
	sub	x2,x2,x19
	adr	x0,sock
	ldr	x0,[x0]
	bl	net_recv
	cmn	x0,11				; E_AGAIN : rien
	b.eq	.ok
	cmp	x0,0
	b.le	.closed
	add	x19,x19,x0
	adr	x1,in_n
	str	x19,[x1]
.heard:	bl	now_ms
	adr	x1,last_rx
	str	x0,[x1]
.ok:	mov	x0,1
	b	.out
.closed:
	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = octets, x1 = attente totale (ms) -> x0 = 1 si inbuf en a autant
need:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	str	x1,[sp,-16]!
	bl	now_ms
	ldr	x1,[sp],16
	add	x20,x0,x1
.l:	adr	x0,in_n
	ldr	x0,[x0]
	cmp	x0,x19
	b.hs	.yes
	mov	x0,100
	bl	fill_in
	cbz	x0,.no
	bl	now_ms
	cmp	x0,x20
	b.lo	.l
.no:	mov	x0,0
	b	.out
.yes:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = octets : retires du debut d'inbuf
consume:
	adr	x1,in_n
	ldr	x2,[x1]
	sub	x2,x2,x0
	str	x2,[x1]
	adr	x3,inbuf
	add	x4,x3,x0
.c:	cbz	x2,.ret
	ldrb	w5,[x4],1
	strb	w5,[x3],1
	sub	x2,x2,1
	b	.c
.ret:	ret

; ---------------------------------------------------------------------------
;  Une session : sock (le client), jusqu'a sa fin
; ---------------------------------------------------------------------------
session:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,in_n				; remise a zero
	str	xzr,[x0]
	adr	x0,outlen
	str	xzr,[x0]
	adr	x0,dead
	str	xzr,[x0]
	adr	x0,opt_desktop
	str	xzr,[x0]
	adr	x0,btn
	str	xzr,[x0]
	adr	x0,mods
	str	xzr,[x0]
	adr	x0,order_n
	mov	x1,-1
	str	x1,[x0]
	adr	x0,slots			; les fenetres oubliees (copies gardees)
	mov	x1,MAXWIN
.s:	str	xzr,[x0,SL_ID]
	str	xzr,[x0,SL_SENT]
	add	x0,x0,SL_SIZE
	subs	x1,x1,1
	b.ne	.s
	adr	x0,scr_w			; taille de l'ecran
	sys	SYS_SCREEN_INFO
	adr	x0,defi				; le defi du mot de passe
	mov	x1,16
	sys	SYS_GETRANDOM
	cmp	x0,16
	b.ne	.out
	adr	x0,s_magic			; salut
	mov	x1,8
	bl	put
	adr	x0,scr_w
	ldr	x0,[x0]
	bl	put16
	adr	x0,scr_h
	ldr	x0,[x0]
	bl	put16
	mov	x0,DPI
	bl	put16
	adr	x0,defi
	mov	x1,16
	bl	put
	bl	flush_out
	mov	x0,AUTH_LEN			; la reponse du client
	movl	x1,15000
	bl	need
	cbz	x0,.out
	adr	x0,inbuf
	ldr	x1,[x0]
	adr	x2,s_magic
	ldr	x2,[x2]
	cmp	x1,x2
	b.ne	.out
	bl	auth				; le mot de passe -> x0 = verdict
	mov	x21,x0
	bl	put8
	bl	flush_out
	cbz	x21,.auth_ok
	cmp	x21,AUTH_NONE
	adr	x0,m_refus
	adr	x1,m_aucun
	csel	x0,x1,x0,eq
	bl	puts
	b	.out
.auth_ok:
	adr	x0,inbuf
	ldrb	w1,[x0,8]			; options
	and	x2,x1,1
	adr	x3,opt_16
	str	x2,[x3]
	ubfx	x2,x1,1,1
	adr	x3,opt_noframes
	str	x2,[x3]
	ubfx	x2,x1,2,1
	adr	x3,opt_pipe
	str	x2,[x3]
	mov	x0,AUTH_LEN
	bl	consume
	adr	x0,opt_pipe			; credit : 1 ronde en vol, 3 en pipeline
	ldr	x0,[x0]
	mov	x1,1
	cbz	x0,.w
	mov	x1,PIPE_ROUNDS
.w:	adr	x2,rounds_max
	str	x1,[x2]
	sub	x1,x1,1				; (+ le premier READY du client)
	adr	x2,credit
	str	x1,[x2]
	cbz	x0,.cursor
	mov	x0,9				; CAPS : protocole 2 (MOVE), 3 rondes
	mov	x1,2
	bl	msg_head
	mov	x0,2
	bl	put8
	mov	x0,PIPE_ROUNDS
	bl	put8
.cursor:
	adr	x0,cur_sent			; CURSOR : la fleche, puis cursor_poll
	str	xzr,[x0]
	mov	x0,11
	mov	x1,1
	bl	msg_head
	mov	x0,0
	bl	put8
	bl	flush_out
	bl	now_ms
	adr	x1,last_rx
	str	x0,[x1]
	adr	x1,last_ping
	str	x0,[x1]
	adr	x1,last_poll
	str	x0,[x1]
	sub	x0,x0,1000
	adr	x1,last_round
	str	x0,[x1]
	adr	x1,round_wait
	mov	x0,MIN_ROUND_MS
	str	x0,[x1]
	adr	x0,m_session
	bl	puts
.loop:	adr	x0,dead
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x0,credit			; attente : courte si une ronde peut partir
	ldr	x0,[x0]
	mov	x1,5
	mov	x2,30
	cmp	x0,0
	csel	x0,x1,x2,gt
	bl	fill_in
	cbz	x0,.closed
	bl	parse
	adr	x0,dead
	ldr	x0,[x0]
	cbnz	x0,.out
	bl	now_ms
	mov	x19,x0
	adr	x0,credit
	ldr	x0,[x0]
	cmp	x0,0
	b.le	.alive
	adr	x0,last_round
	ldr	x1,[x0]
	sub	x1,x19,x1
	adr	x2,round_wait
	ldr	x2,[x2]
	cmp	x1,x2
	b.lo	.alive
	str	x19,[x0]			; une ronde
	bl	round_send
	cbz	x0,.took
	adr	x1,credit
	ldr	x2,[x1]
	sub	x2,x2,1
	str	x2,[x1]
.took:	bl	now_ms				; prochaine : 2 x le temps pris, 20 ms
	sub	x0,x0,x19			; au moins
	lsl	x0,x0,1
	mov	x1,MIN_ROUND_MS
	cmp	x0,x1
	csel	x0,x1,x0,lo
	adr	x1,round_wait
	str	x0,[x1]
	bl	now_ms
	mov	x19,x0
.alive:	bl	cursor_poll
	adr	x0,dead
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x0,opt_pipe			; pipeline : PING si silencieux ; fin si
	ldr	x0,[x0]				; plus rien depuis SILENT_MS
	cbz	x0,.poll
	adr	x0,last_rx
	ldr	x20,[x0]
	sub	x20,x19,x20
	movl	x0,SILENT_MS
	cmp	x20,x0
	b.hi	.silent
	cmp	x20,IDLE_PING_MS
	b.lo	.poll
	adr	x0,last_ping
	ldr	x1,[x0]
	sub	x1,x19,x1
	cmp	x1,IDLE_PING_MS
	b.lo	.poll
	str	x19,[x0]
	mov	x0,10
	mov	x1,4
	bl	msg_head
	mov	x0,x19
	bl	put32
	bl	flush_out
.poll:	adr	x0,last_poll			; un nouveau client prend la place
	ldr	x1,[x0]
	sub	x1,x19,x1
	cmp	x1,POLL_MS
	b.lo	.loop
	str	x19,[x0]
	adr	x0,lsock
	ldr	x0,[x0]
	mov	x1,0
	bl	net_accept
	tbnz	x0,63,.loop
	adr	x1,next_sock
	str	x0,[x1]
	adr	x0,m_next
	bl	puts
	b	.out
.silent:
	adr	x0,m_silent
	bl	puts
	b	.out
.closed:
	adr	x0,m_closed
	bl	puts
.out:	bl	release_all
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; inbuf = la reponse au salut ("ONYXRDP2", options, HMAC-SHA256 du defi) ->
; x0 = verdict : 0 accepte, 1 mot de passe faux, AUTH_NONE aucun (SD0:/etc/
; remote.txt absent ou vide : tout est refuse). Comparaison en temps constant ;
; le mot de passe ne reste pas en memoire.
auth:
	prologue
	str	x19,[sp,-16]!
	bl	rp_lire
	mov	x19,AUTH_NONE
	cbz	x0,.wipe
	mov	x1,x0
	adr	x0,rp_pw
	adr	x2,defi
	mov	x3,16
	adr	x4,attendu
	bl	hmac_sha256
	adr	x0,attendu
	adr	x1,inbuf
	add	x1,x1,9
	mov	x2,0
	mov	w3,0
.c:	ldrb	w4,[x0,x2]
	ldrb	w5,[x1,x2]
	eor	w4,w4,w5
	orr	w3,w3,w4
	add	x2,x2,1
	cmp	x2,32
	b.lo	.c
	cmp	w3,0
	cset	x19,ne
.wipe:	adr	x0,rp_pw			; (le mot de passe, le hache attendu)
	mov	x1,0
.w:	strb	wzr,[x0,x1]
	add	x1,x1,1
	cmp	x1,RP_MAX+1
	b.lo	.w
	adr	x0,attendu
	stp	xzr,xzr,[x0]
	stp	xzr,xzr,[x0,16]
	mov	x0,x19
	ldr	x19,[sp],16
	epilogue

include '../../inc/remotepw.inc'
include 'sha256.inc'

; x19 = maintenant : la forme du curseur a change -> CURSOR (KAPI_CURSOR_* d'Onyx, OnyxRemote)
cursor_poll:
	prologue
	adr	x0,cur_at
	ldr	x1,[x0]
	sub	x1,x19,x1
	cmp	x1,CURSOR_POLL_MS
	b.lo	.out
	str	x19,[x0]
	mov	x0,WS_OP_CURSOR_GET
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	wscall
	tbnz	x0,63,.out
	cmp	x0,12
	b.hs	.out
	adr	x1,cur_kapi
	ldrb	w2,[x1,x0]
	adr	x1,cur_sent
	ldr	x3,[x1]
	cmp	x2,x3
	b.eq	.out
	str	x2,[x1]
	str	x2,[sp,-16]!
	mov	x0,11
	mov	x1,1
	bl	msg_head
	ldr	x0,[sp],16
	bl	put8
	bl	flush_out
.out:	epilogue

; UC_* -> KAPI_CURSOR_* : fleche, texte, main, H, V, attente, croix, glisser, deplacer, coins, interdit
cur_kapi	db 0,2,1,4,5,10,9,3,3,6,7,11
		align 4

; les messages complets d'inbuf ; un message inconnu -> dead
parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
.m:	adr	x0,in_n
	ldr	x20,[x0]
	cbz	x20,.done
	adr	x19,inbuf
	ldrb	w21,[x19]			; type
	cbz	w21,.bad
	cmp	w21,9
	b.hi	.bad
	cmp	w21,8				; PONG, MOVE : clients en pipeline
	b.lo	.len
	adr	x0,opt_pipe
	ldr	x0,[x0]
	cbz	x0,.bad
.len:	adr	x0,msglen
	ldrb	w22,[x0,x21]
	cmp	x20,x22
	b.lo	.done				; incomplet : la suite plus tard
	cmp	w21,1
	b.eq	.ready
	cmp	w21,2
	b.eq	.ptr
	cmp	w21,3
	b.eq	.key
	cmp	w21,4
	b.eq	.raise
	cmp	w21,5
	b.eq	.close
	cmp	w21,6
	b.eq	.char
	cmp	w21,7
	b.eq	.desk
	cmp	w21,9
	b.eq	.move
	b	.next				; 8 PONG : rien
.ready:	adr	x0,credit
	ldr	x1,[x0]
	add	x1,x1,1
	adr	x2,rounds_max
	ldr	x2,[x2]
	cmp	x1,x2
	csel	x1,x2,x1,gt
	str	x1,[x0]
	b	.next
.ptr:	add	x0,x19,5
	bl	get16s
	mov	x20,x0				; x
	add	x0,x19,7
	bl	get16s
	mov	x2,x0				; y
	ldrb	w3,[x19,9]			; boutons
	ldrsb	x4,[x19,10]			; molette
	add	x0,x19,1
	stp	x2,x3,[sp,-16]!
	str	x4,[sp,-16]!
	bl	get32
	ldr	x4,[sp],16
	ldp	x2,x3,[sp],16
	mov	x1,x20
	bl	pointer
	b	.next
.key:	ldrb	w20,[x19,1]			; drapeaux : 1 appui, 2 tenue seulement
	tbnz	w20,1,.next			; (les jeux d'Onyx : rien ici)
	add	x0,x19,2
	bl	get32
	mov	w1,w0
	and	x0,x20,1
	bl	key_sym
	b	.next
.raise:	add	x0,x19,1
	bl	get32
	bl	do_raise
	b	.next
.close:	add	x0,x19,1
	bl	get32
	movl	x1,DESK
	cmp	x0,x1
	b.eq	.next
	mov	x1,x0
	mov	x0,WS_OP_WIN_CLOSE
	bl	wscall
	b	.next
.char:	add	x0,x19,1
	bl	get32
	bl	char_in
	b	.next
.desk:	ldrb	w0,[x19,1]
	cmp	w0,0
	cset	x0,ne
	adr	x1,opt_desktop
	str	x0,[x1]
	b	.next
.move:	add	x0,x19,5
	bl	get16s
	mov	x20,x0
	add	x0,x19,7
	bl	get16s
	mov	x21,x0
	add	x0,x19,1
	bl	get32
	mov	x22,x0
	bl	find_slot
	cbz	x0,.mnext
	ldr	x1,[x0,SL_FLAGS]
	tst	x1,WF_BACK or WF_TOP
	b.ne	.mnext
	mov	x0,WS_OP_WIN_MOVE
	mov	x1,x22
	mov	x2,x20
	mov	x3,x21
	bl	wscall
.mnext:	adr	x19,inbuf			; (longueur de MOVE)
	mov	x22,9
.next:	mov	x0,x22
	bl	consume
	b	.m
.bad:	adr	x0,m_bad
	bl	puts
	adr	x0,dead
	mov	x1,1
	str	x1,[x0]
.done:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = id -> x0 = sa place (0 : aucune)
find_slot:
	adr	x1,slots
	mov	x2,MAXWIN
.s:	ldr	x3,[x1,SL_ID]
	cmp	x3,x0
	b.eq	.got
	add	x1,x1,SL_SIZE
	subs	x2,x2,1
	b.ne	.s
	mov	x0,0
	ret
.got:	mov	x0,x1
	ret

; x0 = id : RAISE du client (sa fenetre a pris le focus sur le PC)
do_raise:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	find_slot
	cbz	x0,.out
	ldr	x1,[x0,SL_FLAGS]
	tst	x1,WF_BACK or WF_TOP
	b.ne	.out
	movl	x1,DESK
	cmp	x19,x1
	b.eq	.out
	mov	x0,WS_OP_RAISE
	mov	x1,x19
	bl	wscall
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Entrees vers HIDManager
; ---------------------------------------------------------------------------

; x0 = id, x1 = x, x2 = y (dans le contenu ; le bureau : l'ecran), x3 =
; boutons (1 gauche, 2 droit, 4 milieu : comme HIDManager), x4 = molette
pointer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	and	x22,x3,7
	mov	x23,x4
	movl	x0,DESK
	cmp	x19,x0
	b.eq	.wheel
	mov	x0,x19
	bl	find_slot
	cbz	x0,.out
	mov	x24,x0
	ldr	x1,[x24,SL_X]			; a sa place sur l'ecran
	add	x20,x20,x1
	ldr	x1,[x24,SL_Y]
	add	x21,x21,x1
	cbz	x22,.wheel			; un clic : la fenetre passe devant
	adr	x0,btn				; d'abord (sinon une autre, devant sur
	ldr	x0,[x0]				; le Pi, recevrait le clic)
	cbnz	x0,.wheel
	ldr	x0,[x24,SL_STATE]
	tbnz	x0,0,.wheel			; (deja au clavier)
	ldr	x0,[x24,SL_FLAGS]
	tst	x0,WF_BACK or WF_TOP or WF_NOFOCUS
	b.ne	.wheel
	mov	x0,WS_OP_RAISE
	mov	x1,x19
	bl	wscall
.wheel:	cbz	x23,.pos
	mov	x0,EV_WHEEL
	mov	x1,x23
	mov	x2,0
	mov	x3,0
	bl	ring_push
.pos:	adr	x3,scr_w			; 0..32767
	ldp	x4,x5,[x3]
	sub	x4,x4,1
	sub	x5,x5,1
	cmp	x20,0
	csel	x20,xzr,x20,lt
	cmp	x20,x4
	csel	x20,x4,x20,gt
	cmp	x21,0
	csel	x21,xzr,x21,lt
	cmp	x21,x5
	csel	x21,x5,x21,gt
	movl	x6,32767
	mul	x1,x20,x6
	udiv	x1,x1,x4
	mul	x2,x21,x6
	udiv	x2,x2,x5
	adr	x0,ptr_pos
	stp	x1,x2,[x0]
	mov	x3,x22
	mov	x0,EV_MOUSE_ABS
	bl	ring_push
	adr	x0,btn
	str	x22,[x0]
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = appui (1) / relache (0), w1 = keysym X11 d'une touche speciale (les
; caracteres arrivent par CHAR)
key_sym:
	prologue
	adr	x2,keysyms			; keysym, code HID, bit de modificateur
.s:	ldr	w3,[x2]
	cbz	w3,.out
	cmp	w3,w1
	b.eq	.found
	add	x2,x2,12
	b	.s
.found:	ldr	w1,[x2,4]			; code HID
	ldr	w3,[x2,8]			; modificateur
	cbz	w3,.push
	adr	x4,mods
	ldr	x5,[x4]
	bic	x6,x5,x3
	orr	x7,x5,x3
	cmp	x0,0
	csel	x5,x6,x7,eq
	str	x5,[x4]
.push:	mov	x2,x0
	adr	x3,mods
	ldr	x3,[x3]
	mov	x0,EV_KEY			; caractere : calcule par HIDManager
	bl	ring_push
.out:	epilogue

; x0 = caractere (Latin-1, la disposition du PC appliquee) : appui et relache
; (Ctrl + lettre arrive en 1..26 : la lettre, sans caractere, Ctrl tenu)
char_in:
	prologue
	cbz	x0,.out
	cmp	x0,255
	b.hi	.out
	mov	x1,0				; code HID
	mov	x2,x0				; caractere
	cmp	x0,32
	b.hs	.text
	cmp	x0,26
	b.hi	.out
	add	x1,x0,0x04-1			; Ctrl + lettre
	mov	x2,0
	b	.push
.text:	orr	w3,w2,0x20			; lettres, chiffres, espace
	sub	w4,w3,'a'
	cmp	w4,25
	b.hi	.digit
	cmp	w2,'z'
	b.hi	.digit
	add	w1,w4,0x04
	b	.push
.digit:	sub	w4,w2,'1'
	cmp	w4,8
	b.hi	.zero
	add	w1,w4,0x1E
	b	.push
.zero:	cmp	w2,'0'
	b.ne	.space
	mov	w1,0x27
	b	.push
.space:	cmp	w2,' '
	b.ne	.push
	mov	w1,0x2C
.push:	adr	x3,mods				; caractere deja traduit (bit 31)
	ldr	x3,[x3]
	mov	x4,1
	orr	x3,x3,x4,lsl 31
	mov	x0,EV_KEY
	orr	x0,x0,x2,lsl 32
	stp	x0,x1,[sp,-16]!
	str	x3,[sp,-16]!
	mov	x2,1				; appui
	bl	ring_push
	ldr	x3,[sp],16
	ldp	x0,x1,[sp],16
	mov	x2,0				; relache
	bl	ring_push
.out:	epilogue

; fin de session : aucun bouton ni modificateur laisse tenu
release_all:
	prologue
	adr	x0,btn
	ldr	x1,[x0]
	cbz	x1,.mods
	str	xzr,[x0]
	adr	x0,ptr_pos
	ldp	x1,x2,[x0]
	mov	x3,0
	mov	x0,EV_MOUSE_ABS
	bl	ring_push
.mods:	adr	x0,mods
	str	xzr,[x0]
	epilogue

; x0 = type, x1 = a, x2 = b, x3 = c : un evenement dans l'anneau de
; HIDManager (perdu s'il est plein)
ring_push:
	adr	x9,anneau
	ldr	x9,[x9]
	cbz	x9,.plein
	ldp	x10,x11,[x9]			; tete (ici), queue (HIDManager)
	sub	x12,x10,x11
	cmp	x12,ANNEAU_N
	b.hs	.plein
	and	x12,x10,ANNEAU_N-1
	add	x12,x9,x12,lsl 5
	add	x12,x12,ANNEAU_EV
	stp	x0,x1,[x12]
	stp	x2,x3,[x12,16]
	dmb	ish				; l'evenement avant la tete
	add	x10,x10,1
	str	x10,[x9]
.plein:	ret

; ---------------------------------------------------------------------------
;  Une ronde
; ---------------------------------------------------------------------------

; le tampon d'Elegant (WS_OP_WIN_READ y recopie) : mappe une fois
rbuf_map:
	prologue
	adr	x0,rbuf
	ldr	x0,[x0]
	cbnz	x0,.out
	mov	x0,WS_OP_WIN_READ
	movl	x1,DESK
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	wscall
	tbnz	x0,63,.out
	adr	x9,msg
	ldr	x1,[x9,WM_C]
	adr	x2,rbuf_size
	str	x1,[x2]
	ldr	x0,[x9,WM_D]
	adr	x1,rbuf_num
	str	x0,[x1]
	sys	SYS_BUF_MAP
	tbnz	x0,63,.out
	adr	x1,rbuf
	str	x0,[x1]
.out:	epilogue

; la liste des fenetres -> wl (WE_* ; le bureau d'abord s'il est demande),
; wl_n ; x0 < 0 si Elegant ne repond pas
get_list:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,0				; entrees
	adr	x0,opt_desktop
	ldr	x0,[x0]
	cbz	x0,.list
	adr	x0,wl				; le bureau : l'ecran entier
	stp	xzr,xzr,[x0]
	stp	xzr,xzr,[x0,16]
	stp	xzr,xzr,[x0,32]
	stp	xzr,xzr,[x0,48]
	movl	w1,DESK
	str	w1,[x0,WE_ID]
	adr	x1,scr_w
	ldp	x1,x2,[x1]
	strh	w1,[x0,WE_W]
	strh	w2,[x0,WE_H]
	mov	x19,1
.list:	mov	x20,0				; premiere
.l:	mov	x0,WS_OP_WIN_LIST
	mov	x1,x20
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	wscall
	tbnz	x0,63,.err
	sub	x21,x0,x20			; entrees dans ce message
	cmp	x21,0
	b.le	.end
	cmp	x21,WE_PER_MSG
	mov	x1,WE_PER_MSG
	csel	x21,x1,x21,gt
	mov	x1,MAXWIN
	sub	x1,x1,x19
	cmp	x21,x1
	csel	x21,x1,x21,gt
	cmp	x21,0
	b.le	.end
	mov	x22,x0				; nombre total
	adr	x0,wl
	lsl	x1,x19,6
	add	x0,x0,x1
	adr	x1,msg+WM_DATA
	lsl	x2,x21,6
	bl	copy_bytes
	add	x19,x19,x21
	add	x20,x20,x21
	cmp	x20,x22
	b.lo	.l
.end:	adr	x0,opt_desktop			; generation du bureau : celles des
	ldr	x0,[x0]				; fenetres du fond, leur place
	cbz	x0,.n
	mov	x2,1
	mov	x3,1
	adr	x4,wl
	mov	x6,31
.g:	cmp	x3,x19
	b.hs	.gset
	add	x5,x4,x3,lsl 6
	add	x3,x3,1
	ldr	w7,[x5,WE_FLAGS]
	tbz	w7,WFB_BACK_R,.g
	ldr	w7,[x5,WE_ID]
	madd	x2,x2,x6,x7
	ldr	w7,[x5,WE_GEN]
	madd	x2,x2,x6,x7
	ldr	x7,[x5,WE_X]			; x, y, l, h
	madd	x2,x2,x6,x7
	ldr	w7,[x5,WE_STATE]
	madd	x2,x2,x6,x7
	b	.g
.gset:	str	w2,[x4,WE_GEN]
.n:	adr	x0,wl_n
	str	x19,[x0]
	mov	x0,0
	b	.out
.err:	adr	x1,wl_n				; (Elegant absent : aucune)
	str	xzr,[x1]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si une ronde est partie (... END), 0 si rien n'a change (rien
; envoye : le credit reste)
round_send:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	adr	x0,roundmsgs
	str	xzr,[x0]
	adr	x0,tmp				; nouvelle taille de l'ecran : SCREEN
	sys	SYS_SCREEN_INFO
	adr	x0,tmp
	ldp	x1,x2,[x0]
	adr	x3,scr_w
	ldp	x4,x5,[x3]
	cmp	x1,x4
	ccmp	x2,x5,0,eq
	b.eq	.list
	stp	x1,x2,[x3]
	adr	x0,rbuf				; le tampon d'Elegant est a refaire
	str	xzr,[x0]
	adr	x0,rbuf_num
	ldr	x0,[x0]
	sys	SYS_BUF_FREE
	mov	x0,8
	mov	x1,4
	bl	msg_head
	adr	x0,scr_w
	ldr	x0,[x0]
	bl	put16
	adr	x0,scr_h
	ldr	x0,[x0]
	bl	put16
.list:	bl	rbuf_map
	bl	get_list
	adr	x0,slots			; toutes pas vues
	mov	x1,MAXWIN
.d:	str	xzr,[x0,SL_ALIVE]
	add	x0,x0,SL_SIZE
	subs	x1,x1,1
	b.ne	.d
	adr	x0,norder_n
	str	xzr,[x0]
	adr	x0,wl_n
	ldr	x0,[x0]
	tbnz	x0,63,.gone
	mov	x19,0				; entree
.e:	adr	x0,wl_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.gone
	adr	x20,wl
	add	x20,x20,x19,lsl 6
	add	x19,x19,1
	ldr	w0,[x20,WE_FLAGS]
	tbnz	w0,WFB_BACK_R,.e		; (dans le bureau)
	ldrsh	x21,[x20,WE_X]
	ldrsh	x22,[x20,WE_Y]
	ldrh	w23,[x20,WE_W]
	ldrh	w24,[x20,WE_H]
	cbz	x23,.e
	cbz	x24,.e
	add	x0,x21,x23			; hors de l'ecran : pas dite
	cmp	x0,0
	b.le	.e
	add	x0,x22,x24
	cmp	x0,0
	b.le	.e
	adr	x0,scr_w
	ldp	x1,x2,[x0]
	cmp	x21,x1
	b.ge	.e
	cmp	x22,x2
	b.ge	.e
	ldr	w0,[x20,WE_ID]
	bl	find_slot
	cbnz	x0,.have
	mov	x0,0				; une place libre
	bl	find_slot
	cbz	x0,.e
	ldr	w1,[x20,WE_ID]
	str	x1,[x0,SL_ID]
	str	xzr,[x0,SL_SENT]
	mov	x1,1
	str	x1,[x0,SL_STALE]
.have:	mov	x25,x0
	mov	x1,1
	str	x1,[x25,SL_ALIVE]
	ldr	x26,[x25,SL_SENT]		; a dire (WIN) ?
	eor	x26,x26,1
	ldr	x0,[x25,SL_X]
	cmp	x0,x21
	cinc	x26,x26,ne
	ldr	x0,[x25,SL_Y]
	cmp	x0,x22
	cinc	x26,x26,ne
	ldr	x0,[x25,SL_W]
	cmp	x0,x23
	cinc	x26,x26,ne
	ldr	x0,[x25,SL_H]
	cmp	x0,x24
	cinc	x26,x26,ne
	ldr	w1,[x20,WE_FLAGS]
	ldr	x0,[x25,SL_FLAGS]
	cmp	x0,x1
	cinc	x26,x26,ne
	ldr	w1,[x20,WE_STATE]
	ldr	x0,[x25,SL_STATE]
	cmp	x0,x1
	cinc	x26,x26,ne
	mov	x2,0				; titre (32 octets)
.t:	add	x3,x20,WE_TITLE
	ldr	x0,[x3,x2]
	add	x3,x25,SL_TITLE
	ldr	x1,[x3,x2]
	cmp	x0,x1
	cinc	x26,x26,ne
	str	x0,[x3,x2]
	add	x2,x2,8
	cmp	x2,32
	b.lo	.t
	str	x21,[x25,SL_X]
	str	x22,[x25,SL_Y]
	str	x23,[x25,SL_W]
	str	x24,[x25,SL_H]
	ldr	w0,[x20,WE_FLAGS]
	str	x0,[x25,SL_FLAGS]
	ldr	w0,[x20,WE_STATE]
	str	x0,[x25,SL_STATE]
	cbz	x26,.said
	mov	x0,x25
	bl	send_win
.said:	mov	x0,1
	str	x0,[x25,SL_SENT]
	adr	x0,norder_n			; ordre d'affichage
	ldr	x1,[x0]
	add	x2,x1,1
	str	x2,[x0]
	adr	x0,norder
	ldr	w2,[x20,WE_ID]
	str	x2,[x0,x1,lsl 3]
	ldr	x0,[x25,SL_STATE]		; reduite : pixels plus tard
	tbz	x0,1,.shown
	mov	x0,1
	str	x0,[x25,SL_STALE]
	b	.e
.shown:	ldr	x0,[x25,SL_FLAGS]		; le cadre
	tbnz	x0,WFB_FRAMELESS_R,.content
	ldr	x0,[x25,SL_ID]
	movl	x1,DESK
	cmp	x0,x1
	b.eq	.content
	adr	x0,opt_noframes
	ldr	x0,[x0]
	cbnz	x0,.content
	ldr	x0,[x25,SL_STALE]
	cbnz	x0,.chrome
	ldr	w0,[x20,WE_CGEN]
	ldr	x1,[x25,SL_CGEN]
	cmp	x0,x1
	b.eq	.content
.chrome:
	mov	x0,x25
	bl	send_chrome
.content:
	ldr	x1,[x25,SL_STALE]
	cbnz	x1,.read
	ldr	w0,[x20,WE_GEN]
	ldr	x2,[x25,SL_GEN]
	cmp	x0,x2
	b.eq	.gens
.read:	mov	x0,x25
	bl	send_content
.gens:	ldr	w0,[x20,WE_GEN]
	str	x0,[x25,SL_GEN]
	ldr	w0,[x20,WE_CGEN]
	str	x0,[x25,SL_CGEN]
	str	xzr,[x25,SL_STALE]
	adr	x0,dead
	ldr	x0,[x0]
	cbz	x0,.e
	b	.out0
.gone:	adr	x19,slots			; GONE : celles qui ne sont plus la
	mov	x20,MAXWIN
.g:	ldr	x0,[x19,SL_ID]
	cbz	x0,.gn
	ldr	x0,[x19,SL_ALIVE]
	cbnz	x0,.gn
	ldr	x0,[x19,SL_SENT]
	cbz	x0,.gfree
	mov	x0,2
	mov	x1,4
	bl	msg_head
	ldr	x0,[x19,SL_ID]
	bl	put32
.gfree:	str	xzr,[x19,SL_ID]
	str	xzr,[x19,SL_SENT]
.gn:	add	x19,x19,SL_SIZE
	subs	x20,x20,1
	b.ne	.g
	adr	x0,norder_n			; ZORDER s'il a change
	ldr	x19,[x0]
	adr	x0,order_n
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.zorder
	mov	x1,0
	adr	x2,order
	adr	x3,norder
.zc:	cmp	x1,x19
	b.hs	.end
	ldr	x4,[x2,x1,lsl 3]
	ldr	x5,[x3,x1,lsl 3]
	add	x1,x1,1
	cmp	x4,x5
	b.eq	.zc
.zorder:
	mov	x0,3
	lsl	x1,x19,2
	add	x1,x1,2
	bl	msg_head
	mov	x0,x19
	bl	put16
	adr	x0,order_n
	str	x19,[x0]
	mov	x20,0
.zp:	cmp	x20,x19
	b.hs	.end
	adr	x0,norder
	ldr	x0,[x0,x20,lsl 3]
	adr	x1,order
	str	x0,[x1,x20,lsl 3]
	bl	put32
	add	x20,x20,1
	b	.zp
.end:	adr	x0,roundmsgs
	ldr	x0,[x0]
	cbnz	x0,.send
.out0:	adr	x0,outlen			; rien n'a change : rien
	str	xzr,[x0]
	mov	x0,0
	b	.out
.send:	mov	x0,5				; END
	mov	x1,0
	bl	msg_head
	bl	flush_out
	mov	x0,1
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = place : son message WIN
send_win:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	add	x0,x19,SL_TITLE			; titre en Latin-1 (le client le lit ainsi)
	adr	x1,ttl
	bl	title_latin1
	mov	x20,x0
	mov	x0,1
	add	x1,x20,27
	bl	msg_head
	ldr	x0,[x19,SL_ID]
	bl	put32
	ldr	x0,[x19,SL_X]
	bl	put16
	ldr	x0,[x19,SL_Y]
	bl	put16
	ldr	x0,[x19,SL_W]
	bl	put16
	ldr	x0,[x19,SL_H]
	bl	put16
	mov	x21,0				; cadre : l + 2 bords, h + titre + bord
	mov	x22,0
	ldr	x0,[x19,SL_FLAGS]
	tbnz	x0,WFB_FRAMELESS_R,.f
	ldr	x0,[x19,SL_ID]
	movl	x1,DESK
	cmp	x0,x1
	b.eq	.f
	ldr	x21,[x19,SL_W]
	add	x21,x21,2*FRAME_BORDER
	ldr	x22,[x19,SL_H]
	add	x22,x22,FRAME_TITLE_H+FRAME_BORDER
.f:	mov	x0,x21
	bl	put16
	mov	x0,x22
	bl	put16
	mov	x0,0
	cbz	x21,.il
	mov	x0,FRAME_BORDER
.il:	bl	put16
	mov	x0,0
	cbz	x21,.it
	mov	x0,FRAME_TITLE_H
.it:	bl	put16
	ldr	x1,[x19,SL_FLAGS]		; drapeaux d'Onyx : sans cadre 1, fond 2,
	and	x0,x1,7				; dessus 4 (les memes bits), ALPHA 32
	tbz	x1,WFB_ALPHA_R,.fl
	orr	x0,x0,32
.fl:	bl	put32
	mov	x0,255				; opacite
	bl	put8
	ldr	x1,[x19,SL_STATE]		; etat : 1 au clavier, 4 reduite
	and	x0,x1,1
	tbz	x1,1,.st
	orr	x0,x0,4
.st:	bl	put8
	mov	x0,x20
	bl	put8
	adr	x0,ttl
	mov	x1,x20
	bl	put
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = titre UTF-8 (NUL), x1 = destination -> x0 = octets en Latin-1 (les
; caracteres au-dela : '?')
title_latin1:
	mov	x2,0				; octets ecrits
	mov	x3,0				; lus
.l:	cmp	x3,32
	b.hs	.end
	ldrb	w4,[x0,x3]
	cbz	w4,.end
	add	x3,x3,1
	cmp	w4,0x80
	b.lo	.put
	cmp	w4,0xC0
	b.lo	.l				; (suite isolee : ignoree)
	cmp	w4,0xC3
	b.hi	.other
	ldrb	w5,[x0,x3]			; deux octets : U+0080..U+00FF
	and	w6,w5,0xC0
	cmp	w6,0x80
	b.ne	.l				; (coupe : ignore)
	add	x3,x3,1
	and	w5,w5,0x3F
	and	w4,w4,3
	orr	w4,w5,w4,lsl 6
	b	.put
.other:	mov	w4,'?'
.skip:	cmp	x3,32
	b.hs	.put
	ldrb	w5,[x0,x3]
	and	w6,w5,0xC0
	cmp	w6,0x80
	b.ne	.put
	add	x3,x3,1
	b	.skip
.put:	strb	w4,[x1,x2]
	add	x2,x2,1
	b	.l
.end:	mov	x0,x2
	ret

; x0 = largeur (pixels) -> x0 = lignes par lecture (tampon d'Elegant, par
; multiples de 64)
band_rows:
	adr	x1,rbuf_size
	ldr	x1,[x1]
	lsr	x1,x1,2
	udiv	x0,x1,x0
	cmp	x0,TILE
	b.lo	.r
	and	x0,x0,not (TILE-1)
.r:	ret

; x0 = place : son contenu, les carres de 64 x 64 changes depuis la copie du
; client (tous si SL_STALE ou une nouvelle taille)
send_content:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,32			; [sp] lignes par lecture, [sp,8] pas
	mov	x19,x0
	ldr	x20,[x19,SL_STALE]		; tout
	ldr	x21,[x19,SL_W]
	ldr	x22,[x19,SL_H]
	adr	x0,rbuf
	ldr	x0,[x0]
	cbz	x0,.out
	mul	x23,x21,x22			; une copie de la bonne taille
	lsl	x23,x23,2
	ldr	x0,[x19,SL_BUF]
	cbz	x0,.alloc
	ldr	x0,[x19,SL_BW]
	cmp	x0,x21
	b.ne	.alloc
	ldr	x0,[x19,SL_BH]
	cmp	x0,x22
	b.eq	.ready
.alloc:	ldr	x0,[x19,SL_CAP]
	cmp	x0,x23
	b.hs	.sized
	mov	x0,x23				; (l'ancienne est perdue : malloc n'a
	bl	malloc				; pas de free)
	cbz	x0,.out
	str	x0,[x19,SL_BUF]
	str	x23,[x19,SL_CAP]
.sized:	str	x21,[x19,SL_BW]
	str	x22,[x19,SL_BH]
	mov	x20,1
.ready:	mov	x0,x21
	bl	band_rows
	cbz	x0,.out
	str	x0,[sp]
	lsl	x0,x21,2
	str	x0,[sp,8]
	mov	x23,0				; y0 : debut de la bande
.band:	cmp	x23,x22
	b.hs	.out
	ldr	x24,[sp]			; hauteur de la bande
	sub	x0,x22,x23
	cmp	x24,x0
	csel	x24,x0,x24,hi
	mov	x0,WS_OP_WIN_READ
	ldr	x1,[x19,SL_ID]
	adr	x2,opt_noframes			; (cadres natifs du PC : le contenu seul)
	ldr	x2,[x2]
	cmp	x2,0
	mov	x2,3
	csel	x2,x2,xzr,ne
	lsl	x3,x23,32
	orr	x4,x21,x24,lsl 32
	bl	wscall
	tbnz	x0,63,.out
	mov	x25,0				; ty : bande de carres dans la lecture
.strip:	cmp	x25,x24
	b.hs	.nband
	sub	x26,x24,x25			; th
	cmp	x26,TILE
	mov	x0,TILE
	csel	x26,x0,x26,hi
	mov	x27,-1				; debut d'une suite de carres changes
	mov	x28,0				; tx
.tile:	mov	x0,0				; change ?
	cmp	x28,x21
	b.hs	.have
	mov	x0,1
	cbnz	x20,.have
	ldr	x2,[sp,8]			; actuel : rbuf + ty * pas + tx * 4
	adr	x0,rbuf
	ldr	x0,[x0]
	madd	x0,x25,x2,x0
	add	x0,x0,x28,lsl 2
	ldr	x1,[x19,SL_BUF]			; copie : + (y0 + ty) * pas
	add	x3,x23,x25
	madd	x1,x3,x2,x1
	add	x1,x1,x28,lsl 2
	sub	x3,x21,x28			; tw
	cmp	x3,TILE
	mov	x4,TILE
	csel	x3,x4,x3,hi
	mov	x4,x26
	bl	tile_diff
.have:	cbz	x0,.same
	tbz	x27,63,.nt
	mov	x27,x28
	b	.nt
.same:	tbnz	x27,63,.nt
	cmp	x28,x21				; fin de la suite : un rectangle
	csel	x6,x21,x28,hs
	sub	x6,x6,x27			; largeur
	str	x6,[sp,16]
	ldr	x3,[sp,8]
	adr	x2,rbuf
	ldr	x2,[x2]
	madd	x2,x25,x3,x2
	add	x2,x2,x27,lsl 2
	ldr	x0,[x19,SL_ID]
	mov	x1,0
	ldr	x4,[x19,SL_FLAGS]
	tbz	x4,WFB_ALPHA_R,.op
	mov	x1,0x100			; (transparence : 32 bits)
.op:	mov	x4,x27
	add	x5,x23,x25
	mov	x7,x26
	bl	send_rect
	ldr	x2,[sp,8]			; la copie du client suit
	adr	x0,rbuf
	ldr	x0,[x0]
	madd	x1,x25,x2,x0
	add	x1,x1,x27,lsl 2
	ldr	x0,[x19,SL_BUF]
	add	x3,x23,x25
	madd	x0,x3,x2,x0
	add	x0,x0,x27,lsl 2
	mov	x3,x26
.cp:	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	ldr	x2,[sp,48]			; largeur * 4
	lsl	x2,x2,2
	bl	copy_bytes
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	add	x0,x0,x2
	add	x1,x1,x2
	subs	x3,x3,1
	b.ne	.cp
	mov	x27,-1
.nt:	add	x28,x28,TILE
	add	x0,x21,TILE
	cmp	x28,x0
	b.lo	.tile
	add	x25,x25,TILE
	b	.strip
.nband:	add	x23,x23,x24
	adr	x0,dead
	ldr	x0,[x0]
	cbz	x0,.band
.out:	add	sp,sp,32
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = actuel, x1 = copie, x2 = pas (octets), x3 = largeur, x4 = hauteur
; (pixels) -> x0 = 1 si le carre differe
tile_diff:
.r:	mov	x5,x0
	mov	x6,x1
	mov	x7,x3
.p:	ldr	w8,[x5],4
	ldr	w9,[x6],4
	cmp	w8,w9
	b.ne	.yes
	subs	x7,x7,1
	b.ne	.p
	add	x0,x0,x2
	add	x1,x1,x2
	subs	x4,x4,1
	b.ne	.r
	mov	x0,0
	ret
.yes:	mov	x0,1
	ret

; x0 = place : son cadre, les deux copies (active, inactive), en entier,
; en 32 bits (les coins, l'ombre : transparence)
send_chrome:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	adr	x0,rbuf
	ldr	x0,[x0]
	cbz	x0,.out
	ldr	x21,[x19,SL_W]			; l + 2 bords, h + titre + bord
	add	x21,x21,2*FRAME_BORDER
	ldr	x22,[x19,SL_H]
	add	x22,x22,FRAME_TITLE_H+FRAME_BORDER
	mov	x0,x21
	bl	band_rows
	cbz	x0,.out
	mov	x26,x0
	mov	x20,1				; partie
.part:	mov	x23,0				; y0
.band:	cmp	x23,x22
	b.hs	.npart
	sub	x24,x22,x23
	cmp	x24,x26
	csel	x24,x26,x24,hi
	mov	x0,WS_OP_WIN_READ
	ldr	x1,[x19,SL_ID]
	mov	x2,x20
	lsl	x3,x23,32
	orr	x4,x21,x24,lsl 32
	bl	wscall
	tbnz	x0,63,.out
	mov	x25,0				; ty
.strip:	cmp	x25,x24
	b.hs	.nband
	sub	x7,x24,x25
	cmp	x7,TILE
	mov	x0,TILE
	csel	x7,x0,x7,hi
	lsl	x3,x21,2			; pas
	adr	x2,rbuf
	ldr	x2,[x2]
	madd	x2,x25,x3,x2
	ldr	x0,[x19,SL_ID]
	orr	x1,x20,0x100			; (32 bits)
	mov	x4,0
	add	x5,x23,x25
	mov	x6,x21
	bl	send_rect
	add	x25,x25,TILE
	b	.strip
.nband:	add	x23,x23,x24
	adr	x0,dead
	ldr	x0,[x0]
	cbnz	x0,.out
	b	.band
.npart:	add	x20,x20,1
	cmp	x20,2
	b.ls	.part
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = id, x1 = partie (bit 8 : en 32 bits quoi qu'il arrive), x2 = pixels
; (coin du rectangle), x3 = pas (octets), x4 = x, x5 = y, x6 = largeur,
; x7 = hauteur : un message PIXELS (LZ4 s'il est plus court)
send_rect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
	mov	x25,x6
	mov	x26,x7
	mov	x27,0				; 16 bits ?
	tbnz	x20,8,.b
	adr	x0,opt_16
	ldr	x27,[x0]
.b:	mul	x28,x25,x26			; octets
	lsl	x28,x28,1
	cbnz	x27,.b2
	lsl	x28,x28,1
.b2:	movl	x0,PACK_MAX
	cmp	x28,x0
	b.hi	.out
	adr	x0,packbuf			; les lignes bout a bout
	ldr	x10,[x0]
	mov	x11,x21
	mov	x12,x26
.row:	cbz	x12,.packed
	cbnz	x27,.r16
	mov	x0,x10
	mov	x1,x11
	lsl	x2,x25,2
	stp	x10,x11,[sp,-16]!
	str	x12,[sp,-16]!
	bl	copy_bytes
	ldr	x12,[sp],16
	ldp	x10,x11,[sp],16
	add	x10,x10,x25,lsl 2
	b	.nrow
.r16:	mov	x1,x11				; RGB565
	mov	x2,x25
.px:	ldr	w3,[x1],4
	lsr	w4,w3,8
	and	w4,w4,0xF800
	lsr	w5,w3,5
	and	w5,w5,0x07E0
	orr	w4,w4,w5
	ubfx	w5,w3,3,5
	orr	w4,w4,w5
	strh	w4,[x10],2
	subs	x2,x2,1
	b.ne	.px
.nrow:	add	x11,x11,x22
	sub	x12,x12,1
	b	.row
.packed:
	adr	x0,packbuf
	ldr	x0,[x0]
	mov	x1,x28
	adr	x2,lzbuf
	ldr	x2,[x2]
	bl	lz4_compress
	mov	x21,x0				; octets compresses
	cmp	x21,x28
	cset	x27,lo				; (x27 : lz4)
	adr	x0,opt_16			; (16 bits ?) -> profondeur
	ldr	x0,[x0]
	mov	x1,32
	tbnz	x20,8,.depth
	cbz	x0,.depth
	mov	x1,16
.depth:	str	x1,[sp,-16]!
	mov	x1,x28
	cbz	x27,.len
	mov	x1,x21
.len:	add	x1,x1,15
	mov	x0,4
	bl	msg_head
	mov	x0,x19
	bl	put32
	and	x0,x20,0xFF
	bl	put8
	ldr	x0,[sp],16
	bl	put8
	mov	x0,x27
	bl	put8
	mov	x0,x23
	bl	put16
	mov	x0,x24
	bl	put16
	mov	x0,x25
	bl	put16
	mov	x0,x26
	bl	put16
	adr	x0,packbuf
	ldr	x0,[x0]
	mov	x1,x28
	cbz	x27,.data
	adr	x0,lzbuf
	ldr	x0,[x0]
	mov	x1,x21
.data:	bl	put
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  LZ4 (format bloc : suites de litteraux + une copie d'au moins 4 octets a
;  64 Ko au plus en arriere), le codeur simple de rdpd.c d'Onyx
;  x0 = source, x1 = octets, x2 = destination (place : n + n / 255 + 16)
;  -> x0 = octets compresses
; ---------------------------------------------------------------------------
lz4_compress:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0				; source
	mov	x20,x1				; n
	mov	x21,x2				; debut de la destination
	mov	x22,x2				; op
	adr	x0,htab				; table : -1 partout
	ldr	x23,[x0]
	mov	x0,x23
	mov	x1,HT_BYTES
	mov	x2,-1
.ht:	stp	x2,x2,[x0],16
	subs	x1,x1,16
	b.ne	.ht
	mov	x24,0				; ip
	mov	x25,0				; anchor
	mov	x26,0				; miss
	sub	x27,x20,12			; ip < n - 12
	movl	w28,0x9E3779B1
.loop:	cmp	x24,x27
	b.ge	.last
	ldr	w0,[x19,x24]			; seq
	mul	w1,w0,w28
	lsr	w1,w1,20			; h
	ldr	w2,[x23,x1,lsl 2]		; ref
	str	w24,[x23,x1,lsl 2]
	cmn	w2,1
	b.eq	.miss
	sub	x3,x24,x2			; distance <= 65535
	lsr	x4,x3,16
	cbnz	x4,.miss
	ldr	w4,[x19,x2]
	cmp	w4,w0
	b.ne	.miss
	mov	x26,0
	mov	x5,4				; ml
	sub	x6,x20,5			; lim
	add	x7,x19,x2			; ref
	add	x8,x19,x24			; ip
.ext8:	add	x9,x24,x5
	add	x9,x9,8
	cmp	x9,x6
	b.gt	.ext1
	ldr	x10,[x7,x5]
	ldr	x11,[x8,x5]
	eor	x10,x10,x11
	cbnz	x10,.diff
	add	x5,x5,8
	b	.ext8
.diff:	rbit	x10,x10
	clz	x10,x10
	add	x5,x5,x10,lsr 3
	b	.emit
.ext1:	add	x9,x24,x5
	cmp	x9,x6
	b.ge	.emit
	ldrb	w10,[x7,x5]
	ldrb	w11,[x8,x5]
	cmp	w10,w11
	b.ne	.emit
	add	x5,x5,1
	b	.ext1
.emit:	sub	x9,x24,x25			; litteraux
	sub	x10,x5,4			; ml - 4
	mov	x11,x22				; le jeton
	add	x22,x22,1
	cmp	x9,15
	mov	x12,15
	csel	x12,x9,x12,lo
	cmp	x10,15
	mov	x13,15
	csel	x13,x10,x13,lo
	orr	x12,x13,x12,lsl 4
	strb	w12,[x11]
	cmp	x9,15
	b.lo	.lcopy
	sub	x12,x9,15
.ll:	cmp	x12,255
	b.lo	.lle
	mov	w13,255
	strb	w13,[x22],1
	sub	x12,x12,255
	b	.ll
.lle:	strb	w12,[x22],1
.lcopy:	add	x12,x19,x25
	mov	x13,x9
.lc8:	cmp	x13,8
	b.lo	.lc1
	ldr	x14,[x12],8
	str	x14,[x22],8
	sub	x13,x13,8
	b	.lc8
.lc1:	cbz	x13,.off
	ldrb	w14,[x12],1
	strb	w14,[x22],1
	sub	x13,x13,1
	b	.lc1
.off:	sub	x12,x24,x2			; distance
	strb	w12,[x22],1
	lsr	x12,x12,8
	strb	w12,[x22],1
	cmp	x10,15
	b.lo	.adv
	sub	x12,x10,15
.ml:	cmp	x12,255
	b.lo	.mle
	mov	w13,255
	strb	w13,[x22],1
	sub	x12,x12,255
	b	.ml
.mle:	strb	w12,[x22],1
.adv:	add	x24,x24,x5
	mov	x25,x24
	b	.loop
.miss:	lsr	x0,x26,5			; ip += 1 + (miss++ >> 5)
	add	x26,x26,1
	add	x24,x24,x0
	add	x24,x24,1
	b	.loop
.last:	sub	x9,x20,x25			; les derniers litteraux
	cmp	x9,15
	mov	x12,15
	csel	x12,x9,x12,lo
	lsl	x12,x12,4
	strb	w12,[x22],1
	cmp	x9,15
	b.lo	.lcopy2
	sub	x12,x9,15
.ll2:	cmp	x12,255
	b.lo	.lle2
	mov	w13,255
	strb	w13,[x22],1
	sub	x12,x12,255
	b	.ll2
.lle2:	strb	w12,[x22],1
.lcopy2:
	add	x12,x19,x25
	mov	x13,x9
.l2:	cbz	x13,.done
	ldrb	w14,[x12],1
	strb	w14,[x22],1
	sub	x13,x13,1
	b	.l2
.done:	sub	x0,x22,x21
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

WFB_BACK_R	= 1			; bits de WF_* (comme elegant/core.inc)
WFB_FRAMELESS_R	= 0
WFB_ALPHA_R	= 8

		align 8
s_hid		db 'hid',0
s_magic		db 'ONYXRDP2'
m_pret		db 'rdpd : en ',0xC3,0xA9,'coute sur le port 3390 (Korin Remote, mot de passe)',10,0
m_refus		db 'rdpd : mot de passe faux, client refus',0xC3,0xA9,10,0
m_aucun		db 'rdpd : aucun mot de passe (SD0:/etc/remote.txt), client refus',0xC3,0xA9,10,0
m_client	db 'rdpd : client',10,0
m_session	db 'rdpd : session ouverte',10,0
m_fin		db 'rdpd : client parti',10,0
m_closed	db 'rdpd : le client a ferm',0xC3,0xA9,' la connexion',10,0
m_silent	db 'rdpd : client silencieux, session finie',10,0
m_next		db 'rdpd : un nouveau client prend la place',10,0
m_bad		db 'rdpd : message inconnu, session finie',10,0
m_send		db 'rdpd : envoi impossible, session finie',10,0
m_nomem		db 'rdpd : m',0xC3,0xA9,'moire insuffisante',10,0
; longueur des messages du client (type compris), par type
msglen		db 0, 1, 11, 6, 5, 5, 5, 2, 5, 9
		align 4
; touches speciales : keysym, code HID, bit de modificateur (octet HID)
keysyms:
	dw	0xFF08, 0x2A, 0			; BackSpace
	dw	0xFF09, 0x2B, 0			; Tab
	dw	0xFF0D, 0x28, 0			; Return
	dw	0xFF8D, 0x58, 0			; KP_Enter
	dw	0xFF1B, 0x29, 0			; Escape
	dw	0xFF50, 0x4A, 0			; Home
	dw	0xFF51, 0x50, 0			; Left
	dw	0xFF52, 0x52, 0			; Up
	dw	0xFF53, 0x4F, 0			; Right
	dw	0xFF54, 0x51, 0			; Down
	dw	0xFF55, 0x4B, 0			; Page_Up
	dw	0xFF56, 0x4E, 0			; Page_Down
	dw	0xFF57, 0x4D, 0			; End
	dw	0xFF63, 0x49, 0			; Insert
	dw	0xFFFF, 0x4C, 0			; Delete
	dw	0xFFBE, 0x3A, 0			; F1..F12
	dw	0xFFBF, 0x3B, 0
	dw	0xFFC0, 0x3C, 0
	dw	0xFFC1, 0x3D, 0
	dw	0xFFC2, 0x3E, 0
	dw	0xFFC3, 0x3F, 0
	dw	0xFFC4, 0x40, 0
	dw	0xFFC5, 0x41, 0
	dw	0xFFC6, 0x42, 0
	dw	0xFFC7, 0x43, 0
	dw	0xFFC8, 0x44, 0
	dw	0xFFC9, 0x45, 0
	dw	0xFFE3, 0xE0, 0x01		; Control_L
	dw	0xFFE1, 0xE1, 0x02		; Shift_L
	dw	0xFFE9, 0xE2, 0x04		; Alt_L
	dw	0xFFE7, 0xE3, 0x08		; Meta_L
	dw	0xFFEB, 0xE3, 0x08		; Super_L
	dw	0xFFE4, 0xE4, 0x10		; Control_R
	dw	0xFFE2, 0xE5, 0x20		; Shift_R
	dw	0xFFEA, 0xE6, 0x40		; Alt_R
	dw	0xFE03, 0xE6, 0x40		; ISO_Level3_Shift (AltGr)
	dw	0
		align 8

segment readable writeable
kit_data
		align 8
anneau		dd 0				; anneau de HIDManager
outbuf		dd 0
outlen		dd 0
packbuf		dd 0
lzbuf		dd 0
htab		dd 0
rbuf		dd 0				; tampon d'Elegant (WIN_READ)
rbuf_size	dd 0
rbuf_num	dd 0
lsock		dd 0
sock		dd 0
next_sock	dd -1
dead		dd 0
scr_w		dd 0				; scr_w, scr_h, barre (SCREEN_INFO)
scr_h		dd 0
scr_bar		dd 0
opt_16		dd 0
opt_noframes	dd 0
opt_pipe	dd 0
opt_desktop	dd 0
credit		dd 0
rounds_max	dd 1
roundmsgs	dd 0
last_round	dd 0
round_wait	dd 0
last_rx		dd 0
cur_at		dd 0
cur_sent	dd 0
last_ping	dd 0
last_poll	dd 0
btn		dd 0
mods		dd 0
ptr_pos		dd 0, 0				; derniere place (0..32767)
in_n		dd 0
wl_n		dd 0
order_n		dd -1
norder_n	dd 0
order		rb MAXWIN * 8
norder		rb MAXWIN * 8
tmp		rb 64
ttl		rb 64
msg		rb WS_MSG
inbuf		rb IN_MAX
		align 8
wl		rb MAXWIN * WE_SIZE
slots		rb MAXWIN * SL_SIZE
defi		rb 16				; le defi du mot de passe (une session)
attendu		rb 32				; son HMAC
rp_data
