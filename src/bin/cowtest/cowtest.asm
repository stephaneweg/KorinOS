; cowtest - fork en copie sur ecriture (docs/MICRO-NOYAU.md, « Memoire » ;
; etape 9 de docs/TERMINAL-ET-FORK.md) : memoire separee dans les deux sens
; (le fils ecrit, le pere ecrit, les deux), pages en lecture seule et
; PTE_COW des deux cotes, derniere reference : rendue inscriptible sans
; copie (VS_COWS inchange), fork d'un process de 16 Mo (duree), fork d'un
; fork, sbrk apres fork, mprotect d'une page partagee, exec apres fork, pere
; a deux threads qui ecrit pendant les fork, fuites de pages (fork + exit et
; fork + exec + wait, en deux moities).
;   cowtest             les essais (PASS / FAIL, resume a la fin, code = FAIL)
;   cowtest <n>         idem, n fork + exit dans la boucle de fuites (200)
;   cowtest -z          (apres exec) sortie, code 7
;   cowtest -m          ecrit les pages libres du systeme sur la sortie
;                       standard (essais de fuite depuis bash)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

BOUCLE_DEF	= 200
NP		= 4096				; pages de la region (16 Mo)
PI_FORKED_T	= 128				; (VM_PAGEINFO) partagee par fork
TEMPS_N		= 20				; fork chronometres

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

