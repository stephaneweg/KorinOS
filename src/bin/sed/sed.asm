; sed - edite un flux : sed [-n] [-E] [-e script] script [fichiers] ; adresses, s d p q (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; 1 : -n
	mov	x23,0				; drapeaux des motifs
	mov	x24,0				; scripts lus
	movl	x0,CMD_MAX*CMD_TAILLE
	bl	malloc
	adr	x1,cmds
	str	x0,[x1]
.o:	cmp	x21,x19
	b.hs	.fo
	ldr	x25,[x20,x21,lsl 3]
	mov	x0,x25
	bl	t_est_opt
	cbz	x0,.fo
	add	x21,x21,1
.l:	ldrb	w0,[x25,1]!
	cbz	w0,.o
	cmp	w0,'n'
	b.ne	.l2
	mov	x22,1
	b	.l
.l2:	cmp	w0,'E'
	b.eq	.ere
	cmp	w0,'r'
	b.ne	.l3
.ere:	orr	x23,x23,RE_E
	b	.l
.l3:	cmp	w0,'e'
	b.ne	.l
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	cbz	x0,.us
	bl	script
	mov	x24,1
	b	.o
.fo:	cbnz	x24,.go
	cmp	x21,x19
	b.hs	.us
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	bl	script
.go:	cmp	x21,x19
	b.lo	.a
	mov	x0,0
	bl	flux
	b	.fin
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x25,[x20,x21,lsl 3]
	add	x21,x21,1
	mov	x0,x25
	bl	flux
	b	.a
.fin:	mov	x0,0
	bl	t_fin
.us:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,1
	bl	t_fin

; --- le script -------------------------------------------------------------
CMD_MAX		= 256
CMD_TAILLE	= 72
C_OP		= 0			; lettre
C_A1		= 8			; 1re adresse : 0 aucune, > 0 numero, -1 : $, -2 : motif
C_A1R		= 16
C_A2		= 24
C_A2R		= 32
C_DANS		= 40			; 1 : entre les deux adresses
C_RE		= 48			; s : motif
C_REMP		= 56			; s : remplacement
C_DRAP		= 64			; s : 1 g, 2 p

; x0 = script
script:
	prologue
	stp	x25,x26,[sp,-16]!
	mov	x25,x0
.c:	ldrb	w0,[x25]
	cbz	w0,.out
	cmp	w0,' '
	b.eq	.n
	cmp	w0,10
	b.eq	.n
	cmp	w0,';'
	b.eq	.n
	cmp	w0,9
	b.eq	.n
	adr	x0,cmds_n
	ldr	x1,[x0]
	cmp	x1,CMD_MAX
	b.hs	.out
	add	x2,x1,1
	str	x2,[x0]
	mov	x2,CMD_TAILLE
	mul	x1,x1,x2
	adr	x0,cmds
	ldr	x0,[x0]
	add	x26,x0,x1
	stp	xzr,xzr,[x26,C_A1]
	stp	xzr,xzr,[x26,C_A2]
	stp	xzr,xzr,[x26,C_DANS]
	str	xzr,[x26,C_REMP]
	str	xzr,[x26,C_DRAP]
	add	x1,x26,C_A1
	bl	adresse
	ldrb	w0,[x25]
	cmp	w0,','
	b.ne	.cmd
	add	x25,x25,1
	add	x1,x26,C_A2
	bl	adresse
.cmd:	ldrb	w0,[x25]
	cmp	w0,' '
	b.ne	.cm
	add	x25,x25,1
	b	.cmd
.cm:	add	x25,x25,1
	str	x0,[x26,C_OP]
	cmp	w0,'s'
	b.ne	.c
	bl	substitution
	b	.c
.n:	add	x25,x25,1
	b	.c
.out:	ldp	x25,x26,[sp],16
	epilogue

; x1 = adresse a remplir, x25 = texte
adresse:
	prologue
	str	x26,[sp,-16]!
	mov	x26,x1
	ldrb	w0,[x25]
	cmp	w0,'$'
	b.ne	.nb
	add	x25,x25,1
	mov	x0,-1
	str	x0,[x26]
	b	.out
.nb:	sub	w1,w0,'0'
	cmp	w1,9
	b.hi	.re
	mov	x1,0
.c:	ldrb	w0,[x25]
	sub	w0,w0,'0'
	cmp	w0,9
	b.hi	.f
	mov	x2,10
	mul	x1,x1,x2
	add	x1,x1,x0
	add	x25,x25,1
	b	.c
.f:	str	x1,[x26]
	b	.out
.re:	cmp	w0,'/'
	b.ne	.out
	add	x25,x25,1
	mov	w0,'/'
	bl	jusqua
	bl	compile
	str	x0,[x26,8]
	mov	x0,-2
	str	x0,[x26]
.out:	ldr	x26,[sp],16
	epilogue

; w0 = delimiteur, x25 = texte -> x0 = copie jusqu'au delimiteur, x25 apres lui
jusqua:
	prologue
	stp	x27,x28,[sp,-16]!
	mov	x27,x0
	mov	x0,x25
	bl	str_len
	add	x0,x0,2
	bl	malloc
	mov	x28,x0
	mov	x1,x0
.c:	ldrb	w0,[x25]
	cbz	w0,.f
	add	x25,x25,1
	cmp	w0,w27
	b.eq	.f
	cmp	w0,'\'
	b.ne	.p
	ldrb	w2,[x25]
	cmp	w2,w27				; \/ : le delimiteur lui-meme
	b.ne	.p
	mov	w0,w2
	add	x25,x25,1
.p:	strb	w0,[x1],1
	b	.c
.f:	strb	wzr,[x1]
	mov	x0,x28
	ldp	x27,x28,[sp],16
	epilogue

; x0 = motif -> x0 = programme
compile:
	prologue
	str	x0,[sp,-16]!
	movl	x0,RE_TAILLE
	bl	malloc
	mov	x2,x0
	ldr	x0,[sp]
	str	x2,[sp]
	mov	x1,x23
	bl	re_compile
	ldr	x0,[sp],16
	epilogue

; x25 sur le delimiteur de s
substitution:
	prologue
	ldrb	w0,[x25],1
	stp	x0,xzr,[sp,-16]!
	bl	jusqua
	bl	compile
	str	x0,[x26,C_RE]
	ldp	x0,xzr,[sp],16
	bl	jusqua
	str	x0,[x26,C_REMP]
.f:	ldrb	w0,[x25]
	cmp	w0,'g'
	b.ne	.p
	ldr	x1,[x26,C_DRAP]
	orr	x1,x1,1
	str	x1,[x26,C_DRAP]
	add	x25,x25,1
	b	.f
.p:	cmp	w0,'p'
	b.ne	.out
	ldr	x1,[x26,C_DRAP]
	orr	x1,x1,2
	str	x1,[x26,C_DRAP]
	add	x25,x25,1
	b	.f
.out:	epilogue

; --- l'execution -----------------------------------------------------------

; x0 = chemin
flux:
	prologue
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x25,x0
	bl	t_ouvrir
	tbz	x0,63,.ok
	mov	x0,x25
	bl	t_err_fic
	b	.out
.ok:	bl	t_lire_debut
	adr	x0,num
	str	xzr,[x0]
	bl	t_lire_ligne			; (une ligne d'avance : pour $)
	cbz	x0,.ferme
.l:	adr	x1,espace			; l'espace de travail
	mov	x2,x0
.cp:	ldrb	w3,[x2],1
	strb	w3,[x1],1
	cbnz	w3,.cp
	adr	x0,num
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	bl	t_lire_ligne
	mov	x28,x0				; ligne suivante (0 : c'etait la derniere)
	bl	cycle
	adr	x1,sortir
	ldr	x1,[x1]
	cbnz	x1,.ferme
	cbz	x0,.pasim
	cbnz	x22,.pasim
	adr	x0,espace
	bl	t_puts
	bl	t_nl
.pasim:	mov	x0,x28
	cbnz	x0,.l
.ferme:	adr	x0,t_fd
	ldr	x0,[x0]
	bl	t_fermer
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	epilogue

; les commandes sur l'espace de travail -> x0 = 1 : ligne a ecrire
cycle:
	prologue
	stp	x25,x26,[sp,-16]!
	adr	x25,cmds
	ldr	x25,[x25]
	adr	x26,cmds_n
	ldr	x26,[x26]
.c:	cbz	x26,.oui
	mov	x0,x25
	bl	selection
	cbz	x0,.n
	ldr	x0,[x25,C_OP]
	cmp	w0,'d'
	b.eq	.non
	cmp	w0,'p'
	b.ne	.q
	adr	x0,espace
	bl	t_puts
	bl	t_nl
	b	.n
.q:	cmp	w0,'q'
	b.ne	.s
	cbnz	x22,.q2
	adr	x0,espace
	bl	t_puts
	bl	t_nl
.q2:	adr	x0,sortir
	mov	x1,1
	str	x1,[x0]
	b	.non
.s:	cmp	w0,'s'
	b.ne	.n
	mov	x0,x25
	bl	substituer
.n:	add	x25,x25,CMD_TAILLE
	sub	x26,x26,1
	b	.c
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	epilogue

; x0 = commande -> x0 = 1 si elle s'applique a cette ligne
selection:
	prologue
	stp	x25,x26,[sp,-16]!
	mov	x25,x0
	ldr	x0,[x25,C_A1]
	cbz	x0,.oui
	ldr	x0,[x25,C_A2]
	cbnz	x0,.deux
	add	x0,x25,C_A1
	bl	adr_ok
	b	.out
.deux:	ldr	x0,[x25,C_DANS]
	cbnz	x0,.dans
	add	x0,x25,C_A1
	bl	adr_ok
	cbz	x0,.out
	mov	x0,1
	str	x0,[x25,C_DANS]
	b	.out
.dans:	add	x0,x25,C_A2
	bl	adr_ok
	cbz	x0,.oui
	str	xzr,[x25,C_DANS]
.oui:	mov	x0,1
.out:	ldp	x25,x26,[sp],16
	epilogue

; x0 = adresse -> x0 = 1 si elle designe cette ligne
adr_ok:
	prologue
	ldr	x1,[x0]
	cmn	x1,1
	b.ne	.nb
	cmp	x28,0				; $ : pas de ligne suivante
	cset	x0,eq
	b	.out
.nb:	cmn	x1,2
	b.eq	.re
	adr	x2,num
	ldr	x2,[x2]
	cmp	x1,x2
	cset	x0,eq
	b	.out
.re:	ldr	x0,[x0,8]
	adr	x1,espace
	mov	x2,x1
	bl	re_cherche
.out:	epilogue

; x0 = commande s : l'espace de travail refait
substituer:
	prologue
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x25,x0
	adr	x26,espace			; lecture
	adr	x27,refait			; ecriture
	mov	x28,0				; 1 : au moins un remplacement
.c:	ldr	x0,[x25,C_RE]
	adr	x1,espace
	mov	x2,x26
	bl	re_cherche
	cbz	x0,.fin
	mov	x28,1
	adr	x1,re_cap
	ldp	x2,x3,[x1]			; debut, fin
.av:	cmp	x26,x2
	b.hs	.rp
	ldrb	w0,[x26],1
	strb	w0,[x27],1
	b	.av
.rp:	ldr	x4,[x25,C_REMP]
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
	cmp	x2,x3				; motif vide : avance d'un caractere
	b.ne	.g
	ldrb	w0,[x26]
	cbz	w0,.fin
	strb	w0,[x27],1
	add	x26,x26,1
.g:	ldr	x0,[x25,C_DRAP]
	tbnz	x0,0,.c
.fin:	ldrb	w0,[x26],1			; le reste
	strb	w0,[x27],1
	cbnz	w0,.fin
	cbz	x28,.out
	adr	x0,espace
	adr	x1,refait
.cp:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.cp
	ldr	x0,[x25,C_DRAP]
	tbz	x0,1,.out
	adr	x0,espace
	bl	t_puts
	bl	t_nl
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	epilogue

; x5, x6 = morceau : copie en x27
groupe:
.c:	cmp	x5,x6
	b.hs	.r
	ldrb	w7,[x5],1
	strb	w7,[x27],1
	b	.c
.r:	ret
include '../../inc/regex.inc'

include '../../inc/tool.inc'
s_usage	db 'usage : sed [-nE] [-e script] script [fichiers]',0
	align 4

segment readable writeable
kit_data
tool_data
regex_data
	align 8
cmds	dd 0
cmds_n	dd 0
num	dd 0
sortir	dd 0
espace	rb T_LIGNE
refait	rb 2*T_LIGNE
