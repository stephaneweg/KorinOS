; sh - interpreteur de commandes (docs/SHELL.md) : sous-ensemble de POSIX, sans fork, en
; assembleur pour rester dans le selfhost.
;   sh                     interactif : lit des lignes sur son entree standard (un tube de vfs :
;                          telnetd), ecrit sur sa sortie standard ; Ctrl+C arrete le programme
;   sh -c "commandes" [nom [args]]
;   sh script [args]
; Variables, guillemets, $(...), tubes, redirections, && || ; if for while until case, { } ( ).
; Tout autre nom est cherche dans PATH, puis SD0:/apps/<nom>.app. Les anciennes commandes du
; terminal (ls, cat, cp, mkdir, rm, ps, kill, mem) servent si PATH n'a pas de programme de ce nom.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

IN_MAX		= 1023
OBUF		= 1024
PEND_MAX	= 512
TXT_MAX		= 1 shl 20		; texte d'un script
AR_MAX		= 16 shl 20		; arene des mots
AT_MAX		= 1 shl 20		; pointeurs (argv)
VAR_MAX		= 4 shl 20		; textes des variables
CAP_MAX		= 4 shl 20		; sortie d'un $(...)
TRANCHE_MAX	= 1 shl 20		; texte d'un sh -c

macro tputs lbl* {
	adr	x0,lbl
	bl	term_puts
}

