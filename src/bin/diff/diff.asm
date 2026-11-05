; diff - compare deux fichiers texte ligne a ligne : diff [-q] fichier1 fichier2 (docs/OUTILS.md)
; D'apres diff.c d'Onyx : ce qu'il faut changer dans fichier1 pour avoir fichier2, a la maniere
; classique : « 3c3 » (ligne 3 changee), « 5a6,7 » (lignes ajoutees apres 5), « 8,9d7 » (lignes
; retirees) ; les lignes de fichier1 apres « < », celles de fichier2 apres « > ». Rien d'ecrit :
; les fichiers sont identiques. -q : dit seulement s'ils different. Code de sortie : 0 identiques,
; 1 differents, 2 erreur. Le debut et la fin communs mis a part, la plus longue sous-suite commune
; du milieu (table de 16 bits, 24 M cases au plus).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

DIFF_MAX	= 24*1024*1024			; cases de la table

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	cmp	x19,2
	b.lo	.usage
	ldr	x0,[x20,8]
	adr	x1,s_q
	bl	str_eq
	cbz	x0,.n
	adr	x0,g_quiet
	mov	x1,1
	str	x1,[x0]
	mov	x21,2
.n:	sub	x0,x19,x21
	cmp	x0,2
	b.ne	.usage
	ldr	x0,[x20,x21,lsl 3]
	bl	slurp
	cbz	x0,.err1
	adr	x2,g_ba
	stp	x0,x1,[x2]
	add	x22,x21,1
	ldr	x0,[x20,x22,lsl 3]
	bl	slurp
	cbz	x0,.err2
	adr	x2,g_bb
	stp	x0,x1,[x2]
	; identiques ?
	adr	x2,g_ba
	ldp	x3,x4,[x2]
	adr	x2,g_bb
	ldp	x5,x6,[x2]
	cmp	x4,x6
	b.ne	.diff
	mov	x7,0
.cmpb:	cmp	x7,x4
	b.hs	.same
	ldrb	w8,[x3,x7]
	ldrb	w9,[x5,x7]
	add	x7,x7,1
	cmp	w8,w9
	b.eq	.cmpb
	b	.diff
.same:	mov	x0,0
	bl	t_fin
.usage:	adr	x0,m_usage
	bl	t_err_msg
	mov	x0,2
	bl	t_fin
.err1:	ldr	x0,[x20,x21,lsl 3]
	bl	t_err_fic
	mov	x0,2
	bl	t_fin
.err2:	ldr	x0,[x20,x22,lsl 3]
	bl	t_err_fic
	mov	x0,2
	bl	t_fin
.diff:	adr	x0,g_quiet
	ldr	x0,[x0]
	cbz	x0,.lines
	adr	x0,m_files
	bl	t_dire
	ldr	x0,[x20,x21,lsl 3]
	bl	t_puts
	adr	x0,m_and
	bl	t_dire
	ldr	x0,[x20,x22,lsl 3]
	bl	t_puts
	adr	x0,m_differ
	bl	t_dire
	mov	x0,1
	bl	t_fin

.lines:	adr	x2,g_ba
	ldp	x0,x1,[x2]
	bl	lines
	adr	x3,g_nla
	str	x2,[x3]
	mov	x21,x0				; A
	mov	x23,x1				; n
	adr	x2,g_bb
	ldp	x0,x1,[x2]
	bl	lines
	adr	x3,g_nlb
	str	x2,[x3]
	mov	x22,x0				; B
	mov	x24,x1				; m
	; le debut commun
	mov	x25,0
.p:	cmp	x25,x23
	b.hs	.pd
	cmp	x25,x24
	b.hs	.pd
	ldr	x0,[x21,x25,lsl 3]
	ldr	x1,[x22,x25,lsl 3]
	bl	str_eq
	cbz	x0,.pd
	add	x25,x25,1
	b	.p
	; la fin commune
.pd:	mov	x26,0
.s:	sub	x0,x23,x25
	cmp	x26,x0
	b.hs	.sd
	sub	x0,x24,x25
	cmp	x26,x0
	b.hs	.sd
	sub	x0,x23,1
	sub	x0,x0,x26
	ldr	x0,[x21,x0,lsl 3]
	sub	x1,x24,1
	sub	x1,x1,x26
	ldr	x1,[x22,x1,lsl 3]
	bl	str_eq
	cbz	x0,.sd
	add	x26,x26,1
	b	.s
