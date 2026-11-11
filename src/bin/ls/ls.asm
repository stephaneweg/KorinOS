; ls - liste un dossier : ls [-a] [-l] [-1] [-C] [--color[=QUAND]] [chemins], trie (docs/OUTILS.md)
; -l : « drwxr-xr-x taille nom » (le bit x : vfs, V_STATX) ; -C : en colonnes, triees de haut
; en bas comme GNU ls (largeur : le pty de la sortie, VT_GWINSZ, sinon $COLUMNS, sinon 80) ;
; --color : les couleurs de dircolors (LS_COLORS : di, ex, fi et *.ext). Sans option : un nom
; par ligne, sans couleur (pour les scripts).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../inc/vterm.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

OPT_A		= 1
OPT_L		= 2
OPT_COUL	= 4
OPT_C		= 8

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; OPT_*
	adr	x0,quand
	str	xzr,[x0]			; --color : jamais par defaut
.o:	cmp	x21,x19
	b.hs	.go
	ldr	x24,[x20,x21,lsl 3]
	mov	x0,x24
	bl	t_est_opt
	cbz	x0,.go
	add	x21,x21,1
	ldrb	w0,[x24,1]
	cmp	w0,'-'
	b.ne	.l
	mov	x0,x24				; --color[=QUAND], --
	bl	opt_longue
	cbz	x0,.o
	tbz	x0,63,.go
	mov	x0,2
	bl	t_fin
.l:	ldrb	w0,[x24,1]!
	cbz	w0,.o
	cmp	w0,'a'
	b.ne	.l2
	orr	x22,x22,OPT_A
	b	.l
.l2:	cmp	w0,'l'
	b.ne	.l3
	orr	x22,x22,OPT_L
	b	.l
.l3:	cmp	w0,'C'
	b.ne	.l4
	orr	x22,x22,OPT_C
	b	.l
.l4:	cmp	w0,'1'				; un par ligne : -C oublie
	b.ne	.l
	bic	x22,x22,OPT_C
	b	.l
.go:	bl	sortie_init			; couleur, largeur
	mov	x23,0				; code
	cmp	x21,x19
	b.lo	.a
	adr	x0,s_point
	bl	operande
	mov	x23,x0
	b	.fin
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
	mov	x0,x24
	bl	operande
	orr	x23,x23,x0
	b	.a
.fin:	bl	vider
	mov	x0,x23
	bl	t_fin

; x0 = « --... » -> x0 = 0 (option lue), 1 (« -- » : fin des options), < 0 (erreur dite)
opt_longue:
	prologue
	stp	x19,x25,[sp,-16]!
	mov	x19,x0
	ldrb	w1,[x19,2]
	mov	x0,1
	cbz	w1,.out				; « -- »
	add	x0,x19,2
	adr	x1,s_color
.p:	ldrb	w2,[x1],1			; « color » en tete ?
	cbz	w2,.pf
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.ne	.inconnue
	b	.p
.pf:	ldrb	w2,[x0]
	mov	x3,1				; « --color » seul : toujours
	cbz	w2,.pose
	cmp	w2,'='
	b.ne	.inconnue
	add	x19,x0,1
	adr	x25,t_quand			; « 1always », ... : la valeur, le mot
.m:	ldrb	w0,[x25]
	cbz	w0,.invalide
	mov	x0,x19
	add	x1,x25,1
	bl	str_eq
	cbnz	x0,.trouve
.s:	ldrb	w0,[x25],1			; mot suivant
	cbnz	w0,.s
	b	.m
.trouve:
	ldrb	w3,[x25]
	sub	x3,x3,'0'
.pose:	adr	x0,quand
	str	x3,[x0]
	mov	x0,0
	b	.out
.invalide:
	adr	x0,s_quand_inv
	bl	t_err_msg
	mov	x0,-1
	b	.out
.inconnue:
	adr	x0,s_opt_inv
	bl	t_err_msg
	mov	x0,-1
.out:	ldp	x19,x25,[sp],16
	epilogue

; la couleur (quand : 0 jamais, 1 toujours, 2 si la sortie est un terminal) et la
; largeur de -C : le pty de la sortie standard (V_STDIO : un bout de vterm), sinon $COLUMNS, 80
sortie_init:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,0				; bout de vterm de la sortie (0 : aucun)
	adr	x0,quand
	ldr	x0,[x0]
	cmp	x0,1
	b.ne	.q
	orr	x22,x22,OPT_COUL
