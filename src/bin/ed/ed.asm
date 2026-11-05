; ed - l'editeur de lignes : ed [-p invite] [fichier] (docs/OUTILS.md), d'apres ed.c d'Onyx.
; Le texte est une liste de lignes numerotees ; l'une est la ligne courante (« . »). Une commande :
;   [adresse[,adresse]]commande[parametres]
; Adresses : N, . , $ , +N / -N (depuis .), /re/ (la suivante qui correspond, en bouclant), ?re?
;   (la precedente), 'x (la ligne marquee x) ; « , » seul : 1,$ ; « ; » : .,$
; Commandes (lignes par defaut entre crochets) :
;   a i c        ajouter apres [.], inserer avant [.], changer [.,.] : les lignes tapees, puis
;                une ligne « . » seule
;   d            supprimer [.,.]               j   joindre [.,.+1]
;   p n l        afficher [.,.] : tel quel, numerote, tabulations et fin de ligne visibles
;   s/re/rempl/  substituer sur [.,.] (g, N, p ; & et \1..\9, \n) ; s seul : la derniere
;   m N   t N    deplacer, copier [.,.] apres la ligne N   k x  marquer [.] (a..z)
;   g/re/cmd     cmd sur chaque ligne qui correspond [1,$] ; v/re/cmd : sur les autres
;   r fichier    lire apres [$]               w fichier  ecrire [1,$] (W : ajouter au fichier)
;   e fichier    editer un autre fichier (E : meme si le texte n'est pas enregistre) ; f fichier
;   =            numero de [$]                 u   annuler la derniere commande (u encore : refaire)
;   q            quitter (Q : sans enregistrer) wq  ecrire et quitter
;   h            explique le dernier « ? »     H   explique chaque « ? » desormais
;   une adresse seule (ou une ligne vide) : aller a cette ligne (la suivante) et l'afficher
; Erreur : « ? » (et la raison apres H). Apres un changement, q demande une fois (« ? »).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

macro getv reg*, var* {
	adr	reg,var
	ldr	reg,[reg]
}
macro setv reg*, var*, tmp* {
	adr	tmp,var
	str	reg,[tmp]
}
; (dans command) DEF : sans adresse, [x22, x23] = les deux registres donnes
macro defaults def1*, def2* {
	cmp	x24,0
	csel	x22,def1,x22,eq
	csel	x23,def2,x23,eq
}
; (dans command) NEED : 1 <= a <= b <= $
macro need {
	cmp	x22,1
	b.lt	.badaddr
	getv	x9,g_n
	cmp	x23,x9
	b.gt	.badaddr
	cmp	x22,x23
	b.gt	.badaddr
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	adr	x0,s_vide
	setv	x0,g_prompt,x1
	setv	x0,g_errmsg,x1
	mov	x0,1
	setv	x0,g_snth,x1
	mov	x21,1
	cmp	x19,3
	b.lo	.np
	ldr	x0,[x20,8]
	adr	x1,s_p
	bl	str_eq
	cbz	x0,.np
	ldr	x0,[x20,16]
	setv	x0,g_prompt,x1
	mov	x21,3
.np:	mov	x0,0
	bl	reserve
	cmp	x21,x19
	b.hs	.go
	ldr	x0,[x20,x21,lsl 3]
	bl	filename
	mov	x1,0
	bl	read_file
	tbz	x0,63,.lu
	adr	x0,g_file
	bl	t_puts
	adr	x0,m_new
	bl	t_dire
	b	.go
.lu:	bl	t_putn
	bl	t_nl
.go:	mov	x0,0
	bl	t_lire_debut
.loop:	getv	x0,g_prompt
	bl	t_puts
	bl	t_flush
	bl	t_lire_ligne
	cbz	x0,.end
	bl	strndup				; (les commandes a, i, c lisent d'autres lignes)
	mov	x1,0
	bl	command
	cmp	x0,0
	b.gt	.end
	bl	t_flush
	b	.loop
.end:	mov	x0,0
	bl	t_fin

; --- le texte ------------------------------------------------------------------------

; x0 = n : de la place pour g_ln[0..n+1]
reserve:
	prologue
	stp	x21,x22,[sp,-16]!
	add	x21,x0,2
	getv	x1,g_cap
	cmp	x21,x1
	b.ls	.out
	lsl	x22,x21,1
	add	x22,x22,64
	lsl	x0,x22,3
	bl	malloc
	getv	x1,g_ln
	cbz	x1,.set
	getv	x2,g_n
	add	x2,x2,1
	mov	x3,0
.c:	cmp	x3,x2
	b.hi	.set
	ldr	x4,[x1,x3,lsl 3]
	str	x4,[x0,x3,lsl 3]
	add	x3,x3,1
	b	.c
.set:	setv	x0,g_ln,x1
	setv	x22,g_cap,x1
.out:	ldp	x21,x22,[sp],16
	epilogue

; x0 = apres la ligne, x1 = texte
insert:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	getv	x0,g_n
	add	x0,x0,1
	bl	reserve
	getv	x1,g_ln
	getv	x2,g_n
	mov	x3,x2
.c:	cmp	x3,x21
	b.ls	.p
	ldr	x4,[x1,x3,lsl 3]
	add	x5,x3,1
	str	x4,[x1,x5,lsl 3]
	sub	x3,x3,1
	b	.c
.p:	add	x3,x21,1
	str	x22,[x1,x3,lsl 3]
	add	x2,x2,1
	setv	x2,g_n,x3
	ldp	x21,x22,[sp],16
	epilogue

; x0 = premiere, x1 = derniere ligne a retirer
delete:
	sub	x2,x1,x0
	add	x2,x2,1
	getv	x3,g_ln
	getv	x4,g_n
	add	x5,x1,1
.c:	cmp	x5,x4
	b.hi	.f
	ldr	x6,[x3,x5,lsl 3]
	sub	x7,x5,x2
	str	x6,[x3,x7,lsl 3]
	add	x5,x5,1
	b	.c
.f:	sub	x4,x4,x2
	setv	x4,g_n,x6
	ret

; le texte d'avant la commande (u) : les lignes sont partagees, jamais changees sur place
snapshot:
	prologue
	getv	x0,g_n
	add	x0,x0,2
	lsl	x0,x0,3
	bl	malloc
	getv	x1,g_ln
	getv	x2,g_n
	mov	x3,1
.c:	cmp	x3,x2
	b.hi	.f
	ldr	x4,[x1,x3,lsl 3]
	str	x4,[x0,x3,lsl 3]
	add	x3,x3,1
	b	.c
.f:	setv	x0,u_ln,x4
	setv	x2,u_n,x4
	getv	x3,g_cur
	setv	x3,u_cur,x4
	mov	x3,1
	setv	x3,u_ok,x4
	epilogue

; x0 = chemin, x1 = apres la ligne -> x0 = octets lus, -1 : illisible
read_file:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x22,x1
	bl	slurp
	cbz	x0,.fail
	mov	x21,x1
	bl	lines
	mov	x23,x0
	mov	x24,x1
	getv	x0,g_n
	add	x0,x0,x24
	bl	reserve
	getv	x1,g_ln
	getv	x2,g_n
	mov	x3,x2
.s:	cmp	x3,x22
	b.ls	.f
	ldr	x4,[x1,x3,lsl 3]
	add	x5,x3,x24
	str	x4,[x1,x5,lsl 3]
	sub	x3,x3,1
	b	.s
.f:	mov	x3,0
.c:	cmp	x3,x24
	b.hs	.d
	ldr	x4,[x23,x3,lsl 3]
	add	x5,x22,x3
	add	x5,x5,1
	str	x4,[x1,x5,lsl 3]
	add	x3,x3,1
	b	.c
.d:	add	x2,x2,x24
	setv	x2,g_n,x4
	add	x0,x22,x24
	setv	x0,g_cur,x4
	mov	x0,x21
	b	.out
.fail:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = chemin, x1 = premiere, x2 = derniere ligne, x3 = 1 : ajouter au fichier
;   -> x0 = octets ecrits, -1 : impossible
write_file:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	mov	x24,x3
	mov	x25,0				; octets
	mov	x26,x22
.t:	cmp	x26,x23
	b.gt	.ta
	getv	x0,g_ln
	ldr	x0,[x0,x26,lsl 3]
	bl	str_len
	add	x25,x25,x0
	add	x25,x25,1
	add	x26,x26,1
	b	.t
.ta:	add	x0,x25,1
	bl	malloc
	cbz	x0,.fail
	mov	x26,x0
	mov	x3,0
	getv	x1,g_ln
	mov	x2,x22
.l:	cmp	x2,x23
	b.gt	.op
	ldr	x4,[x1,x2,lsl 3]
.cc:	ldrb	w5,[x4],1
	cbz	w5,.eol
	strb	w5,[x26,x3]
	add	x3,x3,1
	b	.cc
.eol:	mov	w5,10
	strb	w5,[x26,x3]
	add	x3,x3,1
	add	x2,x2,1
	b	.l
.op:	mov	x0,x21
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	mov	x2,O_WRITE or O_CREATE or O_APPEND
	cmp	x24,0
	csel	x1,x2,x1,ne
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x22,x0
	mov	x23,0				; ecrits
.w:	subs	x2,x25,x23
	b.eq	.cl
	movl	x3,65536
	cmp	x2,x3
	csel	x2,x3,x2,hi
	mov	x0,x22
	add	x1,x26,x23
	sys	SYS_WRITE
	cmp	x0,0
	b.le	.wfail
	add	x23,x23,x0
	b	.w
.cl:	mov	x0,x22
	sys	SYS_CLOSE
	mov	x0,x25
	b	.out
.wfail:	mov	x0,x22
	sys	SYS_CLOSE
.fail:	mov	x0,-1
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = chemin -> x0 = contenu (un 0 apres), x1 = octets ; x0 = 0 : illisible (ou un dossier)
slurp:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,16
	mov	x21,x0
	mov	x1,sp
	sys	SYS_STAT
	tbnz	x0,63,.fail
	ldr	x0,[sp,8]
	tst	x0,ATTR_DIR
	b.ne	.fail
	ldr	x22,[sp]
	mov	x0,x21
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x23,x0
	add	x0,x22,1
	bl	malloc
	cbz	x0,.cfail
	mov	x24,x0
	mov	x21,0
.r:	subs	x2,x22,x21
	b.eq	.done
	mov	x0,x23
	add	x1,x24,x21
	sys	SYS_READ
	cmp	x0,0
	b.le	.done
	add	x21,x21,x0
	b	.r
.done:	strb	wzr,[x24,x21]
	mov	x0,x23
	sys	SYS_CLOSE
	mov	x0,x24
	mov	x1,x21
	b	.out
.cfail:	mov	x0,x23
	sys	SYS_CLOSE
.fail:	mov	x0,0
	mov	x1,0
.out:	add	sp,sp,16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = texte (les fins de ligne deviennent des 0), x1 = octets -> x0 = tableau des lignes, x1 = nombre
lines:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x23,0
	mov	x2,0
.c:	cmp	x2,x22
	b.hs	.cf
	ldrb	w3,[x21,x2]
	add	x2,x2,1
	cmp	w3,10
	b.ne	.c
	add	x23,x23,1
	b	.c
.cf:	cbz	x22,.al
	sub	x2,x22,1
	ldrb	w3,[x21,x2]
	cmp	w3,10
	b.eq	.al
	add	x23,x23,1
.al:	add	x0,x23,1
	lsl	x0,x0,3
	bl	malloc
	mov	x1,0
	mov	x2,0
	mov	x3,x21
.f:	cmp	x2,x22
	b.hs	.ff
	ldrb	w4,[x21,x2]
	cmp	w4,10
	b.ne	.fn
	strb	wzr,[x21,x2]
	str	x3,[x0,x1,lsl 3]
	add	x1,x1,1
	add	x3,x21,x2
	add	x3,x3,1
.fn:	add	x2,x2,1
	b	.f
.ff:	cmp	x1,x23
	b.hs	.ok
	str	x3,[x0,x1,lsl 3]
.ok:	mov	x1,x23
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; --- petits outils ----------------------------------------------------------------------

; x0 = texte, x1 = octets -> x0 = copie (0 final)
strndup:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	add	x0,x1,1
	bl	malloc
	mov	x2,0
.c:	cmp	x2,x22
	b.hs	.f
	ldrb	w3,[x21,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	b	.c
.f:	strb	wzr,[x0,x22]
	ldp	x21,x22,[sp],16
	epilogue

; x0 = texte -> x0 = copie
strdup:
	prologue
	str	x21,[sp,-16]!
	mov	x21,x0
	bl	str_len
	mov	x1,x0
	mov	x0,x21
	bl	strndup
	ldr	x21,[sp],16
	epilogue

; x0 = destination, x1 = source
strcpy:
.c:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.c
	ret

; x0 = texte -> x0 apres les blancs
skipb:
.c:	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.n
	cmp	w1,9
	b.ne	.r
.n:	add	x0,x0,1
	b	.c
.r:	ret

; x0 = texte (sur un chiffre) -> x0 = nombre, x1 = apres
parse_num:
	mov	x1,x0
	mov	x0,0
.c:	ldrb	w2,[x1]
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.r
	mov	x3,10
	mul	x0,x0,x3
	add	x0,x0,x2
	add	x1,x1,1
	b	.c
.r:	ret

; x0 = texte apres la commande -> x0 = g_file (le nom donne, ou celui d'avant)
filename:
.b:	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.s
	cmp	w1,9
	b.ne	.g
.s:	add	x0,x0,1
	b	.b
.g:	adr	x2,g_file
	cbz	w1,.r
	mov	x3,0
.c:	ldrb	w1,[x0,x3]
	cbz	w1,.t
	cmp	x3,255
	b.hs	.t
	strb	w1,[x2,x3]
	add	x3,x3,1
	b	.c
.t:	cbz	x3,.z
	sub	x4,x3,1
	ldrb	w1,[x2,x4]
	cmp	w1,' '
	b.eq	.tr
	cmp	w1,9
	b.ne	.z
.tr:	mov	x3,x4
	b	.t
.z:	strb	wzr,[x2,x3]
.r:	mov	x0,x2
	ret

; x0 = raison (texte litteral) : « ? », la raison si H -> x0 = -1
fail:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	setv	x0,g_errmsg,x1
	adr	x0,s_quest
	bl	t_puts
	getv	x0,g_verbose
	cbz	x0,.out
	mov	x0,x19
	bl	t_dire
	bl	t_nl
.out:	mov	x0,-1
	ldr	x19,[sp],16
	epilogue

; x0 = motif -> x0 = programme
compile:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	movl	x0,RE_TAILLE
	bl	malloc
	mov	x22,x0
	mov	x0,x21
	mov	x1,0
	mov	x2,x22
	bl	re_compile
	mov	x0,x22
	ldp	x21,x22,[sp],16
	epilogue

; x0 = texte, x1 = delimiteur -> x0 = morceau (copie ; \delim -> delim), x1 = apres,
;   x2 = 1 si le delimiteur fermant est la
re_piece:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	bl	str_len
	add	x0,x0,1
	bl	malloc
	mov	x23,x0
	mov	x3,x0
	mov	x24,0
.c:	ldrb	w1,[x21]
	cbz	w1,.f
	add	x21,x21,1
	cmp	w1,w22
	b.eq	.cl
	cmp	w1,'\'
	b.ne	.p
	ldrb	w2,[x21]
	cbz	w2,.p
	add	x21,x21,1
	cmp	w2,w22
	b.eq	.lit
	strb	w1,[x3],1
.lit:	strb	w2,[x3],1
	b	.c
.p:	strb	w1,[x3],1
	b	.c
.cl:	mov	x24,1
.f:	strb	wzr,[x3]
	mov	x0,x23
	mov	x1,x21
	mov	x2,x24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = programme, x1 = 1 : en arriere -> x0 = 1 et x1 = la ligne, 0 : aucune (en bouclant)
search:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	getv	x23,g_cur
	mov	x24,0
.c:	getv	x0,g_n
	cmp	x24,x0
	b.hs	.no
	cbz	x22,.fw
	sub	x23,x23,1
	cmp	x23,1
	b.ge	.t
	mov	x23,x0
	b	.t
.fw:	add	x23,x23,1
	cmp	x23,x0
	b.le	.t
	mov	x23,1
.t:	getv	x1,g_ln
	ldr	x1,[x1,x23,lsl 3]
	mov	x0,x21
	mov	x2,x1
	bl	re_cherche
	cbnz	x0,.yes
	add	x24,x24,1
	b	.c
.no:	mov	x0,0
	b	.out
.yes:	mov	x0,1
	mov	x1,x23
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; --- adresses ----------------------------------------------------------------------------

; x0 = texte -> x0 = 1 (x2 = l'adresse), 0 : aucune, -1 : erreur (dite) ; x1 = apres
address:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	skipb
	mov	x21,x0
	mov	x22,0				; a
	mov	x23,0				; 1 : une adresse
	ldrb	w0,[x21]
	sub	w1,w0,'0'
	cmp	w1,9
	b.hi	.dot
	mov	x0,x21
	bl	parse_num
	mov	x22,x0
	mov	x21,x1
	mov	x23,1
	b	.rel
.dot:	cmp	w0,'.'
	b.ne	.dol
	getv	x22,g_cur
	add	x21,x21,1
	mov	x23,1
	b	.rel
.dol:	cmp	w0,'$'
	b.ne	.mk
	getv	x22,g_n
	add	x21,x21,1
	mov	x23,1
	b	.rel
.mk:	cmp	w0,0x27
	b.ne	.re
	ldrb	w1,[x21,1]
	sub	w1,w1,'a'
	cmp	w1,25
	b.hi	.badmk
	adr	x2,g_mark
	ldr	x24,[x2,x1,lsl 3]
	cbz	x24,.badmk
	getv	x3,g_ln
	getv	x4,g_n
	mov	x22,1
.mf:	cmp	x22,x4
	b.hi	.gone
	ldr	x5,[x3,x22,lsl 3]
	cmp	x5,x24
	b.eq	.mok
	add	x22,x22,1
	b	.mf
.mok:	add	x21,x21,2
	mov	x23,1
	b	.rel
.badmk:	adr	x0,e_mark
	b	.fail
.gone:	adr	x0,e_gone
	b	.fail
.re:	cmp	w0,'/'
	b.eq	.re1
	cmp	w0,'?'
	b.ne	.rel
.re1:	mov	x24,x0
	add	x0,x21,1
	mov	x1,x24
	bl	re_piece
	mov	x21,x1
	ldrb	w1,[x0]
	cbz	w1,.old
	bl	compile
	setv	x0,g_re,x1
	b	.srch
.old:	getv	x0,g_re
	cbz	x0,.nopat
.srch:	getv	x0,g_re
	cmp	x24,'?'
	cset	x1,eq
	bl	search
	cbz	x0,.nom
	mov	x22,x1
	mov	x23,1
	b	.rel
.nopat:	adr	x0,e_nopat
	b	.fail
.nom:	adr	x0,e_nomatch
	b	.fail
.rel:	mov	x0,x21				; +N -N + -
	bl	skipb
	mov	x21,x0
	ldrb	w0,[x21]
	cmp	w0,'+'
	b.eq	.pm
	cmp	w0,'-'
	b.ne	.end
.pm:	mov	x24,x0
	add	x21,x21,1
	cbnz	x23,.k
	getv	x22,g_cur
	mov	x23,1
.k:	mov	x2,1
	ldrb	w1,[x21]
	sub	w1,w1,'0'
	cmp	w1,9
	b.hi	.ap
	mov	x0,x21
	bl	parse_num
	mov	x21,x1
	mov	x2,x0
.ap:	cmp	x24,'-'
	b.eq	.neg
	add	x22,x22,x2
	b	.rel
.neg:	sub	x22,x22,x2
	b	.rel
.end:	cbz	x23,.none
	tbnz	x22,63,.inv
	getv	x0,g_n
	cmp	x22,x0
	b.gt	.inv
	mov	x0,1
	mov	x1,x21
	mov	x2,x22
	b	.out
.none:	mov	x0,0
	mov	x1,x21
	mov	x2,0
	b	.out
.inv:	adr	x0,e_addr
.fail:	bl	fail
	mov	x1,x21
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; --- commandes ----------------------------------------------------------------------------

; x0 = premiere, x1 = derniere ligne, w2 = 'p', 'n' ou 'l' ; la derniere devient la courante
print:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
.l:	cmp	x21,x22
	b.gt	.f
	cmp	w23,'n'
	b.ne	.t
	mov	x0,x21
	bl	t_putn
	mov	w0,9
	bl	t_putc
.t:	getv	x24,g_ln
	ldr	x24,[x24,x21,lsl 3]
	cmp	w23,'l'
	b.eq	.lst
	mov	x0,x24
	bl	t_puts
	b	.nl
.lst:	ldrb	w0,[x24],1
	cbz	w0,.dol
	cmp	w0,9
	b.ne	.bs
	mov	w0,'\'
	bl	t_putc
	mov	w0,'t'
	bl	t_putc
	b	.lst
.bs:	cmp	w0,'\'
	b.ne	.ct
	bl	t_putc
	mov	w0,'\'
	bl	t_putc
	b	.lst
.ct:	cmp	w0,32
	b.hs	.pc
	add	w0,w0,64
	str	x0,[sp,-16]!
	mov	w0,'^'
	bl	t_putc
	ldr	x0,[sp],16
.pc:	bl	t_putc
	b	.lst
.dol:	mov	w0,'$'
	bl	t_putc
.nl:	bl	t_nl
	add	x21,x21,1
	b	.l
.f:	setv	x22,g_cur,x0
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = apres la ligne : les lignes tapees jusqu'a « . » -> x0 = combien
input_lines:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,0
.c:	bl	t_flush
	bl	t_lire_ligne
	cbz	x0,.f
	cmp	x1,1
	b.ne	.ins
	ldrb	w2,[x0]
	cmp	w2,'.'
	b.eq	.f
.ins:	bl	strndup
	mov	x1,x0
	add	x0,x21,x22
	bl	insert
	add	x22,x22,1
	b	.c
.f:	add	x0,x21,x22
	setv	x0,g_cur,x1
	mov	x0,x22
	ldp	x21,x22,[sp],16
	epilogue

; x0 = ligne : la substitution de g_sre par g_srepl -> x0 = 1 si changee (le resultat dans refait)
subst_line:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x21,x0
	mov	x26,x0				; lecture
	adr	x27,refait			; ecriture
	mov	x28,0				; 1 : changee
	mov	x24,0				; correspondances
.c:	getv	x0,g_sre
	mov	x1,x21
	mov	x2,x26
	bl	re_cherche
	cbz	x0,.fin
	add	x24,x24,1
	adr	x1,re_cap
	ldp	x2,x3,[x1]			; debut, fin
.av:	cmp	x26,x2
	b.hs	.dec
	ldrb	w0,[x26],1
	strb	w0,[x27],1
	b	.av
.dec:	getv	x5,g_snth
	cmp	x24,x5
	b.eq	.rep
	b.lo	.verb
	getv	x5,g_sg
	cbnz	x5,.rep
.verb:	mov	x5,x2				; (pas celle-ci : telle quelle)
	mov	x6,x3
	bl	groupe
	b	.rf
.rep:	mov	x28,1
	getv	x4,g_srepl
.r:	ldrb	w0,[x4],1
	cbz	w0,.rf
	cmp	w0,'&'
	b.ne	.bs
	mov	x5,x2
	mov	x6,x3
	bl	groupe
	b	.r
.bs:	cmp	w0,'\'
	b.ne	.p
	ldrb	w0,[x4],1
	cbz	w0,.rf
	sub	w5,w0,'0'
	cmp	w5,9
	b.hi	.lt
	adr	x6,re_cap
	lsl	x5,x5,4
	add	x6,x6,x5
	ldp	x5,x6,[x6]
	cbz	x5,.r
	bl	groupe
	b	.r
.lt:	cmp	w0,'n'
	b.ne	.p
	mov	w0,10
.p:	strb	w0,[x27],1
	b	.r
.rf:	mov	x26,x3
	cmp	x2,x3				; correspondance vide : avance d'un caractere
	b.ne	.g
	ldrb	w0,[x26]
	cbz	w0,.fin
	strb	w0,[x27],1
	add	x26,x26,1
.g:	getv	x5,g_sg
	cbnz	x5,.c
	getv	x5,g_snth
	cmp	x24,x5
	b.lo	.c
.fin:	ldrb	w0,[x26],1			; le reste
	strb	w0,[x27],1
	cbnz	w0,.fin
	mov	x0,x28
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x5, x6 = morceau : copie en x27
groupe:
.c:	cmp	x5,x6
	b.hs	.r
	ldrb	w7,[x5],1
	strb	w7,[x27],1
	b	.c
.r:	ret

; x0 = texte apres « s », x1 = premiere, x2 = derniere ligne -> x0 = 0, -1
substitute:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	ldrb	w0,[x21]
	cbz	w0,.again
	cmp	w0,10
	b.eq	.again
	add	x21,x21,1
	mov	x24,x0				; delimiteur
	cmp	w0,' '
	b.eq	.baddel
	cmp	w0,'\'
	b.eq	.baddel
	mov	x0,x21
	mov	x1,x24
	bl	re_piece
	mov	x21,x1
	cbz	x2,.unterm
	mov	x25,x0				; motif
	mov	x0,x21
	mov	x1,x24
	bl	re_piece
	mov	x21,x1
	mov	x26,x2				; 1 : ferme
	mov	x24,x0				; remplacement
	ldrb	w1,[x25]
	cbz	w1,.oldre
	mov	x0,x25
	bl	compile
	setv	x0,g_re,x1
	b	.set
.oldre:	getv	x0,g_re
	cbz	x0,.nopat
.set:	getv	x0,g_re
	setv	x0,g_sre,x1
	setv	x24,g_srepl,x1
	setv	xzr,g_sg,x1
	setv	xzr,g_sp,x1
	mov	x0,1
	setv	x0,g_snth,x1
.fl:	ldrb	w0,[x21]
	cbz	w0,.fe
	cmp	w0,'g'
	b.ne	.fp
	mov	x0,1
	setv	x0,g_sg,x1
	add	x21,x21,1
	b	.fl
.fp:	cmp	w0,'p'
	b.ne	.fn
	mov	x0,1
	setv	x0,g_sp,x1
	add	x21,x21,1
	b	.fl
.fn:	sub	w1,w0,'0'
	cmp	w1,9
	b.hi	.badflag
	mov	x0,x21
	bl	parse_num
	mov	x21,x1
	setv	x0,g_snth,x2
	b	.fl
.fe:	cbnz	x26,.run			; (s/a/b sans / final : affiche, comme ed)
	mov	x0,1
	setv	x0,g_sp,x1
	b	.run
.again:	getv	x0,g_sre
	cbz	x0,.noprev
.run:	mov	x24,0				; 1 : au moins une
	mov	x25,x22				; i
.l:	cmp	x25,x23
	b.gt	.done
	getv	x0,g_ln
	ldr	x0,[x0,x25,lsl 3]
	bl	subst_line
	cbz	x0,.nx
	mov	x0,x25
	mov	x1,x25
	bl	delete
	sub	x26,x25,1			; (un \n du remplacement coupe la ligne)
	adr	x21,refait
.sp:	mov	x1,x21
.se:	ldrb	w2,[x1]
	cbz	w2,.sx
	cmp	w2,10
	b.eq	.sx
	add	x1,x1,1
	b	.se
.sx:	stp	x1,xzr,[sp,-16]!
	sub	x1,x1,x21
	mov	x0,x21
	bl	strndup
	mov	x1,x0
	mov	x0,x26
	bl	insert
	add	x26,x26,1
	ldp	x1,xzr,[sp],16
	ldrb	w2,[x1]
	cbz	w2,.sd
	add	x21,x1,1
	add	x23,x23,1
	b	.sp
.sd:	mov	x25,x26
	setv	x26,g_cur,x0
	mov	x24,1
.nx:	add	x25,x25,1
	b	.l
.done:	cbz	x24,.nom
	mov	x0,1
	setv	x0,g_dirty,x1
	getv	x0,g_sp
	cbz	x0,.ok
	getv	x0,g_cur
	mov	x1,x0
	mov	w2,'p'
	bl	print
.ok:	mov	x0,0
	b	.out
.baddel:
	adr	x0,e_delim
	b	.f
.unterm:
	adr	x0,e_unterm
	b	.f
.nopat:	adr	x0,e_nopat
	b	.f
.badflag:
	adr	x0,e_sflag
	b	.f
.noprev:
	adr	x0,e_noprev
	b	.f
.nom:	adr	x0,e_nomatch
.f:	bl	fail
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; g/re/cmd, v/re/cmd : x0 = texte apres g, x1 = premiere, x2 = derniere ligne, x3 = 1 pour v
;   -> x0 = 0, -1. Les lignes choisies sont notees d'abord, puis cmd tourne sur chacune encore la.
global_cmd:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	mov	x24,x3
	ldrb	w0,[x21]
	cbz	w0,.baddel
	cmp	w0,' '
	b.eq	.baddel
	mov	x1,x0
	add	x0,x21,1
	bl	re_piece
	mov	x21,x1
	ldrb	w1,[x0]
	cbz	w1,.old
	bl	compile
	setv	x0,g_re,x1
	b	.ok
.old:	getv	x0,g_re
	cbz	x0,.nopat
.ok:	ldrb	w0,[x21]
	cbnz	w0,.h
	adr	x21,s_pcmd
.h:	sub	x0,x23,x22
	add	x0,x0,2
	lsl	x0,x0,3
	bl	malloc
	mov	x25,x0
	mov	x26,0
	mov	x27,x22
.s:	cmp	x27,x23
	b.gt	.run
	getv	x1,g_ln
	ldr	x1,[x1,x27,lsl 3]
	mov	x28,x1
	getv	x0,g_re
	mov	x2,x1
	bl	re_cherche
	cmp	x0,x24
	b.eq	.sn
	str	x28,[x25,x26,lsl 3]
	add	x26,x26,1
.sn:	add	x27,x27,1
	b	.s
.run:	mov	x27,0
	mov	x28,0
.r:	cmp	x27,x26
	b.hs	.end
	cbnz	x28,.end
	ldr	x0,[x25,x27,lsl 3]
	getv	x1,g_ln
	getv	x2,g_n
	mov	x3,1
.fi:	cmp	x3,x2
	b.hi	.nx
	ldr	x4,[x1,x3,lsl 3]
	cmp	x4,x0
	b.eq	.fd
	add	x3,x3,1
	b	.fi
.fd:	setv	x3,g_cur,x4
	mov	x0,x21
	mov	x1,1
	bl	command
	tbz	x0,63,.nx
	mov	x28,-1
.nx:	add	x27,x27,1
	b	.r
.end:	mov	x0,x28
	b	.out
.baddel:
	adr	x0,e_delim
	b	.f
.nopat:	adr	x0,e_nopat
.f:	bl	fail
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = une ligne de commande, x1 = 1 sous g -> x0 = 0, -1 erreur (dite), 1 quitter
;   x21 texte, x22 a, x23 b, x24 adresses donnees, x25 sous g, x26 la commande
command:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x25,x1
	bl	skipb
	mov	x21,x0
	mov	x22,0
	mov	x23,0
	mov	x24,0
	ldrb	w0,[x21]
	cmp	w0,','
	b.eq	.comma
	cmp	w0,';'
	b.ne	.addr
.comma:	mov	x22,1				; , = 1,$   ; = .,$
	cmp	w0,','
	b.eq	.c1
	getv	x22,g_cur
.c1:	getv	x23,g_n
	cbnz	x23,.c2
	mov	x22,0
.c2:	mov	x24,2
	add	x21,x21,1
	mov	x0,x21
	bl	address
	tbnz	x0,63,.ret
	mov	x21,x1
	cbz	x0,.parsed
	mov	x23,x2
	b	.parsed
.addr:	mov	x0,x21
	bl	address
	tbnz	x0,63,.ret
	mov	x21,x1
	cbz	x0,.parsed
	mov	x22,x2
	mov	x23,x2
	mov	x24,1
	mov	x0,x21
	bl	skipb
	mov	x21,x0
	ldrb	w0,[x21]
	cmp	w0,','
	b.eq	.sep
	cmp	w0,';'
	b.ne	.parsed
	setv	x22,g_cur,x1
.sep:	add	x21,x21,1
	mov	x24,2
	mov	x0,x21
	bl	address
	tbnz	x0,63,.ret
	mov	x21,x1
	getv	x23,g_n
	cbz	x0,.parsed
	mov	x23,x2
.parsed:
	cmp	x24,2
	b.ne	.p2
	cmp	x22,x23
	b.gt	.badaddr
.p2:	mov	x0,x21
	bl	skipb
	mov	x21,x0
	ldrb	w26,[x21]
	cbz	w26,.nc
	add	x21,x21,1
.nc:	cbnz	x25,.nos			; (une commande qui change le texte : u l'annule)
	cbz	w26,.nos
	adr	x0,s_snap
.sn:	ldrb	w1,[x0],1
	cbz	w1,.nos
	cmp	w1,w26
	b.ne	.sn
	bl	snapshot
.nos:	cmp	w26,'q'
	b.eq	.disp
	cmp	w26,'e'
	b.eq	.disp
	setv	xzr,g_warned,x0
.disp:	cbnz	w26,.ka
	; une adresse seule, une ligne vide
	cbnz	x24,.e1
	getv	x23,g_cur
	add	x23,x23,1
.e1:	cmp	x23,1
	b.lt	.badaddr
	getv	x0,g_n
	cmp	x23,x0
	b.gt	.badaddr
	mov	x0,x23
	mov	x1,x23
	mov	w2,'p'
	bl	print
	b	.ok0
.ka:	cmp	w26,'a'
	b.ne	.ki
	getv	x0,g_cur
	defaults x0,x0
	mov	x0,x23
	bl	input_lines
.dirty_if:
	cbz	x0,.ok0
.setdirty:
	mov	x0,1
	setv	x0,g_dirty,x1
	b	.ok0
.ki:	cmp	w26,'i'
	b.ne	.kc
	getv	x0,g_cur
	defaults x0,x0
	subs	x0,x23,1
	csel	x0,xzr,x0,lt
	bl	input_lines
	b	.dirty_if
.kc:	cmp	w26,'c'
	b.ne	.kd
	getv	x0,g_cur
	defaults x0,x0
	need
	mov	x0,x22
	mov	x1,x23
	bl	delete
	mov	x0,1
	setv	x0,g_dirty,x1
	sub	x0,x22,1
	bl	input_lines
	b	.ok0
.kd:	cmp	w26,'d'
	b.ne	.kj
	getv	x0,g_cur
	defaults x0,x0
	need
	mov	x0,x22
	mov	x1,x23
	bl	delete
	mov	x0,1
	setv	x0,g_dirty,x1
	getv	x0,g_n
	cmp	x22,x0
	csel	x0,x22,x0,le
	setv	x0,g_cur,x1
	b	.ok0
.kj:	cmp	w26,'j'
	b.ne	.kp
	getv	x0,g_cur
	add	x1,x0,1
	defaults x0,x1
	need
	cmp	x22,x23
	b.eq	.ok0
	mov	x27,0				; longueur
	mov	x28,x22
.jl:	cmp	x28,x23
	b.gt	.ja
	getv	x0,g_ln
	ldr	x0,[x0,x28,lsl 3]
	bl	str_len
	add	x27,x27,x0
	add	x28,x28,1
	b	.jl
.ja:	add	x0,x27,1
	bl	malloc
	mov	x27,x0
	mov	x3,x0
	mov	x28,x22
.jc:	cmp	x28,x23
	b.gt	.jd
	getv	x1,g_ln
	ldr	x1,[x1,x28,lsl 3]
.jb:	ldrb	w2,[x1],1
	cbz	w2,.jn
	strb	w2,[x3],1
	b	.jb
.jn:	add	x28,x28,1
	b	.jc
.jd:	strb	wzr,[x3]
	mov	x0,x22
	mov	x1,x23
	bl	delete
	sub	x0,x22,1
	mov	x1,x27
	bl	insert
	setv	x22,g_cur,x0
	b	.setdirty
.kp:	cmp	w26,'p'
	b.eq	.pr
	cmp	w26,'n'
	b.eq	.pr
	cmp	w26,'l'
	b.ne	.ks
.pr:	getv	x0,g_cur
	defaults x0,x0
	need
	mov	x0,x22
	mov	x1,x23
	mov	w2,w26
	bl	print
	b	.ok0
.ks:	cmp	w26,'s'
	b.ne	.km
	getv	x0,g_cur
	defaults x0,x0
	need
	mov	x0,x21
	mov	x1,x22
	mov	x2,x23
	bl	substitute
	b	.ret
.km:	cmp	w26,'m'
	b.eq	.mt
	cmp	w26,'t'
	b.ne	.kk
.mt:	getv	x0,g_cur
	defaults x0,x0
	need
	mov	x0,x21
	bl	address
	tbnz	x0,63,.ret
	mov	x24,x2				; destination
	cbnz	x0,.mt1
	getv	x24,g_cur
.mt1:	sub	x25,x23,x22
	add	x25,x25,1			; k
	lsl	x0,x25,3
	bl	malloc
	mov	x21,x0
	mov	x27,0
.mb:	cmp	x27,x25
	b.hs	.mm
	getv	x0,g_ln
	add	x1,x22,x27
	ldr	x0,[x0,x1,lsl 3]
	cmp	w26,'t'
	b.ne	.mb1
	bl	strdup
.mb1:	str	x0,[x21,x27,lsl 3]
	add	x27,x27,1
	b	.mb
.mm:	cmp	w26,'m'
	b.ne	.mins
	cmp	x24,x22
	b.lt	.mdel
	cmp	x24,x23
	b.lt	.baddest
.mdel:	mov	x0,x22
	mov	x1,x23
	bl	delete
	cmp	x24,x23
	b.lt	.mins
	sub	x24,x24,x25
.mins:	mov	x27,0
.mil:	cmp	x27,x25
	b.hs	.mie
	add	x0,x24,x27
	ldr	x1,[x21,x27,lsl 3]
	bl	insert
	add	x27,x27,1
	b	.mil
.mie:	add	x0,x24,x25
	setv	x0,g_cur,x1
	b	.setdirty
.kk:	cmp	w26,'k'
	b.ne	.kg
	getv	x0,g_cur
	defaults x0,x0
	need
	ldrb	w0,[x21]
	sub	w0,w0,'a'
	cmp	w0,25
	b.hi	.badmark
	getv	x1,g_ln
	ldr	x1,[x1,x23,lsl 3]
	adr	x2,g_mark
	str	x1,[x2,x0,lsl 3]
	b	.ok0
.kg:	cmp	w26,'g'
	b.eq	.gv
	cmp	w26,'v'
	b.ne	.kr
.gv:	cbnz	x25,.gg
	getv	x1,g_n
	mov	x0,1
	defaults x0,x1
	getv	x0,g_n
	cbz	x0,.ok0
	need
	cmp	w26,'v'
	cset	x3,eq
	mov	x0,x21
	mov	x1,x22
	mov	x2,x23
	bl	global_cmd
	b	.ret
.kr:	cmp	w26,'r'
	b.ne	.kw
	getv	x0,g_n
	defaults x0,x0
	adr	x0,g_keep			; (r ne renomme pas le texte)
	adr	x1,g_file
	bl	strcpy
	mov	x0,x21
	bl	filename
	ldrb	w1,[x0]
	cbz	w1,.nofile
	mov	x1,x23
	bl	read_file
	mov	x27,x0
	adr	x0,g_keep
	ldrb	w1,[x0]
	cbz	w1,.r1
	adr	x0,g_file
	adr	x1,g_keep
	bl	strcpy
.r1:	tbnz	x27,63,.cantopen
	mov	x0,x27
	bl	t_putn
	bl	t_nl
	b	.setdirty
.kw:	cmp	w26,'w'
	b.eq	.ww
	cmp	w26,'W'
	b.ne	.ke
.ww:	mov	x27,0				; 1 : wq
	ldrb	w0,[x21]
	cmp	w0,'q'
	b.ne	.w1
	mov	x27,1
	add	x21,x21,1
.w1:	getv	x1,g_n
	mov	x0,1
	defaults x0,x1
	getv	x0,g_n
	cbz	x0,.w2
	need
.w2:	mov	x0,x21
	bl	filename
	ldrb	w1,[x0]
	cbz	w1,.nofile
	mov	x1,x22
	getv	x2,g_n
	cmp	x2,0
	csel	x2,x23,xzr,ne
	cmp	w26,'W'
	cset	x3,eq
	bl	write_file
	tbnz	x0,63,.cantwrite
	bl	t_putn
	bl	t_nl
	cbnz	x24,.w3
	setv	xzr,g_dirty,x0
.w3:	mov	x0,x27
	b	.ret
.ke:	cmp	w26,'e'
	b.eq	.ee
	cmp	w26,'E'
	b.ne	.kf
.ee:	cmp	w26,'e'
	b.ne	.e2
	getv	x0,g_dirty
	cbz	x0,.e2
	getv	x0,g_warned
	cbnz	x0,.e2
	mov	x0,1
	setv	x0,g_warned,x1
	adr	x0,e_unsaved_e
	b	.fail
.e2:	mov	x0,x21
	bl	filename
	ldrb	w1,[x0]
	cbz	w1,.nofile
	setv	xzr,g_n,x1
	setv	xzr,g_cur,x1
	setv	xzr,g_dirty,x1
	setv	xzr,u_ok,x1
	mov	x1,0
	bl	read_file
	tbnz	x0,63,.cantopen
	bl	t_putn
	bl	t_nl
	b	.ok0
.kf:	cmp	w26,'f'
	b.ne	.keq
	mov	x0,x21
	bl	filename
	bl	t_puts
	bl	t_nl
	b	.ok0
.keq:	cmp	w26,'='
	b.ne	.ku
	getv	x0,g_n
	defaults x0,x0
	mov	x0,x23
	bl	t_putn
	bl	t_nl
	b	.ok0
.ku:	cmp	w26,'u'
	b.ne	.kq
	getv	x0,u_ok
	cbz	x0,.noundo
	getv	x0,g_n				; le texte d'avant, et celui-ci pour u encore
	add	x0,x0,2
	lsl	x0,x0,3
	bl	malloc
	mov	x27,x0
	getv	x1,g_ln
	getv	x2,g_n
	mov	x3,1
.u1:	cmp	x3,x2
	b.hi	.u2
	ldr	x4,[x1,x3,lsl 3]
	str	x4,[x27,x3,lsl 3]
	add	x3,x3,1
	b	.u1
.u2:	getv	x28,g_n
	getv	x0,u_n
	bl	reserve
	getv	x1,g_ln
	getv	x2,u_ln
	getv	x5,u_n
	mov	x3,1
.u3:	cmp	x3,x5
	b.hi	.u4
	ldr	x4,[x2,x3,lsl 3]
	str	x4,[x1,x3,lsl 3]
	add	x3,x3,1
	b	.u3
.u4:	setv	x5,g_n,x0
	getv	x6,g_cur
	getv	x0,u_cur
	setv	x0,g_cur,x1
	setv	x27,u_ln,x1
	setv	x28,u_n,x1
	setv	x6,u_cur,x1
	b	.setdirty
.kq:	cmp	w26,'q'
	b.ne	.kQ
	getv	x0,g_dirty
	cbz	x0,.quit
	getv	x0,g_warned
	cbnz	x0,.quit
	mov	x0,1
	setv	x0,g_warned,x1
	adr	x0,e_unsaved_q
	b	.fail
.quit:	mov	x0,1
	b	.ret
.kQ:	cmp	w26,'Q'
	b.eq	.quit
	cmp	w26,'h'
	b.ne	.kH
	getv	x0,g_errmsg
	cbz	x0,.h0
	ldrb	w1,[x0]
	cbnz	w1,.h1
.h0:	adr	x0,e_none
.h1:	bl	t_dire
	bl	t_nl
	b	.ok0
.kH:	cmp	w26,'H'
	b.ne	.unk
	getv	x0,g_verbose
	eor	x0,x0,1
	setv	x0,g_verbose,x1
	b	.ok0
.unk:	adr	x0,e_unknown
	b	.fail
.badaddr:
	adr	x0,e_addr
	b	.fail
.baddest:
	adr	x0,e_dest
	b	.fail
.badmark:
	adr	x0,e_mark
	b	.fail
.gg:	adr	x0,e_gg
	b	.fail
.nofile:
	adr	x0,e_nofile
	b	.fail
.cantopen:
	adr	x0,e_open
	b	.fail
.cantwrite:
	adr	x0,e_write
	b	.fail
.noundo:
	adr	x0,e_undo
.fail:	bl	fail
	b	.ret
.ok0:	mov	x0,0
.ret:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

include '../../inc/regex.inc'

s_vide		db 0
s_p		db '-p',0
s_quest		db '?',10,0
s_snap		db 'aicdjsmtgvr',0
s_pcmd		db 'p',0
m_new		db ' : nouveau fichier',10,0
e_mark		db 'marque invalide',0
e_gone		db 'la ligne marqu',0xC3,0xA9,'e n',0x27,'existe plus',0
e_nopat		db 'pas de motif pr',0xC3,0xA9,'c',0xC3,0xA9,'dent',0
e_nomatch	db 'aucune correspondance',0
e_addr		db 'adresse invalide',0
e_delim		db 'd',0xC3,0xA9,'limiteur invalide',0
e_unterm	db 'commande s non termin',0xC3,0xA9,'e',0
e_sflag		db 'drapeau de s invalide',0
e_noprev	db 'pas de substitution pr',0xC3,0xA9,'c',0xC3,0xA9,'dente',0
e_dest		db 'destination invalide',0
e_gg		db 'g dans g',0
e_nofile	db 'pas de nom de fichier',0
e_open		db 'fichier illisible',0
e_write		db 0xC3,0xA9,'criture impossible',0
e_unsaved_e	db 'texte non enregistr',0xC3,0xA9,' (e encore pour l',0x27,'abandonner)',0
e_unsaved_q	db 'texte non enregistr',0xC3,0xA9,' (q encore pour quitter)',0
e_undo		db 'rien ',0xC3,0xA0,' annuler',0
e_none		db 'pas d',0x27,'erreur',0
e_unknown	db 'commande inconnue',0
		align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
		align 8
g_ln		dd 0				; les lignes, g_ln[1..g_n]
g_n		dd 0
g_cap		dd 0
g_cur		dd 0				; « . »
g_dirty		dd 0
g_warned	dd 0
g_verbose	dd 0
g_errmsg	dd 0
g_prompt	dd 0
g_re		dd 0				; le dernier motif (compile)
g_sre		dd 0				; la derniere substitution
g_srepl		dd 0
g_sg		dd 0
g_snth		dd 0
g_sp		dd 0
u_ln		dd 0				; u : le texte d'avant la derniere commande
u_n		dd 0
u_cur		dd 0
u_ok		dd 0
g_mark		rb 26*8				; 'a .. 'z : le bloc de la ligne marquee
g_file		rb 256
g_keep		rb 256
regex_data
tool_data
		align 8
refait		rb 2*T_LIGNE
