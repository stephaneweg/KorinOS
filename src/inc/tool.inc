; tool.inc - le socle des commandes de SD0:/bin (docs/OUTILS.md), comme tool.h d'Onyx : argv,
; sortie tamponnee, lecture par lignes, chemins. Une commande :
;
;	start:	bl	t_debut			; argc dans x19, argv dans x20
;		...
;		mov	x0,code
;		bl	t_fin
;	include '../../inc/tool.inc'
;	segment readable writeable
;	kit_data
;	tool_data
;
; (t_debut garde la ligne d'arguments donnee en x0 au demarrage : a appeler en premier.)

T_OUT		= 4096
T_LIGNE		= 65536
T_LIRE		= 4096

; x0 = ligne d'arguments -> x19 = argc, x20 = argv
t_debut:
	stp	x29,x30,[sp,-16]!
	mov	x19,x0
	bl	kits_open
	bl	f_start_blob
	cbnz	x0,.blob
	mov	x0,x19
	bl	t_ligne_coupee
	b	.fin
.blob:	ldr	w19,[x0,4]
	add	x1,x0,16
	add	x0,x19,1
	lsl	x0,x0,3
	stp	x1,xzr,[sp,-16]!
	bl	malloc
	ldp	x1,xzr,[sp],16
	mov	x20,x0
	mov	x2,0
.a:	cmp	x2,x19
	b.hs	.af
	str	x1,[x20,x2,lsl 3]
.s:	ldrb	w3,[x1],1
	cbnz	w3,.s
	add	x2,x2,1
	b	.a
.af:	str	xzr,[x20,x2,lsl 3]
.fin:	ldp	x29,x30,[sp],16
	ret

; x0 = ligne (0 : aucune) -> x19 = argc, x20 = argv : coupee aux blancs, guillemets, \" et \\
t_ligne_coupee:
	prologue
	str	x21,[sp,-16]!
	mov	x21,x0
	mov	x0,1024*8
	bl	malloc
	mov	x20,x0
	adr	x0,s_t_nom
	str	x0,[x20]
	mov	x19,1
	cbz	x21,.fin
	mov	x0,x21
	bl	str_len
	add	x0,x0,1
	str	x0,[sp,-16]!
	bl	malloc
	ldr	x2,[sp],16
	mov	x1,x21
	mov	x21,x0
	bl	mem_move
.b:	ldrb	w0,[x21]
	cbz	w0,.fin
	cmp	w0,' '
	b.ne	.m
	add	x21,x21,1
	b	.b
.m:	cmp	w0,'"'
	b.eq	.q
	str	x21,[x20,x19,lsl 3]
	add	x19,x19,1
.t:	ldrb	w0,[x21]
	cbz	w0,.fin
	cmp	w0,' '
	b.eq	.z
	add	x21,x21,1
	b	.t
.z:	strb	wzr,[x21],1
	b	.b
.q:	add	x21,x21,1
	str	x21,[x20,x19,lsl 3]
	add	x19,x19,1
	mov	x1,x21
.q1:	ldrb	w0,[x21]
	cbz	w0,.qf
	add	x21,x21,1
	cmp	w0,'"'
	b.eq	.qz
	cmp	w0,'\'
	b.ne	.q2
	ldrb	w2,[x21]
	cmp	w2,'"'
	b.eq	.q3
	cmp	w2,'\'
	b.ne	.q2
.q3:	mov	w0,w2
	add	x21,x21,1
.q2:	strb	w0,[x1],1
	b	.q1
.qz:	strb	wzr,[x1]
	b	.b
.qf:	strb	wzr,[x1]
.fin:	str	xzr,[x20,x19,lsl 3]
	ldr	x21,[sp],16
	epilogue

; x0 = code : sortie videe, fin du programme
t_fin:
	stp	x0,xzr,[sp,-16]!
	bl	t_flush
	ldp	x0,xzr,[sp],16
	sys	SYS_EXIT

; --- sortie ------------------------------------------------------------------

t_flush:
	prologue
	adr	x3,t_out_n
	ldr	x2,[x3]
	cbz	x2,.out
	str	xzr,[x3]
	mov	x0,1
	adr	x1,t_out
	sys	SYS_WRITE
.out:	epilogue

; w0 = octet
t_putc:
	prologue
	adr	x1,t_out_n
	ldr	x2,[x1]
	cmp	x2,T_OUT
	b.lo	.m
	str	x0,[sp,-16]!
	bl	t_flush
	ldr	x0,[sp],16
	adr	x1,t_out_n
	ldr	x2,[x1]
.m:	adr	x3,t_out
	strb	w0,[x3,x2]
	add	x2,x2,1
	str	x2,[x1]
	epilogue

; x0 = chaine
t_dire:
	prologue
	bl	lang_tr
	bl	t_puts
	epilogue

t_puts:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
.c:	ldrb	w0,[x19],1
	cbz	w0,.out
	bl	t_putc
	b	.c
.out:	ldr	x19,[sp],16
	epilogue

; x0 = tampon, x1 = octets
t_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	cbz	x20,.out
	ldrb	w0,[x19],1
	bl	t_putc
	sub	x20,x20,1
	b	.c
.out:	ldp	x19,x20,[sp],16
	epilogue

t_nl:
	mov	w0,10
	b	t_putc

; x0 = valeur non signee
t_putn:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	bl	t_puts
	add	sp,sp,32
	epilogue

; --- erreurs : sur l'erreur standard -------------------------------------------

; x0 = chaine
t_err:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	t_flush
	mov	x0,x19
	bl	str_len
	mov	x2,x0
	mov	x1,x19
	mov	x0,2
	sys	SYS_WRITE
	ldr	x19,[sp],16
	epilogue

; x0 = texte litteral (jamais une donnee) : traduit, sur l'erreur standard
t_derr:
	prologue
	bl	lang_tr
	bl	t_err
	epilogue

; x0 = texte : « nom: texte » puis fin de ligne
t_err_msg:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,x19
	bl	lang_tr
	mov	x19,x0
	ldr	x0,[x20]
	bl	t_err
	adr	x0,s_t_sep
	bl	t_err
	mov	x0,x19
	bl	t_err
	adr	x0,s_t_nl
	bl	t_err
	ldr	x19,[sp],16
	epilogue

; x0 = chemin : « nom: chemin : inaccessible »
t_err_fic:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x20]
	bl	t_err
	adr	x0,s_t_sep
	bl	t_err
	mov	x0,x19
	bl	t_err
	adr	x0,s_t_acces
	bl	t_derr
	ldr	x19,[sp],16
	epilogue

