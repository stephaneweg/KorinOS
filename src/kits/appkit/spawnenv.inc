; spawnenv.inc - AppKit : argv et environnement d'un fils, le protocole de libposix
; (src/lib/posix/proc.c, docs/POSIX.md) : un objet partage donne par HANDLE_INHERIT, marque
; 0x80000000 | pages ; +0 magique, +4 argc, +8 envc, +12 taille, +16 les chaines a la suite.
BLOB_TAG	= 0x80000000
BLOB_MAGIC	= 0x4E505341
BLOB_CHAINES	= 16
LIGNE_MAX	= 1024			; ARGS_MAX du noyau
SA_MAX		= 256			; argv d'un script (fc_script_argv) : entrees au plus
E_2BIG		= -7
FSE_MSG		= LIGNE_MAX + 16	; f_spawn_env : reponse de V_EXEC (un script ?)
FSE_ARGV	= FSE_MSG + MSG_TAILLE	;   son argv
FSE_SIZE	= FSE_ARGV + SA_MAX * 8

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

; x0 = reponse de V_EXEC d'un script (ATTR_SCRIPT ; donnees : nom, 0, interprete, 0, argument
; de #!, 0, script, 0), x1 = argv de l'appelant (argv[0] compris), x2 = destination (x3 entrees
; au plus) -> x0 = destination : interprete, [argument,] script, argv[1..], 0 ; 0 s'il ne tient pas
fc_script_argv:
	add	x4,x0,MSG_DATA
.n:	ldrb	w5,[x4],1			; (le nom)
	cbnz	w5,.n
	mov	x5,x4				; l'interprete
.i:	ldrb	w6,[x4],1
	cbnz	w6,.i
	mov	x6,x4				; l'argument
.a:	ldrb	w7,[x4],1
	cbnz	w7,.a
	mov	x7,x4				; le script
	sub	x3,x3,1				; (place du 0 final)
	mov	x8,0
	str	x5,[x2,x8,lsl 3]
	add	x8,x8,1
	ldrb	w9,[x6]
	cbz	w9,.s
	str	x6,[x2,x8,lsl 3]
	add	x8,x8,1
.s:	str	x7,[x2,x8,lsl 3]
	add	x8,x8,1
	cbz	x1,.fin
	ldr	x9,[x1]
	cbz	x9,.fin
	add	x1,x1,8
.v:	ldr	x9,[x1],8
	cbz	x9,.fin
	cmp	x8,x3
	b.hs	.non
	str	x9,[x2,x8,lsl 3]
	add	x8,x8,1
	b	.v
.fin:	str	xzr,[x2,x8,lsl 3]
	mov	x0,x2
	ret
.non:	mov	x0,0
	ret

; x0 = reponse de V_EXEC d'un script, x1 = ligne d'arguments (0 : aucune), x2 = destination
; (LIGNE_MAX) -> x0 = destination : « [argument ]script[ ligne] », 0 si elle ne tient pas
fc_script_ligne:
	add	x4,x0,MSG_DATA
	mov	x6,0				; sauter le nom et l'interprete
.n:	ldrb	w5,[x4],1
	cbnz	w5,.n
	add	x6,x6,1
	cmp	x6,2
	b.lo	.n
	mov	x3,x2				; ecriture
	add	x7,x2,LIGNE_MAX-1		; limite
	ldrb	w5,[x4]				; l'argument de #!, puis une espace
	cbz	w5,.sa
.ca:	ldrb	w5,[x4],1
	cbz	w5,.ea
	cmp	x3,x7
	b.hs	.non
	strb	w5,[x3],1
	b	.ca
.ea:	mov	w5,' '
	strb	w5,[x3],1
	b	.sc
.sa:	add	x4,x4,1
.sc:	ldrb	w5,[x4],1			; le script
	cbz	w5,.ls
	cmp	x3,x7
	b.hs	.non
	strb	w5,[x3],1
	b	.sc
.ls:	cbz	x1,.fin				; puis la ligne
	ldrb	w5,[x1]
	cbz	w5,.fin
	cmp	x3,x7
	b.hs	.non
	mov	w5,' '
	strb	w5,[x3],1
.cl:	ldrb	w5,[x1],1
	cbz	w5,.fin
	cmp	x3,x7
	b.hs	.non
	strb	w5,[x3],1
	b	.cl
.fin:	strb	wzr,[x3]
	mov	x0,x2
	ret
.non:	mov	x0,0
	ret

; --- f_spawn_env(chemin, argv, envp, &fds[3]) -> pid : comme f_spawn_stdio, avec argv (argv[0]
; compris) et l'environnement, sans limite de longueur, pour un programme C ; un programme en
; assembleur lit la ligne d'arguments (vide si elle depasse 1 024 octets). Un script (V_EXEC :
; ATTR_SCRIPT) : son interprete est lance, argv = interprete, [argument de #!,] script, argv[1..]
f_spawn_env:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,FSE_SIZE
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	add	x0,sp,FSE_MSG			; un script ? (V_EXEC, fichier referme)
	mov	x1,V_EXEC
	mov	x2,0
	mov	x3,x19
	bl	fc_msg_chemin
	cbnz	x0,.prog
	add	x0,sp,FSE_MSG
	bl	fc_vfs
	tbnz	x0,63,.prog			; (l'erreur viendra du lancement)
	add	x2,sp,FSE_MSG
	ldp	x0,x1,[x2,MSG_A]
	bl	fc_fs_close
	ldr	x0,[sp,FSE_MSG+MSG_C]
	tst	x0,ATTR_SCRIPT
	b.eq	.prog
	add	x0,sp,FSE_MSG
	mov	x1,x20
	add	x2,sp,FSE_ARGV
	mov	x3,SA_MAX
	bl	fc_script_argv
	cbz	x0,.e2big
	mov	x20,x0
	add	x19,sp,FSE_MSG+MSG_DATA		; l'interprete : apres le nom
.in:	ldrb	w0,[x19],1
	cbnz	w0,.in
.prog:	mov	x0,x20
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
	b	.out
.e2big:	mov	x19,E_2BIG
.out:	mov	x0,x19
	add	sp,sp,FSE_SIZE
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