.sd:	adr	x0,g_pre
	str	x25,[x0]
	add	x21,x21,x25,lsl 3		; A' = A + pre
	add	x22,x22,x25,lsl 3
	sub	x23,x23,x25
	sub	x23,x23,x26			; N
	sub	x24,x24,x25
	sub	x24,x24,x26			; M
	add	x0,x23,1
	add	x1,x24,1
	mul	x0,x0,x1
	movl	x1,DIFF_MAX
	cmp	x0,x1
	b.hi	.trop
	lsl	x0,x0,1
	bl	malloc
	cbz	x0,.trop
	mov	x25,x0				; L
	add	x26,x24,1			; pas d'une ligne de la table
	; la table, de la fin vers le debut
	mov	x27,x23
.li:	mov	x28,x24
.lj:	cmp	x27,x23
	b.eq	.zero
	cmp	x28,x24
	b.eq	.zero
	ldr	x0,[x21,x27,lsl 3]
	ldr	x1,[x22,x28,lsl 3]
	bl	str_eq
	mul	x9,x27,x26
	add	x9,x9,x28
	cbz	x0,.max
	add	x10,x9,x26
	add	x10,x10,1
	ldrh	w11,[x25,x10,lsl 1]
	add	w11,w11,1
	b	.put
.max:	add	x10,x9,x26
	ldrh	w11,[x25,x10,lsl 1]
	add	x10,x9,1
	ldrh	w12,[x25,x10,lsl 1]
	cmp	w11,w12
	csel	w11,w11,w12,hs
	b	.put
.zero:	mul	x9,x27,x26
	add	x9,x9,x28
	mov	w11,0
.put:	strh	w11,[x25,x9,lsl 1]
	cbz	x28,.ni
	sub	x28,x28,1
	b	.lj
.ni:	cbz	x27,.walk
	sub	x27,x27,1
	b	.li

	; les morceaux
.walk:	mov	x27,0
	mov	x28,0
.w:	cmp	x27,x23
	b.lo	.w1
	cmp	x28,x24
	b.hs	.wend
.w1:	cmp	x27,x23
	b.hs	.hunk
	cmp	x28,x24
	b.hs	.hunk
	ldr	x0,[x21,x27,lsl 3]
	ldr	x1,[x22,x28,lsl 3]
	bl	str_eq
	cbz	x0,.hunk
	add	x27,x27,1
	add	x28,x28,1
	b	.w
.hunk:	adr	x0,g_i0
	stp	x27,x28,[x0]
.h:	cmp	x27,x23
	b.lo	.h1
	cmp	x28,x24
	b.hs	.hend
.h1:	cmp	x27,x23
	b.hs	.h2
	cmp	x28,x24
	b.hs	.h2
	ldr	x0,[x21,x27,lsl 3]
	ldr	x1,[x22,x28,lsl 3]
	bl	str_eq
	cbnz	x0,.hend
.h2:	cmp	x28,x24
	b.hs	.inci
	cmp	x27,x23
	b.hs	.incj
	mul	x9,x27,x26
	add	x9,x9,x28
	add	x10,x9,x26
	ldrh	w11,[x25,x10,lsl 1]
	add	x10,x9,1
	ldrh	w12,[x25,x10,lsl 1]
	cmp	w11,w12
	b.hs	.inci
.incj:	add	x28,x28,1
	b	.h
.inci:	add	x27,x27,1
	b	.h
.hend:	bl	hunk
	b	.w
.wend:	adr	x0,g_nla
	ldr	x0,[x0]
	adr	x1,g_nlb
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.fin
	adr	x1,m_nonl2
	adr	x2,m_nonl1
	cmp	x0,0
	csel	x19,x1,x2,ne
	adr	x0,s_bs
	bl	t_puts
	mov	x0,x19
	bl	t_dire
.fin:	mov	x0,1
	bl	t_fin
.trop:	adr	x0,m_trop
	bl	t_err_msg
	mov	x0,2
	bl	t_fin