; --- lecture par lignes ---------------------------------------------------------

; x0 = chemin, 0 ou « - » : l'entree standard -> x0 = descripteur (0), < 0 si erreur
t_ouvrir:
	cbz	x0,.in
	ldrb	w1,[x0]
	cmp	w1,'-'
	b.ne	.f
	ldrb	w1,[x0,1]
	cbnz	w1,.f
.in:	mov	x0,0
	ret
.f:	mov	x1,O_READ
	stp	x29,x30,[sp,-16]!
	sys	SYS_OPEN
	ldp	x29,x30,[sp],16
	ret

; x0 = descripteur (rien si 0)
t_fermer:
	cbz	x0,.r
	stp	x29,x30,[sp,-16]!
	sys	SYS_CLOSE
	ldp	x29,x30,[sp],16
.r:	ret

; x0 = descripteur : une nouvelle lecture
t_lire_debut:
	adr	x1,t_fd
	str	x0,[x1]
	adr	x1,t_lu_n
	str	xzr,[x1]
	adr	x1,t_lu_i
	str	xzr,[x1]
	ret

; -> w0 = octet suivant, -1 a la fin
t_getc:
	prologue
	adr	x2,t_lu_i
	ldr	x3,[x2]
	adr	x4,t_lu_n
	ldr	x5,[x4]
	cmp	x3,x5
	b.lo	.o
	adr	x0,t_fd
	ldr	x0,[x0]
	adr	x1,t_lu
	mov	x2,T_LIRE
	sys	SYS_READ
	cmp	x0,0
	b.le	.fin
	adr	x4,t_lu_n
	str	x0,[x4]
	mov	x3,0
	adr	x2,t_lu_i
.o:	adr	x1,t_lu
	ldrb	w0,[x1,x3]
	add	x3,x3,1
	str	x3,[x2]
	b	.out
.fin:	mov	x0,-1
.out:	epilogue

; -> x0 = ligne (sans fin de ligne, terminee par 0), x1 = longueur, 0 en x0 a la fin
t_lire_ligne:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	adr	x20,t_ligne
.c:	bl	t_getc
	tbnz	x0,63,.fin
	cmp	w0,10
	b.eq	.ok
	movl	x1,T_LIGNE-2
	cmp	x19,x1
	b.hs	.c
	strb	w0,[x20,x19]
	add	x19,x19,1
	b	.c
.fin:	cbnz	x19,.ok
	mov	x0,0
	b	.out
.ok:	strb	wzr,[x20,x19]
	mov	x0,x20
	mov	x1,x19
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- fichiers --------------------------------------------------------------------

; x0 = chemin -> x0 = 1 si c'est un repertoire, 0 si un fichier, -1 si absent
t_type:
	prologue
	sub	sp,sp,16
	mov	x1,sp
	sys	SYS_STAT
	tbnz	x0,63,.non
	ldr	x0,[sp,8]
	tst	x0,ATTR_DIR
	cset	x0,ne
	b	.out
