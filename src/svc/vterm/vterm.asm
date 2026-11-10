; vterm - pseudo-terminaux (docs/VTERM.md ; docs/TERMINAL-ET-FORK.md 2.1, decision
; du 10/11/2026 : un service a part, pas dans vfs). Lance par init2
; (SD0:/etc/services.txt), relance s'il tombe (les pty ouverts sont alors perdus).
;
;  * Un pty : un bout maitre (le Terminal, telnetd) et un bout esclave (le
;    programme) ; deux anneaux de PTY_RING octets : entree (maitre -> esclave,
;    apres la discipline de ligne) et sortie (esclave -> maitre, apres OPOST) ;
;    un etat termios, une taille, un groupe de premier plan. Protocole :
;    src/inc/vterm.inc.
;  * Bouts a plusieurs proprietaires, comme les tubes de vfs (pipes.inc) :
;    couples (poignee, pid) ; un bout compte tant qu'il a un proprietaire.
;    Avis de naissance : le fils devient proprietaire des bouts de vterm que le
;    pere lui donne en entree / sortie (0 : ceux du pere, gardes ici par pid,
;    table stdios) ; avis de fork : de tous les bouts du pere ; avis de fin :
;    le pid les quitte. Plus de maitre : l'esclave lit la fin, SIGHUP au groupe
;    de premier plan. Plus d'esclave : le maitre lit la fin.
;  * Lectures : rend ce qui est la ; sinon reponse differee (WAIT_MS au plus,
;    puis E_AGAIN : le client redemande), VMIN / VTIME en mode brut. Un signal
;    de la discipline (ISIG) rend E_INTR aux lectures en attente de l'esclave.
;  * Discipline (entree, ecrite par le maitre) : ISTRIP, IGNCR, ICRNL, INLCR ;
;    ISIG : VINTR, VQUIT, VSUSP -> SIGINT, SIGQUIT, SIGTSTP au groupe de premier
;    plan (SYS_SIG_SEND, pid negatif), entree videe sauf NOFLSH ; ICANON : ligne
;    editee (VERASE et 0x08, VWERASE, VKILL, VREPRINT, VLNEXT), rendue a la fin
;    de ligne (LF, VEOL) ou a VEOF (VEOF sur une ligne vide : fin pour le
;    lecteur) ; ECHO, ECHOE, ECHOK, ECHOKE, ECHONL, ECHOCTL (^X). Sortie
;    (ecrite par l'esclave) : OPOST, ONLCR, OCRNL.
;  * Controle des taches (cote esclave, s'il y a un groupe de premier plan) :
;    un process d'un autre groupe qui lit recoit SIGTTIN (son groupe), qui
;    ecrit avec TOSTOP, change termios, le groupe ou vide l'entree recoit
;    SIGTTOU, sauf s'il ignore ou masque le signal (SIG_SEND | SIGF_TTY : le
;    noyau decide) ; la requete rend E_AGAIN (lecture) ou E_INTR (le client
;    redemande apres la reprise), ou E_IO (SIGTTIN ignore ou masque, groupe
;    orphelin). Ctrl+Z (SIGTSTP) rend E_AGAIN aux lectures en attente. Ce n'est
;    fait que pour la session du pty (PT_SID, TIOCSCTTY) ; un pty sans session
;    est le terminal de tout le monde.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../inc/vterm.inc'

PTY_MAX		= 16
PTY_RING	= 4096				; (puissance de 2)
LINE_MAX	= 256				; ligne en edition (mode canonique)
PT_USED		= 0				; (32 bits partout)
PT_NM		= 4				; proprietaires du maitre
PT_NS		= 8				; de l'esclave
PT_PGRP		= 12				; groupe de premier plan (0 : aucun)
PT_TERMIOS	= 16				; VTT_* (48 o)
PT_ROWS		= 64
PT_COLS		= 68
PT_XPIX		= 72
PT_YPIX		= 76
PT_IH		= 80				; anneau d'entree : tete, queue
PT_IT		= 84
PT_OH		= 88				; anneau de sortie
PT_OT		= 92
PT_LN		= 96				; octets de la ligne en edition
PT_MARK		= 100				; position d'une marque dans l'entree
PT_MARKF	= 104				; 0 aucune, 1 fin (VEOF), 2 limite
PT_LNEXT	= 108				; 1 : le prochain octet est pris tel quel
PT_SID		= 112				; session dont c'est le terminal de controle (0 : aucune)
PT_SIZE		= 128
OWN_MAX		= 512				; couples (poignee, pid)
PS_MAX		= 128				; entrees / sorties de vterm par pid
PS_SIZE		= 32				; pid, poignees 0, 1, 2
WAIT_MAX	= 32
WAIT_REQ	= 0
WAIT_H		= 8
WAIT_BYTES	= 16
WAIT_LIMIT	= 24
WAIT_PID	= 32
WAIT_VTIME	= 40				; 1 : echeance de VTIME (rendre ce qui est la)
WAIT_SIZE	= 48
WAIT_MS		= 1000				; (court : le client regarde ses signaux entre deux essais)
SIGHUP_		= 1
SIGINT_		= 2
SIGQUIT_	= 3
SIGTSTP_	= 18
SIGTTIN_	= 21
SIGTTOU_	= 22
SIGWINCH_	= 28
SIGCONT_	= 19

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; (puts : journal)

start:
	bl	kits_open
	adr	x0,s_vterm
	mov	x1,SVF_FINS or SVF_NAISSANCES
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	adr	x0,m_pret
	bl	puts
.loop:	bl	waits_tick
	bl	waits_any
	movl	x2,60000
	cbz	x0,.next
	mov	x2,20				; (VTIME : 1/10 s)
.next:	adr	x0,canal
	ldr	x0,[x0]
	adr	x1,msg
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.loop
	mov	x19,x0
	adr	x20,msg
	ldr	x0,[x20]
	movl	x1,SVC_OP_FIN
	cmp	x0,x1
	b.eq	.fin
	movl	x1,SVC_OP_NAISSANCE
	cmp	x0,x1
	b.eq	.naissance
	movl	x1,SVC_OP_FORK
	cmp	x0,x1
	b.eq	.fork
	movl	x1,SVC_OP_SIGNAL
	cmp	x0,x1
	b.eq	.loop
	adr	x1,current_req
	str	x19,[x1]
	bl	ops
	cbnz	x1,.loop			; reponse differee
	str	x0,[x20]
	mov	x0,x19
	adr	x1,msg
	sys	SYS_SVC_REPLY
	b	.loop
.fin:	ldr	x0,[x20,MSG_A]
	bl	pid_gone
	b	.loop
.naissance:
	ldr	x0,[x20,MSG_A]
	ldr	x1,[x20,MSG_B]
	bl	birth
	b	.loop
.fork:	ldp	x0,x1,[x20,MSG_A]
	bl	fork_copy
	b	.loop
quit:	mov	x0,1
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Poignees, proprietaires
; ---------------------------------------------------------------------------

; x0 = poignee -> x0 = pty (0 : invalide ou libre), x1 = numero, x2 = cote
; (0 maitre, 1 esclave) ; ne touche que x0-x4
h_pty:
	movl	x3,VT_TAG + 1
	sub	x3,x0,x3
	cmp	x3,PTY_MAX * 2
	b.hs	.no
	lsr	x1,x3,1
	and	x2,x3,1
	adr	x0,ptys
	add	x0,x0,x1,lsl 7
	ldr	w4,[x0,PT_USED]
	cbz	w4,.no
	ret
.no:	mov	x0,0
	ret

; x0 = numero du pty, x1 = cote -> x0 = poignee
pty_h:
	lsl	x0,x0,1
	add	x0,x0,x1
	movl	x1,VT_TAG + 1
	add	x0,x0,x1
	ret

; x0 = poignee, x1 = pid -> x0 = son couple (0 : aucun) ; ne touche que x0-x5
own_find:
	adr	x2,owners
	mov	x3,OWN_MAX
.f:	ldp	w4,w5,[x2]
	cmp	w4,w0
	b.ne	.n
	cmp	w5,w1
	b.eq	.got
.n:	add	x2,x2,8
	subs	x3,x3,1
	b.ne	.f
	mov	x0,0
	ret
.got:	mov	x0,x2
	ret

; x0 = poignee (pty ouvert), x1 = pid : un proprietaire de plus (rien s'il
; l'est deja) -> x0 = 1, 0 si la table est pleine ; ne touche que x0-x9
own_add:
	mov	x6,x0
	mov	x7,x1
	str	x30,[sp,-16]!
	bl	own_find
	ldr	x30,[sp],16
	cbnz	x0,.oui
	adr	x2,owners
	mov	x3,OWN_MAX
.f:	ldr	w4,[x2]
	cbz	w4,.put
	add	x2,x2,8
	subs	x3,x3,1
	b.ne	.f
	mov	x0,0
	ret
.put:	stp	w6,w7,[x2]
	movl	x3,VT_TAG + 1			; compteur du cote
	sub	x3,x6,x3
	lsr	x8,x3,1
	and	x9,x3,1
	adr	x4,ptys
	add	x4,x4,x8,lsl 7
	add	x4,x4,PT_NM
	ldr	w5,[x4,x9,lsl 2]
	add	w5,w5,1
	str	w5,[x4,x9,lsl 2]
.oui:	mov	x0,1
	ret

; x0 = poignee, x1 = pid -> x0 = pty s'il est a ce pid (0 sinon), x1 = numero,
; x2 = cote ; ne touche que x0-x7
h_check:
	mov	x6,x0
	mov	x7,x1
	str	x30,[sp,-16]!
	bl	h_pty
	cbz	x0,.out
	stp	x0,x1,[sp,-16]!
	str	x2,[sp,-16]!
	mov	x0,x6
	mov	x1,x7
	bl	own_find
	ldr	x2,[sp],16
	mov	x3,x0
	ldp	x0,x1,[sp],16
	cbnz	x3,.out
	mov	x0,0
.out:	ldr	x30,[sp],16
	ret

; x0 = pid -> x0 = son entree dans stdios (0 : aucune) ; ne touche que x0-x3
ps_find:
	adr	x1,stdios
	mov	x2,PS_MAX
.f:	ldr	x3,[x1]
	cmp	x3,x0
	b.eq	.got
	add	x1,x1,PS_SIZE
	subs	x2,x2,1
	b.ne	.f
	mov	x0,0
	ret
.got:	mov	x0,x1
	ret

; x0 = pid -> x0 = son entree (creee ; 0 : table pleine) ; ne touche que x0-x5
ps_get:
	mov	x5,x0
	str	x30,[sp,-16]!
	bl	ps_find
	cbnz	x0,.r
	mov	x0,0
	bl	ps_find				; une place libre (pid 0)
	cbz	x0,.r
	str	x5,[x0]
	stp	xzr,xzr,[x0,8]
	str	xzr,[x0,24]
.r:	ldr	x30,[sp],16
	ret

; x0 = poignee, x1 = pid (un proprietaire) : le pid quitte le bout ; ses
; lectures en attente dessus : E_BADF ; le dernier parti, ce cote est ferme
own_drop:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	own_find
	cbz	x0,.out
	str	xzr,[x0]
	adr	x21,waits
	mov	x22,0
.w:	ldr	x0,[x21,WAIT_REQ]
	cbz	x0,.wn
	ldr	x0,[x21,WAIT_H]
	cmp	x0,x19
	b.ne	.wn
	ldr	x0,[x21,WAIT_PID]
	cmp	x0,x20
	b.ne	.wn
	mov	x0,x21
	mov	x1,E_BADF
	mov	x2,0
	bl	wait_reply
.wn:	add	x21,x21,WAIT_SIZE
	add	x22,x22,1
	cmp	x22,WAIT_MAX
	b.lo	.w
	mov	x0,x20				; plus dans ses entrees / sorties
	bl	ps_find
	cbz	x0,.cnt
	mov	x1,1
.s:	ldr	x2,[x0,x1,lsl 3]
	cmp	x2,x19
	b.ne	.sn
	str	xzr,[x0,x1,lsl 3]
.sn:	add	x1,x1,1
	cmp	x1,4
	b.lo	.s
.cnt:	mov	x0,x19
	bl	h_pty
	cbz	x0,.out
	add	x3,x0,PT_NM
	ldr	w4,[x3,x2,lsl 2]
	subs	w4,w4,1
	csel	w4,wzr,w4,lo
	str	w4,[x3,x2,lsl 2]
	cbnz	w4,.out
	bl	side_closed			; (x0 pty, x1 numero, x2 cote)
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pty, x1 = numero, x2 = cote qui n'a plus personne : l'autre cote lit
; la fin ; plus de maitre : SIGHUP au groupe ; plus personne : pty libere
side_closed:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbnz	x21,.serve
	ldr	w0,[x19,PT_NS]			; plus de maitre : SIGHUP
	cbz	w0,.serve
	mov	x0,x19
	mov	x1,SIGHUP_
	bl	pty_signal
.serve:	mov	x0,x20
	bl	pty_serve
	ldr	w0,[x19,PT_NM]
	ldr	w1,[x19,PT_NS]
	orr	w0,w0,w1
	cbnz	w0,.out
	mov	x0,x20				; plus personne : ses attentes, E_BADF
	mov	x1,E_BADF
	bl	pty_fail_waits
	mov	x0,x19				; libre
	mov	x1,0
.z:	str	xzr,[x0,x1]
	add	x1,x1,8
	cmp	x1,PT_SIZE
	b.lo	.z
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero du pty, x1 = code : les lectures en attente sur ses bouts recoivent
; ce code (x2 = 2 : les deux cotes, 1 : l'esclave seulement)
pty_fail_waits:
	mov	x2,2
pty_fail_side:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x23,x1
	mov	x24,x2
	mov	x1,0				; poignees : maitre x21, esclave x22
	mov	x19,x0
	bl	pty_h
	mov	x21,x0
	add	x22,x0,1
	adr	x19,waits
	mov	x20,0
.w:	ldr	x0,[x19,WAIT_REQ]
	cbz	x0,.n
	ldr	x0,[x19,WAIT_H]
	cmp	x0,x22
	b.eq	.hit
	cmp	x24,2
	b.ne	.n
	cmp	x0,x21
	b.ne	.n
.hit:	mov	x0,x19
	mov	x1,x23
	mov	x2,0
	bl	wait_reply
.n:	add	x19,x19,WAIT_SIZE
	add	x20,x20,1
	cmp	x20,WAIT_MAX
	b.lo	.w
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pty, x1 = signal : au groupe de premier plan (rien s'il n'y en a pas)
pty_signal:
	ldr	w3,[x0,PT_PGRP]
	cbz	w3,.r
	mov	x2,x1
	neg	x0,x3
	mov	x1,0
	mov	x8,SYS_SIG_SEND
	svc	0
.r:	ret

; ---------------------------------------------------------------------------
;  Avis du noyau
; ---------------------------------------------------------------------------

; x0 = pid termine : il quitte ses bouts
pid_gone:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	adr	x20,owners
	mov	x21,0
.e:	ldp	w0,w1,[x20]
	cbz	w0,.n
	cmp	w1,w19
	b.ne	.n
	mov	x1,x19
	bl	own_drop
.n:	add	x20,x20,8
	add	x21,x21,1
	cmp	x21,OWN_MAX
	b.lo	.e
	mov	x0,x19
	bl	ps_find
	cbz	x0,.out
	stp	xzr,xzr,[x0]
	stp	xzr,xzr,[x0,16]
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fils, x1 = pere (msg : c, d, donnees = entree, sortie, erreur donnees ;
; 0 : celle du pere, -1 : aucune) : le fils devient proprietaire des bouts de
; vterm qu'il recoit
birth:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x23,msg
	mov	x21,0
.i:	add	x0,x23,MSG_C			; c, d, puis les donnees
	cmp	x21,2
	b.lo	.cd
	ldr	x22,[x23,MSG_DATA]
	b	.got
.cd:	ldr	x22,[x0,x21,lsl 3]
.got:	cmn	x22,1
	b.eq	.n
	cbnz	x22,.given
	mov	x0,x20				; 0 : celle du pere
	bl	ps_find
	cbz	x0,.n
	add	x0,x0,8
	ldr	x22,[x0,x21,lsl 3]
	cbz	x22,.n
.given:	tst	x22,VT_TAG
	b.eq	.n
	mov	x0,x22				; encore au pere ?
	mov	x1,x20
	bl	h_check
	cbz	x0,.n
	mov	x0,x22
	mov	x1,x19
	bl	own_add
	cbz	x0,.n
	mov	x0,x19
	bl	ps_get
	cbz	x0,.n
	add	x0,x0,8
	str	x22,[x0,x21,lsl 3]
.n:	add	x21,x21,1
	cmp	x21,3
	b.lo	.i
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fils, x1 = pere : fork, le fils tient tous les bouts du pere (memes
; poignees) et ses entrees / sorties
fork_copy:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x21,owners
.e:	ldp	w0,w1,[x21]
	cbz	w0,.n
	cmp	w1,w20
	b.ne	.n
	mov	x1,x19
	bl	own_add
.n:	add	x21,x21,8
	adr	x0,owners + OWN_MAX * 8
	cmp	x21,x0
	b.lo	.e
	mov	x0,x20
	bl	ps_find
	cbz	x0,.out
	mov	x21,x0
	mov	x0,x19
	bl	ps_get
	cbz	x0,.out
	ldp	x1,x2,[x21,8]
	stp	x1,x2,[x0,8]
	ldr	x1,[x21,24]
	str	x1,[x0,24]
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Anneaux
; ---------------------------------------------------------------------------

; x0 = pty, x1 = numero -> x0 = octets dans l'anneau d'entree (ne touche que x0-x2)
in_count:
	ldr	w1,[x0,PT_IH]
	ldr	w2,[x0,PT_IT]
	sub	w1,w1,w2
	and	x0,x1,PTY_RING-1
	ret
out_count:
	ldr	w1,[x0,PT_OH]
	ldr	w2,[x0,PT_OT]
	sub	w1,w1,w2
	and	x0,x1,PTY_RING-1
	ret

; x0 = pty, x1 = numero, w2 = octet : dans l'anneau d'entree -> x0 = 1, 0 plein
; (ne touche que x0-x6)
in_put:
	adr	x3,in_data
	add	x3,x3,x1,lsl 12
	ldr	w4,[x0,PT_IH]
	ldr	w5,[x0,PT_IT]
	add	w6,w4,1
	and	w6,w6,PTY_RING-1
	cmp	w6,w5
	b.eq	.plein
	strb	w2,[x3,x4]
	str	w6,[x0,PT_IH]
	mov	x0,1
	ret
.plein:	mov	x0,0
	ret

; x0 = pty, x1 = numero, w2 = octet : dans l'anneau de sortie -> x0 = 1, 0 plein
out_put:
	adr	x3,out_data
	add	x3,x3,x1,lsl 12
	ldr	w4,[x0,PT_OH]
	ldr	w5,[x0,PT_OT]
	add	w6,w4,1
	and	w6,w6,PTY_RING-1
	cmp	w6,w5
	b.eq	.plein
	strb	w2,[x3,x4]
	str	w6,[x0,PT_OH]
	mov	x0,1
	ret
.plein:	mov	x0,0
	ret

; x0 = pty -> x0 = places libres dans l'anneau de sortie
out_free:
	str	x30,[sp,-16]!
	bl	out_count
	ldr	x30,[sp],16
	mov	x1,PTY_RING-1
	sub	x0,x1,x0
	ret

; x0 = pty, x1 = numero, w2 = octet : sortie avec OPOST (ONLCR, OCRNL)
; -> x0 = 1, 0 si la place manque (rien ecrit) ; garde x19-x28
opost_put:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	bl	out_free
	mov	x22,x0
	ldr	w0,[x19,PT_TERMIOS+VTT_OFLAG]
	tbz	w0,0,.brut			; (VTO_OPOST)
	cmp	w21,10
	b.ne	.cr
	tbz	w0,2,.brut			; (VTO_ONLCR) LF -> CR LF
	cmp	x22,2
	b.lo	.non
	mov	x0,x19
	mov	x1,x20
	mov	w2,13
	bl	out_put
	b	.brut1
.cr:	cmp	w21,13
	b.ne	.brut
	tbz	w0,3,.brut			; (VTO_OCRNL) CR -> LF
	mov	w21,10
.brut:	cbz	x22,.non
.brut1:	mov	x0,x19
	mov	x1,x20
	mov	w2,w21
	bl	out_put
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pty, x1 = numero : l'entree videe (anneau, ligne, marque)
in_flush:
	str	wzr,[x0,PT_IH]
	str	wzr,[x0,PT_IT]
	str	wzr,[x0,PT_LN]
	str	wzr,[x0,PT_MARKF]
	str	wzr,[x0,PT_LNEXT]
	ret

; ---------------------------------------------------------------------------
;  Discipline de ligne : un octet ecrit par le maitre
; ---------------------------------------------------------------------------

; (x19 = pty, x20 = numero) w0 = octet a l'echo : ECHOCTL (^X) pour un octet de
; controle autre que TAB, LF ; garde x19-x28
echo_b:
	prologue
	str	x21,[sp,-16]!
	and	w21,w0,0xFF
	ldr	w1,[x19,PT_TERMIOS+VTT_LFLAG]
	tbz	w1,9,.plain			; (VTL_ECHOCTL)
	cmp	w21,0x7F
	b.eq	.ctl
	cmp	w21,0x20
	b.hs	.plain
	cmp	w21,9
	b.eq	.plain
	cmp	w21,10
	b.eq	.plain
.ctl:	mov	x0,x19
	mov	x1,x20
	mov	w2,'^'
	bl	opost_put
	eor	w21,w21,0x40
.plain:	mov	x0,x19
	mov	x1,x20
	mov	w2,w21
	bl	opost_put
	ldr	x21,[sp],16
	epilogue

; (x19, x20) x0 = chaine : a l'echo telle quelle
echo_s:
	prologue
	str	x21,[sp,-16]!
	mov	x21,x0
.c:	ldrb	w2,[x21],1
	cbz	w2,.out
	mov	x0,x19
	mov	x1,x20
	bl	opost_put
	b	.c
.out:	ldr	x21,[sp],16
	epilogue

; (x19, x20) efface le dernier caractere de la ligne (UTF-8) ; x0 = 1 : echo
; visuel (ECHOE : BS espace BS, deux fois pour un ^X) -> x0 = 1 s'il y en avait un
erase_one:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x22,x0
	ldr	w1,[x19,PT_LN]
	cbz	w1,.none
	adr	x2,lines
	add	x2,x2,x20,lsl 8
.b:	sub	w1,w1,1
	ldrb	w21,[x2,x1]
	cbz	w1,.done
	and	w3,w21,0xC0
	cmp	w3,0x80
	b.eq	.b
.done:	str	w1,[x19,PT_LN]
	cbz	x22,.yes
	adr	x0,s_bsb
	bl	echo_s
	ldr	w1,[x19,PT_TERMIOS+VTT_LFLAG]
	tbz	w1,9,.yes			; (ECHOCTL : ^X tient deux colonnes)
	cmp	w21,0x20
	b.hs	.yes
	cmp	w21,9
	b.eq	.yes
	adr	x0,s_bsb
	bl	echo_s
.yes:	mov	x0,1
	b	.out
.none:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	epilogue

; (x19, x20) la ligne en edition passe dans l'anneau d'entree -> x0 = 1, 0 si
; la place manque (la ligne reste)
line_push:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x0,x19
	bl	in_count
	mov	x1,PTY_RING-1
	sub	x0,x1,x0
	ldr	w21,[x19,PT_LN]
	cmp	x0,x21
	b.lo	.non
	adr	x22,lines
	add	x22,x22,x20,lsl 8
	mov	x3,0
.c:	cmp	x3,x21
	b.hs	.f
	ldrb	w2,[x22,x3]
	stp	x3,x3,[sp,-16]!
	mov	x0,x19
	mov	x1,x20
	bl	in_put
	ldp	x3,x4,[sp],16
	add	x3,x3,1
	b	.c
.f:	str	wzr,[x19,PT_LN]
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	epilogue

; (x19, x20) w0 = octet ajoute a la ligne (s'il y a la place) ; echo
line_add:
	prologue
	str	x21,[sp,-16]!
	and	w21,w0,0xFF
	ldr	w1,[x19,PT_LN]
	cmp	w1,LINE_MAX-1
	b.hs	.out				; (pleine : perdu)
	adr	x2,lines
	add	x2,x2,x20,lsl 8
	strb	w21,[x2,x1]
	add	w1,w1,1
	str	w1,[x19,PT_LN]
	ldr	w1,[x19,PT_TERMIOS+VTT_LFLAG]
	tbz	w1,3,.out			; (VTL_ECHO)
	mov	w0,w21
	bl	echo_b
.out:	ldr	x21,[sp],16
	epilogue

; x19 = pty, x20 = numero, w0 = octet ecrit par le maitre : la discipline
; -> x0 = 1 pris, 0 : pas de place (a redonner plus tard)
ld_input:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	and	w21,w0,0xFF
	ldr	w22,[x19,PT_TERMIOS+VTT_IFLAG]
	ldr	w23,[x19,PT_TERMIOS+VTT_LFLAG]
	add	x24,x19,PT_TERMIOS+VTT_CC
	mov	x0,x19				; place : une ligne entiere au moins
	bl	in_count
	mov	x1,PTY_RING - LINE_MAX - 8
	cmp	x0,x1
	b.hs	.plein
	ldr	w0,[x19,PT_LNEXT]		; apres VLNEXT : tel quel
	cbz	w0,.istrip
	str	wzr,[x19,PT_LNEXT]
	tbz	w23,1,.raw
	ldr	w0,[x19,PT_LN]			; (le ^V affiche : efface)
	cbz	w0,.lnx
	tbz	w23,3,.lnx
	adr	x0,s_bsb
	bl	echo_s
.lnx:	mov	w0,w21
	bl	line_add
	b	.oui
.istrip:
	tbz	w22,5,.cr			; (VTI_ISTRIP)
	and	w21,w21,0x7F
.cr:	cmp	w21,13
	b.ne	.lf
	tbnz	w22,7,.oui			; (VTI_IGNCR) ignore
	tbz	w22,8,.sig			; (VTI_ICRNL)
	mov	w21,10
	b	.sig
.lf:	cmp	w21,10
	b.ne	.sig
	tbz	w22,6,.sig			; (VTI_INLCR)
	mov	w21,13
.sig:	tbz	w23,0,.canon			; (VTL_ISIG)
	ldrb	w0,[x24,VTC_INTR]
	mov	x1,SIGINT_
	cbz	w0,.q
	cmp	w0,w21
	b.eq	.signal
.q:	ldrb	w0,[x24,VTC_QUIT]
	mov	x1,SIGQUIT_
	cbz	w0,.z
	cmp	w0,w21
	b.eq	.signal
.z:	ldrb	w0,[x24,VTC_SUSP]
	mov	x1,SIGTSTP_
	cbz	w0,.canon
	cmp	w0,w21
	b.ne	.canon
.signal:
	mov	x22,x1
	tbnz	w23,7,.sigecho			; (VTL_NOFLSH)
	mov	x0,x19
	mov	x1,x20
	bl	in_flush
.sigecho:
	tbz	w23,3,.sigsend			; (VTL_ECHO) : ^C
	mov	w0,w21
	bl	echo_b
.sigsend:
	mov	x0,x19
	mov	x1,x22
	bl	pty_signal
	mov	x0,x20				; lecteurs de l'esclave : E_INTR ; E_AGAIN
	mov	x1,VT_E_INTR			; pour SIGTSTP (arretes, ils redemandent
	cmp	x22,SIGTSTP_			; une fois repris)
	mov	x2,E_AGAIN
	csel	x1,x2,x1,eq
	mov	x2,1
	bl	pty_fail_side
	b	.oui
.canon:	tbz	w23,1,.raw			; (VTL_ICANON)
	tbz	w23,15,.erase			; (VTL_IEXTEN) : VLNEXT
	ldrb	w0,[x24,VTC_LNEXT]
	cbz	w0,.erase
	cmp	w0,w21
	b.ne	.erase
	mov	w0,1
	str	w0,[x19,PT_LNEXT]
	tbz	w23,3,.oui
	adr	x0,s_lnext			; ^ puis BS, comme Linux
	bl	echo_s
	b	.oui
.erase:	ldrb	w0,[x24,VTC_ERASE]
	cmp	w0,w21
	b.eq	.do_erase
	cmp	w21,8				; (Retour arriere en ^H aussi)
	b.ne	.werase
.do_erase:
	ubfx	w0,w23,3,1			; ECHO et ECHOE : visuel
	ubfx	w1,w23,4,1
	and	w0,w0,w1
	bl	erase_one
	cbnz	x0,.oui
	b	.oui
.werase:
	tbz	w23,15,.kill
	ldrb	w0,[x24,VTC_WERASE]
	cbz	w0,.kill
	cmp	w0,w21
	b.ne	.kill
	adr	x22,lines			; les blancs, puis le mot
	add	x22,x22,x20,lsl 8
.we1:	ldr	w1,[x19,PT_LN]
	cbz	w1,.oui
	sub	w1,w1,1
	ldrb	w2,[x22,x1]
	cmp	w2,' '
	b.eq	.we2
	cmp	w2,9
	b.ne	.we3
.we2:	ubfx	w0,w23,3,1
	bl	erase_one
	b	.we1
.we3:	ldr	w1,[x19,PT_LN]
	cbz	w1,.oui
	sub	w1,w1,1
	ldrb	w2,[x22,x1]
	cmp	w2,' '
	b.eq	.oui
	cmp	w2,9
	b.eq	.oui
	ubfx	w0,w23,3,1
	bl	erase_one
	b	.we3
.kill:	ldrb	w0,[x24,VTC_KILL]
	cbz	w0,.reprint
	cmp	w0,w21
	b.ne	.reprint
	tbz	w23,3,.k0			; (VTL_ECHO)
	tst	w23,VTL_ECHOKE
	b.eq	.kline
.ke:	mov	w0,1				; ECHOKE : tout efface a l'ecran
	bl	erase_one
	cbnz	x0,.ke
	b	.oui
.kline:	mov	w0,w21				; sinon : ^U, puis LF (ECHOK)
	bl	echo_b
	tbz	w23,5,.k0
	adr	x0,s_nl
	bl	echo_s
.k0:	str	wzr,[x19,PT_LN]
	b	.oui
.reprint:
	tbz	w23,15,.eof
	ldrb	w0,[x24,VTC_REPRINT]
	cbz	w0,.eof
	cmp	w0,w21
	b.ne	.eof
	tbz	w23,3,.oui
	mov	w0,w21				; ^R, LF, la ligne
	bl	echo_b
	adr	x0,s_nl
	bl	echo_s
	ldr	w22,[x19,PT_LN]
	adr	x23,lines
	add	x23,x23,x20,lsl 8
.rp:	cbz	w22,.oui
	ldrb	w0,[x23],1
	bl	echo_b
	sub	w22,w22,1
	b	.rp
.eof:	ldrb	w0,[x24,VTC_EOF]
	cbz	w0,.eol
	cmp	w0,w21
	b.ne	.eol
	ldr	w22,[x19,PT_LN]
	bl	line_push
	cbz	x0,.plein
	ldr	w0,[x19,PT_IH]			; marque : fin (ligne vide) ou limite
	str	w0,[x19,PT_MARK]
	mov	w0,1
	cbz	w22,.mk
	mov	w0,2
.mk:	str	w0,[x19,PT_MARKF]
	b	.oui
.eol:	cmp	w21,10
	b.eq	.nl
	ldrb	w0,[x24,VTC_EOL]
	cbz	w0,.add
	cmp	w0,w21
	b.ne	.add
.nl:	ldr	w1,[x19,PT_LN]			; fin de ligne : ajoutee, la ligne part
	adr	x2,lines
	add	x2,x2,x20,lsl 8
	cmp	w1,LINE_MAX-1
	b.lo	.nl1
	mov	w1,LINE_MAX-2			; (pleine : la fin de ligne remplace
.nl1:	strb	w21,[x2,x1]			;  le dernier octet)
	add	w1,w1,1
	str	w1,[x19,PT_LN]
	tbnz	w23,3,.nle			; ECHO, ou ECHONL pour LF
	cmp	w21,10
	b.ne	.nlp
	tbz	w23,6,.nlp
.nle:	mov	w0,w21
	bl	echo_b
.nlp:	bl	line_push
	b	.oui
.add:	mov	w0,w21
	bl	line_add
	b	.oui
.raw:	mov	x0,x19				; mode brut : tel quel
	mov	x1,x20
	mov	w2,w21
	bl	in_put
	cbz	x0,.plein
	tbz	w23,3,.oui
	mov	x0,x19
	mov	x1,x20
	mov	w2,w21
	bl	opost_put
.oui:	mov	x0,1
	b	.out
.plein:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Lectures
; ---------------------------------------------------------------------------

; x0 = pty, x1 = numero, x2 = cote, x3 = destination, x4 = octets voulus,
; x5 = 1 : echeance (VTIME : rendre ce qui est la) -> x0 = octets, 0 fin,
; -1 : pas pret (attendre)
pty_take:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x3
	mov	x22,x4
	mov	x26,x5
	cbnz	x2,.esclave
	bl	out_count			; maitre : la sortie du programme
	cbnz	x0,.mtake
	ldr	w0,[x19,PT_NS]
	cbz	w0,.fin				; plus d'esclave : fin
	b	.attend
.mtake:	cmp	x0,x22
	csel	x22,x0,x22,lo
	adr	x3,out_data
	add	x3,x3,x20,lsl 12
	ldr	w4,[x19,PT_OT]
	mov	x5,0
.mc:	cmp	x5,x22
	b.hs	.mf
	ldrb	w6,[x3,x4]
	strb	w6,[x21,x5]
	add	x5,x5,1
	add	w4,w4,1
	and	w4,w4,PTY_RING-1
	b	.mc
.mf:	str	w4,[x19,PT_OT]
	mov	x0,x22
	b	.out
.esclave:
	ldr	w23,[x19,PT_TERMIOS+VTT_LFLAG]
	tbz	w23,1,.brut			; (ICANON)
	ldr	w0,[x19,PT_MARKF]		; une marque au debut ?
	cbz	w0,.canon
	ldr	w1,[x19,PT_MARK]
	ldr	w2,[x19,PT_IT]
	cmp	w1,w2
	b.ne	.canon
	str	wzr,[x19,PT_MARKF]
	cmp	w0,1
	b.eq	.fin				; VEOF sur une ligne vide
.canon:	mov	x0,x19
	bl	in_count
	cbnz	x0,.ctake
	ldr	w0,[x19,PT_NM]
	cbz	w0,.fin				; plus de maitre : fin
	b	.attend
.ctake:	adr	x3,in_data			; jusqu'a la fin de ligne, ou la marque
	add	x3,x3,x20,lsl 12
	ldr	w4,[x19,PT_IT]
	ldr	w7,[x19,PT_IH]
	ldr	w8,[x19,PT_MARKF]
	ldr	w9,[x19,PT_MARK]
	add	x10,x19,PT_TERMIOS+VTT_CC
	ldrb	w10,[x10,VTC_EOL]
	mov	x5,0
.cc:	cmp	x5,x22
	b.hs	.cf
	cmp	w4,w7
	b.eq	.cf
	cbz	w8,.cb
	cmp	w4,w9
	b.ne	.cb
	cmp	w8,2				; limite (VEOF) : effacee, on s'arrete
	b.ne	.cf
	str	wzr,[x19,PT_MARKF]
	b	.cf
.cb:	ldrb	w6,[x3,x4]
	strb	w6,[x21,x5]
	add	x5,x5,1
	add	w4,w4,1
	and	w4,w4,PTY_RING-1
	cmp	w6,10
	b.eq	.cf
	cbz	w10,.cc
	cmp	w6,w10
	b.ne	.cc
.cf:	str	w4,[x19,PT_IT]
	cbz	w8,.cr				; marque atteinte et passee ?
	cmp	w4,w9
	b.ne	.cr
	cmp	w8,2
	b.ne	.cr
	str	wzr,[x19,PT_MARKF]
.cr:	mov	x0,x5
	b	.out
.brut:	mov	x0,x19				; VMIN, VTIME
	bl	in_count
	mov	x24,x0
	add	x1,x19,PT_TERMIOS+VTT_CC
	ldrb	w25,[x1,VTC_MIN]
	ldrb	w2,[x1,VTC_TIME]
	cbz	x24,.vide
	cbz	x25,.btake			; VMIN 0 : ce qui est la
	cbnz	x26,.btake			; echeance
	cmp	x25,x22				; VMIN au plus ce qui est demande
	csel	x25,x22,x25,hi
	cmp	x24,x25
	b.hs	.btake
	b	.attend
.vide:	ldr	w0,[x19,PT_NM]
	cbz	w0,.fin
	cbnz	x26,.fin			; echeance de VTIME : 0
	orr	w0,w25,w2
	cbz	w0,.fin				; VMIN 0, VTIME 0 : 0 tout de suite
	b	.attend
.btake:	cmp	x24,x22
	csel	x22,x24,x22,lo
	adr	x3,in_data
	add	x3,x3,x20,lsl 12
	ldr	w4,[x19,PT_IT]
	mov	x5,0
.bc:	cmp	x5,x22
	b.hs	.bf
	ldrb	w6,[x3,x4]
	strb	w6,[x21,x5]
	add	x5,x5,1
	add	w4,w4,1
	and	w4,w4,PTY_RING-1
	b	.bc
.bf:	str	w4,[x19,PT_IT]
	mov	x0,x22
	b	.out
.fin:	mov	x0,0
	b	.out
.attend:
	mov	x0,-1
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = attente, x1 = resultat, x2 = octets deja dans rmsg : reponse, place rendue
wait_reply:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,rmsg
	str	x1,[x0]
	str	w2,[x0,MSG_LEN]
	ldr	x0,[x19,WAIT_REQ]
	str	xzr,[x19,WAIT_REQ]
	adr	x1,rmsg
	sys	SYS_SVC_REPLY
	ldr	x19,[sp],16
	epilogue

; x0 = attente, x1 = 1 : echeance -> sert la lecture si elle est prete
; (x0 = 1 : repondue)
wait_try:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x0,[x19,WAIT_H]
	bl	h_pty
	cbz	x0,.badf
	adr	x3,rmsg
	add	x3,x3,MSG_DATA
	ldr	x4,[x19,WAIT_BYTES]
	ldr	x5,[x19,WAIT_VTIME]
	and	x5,x5,x20
	bl	pty_take
	tbnz	x0,63,.non
	mov	x1,x0
	mov	x2,x0
	mov	x0,x19
	bl	wait_reply
	mov	x0,1
	b	.out
.badf:	mov	x0,x19
	mov	x1,E_BADF
	mov	x2,0
	bl	wait_reply
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero d'un pty : sert les lectures qui l'attendent
pty_serve:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x1,0
	bl	pty_h
	mov	x21,x0				; maitre, esclave = x21 + 1
	adr	x19,waits
	mov	x20,0
.w:	ldr	x0,[x19,WAIT_REQ]
	cbz	x0,.n
	ldr	x0,[x19,WAIT_H]
	sub	x0,x0,x21
	cmp	x0,1
	b.hi	.n
	mov	x0,x19
	mov	x1,0
	bl	wait_try
.n:	add	x19,x19,WAIT_SIZE
	add	x20,x20,1
	cmp	x20,WAIT_MAX
	b.lo	.w
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; echeances : VTIME (rendre ce qui est la), sinon E_AGAIN (le client redemande)
waits_tick:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	sys	SYS_TIME_MS
	mov	x21,x0
	adr	x19,waits
	mov	x20,0
.w:	ldr	x0,[x19,WAIT_REQ]
	cbz	x0,.n
	ldr	x0,[x19,WAIT_LIMIT]
	cmp	x0,x21
	b.hi	.n
	mov	x0,x19
	mov	x1,1
	bl	wait_try
	cbnz	x0,.n
	mov	x0,x19
	mov	x1,E_AGAIN
	mov	x2,0
	bl	wait_reply
.n:	add	x19,x19,WAIT_SIZE
	add	x20,x20,1
	cmp	x20,WAIT_MAX
	b.lo	.w
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si une lecture attend
waits_any:
	adr	x1,waits
	mov	x2,0
.w:	ldr	x0,[x1,WAIT_REQ]
	cbnz	x0,.oui
	add	x1,x1,WAIT_SIZE
	add	x2,x2,1
	cmp	x2,WAIT_MAX
	b.lo	.w
	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; ---------------------------------------------------------------------------
;  Requetes (msg, au nom de MSG_PID) -> x0 = resultat, x1 = 1 : differee
; ---------------------------------------------------------------------------
ops:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	adr	x20,msg
	ldr	x25,[x20]
	ldr	w21,[x20,MSG_PID]
	ldr	w0,[x20,MSG_LEN]		; (gardee : la reponse la remet a 0)
	adr	x1,msg_len
	str	w0,[x1]
	str	wzr,[x20,MSG_LEN]
	cmp	x25,VT_OPEN
	b.eq	.open
	cmp	x25,VT_SLAVE
	b.eq	.slave
	ldr	x26,[x20,MSG_A]			; les autres : un bout de l'appelant
	mov	x0,x26
	mov	x1,x21
	bl	h_check
	cbz	x0,.badf
	mov	x19,x0
	mov	x22,x1				; numero
	mov	x23,x2				; cote
	cmp	x25,VT_READ
	b.eq	.read
	cmp	x25,VT_WRITE
	b.eq	.write
	cmp	x25,VT_CLOSE
	b.eq	.close
	cmp	x25,VT_TCGETS
	b.eq	.tcgets
	cmp	x25,VT_TCSETS
	b.eq	.tcsets
	cmp	x25,VT_GWINSZ
	b.eq	.gwinsz
	cmp	x25,VT_SWINSZ
	b.eq	.swinsz
	cmp	x25,VT_GPGRP
	b.eq	.gpgrp
	cmp	x25,VT_SPGRP
	b.eq	.spgrp
	cmp	x25,VT_INFO
	b.eq	.info
	cmp	x25,VT_FLUSH
	b.eq	.flush
	cmp	x25,VT_SCTTY
	b.eq	.sctty
	cmp	x25,VT_NOTTY
	b.eq	.notty
	cmp	x25,VT_GSID
	b.eq	.gsid
	mov	x0,E_INVAL
	b	.now

.open:	adr	x19,ptys			; un pty libre
	mov	x22,0
.of:	ldr	w0,[x19,PT_USED]
	cbz	w0,.ogot
	add	x19,x19,PT_SIZE
	add	x22,x22,1
	cmp	x22,PTY_MAX
	b.lo	.of
	mov	x0,-24				; (EMFILE)
	b	.now
.ogot:	mov	x0,x19
	mov	x1,0
.oz:	str	xzr,[x0,x1]
	add	x1,x1,8
	cmp	x1,PT_SIZE
	b.lo	.oz
	mov	w0,1
	str	w0,[x19,PT_USED]
	add	x0,x19,PT_TERMIOS		; termios par defaut
	adr	x1,termios_defaut
	mov	x2,0
.ot:	ldr	x3,[x1,x2]
	str	x3,[x0,x2]
	add	x2,x2,8
	cmp	x2,VTT_TAILLE
	b.lo	.ot
	mov	w0,24
	str	w0,[x19,PT_ROWS]
	mov	w0,80
	str	w0,[x19,PT_COLS]
	mov	x0,x22
	mov	x1,0
	bl	pty_h
	mov	x23,x0
	mov	x1,x21
	bl	own_add
	cbz	x0,.ofull
	add	x0,x23,1
	mov	x1,x21
	bl	own_add
	cbz	x0,.ofull2
	add	x0,x23,1
	stp	x23,x0,[x20,MSG_A]
	str	x22,[x20,MSG_C]
	mov	x0,0
	b	.now
.ofull2:
	mov	x0,x23
	mov	x1,x21
	bl	own_find
	cbz	x0,.ofull
	str	xzr,[x0]
.ofull:	str	wzr,[x19,PT_USED]
	mov	x0,-24
	b	.now

.slave:	ldr	x22,[x20,MSG_B]			; open de /dev/pts/N
	cmp	x22,PTY_MAX
	b.hs	.noent
	adr	x19,ptys
	add	x19,x19,x22,lsl 7
	ldr	w0,[x19,PT_USED]
	cbz	w0,.noent
	ldr	w0,[x19,PT_NM]			; (sans maitre : il ne revivra pas)
	cbz	w0,.noent
	mov	x0,x22
	mov	x1,1
	bl	pty_h
	mov	x23,x0
	mov	x1,x21
	bl	own_add
	cbz	x0,.mfile
	str	x23,[x20,MSG_A]
	mov	x0,0
	b	.now
.noent:	mov	x0,E_NOENT
	b	.now
.mfile:	mov	x0,-24
	b	.now

.read:	cbz	x23,.read1			; esclave : en arriere-plan ?
	mov	x0,SIGTTIN_
	bl	bg_check
	cmp	x0,1				; (SIGTTIN envoye : E_AGAIN, le client
	b.eq	.again				;  redemande une fois repris)
	cbnz	x0,.bg
.read1:	ldr	x24,[x20,MSG_B]
	cmp	x24,FS_DONNEES
	mov	x0,FS_DONNEES
	csel	x24,x0,x24,hi
	cbz	x24,.zero
	mov	x0,x19
	mov	x1,x22
	mov	x2,x23
	add	x3,x20,MSG_DATA
	mov	x4,x24
	mov	x5,0
	bl	pty_take
	tbnz	x0,63,.wait
	str	w0,[x20,MSG_LEN]
	b	.now
.wait:	ldr	x0,[x20,MSG_C]
	tbnz	x0,0,.again			; VT_NOWAIT
	adr	x0,waits
	mov	x1,0
.wf:	ldr	x2,[x0,WAIT_REQ]
	cbz	x2,.wgot
	add	x0,x0,WAIT_SIZE
	add	x1,x1,1
	cmp	x1,WAIT_MAX
	b.lo	.wf
	b	.again				; (table pleine : le client redemande)
.wgot:	mov	x25,x0
	adr	x0,current_req
	ldr	x0,[x0]
	str	x0,[x25,WAIT_REQ]
	str	x26,[x25,WAIT_H]
	str	x24,[x25,WAIT_BYTES]
	str	x21,[x25,WAIT_PID]
	str	xzr,[x25,WAIT_VTIME]
	sys	SYS_TIME_MS
	movl	x1,WAIT_MS
	add	x1,x0,x1
	cbz	x23,.wlim			; esclave en brut avec VTIME : son echeance
	ldr	w2,[x19,PT_TERMIOS+VTT_LFLAG]
	tbnz	w2,1,.wlim
	add	x2,x19,PT_TERMIOS+VTT_CC
	ldrb	w2,[x2,VTC_TIME]
	cbz	w2,.wlim
	mov	x3,100
	mul	x2,x2,x3
	add	x1,x0,x2
	mov	x0,1
	str	x0,[x25,WAIT_VTIME]
.wlim:	str	x1,[x25,WAIT_LIMIT]
	mov	x0,0
	mov	x1,1
	b	.out
.again:	mov	x0,E_AGAIN
	b	.now

.write:	adr	x0,msg_len
	ldr	w24,[x0]
	cmp	x24,FS_DONNEES
	mov	x0,FS_DONNEES
	csel	x24,x0,x24,hi
	cbnz	x23,.swrite
	mov	x25,0				; maitre : la discipline, octet par octet
	stp	x19,x22,[sp,-16]!
	mov	x20,x22				; (ld_input : x19 pty, x20 numero)
.mw:	cmp	x25,x24
	b.hs	.mwf
	adr	x0,msg
	add	x0,x0,MSG_DATA
	ldrb	w0,[x0,x25]
	bl	ld_input
	cbz	x0,.mwf
	add	x25,x25,1
	b	.mw
.mwf:	ldp	x19,x22,[sp],16
	adr	x20,msg
	mov	x0,x22
	bl	pty_serve
	mov	x0,x25
	b	.now
.swrite:
	ldr	w0,[x19,PT_NM]			; esclave : plus de maitre, E_IO
	cbz	w0,.eio
	ldr	w0,[x19,PT_TERMIOS+VTT_LFLAG]	; TOSTOP : en arriere-plan ?
	tbz	w0,8,.sw0
	mov	x0,SIGTTOU_
	bl	bg_check
	cbnz	x0,.bg
.sw0:	adr	x20,msg
	mov	x25,0
.sw:	cmp	x25,x24
	b.hs	.swf
	add	x0,x20,MSG_DATA
	ldrb	w2,[x0,x25]
	mov	x0,x19
	mov	x1,x22
	bl	opost_put
	cbz	x0,.swf
	add	x25,x25,1
	b	.sw
.swf:	mov	x0,x22
	bl	pty_serve
	mov	x0,x25
	b	.now
.eio:	mov	x0,VT_E_IO
	b	.now

.close:	mov	x0,x26
	mov	x1,x21
	bl	own_drop
	mov	x0,0
	b	.now

.tcgets:
	add	x0,x20,MSG_DATA
	add	x1,x19,PT_TERMIOS
	mov	x2,0
.tg:	ldr	x3,[x1,x2]
	str	x3,[x0,x2]
	add	x2,x2,8
	cmp	x2,VTT_TAILLE
	b.lo	.tg
	mov	w0,VTT_TAILLE
	str	w0,[x20,MSG_LEN]
	mov	x0,0
	b	.now

.tcsets:
	bl	bg_ttou
	cbnz	x0,.bg
	adr	x0,msg_len
	ldr	w0,[x0]
	cmp	x0,VTT_TAILLE
	b.lo	.inval
	ldr	w24,[x19,PT_TERMIOS+VTT_LFLAG]	; ancien mode
	add	x0,x19,PT_TERMIOS
	add	x1,x20,MSG_DATA
	mov	x2,0
.ts:	ldr	x3,[x1,x2]
	str	x3,[x0,x2]
	add	x2,x2,8
	cmp	x2,VTT_TAILLE
	b.lo	.ts
	ldr	x0,[x20,MSG_B]
	cmp	x0,2				; TCSAFLUSH : entree videe
	b.ne	.tsc
	mov	x0,x19
	mov	x1,x22
	bl	in_flush
.tsc:	tbz	w24,1,.tsd			; canonique -> brut : la ligne en
	ldr	w0,[x19,PT_TERMIOS+VTT_LFLAG]	; edition passe a l'anneau
	tbnz	w0,1,.tsd
	stp	x19,x20,[sp,-16]!
	mov	x20,x22
	bl	line_push
	ldp	x19,x20,[sp],16
	str	wzr,[x19,PT_MARKF]
.tsd:	mov	x0,x22
	bl	pty_serve
	mov	x0,0
	b	.now

.gwinsz:
	ldr	w0,[x19,PT_ROWS]
	ldr	w1,[x19,PT_COLS]
	stp	x0,x1,[x20,MSG_A]
	ldr	w0,[x19,PT_XPIX]
	ldr	w1,[x19,PT_YPIX]
	stp	x0,x1,[x20,MSG_C]
	mov	x0,0
	b	.now

.swinsz:
	ldp	x0,x1,[x20,MSG_B]		; lignes, colonnes
	ldr	x2,[x20,MSG_D]
	and	x3,x2,0xFFFF
	ubfx	x2,x2,16,16
	ldr	w4,[x19,PT_ROWS]
	ldr	w5,[x19,PT_COLS]
	str	w0,[x19,PT_ROWS]
	str	w1,[x19,PT_COLS]
	str	w3,[x19,PT_XPIX]
	str	w2,[x19,PT_YPIX]
	cmp	w0,w4
	b.ne	.winch
	cmp	w1,w5
	b.eq	.okz
.winch:	mov	x0,x19
	mov	x1,SIGWINCH_
	bl	pty_signal
.okz:	mov	x0,0
	b	.now

.gpgrp:	ldr	w0,[x19,PT_PGRP]
	str	x0,[x20,MSG_A]
	mov	x0,0
	b	.now
.spgrp:	bl	bg_ttou
	cbnz	x0,.bg
	ldr	x0,[x20,MSG_B]
	cmp	x0,0
	b.le	.inval
	str	w0,[x19,PT_PGRP]
	mov	x0,0
	b	.now

.sctty:	ldr	w0,[x20,MSG_PID]		; TIOCSCTTY : meneur de session seulement ;
	mov	x24,x0				; le pty ne doit pas etre a une autre
	movl	x1,GETPGID_SID_MASK		; session
	orr	x0,x0,x1
	sys	SYS_GETPGID
	cmp	x0,x24
	b.ne	.perm
	ldr	w1,[x19,PT_SID]
	cbz	w1,.sct1
	cmp	w1,w24
	b.ne	.perm
.sct1:	str	w24,[x19,PT_SID]
	mov	x0,x24				; son groupe au premier plan
	sys	SYS_GETPGID
	tbnz	x0,63,.okz
	str	w0,[x19,PT_PGRP]
	b	.okz
.notty:	ldr	w0,[x20,MSG_PID]		; TIOCNOTTY : le meneur rend le terminal
	mov	x24,x0
	movl	x1,GETPGID_SID_MASK
	orr	x0,x0,x1
	sys	SYS_GETPGID
	ldr	w1,[x19,PT_SID]
	cbz	w1,.nott
	cmp	w0,w1
	b.ne	.nott
	cmp	x0,x24
	b.ne	.okz
	mov	x0,x19				; (comme Linux : SIGHUP, SIGCONT au
	mov	x1,SIGHUP_			;  groupe de premier plan)
	bl	pty_signal
	mov	x0,x19
	mov	x1,SIGCONT_
	bl	pty_signal
	str	wzr,[x19,PT_SID]
	str	wzr,[x19,PT_PGRP]
	b	.okz
.nott:	mov	x0,VT_E_NOTTY
	b	.now
.gsid:	ldr	w0,[x19,PT_SID]
	str	x0,[x20,MSG_A]
	b	.okz
.bg:	cmp	x0,1				; arriere-plan : E_INTR (arrete), E_IO
	mov	x0,VT_E_INTR
	b.eq	.now
	mov	x0,VT_E_IO
	b	.now
.perm:	mov	x0,VT_E_PERM
	b	.now

.info:	add	x0,x23,1
	stp	x0,x22,[x20,MSG_A]
	mov	x0,x19
	cbnz	x23,.ise
	bl	out_count
	ldr	w1,[x19,PT_NS]
	b	.isf
.ise:	bl	in_count
	ldr	w1,[x19,PT_NM]
	ldr	w2,[x19,PT_TERMIOS+VTT_LFLAG]	; (fin marquee par VEOF : vue aussi)
	tbz	w2,1,.isf
	ldr	w2,[x19,PT_MARKF]
	cmp	w2,1
	b.ne	.isf
	mov	w1,0
.isf:	str	x0,[x20,MSG_C]
	cmp	w1,0
	cset	x1,eq
	str	x1,[x20,MSG_D]
	mov	x0,0
	b	.now

.flush:	bl	bg_ttou
	cbnz	x0,.bg
	ldr	x24,[x20,MSG_B]
	cmp	x24,1
	b.eq	.fo
	mov	x0,x19
	mov	x1,x22
	bl	in_flush
	cmp	x24,0
	b.eq	.okz
.fo:	str	wzr,[x19,PT_OH]
	str	wzr,[x19,PT_OT]
	b	.okz

.zero:	mov	x0,0
	b	.now
.inval:	mov	x0,E_INVAL
	b	.now
.badf:	mov	x0,E_BADF
.now:	mov	x1,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ops, x19 = pty, x23 = cote) changement par l'esclave : SIGTTOU s'il est en
; arriere-plan -> x0 comme bg_check
bg_ttou:
	cbz	x23,.non
	mov	x0,SIGTTOU_
	b	bg_check
.non:	mov	x0,0
	ret

; x19 = pty, x0 = SIGTTIN_ ou SIGTTOU_ ; l'appelant (msg) est-il en arriere-plan
; dans la session du pty ? -> x0 = 0 : continuer ; 1 : signal envoye a son
; groupe (rendre E_INTR) ; 2 : rendre E_IO (SIGTTIN ignore ou masque, groupe
; orphelin)
bg_check:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x22,x0
	adr	x0,msg
	ldr	w20,[x0,MSG_PID]
	ldr	w21,[x19,PT_PGRP]
	cbz	w21,.go
	mov	x0,x20
	sys	SYS_GETPGID
	tbnz	x0,63,.go
	cmp	w0,w21
	b.eq	.go
	ldr	w23,[x19,PT_SID]		; terminal d'une autre session : rien
	cbz	w23,.sig
	movl	x1,GETPGID_SID_MASK
	orr	x0,x20,x1
	sys	SYS_GETPGID
	cmp	x0,x23
	b.ne	.go
.sig:	mov	x0,x20
	mov	x1,0
	movl	x2,SIGF_TTY_MASK
	orr	x2,x2,x22
	sys	SYS_SIG_SEND
	cbz	x0,.envoye
	cmp	x0,2
	b.eq	.eio
	cmp	x22,SIGTTIN_			; ignore ou masque : SIGTTOU laisse faire,
	b.eq	.eio				; SIGTTIN rend E_IO
.go:	mov	x0,0
	b	.out
.envoye:
	mov	x0,1
	b	.out
.eio:	mov	x0,2
.out:	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

s_vterm		db 'vterm',0
m_pret		db 'vterm : pret',10,0
s_bsb		db 8,' ',8,0
s_lnext		db '^',8,0
s_nl		db 10,0
		align 8
; termios par defaut : ICRNL IUTF8 ; OPOST ONLCR ; CS8 CREAD ; ISIG ICANON
; ECHO ECHOE ECHOK ECHOCTL ECHOKE IEXTEN ; ^C ^\ DEL ^U ^D, VMIN 1, ^Z, ^R ^W ^V
termios_defaut:
	dw VTI_ICRNL or VTI_IUTF8, VTO_OPOST or VTO_ONLCR, 0xB0
	dw VTL_ISIG or VTL_ICANON or VTL_ECHO or VTL_ECHOE or VTL_ECHOK or VTL_ECHOCTL or VTL_ECHOKE or VTL_IEXTEN
	db 3, 0x1C, 0x7F, 0x15, 4, 0, 1, 0, 0x11, 0x13, 0x1A, 0, 0x12, 0x0F, 0x17, 0x16
	db 16 dup (0)
		align 8

segment readable writeable
kit_data
		align 8
canal		dd 0
current_req	dd 0
msg_len		dd 0
msg		rb MSG_TAILLE
rmsg		rb MSG_TAILLE
ptys		rb PTY_MAX * PT_SIZE
owners		rb OWN_MAX * 8
stdios		rb PS_MAX * PS_SIZE
waits		rb WAIT_MAX * WAIT_SIZE
lines		rb PTY_MAX * LINE_MAX
in_data		rb PTY_MAX * PTY_RING
out_data	rb PTY_MAX * PTY_RING