; un morceau : g_i0, g_j0 au debut, x27, x28 a la fin (registres de start)
hunk:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,g_i0
	ldp	x19,x20,[x0]			; i0, j0
	adr	x0,g_pre
	ldr	x9,[x0]
	sub	x10,x27,x19			; da
	sub	x11,x28,x20			; db
	cbz	x10,.add
	cbz	x11,.del
	add	x0,x9,x19			; c
	add	x0,x0,1
	add	x1,x9,x27
	bl	range
	mov	w0,'c'
	bl	t_putc
	adr	x0,g_pre
	ldr	x9,[x0]
	add	x0,x9,x20
	add	x0,x0,1
	add	x1,x9,x28
	bl	range
	b	.nl
.del:	add	x0,x9,x19
	add	x0,x0,1
	add	x1,x9,x27
	bl	range
	mov	w0,'d'
	bl	t_putc
	adr	x0,g_pre
	ldr	x9,[x0]
	add	x0,x9,x20
	bl	t_putn
	b	.nl
.add:	add	x0,x9,x19
	bl	t_putn
	mov	w0,'a'
	bl	t_putc
	adr	x0,g_pre
	ldr	x9,[x0]
	add	x0,x9,x20
	add	x0,x0,1
	add	x1,x9,x28
	bl	range
.nl:	bl	t_nl
.la:	cmp	x19,x27
	b.hs	.sep
	adr	x0,s_lt
	bl	t_puts
	ldr	x0,[x21,x19,lsl 3]
	bl	t_puts
	bl	t_nl
	add	x19,x19,1
	b	.la
.sep:	adr	x0,g_i0
	ldr	x19,[x0]
	cmp	x19,x27				; da et db : « --- »
	b.eq	.lb
	cmp	x20,x28
	b.eq	.lb
	adr	x0,s_sep
	bl	t_puts
.lb:	cmp	x20,x28
	b.hs	.out
	adr	x0,s_gt
	bl	t_puts
	ldr	x0,[x22,x20,lsl 3]
	bl	t_puts
	bl	t_nl
	add	x20,x20,1
	b	.lb
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = premiere, x1 = derniere : « a » ou « a,b »
range:
	prologue
	stp	x0,x1,[sp,-16]!
	bl	t_putn
	ldp	x0,x1,[sp],16
	cmp	x1,x0
	b.ls	.out
	str	x1,[sp,-16]!
	mov	w0,','
	bl	t_putc
	ldr	x0,[sp],16
	bl	t_putn
.out:	epilogue

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
	ldr	x22,[sp]			; taille
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

; x0 = texte (modifie : les fins de ligne deviennent des 0), x1 = octets
;   -> x0 = tableau des lignes, x1 = nombre, x2 = 1 si le texte finit par une fin de ligne (ou est vide)
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
.cf:	mov	x24,1
	cbz	x22,.al
	sub	x2,x22,1
	ldrb	w3,[x21,x2]
	cmp	w3,10
	b.eq	.al
	mov	x24,0
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
	mov	x2,x24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

s_q		db '-q',0
s_lt		db '< ',0
s_gt		db '> ',0
s_sep		db '---',10,0
m_usage		db 'usage : diff [-q] fichier1 fichier2',0
m_files		db 'Les fichiers ',0
m_and		db ' et ',0
m_differ	db ' sont diff',0xC3,0xA9,'rents',10,0
s_bs		db '\ ',0
m_nonl1		db 'Pas de fin de ligne ',0xC3,0xA0,' la fin de fichier1',10,0
m_nonl2		db 'Pas de fin de ligne ',0xC3,0xA0,' la fin de fichier2',10,0
m_trop		db 'les fichiers diff',0xC3,0xA8,'rent trop pour ',0xC3,0xAA,'tre compar',0xC3,0xA9,'s (trop de lignes)',0
		align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
		align 8
g_quiet		dd 0
g_ba		dd 0, 0				; texte, octets
g_bb		dd 0, 0
g_nla		dd 0
g_nlb		dd 0
g_pre		dd 0
g_i0		dd 0				; (g_j0 suit)
g_j0		dd 0
tool_data
