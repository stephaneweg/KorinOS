; forktest - SYS_FORK (docs/ABI.md, 0x6A ; etude docs/TERMINAL-ET-FORK.md) :
; valeurs rendues, memoire separee, registres (FP, TPIDR_EL0) du fils, tube
; ouvert avant le fork entre pere et fils (fin quand les deux ecrivains
; ferment), wait, deuxieme generation, fuites de pages sur N fork + exit.
;   forktest            les essais (PASS / FAIL, resume a la fin, code = FAIL)
;   forktest <n>        idem, n fork + exit dans la boucle de fuites (1000)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

BOUCLE_DEF	= 1000

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

; fork brut : x0 = pid (pere), 0 (fils), < 0 erreur
macro fork {
	mov	x8,SYS_FORK
	svc	0
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0				; (arguments : nombre de tours)
	bl	kits_open
	mov	x0,BOUCLE_DEF
	cbz	x19,.n
	mov	x0,x19
	bl	lire_dec
	cmp	x0,0
	mov	x1,BOUCLE_DEF
	csel	x0,x1,x0,le
.n:	adr	x1,tours
	str	x0,[x1]
	adr	x0,m_debut
	bl	puts

	; --- 0. droits : un programme de SD0:/sys est refuse ------------------
	sys	SYS_PROC_FLAGS
	tst	x0,PROC_RIGHTS
	b.eq	.sans_droits
	fork
	cmn	x0,1
	verifier eq, 'fork avec les droits de SD0:/sys : E_PERM'
	b	.e1
.sans_droits:
	adr	x0,m_droits
	bl	puts

	; --- 1. valeurs rendues, registres du fils ---------------------------
.e1:	movl	x0,0x4048F5C3			; FP : d9 du fils = celui du pere
	fmov	s9,w0
	mrs	x20,tpidr_el0			; TPIDR_EL0 : celui du pere
	movl	x0,0x7E57AB1E
	msr	tpidr_el0,x0
	movl	x21,0x1122334455667788		; registre garde
	fork
	cbz	x0,.fils1
	msr	tpidr_el0,x20			; (pere : le sien)
	mov	x22,x0
	cmp	x22,0
	verifier gt, 'fork rend le pid du fils au pere'
	movl	x0,0x1122334455667788
	cmp	x21,x0
	verifier eq, 'pere : registres gardes'
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,42
	verifier eq, 'fils : fork rend 0, x21, d9 et TPIDR_EL0 du pere (code 42 par wait)'
	b	.e2
.fils1:	mov	x1,42				; 42 : tout est bon, sinon 1, 2, 3
	movl	x2,0x1122334455667788
	cmp	x21,x2
	mov	x2,1
	csel	x1,x2,x1,ne
	fmov	w2,s9
	movl	x3,0x4048F5C3
	cmp	w2,w3
	mov	x2,2
	csel	x1,x2,x1,ne
	mrs	x2,tpidr_el0
	movl	x3,0x7E57AB1E
	cmp	x2,x3
	mov	x2,3
	csel	x1,x2,x1,ne
	mov	x0,x1
	sys	SYS_EXIT

	; --- 2. memoire separee ------------------------------------------------
.e2:	adr	x0,marque
	mov	x1,1
	str	x1,[x0]
	adr	x0,gros				; une page du .bss deja touchee
	mov	x1,0x55
	strb	w1,[x0,4000]
	fork
	cbz	x0,.fils2
	mov	x22,x0
	sys	SYS_WAIT
	mov	x23,x0
	cmp	x23,2
	verifier eq, 'fils : sa copie de la memoire (code 2)'
	adr	x0,marque
	ldr	x0,[x0]
	cmp	x0,1
	verifier eq, 'pere : sa memoire inchangee apres l''ecriture du fils'
	adr	x0,gros
	ldrb	w0,[x0,4000]
	cmp	w0,0x55
	verifier eq, 'pere : page du .bss inchangee'
	b	.e3
.fils2:	adr	x0,marque			; lit 1, ecrit 2, relit
	ldr	x1,[x0]
	cmp	x1,1
	b.ne	.f2bad
	mov	x1,2
	str	x1,[x0]
	adr	x0,gros
	ldrb	w1,[x0,4000]
	cmp	w1,0x55
	b.ne	.f2bad
	mov	w1,0xAA
	strb	w1,[x0,4000]
	adr	x0,marque
	ldr	x0,[x0]
	sys	SYS_EXIT
.f2bad:	mov	x0,99
	sys	SYS_EXIT

	; --- 3. tubes ouverts avant le fork ------------------------------------
	; fds : du fils vers le pere ; fds2 : du pere vers le fils
.e3:	adr	x0,fds
	bl	f_pipe
	mov	x20,x0
	adr	x0,fds2
	bl	f_pipe
	orr	x0,x0,x20
	cmp	x0,0
	verifier eq, 'deux f_pipe avant le fork'
	fork
	cbz	x0,.fils3
	mov	x22,x0
	cmp	x22,0
	verifier gt, 'fork avec des tubes ouverts'
	adr	x0,fds				; pere : son ecrivain de fds ferme
	ldr	x0,[x0,8]			; tout de suite (la fin attend celui
	sys	SYS_CLOSE			; du fils)
	adr	x0,fds2				; et son lecteur de fds2
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,fds2				; vers le fils
	ldr	x0,[x0,8]
	adr	x1,s_pere
	mov	x2,s_pere_n
	sys	SYS_WRITE
	cmp	x0,s_pere_n
	verifier eq, 'pere : ecriture dans le tube vers le fils'
	adr	x0,fds2
	ldr	x0,[x0,8]
	sys	SYS_CLOSE
	mov	x20,0				; lecture jusqu'a la fin
.r3:	adr	x0,fds
	ldr	x0,[x0]
	adr	x1,buf
	add	x1,x1,x20
	mov	x2,100
	sys	SYS_READ
	cmp	x0,0
	b.le	.r3f
	add	x20,x20,x0
	cmp	x20,900
	b.lo	.r3
.r3f:	mov	x21,x0
	cmp	x21,0
	verifier eq, 'pere : fin du tube (0) quand le fils ferme'
	cmp	x20,s_fils_n
	verifier eq, 'pere : octets du fils (pas de fin a sa propre fermeture)'
	adr	x0,buf
	adr	x1,s_fils
	bl	commence
	cmp	x0,1
	verifier eq, 'pere : texte du fils'
	adr	x0,fds
	ldr	x0,[x0]
	sys	SYS_CLOSE
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,3
	verifier eq, 'fils : texte du pere lu jusqu''a la fin, ecriture (code 3)'
	b	.e4
.fils3:	adr	x0,fds				; fils : ses bouts inutiles fermes
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,fds2
	ldr	x0,[x0,8]
	sys	SYS_CLOSE
	mov	x21,0				; le texte du pere, jusqu'a la fin
.r3c:	adr	x0,fds2
	ldr	x0,[x0]
	adr	x1,buf
	add	x1,x1,x21
	mov	x2,100
	sys	SYS_READ
	cmp	x0,0
	b.le	.r3cf
	add	x21,x21,x0
	cmp	x21,900
	b.lo	.r3c
.r3cf:	mov	x23,x0
	adr	x0,fds2
	ldr	x0,[x0]
	sys	SYS_CLOSE
	mov	x0,300				; (le pere attend deja)
	sys	SYS_SLEEP
	adr	x0,fds
	ldr	x0,[x0,8]
	adr	x1,s_fils
	mov	x2,s_fils_n
	sys	SYS_WRITE
	adr	x0,fds
	ldr	x0,[x0,8]
	sys	SYS_CLOSE
	mov	x0,3
	mov	x1,98				; 98 : pas le texte, 97 : pas de fin
	cmp	x21,s_pere_n
	csel	x0,x1,x0,ne
	mov	x1,97
	cmp	x23,0
	csel	x0,x1,x0,ne
	sys	SYS_EXIT

	; --- 4. deuxieme generation -------------------------------------------
.e4:	fork
	cbz	x0,.fils4
	sys	SYS_WAIT
	cmp	x0,10
	verifier eq, 'petit-fils : fork du fils, wait dans le fils (code 9 + 1)'
	b	.e4b
.fils4:	fork
	cbz	x0,.pfils4
	tbnz	x0,63,.f4bad
	sys	SYS_WAIT
	add	x0,x0,1
	sys	SYS_EXIT
.f4bad:	mov	x0,97
	sys	SYS_EXIT
.pfils4:
	mov	x0,9
	sys	SYS_EXIT

	; --- 4 bis. poignees d'IPC : memes numeros dans le fils ------------------
.e4b:	mov	x0,IPC_SOCK_SEQPACKET
	adr	x1,chan
	sys	SYS_CHAN_CREATE
	cmp	x0,0
	verifier eq, 'chan_create avant le fork'
	fork
	cbz	x0,.fils4b
	mov	x22,x0
	adr	x0,iov				; reception sur le bout 0
	adr	x1,buf
	mov	x2,64
	stp	x1,x2,[x0]
	adr	x1,ipcm
	str	x0,[x1,IPCM_IOV]
	mov	w2,1
	str	w2,[x1,IPCM_IOVCNT]
	str	wzr,[x1,IPCM_NH]
	stp	xzr,xzr,[x1,16]
	adr	x0,chan
	ldr	w0,[x0]
	mov	x2,0
	mov	x3,5000
	sys	SYS_CHAN_RECV
	cmp	x0,s_canal_n
	verifier eq, 'pere : message du fils par le canal herite'
	adr	x0,buf
	adr	x1,s_canal
	bl	commence
	cmp	x0,1
	verifier eq, 'pere : texte du message'
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,6
	verifier eq, 'fils : envoi par la poignee de meme numero (code 6)'
	adr	x0,chan
	ldr	w0,[x0]
	sys	SYS_HANDLE_CLOSE
	adr	x0,chan
	ldr	w0,[x0,4]
	sys	SYS_HANDLE_CLOSE
	b	.e4t
.fils4b:
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
	adr	x0,chan
	ldr	w0,[x0,4]
	mov	x2,0
	mov	x3,5000
	sys	SYS_CHAN_SEND
	cmp	x0,s_canal_n
	mov	x0,6
	mov	x1,96
	csel	x0,x1,x0,ne
	sys	SYS_EXIT

	; --- 4 ter. fichier ouvert avant le fork : position partagee ------------
	; (sys SYS_FORK : f_fork d'AppKit ; RAM0:, puis SD0:)
.e4t:	adr	x0,s_fic_ram
	bl	fichier_partage
	cmp	x0,1
	verifier eq, 'RAM0: : le fils ecrit a la position du pere, le pere apres lui (ABC)'
	adr	x0,s_fic_sd
	bl	fichier_partage
	cmp	x0,1
	verifier eq, 'SD0: : le fils ecrit a la position du pere, le pere apres lui (ABC)'

	; --- 5. fuites : N fork + exit, en deux moities ------------------------
	; Les pages que les services gardent au premier passage (fiches, files,
	; journal) se voient dans la premiere moitie ; la seconde doit etre a
	; plat : une fuite d'une page par fork y perdrait N / 2 pages.
.e5:	mov	x0,10				; (premiers tours : caches des services)
	bl	tours_fork
	mov	x0,1000
	sys	SYS_SLEEP
	bl	pages_libres
	mov	x24,x0				; P0
	sys	SYS_TIME_MS
	mov	x25,x0
	adr	x0,tours
	ldr	x0,[x0]
	lsr	x0,x0,1
	bl	tours_fork
	mov	x21,x0				; refus
	mov	x22,x1				; codes faux
	mov	x0,1000
	sys	SYS_SLEEP
	bl	pages_libres
	mov	x26,x0				; P1
	adr	x0,tours
	ldr	x0,[x0]
	lsr	x0,x0,1
	bl	tours_fork
	add	x21,x21,x0
	add	x22,x22,x1
	sys	SYS_TIME_MS
	sub	x25,x0,x25
	cmp	x21,0
	verifier eq, 'boucle : aucun fork refuse'
	cmp	x22,0
	verifier eq, 'boucle : chaque fils rend son code (5)'
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
	adr	x0,tours			; moins d'une page pour 8 fork
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

.fils5:	mov	x0,5
	sys	SYS_EXIT

; x0 = tours : fork, le fils sort (code 5), wait -> x0 = fork refuses,
; x1 = codes faux
; x0 = chemin : ecrit A, fork (sys SYS_FORK), le fils ecrit B, le pere attend
; puis ecrit C ; relu depuis 0 -> x0 = 1 si « ABC » (fichier efface ensuite)
fichier_partage:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x1,O_READ or O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	mov	x21,0
	tbnz	x0,63,.out
	mov	x20,x0
	adr	x1,s_abc
	mov	x2,1
	sys	SYS_WRITE
	sys	SYS_FORK
	cbz	x0,.fils
	tbnz	x0,63,.ferme
	sys	SYS_WAIT
	mov	x22,x0
	mov	x0,x20
	adr	x1,s_abc+2
	mov	x2,1
	sys	SYS_WRITE
	mov	x0,x20
	mov	x1,0
	mov	x2,0
	sys	SYS_SEEK
	adr	x1,buf
	str	xzr,[x1]
	mov	x0,x20
	mov	x2,8
	sys	SYS_READ
	cmp	x0,3
	b.ne	.ferme
	cmp	x22,5
	b.ne	.ferme
	adr	x0,buf
	adr	x1,s_abc
	bl	commence
	mov	x21,x0
.ferme:	mov	x0,x20
	sys	SYS_CLOSE
	mov	x0,x19
	sys	SYS_UNLINK
.out:	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.fils:	mov	x0,x20				; fils : B a la position du pere
	adr	x1,s_abc+1
	mov	x2,1
	sys	SYS_WRITE
	cmp	x0,1
	mov	x0,5
	mov	x1,95
	csel	x0,x1,x0,ne
	sys	SYS_EXIT

tours_fork:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,0
	mov	x21,0
	cbz	x19,.fin
.t:	fork
	cbz	x0,start.fils5
	tbnz	x0,63,.err
	sys	SYS_WAIT
	cmp	x0,5
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

; x0 = chaine -> x0 = nombre decimal en tete (0 si aucun)
lire_dec:
	mov	x1,0
	mov	x3,10
.c:	ldrb	w2,[x0],1
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.f
	madd	x1,x1,x3,x2
	b	.c
.f:	mov	x0,x1
	ret

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

s_pere		db 'du pere ; ',0
s_pere_n	= $ - s_pere - 1
s_fils		db 'du fils.',10,0
s_fils_n	= $ - s_fils - 1
s_canal		db 'canal',0
s_fic_ram	db 'RAM0:/forktest.tmp',0
s_fic_sd	db 'SD0:/forktest.tmp',0
s_abc		db 'ABC',0
s_canal_n	= $ - s_canal - 1
m_debut		db 'forktest : SYS_FORK (copie sur ecriture)',10,0
m_droits	db 'forktest : sans droits de SD0:/sys (refus E_PERM non essaye)',10,0
m_tours		db 'forktest : ',0
m_tours2	db ' fork + exit en ',0
m_tours3	db ' ms ; pages libres perdues : ',0
m_tours4	db ' (1re moitie), ',0
m_tours5	db ' (2e moitie)',10,0
m_moins		db '-',0
m_pass1		db 'PASS ',0
m_fail1		db 'FAIL ',0
m_fin		db 'forktest : ',0
m_pass		db ' PASS, ',0
m_fail		db ' FAIL',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 8
tours		dd 0
marque		dd 0
fds		dd 0, 0
fds2		dd 0, 0
chan		dw 0, 0
iov		dd 0, 0
ipcm		rb IPCM_SIZE
n_pass		dd 0
n_fail		dd 0
stats		rb 64
buf		rb 1024
gros		rb 8192