.non:	mov	x0,-1
.out:	add	sp,sp,16
	epilogue

; x0 = destination (300 o au moins), x1 = dossier, x2 = nom
t_join:
	mov	x3,x0
.a:	ldrb	w4,[x1],1
	cbz	w4,.af
	strb	w4,[x3],1
	b	.a
.af:	cmp	x3,x0
	b.eq	.b
	ldurb	w4,[x3,-1]
	cmp	w4,'/'
	b.eq	.b
	cmp	w4,':'
	b.eq	.b
	mov	w4,'/'
	strb	w4,[x3],1
.b:	ldrb	w4,[x2],1
	strb	w4,[x3],1
	cbnz	w4,.b
	ret

; x0 = chemin -> x0 = son dernier composant (dans le chemin)
t_base:
	mov	x1,x0
.c:	ldrb	w2,[x1],1
	cbz	w2,.r
	cmp	w2,'/'
	b.eq	.s
	cmp	w2,':'
	b.ne	.c
.s:	ldrb	w2,[x1]
	cbz	w2,.c
	mov	x0,x1
	b	.c
.r:	ret

; x0 = message (MSG_TAILLE) : au service vfs -> resultat
t_vfs:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,t_canal
	ldr	x0,[x0]
	cbnz	x0,.a
	adr	x0,s_t_vfs
	mov	x1,5000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	adr	x1,t_canal
	str	x0,[x1]
.a:	mov	x1,x19
	sys	SYS_SVC_CALL
.out:	ldr	x19,[sp],16
	epilogue

; x0 = message, x1 = op, x2 = a, x3 = b, x4 = chemin, x5 = second chemin (0 : aucun)
t_msg:
	stp	x1,x2,[x0]
	stp	x3,xzr,[x0,16]
	str	xzr,[x0,32]
	add	x6,x0,MSG_DATA
.a:	ldrb	w7,[x4],1
	strb	w7,[x6],1
	cbnz	w7,.a
	cbz	x5,.f
.b:	ldrb	w7,[x5],1
	strb	w7,[x6],1
	cbnz	w7,.b
.f:	sub	x6,x6,x0
	sub	x6,x6,MSG_DATA
	str	w6,[x0,MSG_LEN]
	ret

; x0 = ancien, x1 = nouveau -> resultat de V_RENAME (E_XDEV : deux volumes)
t_rename:
	prologue
	sub	sp,sp,MSG_TAILLE
	mov	x4,x0
	mov	x5,x1
	mov	x0,sp
	mov	x1,V_RENAME
	mov	x2,0
	mov	x3,0
	bl	t_msg
	mov	x0,sp
	bl	t_vfs
	add	sp,sp,MSG_TAILLE
	epilogue

; x0 = chemin, x1 = secondes UTC -> resultat de V_UTIME
t_utime:
	prologue
	sub	sp,sp,MSG_TAILLE
	mov	x4,x0
	mov	x3,x1
	mov	x0,sp
	mov	x1,V_UTIME
	mov	x2,0
	mov	x5,0
	bl	t_msg
	mov	x0,sp
	bl	t_vfs
	add	sp,sp,MSG_TAILLE
	epilogue

; x0 = source, x1 = destination -> x0 = 0, ou erreur
t_copier:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x1
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x0,x21
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.ferme1
	mov	x20,x0
	adr	x21,t_lu
.c:	mov	x0,x19
	mov	x1,x21
	mov	x2,T_LIRE
	sys	SYS_READ
	cmp	x0,0
	b.le	.f
	mov	x2,x0
	mov	x0,x20
	mov	x1,x21
	sys	SYS_WRITE
	tbz	x0,63,.c
.f:	mov	x21,x0
	mov	x0,x20
	sys	SYS_CLOSE
	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,x21
	cmp	x0,0
	csel	x0,xzr,x0,ge
	b	.out
.ferme1:
	mov	x21,x0
	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,x21
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte « -abc » : -> x0 = 1 si c'est un groupe d'options
t_est_opt:
	ldrb	w1,[x0]
	cmp	w1,'-'
	b.ne	.non
	ldrb	w1,[x0,1]
	cbz	w1,.non
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

s_t_nom		db 'commande',0
s_t_sep		db ': ',0
s_t_nl		db 10,0
s_t_acces	db ' : inaccessible',10,0
s_t_vfs		db 'vfs',0
		align 4

macro tool_data {
		align 8
t_out_n		dd 0
t_canal		dd 0
t_fd		dd 0
t_lu_n		dd 0
t_lu_i		dd 0
t_out		rb T_OUT
t_lu		rb T_LIRE
t_ligne		rb T_LIGNE
}
