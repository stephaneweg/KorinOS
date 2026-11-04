; pids.inc - vfs : une fiche par process connu (dossier courant, entrees /
; sorties standard), par blocs de PR_PER fiches ajoutes au besoin et jamais
; deplaces : une fiche garde son adresse, et le nombre de process n'a pas de
; plafond (les pids du noyau ne sont plus des indices : proc.asm). Fiche creee
; a la naissance (ou au premier besoin), rendue a l'avis de fin.

PR_PID		= 0			; 0 : fiche libre
PR_STDIO	= 8			; trois poignees (pipes.inc)
PR_DIR		= 32			; dossier courant interne (vide : « /sd0 »)
PR_SIZE		= 32 + PATH_MAX
PR_PER		= 32			; fiches par bloc
PB_NEXT		= 0			; bloc suivant
PB_RECS		= 16
PB_SIZE		= PB_RECS + PR_PER * PR_SIZE

; x0 = pid -> x0 = sa fiche (0 : aucune) ; ne touche que x0
pid_find:
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	mov	x4,x0
	mov	x0,0
	cbz	x4,.out
	adr	x1,pr_blocks
	ldr	x1,[x1]
.b:	cbz	x1,.out
	add	x2,x1,PB_RECS
	mov	x3,PR_PER
.r:	ldr	x0,[x2,PR_PID]
	cmp	x0,x4
	b.eq	.got
	add	x2,x2,PR_SIZE
	subs	x3,x3,1
	b.ne	.r
	ldr	x1,[x1,PB_NEXT]
	b	.b
.got:	mov	x0,x2
	b	.ret
.out:	mov	x0,0
.ret:	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	ret

; x0 = pid -> x0 = sa fiche, creee vide s'il n'en a pas (0 : pid 0, ou plus
; de memoire) ; ne touche que x0
pid_get:
	stp	x29,x30,[sp,-16]!
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	stp	x5,x8,[sp,-16]!
	mov	x4,x0
	bl	pid_find
	cbnz	x0,.ret
	cbz	x4,.ret
	adr	x1,pr_blocks			; une fiche libre
	ldr	x1,[x1]
.b:	cbz	x1,.neuf
	add	x2,x1,PB_RECS
	mov	x3,PR_PER
.r:	ldr	x0,[x2,PR_PID]
	cbz	x0,.prendre
	add	x2,x2,PR_SIZE
	subs	x3,x3,1
	b.ne	.r
	ldr	x1,[x1,PB_NEXT]
	b	.b
.neuf:	stp	x6,x7,[sp,-16]!			; un bloc de plus, en tete (malloc
	stp	x9,x10,[sp,-16]!		;  d'AppKit : rien d'autre ne change)
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	stp	x15,x16,[sp,-16]!
	stp	x17,x18,[sp,-16]!
	movl	x0,PB_SIZE
	bl	malloc
	ldp	x17,x18,[sp],16
	ldp	x15,x16,[sp],16
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	ldp	x6,x7,[sp],16
	cbz	x0,.rien
	mov	x1,x0
	movl	x2,PB_SIZE
.z:	strb	wzr,[x1],1
	subs	x2,x2,1
	b.ne	.z
	adr	x1,pr_blocks
	ldr	x2,[x1]
	str	x2,[x0,PB_NEXT]
	str	x0,[x1]
	add	x2,x0,PB_RECS
.prendre:
	mov	x0,x2				; (fiche a zero : sans dossier,
	mov	x1,0				;  sans entrees / sorties)
.c:	str	xzr,[x0,x1]
	add	x1,x1,8
	cmp	x1,PR_SIZE
	b.lo	.c
	str	x4,[x0,PR_PID]
	b	.ret
.rien:	mov	x0,0
.ret:	ldp	x5,x8,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	ldp	x29,x30,[sp],16
	ret

; x0 = pid termine : sa fiche est rendue ; ne touche que x0
pid_forget:
	str	x30,[sp,-16]!
	bl	pid_find
	cbz	x0,.out
	str	xzr,[x0,PR_PID]
.out:	ldr	x30,[sp],16
	ret

; x0 = poignee : retiree des entrees / sorties standard de tous les process
; (pipes.inc) ; ne touche que x0-x3
pid_stdio_drop:
	adr	x1,pr_blocks
	ldr	x1,[x1]
.b:	cbz	x1,.out
	add	x2,x1,PB_RECS
	mov	x3,PR_PER
.r:	str	x4,[sp,-16]!
	ldr	x4,[x2,PR_STDIO]
	cmp	x4,x0
	b.ne	.s1
	str	xzr,[x2,PR_STDIO]
.s1:	ldr	x4,[x2,PR_STDIO+8]
	cmp	x4,x0
	b.ne	.s2
	str	xzr,[x2,PR_STDIO+8]
.s2:	ldr	x4,[x2,PR_STDIO+16]
	cmp	x4,x0
	b.ne	.s3
	str	xzr,[x2,PR_STDIO+16]
.s3:	ldr	x4,[sp],16
	add	x2,x2,PR_SIZE
	subs	x3,x3,1
	b.ne	.r
	ldr	x1,[x1,PB_NEXT]
	b	.b
.out:	ret
