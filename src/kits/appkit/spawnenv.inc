; spawnenv.inc - AppKit : argv et environnement d'un fils, le protocole de libposix
; (src/lib/posix/proc.c, docs/POSIX.md) : un objet partage donne par HANDLE_INHERIT, marque
; 0x80000000 | pages ; +0 magique, +4 argc, +8 envc, +12 taille, +16 les chaines a la suite.
BLOB_TAG	= 0x80000000
BLOB_MAGIC	= 0x4E505341
BLOB_CHAINES	= 16
LIGNE_MAX	= 1024			; ARGS_MAX du noyau

; x0 = tableau de chaines termine par 0 -> x0 = nombre, x1 = octets (avec les 0)
env_compter:
	mov	x2,x0
	mov	x0,0
	mov	x1,0
	cbz	x2,.r
.v:	ldr	x3,[x2,x0,lsl 3]
	cbz	x3,.r
.c:	ldrb	w4,[x3],1
	add	x1,x1,1
	cbnz	w4,.c
	add	x0,x0,1
	b	.v
.r:	ret

; x0 = tableau, x1 = n, x2 = destination -> x0 = fin
env_copier:
	mov	x3,x0
	mov	x4,0
.v:	cmp	x4,x1
	b.hs	.f
	ldr	x5,[x3,x4,lsl 3]
.c:	ldrb	w6,[x5],1
	strb	w6,[x2],1
	cbnz	w6,.c
	add	x4,x4,1
	b	.v
.f:	mov	x0,x2
	ret

; x0 = argv, x1 = destination (LIGNE_MAX octets) -> x0 = 1, ou 0 si elle ne tient pas :
; argv[1...] en une ligne, un argument avec espaces ou guillemets entre guillemets
env_ligne:
	cbz	x0,.vide
	ldr	x2,[x0]
	cbz	x2,.vide
	add	x0,x0,8				; (pas argv[0])
	mov	x2,x1				; ecriture
	add	x3,x1,LIGNE_MAX-8		; limite
.a:	ldr	x4,[x0],8
	cbz	x4,.fin
	cmp	x2,x1
	b.eq	.pas
	mov	w5,' '
	strb	w5,[x2],1
.pas:	mov	x5,x4				; a citer ?
	ldrb	w6,[x5]
	mov	x7,0
	cbnz	w6,.t
	mov	x7,1
.t:	ldrb	w6,[x5],1
	cbz	w6,.g
	cmp	w6,' '
	b.eq	.q
	cmp	w6,9
	b.eq	.q
	cmp	w6,'"'
	b.eq	.q
	cmp	w6,'\'
	b.ne	.t
.q:	mov	x7,1
	b	.t
.g:	cbz	x7,.c
	mov	w6,'"'
	strb	w6,[x2],1
.c:	ldrb	w6,[x4],1
	cbz	w6,.cf
	cmp	x2,x3
	b.hs	.non
	cbz	x7,.p
	cmp	w6,'"'
	b.eq	.e
	cmp	w6,'\'
	b.ne	.p
.e:	mov	w5,'\'
	strb	w5,[x2],1
.p:	strb	w6,[x2],1
	b	.c
.cf:	cbz	x7,.a
	mov	w6,'"'
	strb	w6,[x2],1
	b	.a
.fin:	strb	wzr,[x2]
	mov	x0,1
	ret
.vide:	strb	wzr,[x1]
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; --- f_spawn_env(chemin, argv, envp, &fds[3]) -> pid : comme f_spawn_stdio, avec argv (argv[0]
; compris) et l'environnement, sans limite de longueur, pour un programme C ; un programme en
; assembleur lit la ligne d'arguments (vide si elle depasse 1 024 octets)
f_spawn_env:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,LIGNE_MAX+16
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x0,x20
	bl	env_compter
	mov	x23,x0				; argc
	mov	x25,x1
	mov	x0,x21
	bl	env_compter
	mov	x24,x0				; envc
	add	x25,x25,x1
	add	x25,x25,BLOB_CHAINES
	add	x25,x25,0xFFF
	and	x25,x25,not 0xFFF		; octets
	mov	x0,x25
	mov	x1,0
	mov	x8,SYS_SHM_CREATE
	svc	0
	tbnz	x0,63,.ligne
	mov	x26,x0				; poignee
	mov	x1,0
	mov	x2,x25
	mov	x3,PROT_READ or PROT_WRITE
	mov	x4,0
	mov	x5,0
	mov	x8,SYS_SHM_MAP
	svc	0
	cmn	x0,4096
	b.hs	.ferme
	mov	x4,x0				; (x4 : l'objet, jusqu'au demappage)
	movl	w1,BLOB_MAGIC
	str	w1,[x4]
	stp	w23,w24,[x4,4]
	str	w25,[x4,12]
	str	x4,[sp,LIGNE_MAX]
	mov	x0,x20
	mov	x1,x23
	add	x2,x4,BLOB_CHAINES
	bl	env_copier
	mov	x2,x0
	mov	x0,x21
	mov	x1,x24
	bl	env_copier
	ldr	x0,[sp,LIGNE_MAX]
	mov	x1,x25
	mov	x8,SYS_VM_UNMAP
	svc	0
	lsr	x1,x25,12			; { poignee, marque }
	movl	w2,BLOB_TAG
	orr	w1,w1,w2
	str	w26,[sp,LIGNE_MAX]
	str	w1,[sp,LIGNE_MAX+4]
	add	x0,sp,LIGNE_MAX
	mov	x1,1
	mov	x8,SYS_HANDLE_INHERIT
	svc	0
	b	.lance
.ferme:	mov	x0,x26
	mov	x8,SYS_HANDLE_CLOSE
	svc	0
.ligne:	mov	x26,-1				; (sans objet : la ligne seule)
.lance:	mov	x0,x20
	mov	x1,sp
	bl	env_ligne
	mov	x1,0				; (trop longue : vide)
	cbz	x0,.sp
	ldrb	w0,[sp]
	cbz	w0,.sp
	mov	x1,sp
.sp:	mov	x0,x19
	mov	x2,x22
	bl	f_spawn_stdio
	mov	x19,x0
	tbz	x26,63,.obj
	b	.out
.obj:	tbz	x19,63,.rend
	mov	x0,0				; pas de fils : l'objet est rendu
	mov	x1,0
	mov	x8,SYS_HANDLE_INHERIT
	svc	0
.rend:	mov	x0,x26
	mov	x8,SYS_HANDLE_CLOSE
	svc	0
.out:	mov	x0,x19
	add	sp,sp,LIGNE_MAX+16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- f_start_blob() -> x0 = l'objet du pere, projete (+4 argc, +8 envc, +16 chaines),
; 0 sans objet (lance par un programme qui ne donne que la ligne d'arguments)
f_start_blob:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,16*256
	mov	x0,sp
	mov	x1,256
	mov	x2,1
	mov	x8,SYS_HANDLE_LIST
	svc	0
	mov	x19,0
	cmp	x0,0
	b.le	.out
	mov	x1,sp
.e:	ldr	w2,[x1,4]
	tbnz	w2,31,.got
	add	x1,x1,16
	subs	x0,x0,1
	b.ne	.e
	b	.out
.got:	ldr	w20,[x1]			; poignee
	and	w2,w2,0x7FFFFFFF
	mov	x0,x20
	mov	x1,0
	lsl	x2,x2,12
	mov	x3,PROT_READ or PROT_WRITE	; (les chaines sont a lui : basename les coupe)
	mov	x4,0
	mov	x5,0
	mov	x8,SYS_SHM_MAP
	svc	0
	cmn	x0,4096
	b.hs	.ferme
	ldr	w1,[x0]
	movl	w2,BLOB_MAGIC
	cmp	w1,w2
	b.ne	.ferme
	mov	x19,x0
.ferme:	mov	x0,x20
	mov	x8,SYS_HANDLE_CLOSE
	svc	0
.out:	mov	x0,x19
	add	sp,sp,16*256
	ldp	x19,x20,[sp],16
	epilogue