macro tdire lbl* {				; (texte du shell : traduit, jamais une donnee)
	adr	x0,lbl
	bl	lang_tr
	bl	term_puts
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0				; ligne d'arguments
	bl	kits_open
	bl	mem_init
	mov	x0,x19
	bl	args_init
	bl	options
	cbnz	x0,sh_interactif
	bl	sh_sortie

; ---------------------------------------------------------------------------
;  Demarrage
; ---------------------------------------------------------------------------

; x0 = octets -> x0 = region a zero, a la demande
region:
	mov	x1,x0
	mov	x0,0
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	ret

mem_init:
	prologue
	movl	x0,TXT_MAX+8
	bl	region
	adr	x1,txt
	str	x0,[x1]
	movl	x0,AR_MAX
	bl	region
	adr	x1,ar_p
	str	x0,[x1]
	movl	x0,AT_MAX
	bl	region
	adr	x1,at_p
	str	x0,[x1]
	movl	x0,VAR_MAX
	bl	region
	adr	x1,varm_p
	str	x0,[x1]
	movl	x0,CAP_MAX
	bl	region
	adr	x1,cap_p
	str	x0,[x1]
	movl	x0,TRANCHE_MAX
	bl	region
	adr	x1,tranche_p
	str	x0,[x1]
	epilogue

; x0 = ligne d'arguments : argv (arg_v) et l'environnement, donnes par le pere si c'est un
; programme C (ou un sh), sinon la ligne coupee et les valeurs par defaut
args_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	f_start_blob
	cbz	x0,.ligne
	mov	x20,x0
	ldr	w21,[x20,4]			; argc
	ldr	w22,[x20,8]			; envc
	add	x20,x20,16
	adr	x0,at_p
	ldr	x0,[x0]
	adr	x1,arg_v
	str	x0,[x1]
	adr	x1,arg_n
	str	x21,[x1]
.a:	cbz	x21,.af
	mov	x0,x20
	bl	at_pousse
	mov	x0,x20
	bl	str_len
	add	x20,x20,x0
	add	x20,x20,1
	sub	x21,x21,1
	b	.a
.af:	mov	x0,0
	bl	at_pousse
.e:	cbz	x22,.out
	mov	x0,x20
	mov	x1,1
	bl	var_pose_kv
	mov	x0,x20
	bl	str_len
	add	x20,x20,x0
	add	x20,x20,1
	sub	x22,x22,1
	b	.e
.ligne:	adr	x20,env_defaut			; valeurs par defaut
.d:	ldr	x0,[x20],8
	cbz	x0,.df
	mov	x1,1
	bl	var_pose_kv
	b	.d
.df:	adr	x0,at_p
	ldr	x0,[x0]
	adr	x1,arg_v
	str	x0,[x1]
	adr	x0,s_sh
	bl	at_pousse
	mov	x20,1				; argc
	cbz	x19,.fin
	mov	x0,x19				; une copie a soi, coupee aux blancs
	bl	str_len
	add	x0,x0,1
	mov	x2,x0
	adr	x0,ar_p
	ldr	x0,[x0]
	add	x3,x0,x2
	adr	x4,ar_p
	str	x3,[x4]
	mov	x21,x0
	mov	x1,x19
	bl	mem_move
	mov	x19,x21
.b:	ldrb	w0,[x19]
	cbz	w0,.fin
	cmp	w0,' '
	b.ne	.m
	add	x19,x19,1
	b	.b
.m:	cmp	w0,'"'
	b.eq	.q
	mov	x0,x19
	bl	at_pousse
	add	x20,x20,1
.t:	ldrb	w0,[x19]
	cbz	w0,.fin
	cmp	w0,' '
	b.eq	.z
	add	x19,x19,1
	b	.t
.z:	strb	wzr,[x19],1
	b	.b
.q:	add	x19,x19,1			; entre guillemets : \" et \\
	mov	x0,x19
	bl	at_pousse
	add	x20,x20,1
	mov	x1,x19
.q1:	ldrb	w0,[x19]
	cbz	w0,.qf
	add	x19,x19,1
	cmp	w0,'"'
	b.eq	.qz
	cmp	w0,'\'
	b.ne	.q2
	ldrb	w2,[x19]
	cmp	w2,'"'
	b.eq	.q3
	cmp	w2,'\'
	b.ne	.q2
.q3:	mov	w0,w2
	add	x19,x19,1
.q2:	strb	w0,[x1],1
	b	.q1
.qz:	strb	wzr,[x1]
	b	.b
.qf:	strb	wzr,[x1]
.fin:	adr	x1,arg_n
	str	x20,[x1]
	mov	x0,0
	bl	at_pousse
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les options -> x0 = 1 : interactif ; 0 : le texte a ete execute
options:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x19,arg_v
	ldr	x19,[x19]
	mov	x0,x19
	adr	x1,pos_v
	str	x0,[x1]
	add	x19,x19,8			; argv[1]
	mov	x21,0				; le texte de -c
.o:	ldr	x20,[x19]
	cbz	x20,.fini
	ldrb	w0,[x20]
	cmp	w0,'-'
	b.ne	.fini
	ldrb	w0,[x20,1]
	cbz	w0,.fini
	add	x19,x19,8
	cmp	w0,'-'
	b.eq	.fini
.l:	add	x20,x20,1
	ldrb	w0,[x20]
	cbz	w0,.lc
	cmp	w0,'c'
	b.ne	.e
	mov	x21,1
	b	.l
.e:	cmp	w0,'e'
	b.ne	.x
	adr	x0,opt_e
	mov	x1,1
	str	x1,[x0]
	b	.l
.x:	cmp	w0,'x'
	b.ne	.l
	adr	x0,opt_x
	mov	x1,1
	str	x1,[x0]
	b	.l
.lc:	cbz	x21,.o
.fini:	cbz	x21,.script
	ldr	x21,[x19]			; sh -c texte [nom [args]]
	cbz	x21,.vide
	add	x19,x19,8
	ldr	x0,[x19]
	cbz	x0,.posp
	mov	x0,x19
	b	.setp
.posp:	adr	x0,arg_v
	ldr	x0,[x0]
	b	.set0
.setp:	mov	x1,0
.cp:	ldr	x2,[x0,x1,lsl 3]
	cbz	x2,.cf
	add	x1,x1,1
	b	.cp
.cf:	sub	x1,x1,1
	adr	x2,pos_v
	str	x0,[x2]
	adr	x2,pos_n
	str	x1,[x2]
	b	.run
.set0:	adr	x2,pos_v
	str	x0,[x2]
	adr	x2,pos_n
	str	xzr,[x2]
.run:	mov	x0,x21
	bl	run_texte
	mov	x0,0
	b	.out
.vide:	mov	x0,0
	b	.out
.script:
	ldr	x21,[x19]
	cbnz	x21,.fic
	mov	x0,1				; interactif
	b	.out
.fic:	mov	x0,x19				; $0 : le script, $1... : ses arguments
	mov	x1,0
.cq:	ldr	x2,[x0,x1,lsl 3]
	cbz	x2,.cr
	add	x1,x1,1
	b	.cq
.cr:	sub	x1,x1,1
	adr	x2,pos_v
	str	x0,[x2]
	adr	x2,pos_n
	str	x1,[x2]
	mov	x0,x21
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.ko
	mov	x19,x0
	adr	x20,txt
	ldr	x20,[x20]
.rd:	mov	x0,x19
	mov	x1,x20
	mov	x2,4096
	sys	SYS_READ
	cmp	x0,0
	b.le	.lu
	add	x20,x20,x0
	adr	x1,txt
	ldr	x1,[x1]
	sub	x2,x20,x1
	movl	x3,TXT_MAX-4096
	cmp	x2,x3
	b.lo	.rd
.lu:	strb	wzr,[x20]
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,txt
	ldr	x0,[x0]
	bl	run_texte
	mov	x0,0
	b	.out
.ko:	mov	x0,x21
	bl	err_fichier
	mov	x0,127
	adr	x1,last_code
	str	x0,[x1]
	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; fin du shell, avec le code de la derniere commande
sh_sortie:
	bl	flush
	adr	x0,last_code
	ldr	x0,[x0]
	bl	code_shell
	sys	SYS_EXIT

; x0 = texte : verifie, puis execute -> x0 = 1 si le texte est incomplet (rien n'a ete lance)
run_texte:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,syntaxe
	str	xzr,[x0]
	adr	x0,incomplet
	str	xzr,[x0]
	mov	x28,x19
	bl	noexec_plus
	bl	do_list
	bl	noexec_moins
	ldrb	w0,[x28]
	cbz	w0,.lu
	bl	erreur_syntaxe
.lu:	adr	x0,incomplet
	ldr	x0,[x0]
	cbnz	x0,.coupe
	adr	x0,syntaxe
	ldr	x0,[x0]
	cbz	x0,.exec
	adr	x0,s_syntaxe
	bl	err_dire
	mov	x0,2
	adr	x1,last_code
	str	x0,[x1]
	mov	x0,0
	b	.out
.coupe:	adr	x0,interactif
	ldr	x0,[x0]
	cbnz	x0,.plus
	adr	x0,s_syntaxe
	bl	err_dire
	mov	x0,2
	adr	x1,last_code
	str	x0,[x1]
	mov	x0,0
	b	.out
.plus:	mov	x0,1
	b	.out
.exec:	mov	x28,x19
	bl	do_list
	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Interactif
; ---------------------------------------------------------------------------
sh_interactif:
	adr	x0,s_vfs			; l'entree standard : un tube de vfs
	mov	x1,5000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,vfs
	str	x0,[x1]
	adr	x1,msg
	mov	x2,V_STDIO
	str	x2,[x1]
	str	wzr,[x1,40]
	sys	SYS_SVC_CALL
	tbnz	x0,63,quit
	adr	x0,msg
	ldr	x0,[x0,8]
	adr	x1,in_h
	str	x0,[x1]
	cbz	x0,quit				; (console : rien a lire)
	adr	x0,interactif
	mov	x1,1
	str	x1,[x0]
	tdire	s_banner
.loop:	bl	update_cwd			; invite
	tputs	cwd
	tputs	s_gt
	bl	flush
	adr	x19,txt
	ldr	x19,[x19]			; le texte, ligne apres ligne s'il est incomplet
.ligne:	bl	read_line
	tbnz	x0,63,quit			; fin de l'entree : fin de la session
	adr	x1,inbuf
.cp:	ldrb	w2,[x1],1
	strb	w2,[x19],1
	cbnz	w2,.cp
	adr	x0,txt
	ldr	x0,[x0]
	bl	run_texte
	cbz	x0,.fin
	sub	x19,x19,1			; une ligne de plus
	mov	w0,10
	strb	w0,[x19],1
	tputs	s_suite
	bl	flush
	b	.ligne
.fin:	bl	term_ensure_nl
	b	.loop
quit:	bl	flush
	mov	x0,0
	sys	SYS_EXIT

; x0 = octets voulus (<= 464), x1 = 1 sans attente -> x0 = octets (dans
; msg + 48), 0 fin, E_AGAIN
in_read:
	prologue
	adr	x2,msg
	mov	x3,V_PIPE_READ
	str	x3,[x2]
	adr	x3,in_h
	ldr	x3,[x3]
	stp	x3,x0,[x2,8]
	str	x1,[x2,24]
	str	wzr,[x2,40]
	adr	x0,vfs
	ldr	x0,[x0]
	mov	x1,x2
	sys	SYS_SVC_CALL
	epilogue

; une ligne de l'entree dans inbuf (sans fin de ligne) -> x0 = 0, < 0 a la
; fin de l'entree. Octets en avance gardes (pend, pend_n).
read_line:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0				; longueur
.c:	adr	x0,pend_n			; un octet en reserve ?
	ldr	x1,[x0]
	adr	x2,pend_i
	ldr	x3,[x2]
	cmp	x3,x1
	b.lo	.octet
	mov	x0,464				; lire (attente)
	mov	x1,0
	bl	in_read
	cmn	x0,11
	b.eq	.c
	cmp	x0,0
	b.le	.fin
	adr	x1,pend_n
	str	x0,[x1]
	adr	x1,pend_i
	str	xzr,[x1]
	adr	x0,pend				; (copie : msg resert)
	adr	x1,msg
	add	x1,x1,48
	adr	x2,pend_n
	ldr	x2,[x2]
	bl	mem_move
	b	.c
.octet:	adr	x4,pend
	ldrb	w20,[x4,x3]
	add	x3,x3,1
	str	x3,[x2]
	cmp	w20,10
	b.eq	.eol
	cmp	w20,13
	b.eq	.c
	cmp	w20,3				; Ctrl+C : la ligne est abandonnee
	b.eq	.annule
	cmp	x19,IN_MAX
	b.hs	.c
	adr	x0,inbuf
	strb	w20,[x0,x19]
	add	x19,x19,1
	b	.c
.annule:
	mov	x19,0
	tputs	s_ctrlc
	bl	flush
	b	.c
.eol:	adr	x0,inbuf
	strb	wzr,[x0,x19]
	adr	x0,last_c			; (la ligne tapee finit la ligne)
	mov	x1,10
	str	x1,[x0]
	mov	x0,0
	b	.out
.fin:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- sortie (tampon de OBUF octets) --------------------------------------------
flush:
	prologue
	adr	x3,obuf_n
	ldr	x2,[x3]
	cbz	x2,.out
	str	xzr,[x3]
	adr	x0,out_fd
	ldr	x0,[x0]
	tbnz	x0,63,.out			; (/dev/null)
	adr	x1,obuf
	sys	SYS_WRITE
.out:	epilogue

; w0 = octet
term_putb:
	prologue
	adr	x1,obuf_n
	ldr	x2,[x1]
	cmp	x2,OBUF
	b.lo	.m
	str	x0,[sp,-16]!
	bl	flush
	ldr	x0,[sp],16
	adr	x1,obuf_n
	ldr	x2,[x1]
.m:	adr	x3,obuf
	strb	w0,[x3,x2]
	add	x2,x2,1
	str	x2,[x1]
	adr	x1,last_c
	str	x0,[x1]
	epilogue

; x0 = chaine
term_puts:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
.c:	ldrb	w0,[x19],1
	cbz	w0,.out
	bl	term_putb
	b	.c
.out:	ldr	x19,[sp],16
	epilogue

; x0 = tampon, x1 = octets
term_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	cbz	x20,.out
	ldrb	w0,[x19],1
	bl	term_putb
	sub	x20,x20,1
	b	.c
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur signee
term_dec:
	prologue
	sub	sp,sp,32
	cmp	x0,0
	b.ge	.p
	neg	x0,x0
	str	x0,[sp,24]
	mov	w0,'-'
	bl	term_putb
	ldr	x0,[sp,24]
.p:	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	bl	term_puts
	add	sp,sp,32
	epilogue

term_ensure_nl:
	prologue
	adr	x0,last_c
	ldr	x0,[x0]
	cmp	x0,10
	b.eq	.out
	adr	x1,obuf_n
	ldr	x1,[x1]
	cbz	x1,.vide
	mov	w0,10
	bl	term_putb
	b	.out
.vide:	adr	x0,last_c			; (rien encore ecrit : pas de saut)
	ldr	x0,[x0]
	cbz	x0,.out
	mov	w0,10
	bl	term_putb
.out:	epilogue

count_cols:
	mov	x1,0
.c:	ldrb	w2,[x0],1
	cbz	w2,.e
	and	w2,w2,0xC0
	cmp	w2,0x80
	cinc	x1,x1,ne
	b	.c
.e:	mov	x0,x1
	ret

update_cwd:
	adr	x0,cwd
	mov	x1,256
	sys	SYS_GETCWD
	ret

include 'sh_word.inc'
include 'sh_run.inc'
include 'sh_cmds.inc'

k_if		db 'if',0
k_then		db 'then',0
k_elif		db 'elif',0
k_else		db 'else',0
k_fi		db 'fi',0
k_for		db 'for',0
k_in		db 'in',0
k_while		db 'while',0
k_until		db 'until',0
k_do		db 'do',0
k_done		db 'done',0
k_case		db 'case',0
k_esac		db 'esac',0
k_acco		db '{',0
k_accf		db '}',0
s_vfs		db 'vfs',0
s_sh		db 'sh',0
s_c		db '-c',0
s_ec		db '-ec',0
p_sh		db 'SD0:/bin/sh',0
s_PATH		db 'PATH',0
s_HOME		db 'HOME',0
s_REPLY		db 'REPLY',0
s_devnull	db '/dev/null',0
s_n		db '-n',0
s_p		db '-p',0
s_bang		db '!',0
s_crochet	db ']',0
s_vide		db 0
s_blanc		db ' ',0
s_nl		db 10,0
s_plus		db '+ ',0
s_prefixe	db 'sh: ',0
s_nofile	db 'fichier inaccessible : ',0
s_notfound	db 'commande introuvable : ',0
s_syntaxe	db 'sh: erreur de syntaxe',10,0
s_banner	db 'Korin OS - sh. Tapez help pour la liste des commandes.',10,0
s_gt		db '> ',0
s_suite		db '... ',0
s_err		db 'sh: erreur ',0
s_code		db '[code de sortie ',0
s_close		db ']',10,0
s_ctrlc		db '^C',10,0
s_dot		db '.',0
s_sd		db 'SD0:/',0
s_octets	db ' o',0
s_cp_usage	db 'usage : cp source destination',10,0
s_ps_head	db '   PID  THREADS  TAS Ko  NOM',10,0
s_mem_cpu	db 'Coeurs       : ',0
s_mem_free	db 10,'M',0xC3,0xA9,'moire libre : ',0
s_mem_tot	db ' Ko sur ',0
s_mem_up	db ' Ko',10,'En marche    : ',0
s_mem_proc	db ' s',10,'Process      : ',0
s_mem_load	db 10,'Charge       : ',0
s_pct		db '% ',0
s_help	db 'Le shell : variables, guillemets, $(...), tubes |, redirections < > >> 2>&1,',10
	db '&& || ; if for while until case { } ( ), sh -c, scripts (docs/SHELL.md).',10
	db 'Commandes int',0xC3,0xA9,'gr',0xC3,0xA9,'es : echo cd pwd exit export unset set shift test [ read',10
	db 'exec true false break continue, et si PATH n',39,'a pas ces programmes :',10
	db 'ls cat cp mkdir rm ps kill mem.',10
	db 'Tout autre nom est cherch',0xC3,0xA9,' dans PATH puis SD0:/apps/<nom>.app',10
	db '(Ctrl+C l',39,'interrompt).',10,0
	align 8

segment readable writeable
kit_data
		align 8
integrees:
	dd n_echo, cmd_echo, n_cd, cmd_cd, n_pwd, cmd_pwd, n_exit, cmd_exit
	dd n_export, cmd_export, n_unset, cmd_unset, n_set, cmd_set, n_shift, cmd_shift
	dd n_test, cmd_test, n_crochet, cmd_test, n_true, cmd_true, n_deuxp, cmd_true
	dd n_false, cmd_false, n_break, cmd_break, n_continue, cmd_continue
	dd n_read, cmd_read, n_exec, cmd_exec, n_help, cmd_help
	dd 0, 0
anciennes:
	dd n_clear, cmd_clear, n_mkdir, cmd_mkdir, n_rm, cmd_rm, n_kill, cmd_kill
	dd n_ls, cmd_ls, n_dir, cmd_ls, n_cat, cmd_cat, n_cp, cmd_cp
	dd n_ps, cmd_ps, n_mem, cmd_mem, n_free, cmd_mem
	dd 0, 0
env_defaut:
	dd e_home, e_path, e_tmp, e_lang, e_term, 0
e_home	db 'HOME=SD0:/',0
e_path	db 'PATH=/bin:/gcc/bin',0
e_tmp	db 'TMPDIR=/tmp',0
e_lang	db 'LANG=C.UTF-8',0
e_term	db 'TERM=dumb',0
n_echo	db 'echo',0
n_cd	db 'cd',0
n_pwd	db 'pwd',0
n_exit	db 'exit',0
n_export db 'export',0
n_unset	db 'unset',0
n_set	db 'set',0
n_shift	db 'shift',0
n_test	db 'test',0
n_crochet db '[',0
n_true	db 'true',0
n_deuxp	db ':',0
n_false	db 'false',0
n_break	db 'break',0
n_continue db 'continue',0
n_read	db 'read',0
n_exec	db 'exec',0
n_help	db 'help',0
n_clear	db 'clear',0
n_mkdir	db 'mkdir',0
n_rm	db 'rm',0
n_kill	db 'kill',0
n_ls	db 'ls',0
n_dir	db 'dir',0
n_cat	db 'cat',0
n_cp	db 'cp',0
n_ps	db 'ps',0
n_mem	db 'mem',0
n_free	db 'free',0
		align 8
vfs		dd 0
in_h		dd 0
pend_n		dd 0
pend_i		dd 0
obuf_n		dd 0
last_c		dd 0
last_code	dd 0
out_fd		dd 1
err_fd		dd 2
interactif	dd 0
noexec		dd 0
cond		dd 0
saut		dd 0
syntaxe		dd 0
incomplet	dd 0
chaine_ok	dd 0
subst_code	dd 0
opt_e		dd 0
opt_x		dd 0
dans_tube	dd 0
tube_in		dd 0
tube_out	dd 0
tube_pid	dd 0
reds_n		dd 0
red_dn		dd 0
arg_v		dd 0
arg_n		dd 0
pos_v		dd 0
pos_n		dd 0
vars_n		dd 0
txt		dd 0
ar_p		dd 0
at_p		dd 0
varm_p		dd 0
cap_p		dd 0
tranche_p	dd 0
reds		rb RED_MAX * RE_TAILLE
red_d		rb RED_MAX * 8
vars		rb VARS_MAX * VE_TAILLE
cwd		rb 256
chemin_tmp	rb 256
tmp_num		rb 32
inbuf		rb IN_MAX + 1
ligne		rb 4096
dirent		rb DIRENT_BYTES
iobuf		rb 520
pend		rb PEND_MAX
obuf		rb OBUF
msg		rb 512
		align 8
iobuf2		rb 64*64
