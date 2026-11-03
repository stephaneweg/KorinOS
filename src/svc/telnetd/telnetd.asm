; telnetd - acces texte a distance (docs/RESEAU.md 5.1, etape 8), port 23,
; lance par init2 (SD0:/etc/services.txt). Comme telnetd d'Onyx : chaque
; connexion (8 au plus) a son interpreteur de commandes SD0:/bin/sh, relie par
; deux tubes (ce que tape le client -> entree de sh ; sortie de sh -> le
; client). Mode ligne : le client fait l'echo et l'edition (telnet, PuTTY,
; un script Python) ; les negociations telnet sont lues et ignorees.
; Ctrl+C (octet 3, ou IAC IP) va a sh, qui arrete le programme en cours. Un
; NOP telnet toutes les 5 min de silence : connexion perdue = session finie
; (Ctrl+C puis fin de l'entree de sh).
; Pas de mot de passe (decision de l'utilisateur : l'authentification vient
; a la toute fin du projet) : a garder sur le reseau de la maison.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/netkit/netkit.inc'

PORT		= 23
SESS_MAX	= 8
SS_USED		= 0			; 0 libre ; threads encore en vie
SS_SOCK		= 8
SS_IN_W		= 16			; entree de sh (on y ecrit)
SS_OUT_R	= 24			; sortie de sh (on la lit)
SS_PID		= 32
SS_STATE	= 40			; analyse telnet
SS_CR		= 48			; un CR vient de passer
SS_SIZE		= 64
TSTACK		= 32768
IAC		= 255
T_SB		= 250
T_SE		= 240
T_IP		= 244
T_NOP		= 241
T_WILL		= 251
T_DONT		= 254

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	kits_open
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
.p:	ldr	x1,[x20,SS_USED]
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
	mov	x0,sp				; entree de sh : [sp] lecture, [sp+8]
	bl	f_pipe				; ecriture
	tbnz	x0,63,.ko
	add	x0,sp,16			; sortie de sh : [sp+16], [sp+24]
	bl	f_pipe
	tbnz	x0,63,.ko1
	ldr	x0,[sp]				; sh : entree, sortie, erreur
	ldr	x1,[sp,24]
	stp	x0,x1,[sp,32]
	str	x1,[sp,48]
	adr	x0,p_sh
	mov	x1,0
	add	x2,sp,32
	bl	f_spawn_stdio
	mov	x21,x0
	ldr	x0,[sp]				; nos copies des bouts de sh
	bl	f_fermer
	ldr	x0,[sp,24]
	bl	f_fermer
	tbnz	x21,63,.ko2
	str	x21,[x20,SS_PID]
	ldr	x0,[sp,8]
	str	x0,[x20,SS_IN_W]
	ldr	x0,[sp,16]
	str	x0,[x20,SS_OUT_R]
	mov	x0,2				; deux threads
	str	x0,[x20,SS_USED]
	adr	x0,m_session			; "telnetd : session, sh pid N"
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,vers_sh
	mov	x1,x20
	bl	thread
	adr	x0,vers_client
	mov	x1,x20
	bl	thread
	b	.out
.ko2:	ldr	x0,[sp,8]
	bl	f_fermer
	ldr	x0,[sp,16]
	bl	f_fermer
	b	.ko
.ko1:	ldr	x0,[sp]
	bl	f_fermer
	ldr	x0,[sp,8]
	bl	f_fermer
.ko:	mov	x0,x19
	bl	net_close
	str	xzr,[x20,SS_USED]
.out:	add	sp,sp,64
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree, x1 = argument : un thread avec sa pile
thread:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,TSTACK
	bl	malloc
	cbz	x0,.out
	add	x1,x0,TSTACK
	and	x1,x1,0xFFFFFFFFFFFFFFF0
	mov	x0,x19
	mov	x2,x20
	sys	SYS_THREAD_CREATE
.out:	ldp	x19,x20,[sp],16
	epilogue

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
	cbz	x0,.loop
	mov	x2,x0
	ldr	x0,[x19,SS_IN_W]
	add	x1,sp,512
	bl	f_ecrire
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
.fin:	mov	w0,3				; fin : Ctrl+C, puis fin de l'entree
	strb	w0,[sp]
	ldr	x0,[x19,SS_IN_W]
	mov	x1,sp
	mov	x2,1
	bl	f_ecrire
	ldr	x0,[x19,SS_IN_W]		; (sh s'arrete : vers_client ferme
	bl	f_fermer			;  la connexion, une seule fois)
	mov	x0,x19
	bl	fin_thread
	sys	SYS_THREAD_EXIT

; (thread) x0 = session : sortie de sh -> client (LF -> CRLF, IAC double)
vers_client:
	mov	x19,x0
	sub	sp,sp,1552
.loop:	ldr	x0,[x19,SS_OUT_R]
	mov	x1,sp
	mov	x2,512
	bl	f_lire
	cmp	x0,0
	b.le	.fin				; sh a fini
	mov	x2,x0
	mov	x3,0				; lecture
	mov	x4,0				; ecriture
	add	x5,sp,512
.c:	cmp	x3,x2
	b.hs	.envoi
	ldrb	w6,[sp,x3]
	add	x3,x3,1
	cmp	w6,10
	b.ne	.iac
	mov	w7,13
	strb	w7,[x5,x4]
	add	x4,x4,1
	b	.st
.iac:	cmp	w6,IAC
	b.ne	.st
	strb	w6,[x5,x4]
	add	x4,x4,1
.st:	strb	w6,[x5,x4]
	add	x4,x4,1
	b	.c
.envoi:	ldr	x0,[x19,SS_SOCK]
	add	x1,sp,512
	mov	x2,x4
	bl	net_send
	b	.loop				; (client parti : vers_sh le voit)
.fin:	ldr	x0,[x19,SS_OUT_R]
	bl	f_fermer
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
	cmp	w7,13
	b.ne	.put
	mov	x6,1
	mov	w7,10
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
	b.ne	.c
	mov	x5,4
	b	.c
.s_sbiac:
	cmp	w7,T_SE
	mov	x8,3
	csel	x5,xzr,x8,eq
	b	.c
.fin:	str	x5,[x3,SS_STATE]
	str	x6,[x3,SS_CR]
	mov	x0,x4
	ret

p_sh		db 'SD0:/bin/sh',0
m_pret		db 'telnetd : en ',0xC3,0xA9,'coute sur le port 23 (sans mot de passe)',10,0
m_session	db 'telnetd : session, sh pid ',0
m_finie		db 'telnetd : session finie',10,0
m_nl		db 10,0
m_plein		db 'telnetd : trop de sessions (8)',0
s_crlf		db 13,10
		align 8

segment readable writeable
kit_data
		align 8
sess		rb SESS_MAX * SS_SIZE
