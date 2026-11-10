; sh_run.inc - sh : analyse et execution (docs/SHELL.md)
;
; Pas d'arbre : le texte est analyse en s'executant, x28 est le curseur dans le texte. noexec > 0 :
; on analyse sans rien lancer (branche non prise, tranche d'un tube, verification avant
; d'executer) ; une boucle revient en arriere en remettant x28. Tout ce qui demande un autre
; process sans fork (tube, ( ... ), $(...), compose redirige) relance sh -c sur sa tranche de texte.

RED_MAX		= 8
R_IN		= 0
R_OUT		= 1
R_APP		= 2
R_DUP		= 3
RE_FD		= 0
RE_TYPE		= 8
RE_CIBLE	= 16
RE_TAILLE	= 24
AS_MAX		= 32			; affectations devant une commande
AS_TAILLE	= 24
TUBE_MAX	= 32			; commandes d'un tube

; ---------------------------------------------------------------------------
;  Lexique
; ---------------------------------------------------------------------------

; espaces, suites de ligne, commentaire
skip_blank:
.c:	ldrb	w0,[x28]
	cmp	w0,' '
	b.eq	.n
	cmp	w0,9
	b.eq	.n
	cmp	w0,13
	b.eq	.n
	cmp	w0,'\'
	b.ne	.h
	ldrb	w1,[x28,1]
	cmp	w1,10
	b.ne	.r
	add	x28,x28,2
	b	.c
.h:	cmp	w0,'#'
	b.ne	.r
.d:	ldrb	w0,[x28]
	cbz	w0,.r
	cmp	w0,10
	b.eq	.r
	add	x28,x28,1
	b	.d
.n:	add	x28,x28,1
	b	.c
.r:	ret

; espaces, fins de ligne et « ; » (pas « ;; »)
skip_seps:
	prologue
.c:	bl	skip_blank
	ldrb	w0,[x28]
	cmp	w0,10
	b.eq	.n
	cmp	w0,';'
	b.ne	.r
	ldrb	w1,[x28,1]
	cmp	w1,';'
	b.eq	.r
.n:	add	x28,x28,1
	b	.c
.r:	epilogue

; x0 = mot -> x0 = 1 si le texte en x28 est ce mot entier
est_mot:
	mov	x1,0
.c:	ldrb	w2,[x0,x1]
	cbz	w2,.f
	ldrb	w3,[x28,x1]
	cmp	w2,w3
	b.ne	.non
	add	x1,x1,1
	b	.c
.f:	ldrb	w3,[x28,x1]
	mov	x0,1
	cbz	w3,.r
	cmp	w3,' '
	b.eq	.r
	cmp	w3,9
	b.eq	.r
	cmp	w3,10
	b.eq	.r
	cmp	w3,13
	b.eq	.r
	cmp	w3,';'
	b.eq	.r
	cmp	w3,'&'
	b.eq	.r
	cmp	w3,'|'
	b.eq	.r
	cmp	w3,')'
	b.eq	.r
.non:	mov	x0,0
.r:	ret

; x0 = mot -> x0 = 1 et le mot passe, s'il est la
prend_mot:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	est_mot
	cbz	x0,.out
	mov	x0,x19
	bl	str_len
	add	x28,x28,x0
	mov	x0,1
.out:	ldr	x19,[sp],16
	epilogue

; x0 = mot obligatoire
attendre_mot:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	skip_seps
	mov	x0,x19
	bl	prend_mot
	cbnz	x0,.out
	bl	erreur_syntaxe
.out:	ldr	x19,[sp],16
	epilogue

erreur_syntaxe:
	prologue
	adr	x0,incomplet			; (fin du texte : une ligne de plus, en interactif)
	ldrb	w1,[x28]
	cbnz	w1,.vrai
	mov	x1,1
	str	x1,[x0]
.vrai:	adr	x0,syntaxe
	ldr	x1,[x0]
	cbnz	x1,.out
	mov	x1,1
	str	x1,[x0]
.out:	epilogue

; -> x0 = 1 si une liste s'arrete ici
a_la_fin:
	prologue
	mov	x0,1
	ldrb	w1,[x28]
	cbz	w1,.out
	cmp	w1,')'
	b.eq	.out
	cmp	w1,';'
	b.ne	.m
	ldrb	w2,[x28,1]
	cmp	w2,';'
	b.eq	.out
.m:	adr	x0,k_then
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_elif
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_else
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_fi
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_do
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_done
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_esac
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_accf
	bl	est_mot
.out:	epilogue

; ---------------------------------------------------------------------------
;  Listes
; ---------------------------------------------------------------------------

; une suite de commandes, jusqu'a un mot reserve, « ) », « ;; » ou la fin
do_list:
	prologue
	str	x19,[sp,-16]!
.c:	bl	skip_seps
	bl	a_la_fin
	cbnz	x0,.out
	adr	x0,syntaxe
	ldr	x0,[x0]
	cbnz	x0,.out
	mov	x19,x28
	adr	x0,saut				; (break, continue : le reste est seulement lu)
	ldr	x0,[x0]
	cbz	x0,.lance
	bl	noexec_plus
	bl	do_andor
	bl	noexec_moins
	b	.lu
.lance:	bl	do_andor
.lu:
	bl	skip_blank
	ldrb	w0,[x28]
	cmp	w0,'&'				; (pas d'arriere-plan : comme « ; »)
	b.ne	.e
	add	x28,x28,1
.e:	cmp	x28,x19				; rien de lu : un caractere inattendu
	b.ne	.err
	bl	erreur_syntaxe
	b	.out
.err:	adr	x0,noexec
	ldr	x0,[x0]
	cbnz	x0,.c
	adr	x0,cond
	ldr	x0,[x0]
	cbnz	x0,.c
	adr	x0,opt_e			; set -e
	ldr	x0,[x0]
	cbz	x0,.c
	adr	x0,chaine_ok
	ldr	x0,[x0]
	cbnz	x0,.c
	adr	x0,last_code
	ldr	x0,[x0]
	cbz	x0,.c
	bl	sh_sortie
.out:	ldr	x19,[sp],16
	epilogue

; pipeline { && pipeline | || pipeline }
do_andor:
	prologue
	str	x19,[sp,-16]!
	adr	x0,chaine_ok
	str	xzr,[x0]
	bl	do_pipeline
.c:	bl	skip_blank
	ldrb	w0,[x28]
	ldrb	w1,[x28,1]
	cmp	w0,w1
	b.ne	.out
	cmp	w0,'&'
	b.eq	.et
	cmp	w0,'|'
	b.ne	.out
	mov	x19,1				; ||
	b	.op
.et:	mov	x19,0				; &&
.op:	add	x28,x28,2
	bl	skip_seps
	adr	x0,chaine_ok			; (set -e : pas ce qui est a gauche)
	mov	x1,1
	str	x1,[x0]
	adr	x0,last_code
	ldr	x0,[x0]
	cmp	x0,0
	cset	x0,ne				; 1 : echec
	cmp	x0,x19
	b.eq	.lance
	adr	x0,noexec			; a passer
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	bl	do_pipeline
	adr	x0,noexec
	ldr	x1,[x0]
	sub	x1,x1,1
	str	x1,[x0]
	b	.c
.lance:	adr	x0,chaine_ok
	str	xzr,[x0]
	bl	do_pipeline
	b	.c
.out:	ldr	x19,[sp],16
	epilogue

; x28 sur « | » seul ?  -> x0 = 1
est_tube:
	ldrb	w0,[x28]
	cmp	w0,'|'
	b.ne	.non
	ldrb	w0,[x28,1]
	cmp	w0,'|'
	b.eq	.non
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

noexec_plus:
	adr	x0,noexec
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	ret
noexec_moins:
	adr	x0,noexec
	ldr	x1,[x0]
	sub	x1,x1,1
	str	x1,[x0]
	ret

; [!] commande { | commande }
do_pipeline:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,TUBE_MAX*32
	bl	skip_blank
	mov	x19,0				; 1 : « ! »
	ldrb	w0,[x28]
	cmp	w0,'!'
	b.ne	.pas
	ldrb	w0,[x28,1]
	cmp	w0,' '
	b.ne	.pas
	mov	x19,1
	add	x28,x28,1
	bl	skip_blank
.pas:	adr	x0,noexec
	ldr	x0,[x0]
	cbz	x0,.exec
.an:	bl	do_command			; analyse seule
	bl	skip_blank
	bl	est_tube
	cbz	x0,.out
	add	x28,x28,1
	bl	skip_seps
	b	.an
.exec:	mov	x20,x28				; les tranches : debut, fin
	mov	x21,0				; commandes
	bl	noexec_plus
.t:	mov	x22,x28
	bl	do_command
	bl	skip_blank
	cmp	x21,TUBE_MAX
	b.hs	.tf
	mov	x0,sp
	lsl	x1,x21,4
	add	x0,x0,x1
	stp	x22,x28,[x0]
	add	x21,x21,1
	bl	est_tube
	cbz	x0,.tf
	add	x28,x28,1
	bl	skip_seps
	b	.t
.tf:	bl	noexec_moins
	adr	x0,syntaxe
	ldr	x0,[x0]
	cbnz	x0,.out
	cmp	x21,1
	b.ne	.tube
	mov	x24,x28				; une seule : executee ici
	mov	x28,x20
	bl	do_command
	mov	x28,x24
	b	.neg
.tube:	mov	x24,x28
	mov	x22,0				; rang
	mov	x23,-1				; bout de lecture du precedent
.u:	sub	x0,x21,1
	cmp	x22,x0
	b.hs	.dern
	add	x0,sp,TUBE_MAX*16
	bl	f_pipe
	tbnz	x0,63,.echec
	b	.lc
.dern:	add	x1,sp,TUBE_MAX*16		; dernier : notre sortie
	mov	x2,-1
	mov	x0,1
	stp	x2,x0,[x1]
.lc:	mov	x0,sp
	lsl	x1,x22,4
	add	x0,x0,x1
	ldp	x28,x4,[x0]			; (x28 : la tranche ; x4 : sa fin)
	add	x1,sp,TUBE_MAX*16
	ldr	x2,[x1,8]
	adr	x3,tube_in
	str	x23,[x3]
	adr	x3,tube_out
	str	x2,[x3]
	adr	x3,tube_pid
	str	xzr,[x3]
	mov	x0,x4
	bl	lance_membre
	add	x3,sp,TUBE_MAX*24		; pids
	str	x0,[x3,x22,lsl 3]
	bl	job_fg				; (pty : le groupe du tube)
	tbnz	x23,63,.pf
	mov	x0,x23
	sys	SYS_CLOSE
.pf:	add	x1,sp,TUBE_MAX*16
	ldp	x23,x0,[x1]			; lecture, ecriture
	cmp	x0,3
	b.lo	.pg
	sys	SYS_CLOSE
.pg:	add	x22,x22,1
	cmp	x22,x21
	b.lo	.u
	mov	x22,0				; tous attendus, le dernier donne le code
.w:	add	x3,sp,TUBE_MAX*24
	ldr	x0,[x3,x22,lsl 3]
	tbnz	x0,63,.wn
	bl	attendre
.wn:	add	x22,x22,1
	cmp	x22,x21
	b.lo	.w
	bl	job_fin
	mov	x28,x24
	b	.neg
.echec:	mov	x28,x24
	mov	x0,1
	adr	x1,last_code
	str	x0,[x1]
.neg:	cbz	x19,.out
	adr	x1,last_code
	ldr	x0,[x1]
	cmp	x0,0
	cset	x0,eq
	str	x0,[x1]
.out:	add	sp,sp,TUBE_MAX*32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x28 = debut de la tranche d'un tube, x0 = sa fin : lancee sans attendre -> x0 = pid
; (une commande simple directement ; sinon, un sh -c)
lance_membre:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x28
	adr	x0,dans_tube
	mov	x1,1
	str	x1,[x0]
	bl	skip_blank
	bl	est_compose
	cbnz	x0,.sh
	bl	do_simple
	adr	x0,tube_pid
	ldr	x0,[x0]
	cbnz	x0,.fin
.sh:	sub	sp,sp,32			; compose, ou commande integree
	mov	x0,sp
	bl	fds_defaut
	mov	x0,x20
	mov	x1,x19
	mov	x2,sp
	bl	lance_tranche
	add	sp,sp,32
.fin:	adr	x1,dans_tube
	str	xzr,[x1]
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Commandes
; ---------------------------------------------------------------------------

; -> x0 = 1 si x28 est sur une commande composee
est_compose:
	prologue
	ldrb	w0,[x28]
	cmp	w0,'('
	b.eq	.oui
	adr	x0,k_if
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_while
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_until
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_for
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_case
	bl	est_mot
	cbnz	x0,.out
	adr	x0,k_acco
	bl	est_mot
	b	.out
.oui:	mov	x0,1
.out:	epilogue

; une commande, simple ou composee
do_command:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	skip_blank
	bl	est_compose
	cbz	x0,.simple
	adr	x0,noexec
	ldr	x0,[x0]
	cbnz	x0,.comp
	mov	x19,x28				; suivie d'une redirection, ou ( ... ) :
	bl	noexec_plus			; dans un autre process
	bl	compose
	bl	noexec_moins
	mov	x20,x28
	bl	skip_blank
	bl	est_redir
	cbnz	x0,.autre
	ldrb	w0,[x19]
	cmp	w0,'('
	b.eq	.autre
	mov	x28,x19
.comp:	bl	compose
	b	.out
.autre:	mov	x0,x19
	mov	x1,x20
	ldrb	w2,[x19]
	cmp	w2,'('
	b.ne	.ap
	add	x0,x0,1				; (l'interieur des parentheses)
	sub	x1,x1,1
.ap:	bl	compose_a_part
	b	.out
.simple:
	bl	do_simple
.out:	ldp	x19,x20,[sp],16
	epilogue

compose:
	prologue
	ldrb	w0,[x28]
	cmp	w0,'('
	b.eq	do_sous
	adr	x0,k_if
	bl	est_mot
	cbnz	x0,do_if
	adr	x0,k_while
	bl	est_mot
	cbnz	x0,do_while
	adr	x0,k_until
	bl	est_mot
	cbnz	x0,do_while
	adr	x0,k_for
	bl	est_mot
	cbnz	x0,do_for
	adr	x0,k_case
	bl	est_mot
	cbnz	x0,do_case
	b	do_acco

; (analyse seule, ou dans un process a part) ( liste )
do_sous:
	add	x28,x28,1
	bl	do_list
	ldrb	w0,[x28]
	cmp	w0,')'
	b.eq	.ok
	bl	erreur_syntaxe
	epilogue
.ok:	add	x28,x28,1
	epilogue

do_acco:
	add	x28,x28,1
	bl	do_list
	adr	x0,k_accf
	bl	attendre_mot
	epilogue

; if liste then liste { elif liste then liste } [ else liste ] fi
do_if:
	stp	x19,x20,[sp,-16]!
	add	x28,x28,2
	mov	x19,0				; 1 : une branche a ete prise
.test:	cbz	x19,.t1
	bl	noexec_plus
.t1:	adr	x0,cond
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	bl	do_list
	adr	x0,cond
	ldr	x1,[x0]
	sub	x1,x1,1
	str	x1,[x0]
	cbz	x19,.t2
	bl	noexec_moins
.t2:	adr	x0,k_then
	bl	attendre_mot
	mov	x20,0				; a executer ?
	cbnz	x19,.corps
	adr	x0,last_code
	ldr	x0,[x0]
	cbnz	x0,.corps
	mov	x20,1
.corps:	cbnz	x20,.c1
	bl	noexec_plus
.c1:	bl	do_list
	cbnz	x20,.c2
	bl	noexec_moins
.c2:	orr	x19,x19,x20
	bl	skip_seps
	adr	x0,k_elif
	bl	prend_mot
	cbnz	x0,.test
	adr	x0,k_else
	bl	prend_mot
	cbz	x0,.fi
	cbz	x19,.e1
	bl	noexec_plus
.e1:	bl	do_list
	cbz	x19,.e2
	bl	noexec_moins
.e2:	mov	x19,1
.fi:	adr	x0,k_fi
	bl	attendre_mot
	cbnz	x19,.out
	adr	x0,last_code			; aucune branche : 0
	str	xzr,[x0]
.out:	ldp	x19,x20,[sp],16
	epilogue

; while / until liste do liste done
do_while:
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,0
	ldrb	w0,[x28]
	cmp	w0,'u'
	cset	x20,eq				; 1 : until
	add	x28,x28,5
	mov	x19,x28
	mov	x21,0				; code du corps
.test:	mov	x28,x19
	adr	x0,cond
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	bl	do_list
	adr	x0,cond
	ldr	x1,[x0]
	sub	x1,x1,1
	str	x1,[x0]
	adr	x0,k_do
	bl	attendre_mot
	adr	x0,noexec
	ldr	x0,[x0]
	cbnz	x0,.passe
	adr	x0,last_code
	ldr	x0,[x0]
	cmp	x0,0
	cset	x0,eq				; 1 : test reussi
	cmp	x0,x20
	b.eq	.passe				; (while echoue, until reussit)
	bl	do_list
	adr	x0,last_code
	ldr	x21,[x0]
	adr	x0,saut
	ldr	x1,[x0]
	cbz	x1,.test
	cmp	x1,1
	b.ne	.brk				; break : 2, continue : 1
	str	xzr,[x0]
	b	.test
.brk:	str	xzr,[x0]
.passe:	bl	noexec_plus
	bl	do_list
	bl	noexec_moins
	adr	x0,k_done
	bl	attendre_mot
	adr	x0,noexec
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x0,last_code
	str	x21,[x0]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; for NOM [ in mots ] ; do liste done
do_for:
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	add	x28,x28,3
	bl	skip_blank
	mov	x19,x28				; le nom
	bl	nom_passer
	sub	x20,x28,x19
	adr	x0,ar_p
	ldr	x23,[x0]			; marques
	adr	x0,at_p
	ldr	x21,[x0]
	bl	skip_seps
	adr	x0,k_in
	bl	prend_mot
	cbz	x0,.pos
.m:	bl	skip_blank
	ldrb	w0,[x28]
	bl	fin_mot
	cbnz	x1,.mf
	adr	x0,noexec
	ldr	x0,[x0]
	cbnz	x0,.ms
	mov	x0,0
	bl	mot_etendre
	b	.m
.ms:	bl	mot_sauter
	b	.m
.pos:	adr	x0,pos_n			; sans « in » : "$@"
	ldr	x5,[x0]
	adr	x0,pos_v
	ldr	x3,[x0]
	mov	x4,1
.pc:	cmp	x4,x5
	b.hi	.mf
	ldr	x0,[x3,x4,lsl 3]
	bl	at_pousse
	add	x4,x4,1
	b	.pc
.mf:	adr	x0,at_p
	ldr	x22,[x0]			; fin de la liste
	mov	x24,x21				; courant
	adr	x0,k_do
	bl	attendre_mot
	mov	x0,x28
	str	x0,[sp,-16]!			; le corps
	adr	x0,noexec
	ldr	x0,[x0]
	cbnz	x0,.passe
	adr	x0,last_code
	str	xzr,[x0]
.it:	cmp	x24,x22
	b.hs	.passe
	ldr	x2,[x24],8
	mov	x0,x19
	mov	x1,x20
	mov	x3,0
	bl	var_pose
	ldr	x28,[sp]
	bl	do_list
	adr	x0,saut
	ldr	x1,[x0]
	cbz	x1,.it
	str	xzr,[x0]
	cmp	x1,1
	b.eq	.it
.passe:	ldr	x28,[sp],16
	bl	noexec_plus
	bl	do_list
	bl	noexec_moins
	adr	x0,k_done
	bl	attendre_mot
	adr	x0,at_p
	str	x21,[x0]
	adr	x0,ar_p
	str	x23,[x0]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; case mot in motif { | motif } ) liste ;; ... esac
do_case:
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	add	x28,x28,4
	bl	skip_blank
	adr	x0,ar_p
	ldr	x21,[x0]
	mov	x19,0				; le mot
	adr	x0,noexec
	ldr	x0,[x0]
	cbnz	x0,.ms
	mov	x0,1
	bl	mot_etendre
	mov	x19,x0
	b	.in
.ms:	bl	mot_sauter
.in:	bl	skip_seps
	adr	x0,k_in
	bl	prend_mot
	cbnz	x0,.vu
	bl	erreur_syntaxe
	b	.out
.vu:	mov	x20,0				; 1 : branche prise
.br:	bl	skip_seps
	adr	x0,k_esac
	bl	prend_mot
	cbnz	x0,.out
	ldrb	w0,[x28]
	cbz	w0,.err
	cmp	w0,'('
	b.ne	.pat
	add	x28,x28,1
.pat:	mov	x22,0				; ce motif correspond ?
.p:	bl	skip_blank
	adr	x0,noexec
	ldr	x0,[x0]
	cbnz	x0,.ps
	cbz	x19,.ps
	cbnz	x20,.ps
	cbnz	x22,.ps
	mov	x0,2
	bl	mot_etendre
	mov	x1,x19
	bl	motif_ok
	mov	x22,x0
	b	.pn
.ps:	bl	mot_sauter
.pn:	bl	skip_blank
	ldrb	w0,[x28]
	cmp	w0,'|'
	b.ne	.pf
	add	x28,x28,1
	b	.p
.pf:	cmp	w0,')'
	b.ne	.err
	add	x28,x28,1
	cbnz	x22,.corps
	bl	noexec_plus
	bl	do_list
	bl	noexec_moins
	b	.fb
.corps:	mov	x20,1
	bl	do_list
.fb:	bl	skip_seps
	ldrb	w0,[x28]
	cmp	w0,';'
	b.ne	.br
	add	x28,x28,2
	b	.br
.err:	bl	erreur_syntaxe
.out:	adr	x0,ar_p
	str	x21,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = tranche du compose (x28 sur ses redirections) : un sh -c a part
compose_a_part:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x20,x1
	adr	x0,ar_p
	ldr	x21,[x0]
	adr	x0,reds_n
	str	xzr,[x0]
.r:	bl	skip_blank
	bl	est_redir
	cbz	x0,.lance
	bl	lire_redir
	b	.r
.lance:	mov	x0,sp
	bl	fds_preparer
	tbnz	x0,63,.fail
	mov	x0,x19
	mov	x1,x20
	mov	x2,sp
	bl	lance_tranche
	mov	x19,x0
	bl	fds_fermer
	mov	x0,x19
	tbnz	x0,63,.echec
	bl	job_fg
	mov	x0,x19
	bl	attendre
	bl	job_fin
	b	.out
.echec:	bl	erreur_code
	b	.out
.fail:	mov	x0,1
	adr	x1,last_code
	str	x0,[x1]
.out:	adr	x0,ar_p
	str	x21,[x0]
	add	sp,sp,32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Redirections
; ---------------------------------------------------------------------------

; -> x0 = 1 si x28 est sur une redirection (chiffres puis < ou >)
est_redir:
	mov	x1,x28
.c:	ldrb	w0,[x1]
	sub	w2,w0,'0'
	cmp	w2,9
	b.hi	.f
	add	x1,x1,1
	b	.c
.f:	cmp	w0,'<'
	b.eq	.oui
	cmp	w0,'>'
	b.eq	.oui
	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; x28 sur une redirection : ajoutee a reds (mot developpe)
lire_redir:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	mov	x20,0				; 1 : un chiffre etait la
.c:	ldrb	w0,[x28]
	sub	w1,w0,'0'
	cmp	w1,9
	b.hi	.op
	mov	x2,10
	mul	x19,x19,x2
	add	x19,x19,x1
	mov	x20,1
	add	x28,x28,1
	b	.c
.op:	add	x28,x28,1
	ldrb	w1,[x28]
	mov	x2,R_IN
	cmp	w0,'<'
	b.eq	.fd
	mov	x2,R_OUT
	cmp	w1,'>'
	b.ne	.fd
	mov	x2,R_APP
	add	x28,x28,1
.fd:	cbnz	x20,.dup
	cmp	w0,'>'
	cset	x19,eq				; < : 0, > : 1
.dup:	ldrb	w1,[x28]
	cmp	w1,'&'
	b.ne	.mot
	add	x28,x28,1
	mov	x2,R_DUP
.mot:	stp	x2,x2,[sp,-16]!
	bl	skip_blank
	adr	x0,noexec
	ldr	x0,[x0]
	cbz	x0,.et
	bl	mot_sauter
	add	sp,sp,16
	b	.out
.et:	ldrb	w0,[x28]
	bl	fin_mot
	cbz	x1,.ok
	add	sp,sp,16
	bl	erreur_syntaxe
	b	.out
.ok:	mov	x0,1
	bl	mot_etendre
	ldr	x2,[sp],16
	adr	x1,reds_n
	ldr	x3,[x1]
	cmp	x3,RED_MAX
	b.hs	.out
	add	x4,x3,1
	str	x4,[x1]
	mov	x4,RE_TAILLE
	mul	x3,x3,x4
	adr	x4,reds
	add	x3,x3,x4
	str	x19,[x3,RE_FD]
	str	x2,[x3,RE_TYPE]
	str	x0,[x3,RE_CIBLE]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = &fds[3] : entree, sortie, erreur par defaut d'un fils
fds_defaut:
	adr	x1,interactif
	ldr	x1,[x1]
	adr	x2,tty				; (sur un pty : le fils a le terminal)
	ldr	x2,[x2]
	cmp	x2,0
	csel	x1,xzr,x1,ne
	mov	x2,-1				; interactif sur un tube : son entree est la
	mov	x3,0				; notre, le fils n'y lit rien
	cmp	x1,0
	csel	x2,x3,x2,eq
	mov	x3,1
	stp	x2,x3,[x0]
	mov	x3,2
	str	x3,[x0,16]
	adr	x1,dans_tube
	ldr	x1,[x1]
	cbz	x1,.r
	adr	x1,tube_in
	ldr	x2,[x1]
	tbnz	x2,63,.o
	str	x2,[x0]
.o:	adr	x1,tube_out
	ldr	x2,[x1]
	str	x2,[x0,8]
.r:	ret

; x0 = &fds[3] : remplis, redirections comprises -> x0 = 0, < 0 si un fichier a echoue (message
; ecrit). Les descripteurs ouverts sont dans red_d (fds_fermer les rend).
fds_preparer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	fds_defaut
	adr	x0,red_dn
	str	xzr,[x0]
	adr	x20,reds
	adr	x21,reds_n
	ldr	x21,[x21]
.r:	cbz	x21,.ok
	ldr	x22,[x20,RE_FD]
	cmp	x22,2
	b.hi	.n
	ldr	x0,[x20,RE_TYPE]
	cmp	x0,R_DUP
	b.ne	.fic
	ldr	x0,[x20,RE_CIBLE]
	ldrb	w1,[x0]
	sub	w1,w1,'0'
	cmp	w1,2
	b.hi	.n
	ldr	x0,[x19,x1,lsl 3]
	str	x0,[x19,x22,lsl 3]
	b	.n
.fic:	ldr	x0,[x20,RE_TYPE]
	cmp	x0,R_IN
	b.eq	.ouv
	ldr	x0,[x20,RE_CIBLE]		; cree (ou vide) ici
	adr	x1,s_devnull
	bl	str_eq
	cbnz	x0,.ouv
	ldr	x0,[x20,RE_CIBLE]
	ldr	x2,[x20,RE_TYPE]
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	cmp	x2,R_OUT
	b.eq	.o1
	mov	x1,O_WRITE or O_CREATE
.o1:	sys	SYS_OPEN
	tbnz	x0,63,.err
	sys	SYS_CLOSE
.ouv:	ldr	x0,[x20,RE_CIBLE]
	ldr	x2,[x20,RE_TYPE]
	mov	x1,O_READ
	cmp	x2,R_IN
	b.eq	.o2
	mov	x1,O_WRITE
.o2:	bl	f_redir
	tbnz	x0,63,.err
	str	x0,[x19,x22,lsl 3]
	adr	x1,red_dn
	ldr	x2,[x1]
	add	x3,x2,1
	str	x3,[x1]
	adr	x1,red_d
	str	x0,[x1,x2,lsl 3]
.n:	add	x20,x20,RE_TAILLE
	sub	x21,x21,1
	b	.r
.err:	ldr	x0,[x20,RE_CIBLE]
	bl	err_fichier
	bl	fds_fermer
	mov	x0,-1
	b	.out
.ok:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

fds_fermer:
	prologue
	str	x19,[sp,-16]!
	adr	x0,red_dn
	ldr	x19,[x0]
	str	xzr,[x0]
.c:	cbz	x19,.out
	sub	x19,x19,1
	adr	x0,red_d
	ldr	x0,[x0,x19,lsl 3]
	sys	SYS_CLOSE
	b	.c
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Commande simple
; ---------------------------------------------------------------------------

; -> x0 = longueur du nom si x28 est sur NOM=... , 0 sinon
est_affect:
	mov	x0,0
.c:	ldrb	w1,[x28,x0]
	cmp	w1,'='
	b.eq	.eg
	cmp	w1,'_'
	b.eq	.n
	sub	w2,w1,'0'
	cmp	w2,9
	b.ls	.ch
	orr	w2,w1,0x20
	sub	w2,w2,'a'
	cmp	w2,25
	b.hi	.non
.n:	add	x0,x0,1
	b	.c
.ch:	cbnz	x0,.n				; (pas de chiffre en tete)
.non:	mov	x0,0
.eg:	ret

do_simple:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,AS_MAX*AS_TAILLE+32
	mov	x19,x28				; debut, pour une tranche
	adr	x0,ar_p
	ldr	x20,[x0]
	adr	x0,at_p
	ldr	x21,[x0]
	mov	x23,0				; affectations
	adr	x0,reds_n
	str	xzr,[x0]
	adr	x0,subst_code
	str	xzr,[x0]
.b:	bl	skip_blank
	bl	est_redir
	cbz	x0,.mot
	bl	lire_redir
	b	.b
.mot:	ldrb	w0,[x28]
	bl	fin_mot
	cbnz	x1,.fin
	adr	x0,noexec
	ldr	x0,[x0]
	cbz	x0,.vrai
	bl	mot_sauter
	b	.b
.vrai:	adr	x0,at_p				; une commande deja vue ?
	ldr	x0,[x0]
	cmp	x0,x21
	b.ne	.arg
	bl	est_affect
	cbz	x0,.arg
	mov	x22,x0
	cmp	x23,AS_MAX
	b.hs	.arg
	mov	x24,x28
	add	x28,x28,x22
	add	x28,x28,1
	mov	x0,1
	bl	mot_etendre
	mov	x1,AS_TAILLE
	mul	x1,x23,x1
	add	x1,x1,sp
	stp	x24,x22,[x1]
	str	x0,[x1,16]
	add	x23,x23,1
	b	.b
.arg:	mov	x0,0
	bl	mot_etendre
	b	.b
.fin:	mov	x26,x28				; fin de la tranche
	adr	x0,noexec
	ldr	x0,[x0]
	cbnz	x0,.rest
	adr	x0,syntaxe
	ldr	x0,[x0]
	cbnz	x0,.rest
	adr	x0,at_p
	ldr	x24,[x0]
	cmp	x24,x21
	b.ne	.cmd
	mov	x24,0				; pas de commande : les affectations
.af:	cmp	x24,x23
	b.hs	.afin
	mov	x1,AS_TAILLE
	mul	x1,x24,x1
	add	x1,x1,sp
	ldr	x0,[x1]
	ldr	x2,[x1,16]
	ldr	x1,[x1,8]
	mov	x3,0
	bl	var_pose
	add	x24,x24,1
	b	.af
.afin:	adr	x0,subst_code
	ldr	x0,[x0]
	adr	x1,last_code
	str	x0,[x1]
	b	.rest
.cmd:	mov	x0,0				; argv termine
	bl	at_pousse
	mov	x24,x21				; argv
	adr	x0,opt_x
	ldr	x0,[x0]
	cbz	x0,.pasx
	mov	x0,x24
	bl	trace
.pasx:	ldr	x0,[x24]
	bl	integree_cherche
	mov	x25,x0
	cbz	x25,.ext
	adr	x0,dans_tube
	ldr	x0,[x0]
	cbnz	x0,.rest			; (dans un tube : lance_membre fait un sh -c)
	mov	x22,0				; affectations : gardees
.ia:	cmp	x22,x23
	b.hs	.ib
	mov	x1,AS_TAILLE
	mul	x1,x22,x1
	add	x1,x1,sp
	ldr	x0,[x1]
	ldr	x2,[x1,16]
	ldr	x1,[x1,8]
	mov	x3,0
	bl	var_pose
	add	x22,x22,1
	b	.ia
.ib:	mov	x0,x25
	mov	x1,x24
	bl	integree_lance
	b	.rest
.ext:	mov	x0,x24
	mov	x1,x23
	mov	x2,sp
	bl	lance_externe
.rest:	mov	x28,x26
	adr	x0,ar_p
	str	x20,[x0]
	adr	x0,at_p
	str	x21,[x0]
	add	sp,sp,AS_MAX*AS_TAILLE+32
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = argv : le programme, ses redirections et l'environnement ; x1 = affectations, x2 = leur
; tableau ; attendu, sauf dans un tube (tube_pid)
lance_externe:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldr	x0,[x19]
	bl	prog_trouver
	mov	x22,x0
	cbnz	x22,.trouve
	ldr	x0,[x19]			; anciennes commandes du terminal
	bl	ancienne_cherche
	cbz	x0,.brut
	mov	x22,x0
	adr	x0,dans_tube
	ldr	x0,[x0]
	cbnz	x0,.out
	mov	x0,x22
	mov	x1,x19
	bl	integree_lance
	b	.out
.brut:	ldr	x22,[x19]			; (SD0:/apps/<nom>.app : vfs)
.trouve:
	mov	x0,sp
	bl	fds_preparer
	tbnz	x0,63,.fail
	adr	x0,at_p				; l'environnement : affectations d'abord
	ldr	x23,[x0]
	mov	x24,0
.a:	cmp	x24,x20
	b.hs	.e
	mov	x1,AS_TAILLE
	mul	x1,x24,x1
	add	x1,x1,x21
	ldp	x2,x3,[x1]			; nom, longueur
	ldr	x4,[x1,16]
	adr	x0,ar_p
	ldr	x5,[x0]
	mov	x6,x5
.n:	cbz	x3,.eg
	ldrb	w7,[x2],1
	strb	w7,[x5],1
	sub	x3,x3,1
	b	.n
.eg:	mov	w7,'='
	strb	w7,[x5],1
.v:	ldrb	w7,[x4],1
	strb	w7,[x5],1
	cbnz	w7,.v
	str	x5,[x0]
	mov	x0,x6
	bl	at_pousse
	add	x24,x24,1
	b	.a
.e:	mov	x0,0
	bl	env_tableau
	bl	flush
	mov	x0,x22
	mov	x1,x19
	mov	x2,x23
	mov	x3,sp
	bl	f_spawn_env
	mov	x24,x0
	bl	fds_fermer
	adr	x0,at_p
	str	x23,[x0]
	mov	x0,x24
	tbnz	x0,63,.err
	adr	x1,dans_tube
	ldr	x1,[x1]
	cbz	x1,.att
	adr	x1,tube_pid
	str	x0,[x1]
	b	.out
.att:	mov	x24,x0
	bl	job_fg
	mov	x0,x24
	bl	attendre
	bl	job_fin
	b	.out
.err:	cmn	x0,2
	b.ne	.autre
	ldr	x0,[x19]
	bl	err_introuvable
	mov	x0,127
	b	.code
.autre:	bl	erreur_code
	b	.out
.fail:	mov	x0,1
.code:	adr	x1,last_code
	str	x0,[x1]
.out:	add	sp,sp,32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom -> x0 = chemin a lancer (PATH), 0 si aucun ; le nom seul s'il contient / ou :
prog_trouver:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x0,x19
.t:	ldrb	w1,[x0],1
	cbz	w1,.path
	cmp	w1,'/'
	b.eq	.lui
	cmp	w1,':'
	b.ne	.t
.lui:	mov	x0,x19
	b	.out
.path:	adr	x0,s_PATH
	mov	x1,4
	bl	var_valeur
	mov	x20,x0
	cbz	x20,.non
.e:	ldrb	w0,[x20]
	cbz	w0,.non
	adr	x21,chemin_tmp			; dossier/nom
	mov	x22,0
.d:	ldrb	w0,[x20]
	cbz	w0,.df
	add	x20,x20,1
	cmp	w0,':'
	b.eq	.df
	cmp	x22,200
	b.hs	.d
	strb	w0,[x21,x22]
	add	x22,x22,1
	b	.d
.df:	cbz	x22,.e
	mov	w0,'/'
	strb	w0,[x21,x22]
	add	x22,x22,1
	mov	x1,x19
.n:	ldrb	w0,[x1],1
	strb	w0,[x21,x22]
	add	x22,x22,1
	cmp	x22,250
	b.hs	.e
	cbnz	w0,.n
	mov	x0,x21
	mov	x1,sp
	sys	SYS_STAT
	tbnz	x0,63,.e
	ldr	x0,[sp,8]
	tst	x0,ATTR_DIR
	b.ne	.e
	mov	x0,x21
	b	.out
.non:	mov	x0,0
.out:	add	sp,sp,16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = tranche de texte, x2 = &fds[3] : lancee par sh -c, sans attendre -> x0 = pid, < 0
lance_tranche:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x0,x1,[sp,-16]!
	mov	x19,x2
	bl	flush
	ldp	x0,x1,[sp],16
	adr	x2,tranche_p			; la tranche, terminee par 0
	ldr	x2,[x2]
	mov	x3,x2
.c:	cmp	x0,x1
	b.hs	.f
	ldrb	w4,[x0],1
	strb	w4,[x3],1
	b	.c
.f:	strb	wzr,[x3]
	adr	x0,at_p
	ldr	x20,[x0]
	str	x2,[sp,-16]!
	adr	x0,s_sh
	bl	at_pousse
	adr	x0,opt_e
	ldr	x0,[x0]
	cbz	x0,.pe
	adr	x0,s_ec
	b	.pc
.pe:	adr	x0,s_c
.pc:	bl	at_pousse
	ldr	x0,[sp],16
	bl	at_pousse
	adr	x0,pos_n			; $0, $1...
	ldr	x5,[x0]
	adr	x0,pos_v
	ldr	x3,[x0]
	mov	x4,0
.p:	cmp	x4,x5
	b.hi	.pf
	ldr	x0,[x3,x4,lsl 3]
	bl	at_pousse
	add	x4,x4,1
	b	.p
.pf:	mov	x0,0
	bl	at_pousse
	mov	x0,1
	bl	env_tableau
	mov	x2,x0
	adr	x0,p_sh
	mov	x1,x20
	mov	x3,x19
	bl	f_spawn_env
	adr	x1,at_p
	str	x20,[x1]
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = tranche, x2 = 1 : entre guillemets : sa sortie, sans les fins de ligne finales, posee
; dans le mot en cours
capturer:
	prologue
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	sub	sp,sp,48
	mov	x24,x0
	mov	x25,x1
	mov	x26,x2
	adr	x27,ar_p			; (les pointeurs d'at_p restent bons)
	mov	x0,sp
	bl	f_pipe
	tbnz	x0,63,.out
	add	x0,sp,16
	bl	fds_defaut
	ldr	x0,[sp,8]
	str	x0,[sp,24]			; sortie : le tube
	mov	x0,x24
	mov	x1,x25
	add	x2,sp,16
	bl	lance_tranche
	mov	x24,x0
	ldr	x0,[sp,8]
	sys	SYS_CLOSE
	adr	x25,cap_p			; lue en entier
	ldr	x25,[x25]
	mov	x27,x25
.l:	ldr	x0,[sp]
	mov	x1,x27
	mov	x2,4096
	sys	SYS_READ
	cmp	x0,0
	b.le	.fl
	add	x27,x27,x0
	b	.l
.fl:	ldr	x0,[sp]
	sys	SYS_CLOSE
.nl:	cmp	x27,x25
	b.ls	.z
	ldurb	w0,[x27,-1]
	cmp	w0,10
	b.ne	.z
	sub	x27,x27,1
	b	.nl
.z:	strb	wzr,[x27]
	tbnz	x24,63,.pose
	mov	x0,x24
	bl	attendre
	adr	x0,last_code
	ldr	x0,[x0]
	adr	x1,subst_code
	str	x0,[x1]
.pose:	mov	x0,x25
	mov	x1,x26
	bl	pose_valeur
.out:	add	sp,sp,48
	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	epilogue

; x0 = pid : sa fin (interactif : Ctrl+C sur notre entree l'arrete) ; last_code
attendre:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	flush
	adr	x0,interactif
	ldr	x0,[x0]
	cbz	x0,.fin
	adr	x0,tty				; pty : Ctrl+C est un signal (vterm)
	ldr	x0,[x0]
	cbnz	x0,.fin
.att:	mov	x0,50
	sys	SYS_SLEEP
	mov	x0,464
	mov	x1,1				; (sans attente)
	bl	in_read
	cmp	x0,0
	b.le	.vit
	bl	entree_garder
	cbz	x0,.vit
	mov	x0,x19
	sys	SYS_KILL
	tputs	s_ctrlc
.vit:	mov	x0,x19				; encore vivant ?
	bl	alive
	cbnz	x0,.att
.fin:	mov	x0,x19
	sys	SYS_WAIT
	adr	x1,last_code
	str	x0,[x1]
	adr	x1,interactif
	ldr	x1,[x1]
	cbz	x1,.out
	adr	x1,dans_tube
	ldr	x1,[x1]
	cbnz	x1,.out
	cbz	x0,.out
	mov	x19,x0
	bl	term_ensure_nl
	tdire	s_code
	mov	x0,x19
	bl	term_dec
	tputs	s_close
.out:	ldr	x19,[sp],16
	epilogue

; x0 = octets recus dans msg pendant qu'un programme tourne : gardes pour la suite (sh les lira
; ensuite comme des commandes) -> x0 = 1 si un Ctrl+C s'y trouve
entree_garder:
	adr	x1,msg
	add	x1,x1,48
	mov	x2,0				; Ctrl+C
	mov	x3,0
	adr	x4,pend_n
	ldr	x5,[x4]
	adr	x6,pend_i
	ldr	x7,[x6]
	cmp	x7,x5				; tout lu : le tampon repart de zero
	b.lo	.c
	mov	x5,0
	str	xzr,[x6]
.c:	cmp	x3,x0
	b.hs	.f
	ldrb	w7,[x1,x3]
	add	x3,x3,1
	cmp	w7,3
	b.ne	.g
	mov	x2,1
	b	.c
.g:	cmp	x5,PEND_MAX
	b.hs	.c
	adr	x8,pend
	strb	w7,[x8,x5]
	add	x5,x5,1
	b	.c
.f:	str	x5,[x4]
	mov	x0,x2
	ret

; x0 = pid -> x0 = 1 s'il est encore dans la liste des process
alive:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x1,iobuf2			; (SYS_PROC_INFO)
	sys	SYS_PROC_INFO
	cbnz	x0,.non
	adr	x1,iobuf2
	ldr	x2,[x1,56]			; (etat : 1 en fin de vie)
	cbnz	x2,.non
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue
