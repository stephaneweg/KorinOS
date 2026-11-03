; regex.inc - expressions regulieres de grep et sed (docs/OUTILS.md) : le motif est compile en une
; petite machine, executee avec retour arriere. Syntaxe de base (\( \) \| \+ \?) ou etendue
; (RE_E : ( ) | + ?) ; . [abc] [^a-z] * ^ $ \1 absent ; \t, \n ; RE_I : sans la casse.
;
;	re_compile (motif, drapeaux, programme) ; re_cherche (ligne, depart) -> 1 : re_cap[0..19]

RE_E		= 1
RE_I		= 2
RE_INSTR	= 16			; +0 op, +4 a, +8 b (ou adresse de l'ensemble)
RE_MAX		= 1024			; instructions d'un programme
RE_TAILLE	= RE_MAX * RE_INSTR + 32 * 64	; programme + 64 ensembles
RE_CAPS		= 20

OP_FIN		= 0
OP_CAR		= 1			; a : caractere
OP_TOUT		= 2
OP_ENS		= 3			; b : ensemble de 32 o
OP_DEB		= 4
OP_EOL		= 5
OP_SAUT		= 6			; a : ecart relatif
OP_CHOIX	= 7			; a, b : ecarts relatifs
OP_CAPT		= 8			; a : numero de la capture

; x0 = motif, x1 = drapeaux, x2 = programme (RE_TAILLE octets)
re_compile:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0				; curseur
	adr	x0,re_drap
	str	x1,[x0]
	adr	x0,re_prog
	str	x2,[x0]
	adr	x0,re_pc
	str	x2,[x0]
	add	x2,x2,RE_MAX*RE_INSTR
	adr	x0,re_ens
	str	x2,[x0]
	adr	x0,re_ngr
	mov	x1,1
	str	x1,[x0]
	mov	x0,OP_CAPT
	mov	x1,0
	bl	re_emet
	bl	re_alternatives
	mov	x0,OP_CAPT
	mov	x1,1
	bl	re_emet
	mov	x0,OP_FIN
	mov	x1,0
	bl	re_emet
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = op, x1 = a -> x0 = l'instruction
re_emet:
	adr	x2,re_pc
	ldr	x3,[x2]
	add	x4,x3,RE_INSTR
	str	x4,[x2]
	str	w0,[x3]
	str	w1,[x3,4]
	mov	x0,x3
	ret

; x0 = debut, une instruction libre inseree en x0 (le reste est relatif, il se deplace d'un bloc)
re_insere:
	adr	x2,re_pc
	ldr	x3,[x2]
	add	x4,x3,RE_INSTR
	str	x4,[x2]
.c:	cmp	x3,x0
	b.ls	.r
	sub	x3,x3,RE_INSTR
	ldp	x5,x6,[x3]
	stp	x5,x6,[x3,RE_INSTR]
	b	.c
.r:	ret

; a|b : jusqu'a la fin, ou la parenthese fermante
re_alternatives:
	prologue
	stp	x21,x22,[sp,-16]!
	adr	x21,re_pc
	ldr	x21,[x21]			; debut
	mov	x22,0				; sauts a corriger (chaine dans leur a)
.b:	bl	re_branche
	bl	re_est_ou
	cbz	x0,.f
	mov	x0,x21				; CHOIX devant la branche
	bl	re_insere
	mov	x0,OP_SAUT			; apres la branche : vers la fin
	mov	x1,0
	bl	re_emet
	str	x22,[x0,8]
	mov	x22,x0
	mov	w1,OP_CHOIX
	str	w1,[x21]
	mov	w1,1
	str	w1,[x21,4]
	adr	x1,re_pc
	ldr	x1,[x1]
	sub	x1,x1,x21
	asr	x1,x1,4
	str	w1,[x21,8]
	adr	x21,re_pc
	ldr	x21,[x21]
	b	.b
.f:	adr	x0,re_pc			; les sauts vers la fin
	ldr	x0,[x0]
.j:	cbz	x22,.out
	ldr	x1,[x22,8]
	sub	x2,x0,x22
	asr	x2,x2,4
	str	w2,[x22,4]
	mov	x22,x1
	b	.j
.out:	ldp	x21,x22,[sp],16
	epilogue

; -> x0 = 1 et le | passe
re_est_ou:
	adr	x0,re_drap
	ldr	x0,[x0]
	ldrb	w1,[x19]
	tbz	x0,0,.base
	cmp	w1,'|'
	b.ne	.non
	add	x19,x19,1
	mov	x0,1
	ret
.base:	cmp	w1,'\'
	b.ne	.non
	ldrb	w1,[x19,1]
	cmp	w1,'|'
	b.ne	.non
	add	x19,x19,2
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; -> x0 = 1 si x19 est sur | ) ou la fin
re_fin_branche:
	ldrb	w1,[x19]
	mov	x0,1
	cbz	w1,.r
	adr	x2,re_drap
	ldr	x2,[x2]
	tbz	x2,0,.base
	cmp	w1,'|'
	b.eq	.r
	cmp	w1,')'
	b.eq	.r
	mov	x0,0
.r:	ret
.base:	cmp	w1,'\'
	b.ne	.non
	ldrb	w1,[x19,1]
	cmp	w1,'|'
	b.eq	.r
	cmp	w1,')'
	b.eq	.r
.non:	mov	x0,0
	ret

; une suite d'atomes quantifies
re_branche:
	prologue
	stp	x23,x24,[sp,-16]!
	mov	x24,1				; 1 : debut de branche
.a:	bl	re_fin_branche
	cbnz	x0,.out
	adr	x23,re_pc
	ldr	x23,[x23]			; debut de l'atome
	bl	re_atome
	mov	x24,0
.q:	adr	x0,re_drap
	ldr	x0,[x0]
	ldrb	w1,[x19]
	cmp	w1,'*'
	b.eq	.etoile
	tbz	x0,0,.qb
	cmp	w1,'+'
	b.eq	.plus
	cmp	w1,'?'
	b.eq	.point
	b	.a
.qb:	cmp	w1,'\'
	b.ne	.a
	ldrb	w1,[x19,1]
	cmp	w1,'+'
	b.ne	.qb2
	add	x19,x19,1
	b	.plus
.qb2:	cmp	w1,'?'
	b.ne	.a
	add	x19,x19,1
	b	.point
.etoile:				; CHOIX atome SAUT-arriere
	add	x19,x19,1
	mov	x0,x23
	bl	re_insere
	mov	x0,OP_SAUT
	sub	x1,x23,0
	adr	x2,re_pc
	ldr	x2,[x2]
	sub	x1,x23,x2
	asr	x1,x1,4
	bl	re_emet
	mov	w1,OP_CHOIX
	str	w1,[x23]
	mov	w1,1
	str	w1,[x23,4]
	adr	x1,re_pc
	ldr	x1,[x1]
	sub	x1,x1,x23
	asr	x1,x1,4
	str	w1,[x23,8]
	b	.q
.plus:	add	x19,x19,1			; atome CHOIX-arriere
	mov	x0,OP_CHOIX
	adr	x2,re_pc
	ldr	x2,[x2]
	sub	x1,x23,x2
	asr	x1,x1,4
	bl	re_emet
	mov	w1,1
	str	w1,[x0,8]
	b	.q
.point:	add	x19,x19,1			; CHOIX atome
	mov	x0,x23
	bl	re_insere
	mov	w1,OP_CHOIX
	str	w1,[x23]
	mov	w1,1
	str	w1,[x23,4]
	adr	x1,re_pc
	ldr	x1,[x1]
	sub	x1,x1,x23
	asr	x1,x1,4
	str	w1,[x23,8]
	b	.q
.out:	ldp	x23,x24,[sp],16
	epilogue

; un atome (x24 = 1 : debut de branche)
re_atome:
	prologue
	stp	x21,x22,[sp,-16]!
	adr	x0,re_drap
	ldr	x21,[x0]
	ldrb	w0,[x19],1
	cmp	w0,'^'
	b.ne	.dol
	cbnz	x24,.deb
	tbz	x21,0,.lit
.deb:	mov	x0,OP_DEB
	mov	x1,0
	bl	re_emet
	b	.out
.dol:	cmp	w0,'$'
	b.ne	.pt
	stp	x0,xzr,[sp,-16]!
	bl	re_fin_branche
	mov	x1,x0
	ldp	x0,xzr,[sp],16
	cbnz	x1,.eol
	tbz	x21,0,.lit
.eol:	mov	x0,OP_EOL
	mov	x1,0
	bl	re_emet
	b	.out
.pt:	cmp	w0,'.'
	b.ne	.cl
	mov	x0,OP_TOUT
	mov	x1,0
	bl	re_emet
	b	.out
.cl:	cmp	w0,'['
	b.ne	.pa
	bl	re_ensemble
	b	.out
.pa:	cmp	w0,'('
	b.ne	.bs
	tbz	x21,0,.lit
	bl	re_groupe
	b	.out
.bs:	cmp	w0,'\'
	b.ne	.lit
	ldrb	w0,[x19],1
	cbz	w0,.fin0
	cmp	w0,'('
	b.ne	.bs1
	tbnz	x21,0,.lit
	bl	re_groupe
	b	.out
.bs1:	cmp	w0,'t'
	b.ne	.bs2
	mov	w0,9
	b	.lit
.bs2:	cmp	w0,'n'
	b.ne	.lit
	mov	w0,10
	b	.lit
.fin0:	sub	x19,x19,1
	mov	w0,'\'
.lit:	mov	x1,x0
	tbz	x21,1,.pas
	sub	w2,w1,'A'
	cmp	w2,25
	b.hi	.pas
	add	w1,w1,32
.pas:	mov	x0,OP_CAR
	bl	re_emet
.out:	ldp	x21,x22,[sp],16
	epilogue

; apres la parenthese ouvrante
re_groupe:
	prologue
	str	x22,[sp,-16]!
	adr	x0,re_ngr
	ldr	x22,[x0]
	add	x1,x22,1
	str	x1,[x0]
	mov	x0,OP_CAPT
	lsl	x1,x22,1
	bl	re_emet
	bl	re_alternatives
	ldrb	w1,[x19]			; la fermante
	adr	x0,re_drap
	ldr	x0,[x0]
	tbz	x0,0,.b
	cmp	w1,')'
	b.ne	.f
	add	x19,x19,1
	b	.f
.b:	cmp	w1,'\'
	b.ne	.f
	add	x19,x19,2
.f:	mov	x0,OP_CAPT
	lsl	x1,x22,1
	add	x1,x1,1
	bl	re_emet
	ldr	x22,[sp],16
	epilogue

; apres le crochet ouvrant
re_ensemble:
	prologue
	stp	x23,x24,[sp,-16]!
	adr	x0,re_ens
	ldr	x23,[x0]
	add	x1,x23,32
	str	x1,[x0]
	stp	xzr,xzr,[x23]
	stp	xzr,xzr,[x23,16]
	mov	x24,0				; 1 : ^
	ldrb	w0,[x19]
	cmp	w0,'^'
	b.ne	.prem
	mov	x24,1
	add	x19,x19,1
.prem:	ldrb	w0,[x19]			; ] en tete : un caractere
	cmp	w0,']'
	b.ne	.c
	add	x19,x19,1
	mov	w1,w0
	bl	re_ens_pose
.c:	ldrb	w0,[x19]
	cbz	w0,.fin
	add	x19,x19,1
	cmp	w0,']'
	b.eq	.fin
	ldrb	w2,[x19]
	cmp	w2,'-'
	b.ne	.un
	ldrb	w2,[x19,1]
	cbz	w2,.un
	cmp	w2,']'
	b.eq	.un
	add	x19,x19,2
	mov	w1,w0
.pl:	bl	re_ens_pose
	add	w1,w1,1
	cmp	w1,w2
	b.ls	.pl
	b	.c
.un:	mov	w1,w0
	bl	re_ens_pose
	b	.c
.fin:	cbz	x24,.emet
	ldp	x0,x1,[x23]			; inverse
	mvn	x0,x0
	mvn	x1,x1
	stp	x0,x1,[x23]
	ldp	x0,x1,[x23,16]
	mvn	x0,x0
	mvn	x1,x1
	stp	x0,x1,[x23,16]
.emet:	mov	x0,OP_ENS
	mov	x1,0
	bl	re_emet
	str	x23,[x0,8]
	ldp	x23,x24,[sp],16
	epilogue

; w1 = caractere : dans l'ensemble x23 (ne touche que x3 a x6)
re_ens_pose:
	and	w3,w1,31
	lsr	w4,w1,5
	mov	w5,1
	lsl	w5,w5,w3
	ldr	w6,[x23,x4,lsl 2]
	orr	w6,w6,w5
	str	w6,[x23,x4,lsl 2]
	adr	x3,re_drap
	ldr	x3,[x3]
	tbz	x3,1,.r
	sub	w3,w1,'a'			; RE_I : les deux casses
	cmp	w3,25
	b.hi	.m
	sub	w3,w1,32
	b	.p
.m:	sub	w3,w1,'A'
	cmp	w3,25
	b.hi	.r
	add	w3,w1,32
.p:	and	w5,w3,31
	lsr	w4,w3,5
	mov	w6,1
	lsl	w6,w6,w5
	ldr	w5,[x23,x4,lsl 2]
	orr	w5,w5,w6
	str	w5,[x23,x4,lsl 2]
.r:	ret

; x0 = instruction, x1 = texte -> x0 = 1 si la suite correspond
re_exec:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	ldr	w0,[x19]
	cmp	w0,OP_CAR
	b.ne	.t
	ldrb	w1,[x20]
	cbz	w1,.non
	adr	x2,re_drap
	ldr	x2,[x2]
	tbz	x2,1,.c1
	sub	w2,w1,'A'
	cmp	w2,25
	b.hi	.c1
	add	w1,w1,32
.c1:	ldr	w2,[x19,4]
	cmp	w1,w2
	b.ne	.non
	add	x20,x20,1
	add	x19,x19,RE_INSTR
	b	.c
.t:	cmp	w0,OP_TOUT
	b.ne	.e
	ldrb	w1,[x20]
	cbz	w1,.non
	add	x20,x20,1
	add	x19,x19,RE_INSTR
	b	.c
.e:	cmp	w0,OP_ENS
	b.ne	.d
	ldrb	w1,[x20]
	cbz	w1,.non
	ldr	x2,[x19,8]
	lsr	w3,w1,5
	ldr	w2,[x2,x3,lsl 2]
	and	w3,w1,31
	lsr	w2,w2,w3
	tbz	w2,0,.non
	add	x20,x20,1
	add	x19,x19,RE_INSTR
	b	.c
.d:	cmp	w0,OP_DEB
	b.ne	.f
	adr	x1,re_ligne
	ldr	x1,[x1]
	cmp	x20,x1
	b.ne	.non
	add	x19,x19,RE_INSTR
	b	.c
.f:	cmp	w0,OP_EOL
	b.ne	.s
	ldrb	w1,[x20]
	cbnz	w1,.non
	add	x19,x19,RE_INSTR
	b	.c
.s:	cmp	w0,OP_SAUT
	b.ne	.ch
	ldrsw	x1,[x19,4]
	add	x19,x19,x1,lsl 4
	b	.c
.ch:	cmp	w0,OP_CHOIX
	b.ne	.ca
	ldrsw	x1,[x19,4]
	add	x0,x19,x1,lsl 4
	mov	x1,x20
	bl	re_exec
	cbnz	x0,.oui
	ldrsw	x1,[x19,8]
	add	x19,x19,x1,lsl 4
	b	.c
.ca:	cmp	w0,OP_CAPT
	b.ne	.oui				; OP_FIN
	ldr	w1,[x19,4]
	cmp	w1,RE_CAPS
	b.hs	.cs
	adr	x2,re_cap
	add	x2,x2,x1,lsl 3
	ldr	x3,[x2]
	str	x3,[sp,-16]!
	str	x2,[sp,8]
	str	x20,[x2]
	add	x0,x19,RE_INSTR
	mov	x1,x20
	bl	re_exec
	ldr	x2,[sp,8]
	ldr	x3,[sp],16
	cbnz	x0,.oui
	str	x3,[x2]
	b	.non
.cs:	add	x19,x19,RE_INSTR
	b	.c
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = programme (x0 = 0 : le dernier compile), x1 = ligne, x2 = depart -> x0 = 1 si trouve,
; re_cap[0] et [1] : le debut et la fin ; [2n], [2n+1] : le groupe n
re_cherche:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	adr	x0,re_ligne
	str	x1,[x0]
.c:	adr	x0,re_cap			; captures effacees
	mov	x1,RE_CAPS
.z:	str	xzr,[x0],8
	subs	x1,x1,1
	b.ne	.z
	mov	x0,x19
	mov	x1,x20
	bl	re_exec
	cbnz	x0,.out
	ldrb	w0,[x20],1
	cbnz	w0,.c
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

macro regex_data {
		align 8
re_drap		dd 0
re_prog		dd 0
re_pc		dd 0
re_ens		dd 0
re_ngr		dd 0
re_ligne	dd 0
re_cap		rb RE_CAPS * 8
}