.q:	tst	x22,OPT_C
	b.ne	.stdio
	cmp	x0,2
	b.ne	.larg
.stdio:	mov	x0,V_STDIO
	str	x0,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	t_vfs
	tbnz	x0,63,.auto
	ldr	x0,[sp,MSG_B]
	tst	x0,VT_TAG
	csel	x19,x0,xzr,ne
.auto:	adr	x0,quand
	ldr	x0,[x0]
	cmp	x0,2
	b.ne	.larg
	cbz	x19,.larg
	orr	x22,x22,OPT_COUL
.larg:	movl	x20,80
	tst	x22,OPT_C
	b.eq	.f
	adr	x0,s_columns			; $COLUMNS
	bl	getenv
	cbz	x0,.vt
	bl	nombre
	cmp	x0,0
	csel	x20,x0,x20,gt
.vt:	cbz	x19,.f				; le pty : sa taille
	adr	x0,s_vterm
	mov	x1,5000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.f
	mov	x1,VT_GWINSZ
	str	x1,[sp]
	str	x19,[sp,MSG_A]
	str	wzr,[sp,MSG_LEN]
	mov	x1,sp
	sys	SYS_SVC_CALL
	tbnz	x0,63,.f
	ldr	x0,[sp,MSG_B]
	cmp	x0,0
	csel	x20,x0,x20,gt
.f:	adr	x0,largeur
	str	x20,[x0]
	tst	x22,OPT_COUL			; LS_COLORS
	b.eq	.out
	adr	x0,s_ls_colors
	bl	getenv
	adr	x1,lsc
	str	x0,[x1]
.out:	add	sp,sp,MSG_TAILLE
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom -> x0 = sa valeur dans l'environnement, 0 : absente
getenv:
	adr	x1,t_env
	ldr	x2,[x1]
	adr	x1,t_envc
	ldr	x3,[x1]
	cbz	x2,.non
.v:	cbz	x3,.non
	mov	x4,x0
	mov	x5,x2
.c:	ldrb	w6,[x4],1
	cbz	w6,.egal
	ldrb	w7,[x5],1
	cmp	w6,w7
	b.eq	.c
	b	.suiv
.egal:	ldrb	w7,[x5],1
	cmp	w7,'='
	b.ne	.suiv
	mov	x0,x5
	ret
.suiv:	ldrb	w6,[x2],1			; chaine suivante
	cbnz	w6,.suiv
	sub	x3,x3,1
	b	.v
.non:	mov	x0,0
	ret

; x0 = texte decimal -> x0 = valeur (0 : illisible)
nombre:
	mov	x1,0
	mov	x3,10
.c:	ldrb	w2,[x0],1
	cbz	w2,.f
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.non
	madd	x1,x1,x3,x2
	b	.c
.f:	mov	x0,x1
	ret
.non:	mov	x0,0
	ret

