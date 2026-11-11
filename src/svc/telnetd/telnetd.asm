; telnetd - acces texte a distance (docs/RESEAU.md 5.1, etape 8), port 23,
; lance par init2 (SD0:/etc/services.txt). Comme telnetd d'Onyx : chaque
; connexion (8 au plus) a son interpreteur de commandes, SD0:/bin/bash s'il
; est sur la carte, sinon SD0:/bin/sh, sur un pseudo-terminal (service vterm,
; docs/VTERM.md) : telnetd tient le bout maitre (ce que tape le client ->
; maitre ; maitre -> le client), le shell l'esclave, dans un groupe a lui au
; premier plan du pty.
; Mode caractere : IAC WILL ECHO, WILL SGA ; la discipline de ligne de vterm
; fait l'echo et l'edition ; Ctrl+C (octet 3, ou IAC IP) devient SIGINT au
; programme au premier plan. NAWS (IAC DO NAWS) : la taille de la fenetre du
; client va au pty (VT_SWINSZ, SIGWINCH). Les autres negociations sont lues et
; ignorees (pas de TTYPE : TERM=xterm-256color, pose par sh). Un NOP telnet
; toutes les 5 min de silence : connexion perdue = session finie (le maitre
; ferme : SIGHUP, fin de l'entree de sh).
; Mot de passe (inc/remotepw.inc, SD0:/etc/remote.txt, commun a ftpd, vncd,
; rdpd ; choisi au premier demarrage ou dans Reglages) demande avant sh, dans
; le thread de la connexion : IAC WILL ECHO pendant la saisie (le client ne
; l'affiche pas), 3 essais, 2 s d'attente apres chaque faux. Aucun mot de passe
; defini : connexion refusee.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/netkit/netkit.inc'
include '../../inc/fs.inc'
include '../../inc/vterm.inc'

PORT		= 23
SESS_MAX	= 8
SS_USED		= 0			; 0 libre ; threads encore en vie
SS_SOCK		= 8
SS_MASTER	= 16			; bout maitre du pty de sh
SS_NAWS		= 24			; 1 : nouvelle taille (SS_ROWS, SS_COLS)
SS_PID		= 32
SS_STATE	= 40			; analyse telnet
SS_CR		= 48			; un CR vient de passer
SS_STK2		= 56			; pile de vers_client (allouee par session)
SS_SBN		= 64			; octets de la sous-negociation en cours
SS_ROWS		= 72
SS_COLS		= 80
SS_SB		= 88			; sous-negociation (16 o)
SS_SIZE		= 128			; (session : decalage de 7 pour piles)
SB_MAX		= 16
TSTACK		= 32768
IAC		= 255
T_SB		= 250
T_SE		= 240
T_IP		= 244
T_NOP		= 241
T_WILL		= 251
T_WONT		= 252
T_DO		= 253
T_DONT		= 254
TO_ECHO		= 1
TO_SGA		= 3
TO_NAWS		= 31
ESSAIS		= 3

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	kits_open
	bl	lang_get			; (dictionnaires lus ici : malloc, avant les threads)
.sock:	mov	x0,SOCK_TCP
	bl	net_socket
	tbnz	x0,63,.attente			; (netd pas encore la)
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
	cmn	x0,11				; (E_AGAIN : encore)
	b.eq	.loop
	tbnz	x0,63,.erreur
	bl	session
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

; x0 = socket de la connexion : une session (place, tubes, sh, threads)
session:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	adr	x20,sess			; une place libre
	mov	x0,SESS_MAX
.p:	ldar	x1,[x20,SS_USED]		; (libere par stlr en fin de thread)
	cbz	x1,.place
	add	x20,x20,SS_SIZE
	subs	x0,x0,1
	b.ne	.p
	adr	x0,m_plein			; plus de place
	bl	lang_tr
	mov	x20,x0
	bl	str_len
	mov	x2,x0
	mov	x0,x19
	mov	x1,x20
	bl	net_send
	mov	x0,x19
	adr	x1,s_crlf
	mov	x2,2
	bl	net_send
	mov	x0,x19
	bl	net_close
	b	.out
.place:	mov	x0,x20
	mov	x1,SS_SIZE
	bl	zero
	str	x19,[x20,SS_SOCK]
	mov	x0,1				; place prise (le thread auth)
	str	x0,[x20,SS_USED]
	adr	x0,sess				; les deux piles de cette place :
	sub	x0,x20,x0			;  allouees a sa premiere session,
	lsr	x0,x0,7 - 4			;  reprises par les suivantes (un
	adr	x21,piles			;  refus ne perd rien) ; malloc ici,
	add	x21,x21,x0			;  pas depuis les threads
	ldr	x0,[x21]
	cbnz	x0,.p1
	mov	x0,TSTACK
	bl	malloc
	cbz	x0,.ko
	str	x0,[x21]
.p1:	ldr	x0,[x21,8]
	cbnz	x0,.p2
	mov	x0,TSTACK
	bl	malloc
	cbz	x0,.ko
	str	x0,[x21,8]
.p2:	str	x0,[x20,SS_STK2]
	ldr	x21,[x21]
	add	x1,x21,TSTACK
	and	x1,x1,0xFFFFFFFFFFFFFFF0
	adr	x0,auth
	mov	x2,x20
	sys	SYS_THREAD_CREATE
	tbz	x0,63,.out
.ko:	mov	x0,x19
	bl	net_close
	str	xzr,[x20,SS_USED]
.out:	add	sp,sp,64
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (thread) x0 = session : le mot de passe, puis sh et les deux threads de copie
; (celui-ci devient vers_sh). Pile : recu [sp] 512, apres telnet_in [sp+512]
; 512, ligne [sp+1024] 128.
AU_PILE		= 1168
auth:
	mov	x19,x0
	sub	sp,sp,AU_PILE
	mov	x20,ESSAIS
	mov	x22,0				; octets en attente dans [sp+512] :
	mov	x23,0				;  x22 lu, x23 fin
	bl	rp_verifie_vide			; aucun mot de passe : refus
	cbnz	x0,.essai
	adr	x0,m_aucun
	bl	envoie_tr
	b	.refus
.essai:	adr	x0,m_mdp			; "Mot de passe : ", echo coupe
	bl	lang_tr
	bl	envoie_chaine
	mov	w0,T_WILL
	bl	envoie_echo
	mov	x21,0				; longueur de la ligne
.lit:	cmp	x22,x23
	b.lo	.octet
	ldr	x0,[x19,SS_SOCK]
	mov	x1,sp
	mov	x2,512
	movl	x3,120000			; 2 min pour taper
	bl	net_recv
	cmp	x0,0
	b.le	.refus				; parti (E_AGAIN compris)
	mov	x1,x0
	mov	x0,sp
	add	x2,sp,512
	mov	x3,x19
	bl	telnet_in
	mov	x22,0
	mov	x23,x0
	b	.lit
.octet:	add	x1,sp,512
	ldrb	w0,[x1,x22]
	add	x22,x22,1
	cmp	w0,10
	b.eq	.ligne
	cmp	w0,13				; (Entree : CR, telnet_in le garde)
	b.eq	.ligne
	cmp	w0,8				; (client en mode caractere)
	b.eq	.arr
	cmp	w0,127
	b.eq	.arr
	cmp	w0,32
	b.lo	.lit				; Ctrl+C, NUL... ignores
	cmp	x21,128
	b.hs	.long
	add	x1,sp,1024
	strb	w0,[x1,x21]
.long:	cmp	x21,200				; (trop long : jamais le bon)
	b.hs	.lit
	add	x21,x21,1
	b	.lit
.arr:	cbz	x21,.lit
	sub	x21,x21,1
	b	.lit
.ligne:	mov	w0,T_WONT			; echo rendu au client
	bl	envoie_echo
	ldr	x0,[x19,SS_SOCK]
	adr	x1,s_crlf
	mov	x2,2
	bl	net_send
	bl	rp_prendre			; (rp_pw commun aux threads)
	bl	rp_lire
	cbz	x0,.vide
	add	x0,sp,1024
	mov	x1,x21
	bl	rp_egal
	mov	x21,x0
	bl	rp_rendre
	add	x0,sp,1024			; la saisie effacee
	mov	x1,128
	bl	zero
	cbnz	x21,.bon
	adr	x0,m_faux
	bl	envoie_tr
	mov	x0,2000
	sys	SYS_SLEEP
	subs	x20,x20,1
	b.ne	.essai
	b	.refus
.vide:	bl	rp_rendre			; retire entre-temps
	adr	x0,m_aucun
	bl	envoie_tr
	b	.refus
.bon:	bl	lancer_sh
	tbnz	x0,63,.refus
	adr	x1,s_mode			; mode caractere : WILL ECHO, WILL SGA,
	mov	x2,9				; DO NAWS
	ldr	x0,[x19,SS_SOCK]
	bl	net_send
	cmp	x22,x23				; tape d'avance : a sh
	b.hs	.fils
	add	x0,sp,512
	add	x0,x0,x22
	sub	x1,x23,x22
	bl	vers_maitre
.fils:	mov	x0,2				; deux threads : celui-ci, vers_client
	str	x0,[x19,SS_USED]
	ldr	x1,[x19,SS_STK2]
	add	x1,x1,TSTACK
	and	x1,x1,0xFFFFFFFFFFFFFFF0
	adr	x0,vers_client
	mov	x2,x19
	sys	SYS_THREAD_CREATE
	tbnz	x0,63,.sans
	add	sp,sp,AU_PILE
	mov	x0,x19
	b	vers_sh
.sans:	bl	maitre_fermer			; (sh voit la fin de son entree)
	b	.refus
.refus:	ldr	x0,[x19,SS_SOCK]
	bl	net_close
	adr	x0,m_refus
	bl	puts
	add	x0,x19,SS_USED			; place libre
	stlr	xzr,[x0]
	sys	SYS_THREAD_EXIT

; x19 = session : un pty, SD0:/bin/bash (sinon SD0:/bin/sh) sur son esclave -> x0 = pid (< 0 :
; echec, rien d'ouvert)
lancer_sh:
	prologue
	stp	x20,x21,[sp,-16]!
	sub	sp,sp,MSG_TAILLE+32
	adr	x0,s_vterm
	mov	x1,5000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.ko
	adr	x1,vtc
	str	x0,[x1]
	mov	x0,VT_OPEN			; le pty
	str	x0,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	vt_appel
	tbnz	x0,63,.ko
	ldp	x0,x20,[sp,MSG_A]		; maitre, esclave
	str	x0,[x19,SS_MASTER]
	add	x0,sp,MSG_TAILLE		; sh : entree, sortie, erreur = l'esclave
	stp	x20,x20,[x0]
	str	x20,[x0,16]
	mov	x2,x0
	adr	x0,p_bash			; bash s'il est sur la carte (comme le
	mov	x1,0				; Terminal), sinon sh
	bl	f_spawn_stdio
	tbz	x0,63,.lance
	add	x2,sp,MSG_TAILLE
	adr	x0,p_sh
	mov	x1,0
	bl	f_spawn_stdio
.lance:	mov	x21,x0
	tbnz	x21,63,.rendre
	mov	x0,x21				; le shell : un groupe a lui, au premier
	mov	x1,x21				; plan du pty (comme login sous Unix :
	sys	SYS_SETPGID			; Ctrl+C va au shell et a ses commandes)
	mov	x0,VT_SPGRP
	ldr	x1,[x19,SS_MASTER]
	stp	x0,x1,[sp]
	str	x21,[sp,MSG_B]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	vt_appel
.rendre:
	mov	x0,VT_CLOSE			; notre copie de l'esclave : rendue
	stp	x0,x20,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	vt_appel
	tbnz	x21,63,.ko2
	str	x21,[x19,SS_PID]
	adr	x0,m_session			; "telnetd : session, shell pid N"
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,x21
	b	.out
.ko2:	bl	maitre_fermer
.ko:	mov	x0,-1
.out:	add	sp,sp,MSG_TAILLE+32
	ldp	x20,x21,[sp],16
	epilogue

; x0 = message : a vterm (canal vtc) -> resultat
vt_appel:
	mov	x1,x0
	adr	x0,vtc
	ldr	x0,[x0]
	mov	x8,SYS_SVC_CALL
	svc	0
	ret

; x19 = session : le bout maitre est rendu (sh lit la fin, SIGHUP) ; une fois
maitre_fermer:
	prologue
	sub	sp,sp,MSG_TAILLE
	add	x0,x19,SS_MASTER
.x:	ldaxr	x1,[x0]
	stlxr	w2,xzr,[x0]
	cbnz	w2,.x
	cbz	x1,.out
	mov	x0,VT_CLOSE
	stp	x0,x1,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	vt_appel
.out:	add	sp,sp,MSG_TAILLE
	epilogue

; x19 = session, x0 = octets, x1 = longueur : sur le bout maitre (jusqu'au bout,
; pty plein : 2 ms d'attente) -> x0 = 0, < 0 si le pty est parti
vers_maitre:
	prologue
	stp	x20,x21,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x20,x0
	mov	x21,x1
.m:	mov	x0,0
	cbz	x21,.out
	mov	x0,VT_WRITE
	ldr	x1,[x19,SS_MASTER]
	stp	x0,x1,[sp]
	mov	x2,x21
	mov	x0,FS_DONNEES
	cmp	x2,x0
	csel	x2,x0,x2,hi
	str	w2,[sp,MSG_LEN]
	add	x0,sp,MSG_DATA
	mov	x1,x20
	bl	mem_move
	mov	x0,sp
	bl	vt_appel
	tbnz	x0,63,.out
	cbz	x0,.plein
	add	x20,x20,x0
	sub	x21,x21,x0
	b	.m
.plein:	mov	x0,2
	sys	SYS_SLEEP
	b	.m
.out:	add	sp,sp,MSG_TAILLE
	ldp	x20,x21,[sp],16
	epilogue

; x19 = session : la taille donnee par NAWS au pty (VT_SWINSZ)
naws_pty:
	prologue
	sub	sp,sp,MSG_TAILLE
	str	xzr,[x19,SS_NAWS]
	mov	x0,VT_SWINSZ
	ldr	x1,[x19,SS_MASTER]
	stp	x0,x1,[sp]
	ldr	x0,[x19,SS_ROWS]
	ldr	x1,[x19,SS_COLS]
	stp	x0,x1,[sp,MSG_B]
	str	xzr,[sp,MSG_D]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	vt_appel
	add	sp,sp,MSG_TAILLE
	epilogue

; x19 = session, x0 = texte : traduit, puis CR LF
envoie_tr:
	prologue
	bl	lang_tr
	bl	envoie_chaine
	ldr	x0,[x19,SS_SOCK]
	adr	x1,s_crlf
	mov	x2,2
	bl	net_send
	epilogue

; x19 = session, x0 = chaine : envoyee
envoie_chaine:
	prologue
	str	x20,[sp,-16]!
	mov	x20,x0
	bl	str_len
	mov	x2,x0
	ldr	x0,[x19,SS_SOCK]
	mov	x1,x20
	bl	net_send
	ldr	x20,[sp],16
	epilogue

; x19 = session, w0 = T_WILL ou T_WONT : IAC w0 ECHO
envoie_echo:
	prologue
	sub	sp,sp,16
	mov	w1,IAC
	strb	w1,[sp]
	strb	w0,[sp,1]
	mov	w1,TO_ECHO
	strb	w1,[sp,2]
	ldr	x0,[x19,SS_SOCK]
	mov	x1,sp
	mov	x2,3
	bl	net_send
	add	sp,sp,16
	epilogue

; -> x0 = longueur du mot de passe (0 : aucun), sous le verrou
rp_verifie_vide:
	prologue
	bl	rp_prendre
	bl	rp_lire
	str	x0,[sp,-16]!
	bl	rp_rendre
	ldr	x0,[sp],16
	epilogue

; verrou de rp_pw / rp_tampon (rp_lire, rp_egal : un thread a la fois)
rp_prendre:
	prologue
.t:	adr	x0,rp_verrou
	ldaxr	x1,[x0]
	cbnz	x1,.att
	mov	x1,1
	stxr	w2,x1,[x0]
	cbnz	w2,.t
	epilogue
.att:	clrex
	sys	SYS_YIELD
	b	.t
rp_rendre:
	adr	x0,rp_verrou
	stlr	xzr,[x0]
	ret

; x0 = adresse, x1 = octets
zero:
	cbz	x1,.r
.z:	strb	wzr,[x0],1
	subs	x1,x1,1
	b.ne	.z
.r:	ret

; (thread) x0 = session : client -> entree de sh
vers_sh:
	mov	x19,x0
	sub	sp,sp,1040			; recu (512), pour sh (512)
.loop:	ldr	x0,[x19,SS_SOCK]
	mov	x1,sp
	mov	x2,512
	movl	x3,300000			; 5 min de silence : un NOP
	bl	net_recv
	cmn	x0,11
	b.eq	.nop
	cmp	x0,0
	b.le	.fin
	mov	x1,x0				; analyse telnet
	mov	x0,sp
	add	x2,sp,512
	mov	x3,x19
	bl	telnet_in
	mov	x20,x0
	ldr	x0,[x19,SS_NAWS]		; une taille donnee par le client
	cbz	x0,.don
	bl	naws_pty
.don:	cbz	x20,.loop
	add	x0,sp,512
	mov	x1,x20
	bl	vers_maitre
	tbnz	x0,63,.fin
	b	.loop
.nop:	mov	w0,IAC				; le client est-il la ?
	strb	w0,[sp]
	mov	w0,T_NOP
	strb	w0,[sp,1]
	ldr	x0,[x19,SS_SOCK]
	mov	x1,sp
	mov	x2,2
	bl	net_send
	tbz	x0,63,.loop
.fin:	bl	maitre_fermer			; fin : SIGHUP, fin de l'entree de sh
	mov	x0,x19				; (vers_client ferme la connexion)
	bl	fin_thread
	sys	SYS_THREAD_EXIT

; (thread) x0 = session : sortie de sh (bout maitre) -> client (IAC double ;
; CR LF : ONLCR de vterm)
vers_client:
	mov	x19,x0
	sub	sp,sp,MSG_TAILLE+1024
.loop:	mov	x0,VT_READ
	ldr	x1,[x19,SS_MASTER]
	cbz	x1,.fin
	stp	x0,x1,[sp]
	mov	x0,FS_DONNEES
	stp	x0,xzr,[sp,MSG_B]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	vt_appel
	cmn	x0,11				; E_AGAIN : redemander
	b.eq	.loop
	cmp	x0,0
	b.le	.fin				; sh a fini (ou le maitre est rendu)
	mov	x2,x0
	mov	x3,0				; lecture
	mov	x4,0				; ecriture
	add	x5,sp,MSG_TAILLE
.c:	cmp	x3,x2
	b.hs	.envoi
	add	x6,sp,MSG_DATA
	ldrb	w6,[x6,x3]
	add	x3,x3,1
	cmp	w6,IAC
	b.ne	.st
	strb	w6,[x5,x4]
	add	x4,x4,1
.st:	strb	w6,[x5,x4]
	add	x4,x4,1
	b	.c
.envoi:	ldr	x0,[x19,SS_SOCK]
	add	x1,sp,MSG_TAILLE
	mov	x2,x4
	bl	net_send
	b	.loop				; (client parti : vers_sh le voit)
.fin:	bl	maitre_fermer
	ldr	x0,[x19,SS_SOCK]		; fin de la connexion : vers_sh sort
	bl	net_close			; de net_recv
	adr	x0,m_finie
	bl	puts
	mov	x0,x19
	bl	fin_thread
	sys	SYS_THREAD_EXIT

; x0 = session : un de ses threads s'arrete (le dernier libere la place)
fin_thread:
	add	x0,x0,SS_USED
.t:	ldaxr	x1,[x0]
	sub	x1,x1,1
	stlxr	w2,x1,[x0]
	cbnz	w2,.t
	ret

; x0 = recu, x1 = octets, x2 = destination, x3 = session -> x0 = octets pour
; sh (negociations retirees, CR LF / CR NUL / CR -> LF, IAC IP -> 3)
telnet_in:
	mov	x4,0				; ecriture
	ldr	x5,[x3,SS_STATE]		; 0 donnees, 1 IAC, 2 option, 3 SB,
	ldr	x6,[x3,SS_CR]			; 4 IAC dans SB
.c:	cbz	x1,.fin
	ldrb	w7,[x0],1
	sub	x1,x1,1
	cmp	x5,1
	b.eq	.s_iac
	cmp	x5,2
	b.eq	.s_opt
	cmp	x5,3
	b.eq	.s_sb
	cmp	x5,4
	b.eq	.s_sbiac
	cmp	w7,IAC				; donnees
	b.ne	.data
	mov	x5,1
	b	.c
.data:	cbz	x6,.pas_cr
	mov	x6,0				; apres un CR : LF ou NUL avales
	cmp	w7,10
	b.eq	.c
	cbz	w7,.c
.pas_cr:
	cmp	w7,13				; CR (ICRNL du pty en fait LF ;
	b.ne	.put				;  un programme en brut voit CR)
	mov	x6,1
.put:	strb	w7,[x2,x4]
	add	x4,x4,1
	b	.c
.s_iac:	mov	x5,0
	cmp	w7,IAC				; IAC IAC : un octet 255
	b.eq	.put
	cmp	w7,T_IP				; interruption : Ctrl+C
	b.ne	.s_i2
	mov	w7,3
	b	.put
.s_i2:	cmp	w7,T_SB
	b.ne	.s_i3
	mov	x5,3
	str	xzr,[x3,SS_SBN]
	b	.c
.s_i3:	cmp	w7,T_WILL
	b.lo	.c				; NOP, GA, AYT...
	cmp	w7,T_DONT
	b.hi	.c
	mov	x5,2				; WILL / WONT / DO / DONT <option>
	b	.c
.s_opt:	mov	x5,0
	b	.c
.s_sb:	cmp	w7,IAC
	b.ne	.sbput
	mov	x5,4
	b	.c
.sbput:	ldr	x8,[x3,SS_SBN]			; garde (16 octets au plus)
	cmp	x8,SB_MAX
	b.hs	.c
	add	x9,x3,SS_SB
	strb	w7,[x9,x8]
	add	x8,x8,1
	str	x8,[x3,SS_SBN]
	b	.c
.s_sbiac:
	mov	x5,3
	cmp	w7,IAC				; IAC IAC : 255 dans la sous-negociation
	b.eq	.sbput
	cmp	w7,T_SE
	b.ne	.c
	mov	x5,0				; fin : NAWS ? largeur, hauteur (16 bits)
	ldr	x8,[x3,SS_SBN]
	cmp	x8,5
	b.lo	.c
	add	x9,x3,SS_SB
	ldrb	w10,[x9]
	cmp	w10,TO_NAWS
	b.ne	.c
	ldrb	w10,[x9,1]
	ldrb	w11,[x9,2]
	orr	w10,w11,w10,lsl 8
	str	x10,[x3,SS_COLS]
	ldrb	w10,[x9,3]
	ldrb	w11,[x9,4]
	orr	w10,w11,w10,lsl 8
	str	x10,[x3,SS_ROWS]
	mov	x10,1
	str	x10,[x3,SS_NAWS]
	b	.c
.fin:	str	x5,[x3,SS_STATE]
	str	x6,[x3,SS_CR]
	mov	x0,x4
	ret

include '../../inc/remotepw.inc'

p_bash		db 'SD0:/bin/bash',0
p_sh		db 'SD0:/bin/sh',0
s_vterm		db 'vterm',0
s_mode		db IAC,T_WILL,TO_ECHO, IAC,T_WILL,TO_SGA, IAC,T_DO,TO_NAWS
m_pret		db 'telnetd : en ',0xC3,0xA9,'coute sur le port 23',10,0
m_refus		db 'telnetd : connexion refus',0xC3,0xA9,'e',10,0
m_mdp		db 'Mot de passe : ',0
m_faux		db 'Mot de passe incorrect.',0
m_aucun		db 'Acc',0xC3,0xA8,'s ',0xC3,0xA0,' distance sans mot de passe : d',0xC3,0xA9,'finissez-le dans R',0xC3,0xA9,'glages.',0
m_session	db 'telnetd : session, shell pid ',0
m_finie		db 'telnetd : session finie',10,0
m_nl		db 10,0
m_plein		db 'telnetd : trop de sessions (8)',0
s_crlf		db 13,10
		align 8

segment readable writeable
kit_data
		align 8
sess		rb SESS_MAX * SS_SIZE
piles		rb SESS_MAX * 16		; par place : pile de auth/vers_sh, de vers_client
rp_verrou	dd 0
vtc		dd 0				; canal de vterm
rp_data