; r = adresse de la page i de la region
macro page_va r, i {
	adr	r,region
	ldr	r,[r]
	add	r,r,i,lsl 12
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0				; (arguments)
	bl	kits_open
	cbz	x19,.n0
	ldrb	w0,[x19]
	cmp	w0,'-'
	b.ne	.nb
	ldrb	w0,[x19,1]
	cmp	w0,'m'
	b.eq	libres
	mov	x0,7				; -z : apres exec, code 7
	sys	SYS_EXIT
.nb:	mov	x0,x19
	bl	lire_dec
	cmp	x0,0
	b.gt	.n
.n0:	mov	x0,BOUCLE_DEF
.n:	adr	x1,tours
	str	x0,[x1]
	adr	x0,m_debut
	bl	puts

	; --- region de 16 Mo, peuplee, page i : i ---------------------------
	mov	x0,0
	movl	x1,NP*4096
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,VM_POPULATE
	sys	SYS_VM_MAP
	adr	x1,region
	str	x0,[x1]
	cmp	x0,0
	verifier gt, 'region de 16 Mo (VM_MAP, VM_POPULATE)'
	bl	remplir

	; --- 1. le fils ecrit ---------------------------------------------------
	fork
	cbnz	x0,.p1
	mov	x0,2
	bl	pageinfo_page
	mov	x20,x0
	mov	x0,1
	bl	lire_page
	cmp	x0,1
	b.ne	.f1a
	mov	x0,NP-1
	bl	lire_page
	cmp	x0,NP-1
	b.ne	.f1a
	mov	x0,0
	movl	x1,0xC0DE
	bl	ecrire_page
	mov	x0,7
	movl	x1,0xC0DE7
	bl	ecrire_page
	mov	x0,0
	bl	lire_page
	movl	x1,0xC0DE
	cmp	x0,x1
	b.ne	.f1b
	mov	x0,2				; 2, ou 12 : pas en copie sur ecriture
	tst	x20,PI_COW
	b.eq	.f1c
	tst	x20,PI_WRITE
	b.ne	.f1c
	sys	SYS_EXIT
.f1a:	mov	x0,90
	sys	SYS_EXIT
.f1b:	mov	x0,91
	sys	SYS_EXIT
.f1c:	mov	x0,12
	sys	SYS_EXIT
.p1:	sys	SYS_WAIT
	mov	x22,x0
	sub	x0,x22,90			; 90, 91 : lecture ou relecture fausse
	cmp	x0,1
	verifier hi, 'fils : memoire du pere lue, sa propre ecriture relue'
	cmp	x22,2
	verifier eq, 'fils : page partagee en lecture seule, PTE_COW (code 2)'
	mov	x0,0
	bl	lire_page
	cmp	x0,0
	verifier eq, 'pere : page 0 intacte apres l''ecriture du fils'
	mov	x0,7
	bl	lire_page
	cmp	x0,7
	verifier eq, 'pere : page 7 intacte apres l''ecriture du fils'

	; --- 2. le pere ecrit, le fils lit apres ------------------------------
	fork
	cbnz	x0,.p2
	mov	x0,100
	sys	SYS_SLEEP
	mov	x0,5
	bl	lire_page
	cmp	x0,5
	mov	x0,3
	mov	x1,92
	csel	x0,x1,x0,ne
	sys	SYS_EXIT
.p2:	mov	x22,x0
	mov	x0,5
	movl	x1,0xBAD
	bl	ecrire_page
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,3
	verifier eq, 'fils : page 5 d''avant le fork, le pere l''a ecrite (code 3)'
	mov	x0,5
	bl	lire_page
	movl	x1,0xBAD
	cmp	x0,x1
	verifier eq, 'pere : sa propre ecriture'
	mov	x0,5
	mov	x1,5
	bl	ecrire_page

	; --- 3. les deux ecrivent la meme page -------------------------------
	fork
	cbnz	x0,.p3
	mov	x0,6
	mov	x1,0xC6
	bl	ecrire_page
	mov	x0,50
	sys	SYS_SLEEP
	mov	x0,6
	bl	lire_page
	cmp	x0,0xC6
	mov	x0,4
	mov	x1,93
	csel	x0,x1,x0,ne
	sys	SYS_EXIT
.p3:	mov	x22,x0
	mov	x0,6
	mov	x1,0xB6
	bl	ecrire_page
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,4
	verifier eq, 'les deux ecrivent la page 6 : le fils garde la sienne (code 4)'
	mov	x0,6
	bl	lire_page
	cmp	x0,0xB6
	verifier eq, 'les deux ecrivent la page 6 : le pere garde la sienne'
	mov	x0,6
	mov	x1,6
	bl	ecrire_page

	; --- 4. compteurs : copie tant que le fils vit, sans copie ensuite ----
	fork
	cbnz	x0,.p4
	mov	x0,400
	sys	SYS_SLEEP
	mov	x0,0				; le fils relit ce que le pere ecrit
	mov	x1,256
	bl	verifier_pages
	mov	x1,5
	mov	x2,94
	cmp	x0,0
	csel	x0,x1,x2,eq
	sys	SYS_EXIT
.p4:	mov	x22,x0
	mov	x0,10
	bl	pageinfo_page
	mov	x23,x0
	tst	x23,PI_COW
	cset	x1,ne
	tst	x23,PI_WRITE
	cset	x2,eq
	and	x1,x1,x2
	tst	x23,PI_FORKED_T
	cset	x2,ne
	and	x1,x1,x2
	cmp	x1,1
	verifier eq, 'pere : page en lecture seule, PTE_COW, partagee par fork'
	bl	cows
	mov	x24,x0
	mov	x0,0				; 256 pages ecrites : 256 copies
	mov	x1,256
	movl	x2,0x10000
	bl	ecrire_pages
	bl	cows
	sub	x25,x0,x24
	cmp	x25,256
	verifier eq, 'pere : 256 pages ecrites, fils vivant : 256 copies (VS_COWS)'
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,5
	verifier eq, 'fils : ses 256 pages d''avant les ecritures du pere (code 5)'
	mov	x0,100				; (le faucheur rend le fils)
	sys	SYS_SLEEP
	mov	x0,300
	bl	pageinfo_page
	mov	x23,x0
	tst	x23,PI_FORKED_T
	verifier eq, 'fils parti : plus partagee (compteur rendu)'
	bl	cows
	mov	x24,x0
	bl	remplir				; 4096 pages ecrites : aucune copie
	bl	cows
	sub	x25,x0,x24
	adr	x0,m_cows
	bl	puts
	mov	x0,x25
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	cmp	x25,0
	verifier eq, 'pere seul : 4096 ecritures sans copie (derniere reference)'
	mov	x0,300
	bl	pageinfo_page
	tst	x0,PI_WRITE
	verifier ne, 'pere seul : page de nouveau inscriptible'

	; --- 5. duree d'un fork de 16 Mo -------------------------------------
	sys	SYS_TIME_MS
	mov	x24,x0
	mov	x23,TEMPS_N
	mov	x25,0				; ms dans fork (cote pere)
.t5:	sys	SYS_TIME_MS
	mov	x26,x0
	fork
	cbz	x0,.f5
	mov	x22,x0
	sys	SYS_TIME_MS
	sub	x0,x0,x26
	add	x25,x25,x0
	mov	x0,x22
	sys	SYS_WAIT
	subs	x23,x23,1
	b.ne	.t5
	sys	SYS_TIME_MS
	sub	x24,x0,x24
	adr	x0,m_temps
	bl	puts
	mov	x0,x25
	bl	print_dec
	adr	x0,m_temps2
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_temps3
	bl	puts
	sys	SYS_TIME_MS			; le fils ecrit les 16 Mo (4096 copies)
	mov	x24,x0
	fork
	cbz	x0,.f5b
	sys	SYS_WAIT
	mov	x22,x0
	sys	SYS_TIME_MS
	sub	x24,x0,x24
	adr	x0,m_temps4
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_temps5
	bl	puts
	cmp	x22,13
	b	.e6
.f5:	mov	x0,0
	sys	SYS_EXIT
.f5b:	bl	cows
	mov	x20,x0
	mov	x0,0
	mov	x1,NP
	movl	x2,0x20000
	bl	ecrire_pages
	bl	cows
	sub	x0,x0,x20			; (+ la page de stats, celle de la pile)
	sub	x0,x0,NP
	cmp	x0,4
	mov	x0,13				; 13 : 4096 copies, 14 sinon
	mov	x1,14
	csel	x0,x1,x0,hi
	sys	SYS_EXIT

	; --- 6. fork d'un fork --------------------------------------------------
.e6:	verifier eq, 'fils : 16 Mo ecrits, 4096 copies (a 4 pres : pile, stats)'
	bl	remplir
	fork
	cbnz	x0,.p6
	fork					; fils : un petit-fils
	cbnz	x0,.c6
	mov	x0,50				; petit-fils : la page 21 d'avant,
	sys	SYS_SLEEP			; ecrit la 20
	mov	x0,21
	bl	lire_page
	cmp	x0,21
	b.ne	.g6bad
	mov	x0,20
	mov	x1,0x6020
	bl	ecrire_page
	mov	x0,9
	sys	SYS_EXIT
.g6bad:	mov	x0,95
	sys	SYS_EXIT
.c6:	tbnz	x0,63,.c6bad
	mov	x22,x0
	mov	x0,21
	mov	x1,0x6021
	bl	ecrire_page
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,9
	b.ne	.c6code
	mov	x0,20
	bl	lire_page
	cmp	x0,20
	b.ne	.c6bad
	mov	x0,21
	bl	lire_page
	movl	x1,0x6021
	cmp	x0,x1
	b.ne	.c6bad
	mov	x0,10
	sys	SYS_EXIT
.c6code:
	add	x0,x0,100
	sys	SYS_EXIT
.c6bad:	mov	x0,96
	sys	SYS_EXIT
.p6:	sys	SYS_WAIT
	cmp	x0,10
	verifier eq, 'fork d''un fork : chacun sa page 20 et 21 (code 9 + 1)'
	mov	x0,20
	bl	lire_page
	mov	x20,x0
	mov	x0,21
	bl	lire_page
	cmp	x0,21
	ccmp	x20,20,0,eq
	verifier eq, 'fork d''un fork : pages du pere intactes'

	; --- 7. sbrk apres fork ---------------------------------------------
	mov	x0,100				; un tas, son dernier octet marque
	sys	SYS_SBRK
	add	x20,x0,99
	mov	w1,0x5A
	strb	w1,[x20]
	fork
	cbnz	x0,.p7
	mov	x0,8192				; fils : le tas grandit, le dernier
	sys	SYS_SBRK			; octet du pere ecrit, puis le neuf
	tbnz	x0,63,.f7bad
	mov	w1,0xA5
	strb	w1,[x20]
	add	x1,x20,4096
	strb	w1,[x1]
	ldrb	w1,[x20]
	cmp	w1,0xA5
	b.ne	.f7bad
	mov	x0,11
	sys	SYS_EXIT
.f7bad:	mov	x0,97
	sys	SYS_EXIT
.p7:	sys	SYS_WAIT
	cmp	x0,11
	verifier eq, 'sbrk dans le fils : le tas grandit, la page du bord se copie (code 11)'
	ldrb	w0,[x20]
	cmp	w0,0x5A
	verifier eq, 'sbrk dans le fils : la page du bord du pere intacte'

	; --- 8. mprotect d'une page partagee ---------------------------------
	fork
	cbnz	x0,.p8
	mov	x0,200
	sys	SYS_SLEEP
	mov	x0,40
	bl	lire_page
	cmp	x0,40
	mov	x0,12
	mov	x1,98
	csel	x0,x1,x0,ne
	sys	SYS_EXIT
.p8:	mov	x22,x0
	mov	x0,40
	bl	page_adr
	mov	x23,x0
	mov	x1,4096
	mov	x2,PROT_READ
	sys	SYS_VM_PROTECT
	mov	x24,x0
	mov	x0,x23
	mov	x1,4096
	mov	x2,PROT_READ or PROT_WRITE
	sys	SYS_VM_PROTECT
	orr	x24,x24,x0
	mov	x0,x23
	sys	SYS_VM_PAGEINFO
	mov	x25,x0
	cmp	x24,0
	verifier eq, 'mprotect lecture puis lecture + ecriture d''une page partagee'
	tst	x25,PI_WRITE
	verifier eq, 'mprotect : la page partagee reste en lecture seule (copie a l''ecriture)'
	mov	x0,40
	movl	x1,0x8040
	bl	ecrire_page
	mov	x0,x22
	sys	SYS_WAIT
	cmp	x0,12
	verifier eq, 'mprotect puis ecriture du pere : le fils garde la page d''avant (code 12)'
	mov	x0,40
	mov	x1,40
	bl	ecrire_page

	; --- 9. exec apres fork : les pages reviennent au pere seul ----------
	fork
	cbnz	x0,.p9
	adr	x0,p_moi
	adr	x1,argv
	adr	x2,a_z
	stp	x0,x2,[x1]
	str	xzr,[x1,16]
	mov	x2,0
	mov	x3,0
	bl	f_exec
	mov	x0,99
	sys	SYS_EXIT
.p9:	sys	SYS_WAIT
	cmp	x0,7
	verifier eq, 'exec apres fork (code 7)'
	mov	x0,100
	sys	SYS_SLEEP
	bl	cows
	mov	x24,x0
	mov	x0,1000
	mov	x1,64
	movl	x2,0x90000
	bl	ecrire_pages
	bl	cows
	sub	x0,x0,x24
	cmp	x0,0
	verifier eq, 'exec apres fork : 64 ecritures du pere sans copie'

	; --- 10. pere a deux threads qui ecrit pendant les fork -------------
	bl	remplir
	adr	x0,stop
	str	xzr,[x0]
	adr	x0,perdu
	str	xzr,[x0]
	adr	x0,ecrivain
	adr	x1,pile+16384
	mov	x2,0
	sys	SYS_THREAD_CREATE
	mov	x26,x0
	cmp	x26,0
	verifier gt, 'thread ecrivain'
	mov	x0,20
	sys	SYS_SLEEP
	mov	x23,30
	mov	x24,0				; fils dont l'image a bouge
.t10:	fork
	cbz	x0,.f10
	sys	SYS_WAIT
	cmp	x0,8
	cinc	x24,x24,ne
	subs	x23,x23,1
	b.ne	.t10
	adr	x0,stop
	mov	x1,1
	str	x1,[x0]
	mov	x0,x26
	sys	SYS_THREAD_JOIN
	mov	x25,x0				; tours de l'ecrivain
	cmp	x24,0
	verifier eq, 'pere a deux threads, 30 fork : l''image du fils ne bouge pas'
	adr	x0,perdu
	ldr	x0,[x0]
	cmp	x0,0
	verifier eq, 'pere a deux threads : aucune ecriture de l''ecrivain perdue'
	cmp	x25,30
	verifier hi, 'pere a deux threads : l''ecrivain a tourne pendant les fork'
	b	.e11
.f10:	mov	x0,100				; somme des pages 100..1099, deux
	mov	x1,1000				; fois a 20 ms d'ecart
	bl	somme
	mov	x20,x0
	mov	x0,20
	sys	SYS_SLEEP
	mov	x0,100
	mov	x1,1000
	bl	somme
	cmp	x0,x20
	mov	x0,8
	mov	x1,9
	csel	x0,x1,x0,ne
	sys	SYS_EXIT

	; --- 11. fuites : N fork + exit (le fils ecrit 16 pages, le pere aussi)
.e11:	mov	x0,10
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
	mov	x21,x0				; refus, codes faux
	mov	x0,1000
	sys	SYS_SLEEP
	bl	pages_libres
	mov	x26,x0				; P1
	adr	x0,tours
	ldr	x0,[x0]
	lsr	x0,x0,1
	bl	tours_fork
	add	x21,x21,x0
	sys	SYS_TIME_MS
	sub	x25,x0,x25
	cmp	x21,0
	verifier eq, 'boucle fork + exit : aucun refus, chaque fils rend son code'
	mov	x0,1000
	sys	SYS_SLEEP
	bl	pages_libres
	sub	x23,x26,x0
	sub	x24,x24,x26
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
	cmp	x23,16
	verifier lt, 'boucle fork + exit d''un process de 16 Mo : pas de fuite (2e moitie)'

	; --- 12. fuites : fork + exec + wait ---------------------------------
	mov	x0,5
	bl	tours_exec
	mov	x0,1000
	sys	SYS_SLEEP
	bl	pages_libres
	mov	x24,x0
	mov	x0,50
	bl	tours_exec
	mov	x21,x0
	mov	x0,1000
	sys	SYS_SLEEP
	bl	pages_libres
	mov	x26,x0
	mov	x0,50
	bl	tours_exec
	add	x21,x21,x0
	cmp	x21,0
	verifier eq, 'boucle fork + exec + wait : chaque exec aboutit'
	mov	x0,1000
	sys	SYS_SLEEP
	bl	pages_libres
	sub	x23,x26,x0
	sub	x24,x24,x26
	adr	x0,m_exec
	bl	puts
	mov	x0,x24
	bl	print_sdec
	adr	x0,m_tours4
	bl	puts
	mov	x0,x23
	bl	print_sdec
	adr	x0,m_tours5
	bl	puts
	cmp	x23,16
	verifier lt, 'boucle fork + exec + wait : pas de fuite (2e moitie)'

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

; -m : pages libres, en decimal, sur la sortie standard
libres:
	bl	pages_libres
	adr	x1,stats+63			; chiffres de la fin vers le debut
	mov	w2,10
	strb	w2,[x1]
	mov	x3,10
.c:	udiv	x4,x0,x3
	msub	x5,x4,x3,x0
	add	w5,w5,'0'
	strb	w5,[x1,-1]!
	mov	x0,x4
	cbnz	x0,.c
	adr	x2,stats+64
	sub	x2,x2,x1
	mov	x0,1
	sys	SYS_WRITE
	mov	x0,0
	sys	SYS_EXIT

; Thread ecrivain : pages 100..1099, mot +16 : un compteur, relu aussitot
; (une ecriture perdue : perdu += 1) ; jusqu'a stop -> code = tours
ecrivain:
	mov	x19,0				; tour
.tour:	add	x19,x19,1
	mov	x20,100
.p:	page_va x1,x20
	str	x19,[x1,16]
	ldr	x2,[x1,16]
	cmp	x2,x19
	b.eq	.ok
	adr	x2,perdu
	ldr	x3,[x2]
	add	x3,x3,1
	str	x3,[x2]
.ok:	add	x20,x20,1
	cmp	x20,1100
	b.lo	.p
	adr	x0,stop
	ldr	x0,[x0]
	cbz	x0,.tour
	mov	x0,x19
	sys	SYS_THREAD_EXIT

; x0 = premiere page, x1 = pages -> x0 = somme des mots +16
somme:
	mov	x2,0
	adr	x3,region
	ldr	x3,[x3]
	add	x3,x3,x0,lsl 12
.l:	cbz	x1,.f
	ldr	x4,[x3,16]
	add	x2,x2,x4
	add	x3,x3,4096
	sub	x1,x1,1
	b	.l
.f:	mov	x0,x2
	ret

; page i : i (toutes)
remplir:
	adr	x3,region
	ldr	x3,[x3]
	mov	x0,0
.l:	add	x4,x3,x0,lsl 12
	str	x0,[x4]
	add	x0,x0,1
	cmp	x0,NP
	b.lo	.l
	ret

; x0 = page -> x0 = son mot 0
lire_page:
	adr	x1,region
	ldr	x1,[x1]
	add	x1,x1,x0,lsl 12
	ldr	x0,[x1]
	ret

; x0 = page, x1 = valeur
ecrire_page:
	adr	x2,region
	ldr	x2,[x2]
	add	x2,x2,x0,lsl 12
	str	x1,[x2]
	ret

; x0 = page -> x0 = son adresse
page_adr:
	adr	x1,region
	ldr	x1,[x1]
	add	x0,x1,x0,lsl 12
	ret

; x0 = premiere page, x1 = pages, x2 = base : page i <- base + i
ecrire_pages:
	adr	x3,region
	ldr	x3,[x3]
.l:	cbz	x1,.f
	add	x4,x2,x0
	add	x5,x3,x0,lsl 12
	str	x4,[x5]
	add	x0,x0,1
	sub	x1,x1,1
	b	.l
.f:	ret

; x0 = premiere page, x1 = pages -> x0 = pages dont le mot 0 n'est pas i
verifier_pages:
	adr	x3,region
	ldr	x3,[x3]
	mov	x4,0
.l:	cbz	x1,.f
	add	x5,x3,x0,lsl 12
	ldr	x5,[x5]
	cmp	x5,x0
	cinc	x4,x4,ne
	add	x0,x0,1
	sub	x1,x1,1
	b	.l
.f:	mov	x0,x4
	ret

; x0 = page -> x0 = bits PI_*
pageinfo_page:
	prologue
	bl	page_adr
	sys	SYS_VM_PAGEINFO
	epilogue

; -> x0 = pages copiees sur ecriture (VM_STATS, soi)
cows:
	prologue
	mov	x0,0
	adr	x1,stats
	sys	SYS_VM_STATS
	adr	x1,stats
	ldr	x0,[x1,VS_COWS]
	epilogue

; x0 = tours : fork, le fils ecrit 16 pages et sort (code 5), wait, le pere
; ecrit 16 pages -> x0 = fork refuses + codes faux
tours_fork:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,0
	mov	x21,0				; pages : tournent
	cbz	x19,.fin
.t:	fork
	cbz	x0,.fils
	tbnz	x0,63,.err
	sys	SYS_WAIT
	cmp	x0,5
	cinc	x20,x20,ne
	mov	x0,x21
	mov	x1,16
	mov	x2,0
	bl	ecrire_pages
	b	.n
.err:	add	x20,x20,1
	mov	x0,10
	sys	SYS_SLEEP
.n:	add	x21,x21,16
	and	x21,x21,NP-1
	mov	x0,1
	sys	SYS_SLEEP
	subs	x19,x19,1
	b.ne	.t
.fin:	mov	x0,x20
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.fils:	add	x0,x21,2048
	and	x0,x0,NP-1
	mov	x1,16
	mov	x2,0x55
	bl	ecrire_pages
	mov	x0,5
	sys	SYS_EXIT

; x0 = tours : fork, exec « -z » (code 7), wait -> x0 = refus + codes faux
tours_exec:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.t:	fork
	cbz	x0,.fils
	tbnz	x0,63,.err
	sys	SYS_WAIT
	cmp	x0,7
	cinc	x20,x20,ne
	b	.n
.err:	add	x20,x20,1
	mov	x0,10
	sys	SYS_SLEEP
.n:	mov	x0,1
	sys	SYS_SLEEP
	subs	x19,x19,1
	b.ne	.t
	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue
.fils:	adr	x0,p_moi
	adr	x1,argv
	adr	x2,a_z
	stp	x0,x2,[x1]
	str	xzr,[x1,16]
	mov	x2,0
	mov	x3,0
	bl	f_exec
	mov	x0,99
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

p_moi		db 'SD0:/bin/cowtest',0
a_z		db '-z',0
m_debut		db 'cowtest : fork en copie sur ecriture',10,0
m_cows		db 'cowtest : copies apres le depart du fils (4096 ecritures) : ',0
m_temps		db 'cowtest : ',0
m_temps2	db ' ms dans 20 fork d''un process de 16 Mo (',0
m_temps3	db ' ms avec exit + wait)',10,0
m_temps4	db 'cowtest : fork + le fils ecrit 16 Mo + wait : ',0
m_temps5	db ' ms',10,0
m_tours		db 'cowtest : ',0
m_tours2	db ' fork + exit (16 Mo) en ',0
m_tours3	db ' ms ; pages libres perdues : ',0
m_tours4	db ' (1re moitie), ',0
m_tours5	db ' (2e moitie)',10,0
m_exec		db 'cowtest : 100 fork + exec + wait ; pages libres perdues : ',0
m_moins		db '-',0
m_pass1		db 'PASS ',0
m_fail1		db 'FAIL ',0
m_fin		db 'cowtest : ',0
m_pass		db ' PASS, ',0
m_fail		db ' FAIL',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 8
tours		dd 0, 0
region		dd 0, 0
stop		dd 0, 0
perdu		dd 0, 0
argv		rd 8
n_pass		dd 0, 0
n_fail		dd 0, 0
stats		rb 64
		align 16
pile		rb 16384