; x0 = chemin donne -> x0 = 0, 1 (inaccessible). Un dossier : liste ; un fichier : mis
; en attente (les fichiers qui se suivent s'affichent ensemble, en colonnes avec -C)
operande:
	prologue
	stp	x24,x25,[sp,-16]!
	sub	sp,sp,16
	mov	x24,x0
	mov	x1,sp
	sys	SYS_STAT
	tbnz	x0,63,.err
	ldp	x1,x2,[sp]			; taille, attributs
	tst	x2,ATTR_DIR
	b.ne	.dos
	mov	x0,x24
	bl	nouvelle_entree
	adr	x1,attente_n
	ldr	x2,[x1]
	cmp	x2,NOMS_MAX
	b.hs	.ok
	adr	x3,attente
	ldr	x3,[x3]
	cbnz	x3,.pose
	stp	x0,x1,[sp,-16]!
	movl	x0,8*NOMS_MAX
	bl	malloc
	mov	x3,x0
	adr	x0,attente
	str	x3,[x0]
	ldp	x0,x1,[sp],16
	ldr	x2,[x1]
.pose:	str	x0,[x3,x2,lsl 3]
	add	x2,x2,1
	str	x2,[x1]
.ok:	mov	x0,0
	b	.out
.dos:	bl	vider
	mov	x0,x24
	bl	liste
	b	.out
.err:	bl	vider
	mov	x0,x24
	bl	t_err_fic
	mov	x0,1
.out:	add	sp,sp,16
	ldp	x24,x25,[sp],16
	epilogue

; les fichiers donnes en attente : affiches
vider:
	prologue
	adr	x2,attente_n
	ldr	x1,[x2]
	cbz	x1,.out
	str	xzr,[x2]
	adr	x0,attente
	ldr	x0,[x0]
	stp	x0,x1,[sp,-16]!
	mov	x2,0
	bl	preparer
	ldp	x0,x1,[sp],16
	bl	afficher
.out:	epilogue

; x0 = nom, x1 = taille, x2 = attributs -> x0 = entree : le nom copie, taille en
; [x0-16], attributs en [x0-8]
nouvelle_entree:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,xzr,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	str_len
	add	x0,x0,17
	bl	malloc
	stp	x20,x21,[x0],16
	mov	x1,x0
.c:	ldrb	w2,[x19],1
	strb	w2,[x1],1
	cbnz	w2,.c
	ldp	x21,xzr,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin d'un dossier -> x0 = 0, 1
liste:
	prologue
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	mov	x24,x0
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x25,x0
	movl	x0,8*NOMS_MAX
	bl	malloc
	mov	x26,x0
	mov	x27,0
.l:	mov	x0,x25
	adr	x1,ent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.f
	adr	x0,ent
	ldrb	w1,[x0]
	cmp	w1,'.'
	b.ne	.garde
	tbz	x22,0,.l
.garde:	cmp	x27,NOMS_MAX
	b.hs	.l
	adr	x0,ent
	ldr	x1,[x0,DIRENT_SIZE]
	ldr	x2,[x0,DIRENT_ATTR]
	bl	nouvelle_entree
	str	x0,[x26,x27,lsl 3]
	add	x27,x27,1
	b	.l
.f:	mov	x0,x25
	sys	SYS_CLOSE
	mov	x0,x26
	add	x1,x26,x27,lsl 3
	bl	trier
	mov	x0,x26
	mov	x1,x27
	mov	x2,x24
	bl	preparer
	mov	x0,x26
	mov	x1,x27
	bl	afficher
	mov	x0,0
	b	.out
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x0,1
.out:	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	epilogue

; x0 = entrees, x1 = nombre, x2 = leur dossier (0 : des chemins donnes) : le bit
; d'execution (V_STATX, un appel par fichier) s'il sert, avec -l ou la couleur
preparer:
	tst	x22,OPT_L or OPT_COUL
	b.ne	.oui
	ret
.oui:	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x23,[sp,-16]!
	sub	sp,sp,MSG_TAILLE+320
	mov	x19,x0
	add	x20,x0,x1,lsl 3
	mov	x21,x2
.e:	cmp	x19,x20
	b.hs	.out
	ldr	x23,[x19],8
	ldur	x0,[x23,-8]
	tst	x0,ATTR_DIR
	b.ne	.e
	mov	x4,x23
	cbz	x21,.msg
	add	x0,sp,MSG_TAILLE		; le chemin : dossier/nom
	mov	x1,x21
	mov	x2,x23
	bl	t_join
	add	x4,sp,MSG_TAILLE
.msg:	mov	x0,sp
	mov	x1,V_STATX
	mov	x2,0
	mov	x3,0
	mov	x5,0
	bl	t_msg
	mov	x0,sp
	bl	t_vfs
	tbnz	x0,63,.e
	ldr	x0,[sp,MSG_C]
	tst	x0,ATTR_EXEC
	b.eq	.e
	ldur	x0,[x23,-8]
	orr	x0,x0,ATTR_EXEC
	stur	x0,[x23,-8]
	b	.e
.out:	add	sp,sp,MSG_TAILLE+320
	ldp	x21,x23,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entrees, x1 = nombre : -l, -C, ou un nom par ligne
afficher:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	add	x20,x0,x1,lsl 3
	tst	x22,OPT_L
	b.ne	.l
	tst	x22,OPT_C
	b.eq	.u
	bl	colonnes
	b	.out
.u:	cmp	x19,x20
	b.hs	.out
	ldr	x0,[x19],8
	bl	nom
	bl	t_nl
	b	.u
.l:	cmp	x19,x20
	b.hs	.out
	ldr	x0,[x19],8
	bl	ligne_longue
	b	.l
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree : « drwxr-xr-x taille nom »
ligne_longue:
	prologue
	str	x24,[sp,-16]!
	mov	x24,x0
	ldur	x1,[x24,-8]
	adr	x0,s_perm_f
	adr	x2,s_perm_x
	tst	x1,ATTR_EXEC
	csel	x0,x2,x0,ne
	adr	x2,s_perm_d
	tst	x1,ATTR_DIR
	csel	x0,x2,x0,ne
	bl	t_puts
	mov	w0,' '
	bl	t_putc
	ldur	x0,[x24,-16]
	bl	t_putn
	mov	w0,' '
	bl	t_putc
	mov	x0,x24
	bl	nom
	bl	t_nl
	ldr	x24,[sp],16
	epilogue

; x0 = entree : son nom, en couleur s'il le faut (comme GNU ls : ESC[0m une fois avant
; la premiere couleur, puis ESC[<couleur>m nom ESC[0m)
nom:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	tst	x22,OPT_COUL
	b.eq	.simple
	bl	couleur
	cbz	x1,.simple
	cmp	x1,2				; « 0 », « 00 » : pas de couleur (GNU)
	b.hi	.c
	ldrb	w2,[x0]
	sub	x4,x1,1
	ldrb	w4,[x0,x4]
	mov	w5,'0'
	cmp	w2,w5
	ccmp	w4,w5,0,eq
	b.eq	.simple
.c:	mov	x20,x1
	stp	x0,xzr,[sp,-16]!
	adr	x0,deja
	ldr	x1,[x0]
	cbnz	x1,.deja
	mov	x1,1
	str	x1,[x0]
	adr	x0,s_raz
	bl	t_puts
.deja:	mov	w0,27
	bl	t_putc
	mov	w0,'['
	bl	t_putc
	ldp	x0,xzr,[sp],16
	mov	x1,x20
	bl	t_write
	mov	w0,'m'
	bl	t_putc
	mov	x0,x19
	bl	t_puts
	adr	x0,s_raz
	bl	t_puts
	b	.out
.simple:
	mov	x0,x19
	bl	t_puts
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree -> x0 = sa couleur (« 01;34 »), x1 = longueur (0 : aucune). LS_COLORS
; d'abord (di, ex, *.ext, fi), puis les couleurs de dircolors
couleur:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,80
	mov	x19,x0
	ldur	x20,[x19,-8]
	tst	x20,ATTR_DIR
	b.eq	.x
	adr	x0,s_k_di
	bl	lsc_cherche
	cbnz	x1,.out
	adr	x0,s_c_di
	b	.fixe
.x:	tst	x20,ATTR_EXEC
	b.eq	.ext
	adr	x0,s_k_ex
	bl	lsc_cherche
	cbnz	x1,.out
	adr	x0,s_c_ex
	b	.fixe
.ext:	mov	x0,x19				; l'extension : apres le dernier point
	mov	x1,0
.p:	ldrb	w2,[x0],1
	cbz	w2,.pf
	cmp	w2,'.'
	csel	x1,x0,x1,eq
	b	.p
.pf:	cbz	x1,.fi
	ldrb	w2,[x1]
	cbz	w2,.fi
	mov	x20,x1
	sub	x2,x0,x1
	cmp	x2,64
	b.hs	.fi
	mov	w2,'*'				; « *.ext » dans LS_COLORS
	strb	w2,[sp]
	mov	w2,'.'
	strb	w2,[sp,1]
	add	x3,sp,2
.k:	ldrb	w2,[x1],1
	strb	w2,[x3],1
	cbnz	w2,.k
	mov	x0,sp
	bl	lsc_cherche
	cbnz	x1,.out
	adr	x3,t_exts			; la table : couleur, extensions, 0
.g:	ldrb	w2,[x3]
	cbz	w2,.fi
	mov	x4,x3				; la couleur du groupe
.g1:	ldrb	w2,[x3],1
	cbnz	w2,.g1
.e:	ldrb	w2,[x3]
	cbnz	w2,.cmp
	add	x3,x3,1				; groupe suivant
	b	.g
.cmp:	mov	x5,x20
.c:	ldrb	w2,[x3],1
	ldrb	w6,[x5],1
	sub	w7,w6,'A'			; casse ignoree
	cmp	w7,25
	b.hi	.c1
	add	w6,w6,32
.c1:	cmp	w2,w6
	b.ne	.saute
	cbnz	w2,.c
	mov	x0,x4
	b	.fixe
.saute:	cbz	w2,.e				; (le reste de cette extension)
.s1:	ldrb	w2,[x3],1
	cbnz	w2,.s1
	b	.e
.fi:	adr	x0,s_k_fi
	bl	lsc_cherche
	b	.out
.fixe:	mov	x1,0
.n:	ldrb	w2,[x0,x1]
	cbz	w2,.out
	add	x1,x1,1
	b	.n
.out:	add	sp,sp,80
	ldp	x19,x20,[sp],16
	epilogue

; x0 = cle (« di », « *.png ») -> x0 = sa valeur dans LS_COLORS, x1 = longueur (0 : absente)
lsc_cherche:
	adr	x1,lsc
	ldr	x2,[x1]
	cbz	x2,.non
.ent:	ldrb	w3,[x2]
	cbz	w3,.non
	mov	x4,x0
.c:	ldrb	w5,[x4],1
	cbz	w5,.fin
	ldrb	w3,[x2],1
	sub	w6,w3,'A'			; casse ignoree
	cmp	w6,25
	b.hi	.c1
	add	w3,w3,32
.c1:	sub	w6,w5,'A'
	cmp	w6,25
	b.hi	.c2
	add	w5,w5,32
.c2:	cmp	w3,w5
	b.eq	.c
	b	.saute
.fin:	ldrb	w3,[x2]
	cmp	w3,'='
	b.ne	.saute
	add	x0,x2,1
	mov	x1,0
.v:	ldrb	w3,[x0,x1]
	cbz	w3,.r
	cmp	w3,':'
	b.eq	.r
	add	x1,x1,1
	b	.v
.r:	ret
.saute:	ldrb	w3,[x2]				; entree suivante
	cbz	w3,.non
	add	x2,x2,1
	cmp	w3,':'
	b.ne	.saute
	b	.ent
.non:	mov	x0,0
	mov	x1,0
	ret

; x0 = chaine UTF-8 -> x0 = colonnes (un caractere : une)
largeur_nom:
	mov	x1,0
.c:	ldrb	w2,[x0],1
	cbz	w2,.f
	and	w2,w2,0xC0
	cmp	w2,0x80
	cinc	x1,x1,ne
	b	.c
.f:	mov	x0,x1
	ret

; x19, x20 = entrees [debut, fin[ : -C comme GNU ls. Le plus de colonnes possible, les
; noms de haut en bas ; une colonne : son nom le plus long + 2 (la derniere, sans les
; 2), 3 au moins ; la ligne doit rester plus courte que la largeur
colonnes:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	x21,x20,x19
	lsr	x21,x21,3			; n
	cbz	x21,.out
	lsl	x0,x21,3			; les largeurs des noms
	bl	malloc
	mov	x23,x0
	mov	x24,0
.lg:	cmp	x24,x21
	b.hs	.lgf
	ldr	x0,[x19,x24,lsl 3]
	bl	largeur_nom
	str	x0,[x23,x24,lsl 3]
	add	x24,x24,1
	b	.lg
.lgf:	adr	x0,largeur
	ldr	x24,[x0]			; W
	mov	x0,3
	udiv	x25,x24,x0			; colonnes au plus : W / 3, n
	cmp	x25,1
	csinc	x25,x25,xzr,hs
	cmp	x25,x21
	csel	x25,x21,x25,hi
	lsl	x0,x25,3
	bl	malloc
	mov	x26,x0				; largeurs des colonnes
.essai:	cmp	x25,1
	b.ls	.une
	add	x27,x21,x25			; lignes
	sub	x27,x27,1
	udiv	x27,x27,x25
	mov	x0,0
	mov	x1,3
.i:	str	x1,[x26,x0,lsl 3]
	add	x0,x0,1
	cmp	x0,x25
	b.lo	.i
	add	x28,x25,x25,lsl 1		; longueur de la ligne
	mov	x0,0				; fichier
.f:	cmp	x0,x21
	b.hs	.trouve
	udiv	x1,x0,x27			; sa colonne
	ldr	x2,[x23,x0,lsl 3]
	sub	x3,x25,1
	cmp	x1,x3
	add	x3,x2,2
	csel	x2,x2,x3,eq
	ldr	x3,[x26,x1,lsl 3]
	cmp	x3,x2
	b.hs	.f1
	sub	x3,x2,x3
	add	x28,x28,x3
	str	x2,[x26,x1,lsl 3]
	cmp	x28,x24
	b.hs	.moins
.f1:	add	x0,x0,1
	b	.f
.moins:	sub	x25,x25,1
	b	.essai
.une:	mov	x25,1
	mov	x27,x21
.trouve:					; x25 colonnes, x27 lignes
	mov	x28,0				; ligne
.r:	cmp	x28,x27
	b.hs	.out
	mov	x24,x28				; fichier
.r2:	ldr	x0,[x19,x24,lsl 3]
	bl	nom
	udiv	x1,x24,x27			; sa colonne : sa largeur, moins le nom
	ldr	x25,[x26,x1,lsl 3]
	ldr	x1,[x23,x24,lsl 3]
	sub	x25,x25,x1
	add	x24,x24,x27
	cmp	x24,x21
	b.hs	.nl
.sp:	cbz	x25,.r2
	mov	w0,' '
	bl	t_putc
	sub	x25,x25,1
	b	.sp
.nl:	bl	t_nl
	add	x28,x28,1
	b	.r
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = tableau de chaines [debut, fin[
trier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	add	x21,x19,8
.i:	cmp	x21,x20
	b.hs	.f
	ldr	x22,[x21]
	mov	x2,x21
.j:	cmp	x2,x19
	b.ls	.pose
	ldur	x3,[x2,-8]
	mov	x4,0
.c:	ldrb	w5,[x3,x4]
	ldrb	w6,[x22,x4]
	cmp	w5,w6
	b.ne	.d
	cbz	w5,.pose
	add	x4,x4,1
	b	.c
.d:	b.ls	.pose
	str	x3,[x2]
	sub	x2,x2,8
	b	.j
.pose:	str	x22,[x2]
	add	x21,x21,8
	b	.i
.f:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

NOMS_MAX = 16384

include '../../inc/tool.inc'
s_point		db '.',0
s_color		db 'color',0
t_quand		db '1always',0,'1yes',0,'1force',0,'0never',0,'0no',0,'0none',0
		db '2auto',0,'2tty',0,'2if-tty',0,0
s_quand_inv	db "--color : QUAND vaut always, auto ou never",0
s_opt_inv	db "option inconnue (--color[=QUAND], -a, -l, -C, -1)",0
s_columns	db 'COLUMNS',0
s_ls_colors	db 'LS_COLORS',0
s_vterm		db 'vterm',0
s_perm_d	db 'drwxr-xr-x',0
s_perm_x	db '-rwxr-xr-x',0
s_perm_f	db '-rw-r--r--',0
s_raz		db 27,'[0m',0
s_k_di		db 'di',0
s_k_ex		db 'ex',0
s_k_fi		db 'fi',0
s_c_di		db '01;34',0
s_c_ex		db '01;32',0
; les couleurs de dircolors (GNU) : un groupe = sa couleur, ses extensions, 0
t_exts		db '01;31',0,'tar',0,'tgz',0,'gz',0,'bz2',0,'xz',0,'zst',0,'zip',0,'7z',0
		db 'rar',0,'opk',0,'deb',0,'rpm',0,'jar',0,0
		db '01;35',0,'png',0,'jpg',0,'jpeg',0,'gif',0,'bmp',0,'svg',0,'webp',0,'tif',0
		db 'tiff',0,'ico',0,'mp4',0,'webm',0,'mkv',0,'avi',0,'mov',0,0
		db '00;36',0,'mp3',0,'wav',0,'flac',0,'ogg',0,'opus',0,'m4a',0,'aac',0,'mid',0
		db 'midi',0,0
		db 0
	align 4

segment readable writeable
kit_data
tool_data
	align 8
quand		dd 0			; --color : 0 jamais, 1 toujours, 2 sur un terminal
largeur		dd 80
lsc		dd 0			; LS_COLORS (0 : absente)
deja		dd 0			; ESC[0m deja ecrit
attente		dd 0			; les fichiers donnes, en attente
attente_n	dd 0
ent	rb DIRENT_BYTES
