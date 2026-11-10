; pipes.inc - vfs : tubes et entrees / sorties standard des programmes
; (docs/FICHIERS.md, etape 4 ; reponse 8 de l'utilisateur : les tubes vont
; dans vfs).
;
;  * Un tube : un anneau de PIPE_CAP octets dans vfs, compteurs de lecteurs
;    et d'ecrivains. Un bout de tube : une poignee (1..END_MAX) qui appartient
;    a un ou plusieurs pid (MSG_PID ; table owners) ; seuls eux s'en servent
;    (E_BADF sinon). Plusieurs : apres un fork (avis SVC_OP_FORK, 0xFFFC), le
;    fils tient tous les bouts du pere, aux memes numeros (sa memoire recopiee,
;    les tables d'AppKit comprises, reste juste). Un bout compte pour un
;    lecteur ou un ecrivain tant qu'il a un proprietaire : fermer (V_PIPE_CLOSE,
;    fin du process) retire le pid ; le bout se ferme avec le dernier, la fin
;    (0) vient quand plus aucun pid ne tient d'ecrivain.
;  * Ops 37-41 (fs.inc) : creer, lire, ecrire, fermer, entrees / sorties
;    standard du pid. Lecture : rend ce qui est la (<= 464 o) ; vide avec un
;    ecrivain : E_AGAIN (PIPE_NOWAIT) ou reponse differee (la requete attend
;    des donnees ou la fin, WAIT_MS au plus, WAIT_COURT avec PIPE_COURT :
;    puis E_AGAIN, le client
;    redemande ; svc_call abandonne au bout de 30 s). Fin (0) quand le
;    dernier ecrivain ferme. Ecriture : ce qui tient (0 : plein, le client
;    reessaie) ; E_PIPE sans lecteur.
;  * Entrees / sorties standard (stdio) : trois poignees par pid
;    (fiche du process, pids.inc). L'avis de naissance (0xFFFE) porte celles que le pere
;    donne au fils (c entree, d sortie, donnees : erreur ; 0 = la sienne,
;    -1 = aucune) ; chaque bout donne doit appartenir au pere ; le fils en
;    recoit une copie a lui (un lecteur ou un ecrivain de plus). Le fils les
;    demande par V_STDIO (AppKit, au premier besoin).
;    Un bout de vterm (poignee avec VT_TAG, src/inc/vterm.inc) donne ou herite
;    est garde tel quel dans la fiche du fils et rendu tel quel par V_STDIO :
;    vfs ne le compte pas, vterm tient ses proprietaires (docs/VTERM.md).
;  * Avis de fin : le pid quitte ses bouts (fermes s'il etait le dernier),
;    ses lectures en attente sont abandonnees.
;  * Redirection vers un fichier (V_FILE_END, docs/FICHIERS.md) : un tube sans
;    anneau (PIPE_USED = 2), qui garde un chemin. Ses bouts se donnent et se
;    comptent comme les autres ; le fils voit par V_STDIO que c'est un fichier
;    et l'ouvre lui-meme (AppKit) : ni copie ni thread, et ses propres fils
;    en heritent.

PIPE_MAX	= 128				; (bash : tubes et $(...) imbriques)
PIPE_CAP	= 4096				; (puissance de 2)
PIPE_USED	= 0
PIPE_HEAD	= 8
PIPE_TAIL	= 16
PIPE_READERS	= 24				; (32 bits)
PIPE_WRITERS	= 28
PIPE_SIZE	= 32
PIPE_FICHIER	= 2				; (PIPE_USED) un bout-fichier
PIPE_CHEMIN	= 256				; pipe_chemins : un chemin par tube
END_MAX		= 512
END_PIPE	= 0				; tube + 1 (0 : libre) (32 bits)
END_NOWN	= 4				; proprietaires (32 bits, table owners)
END_MODE	= 8				; 1 lecture, 2 ecriture
END_SIZE	= 16
OWN_MAX		= 4096				; couples (bout, pid) : owners
OWN_END		= 0				; poignee (32 bits, 0 : libre)
OWN_PID		= 4				; (32 bits)
WAIT_MAX	= 64				; lectures en attente
WAIT_REQ	= 0				; numero de la requete (0 : libre)
WAIT_END	= 8				; poignee
WAIT_BYTES	= 16				; octets demandes
WAIT_LIMIT	= 24				; echeance (ms)
WAIT_PID	= 32				; pid qui lit
WAIT_SIZE	= 40
WAIT_MS		= 10000
WAIT_COURT	= 1000				; (PIPE_COURT)
PIPE_E_MFILE	= -24

; x0 = pid -> x0 = ses trois poignees standard (sa fiche : pids.inc ; 0 :
; plus de memoire) ; ne touche que x0
stdio_entry:
	str	x30,[sp,-16]!
	bl	pid_get
	ldr	x30,[sp],16
	cbz	x0,.out
	add	x0,x0,PR_STDIO
.out:	ret

; x0 = poignee, x1 = pid -> x0 = son couple dans owners (0 : aucun)
; ne touche que x0-x5
own_find:
	adr	x2,owners
	mov	x3,OWN_MAX
.f:	ldr	w4,[x2,OWN_END]
	ldr	w5,[x2,OWN_PID]
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

; x0 = poignee (bout ouvert), x1 = pid : un proprietaire de plus (rien s'il
; l'est deja) -> x0 = 1, 0 si la table est pleine ; ne touche que x0-x7
own_add:
	mov	x6,x0
	mov	x7,x1
	str	x30,[sp,-16]!
	bl	own_find
	ldr	x30,[sp],16
	cbnz	x0,.oui
	adr	x2,owners			; une place libre
	mov	x3,OWN_MAX
.f:	ldr	w4,[x2,OWN_END]
	cbz	w4,.put
	add	x2,x2,8
	subs	x3,x3,1
	b.ne	.f
	mov	x0,0
	ret
.put:	str	w6,[x2,OWN_END]
	str	w7,[x2,OWN_PID]
	sub	x2,x6,1				; END_NOWN + 1
	adr	x3,ends
	add	x3,x3,x2,lsl 4
	ldr	w4,[x3,END_NOWN]
	add	w4,w4,1
	str	w4,[x3,END_NOWN]
.oui:	mov	x0,1
	ret

; x0 = poignee, x1 = pid -> x0 = le bout (0 : libre, invalide ou pas a lui)
; ne touche que x0-x5
end_get:
	sub	x2,x0,1
	cmp	x2,END_MAX
	b.hs	.no
	adr	x3,ends
	add	x3,x3,x2,lsl 4
	ldr	w2,[x3,END_PIPE]
	cbz	w2,.no
	str	x3,[sp,-16]!
	str	x30,[sp,-16]!
	bl	own_find
	ldr	x30,[sp],16
	ldr	x3,[sp],16
	cbz	x0,.no
	mov	x0,x3
	ret
.no:	mov	x0,0
	ret

; x0 = bout -> x0 = son tube, x1 = numero du tube
end_pipe:
	ldr	w1,[x0,END_PIPE]
	sub	x1,x1,1
	adr	x0,pipes
	add	x0,x0,x1,lsl 5
	ret

; x0 = numero du tube, x1 = pid, x2 = mode -> x0 = poignee (0 : table pleine)
; (les compteurs du tube sont a la charge de l'appelant)
end_alloc:
	adr	x3,ends
	mov	x4,0
.f:	ldr	w5,[x3,END_PIPE]
	cbz	w5,.got
	add	x3,x3,END_SIZE
	add	x4,x4,1
	cmp	x4,END_MAX
	b.lo	.f
	mov	x0,0
	ret
.got:	add	w5,w0,1
	str	w5,[x3,END_PIPE]
	str	wzr,[x3,END_NOWN]
	str	x2,[x3,END_MODE]
	stp	x3,x30,[sp,-16]!
	add	x0,x4,1
	mov	x8,x0
	bl	own_add				; (x0-x7)
	ldp	x3,x30,[sp],16
	cbz	x0,.full
	mov	x0,x8
	ret
.full:	str	wzr,[x3,END_PIPE]		; plus de couple : bout rendu
	mov	x0,0
	ret

; x0 = tube -> x0 = octets en attente (ne touche que x0-x2)
pipe_avail:
	ldr	x1,[x0,PIPE_HEAD]
	ldr	x2,[x0,PIPE_TAIL]
	sub	x1,x1,x2
	and	x0,x1,PIPE_CAP-1
	ret

; x0 = tube, x1 = son numero, x2 = destination, x3 = max -> x0 = octets lus
pipe_take:
	adr	x4,pipe_data
	add	x4,x4,x1,lsl 12
	ldr	x5,[x0,PIPE_HEAD]
	ldr	x6,[x0,PIPE_TAIL]
	mov	x7,0
.c:	cmp	x7,x3
	b.hs	.e
	cmp	x6,x5
	b.eq	.e
	ldrb	w8,[x4,x6]
	strb	w8,[x2,x7]
	add	x7,x7,1
	add	x6,x6,1
	and	x6,x6,PIPE_CAP-1
	b	.c
.e:	str	x6,[x0,PIPE_TAIL]
	mov	x0,x7
	ret

; x0 = tube, x1 = son numero, x2 = source, x3 = octets -> x0 = octets ecrits
pipe_put:
	adr	x4,pipe_data
	add	x4,x4,x1,lsl 12
	ldr	x5,[x0,PIPE_HEAD]
	ldr	x6,[x0,PIPE_TAIL]
	mov	x7,0
.c:	cmp	x7,x3
	b.hs	.e
	add	x8,x5,1
	and	x8,x8,PIPE_CAP-1
	cmp	x8,x6
	b.eq	.e				; plein
	ldrb	w9,[x2,x7]
	strb	w9,[x4,x5]
	mov	x5,x8
	add	x7,x7,1
	b	.c
.e:	str	x5,[x0,PIPE_HEAD]
	mov	x0,x7
	ret

; x0 = lecture en attente, x1 = resultat, x2 = octets deja dans rmsg :
; reponse (differee) a son appelant, place rendue
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

; x0 = numero d'un tube : sert les lectures qui l'attendent (donnees, ou fin
; s'il n'a plus d'ecrivain)
pipe_serve:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x20,waits
	mov	x21,0
.w:	ldr	x0,[x20,WAIT_REQ]
	cbz	x0,.n
	ldr	x0,[x20,WAIT_END]
	sub	x0,x0,1
	adr	x1,ends
	add	x0,x1,x0,lsl 4
	bl	end_pipe
	cmp	x1,x19
	b.ne	.n
	mov	x22,x0
	bl	pipe_avail
	cbz	x0,.vide
	mov	x0,x22
	mov	x1,x19
	adr	x2,rmsg
	add	x2,x2,MSG_DATA
	ldr	x3,[x20,WAIT_BYTES]
	bl	pipe_take
	mov	x1,x0
	mov	x2,x0
	mov	x0,x20
	bl	wait_reply
	b	.n
.vide:	ldr	w0,[x22,PIPE_WRITERS]
	cbnz	w0,.n
	mov	x0,x20				; plus d'ecrivain : fin
	mov	x1,0
	mov	x2,0
	bl	wait_reply
.n:	add	x20,x20,WAIT_SIZE
	add	x21,x21,1
	cmp	x21,WAIT_MAX
	b.lo	.w
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = poignee (valide) : le bout est ferme ; un lecteur ou un ecrivain de
; moins ; plus d'ecrivain : fin pour les lectures en attente ; tube libere
; quand il n'a plus personne
end_close:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	sub	x1,x19,1
	adr	x0,ends
	add	x20,x0,x1,lsl 4
	ldr	w0,[x20,END_PIPE]
	cbz	w0,.out
	adr	x21,waits			; ses lectures en attente : E_BADF
	mov	x22,0
.w:	ldr	x0,[x21,WAIT_REQ]
	cbz	x0,.wn
	ldr	x0,[x21,WAIT_END]
	cmp	x0,x19
	b.ne	.wn
	mov	x0,x21
	mov	x1,E_BADF
	mov	x2,0
	bl	wait_reply
.wn:	add	x21,x21,WAIT_SIZE
	add	x22,x22,1
	cmp	x22,WAIT_MAX
	b.lo	.w
	mov	x0,x19				; plus une entree standard
	bl	pid_stdio_drop
	adr	x0,owners			; plus de proprietaire
	mov	x1,OWN_MAX
.o:	ldr	w2,[x0,OWN_END]
	cmp	w2,w19
	b.ne	.on
	str	xzr,[x0]
.on:	add	x0,x0,8
	subs	x1,x1,1
	b.ne	.o
	str	wzr,[x20,END_NOWN]
	mov	x0,x20
	bl	end_pipe
	mov	x21,x0
	mov	x22,x1
	ldr	x0,[x20,END_MODE]
	str	wzr,[x20,END_PIPE]
	cmp	x0,1
	b.ne	.ecr
	ldr	w0,[x21,PIPE_READERS]
	sub	w0,w0,1
	str	w0,[x21,PIPE_READERS]
	b	.vide
.ecr:	ldr	w0,[x21,PIPE_WRITERS]
	sub	w0,w0,1
	str	w0,[x21,PIPE_WRITERS]
	cbnz	w0,.vide
	mov	x0,x22				; dernier ecrivain : fin pour les lecteurs
	bl	pipe_serve
.vide:	ldr	w0,[x21,PIPE_READERS]
	ldr	w1,[x21,PIPE_WRITERS]
	orr	w0,w0,w1
	cbnz	w0,.out
	str	xzr,[x21,PIPE_USED]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = poignee, x1 = pid (un de ses proprietaires) : le pid quitte le bout ;
; ses lectures en attente dessus : E_BADF ; plus son entree standard ; le
; dernier parti, le bout est ferme (end_close)
end_drop:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	own_find
	cbz	x0,.out
	str	xzr,[x0]
	adr	x21,waits			; ses lectures en attente
	mov	x22,0
.w:	ldr	x0,[x21,WAIT_REQ]
	cbz	x0,.wn
	ldr	x0,[x21,WAIT_END]
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
	mov	x0,x20				; son entree standard
	bl	pid_find
	cbz	x0,.nown
	add	x0,x0,PR_STDIO
	mov	x1,0
.s:	ldr	x2,[x0,x1,lsl 3]
	cmp	x2,x19
	b.ne	.sn
	str	xzr,[x0,x1,lsl 3]
.sn:	add	x1,x1,1
	cmp	x1,3
	b.lo	.s
.nown:	sub	x1,x19,1			; un proprietaire de moins
	adr	x0,ends
	add	x0,x0,x1,lsl 4
	ldr	w1,[x0,END_NOWN]
	subs	w1,w1,1
	csel	w1,wzr,w1,lo
	str	w1,[x0,END_NOWN]
	cbnz	w1,.out
	mov	x0,x19				; le dernier : ferme
	bl	end_close
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid termine : il quitte ses bouts de tubes (fermes s'il etait le
; dernier a les tenir)
ends_pid_gone:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	adr	x20,owners
	mov	x21,0
.e:	ldr	w0,[x20,OWN_END]
	cbz	w0,.n
	ldr	w1,[x20,OWN_PID]
	cmp	w1,w19
	b.ne	.n
	mov	x1,x19
	bl	end_drop
.n:	add	x20,x20,8
	add	x21,x21,1
	cmp	x21,OWN_MAX
	b.lo	.e
	mov	x0,x19
	bl	pid_find
	cbz	x0,.out
	add	x0,x0,PR_STDIO
	stp	xzr,xzr,[x0]
	str	xzr,[x0,16]
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fils, x1 = pere : avis de fork (SVC_OP_FORK) ; le fils tient les bouts
; du pere (memes poignees : sa memoire recopiee les nomme deja) et ses entrees
; / sorties standard -> x0 = bouts que la table pleine n'a pas pu lui donner
ends_fork:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x22,0
	adr	x21,owners
.e:	ldr	w0,[x21,OWN_END]
	cbz	w0,.n
	ldr	w1,[x21,OWN_PID]
	cmp	w1,w20
	b.ne	.n
	mov	x1,x19
	bl	own_add
	cbnz	x0,.n
	add	x22,x22,1
.n:	add	x21,x21,8
	adr	x0,owners + OWN_MAX * 8
	cmp	x21,x0
	b.lo	.e
	mov	x0,x20				; ses entrees / sorties standard
	bl	pid_find
	cbz	x0,.out
	mov	x21,x0
	mov	x0,x19
	bl	stdio_entry
	cbz	x0,.out
	ldp	x1,x2,[x21,PR_STDIO]
	stp	x1,x2,[x0]
	ldr	x1,[x21,PR_STDIO+16]
	str	x1,[x0,16]
.out:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Echeances des lectures en attente : E_AGAIN (le client redemande)
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

; -> x0 = 1 si une lecture attend (attente courte de svc_next)
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

; x0 = fils, x1 = pere, x2 = entree, x3 = sortie, x4 = erreur (0 : celle du
; pere, -1 : aucune, sinon un bout du pere) : avis de naissance, le fils
; recoit ses copies
stdio_birth:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,32
	stp	x2,x3,[sp]
	str	x4,[sp,16]
	mov	x19,x0				; fils
	mov	x20,x1				; pere
	bl	stdio_entry
	cbz	x0,.out
	mov	x21,x0
	stp	xzr,xzr,[x21]
	str	xzr,[x21,16]
	mov	x22,0				; entree 0, 1, 2
.i:	ldr	x0,[sp,x22,lsl 3]
	cmn	x0,1
	b.eq	.n				; aucune
	cbnz	x0,.donne
	mov	x0,x20				; 0 : celle du pere
	bl	stdio_entry
	cbz	x0,.n
	ldr	x0,[x0,x22,lsl 3]
	cbz	x0,.n
.donne:	tst	x0,VT_TAG			; un bout de vterm : garde tel quel
	b.ne	.vterm				; (vterm tient ses proprietaires)
	mov	x1,x20				; un bout du pere ?
	bl	end_get
	cbz	x0,.n
	mov	x23,x0
	bl	end_pipe
	mov	x24,x0
	mov	x0,x1
	mov	x1,x19
	ldr	x2,[x23,END_MODE]
	bl	end_alloc
	cbz	x0,.n
	str	x0,[x21,x22,lsl 3]
	ldr	x0,[x23,END_MODE]
	cmp	x0,1
	b.ne	.ecr
	ldr	w0,[x24,PIPE_READERS]
	add	w0,w0,1
	str	w0,[x24,PIPE_READERS]
	b	.n
.ecr:	ldr	w0,[x24,PIPE_WRITERS]
	add	w0,w0,1
	str	w0,[x24,PIPE_WRITERS]
	b	.n
.vterm:	str	x0,[x21,x22,lsl 3]
.n:	add	x22,x22,1
	cmp	x22,3
	b.lo	.i
.out:	add	sp,sp,32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid, x1 = entree, x2 = sortie, x3 = erreur : avis d'exec (SVC_OP_EXEC,
; src/kernel/exec.asm). 0 : inchangee ; -1 : aucune ; sinon un bout de ce pid
; (le sien desormais, sans copie : il le tenait deja) ou de vterm. L'ancienne
; reste un bout du pid comme les autres (fermee a sa fin, ou par lui)
stdio_exec:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,32
	stp	x1,x2,[sp]
	str	x3,[sp,16]
	mov	x19,x0
	bl	stdio_entry
	cbz	x0,.out
	mov	x20,x0
	mov	x21,0
.i:	ldr	x0,[sp,x21,lsl 3]
	cbz	x0,.n				; inchangee
	cmn	x0,1
	b.ne	.bout
	str	xzr,[x20,x21,lsl 3]		; aucune (console)
	b	.n
.bout:	tst	x0,VT_TAG			; un bout de vterm : tel quel
	b.ne	.met
	mov	x22,x0
	mov	x1,x19				; un bout a lui ?
	bl	end_get
	cbz	x0,.n
	mov	x0,x22
.met:	str	x0,[x20,x21,lsl 3]
.n:	add	x21,x21,1
	cmp	x21,3
	b.lo	.i
.out:	add	sp,sp,32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (msg) ops 37-41 au nom de MSG_PID ; current_req = numero de la requete
; -> x0 = resultat (msg pret pour la reponse), x1 = 1 : reponse differee
ops_pipes:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x20,msg
	ldr	x19,[x20]
	ldr	w21,[x20,MSG_PID]
	cmp	x19,V_PIPE
	b.eq	.create
	cmp	x19,V_STDIO
	b.eq	.stdio
	ldr	x0,[x20,MSG_A]
	mov	x1,x21
	bl	end_get
	cbz	x0,.badf
	mov	x22,x0
	cmp	x19,V_PIPE_CLOSE
	b.eq	.close
	mov	x0,x22
	bl	end_pipe
	mov	x23,x0
	mov	x24,x1
	ldr	x0,[x22,END_MODE]
	cmp	x19,V_PIPE_READ
	b.eq	.read
	cmp	x0,2				; ecrire
	b.ne	.badf
	ldr	w0,[x23,PIPE_READERS]
	cbz	w0,.epipe
	ldr	w3,[x20,MSG_LEN]
	cmp	x3,FS_DONNEES
	mov	x0,FS_DONNEES
	csel	x3,x0,x3,hi
	mov	x0,x23
	mov	x1,x24
	add	x2,x20,MSG_DATA
	bl	pipe_put
	mov	x19,x0
	str	wzr,[x20,MSG_LEN]
	cbz	x19,.wr
	mov	x0,x24				; des lecteurs attendaient ?
	bl	pipe_serve
.wr:	mov	x0,x19
	b	.now
.read:	cmp	x0,1
	b.ne	.badf
	str	wzr,[x20,MSG_LEN]
	ldr	x19,[x20,MSG_B]
	cmp	x19,FS_DONNEES
	mov	x0,FS_DONNEES
	csel	x19,x0,x19,hi
	cbz	x19,.zero
	mov	x0,x23
	bl	pipe_avail
	cbz	x0,.empty
	mov	x0,x23
	mov	x1,x24
	add	x2,x20,MSG_DATA
	mov	x3,x19
	bl	pipe_take
	str	w0,[x20,MSG_LEN]
	b	.now
.empty:	ldr	w0,[x23,PIPE_WRITERS]
	cbz	w0,.zero			; plus d'ecrivain : fin
	ldr	x0,[x20,MSG_C]
	tbnz	x0,0,.again			; PIPE_NOWAIT
	adr	x0,waits			; reponse differee
	mov	x1,0
.wf:	ldr	x2,[x0,WAIT_REQ]
	cbz	x2,.wgot
	add	x0,x0,WAIT_SIZE
	add	x1,x1,1
	cmp	x1,WAIT_MAX
	b.lo	.wf
	b	.again				; (table pleine : le client redemande)
.wgot:	mov	x22,x0
	adr	x0,current_req
	ldr	x0,[x0]
	str	x0,[x22,WAIT_REQ]
	ldr	x0,[x20,MSG_A]
	str	x0,[x22,WAIT_END]
	str	x19,[x22,WAIT_BYTES]
	str	x21,[x22,WAIT_PID]
	sys	SYS_TIME_MS
	movl	x1,WAIT_MS
	ldr	x2,[x20,MSG_C]			; PIPE_COURT : 1 s (libposix : un
	tst	x2,PIPE_COURT			; signal rend EINTR entre deux)
	mov	x2,WAIT_COURT
	csel	x1,x2,x1,ne
	add	x0,x0,x1
	str	x0,[x22,WAIT_LIMIT]
	mov	x1,1
	b	.out
.again:	mov	x0,E_AGAIN
	b	.now
.zero:	mov	x0,0
	b	.now
.close:	ldr	x0,[x20,MSG_A]			; ce pid quitte le bout
	mov	x1,x21
	bl	end_drop
	str	wzr,[x20,MSG_LEN]
	mov	x0,0
	b	.now
.create:
	str	wzr,[x20,MSG_LEN]
	adr	x22,pipes			; un tube libre
	mov	x23,0
.pf:	ldr	x0,[x22,PIPE_USED]
	cbz	x0,.pgot
	add	x22,x22,PIPE_SIZE
	add	x23,x23,1
	cmp	x23,PIPE_MAX
	b.lo	.pf
	b	.mfile
.pgot:	mov	x0,x23
	mov	x1,x21
	mov	x2,1
	bl	end_alloc
	cbz	x0,.mfile
	mov	x24,x0
	mov	x0,x23
	mov	x1,x21
	mov	x2,2
	bl	end_alloc
	cbz	x0,.mfile2
	stp	x24,x0,[x20,MSG_A]
	mov	x0,1
	str	x0,[x22,PIPE_USED]
	stp	xzr,xzr,[x22,PIPE_HEAD]
	mov	w0,1
	str	w0,[x22,PIPE_READERS]
	str	w0,[x22,PIPE_WRITERS]
	mov	x0,0
	b	.now
.mfile2:
	mov	x0,x24				; rendre le premier bout
	mov	x1,x21
	bl	own_find
	cbz	x0,.mf2
	str	xzr,[x0]
.mf2:	sub	x1,x24,1
	adr	x2,ends
	add	x2,x2,x1,lsl 4
	str	wzr,[x2,END_PIPE]
.mfile:	mov	x0,PIPE_E_MFILE
	b	.now
.stdio:	str	wzr,[x20,MSG_LEN]
	stp	xzr,xzr,[x20,MSG_A]
	stp	xzr,xzr,[x20,MSG_C]
	mov	x0,x21
	bl	stdio_entry
	cbz	x0,.sdone
	mov	x22,x0
	mov	x23,0
.si:	ldr	x0,[x22,x23,lsl 3]
	cbz	x0,.sn
	mov	x24,x0
	tst	x0,VT_TAG			; un bout de vterm : rendu tel quel
	b.eq	.stube
	add	x0,x20,MSG_A
	str	x24,[x0,x23,lsl 3]
	b	.sn
.stube:
	mov	x1,x21				; (encore a lui ?)
	bl	end_get
	cbz	x0,.sn
	bl	end_pipe
	ldr	x1,[x0,PIPE_USED]
	add	x0,x20,MSG_A
	str	x24,[x0,x23,lsl 3]
	cmp	x1,PIPE_FICHIER
	b.ne	.sn
	ldr	x1,[x20,MSG_D]
	mov	x2,1
	lsl	x2,x2,x23
	orr	x1,x1,x2
	str	x1,[x20,MSG_D]
.sn:	add	x23,x23,1
	cmp	x23,3
	b.lo	.si
.sdone:	mov	x0,0
	b	.now
.epipe:	mov	x0,E_PIPE
	b	.now
.badf:	mov	x0,E_BADF
.now:	mov	x1,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (msg) V_FILE_END, V_END_FILE -> x0 = resultat
ops_bouts:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x20,msg
	ldr	x19,[x20]
	ldr	w21,[x20,MSG_PID]
	cmp	x19,V_END_FILE
	b.eq	.info
	ldr	x24,[x20,MSG_A]			; mode
	sub	x0,x24,1
	cmp	x0,1
	b.hi	.inval
	strb	wzr,[x20,MSG_TAILLE-1]
	mov	x0,x21
	add	x1,x20,MSG_DATA
	adr	x2,resolu
	bl	resoudre
	cbnz	x0,.out
	adr	x22,pipes			; un tube libre
	mov	x23,0
.f:	ldr	x0,[x22,PIPE_USED]
	cbz	x0,.got
	add	x22,x22,PIPE_SIZE
	add	x23,x23,1
	cmp	x23,PIPE_MAX
	b.lo	.f
	b	.mfile
.got:	mov	x0,x23
	mov	x1,x21
	mov	x2,x24
	bl	end_alloc
	cbz	x0,.mfile
	str	x0,[x20,MSG_A]
	mov	x0,PIPE_FICHIER
	str	x0,[x22,PIPE_USED]
	stp	xzr,xzr,[x22,PIPE_HEAD]
	cmp	x24,1
	cset	w0,eq
	str	w0,[x22,PIPE_READERS]
	eor	w0,w0,1
	str	w0,[x22,PIPE_WRITERS]
	adr	x0,resolu
	adr	x1,pipe_chemins
	add	x1,x1,x23,lsl 8
	bl	path_to_volume
	str	wzr,[x20,MSG_LEN]
	mov	x0,0
	b	.out
.info:	ldr	x0,[x20,MSG_A]
	mov	x1,x21
	bl	end_get
	cbz	x0,.badf
	mov	x22,x0
	bl	end_pipe
	ldr	x2,[x0,PIPE_USED]
	str	wzr,[x20,MSG_LEN]
	cmp	x2,PIPE_FICHIER
	mov	x0,0
	b.ne	.out
	ldr	x0,[x22,END_MODE]
	str	x0,[x20,MSG_B]
	adr	x0,pipe_chemins
	add	x0,x0,x1,lsl 8
	add	x1,x20,MSG_DATA
	mov	x2,0
.c:	ldrb	w3,[x0,x2]
	strb	w3,[x1,x2]
	add	x2,x2,1
	cbnz	w3,.c
	str	w2,[x20,MSG_LEN]
	mov	x0,1
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.mfile:	mov	x0,PIPE_E_MFILE
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

macro pipes_data {
		align 8
current_req	dd 0
pipes		rb PIPE_MAX * PIPE_SIZE
ends		rb END_MAX * END_SIZE
owners		rb OWN_MAX * 8			; couples (bout, pid)
waits		rb WAIT_MAX * WAIT_SIZE
pr_blocks	dd 0				; fiches des process (pids.inc)
rmsg		rb MSG_TAILLE
msg_save	rb MSG_TAILLE
pipe_data	rb PIPE_MAX * PIPE_CAP
pipe_chemins	rb PIPE_MAX * PIPE_CHEMIN
}
