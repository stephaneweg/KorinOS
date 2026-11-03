; sh_word.inc - sh : variables, developpement des mots, motifs (docs/SHELL.md)
;
; Un mot est developpe dans l'arene (ar_p) : guillemets, $VAR, ${VAR...}, $(...), `...`, $((...)).
; Un caractere special venu d'un texte cite garde devant lui l'octet ESC, pour que le decoupage
; et les motifs ne le voient pas ; il est retire a la fin.

ESC		= 1
VARS_MAX	= 1024
VE_TEXTE	= 0			; « NOM=VALEUR » (dans varm)
VE_DRAP		= 8			; 1 : exportee
VE_TAILLE	= 16

; ---------------------------------------------------------------------------
;  Variables
; ---------------------------------------------------------------------------

; x0 = nom, x1 = longueur -> x0 = entree, 0 si absente
var_cherche:
	adr	x2,vars
	adr	x3,vars_n
	ldr	x3,[x3]
.e:	cbz	x3,.non
	ldr	x4,[x2,VE_TEXTE]
	cbz	x4,.n
	mov	x5,0
.c:	cmp	x5,x1
	b.hs	.f
	ldrb	w6,[x4,x5]
	ldrb	w7,[x0,x5]
	cmp	w6,w7
	b.ne	.n
	add	x5,x5,1
	b	.c
.f:	ldrb	w6,[x4,x5]
	cmp	w6,'='
	b.eq	.oui
.n:	add	x2,x2,VE_TAILLE
	sub	x3,x3,1
	b	.e
.oui:	mov	x0,x2
	ret
.non:	mov	x0,0
	ret

; x0 = nom, x1 = longueur -> x0 = valeur, 0 si absente
var_valeur:
	prologue
	str	x1,[sp,-16]!
	bl	var_cherche
	ldr	x1,[sp],16
	cbz	x0,.out
	ldr	x0,[x0,VE_TEXTE]
	add	x0,x0,x1
	add	x0,x0,1
.out:	epilogue

; x0 = nom, x1 = longueur, x2 = valeur, x3 = 1 : exportee (0 : comme avant)
var_pose:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	bl	var_cherche
	cbnz	x0,.vu
	adr	x1,vars_n
	ldr	x2,[x1]
	cmp	x2,VARS_MAX
	b.hs	.out
	add	x3,x2,1
	str	x3,[x1]
	adr	x0,vars
	add	x0,x0,x2,lsl 4
	str	xzr,[x0,VE_DRAP]
.vu:	ldr	x1,[x0,VE_DRAP]
	orr	x1,x1,x22
	str	x1,[x0,VE_DRAP]
	adr	x1,varm_p			; le nouveau texte (jamais rendu : peu de
	ldr	x2,[x1]				; variables changent)
	str	x2,[x0,VE_TEXTE]
	mov	x3,0
.n:	cmp	x3,x20
	b.hs	.eg
	ldrb	w4,[x19,x3]
	strb	w4,[x2],1
	add	x3,x3,1
	b	.n
.eg:	mov	w4,'='
	strb	w4,[x2],1
	cbz	x21,.fin
.v:	ldrb	w4,[x21],1
	cbz	w4,.fin
	strb	w4,[x2],1
	b	.v
.fin:	strb	wzr,[x2],1
	str	x2,[x1]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = « NOM=VALEUR », x1 = drapeau d'exportation
var_pose_kv:
	mov	x2,x0
.c:	ldrb	w3,[x2]
	cbz	w3,.r
	cmp	w3,'='
	b.eq	.g
	add	x2,x2,1
	b	.c
.g:	mov	x3,x1
	sub	x1,x2,x0
	add	x2,x2,1
	b	var_pose
.r:	ret

; x0 = nom (chaine) : retiree
var_retire:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	str_len
	mov	x1,x0
	mov	x0,x19
	bl	var_cherche
	cbz	x0,.out
	str	xzr,[x0,VE_TEXTE]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = tout (1 : aussi celles qui ne sont pas exportees) -> x0 = tableau sur at_p, termine par 0
env_tableau:
	adr	x1,at_p
	ldr	x2,[x1]
	mov	x3,x2				; (le tableau)
	adr	x4,vars
	adr	x5,vars_n
	ldr	x5,[x5]
.e:	cbz	x5,.f
	ldr	x6,[x4,VE_TEXTE]
	cbz	x6,.n
	ldr	x7,[x4,VE_DRAP]
	orr	x7,x7,x0
	cbz	x7,.n
	str	x6,[x2],8
.n:	add	x4,x4,VE_TAILLE
	sub	x5,x5,1
	b	.e
.f:	str	xzr,[x2],8
	str	x2,[x1]
	mov	x0,x3
	ret

; ---------------------------------------------------------------------------
;  Mots
; ---------------------------------------------------------------------------

; w0 = caractere -> x1 = 1 s'il termine un mot (ne touche que x1)
fin_mot:
	mov	x1,1
	cbz	w0,.r
	cmp	w0,' '
	b.eq	.r
	cmp	w0,9
	b.eq	.r
	cmp	w0,10
	b.eq	.r
	cmp	w0,13
	b.eq	.r
	cmp	w0,';'
	b.eq	.r
	cmp	w0,'&'
	b.eq	.r
	cmp	w0,'|'
	b.eq	.r
	cmp	w0,'<'
	b.eq	.r
	cmp	w0,'>'
	b.eq	.r
	cmp	w0,'('
	b.eq	.r
	cmp	w0,')'
	b.eq	.r
	mov	x1,0
.r:	ret

; x28 juste apres « ( » -> x28 juste apres la « ) » correspondante
sauter_parens:
	prologue
	str	x19,[sp,-16]!
	mov	x19,1
.c:	ldrb	w0,[x28]
	cbz	w0,.fin
	add	x28,x28,1
	cmp	w0,'('
	b.ne	.p
	add	x19,x19,1
	b	.c
.p:	cmp	w0,')'
	b.ne	.q
	sub	x19,x19,1
	cbnz	x19,.c
	b	.out
.q:	cmp	w0,'\'
	b.ne	.s
	ldrb	w0,[x28]
	cbz	w0,.c
	add	x28,x28,1
	b	.c
.s:	cmp	w0,39
	b.ne	.d
.s1:	ldrb	w0,[x28]
	cbz	w0,.fin
	add	x28,x28,1
	cmp	w0,39
	b.ne	.s1
	b	.c
.d:	cmp	w0,'"'
	b.ne	.c
	bl	sauter_dq
	b	.c
.fin:	bl	texte_coupe
.out:	ldr	x19,[sp],16
	epilogue

; x28 juste apres un « " » -> juste apres le « " » fermant
sauter_dq:
	prologue
.c:	ldrb	w0,[x28]
	cbz	w0,.fin
	add	x28,x28,1
	cmp	w0,'"'
	b.eq	.out
	cmp	w0,'\'
	b.ne	.d
	ldrb	w0,[x28]
	cbz	w0,.c
	add	x28,x28,1
	b	.c
.d:	cmp	w0,'$'
	b.ne	.b
	ldrb	w0,[x28]
	cmp	w0,'('
	b.ne	.c
	add	x28,x28,1
	bl	sauter_parens
	b	.c
.b:	cmp	w0,'`'
	b.ne	.c
.b1:	ldrb	w0,[x28]
	cbz	w0,.fin
	add	x28,x28,1
	cmp	w0,'`'
	b.ne	.b1
	b	.c
.fin:	bl	texte_coupe
.out:	epilogue

; x28 sur un mot : le passe, sans rien developper
mot_sauter:
	prologue
.c:	ldrb	w0,[x28]
	bl	fin_mot
	cbnz	x1,.out
	add	x28,x28,1
	cmp	w0,'\'
	b.ne	.s
	ldrb	w0,[x28]
	cbz	w0,.out
	add	x28,x28,1
	b	.c
.s:	cmp	w0,39
	b.ne	.d
.s1:	ldrb	w0,[x28]
	cbz	w0,.fin
	add	x28,x28,1
	cmp	w0,39
	b.ne	.s1
	b	.c
.d:	cmp	w0,'"'
	b.ne	.do
	bl	sauter_dq
	b	.c
.do:	cmp	w0,'$'
	b.ne	.b
	ldrb	w0,[x28]
	cmp	w0,'('
	b.ne	.acc
	add	x28,x28,1
	bl	sauter_parens
	b	.c
.acc:	cmp	w0,'{'
	b.ne	.c
.a1:	ldrb	w0,[x28]
	cbz	w0,.fin
	add	x28,x28,1
	cmp	w0,'}'
	b.ne	.a1
	b	.c
.b:	cmp	w0,'`'
	b.ne	.c
.b1:	ldrb	w0,[x28]
	cbz	w0,.fin
	add	x28,x28,1
	cmp	w0,'`'
	b.ne	.b1
	b	.c
.fin:	bl	texte_coupe
.out:	epilogue

; Le texte finit au milieu d'une construction : en interactif, une ligne de plus
texte_coupe:
	adr	x0,incomplet
	mov	x1,1
	str	x1,[x0]
	ret

; w0 -> pose le caractere ; x23 = 1 : venu d'un texte cite (x20 : ecriture)
pose_cite:
	cmp	w0,'*'
	b.eq	.e
	cmp	w0,'?'
	b.eq	.e
	cmp	w0,'['
	b.eq	.e
	cmp	w0,']'
	b.eq	.e
	cmp	w0,ESC
	b.ne	.p
.e:	mov	w1,ESC
	strb	w1,[x20],1
.p:	strb	w0,[x20],1
	ret

; x0 = mode : 0 : champs (decoupage, motifs) poses sur at_p ; 1 : une chaine -> x0 = son adresse ;
; 2 : une chaine, ESC gardes (motif de case)
mot_etendre:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	adr	x0,ar_p
	ldr	x20,[x0]
	mov	x21,x20				; debut du champ
	mov	x22,0				; 1 : le champ existe, meme vide
.c:	ldrb	w0,[x28]
	bl	fin_mot
	cbnz	x1,.fin
	add	x28,x28,1
	cmp	w0,'\'
	b.ne	.s
	ldrb	w0,[x28]
	cbz	w0,.fin
	add	x28,x28,1
	cmp	w0,10				; (suite de ligne)
	b.eq	.c
	bl	pose_cite
	mov	x22,1
	b	.c
.s:	cmp	w0,39
	b.ne	.d
	mov	x22,1
.s1:	ldrb	w0,[x28]
	cbz	w0,.coupe
	add	x28,x28,1
	cmp	w0,39
	b.eq	.c
	bl	pose_cite
	b	.s1
.d:	cmp	w0,'"'
	b.ne	.do
	mov	x22,1
.d1:	ldrb	w0,[x28]
	cbz	w0,.coupe
	add	x28,x28,1
	cmp	w0,'"'
	b.eq	.c
	cmp	w0,'\'
	b.ne	.d2
	ldrb	w0,[x28]
	cmp	w0,'"'				; \ ne protege que ceux-ci
	b.eq	.d3
	cmp	w0,'\'
	b.eq	.d3
	cmp	w0,'$'
	b.eq	.d3
	cmp	w0,'`'
	b.eq	.d3
	cmp	w0,10
	b.eq	.d4
	mov	w0,'\'
	bl	pose_cite
	b	.d1
.d4:	add	x28,x28,1
	b	.d1
.d3:	add	x28,x28,1
	bl	pose_cite
	b	.d1
.d2:	cmp	w0,'$'
	b.ne	.d5
	sub	x28,x28,1
	mov	x1,1
	bl	dollar
	b	.d1
.d5:	cmp	w0,'`'
	b.ne	.d6
	mov	x1,1
	bl	guillemet_arriere
	b	.d1
.d6:	bl	pose_cite
	b	.d1
.do:	cmp	w0,'$'
	b.ne	.bq
	sub	x28,x28,1
	mov	x1,0
	bl	dollar
	b	.c
.bq:	cmp	w0,'`'
	b.ne	.lit
	mov	x1,0
	bl	guillemet_arriere
	b	.c
.lit:	strb	w0,[x20],1
	b	.c
.coupe:	bl	texte_coupe
.fin:	cbnz	x19,.chaine
	cmp	x20,x21
	b.ne	.dern
	cbz	x22,.ok
.dern:	bl	finir_champ
.ok:	adr	x0,ar_p
	str	x20,[x0]
	mov	x0,0
	b	.out
.chaine:
	strb	wzr,[x20],1
	adr	x0,ar_p
	str	x20,[x0]
	mov	x0,x21
	cmp	x19,2
	b.eq	.out
	bl	sans_esc
	mov	x0,x21
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine : les ESC retires, sur place
sans_esc:
	mov	x1,x0
.c:	ldrb	w2,[x1],1
	cmp	w2,ESC
	b.ne	.p
	ldrb	w2,[x1],1
.p:	strb	w2,[x0],1
	cbnz	w2,.c
	ret

; (dans mot_etendre, mode 0) le champ [x21, x20[ : motif developpe ou pose ; champ suivant
finir_champ:
	prologue
	strb	wzr,[x20],1
	mov	x0,x21
	bl	a_motif
	cbz	x0,.lit
	bl	motif_etendre
	cbnz	x0,.suite
.lit:	mov	x0,x21
	bl	sans_esc
	mov	x0,x21
	bl	at_pousse
.suite:	mov	x21,x20
	mov	x22,0
	epilogue

; x0 -> pose sur at_p
at_pousse:
	adr	x1,at_p
	ldr	x2,[x1]
	str	x0,[x2],8
	str	x2,[x1]
	ret

; x0 = champ -> x0 = 1 s'il contient * ? [ hors citation
a_motif:
.c:	ldrb	w1,[x0],1
	cbz	w1,.non
	cmp	w1,ESC
	b.ne	.t
	ldrb	w1,[x0],1
	cbz	w1,.non
	b	.c
.t:	cmp	w1,'*'
	b.eq	.oui
	cmp	w1,'?'
	b.eq	.oui
	cmp	w1,'['
	b.ne	.c
.oui:	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = valeur, x1 = 1 : entre guillemets ; dans mot_etendre : posee, decoupee aux blancs sinon
pose_valeur:
	prologue
	stp	x24,x25,[sp,-16]!
	mov	x24,x0
	mov	x25,x1
	cbz	x24,.out
.c:	ldrb	w0,[x24],1
	cbz	w0,.out
	cbz	x25,.nc
	bl	pose_cite
	b	.c
.nc:	cbnz	x19,.lit			; (une chaine : pas de decoupage)
	cmp	w0,' '
	b.eq	.bl
	cmp	w0,9
	b.eq	.bl
	cmp	w0,10
	b.eq	.bl
.lit:	strb	w0,[x20],1
	b	.c
.bl:	cmp	x20,x21
	b.ne	.fc
	cbz	x22,.c
.fc:	bl	finir_champ
	b	.c
.out:	ldp	x24,x25,[sp],16
	epilogue

; x28 sur « $ », x1 = 1 : entre guillemets
dollar:
	prologue
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	sub	sp,sp,32
	mov	x24,x1
	ldrb	w0,[x28,1]
	cmp	w0,'('
	b.ne	.acc
	ldrb	w1,[x28,2]
	cmp	w1,'('
	b.ne	.subst
	add	x28,x28,3			; $(( ... ))
	bl	arith
	mov	x1,sp
	cmp	x0,0
	b.ge	.pos
	neg	x0,x0
	mov	w2,'-'
	strb	w2,[x1],1
.pos:	bl	fmt_dec
	bl	sauter_blancs_arith
	ldrb	w0,[x28]
	cmp	w0,')'
	b.ne	.a1
	add	x28,x28,1
.a1:	ldrb	w0,[x28]
	cmp	w0,')'
	b.ne	.a2
	add	x28,x28,1
.a2:	mov	x0,sp
	mov	x1,x24
	bl	pose_valeur
	b	.out
.subst:	add	x28,x28,2
	mov	x25,x28
	bl	sauter_parens
	mov	x0,x25
	sub	x1,x28,1
	mov	x2,x24
	bl	capturer
	b	.out
.acc:	cmp	w0,'{'
	b.ne	.spec
	add	x28,x28,2
	mov	x25,x28				; le nom
	ldrb	w0,[x28]
	cmp	w0,'#'				; ${#VAR}
	b.ne	.nom
	add	x28,x28,1
	mov	x25,x28
	bl	nom_passer
	ldrb	w0,[x28]
	cmp	w0,'}'
	b.ne	.coupe
	mov	x0,x25
	sub	x1,x28,x25
	add	x28,x28,1
	bl	val_nommee
	cbz	x0,.lz
	bl	str_len
.lz:	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	mov	x1,x24
	bl	pose_valeur
	b	.out
.nom:	bl	nom_passer
	cmp	x28,x25				; ${1}, ${?}...
	b.ne	.nomf
	add	x28,x28,1
.nomf:	sub	x26,x28,x25			; longueur
	ldrb	w27,[x28]
	cmp	w27,'}'
	b.ne	.op
	add	x28,x28,1
	mov	x0,x25
	mov	x1,x26
	bl	val_nommee
	mov	x1,x24
	bl	pose_valeur
	b	.out
.op:	mov	x4,0				; 1 : « : » (vide compte comme absent)
	cmp	w27,':'
	b.ne	.op1
	mov	x4,1
	add	x28,x28,1
	ldrb	w27,[x28]
.op1:	add	x28,x28,1
	str	x4,[sp,16]
	mov	x0,x25
	mov	x1,x26
	bl	val_nommee
	str	x0,[sp,24]			; la valeur (0 : absente)
	ldr	x4,[sp,16]
	cbz	x0,.abs
	cbz	x4,.pres
	ldrb	w1,[x0]
	cbz	w1,.abs
.pres:	cmp	w27,'+'
	b.eq	.motif
	b	.sans
.abs:	cmp	w27,'+'
	b.eq	.sans
.motif:	mov	x1,x24				; le mot qui suit l'operateur
	bl	acc_mot
	cmp	w27,'='
	b.ne	.out
	strb	wzr,[x20]			; ${VAR:=x} (mot simple)
	mov	x0,x25
	mov	x1,x26
	mov	x2,x21
	mov	x3,0
	bl	var_pose
	b	.out
.sans:	bl	acc_sauter
	ldr	x0,[sp,24]
	cmp	w27,'+'
	b.ne	.v
	mov	x0,0
.v:	mov	x1,x24
	bl	pose_valeur
	b	.out
.coupe:	bl	texte_coupe
	b	.out
.spec:	cmp	w0,'?'
	b.ne	.spec2
	add	x28,x28,2
	adr	x0,last_code
	ldr	x0,[x0]
	bl	code_shell
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	mov	x1,x24
	bl	pose_valeur
	b	.out
.spec2:	cmp	w0,'@'
	b.eq	.at
	cmp	w0,'*'
	b.ne	.spec3
.at:	mov	w27,w0
	add	x28,x28,2
	adr	x25,pos_n
	ldr	x25,[x25]
	mov	x26,1				; $1...
.ai:	cmp	x26,x25
	b.hi	.out
	adr	x0,pos_v
	ldr	x0,[x0]
	ldr	x0,[x0,x26,lsl 3]
	mov	x1,x24
	bl	pose_valeur
	add	x26,x26,1
	cmp	x26,x25
	b.hi	.out
	cbz	x24,.asep
	cmp	w27,'@'
	b.ne	.asp2
	mov	x22,1				; "$@" : un champ par parametre
	bl	finir_champ
	mov	x22,1
	b	.ai
.asp2:	mov	w0,' '
	strb	w0,[x20],1
	b	.ai
.asep:	cmp	x20,x21
	b.eq	.ai
	cbnz	x19,.asp2
	bl	finir_champ
	b	.ai
.spec3:	cmp	w0,'#'
	b.ne	.spec4
	add	x28,x28,2
	adr	x0,pos_n
	ldr	x0,[x0]
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	mov	x1,x24
	bl	pose_valeur
	b	.out
.spec4:	cmp	w0,'$'
	b.ne	.spec5
	add	x28,x28,2
	sys	SYS_GETPID
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	mov	x1,x24
	bl	pose_valeur
	b	.out
.spec5:	add	x28,x28,1
	mov	x25,x28
	sub	w1,w0,'0'			; $0 a $9 : un seul chiffre
	cmp	w1,9
	b.hi	.nm
	add	x28,x28,1
	b	.nv
.nm:	bl	nom_passer
	cmp	x28,x25
	b.eq	.lit
.nv:	mov	x0,x25
	sub	x1,x28,x25
	bl	val_nommee
	mov	x1,x24
	bl	pose_valeur
	b	.out
.lit:	mov	w0,'$'				; un $ seul
	strb	w0,[x20],1
.out:	add	sp,sp,32
	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	epilogue

; x28 : passe un nom de variable (lettres, chiffres, _)
nom_passer:
.c:	ldrb	w0,[x28]
	cmp	w0,'_'
	b.eq	.n
	sub	w1,w0,'0'
	cmp	w1,9
	b.ls	.n
	orr	w1,w0,0x20
	sub	w1,w1,'a'
	cmp	w1,25
	b.hi	.r
.n:	add	x28,x28,1
	b	.c
.r:	ret

; x0 = nom, x1 = longueur -> x0 = valeur (positionnels et speciaux compris), 0 si absente
val_nommee:
	cmp	x1,1
	b.ne	.v
	ldrb	w2,[x0]
	cmp	w2,'?'
	b.eq	.code
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.v
	adr	x3,pos_n
	ldr	x3,[x3]
	cmp	x2,x3
	b.hi	.non
	adr	x3,pos_v
	ldr	x3,[x3]
	ldr	x0,[x3,x2,lsl 3]
	ret
.non:	mov	x0,0
	ret
.code:	prologue
	adr	x0,last_code
	ldr	x0,[x0]
	bl	code_shell
	adr	x1,tmp_num
	bl	fmt_dec
	adr	x0,tmp_num
	epilogue
.v:	b	var_valeur

; x0 = code du systeme -> x0 = code du shell, de 0 a 255 (-9 : 137...)
code_shell:
	tbz	x0,63,.m
	neg	x0,x0
	add	x0,x0,128
.m:	and	x0,x0,255
	ret

; x28 apres l'operateur de ${VAR:-mot} : le mot, developpe, jusqu'a « } » ; x1 = entre guillemets
acc_mot:
	prologue
	str	x24,[sp,-16]!
	mov	x24,x1
.c:	ldrb	w0,[x28]
	cbz	w0,.coupe
	add	x28,x28,1
	cmp	w0,'}'
	b.eq	.out
	cmp	w0,'"'
	b.eq	.c
	cmp	w0,39
	b.eq	.c
	cmp	w0,'$'
	b.ne	.l
	sub	x28,x28,1
	mov	x1,x24
	bl	dollar
	b	.c
.l:	cbnz	x24,.q
	strb	w0,[x20],1
	b	.c
.q:	bl	pose_cite
	b	.c
.coupe:	bl	texte_coupe
.out:	ldr	x24,[sp],16
	epilogue

; x28 apres l'operateur : jusqu'apres le « } »
acc_sauter:
	mov	x1,1
.c:	ldrb	w0,[x28]
	cbz	w0,texte_coupe
	add	x28,x28,1
	cmp	w0,'{'
	b.ne	.f
	add	x1,x1,1
	b	.c
.f:	cmp	w0,'}'
	b.ne	.c
	subs	x1,x1,1
	b.ne	.c
	ret

; x28 apres un « ` », x1 = entre guillemets
guillemet_arriere:
	prologue
	stp	x24,x25,[sp,-16]!
	mov	x24,x1
	mov	x25,x28
.c:	ldrb	w0,[x28]
	cbz	w0,.coupe
	add	x28,x28,1
	cmp	w0,'`'
	b.ne	.c
	mov	x0,x25
	sub	x1,x28,1
	mov	x2,x24
	bl	capturer
	b	.out
.coupe:	bl	texte_coupe
.out:	ldp	x24,x25,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  $(( expression )) : + - * / % ( ) et variables
; ---------------------------------------------------------------------------
sauter_blancs_arith:
.c:	ldrb	w0,[x28]
	cmp	w0,' '
	b.eq	.n
	cmp	w0,9
	b.eq	.n
	cmp	w0,10
	b.ne	.r
.n:	add	x28,x28,1
	b	.c
.r:	ret

arith:
	prologue
	stp	x24,x25,[sp,-16]!
	bl	arith_terme
	mov	x24,x0
.c:	bl	sauter_blancs_arith
	ldrb	w25,[x28]
	cmp	w25,'+'
	b.eq	.op
	cmp	w25,'-'
	b.ne	.f
.op:	add	x28,x28,1
	bl	arith_terme
	cmp	w25,'+'
	b.ne	.m
	add	x24,x24,x0
	b	.c
.m:	sub	x24,x24,x0
	b	.c
.f:	mov	x0,x24
	ldp	x24,x25,[sp],16
	epilogue

arith_terme:
	prologue
	stp	x24,x25,[sp,-16]!
	bl	arith_base
	mov	x24,x0
.c:	bl	sauter_blancs_arith
	ldrb	w25,[x28]
	cmp	w25,'*'
	b.eq	.op
	cmp	w25,'/'
	b.eq	.op
	cmp	w25,'%'
	b.ne	.f
.op:	add	x28,x28,1
	bl	arith_base
	cmp	w25,'*'
	b.ne	.d
	mul	x24,x24,x0
	b	.c
.d:	cbz	x0,.c				; (division par zero : ignoree)
	sdiv	x1,x24,x0
	cmp	w25,'/'
	b.ne	.r
	mov	x24,x1
	b	.c
.r:	msub	x24,x1,x0,x24
	b	.c
.f:	mov	x0,x24
	ldp	x24,x25,[sp],16
	epilogue

arith_base:
	prologue
	stp	x24,x25,[sp,-16]!
	bl	sauter_blancs_arith
	ldrb	w0,[x28]
	cmp	w0,'('
	b.ne	.moins
	add	x28,x28,1
	bl	arith
	mov	x24,x0
	bl	sauter_blancs_arith
	ldrb	w0,[x28]
	cmp	w0,')'
	b.ne	.v
	add	x28,x28,1
.v:	mov	x0,x24
	b	.out
.moins:	cmp	w0,'-'
	b.ne	.dol
	add	x28,x28,1
	bl	arith_base
	neg	x0,x0
	b	.out
.dol:	cmp	w0,'$'
	b.ne	.nom
	add	x28,x28,1
.nom:	mov	x24,x28
	bl	nom_passer
	cmp	x28,x24
	b.eq	.zero
	ldrb	w0,[x24]
	sub	w0,w0,'0'
	cmp	w0,9
	b.ls	.nb
	mov	x0,x24
	sub	x1,x28,x24
	bl	val_nommee
	cbz	x0,.out
	bl	dec_signe
	b	.out
.nb:	mov	x0,x24
	bl	dec_signe
	b	.out
.zero:	mov	x0,0
.out:	ldp	x24,x25,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Motifs
; ---------------------------------------------------------------------------

; x0 = motif (avec ESC), x1 = texte -> x0 = 1 s'il correspond
motif_ok:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	ldrb	w0,[x19]
	cbz	w0,.fin
	cmp	w0,'*'
	b.ne	.q
	add	x19,x19,1
.et:	mov	x0,x19
	mov	x1,x20
	bl	motif_ok
	cbnz	x0,.oui
	ldrb	w0,[x20],1
	cbnz	w0,.et
	b	.non
.q:	ldrb	w1,[x20]
	cbz	w1,.non
	cmp	w0,'?'
	b.ne	.cl
	add	x19,x19,1
	add	x20,x20,1
	b	.c
.cl:	cmp	w0,'['
	b.ne	.lit
	add	x21,x19,1			; [abc], [a-z], [!x]
	mov	x22,0				; 1 : vu
	ldrb	w2,[x21]
	mov	x3,0
	cmp	w2,'!'
	b.ne	.cl1
	mov	x3,1
	add	x21,x21,1
.cl1:	ldrb	w2,[x21],1
	cbz	w2,.lit
	cmp	w2,']'
	b.eq	.clf
	ldrb	w4,[x21]
	cmp	w4,'-'
	b.ne	.cl2
	ldrb	w4,[x21,1]
	cmp	w4,']'
	b.eq	.cl2
	add	x21,x21,2
	cmp	w1,w2
	b.lo	.cl1
	cmp	w1,w4
	b.hi	.cl1
	mov	x22,1
	b	.cl1
.cl2:	cmp	w1,w2
	b.ne	.cl1
	mov	x22,1
	b	.cl1
.clf:	cmp	x22,x3
	b.eq	.non
	mov	x19,x21
	add	x20,x20,1
	b	.c
.lit:	cmp	w0,ESC
	b.ne	.l2
	add	x19,x19,1
	ldrb	w0,[x19]
.l2:	cmp	w0,w1
	b.ne	.non
	add	x19,x19,1
	add	x20,x20,1
	b	.c
.fin:	ldrb	w0,[x20]
	cbnz	w0,.non
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (dans mot_etendre) le motif en x21, sur un seul niveau (« rep/*.c ») : les noms trouves, tries,
; sur at_p -> x0 = 1 si au moins un
motif_etendre:
	prologue
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	mov	x24,0				; dernier « / »
	mov	x0,x21
.d:	ldrb	w1,[x0],1
	cbz	w1,.df
	cmp	w1,ESC
	b.ne	.d1
	ldrb	w1,[x0],1
	cbz	w1,.df
	b	.d
.d1:	cmp	w1,'/'
	b.ne	.d
	sub	x24,x0,1
	b	.d
.df:	mov	x25,x21				; le motif du dernier composant
	adr	x26,s_dot
	cbz	x24,.ouv
	add	x25,x24,1
	strb	wzr,[x24]			; (le dossier)
	mov	x0,x21
	bl	sans_esc
	mov	x26,x21
.ouv:	mov	x0,x26
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x27,x0
	adr	x0,at_p
	ldr	x4,[x0]
	str	x4,[sp,-16]!			; (premier nom pose)
	tbnz	x27,63,.fini
.n:	mov	x0,x27
	adr	x1,dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.ferme
	adr	x0,dirent
	ldrb	w1,[x0]
	cmp	w1,'.'				; cache, sauf si le motif le demande
	b.ne	.tst
	ldrb	w1,[x25]
	cmp	w1,'.'
	b.ne	.n
.tst:	mov	x0,x25
	adr	x1,dirent
	bl	motif_ok
	cbz	x0,.n
	mov	x0,x20				; « dossier/nom »
	bl	at_pousse
	cbz	x24,.nom
	mov	x1,x26
.c1:	ldrb	w0,[x1],1
	cbz	w0,.sl
	strb	w0,[x20],1
	b	.c1
.sl:	mov	w0,'/'
	strb	w0,[x20],1
.nom:	adr	x1,dirent
.c2:	ldrb	w0,[x1],1
	strb	w0,[x20],1
	cbnz	w0,.c2
	b	.n
.ferme:	mov	x0,x27
	sys	SYS_CLOSE
.fini:	ldr	x4,[sp],16
	adr	x0,at_p
	ldr	x5,[x0]
	mov	x0,0
	cmp	x4,x5
	b.eq	.remet
	mov	x0,x4
	mov	x1,x5
	bl	trier
	mov	x0,1
	b	.out
.remet:	cbz	x24,.out			; rien : le texte du motif tel quel
	mov	w1,'/'
	strb	w1,[x24]
.out:	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	epilogue

; x0, x1 = tableau de chaines [debut, fin[ : tri par insertion
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
	mov	x4,0				; x3 > x22 ?
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
