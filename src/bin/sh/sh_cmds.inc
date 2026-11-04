; sh_cmds.inc - sh : commandes integrees (docs/SHELL.md)
;
; Une commande : x0 = argv -> x0 = code de sortie. Deux tables : les commandes du shell passent
; avant PATH ; les anciennes commandes du terminal (ls, cat...) seulement si PATH n'a rien.

; x0 = entree de table, x1 = table -> x0 = entree du nom, 0 si absent
table_cherche:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	ldr	x0,[x20]
	cbz	x0,.non
	mov	x1,x19
	bl	str_eq
	cbnz	x0,.oui
	add	x20,x20,16
	b	.c
.oui:	mov	x0,x20
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

integree_cherche:
	adr	x1,integrees
	b	table_cherche

ancienne_cherche:
	adr	x1,anciennes
	b	table_cherche

; x0 = entree, x1 = argv : lancee avec ses redirections ; last_code
integree_lance:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	flush
	adr	x0,out_fd
	ldr	x21,[x0]
	adr	x0,err_fd
	ldr	x22,[x0]
	mov	x23,-1				; fichier ouvert pour cette commande
	adr	x24,reds
	adr	x0,reds_n
	ldr	x9,[x0]
.r:	cbz	x9,.lance
	ldr	x0,[x24,RE_FD]
	ldr	x1,[x24,RE_TYPE]
	cmp	x1,R_DUP
	b.ne	.fic
	ldr	x2,[x24,RE_CIBLE]
	ldrb	w2,[x2]
	cmp	x0,1				; 1>&2
	b.ne	.d2
	adr	x3,err_fd
	ldr	x3,[x3]
	adr	x4,out_fd
	str	x3,[x4]
	b	.n
.d2:	cmp	x0,2				; 2>&1
	b.ne	.n
	adr	x3,out_fd
	ldr	x3,[x3]
	adr	x4,err_fd
	str	x3,[x4]
	b	.n
.fic:	cmp	x1,R_IN
	b.eq	.n
	cmp	x0,2
	b.hi	.n
	cbz	x0,.n
	str	x9,[sp,-16]!
	ldr	x0,[x24,RE_CIBLE]
	adr	x1,s_devnull
	bl	str_eq
	mov	x2,-1
	cbnz	x0,.pose
	ldr	x0,[x24,RE_CIBLE]
	ldr	x2,[x24,RE_TYPE]
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	cmp	x2,R_OUT
	b.eq	.o
	mov	x1,O_WRITE or O_CREATE or O_APPEND
.o:	sys	SYS_OPEN
	mov	x2,x0
	tbz	x0,63,.ouvert
	ldr	x0,[x24,RE_CIBLE]
	bl	err_fichier
	ldr	x9,[sp],16
	mov	x0,1
	b	.fin
.ouvert:
	mov	x23,x0
.pose:	ldr	x9,[sp],16
	ldr	x0,[x24,RE_FD]
	adr	x1,out_fd
	cmp	x0,2
	b.ne	.p1
	adr	x1,err_fd
.p1:	str	x2,[x1]
.n:	add	x24,x24,RE_TAILLE
	sub	x9,x9,1
	b	.r
.lance:	ldr	x1,[x19,8]
	mov	x0,x20
	blr	x1
.fin:	adr	x1,last_code
	str	x0,[x1]
	bl	flush
	adr	x0,out_fd
	str	x21,[x0]
	adr	x0,err_fd
	str	x22,[x0]
	tbnz	x23,63,.out
	mov	x0,x23
	sys	SYS_CLOSE
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- messages ---------------------------------------------------------------

; x0 = chaine, sur l'erreur standard
err_puts:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	flush
	mov	x0,x19
	bl	str_len
	mov	x2,x0
	adr	x0,err_fd
	ldr	x0,[x0]
	tbnz	x0,63,.out
	mov	x1,x19
	sys	SYS_WRITE
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte du shell : traduit, sur l'erreur standard
err_dire:
	prologue
	bl	lang_tr
	bl	err_puts
	epilogue

; x0 = nom : « sh: <texte> <nom> »
err_nom:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,s_prefixe
	bl	err_puts
	mov	x0,x20
	bl	err_dire
	mov	x0,x19
	bl	err_puts
	adr	x0,s_nl
	bl	err_puts
	ldp	x19,x20,[sp],16
	epilogue

err_fichier:
	adr	x1,s_nofile
	b	err_nom

err_introuvable:
	adr	x1,s_notfound
	b	err_nom

; x0 = code d'erreur du systeme : ecrit ; last_code = 1
erreur_code:
	prologue
	sub	sp,sp,32
	neg	x0,x0
	mov	x1,sp
	bl	fmt_dec
	adr	x0,s_err
	bl	err_dire
	mov	x0,sp
	bl	err_puts
	adr	x0,s_nl
	bl	err_puts
	mov	x0,1
	adr	x1,last_code
	str	x0,[x1]
	add	sp,sp,32
	epilogue

; x0 = argv : « + cmd args » (set -x)
trace:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,s_plus
	bl	err_puts
.c:	ldr	x0,[x19],8
	cbz	x0,.f
	bl	err_puts
	ldr	x0,[x19]
	cbz	x0,.f
	adr	x0,s_blanc
	bl	err_puts
	b	.c
.f:	adr	x0,s_nl
	bl	err_puts
	ldr	x19,[sp],16
	epilogue

; --- commandes du shell -----------------------------------------------------

cmd_true:
	mov	x0,0
	ret

cmd_false:
	mov	x0,1
	ret

; echo [-n] mots
cmd_echo:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x0,8
	mov	x20,0				; 1 : pas de fin de ligne
	ldr	x0,[x19]
	cbz	x0,.c
	adr	x1,s_n
	bl	str_eq
	cbz	x0,.c
	mov	x20,1
	add	x19,x19,8
.c:	ldr	x0,[x19],8
	cbz	x0,.f
	bl	term_puts
	ldr	x0,[x19]
	cbz	x0,.f
	mov	w0,' '
	bl	term_putb
	b	.c
.f:	cbnz	x20,.out
	mov	w0,10
	bl	term_putb
.out:	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

cmd_pwd:
	prologue
	bl	update_cwd
	tputs	cwd
	mov	w0,10
	bl	term_putb
	mov	x0,0
	epilogue

cmd_cd:
	prologue
	ldr	x0,[x0,8]
	cbnz	x0,.go
	adr	x0,s_HOME
	mov	x1,4
	bl	var_valeur
	cbnz	x0,.go
	adr	x0,s_sd
.go:	str	x0,[sp,-16]!
	sys	SYS_CHDIR
	ldr	x1,[sp],16
	cbz	x0,.out
	mov	x0,x1
	bl	err_fichier
	mov	x0,1
.out:	epilogue

; exit [code]
cmd_exit:
	prologue
	ldr	x0,[x0,8]
	cbz	x0,.der
	bl	dec_signe
	adr	x1,last_code
	str	x0,[x1]
.der:	bl	sh_sortie

; export NOM[=VALEUR]...
cmd_export:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x0,8
.c:	ldr	x20,[x19],8
	cbz	x20,.f
	mov	x0,0
.l:	ldrb	w1,[x20,x0]
	cbz	w1,.seul
	cmp	w1,'='
	b.eq	.kv
	add	x0,x0,1
	b	.l
.kv:	mov	x0,x20
	mov	x1,1
	bl	var_pose_kv
	b	.c
.seul:	mov	x1,x0
	mov	x0,x20
	bl	var_cherche
	cbz	x0,.neuve
	mov	x1,1
	str	x1,[x0,VE_DRAP]
	b	.c
.neuve:	mov	x0,x20
	bl	str_len
	mov	x1,x0
	mov	x0,x20
	adr	x2,s_vide
	mov	x3,1
	bl	var_pose
	b	.c
.f:	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

cmd_unset:
	prologue
	str	x19,[sp,-16]!
	add	x19,x0,8
.c:	ldr	x0,[x19],8
	cbz	x0,.f
	bl	var_retire
	b	.c
.f:	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; set [-e] [-x] [+e] [+x] [-- args]
cmd_set:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x0,8
.c:	ldr	x20,[x19]
	cbz	x20,.fin
	ldrb	w0,[x20]
	cmp	w0,'-'
	b.eq	.opt
	cmp	w0,'+'
	b.ne	.args
