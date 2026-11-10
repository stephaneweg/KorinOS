; exectest - SYS_EXEC (docs/ABI.md, 0x6B ; etude docs/TERMINAL-ET-FORK.md,
; 4.4) par f_exec d'AppKit : meme pid et meme pere apres exec, bout de tube
; et poignee d'IPC gardes, argv et environnement, exec d'un fichier absent ou
; qui n'est pas un ELF (erreur, le programme continue), exec d'un process a
; 3 threads (les autres disparaissent), signaux (gere -> defaut, ignore
; garde), N fork + exec + wait sans fuite de pages ni manque d'ASID.
;   exectest            les essais (PASS / FAIL, resume a la fin, code = FAIL)
;   exectest <n>        idem, n fork + exec + wait dans la boucle (500)
;   exectest -<mode> .. le programme apres exec (modes plus bas)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

BOUCLE_DEF	= 500
SIGUSR1		= 30
SIGUSR2		= 31

macro verifier cond*, nom* {
	local .ok, .s
	b.#cond	.ok
	adr	x0,.s
	bl	echec
	b	@f
.s	db nom,0
	align 4
.ok:	adr	x0,.s
	bl	reussite
@@:
}

macro fork {
	sys	SYS_FORK
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0				; (arguments)
	bl	kits_open
	cbz	x19,.n0
	ldrb	w0,[x19]
	cmp	w0,'-'
	b.eq	mode
	mov	x0,x19
	bl	lire_dec
	cmp	x0,0
	b.gt	.n
.n0:	mov	x0,BOUCLE_DEF
.n:	adr	x1,tours
	str	x0,[x1]
	adr	x0,m_debut
	bl	puts

	; --- 1. meme pid, meme pere ---------------------------------------------
	fork
	cbnz	x0,.p1
	adr	x0,a_p				; fils : exec « -p <pid> <pere> »
	bl	argv_mode
	sys	SYS_GETPID
	adr	x1,s_n1
	bl	fmt_dec
	strb	wzr,[x0]
	sys	SYS_GETPPID
	adr	x1,s_n2
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,s_n1
	adr	x1,s_n2
	bl	argv_deux
	bl	exec_moi
	mov	x0,90
	sys	SYS_EXIT
.p1:	mov	x22,x0
	cmp	x22,0
	verifier gt, 'fork avant exec'
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,40
	verifier eq, 'exec garde le pid et le pere (getpid, getppid ; code 40)'

	; --- 2. bout de tube et poignee d'IPC gardes ---------------------------
	adr	x0,fds
	bl	f_pipe
	mov	x20,x0
	mov	x0,IPC_SOCK_SEQPACKET
	adr	x1,chan
	sys	SYS_CHAN_CREATE
	orr	x0,x0,x20
	cmp	x0,0
	verifier eq, 'tube et canal avant fork + exec'
	fork
	cbnz	x0,.p2
	adr	x0,fds				; fils : son lecteur ferme ; exec
	ldr	x0,[x0]				; « -t <ecrivain> <bout du canal> »
	sys	SYS_CLOSE
	adr	x0,a_t
	bl	argv_mode
	adr	x0,fds
	ldr	x0,[x0,8]
	adr	x1,s_n1
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,chan
	ldr	w0,[x0,4]
	adr	x1,s_n2
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,s_n1
	adr	x1,s_n2
	bl	argv_deux
	bl	exec_moi
	mov	x0,90
	sys	SYS_EXIT
.p2:	mov	x22,x0
	adr	x0,fds				; pere : son ecrivain ferme
	ldr	x0,[x0,8]
	sys	SYS_CLOSE
	mov	x20,0				; lecture jusqu'a la fin
.r2:	adr	x0,fds
	ldr	x0,[x0]
	adr	x1,buf
	add	x1,x1,x20
	mov	x2,100
	sys	SYS_READ
	cmp	x0,0
	b.le	.r2f
	add	x20,x20,x0
	cmp	x20,900
	b.lo	.r2
.r2f:	mov	x21,x0
	adr	x0,buf
	strb	wzr,[x0,x20]
	adr	x1,s_tube
	bl	str_eq
	cmp	x0,1
	verifier eq, 'apres exec : ecriture par le descripteur du tube herite'
	cmp	x21,0
	verifier eq, 'fin du tube (0) quand le nouveau programme le ferme'
	adr	x0,fds
	ldr	x0,[x0]
	sys	SYS_CLOSE
	bl	ipcm_recevoir
	adr	x0,chan
	ldr	w0,[x0]
	mov	x2,0
	mov	x3,5000
	sys	SYS_CHAN_RECV
	cmp	x0,s_canal_n
	verifier eq, 'apres exec : message par la poignee d''IPC de meme numero'
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,41
	verifier eq, 'nouveau programme : tube et canal (code 41)'
	adr	x0,chan
	ldr	w0,[x0]
	sys	SYS_HANDLE_CLOSE
	adr	x0,chan
	ldr	w0,[x0,4]
	sys	SYS_HANDLE_CLOSE

	; --- 3. argv et environnement ---------------------------------------------
	fork
	cbnz	x0,.p3
	adr	x0,a_a
	bl	argv_mode
	adr	x0,s_deux
	adr	x1,s_guill
	bl	argv_deux
	adr	x1,envp				; environnement : deux variables
	adr	x0,s_env1
	str	x0,[x1]
	adr	x0,s_env2
	str	x0,[x1,8]
	str	xzr,[x1,16]
	adr	x0,p_moi
	adr	x1,argv
	adr	x2,envp
	mov	x3,0
	bl	f_exec
	mov	x0,90
	sys	SYS_EXIT
.p3:	sys	SYS_WAIT
	cmp	x0,42
	verifier eq, 'nouveau programme : argv (espaces, guillemets) et environnement (code 42)'

	; --- 4. fichier absent, fichier qui n'est pas un ELF ---------------------
	sys	SYS_GETPID
	mov	x22,x0
	adr	x0,a_z
	bl	argv_mode
	adr	x1,argv
	str	xzr,[x1,16]
	adr	x0,p_absent
	adr	x1,argv
	mov	x2,0
	mov	x3,0
	bl	f_exec
	cmp	x0,0
	verifier lt, 'exec d''un fichier absent : erreur, le programme continue'
	adr	x0,p_texte
	adr	x1,argv
	mov	x2,0
	mov	x3,0
	bl	f_exec
	cmp	x0,0
	verifier lt, 'exec d''un fichier qui n''est pas un ELF : erreur, le programme continue'
	sys	SYS_GETPID
	cmp	x0,x22
	verifier eq, 'apres ces echecs : meme pid'
	adr	x0,fds				; ses descripteurs marchent encore
	bl	f_pipe
	cmp	x0,0
	verifier eq, 'apres ces echecs : f_pipe'
	adr	x0,fds
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,fds
	ldr	x0,[x0,8]
	sys	SYS_CLOSE

	; --- 5. exec d'un process a 3 threads -------------------------------------
	fork
	cbnz	x0,.p5
	adr	x0,fds				; un tube : un thread y lit (requete
	bl	f_pipe				;  a vfs en attente)
	adr	x0,dort
	adr	x1,pile1+16384
	mov	x2,0
	sys	SYS_THREAD_CREATE
	adr	x0,lit
	adr	x1,pile2+16384
	mov	x2,0
	sys	SYS_THREAD_CREATE
	adr	x0,attend
	adr	x1,pile3+16384
	mov	x2,0
	sys	SYS_THREAD_CREATE
	mov	x0,100
	sys	SYS_SLEEP
	adr	x1,buf
	mov	x0,0
	mov	x2,8
	sys	SYS_THREAD_LIST
	mov	x23,x0
	adr	x0,a_x
	bl	argv_mode
	adr	x1,argv
	str	xzr,[x1,16]
	mov	x0,4				; (4 threads avant : sinon code 60)
	cmp	x23,4
	b.ne	.f5
	bl	exec_moi
	mov	x0,90
.f5:	add	x0,x0,56
	sys	SYS_EXIT
.p5:	sys	SYS_WAIT
	cmp	x0,43
	verifier eq, 'exec avec 4 threads (sommeil, lecture de tube, futex) : un seul apres (code 43)'

	; --- 6. signaux -----------------------------------------------------------
	fork
	cbnz	x0,.p6
	mov	x0,SIGUSR1			; gere
	adr	x1,gestionnaire
	mov	x2,0
	mov	x3,0
	adr	x4,gestionnaire
	sys	SYS_SIG_ACTION
	mov	x0,SIGUSR2			; ignore
	mov	x1,1
	mov	x2,0
	mov	x3,0
	mov	x4,0
	sys	SYS_SIG_ACTION
	adr	x0,a_s
	bl	argv_mode
	adr	x1,argv
	str	xzr,[x1,16]
	bl	exec_moi
	mov	x0,90
	sys	SYS_EXIT
.p6:	sys	SYS_WAIT
	cmn	x0,SIGUSR1
	verifier eq, 'signaux apres exec : ignore garde, gere remis par defaut (fin par SIGUSR1)'

	; --- 7. fuites : N fork + exec + wait, en deux moities -------------------
	adr	x0,a_z
	bl	argv_mode
	adr	x1,argv
	str	xzr,[x1,16]
	mov	x0,10				; (premiers tours : caches des services)
	bl	tours_exec
	mov	x0,1000
	sys	SYS_SLEEP
	bl	pages_libres
	mov	x24,x0				; P0
	sys	SYS_TIME_MS
	mov	x25,x0
	adr	x0,tours
	ldr	x0,[x0]
	lsr	x0,x0,1
	bl	tours_exec
	mov	x21,x0				; refus
	mov	x22,x1				; codes faux
	mov	x0,1000
	sys	SYS_SLEEP
	bl	pages_libres
	mov	x26,x0				; P1
	adr	x0,tours
	ldr	x0,[x0]
	lsr	x0,x0,1
	bl	tours_exec
	add	x21,x21,x0
	add	x22,x22,x1
	sys	SYS_TIME_MS
	sub	x25,x0,x25
	cmp	x21,0
	verifier eq, 'boucle : aucun fork refuse (ni ASID ni memoire)'
	cmp	x22,0
	verifier eq, 'boucle : chaque exec aboutit (code 7)'
	mov	x0,1000				; le faucheur rend les fils
	sys	SYS_SLEEP
	bl	pages_libres
	sub	x23,x26,x0			; pages perdues, 2e moitie
	sub	x24,x24,x26			; 1re moitie
	adr	x0,m_tours
	bl	puts
	adr	x0,tours
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_tours2
	bl	puts
	mov	x0,x25
	bl	print_dec
	adr	x0,m_tours3
	bl	puts
	mov	x0,x24
	bl	print_sdec
	adr	x0,m_tours4
	bl	puts
	mov	x0,x23
	bl	print_sdec
	adr	x0,m_tours5
	bl	puts
	adr	x0,tours			; moins d'une page pour 8 exec
	ldr	x0,[x0]
	lsr	x0,x0,4
	add	x0,x0,16
	cmp	x23,x0
	verifier lt, 'boucle : pas de fuite de pages (2e moitie)'

	adr	x0,m_fin			; resume
	bl	puts
	adr	x0,n_pass
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_pass
	bl	puts
	adr	x0,n_fail
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_fail
	bl	puts
	adr	x0,n_fail
	ldr	x0,[x0]
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Le programme apres exec : « -<mode> arguments » (x19 = la ligne)
; ---------------------------------------------------------------------------
mode:
	ldrb	w20,[x19,1]
	add	x0,x19,2
	cmp	w20,'p'
	b.eq	mode_p
	cmp	w20,'t'
	b.eq	mode_t
	cmp	w20,'a'
	b.eq	mode_a
	cmp	w20,'x'
	b.eq	mode_x
	cmp	w20,'s'
	b.eq	mode_s
	mov	x0,7				; -z : sortie, code 7
	sys	SYS_EXIT

; -p <pid> <pere> : code 40 si getpid et getppid sont ceux d'avant exec
mode_p:	bl	lire_dec
	mov	x20,x0
	mov	x0,x1
	bl	lire_dec
	mov	x21,x0
	sys	SYS_GETPID
	cmp	x0,x20
	mov	x0,1
	b.ne	.out
	sys	SYS_GETPPID
	cmp	x0,x21
	mov	x0,2
	b.ne	.out
	mov	x0,40
.out:	sys	SYS_EXIT

; -t <descripteur> <poignee> : ecrit « tube » sur le descripteur d'AppKit
; herite, le ferme ; « canal » par la poignee ; code 41
mode_t:	bl	lire_dec
	mov	x20,x0
	mov	x0,x1
	bl	lire_dec
	mov	x21,x0
	mov	x0,x20
	adr	x1,s_tube
	mov	x2,s_tube_n
	sys	SYS_WRITE
	cmp	x0,s_tube_n
	mov	x0,1
	b.ne	.out
	mov	x0,x20
	sys	SYS_CLOSE
	adr	x0,iov
	adr	x1,s_canal
	mov	x2,s_canal_n
	stp	x1,x2,[x0]
	adr	x1,ipcm
	str	x0,[x1,IPCM_IOV]
	mov	w2,1
	str	w2,[x1,IPCM_IOVCNT]
	str	wzr,[x1,IPCM_NH]
	stp	xzr,xzr,[x1,16]
	mov	x0,x21
	mov	x2,0
	mov	x3,5000
	sys	SYS_CHAN_SEND
	cmp	x0,s_canal_n
	mov	x0,2
	b.ne	.out
	mov	x0,41
.out:	sys	SYS_EXIT

; -a : argv = { chemin, -a, « deux mots », x"y }, environnement = s_env1,
; s_env2 (objet de f_start_blob) : code 42
mode_a:	bl	f_start_blob
	mov	x20,x0
	mov	x0,1
	cbz	x20,.out
	ldp	w1,w2,[x20,4]			; argc, envc
	cmp	w1,4
	mov	x0,2
	b.ne	.out
	cmp	w2,2
	mov	x0,3
	b.ne	.out
	add	x21,x20,16			; argv[0] (le chemin), argv[1]
	mov	x0,x21
	bl	suivante
	mov	x21,x0
	adr	x1,a_a
	bl	str_eq
	cbz	x0,.f4
	mov	x0,x21
	bl	suivante
	mov	x21,x0
	adr	x1,s_deux
	bl	str_eq
	cbz	x0,.f5
	mov	x0,x21
	bl	suivante
	mov	x21,x0
	adr	x1,s_guill
	bl	str_eq
	cbz	x0,.f6
	mov	x0,x21
	bl	suivante
	mov	x21,x0
	adr	x1,s_env1
	bl	str_eq
	cbz	x0,.f7
	mov	x0,x21
	bl	suivante
	adr	x1,s_env2
	bl	str_eq
	cbz	x0,.f8
	mov	x0,42
	b	.out
.f4:	mov	x0,4
	b	.out
.f5:	mov	x0,5
	b	.out
.f6:	mov	x0,6
	b	.out
.f7:	mov	x0,7
	b	.out
.f8:	mov	x0,8
.out:	sys	SYS_EXIT

; -x : un seul thread (THREAD_LIST) : code 43, sinon 50 + n
mode_x:	adr	x1,buf
	mov	x0,0
	mov	x2,8
	sys	SYS_THREAD_LIST
	cmp	x0,1
	add	x0,x0,50
	mov	x1,43
	csel	x0,x1,x0,eq
	sys	SYS_EXIT

; -s : SIGUSR2 (ignore avant exec) a soi, puis SIGUSR1 (gere avant : par
; defaut maintenant, fin du process, code -30) ; 44 si on survit
mode_s:	mov	x0,0
	mov	x1,0
	mov	x2,SIGUSR2
	sys	SYS_SIG_SEND
	mov	x0,20
	sys	SYS_SLEEP
	mov	x0,0
	mov	x1,0
	mov	x2,SIGUSR1
	sys	SYS_SIG_SEND
	mov	x0,20
	sys	SYS_SLEEP
	mov	x0,44
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
gestionnaire:					; (jamais appele apres exec)
	mov	x8,SYS_SIG_RETURN
	svc	0

; threads de l'essai 5
dort:	movl	x0,100000
	sys	SYS_SLEEP
	b	dort
lit:	adr	x0,fds				; tube vide, ecrivain ouvert : la
	ldr	x0,[x0]				; requete attend chez vfs
	adr	x1,buf2
	mov	x2,16
	sys	SYS_READ
	b	lit
attend:	adr	x0,mot
	mov	x1,0
	mov	x2,0
	sys	SYS_FUTEX_WAIT
	b	attend

; x0 = mode (« -p »...) : argv = { chemin, mode, 0, 0, 0 }
argv_mode:
	adr	x1,argv
	adr	x2,p_moi
	stp	x2,x0,[x1]
	stp	xzr,xzr,[x1,16]
	str	xzr,[x1,32]
	ret

; x0, x1 = argv[2], argv[3]
argv_deux:
	adr	x2,argv
	stp	x0,x1,[x2,16]
	str	xzr,[x2,32]
	ret

; f_exec(p_moi, argv, 0, 0) -> erreur
exec_moi:
	prologue
	adr	x0,p_moi
	adr	x1,argv
	mov	x2,0
	mov	x3,0
	bl	f_exec
	epilogue

; prepare ipcm pour une reception de 64 o dans buf
ipcm_recevoir:
	adr	x0,iov
	adr	x1,buf
	mov	x2,64
	stp	x1,x2,[x0]
	adr	x1,ipcm
	str	x0,[x1,IPCM_IOV]
	mov	w2,1
	str	w2,[x1,IPCM_IOVCNT]
	str	wzr,[x1,IPCM_NH]
	stp	xzr,xzr,[x1,16]
	ret

; x0 = tours : fork, le fils fait exec « -z » (code 7), wait -> x0 = fork
; refuses, x1 = codes faux
tours_exec:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,0
	mov	x21,0
	cbz	x19,.fin
.t:	fork
	cbz	x0,.fils
	tbnz	x0,63,.err
	sys	SYS_WAIT
	cmp	x0,7
	cinc	x21,x21,ne
	b	.n
.err:	add	x20,x20,1
	mov	x0,10
	sys	SYS_SLEEP
.n:	mov	x0,1				; (les avis de fin aux services)
	sys	SYS_SLEEP
	subs	x19,x19,1
	b.ne	.t
.fin:	mov	x0,x20
	mov	x1,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.fils:	bl	exec_moi
	mov	x0,90
	sys	SYS_EXIT

; -> x0 = pages libres du systeme (VM_STATS)
pages_libres:
	prologue
	mov	x0,0
	adr	x1,stats
	sys	SYS_VM_STATS
	adr	x1,stats
	ldr	x0,[x1,VS_FREE_LOW]
	ldr	x1,[x1,VS_FREE_HIGH]
	add	x0,x0,x1
	epilogue

; x0 = valeur signee : en decimal
print_sdec:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	tbz	x19,63,.pos
	adr	x0,m_moins
	bl	puts
	neg	x19,x19
.pos:	mov	x0,x19
	bl	print_dec
	ldr	x19,[sp],16
	epilogue

; x0 = chaine (espaces en tete sautes) -> x0 = nombre decimal (0 si aucun),
; x1 = apres lui
lire_dec:
.sp:	ldrb	w2,[x0]
	cmp	w2,' '
	b.ne	.go
	add	x0,x0,1
	b	.sp
.go:	mov	x1,0
	mov	x3,10
.c:	ldrb	w2,[x0]
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.f
	madd	x1,x1,x3,x2
	add	x0,x0,1
	b	.c
.f:	mov	x2,x0
	mov	x0,x1
	mov	x1,x2
	ret

; x0 = chaine -> x0 = la suivante (apres son 0)
suivante:
.c:	ldrb	w1,[x0],1
	cbnz	w1,.c
	ret

reussite:
	prologue
	str	x0,[sp,-16]!
	adr	x0,m_pass1
	bl	puts
	ldr	x0,[sp],16
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x0,n_pass
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	epilogue

echec:
	prologue
	str	x0,[sp,-16]!
	adr	x0,m_fail1
	bl	puts
	ldr	x0,[sp],16
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x0,n_fail
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	epilogue

p_moi		db 'SD0:/bin/exectest',0
p_absent	db 'SD0:/bin/exectest_absent',0
p_texte		db 'SD0:/etc/startup.txt',0
a_p		db '-p',0
a_t		db '-t',0
a_a		db '-a',0
a_x		db '-x',0
a_s		db '-s',0
a_z		db '-z',0
s_deux		db 'deux mots',0
s_guill		db 'x"y',0
s_env1		db 'EXECTEST=oui',0
s_env2		db 'B=2',0
s_tube		db 'tube',0
s_tube_n	= $ - s_tube - 1
s_canal		db 'canal',0
s_canal_n	= $ - s_canal - 1
m_debut		db 'exectest : SYS_EXEC (meme pid, nouvel espace)',10,0
m_tours		db 'exectest : ',0
m_tours2	db ' fork + exec + wait en ',0
m_tours3	db ' ms ; pages libres perdues : ',0
m_tours4	db ' (1re moitie), ',0
m_tours5	db ' (2e moitie)',10,0
m_moins		db '-',0
m_pass1		db 'PASS ',0
m_fail1		db 'FAIL ',0
m_fin		db 'exectest : ',0
m_pass		db ' PASS, ',0
m_fail		db ' FAIL',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 8
tours		dd 0
fds		dd 0, 0
chan		dw 0, 0
mot		dw 0, 0
iov		dd 0, 0
ipcm		rb IPCM_SIZE
argv		rd 6
envp		rd 4
n_pass		dd 0
n_fail		dd 0
stats		rb 64
s_n1		rb 24
s_n2		rb 24
buf		rb 1024
buf2		rb 64
		align 16
pile1		rb 16384
pile2		rb 16384
pile3		rb 16384
