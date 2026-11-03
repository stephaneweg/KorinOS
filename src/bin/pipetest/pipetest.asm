; pipetest - tubes et lancement des programmes (docs/FICHIERS.md, etape 4) :
; tubes de vfs, sortie d'un fils lue pendant qu'il tourne, code de sortie
; par wait, entree standard, petit-fils qui herite de la sortie, erreurs.
;   pipetest            les essais (PASS / FAIL, resume a la fin)
;   pipetest child      (fils) 5 lignes, 200 ms entre elles, code 7
;   pipetest cat        (fils) recopie l'entree standard sur la sortie, code 3
;   pipetest parent     (fils) lance « pipetest child » (sortie heritee), code 5
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

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

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0				; (arguments)
	bl	kits_open
	cbz	x19,essais
	mov	x0,x19
	adr	x1,a_child
	bl	str_eq
	cbnz	x0,mode_child
	mov	x0,x19
	adr	x1,a_cat
	bl	str_eq
	cbnz	x0,mode_cat
	mov	x0,x19
	adr	x1,a_parent
	bl	str_eq
	cbnz	x0,mode_parent

essais:
	adr	x0,m_debut
	bl	puts
	; --- 1. spawn_pipe de hello (comme avant) ---------------------------
	adr	x0,p_hello
	adr	x1,a_test
	adr	x2,fd
	sys	SYS_SPAWN_PIPE
	adr	x1,pid
	str	x0,[x1]
	cmp	x0,0
	verifier gt, 'spawn_pipe hello'
	adr	x0,fd
	ldr	x0,[x0]
	cmp	x0,64
	verifier ge, 'descripteur du tube dans AppKit (>= 64)'
	bl	lire_tout			; -> x0 = octets, x1 = 1er, x2 = fin (ms)
	cmp	x0,0
	verifier gt, 'sortie de hello lue'
	adr	x0,pid
	ldr	x0,[x0]
	sys	SYS_WAIT
	cmp	x0,0
	verifier eq, 'hello : code 0 par wait'

	; --- 2. sortie lue pendant que le fils tourne, code de sortie ------------
	adr	x0,p_self
	adr	x1,a_child
	adr	x2,fd
	sys	SYS_SPAWN_PIPE
	adr	x1,pid
	str	x0,[x1]
	cmp	x0,0
	verifier gt, 'spawn_pipe pipetest child'
	bl	lire_tout
	mov	x20,x0
	sub	x21,x2,x1			; fin - premiere donnee
	adr	x0,m_lu
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_octets
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	cmp	x20,5*8
	verifier eq, '5 lignes recues'
	cmp	x21,500
	verifier ge, 'sortie en continu (1re ligne >= 500 ms avant la fin)'
	adr	x0,buf
	adr	x1,s_ligne
	bl	commence
	cmp	x0,1
	verifier eq, 'contenu : ligne 1'
	adr	x0,pid
	ldr	x0,[x0]
	sys	SYS_WAIT
	cmp	x0,7
	verifier eq, 'code de sortie 7 par wait'

	; --- 3. f_pipe, lecture bloquante, f_spawn_stdio ----------------------------
	adr	x0,fds
	bl	f_pipe
	cmp	x0,0
	verifier eq, 'f_pipe'
	adr	x0,fds				; lire le bout d'ecriture : E_BADF
	ldr	x0,[x0,8]
	adr	x1,buf
	mov	x2,4
	sys	SYS_READ
	cmn	x0,9
	verifier eq, 'read du bout d''ecriture : E_BADF'
	adr	x0,fds				; write sur le bout de lecture : E_BADF
	ldr	x0,[x0]
	adr	x1,buf
	mov	x2,4
	sys	SYS_WRITE
	cmn	x0,9
	verifier eq, 'write du bout de lecture : E_BADF'
	adr	x0,fds				; sortie et erreur du fils : le tube
	ldr	x1,[x0,8]
	adr	x2,sio
	mov	x3,-1
	str	x3,[x2]
	stp	x1,x1,[x2,8]
	adr	x0,p_self
	adr	x1,a_child
	bl	f_spawn_stdio
	adr	x1,pid
	str	x0,[x1]
	cmp	x0,0
	verifier gt, 'f_spawn_stdio (sortie dans le tube)'
	adr	x0,fds				; le pere ferme son bout d'ecriture
	ldr	x0,[x0,8]
	sys	SYS_CLOSE
	mov	x20,0				; lecture bloquante jusqu'a la fin
.b:	adr	x0,fds
	ldr	x0,[x0]
	adr	x1,buf
	add	x1,x1,x20
	mov	x2,100
	sys	SYS_READ
	cmp	x0,0
	b.le	.bf
	add	x20,x20,x0
	b	.b
.bf:	mov	x21,x0
	cmp	x21,0
	verifier eq, 'lecture bloquante : fin (0), pas E_AGAIN'
	cmp	x20,5*8
	verifier eq, 'lecture bloquante : 5 lignes'
	adr	x0,fds
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,pid
	ldr	x0,[x0]
	sys	SYS_WAIT
	cmp	x0,7
	verifier eq, 'code 7'

	; --- 4. entree standard d'un fils (cat) ----------------------------------
	adr	x0,fds				; vers le fils
	bl	f_pipe
	cmp	x0,0
	verifier eq, 'f_pipe (entree)'
	adr	x0,fds2				; depuis le fils
	bl	f_pipe
	cmp	x0,0
	verifier eq, 'f_pipe (sortie)'
	adr	x2,sio
	adr	x0,fds
	ldr	x1,[x0]
	str	x1,[x2]				; entree : lecture du 1er tube
	adr	x0,fds2
	ldr	x1,[x0,8]
	stp	x1,x1,[x2,8]			; sortie, erreur : 2e tube
	adr	x0,p_self
	adr	x1,a_cat
	bl	f_spawn_stdio
	adr	x1,pid
	str	x0,[x1]
	cmp	x0,0
	verifier gt, 'f_spawn_stdio (cat)'
	adr	x0,fds				; nos bouts du fils : fermes
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,fds2
	ldr	x0,[x0,8]
	sys	SYS_CLOSE
	adr	x0,fds
	ldr	x0,[x0,8]
	adr	x1,s_cat
	mov	x2,s_cat_n
	sys	SYS_WRITE
	cmp	x0,s_cat_n
	verifier eq, 'write vers l''entree du fils'
	adr	x0,fds				; fin de son entree
	ldr	x0,[x0,8]
	sys	SYS_CLOSE
	mov	x20,0
.c:	adr	x0,fds2
	ldr	x0,[x0]
	adr	x1,buf
	add	x1,x1,x20
	mov	x2,100
	sys	SYS_READ
	cmp	x0,0
	b.le	.cf
	add	x20,x20,x0
	b	.c
.cf:	cmp	x20,s_cat_n
	verifier eq, 'cat : meme longueur'
	adr	x0,buf
	strb	wzr,[x0,x20]
	adr	x1,s_cat
	bl	str_eq
	cmp	x0,1
	verifier eq, 'cat : meme contenu'
	adr	x0,fds2
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,pid
	ldr	x0,[x0]
	sys	SYS_WAIT
	cmp	x0,3
	verifier eq, 'cat : code 3'

	; --- 5. petit-fils : sortie heritee -------------------------------------
	adr	x0,p_self
	adr	x1,a_parent
	adr	x2,fd
	sys	SYS_SPAWN_PIPE
	adr	x1,pid
	str	x0,[x1]
	cmp	x0,0
	verifier gt, 'spawn_pipe pipetest parent'
	bl	lire_tout
	cmp	x0,5*8
	verifier eq, 'petit-fils : ses 5 lignes dans le tube du grand-pere'
	adr	x0,pid
	ldr	x0,[x0]
	sys	SYS_WAIT
	cmp	x0,5
	verifier eq, 'parent : code 5'

	; --- 6. ecrire sans lecteur : E_PIPE ; tube vide sans attente -----------
	adr	x0,fds
	bl	f_pipe
	adr	x0,fds
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,fds
	ldr	x0,[x0,8]
	adr	x1,s_cat
	mov	x2,4
	sys	SYS_WRITE
	cmn	x0,32
	verifier eq, 'write sans lecteur : E_PIPE'
	adr	x0,fds
	ldr	x0,[x0,8]
	sys	SYS_CLOSE
	cmp	x0,0
	verifier eq, 'close'
	adr	x0,fds
	ldr	x0,[x0,8]
	sys	SYS_CLOSE
	cmn	x0,9
	verifier eq, 'close d''un descripteur ferme : E_BADF'
	adr	x0,p_self			; spawn_pipe d'un fils qui ne dit rien
	adr	x1,a_parent			; tout de suite : E_AGAIN
	adr	x2,fd
	sys	SYS_SPAWN_PIPE
	adr	x1,pid
	str	x0,[x1]
	adr	x0,fd
	ldr	x0,[x0]
	adr	x1,buf
	mov	x2,10
	sys	SYS_READ
	cmn	x0,11
	verifier eq, 'tube vide (O_NONBLOCK) : E_AGAIN'
	bl	lire_tout
	adr	x0,pid
	ldr	x0,[x0]
	sys	SYS_WAIT
	adr	x0,p_nobody			; programme absent
	mov	x1,0
	sys	SYS_SPAWN
	cmn	x0,2
	verifier eq, 'spawn d''un programme absent : E_NOENT'

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

; lit fd (O_NONBLOCK) jusqu'a la fin, dans buf -> x0 = octets, x1 = ms de la
; premiere donnee, x2 = ms de la fin
lire_tout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,0
	mov	x21,0
.rd:	adr	x0,fd
	ldr	x0,[x0]
	adr	x1,buf
	add	x1,x1,x19
	mov	x2,200
	sys	SYS_READ
	cmn	x0,11
	b.eq	.att
	cmp	x0,0
	b.le	.eof
	add	x19,x19,x0
	cbnz	x21,.rd
	sys	SYS_TIME_MS
	mov	x21,x0
	b	.rd
.att:	mov	x0,10
	sys	SYS_SLEEP
	b	.rd
.eof:	adr	x0,fd
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,buf
	strb	wzr,[x0,x19]
	sys	SYS_TIME_MS
	mov	x2,x0
	mov	x1,x21
	mov	x0,x19
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine, x1 = prefixe -> x0 = 1 si x0 commence par x1
commence:
.c:	ldrb	w2,[x1],1
	cbz	w2,.oui
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
	ret
.oui:	mov	x0,1
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

; (fils) 5 lignes de 8 octets, 200 ms entre elles, code 7
mode_child:
	mov	x19,1
.l:	adr	x0,buf
	adr	x1,s_ligne
	bl	str_cat
	add	w1,w19,'0'
	strb	w1,[x0],1
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
	adr	x0,buf
	bl	puts
	mov	x0,200
	sys	SYS_SLEEP
	add	x19,x19,1
	cmp	x19,5
	b.ls	.l
	mov	x0,7
	sys	SYS_EXIT

; (fils) entree -> sortie jusqu'a la fin, code 3
mode_cat:
.r:	mov	x0,0
	adr	x1,buf
	mov	x2,100
	sys	SYS_READ
	cmp	x0,0
	b.le	.f
	mov	x2,x0
	mov	x0,1
	adr	x1,buf
	sys	SYS_WRITE
	b	.r
.f:	mov	x0,3
	sys	SYS_EXIT

; (fils) lance « pipetest child » (sortie heritee), l'attend, code 5
mode_parent:
	mov	x0,300				; (rien pendant 300 ms : E_AGAIN)
	sys	SYS_SLEEP
	adr	x0,p_self
	adr	x1,a_child
	sys	SYS_SPAWN
	tbnz	x0,63,.f
	sys	SYS_WAIT
.f:	mov	x0,5
	sys	SYS_EXIT

p_hello		db 'SD0:/bin/hello',0
p_self		db 'SD0:/bin/pipetest',0
p_nobody	db 'pas_de_programme',0
a_test		db 'arguments de test',0
a_child		db 'child',0
a_cat		db 'cat',0
a_parent	db 'parent',0
s_ligne		db 'ligne ',0
s_cat		db 'Bonjour, le tube !',10,'Deuxieme ligne.',10,0
s_cat_n		= $ - s_cat - 1
m_debut		db 'pipetest : tubes de vfs (etape 4)',10,0
m_lu		db 'pipetest : ',0
m_octets	db ' octets, ',0
m_ms		db ' ms entre la 1re ligne et la fin',10,0
m_pass1		db 'PASS ',0
m_fail1		db 'FAIL ',0
m_fin		db 'pipetest : ',0
m_pass		db ' PASS, ',0
m_fail		db ' FAIL',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 8
fd		dd 0
pid		dd 0
fds		dd 0, 0
fds2		dd 0, 0
sio		dd 0, 0, 0
n_pass		dd 0
n_fail		dd 0
buf		rb 1024