.opt:	cmp	w0,'-'
	cset	x2,eq				; 1 : activer
	ldrb	w0,[x20,1]
	cmp	w0,'-'
	b.ne	.f
	add	x19,x19,8
	b	.args
.f:	add	x20,x20,1
	ldrb	w0,[x20]
	cbz	w0,.nx
	cmp	w0,'e'
	b.ne	.x
	adr	x1,opt_e
	str	x2,[x1]
	b	.f
.x:	cmp	w0,'x'
	b.ne	.f
	adr	x1,opt_x
	str	x2,[x1]
	b	.f
.nx:	add	x19,x19,8
	b	.c
.args:	ldr	x0,[x19]
	cbz	x0,.fin
	mov	x0,x19				; nouveaux $1... (copie permanente)
	bl	pos_copier
.fin:	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tableau de chaines termine par 0 : deviennent $1, $2... ($0 garde)
pos_copier:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,pos_v
	ldr	x1,[x0]
	ldr	x2,[x1]				; $0
	adr	x3,varm_p			; (tableau et textes dans varm)
	ldr	x3,[x3]
	add	x3,x3,7
	and	x3,x3,not 7
	str	x3,[x0]
	str	x2,[x3]
	mov	x4,0
.n:	ldr	x5,[x19,x4,lsl 3]
	cbz	x5,.nf
	add	x4,x4,1
	b	.n
.nf:	adr	x0,pos_n
	str	x4,[x0]
	add	x6,x4,2
	add	x6,x3,x6,lsl 3			; textes apres le tableau
	mov	x7,0
.c:	cmp	x7,x4
	b.hs	.f
	ldr	x5,[x19,x7,lsl 3]
	add	x7,x7,1
	str	x6,[x3,x7,lsl 3]
.t:	ldrb	w8,[x5],1
	strb	w8,[x6],1
	cbnz	w8,.t
	b	.c
.f:	adr	x0,varm_p
	str	x6,[x0]
	ldp	x19,x20,[sp],16
	epilogue

; shift [n]
cmd_shift:
	prologue
	mov	x2,1
	ldr	x0,[x0,8]
	cbz	x0,.d
	bl	dec_signe
	mov	x2,x0
.d:	adr	x0,pos_n
	ldr	x1,[x0]
	cmp	x2,x1
	b.hi	.non
	sub	x1,x1,x2
	str	x1,[x0]
	adr	x0,pos_v
	ldr	x3,[x0]
	ldr	x4,[x3]				; $0 reste devant
	add	x3,x3,x2,lsl 3
	str	x4,[x3]
	str	x3,[x0]
	mov	x0,0
	epilogue
.non:	mov	x0,1
	epilogue

cmd_break:
	mov	x1,2
	b	cmd_saut
cmd_continue:
	mov	x1,1
cmd_saut:
	adr	x2,saut
	str	x1,[x2]
	mov	x0,0
	ret

; read NOM : une ligne de l'entree standard -> 1 a la fin
cmd_read:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,16
	ldr	x19,[x0,8]
	cbnz	x19,.nom
	adr	x19,s_REPLY
.nom:	adr	x20,ligne
.l:	mov	x0,0
	mov	x1,sp
	mov	x2,1
	sys	SYS_READ
	cmp	x0,1
	b.ne	.fin
	ldrb	w0,[sp]
	cmp	w0,10
	b.eq	.pose
	adr	x1,ligne
	add	x1,x1,4000
	cmp	x20,x1
	b.hs	.l
	strb	w0,[x20],1
	b	.l
.fin:	adr	x1,ligne
	cmp	x20,x1
	mov	x0,1
	b.eq	.out
.pose:	strb	wzr,[x20]
	mov	x0,x19
	bl	str_len
	mov	x1,x0
	mov	x0,x19
	adr	x2,ligne
	mov	x3,0
	bl	var_pose
	mov	x0,0
.out:	add	sp,sp,16
	ldp	x19,x20,[sp],16
	epilogue

; exec commande : lancee, puis fin du shell avec son code
cmd_exec:
	prologue
	add	x0,x0,8
	ldr	x1,[x0]
	cbz	x1,.rien
	mov	x1,0
	mov	x2,0
	bl	lance_externe
	bl	sh_sortie
.rien:	mov	x0,0
	epilogue

include '../../inc/testcmd.inc'

cmd_help:
	prologue
	tdire	s_help
	mov	x0,0
	epilogue

; --- anciennes commandes du terminal -----------------------------------------

cmd_clear:
	mov	x0,0
	ret

; mkdir [-p] dossiers
cmd_mkdir:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	add	x19,x0,8
	mov	x20,0				; 1 : -p
	mov	x21,0				; code
	ldr	x0,[x19]
	cbz	x0,.f
	adr	x1,s_p
	bl	str_eq
	cbz	x0,.c
	mov	x20,1
	add	x19,x19,8
.c:	ldr	x0,[x19],8
	cbz	x0,.f
	cbz	x20,.un
	bl	mkdir_p
	b	.ch
.un:	str	x0,[sp,-16]!
	sys	SYS_MKDIR
	ldr	x1,[sp],16
	cbz	x0,.c
	mov	x0,x1
	bl	err_fichier
	mov	x0,1
.ch:	orr	x21,x21,x0
	b	.c
.f:	mov	x0,x21
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin : chaque niveau cree au besoin -> x0 = 0
mkdir_p:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x1,x0
	adr	x0,chemin_tmp
	mov	x2,0
.cp:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	cbz	w3,.go
	cmp	x2,250
	b.lo	.cp
	strb	wzr,[x0,x2]
.go:	adr	x19,chemin_tmp
	add	x20,x19,1
.c:	ldrb	w0,[x20]
	cbz	w0,.der
	cmp	w0,'/'
	b.ne	.n
	ldurb	w0,[x20,-1]			; pas la racine d'un volume
	cmp	w0,':'
	b.eq	.n
	strb	wzr,[x20]
	mov	x0,x19
	sys	SYS_MKDIR
	mov	w0,'/'
	strb	w0,[x20]
.n:	add	x20,x20,1
	b	.c
.der:	mov	x0,x19
	sys	SYS_MKDIR
	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

; rm [-f] [-r] chemins
cmd_rm:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	add	x19,x0,8
	mov	x20,0				; 1 : -f
	mov	x21,0
.o:	ldr	x0,[x19]
	cbz	x0,.f
	ldrb	w1,[x0]
	cmp	w1,'-'
	b.ne	.c
	mov	x20,1
	add	x19,x19,8
	b	.o
.c:	ldr	x0,[x19],8
	cbz	x0,.f
	str	x0,[sp,-16]!
	sys	SYS_UNLINK
	ldr	x1,[sp],16
	cbz	x0,.c
	cbnz	x20,.c
	mov	x0,x1
	bl	err_fichier
	mov	x21,1
	b	.c
.f:	mov	x0,x21
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

cmd_kill:
	prologue
	ldr	x0,[x0,8]
	cbz	x0,.u
	bl	dec_signe
	sys	SYS_KILL
	cbz	x0,.out
	bl	erreur_code
.u:	mov	x0,1
.out:	epilogue

cmd_ls:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x0,[x0,8]
	cbnz	x0,.op
	adr	x0,s_dot
.op:	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x19,x0
.next:	mov	x0,x19
	adr	x1,dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.end
	adr	x0,dirent
	bl	term_puts
	adr	x1,dirent
	ldr	x0,[x1,DIRENT_ATTR]
	tst	x0,ATTR_DIR
	b.eq	.file
	mov	w0,'/'
	bl	term_putb
	b	.nl
.file:	adr	x0,dirent			; aligner la taille en colonne 40
	bl	count_cols
	mov	x20,x0
.pad:	cmp	x20,38
	b.hs	.size
	mov	w0,' '
	bl	term_putb
	add	x20,x20,1
	b	.pad
.size:	mov	w0,' '
	bl	term_putb
	adr	x1,dirent
	ldr	x0,[x1,DIRENT_SIZE]
	bl	term_dec
	tdire	s_octets
.nl:	mov	w0,10
	bl	term_putb
	b	.next
.end:	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,0
	b	.out
.err:	bl	erreur_code
	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; cat fichiers
cmd_cat:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	add	x19,x0,8
	mov	x21,0
.a:	ldr	x20,[x19],8
	cbz	x20,.f
	mov	x0,x20
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x20,x0
.rd:	mov	x0,x20
	adr	x1,iobuf
	mov	x2,512
	sys	SYS_READ
	cmp	x0,0
	b.le	.end
	mov	x1,x0
	adr	x0,iobuf
	bl	term_write
	b	.rd
.end:	mov	x0,x20
	sys	SYS_CLOSE
	b	.a
.err:	mov	x0,x20
	bl	err_fichier
	mov	x21,1
	b	.a
.f:	mov	x0,x21
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; cp source destination
cmd_cp:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x19,[x0,8]
	cbz	x19,.usage
	ldr	x20,[x0,16]
	cbz	x20,.usage
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x21,x0
	mov	x0,x20
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.err_close
	mov	x22,x0
.cp:	mov	x0,x21
	adr	x1,iobuf
	mov	x2,512
	sys	SYS_READ
	cmp	x0,0
	b.le	.done
	mov	x2,x0
	mov	x0,x22
	adr	x1,iobuf
	sys	SYS_WRITE
	b	.cp
.done:	mov	x0,x22
	sys	SYS_CLOSE
	mov	x0,x21
	sys	SYS_CLOSE
	mov	x0,0
	b	.out
.err_close:
	mov	x19,x0
	mov	x0,x21
	sys	SYS_CLOSE
	mov	x0,x19
.err:	bl	erreur_code
	mov	x0,1
	b	.out
.usage:	adr	x0,s_cp_usage
	bl	err_dire
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

cmd_ps:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	tdire	s_ps_head
	mov	x21,0				; emplacement de depart (SYS_PROCLIST_FROM)
.paq:	adr	x0,iobuf2
	mov	x1,64
	mov	x2,x21
	sys	SYS_PROCLIST_FROM
	tbnz	x0,63,.err
	mov	x19,x0
	mov	x22,x0
	adr	x20,iobuf2
.p:	cbz	x19,.suite
	ldr	x0,[x20]
	bl	term_dec_pad6
	ldr	x0,[x20,8]
	bl	term_dec_pad6
	ldr	x0,[x20,16]
	bl	term_dec_pad6
	mov	w0,' '
	bl	term_putb
	add	x0,x20,24
	bl	term_puts
	mov	w0,10
	bl	term_putb
	ldr	x21,[x20]			; (la suite : apres ce process)
	and	x21,x21,0xFFFF
	add	x20,x20,64
	sub	x19,x19,1
	b	.p
.suite:	cmp	x22,64				; morceau plein : la suite
	b.eq	.paq
	b	.fin
.err:	bl	erreur_code
.fin:	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur : ecrite sur 6 colonnes alignees a droite, puis 2 espaces
term_dec_pad6:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	bl	str_len
	mov	x19,x0
.p:	cmp	x19,6
	b.hs	.v
	mov	w0,' '
	bl	term_putb
	add	x19,x19,1
	b	.p
.v:	mov	x0,sp
	bl	term_puts
	mov	w0,' '
	bl	term_putb
	mov	w0,' '
	bl	term_putb
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

cmd_mem:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,iobuf2
	sys	SYS_SYSINFO
	tbnz	x0,63,.err
	adr	x19,iobuf2
	tdire	s_mem_cpu
	ldr	x0,[x19]
	bl	term_dec
	tdire	s_mem_free
	ldr	x0,[x19,8]
	bl	term_dec
	tdire	s_mem_tot
	ldr	x0,[x19,16]
	bl	term_dec
	tdire	s_mem_up
	ldr	x0,[x19,24]
	mov	x1,1000
	udiv	x0,x0,x1
	bl	term_dec
	tdire	s_mem_proc
	ldr	x0,[x19,32]
	bl	term_dec
	tdire	s_mem_load
	mov	x20,0
.l:	ldr	x0,[x19,40]
	add	x19,x19,8
	bl	term_dec
	tputs	s_pct
	add	x20,x20,1
	cmp	x20,4
	b.lo	.l
	mov	w0,10
	bl	term_putb
	b	.out
.err:	bl	erreur_code
.out:	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue
