; threadtest - essai des threads (appels 0x20-0x2A, docs/ABI.md) et des
; verrous d'AppKit (mtx_*, cnd_* : docs/KITS.md) :
;  * TPIDR_EL0 : 0 au depart du thread principal ; valeur donnee par
;    thread_create_ex, gardee sous preemption (8 threads sur 4 coeurs qui le
;    relisent et le changent sans cesse pendant 1,5 s), sur plusieurs coeurs
;    (thread_list : coeur de chacun) ; celui du thread principal intact ;
;  * thread_self, thread_set_name et thread_list (noms), thread_join (code
;    de sortie de thread_exit, E_SRCH, E_DEADLK), thread_kill (thread bloque
;    dans un futex, thread qui tourne sans appel systeme) ;
;  * priorites : 16 threads qui tournent 2 s, moitie en priorite haute : ces
;    derniers ont plus de temps processeur sur un meme coeur, les normaux en
;    ont quand meme (pas de famine) ; temps reel sans droits : E_PERM ;
;  * mutex (4 threads, compteur protege), condition (producteur et
;    consommateur), attentes avec delai (cnd_timedwait, futex_wait).
; Sortie : « threadtest : N PASS, M FAIL », code de sortie = M.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

STK		= 16384			; pile d'un thread
NSTK		= 16			; piles (threads en meme temps au plus)
REC		= 64			; fiche d'un thread
R_TLS		= 0			; TPIDR_EL0 attendu
R_TID		= 8			; tid (thread_self)
R_BAD		= 16			; lectures fausses de TPIDR_EL0
R_ITER		= 24			; tours de boucle
R_INDEX		= 32
R_PRIO		= 40
N_TLS		= 8
N_PRIO		= 16
N_MTX		= 4
MTX_TOURS	= 20000
N_PROD		= 1000
E_SRCH		= -3
E_DEADLK	= -35
E_PERM		= -1
E_BUSY		= -16

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,m_debut
	bl	puts

	; --- TPIDR_EL0 du thread principal ------------------------------------
	mrs	x2,tpidr_el0
	cmp	x2,0
	cset	x0,eq
	adr	x1,l_tp0
	bl	verdict
	movl	x0,0xABCDEF
	msr	tpidr_el0,x0

	; --- thread_self, nom du thread principal -----------------------------
	sys	SYS_THREAD_SELF
	adr	x1,tid_main
	str	x0,[x1]
	mov	x2,x0
	cmp	x0,0
	cset	x0,gt
	adr	x1,l_self
	bl	verdict
	mov	x0,0
	adr	x1,n_main
	sys	SYS_THREAD_SET_NAME
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_setname
	bl	verdict

	; --- 8 threads TPIDR_EL0 ------------------------------------------------
	mov	x19,0
.ctls:	adr	x20,recs
	add	x20,x20,x19,lsl 6
	add	x0,x19,1
	lsl	x0,x0,32
	add	x0,x0,0x5000
	str	x0,[x20,R_TLS]
	str	x19,[x20,R_INDEX]
	str	xzr,[x20,R_TID]
	mov	x3,x0				; TPIDR_EL0 initial
	adr	x0,w_tls
	mov	x1,x19
	bl	pile
	mov	x2,x20
	mov	x4,THREAD_PRIO_NORMAL
	adr	x5,n_tls
	sys	SYS_THREAD_CREATE_EX
	adr	x1,tids
	str	x0,[x1,x19,lsl 3]
	tbnz	x0,63,.ctls_ko
	add	x19,x19,1
	cmp	x19,N_TLS
	b.lo	.ctls
.ctls_ko:
	mov	x2,x19
	cmp	x19,N_TLS
	cset	x0,eq
	adr	x1,l_create
	bl	verdict
	mov	x0,500				; pendant qu'ils tournent : coeurs et noms
	sys	SYS_SLEEP
	bl	liste
	mov	x21,0				; coeurs vus (bits)
	mov	x22,0				; noms « tls-worker »
	mov	x19,0
.vc:	adr	x0,tids
	ldr	x0,[x0,x19,lsl 3]
	bl	cherche
	cbz	x0,.vn
	mov	x20,x0
	ldr	x1,[x20,THL_CPU]
	mov	x2,1
	lsl	x2,x2,x1
	orr	x21,x21,x2
	add	x0,x20,THL_NAME
	adr	x1,n_tls
	bl	str_eq
	add	x22,x22,x0
.vn:	add	x19,x19,1
	cmp	x19,N_TLS
	b.lo	.vc
	mov	x2,0				; nombre de coeurs
	mov	x0,x21
.pc:	cbz	x0,.pcf
	and	x1,x0,1
	add	x2,x2,x1
	lsr	x0,x0,1
	b	.pc
.pcf:	cmp	x2,2
	cset	x0,hs
	adr	x1,l_coeurs
	bl	verdict
	mov	x2,x22
	cmp	x22,N_TLS
	cset	x0,eq
	adr	x1,l_noms
	bl	verdict
	adr	x0,tid_main			; nom du thread principal dans la liste
	ldr	x0,[x0]
	bl	cherche
	mov	x2,0
	cbz	x0,.nm
	add	x0,x0,THL_NAME
	adr	x1,n_main
	bl	str_eq
	mov	x2,x0
.nm:	mov	x0,x2
	adr	x1,l_nom_main
	bl	verdict
	mov	x19,0				; join : codes, erreurs de TPIDR_EL0
	mov	x21,0				; codes justes
	mov	x22,0				; erreurs
	mov	x23,0				; tours
	mov	x24,0				; thread_self justes
.jt:	adr	x0,tids
	ldr	x0,[x0,x19,lsl 3]
	sys	SYS_THREAD_JOIN
	mov	x1,1000
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0			; code mod 1000 = index * 7
	mov	x4,7
	mul	x4,x19,x4
	cmp	x3,x4
	cinc	x21,x21,eq
	adr	x20,recs
	add	x20,x20,x19,lsl 6
	ldr	x0,[x20,R_BAD]
	add	x22,x22,x0
	ldr	x0,[x20,R_ITER]
	add	x23,x23,x0
	ldr	x0,[x20,R_TID]
	adr	x1,tids
	ldr	x1,[x1,x19,lsl 3]
	cmp	x0,x1
	cinc	x24,x24,eq
	add	x19,x19,1
	cmp	x19,N_TLS
	b.lo	.jt
	mov	x2,x21
	cmp	x21,N_TLS
	cset	x0,eq
	adr	x1,l_join
	bl	verdict
	mov	x2,x24
	cmp	x24,N_TLS
	cset	x0,eq
	adr	x1,l_self_th
	bl	verdict
	mov	x2,x22
	cmp	x22,0
	cset	x0,eq
	adr	x1,l_tls
	bl	verdict
	mov	x0,1000				; (tours, en milliers)
	udiv	x2,x23,x0
	movl	x0,N_TLS*0x10000
	cmp	x23,x0
	cset	x0,hs
	adr	x1,l_tours
	bl	verdict
	mrs	x2,tpidr_el0
	movl	x0,0xABCDEF
	cmp	x2,x0
	cset	x0,eq
	adr	x1,l_tp_main
	bl	verdict

	; --- join : erreurs ------------------------------------------------------
	adr	x0,tids				; deja fini (dans l'anneau) : son code
	ldr	x0,[x0,8]
	sys	SYS_THREAD_JOIN
	mov	x2,x0
	cmp	x0,7
	cset	x0,eq
	adr	x1,l_rejoin
	bl	verdict
	movl	x0,0x7FFFFFF0
	sys	SYS_THREAD_JOIN
	mov	x2,x0
	cmn	x0,-E_SRCH
	cset	x0,eq
	adr	x1,l_srch
	bl	verdict
	adr	x0,tid_main
	ldr	x0,[x0]
	sys	SYS_THREAD_JOIN
	mov	x2,x0
	cmn	x0,-E_DEADLK
	cset	x0,eq
	adr	x1,l_deadlk
	bl	verdict

	; --- priorites ------------------------------------------------------------
	mov	x0,0				; soi : haute puis normale (ancienne rendue)
	mov	x1,THREAD_PRIO_HIGH
	sys	SYS_THREAD_SET_PRIORITY
	mov	x19,x0
	mov	x0,0
	mov	x1,THREAD_PRIO_NORMAL
	sys	SYS_THREAD_SET_PRIORITY
	mov	x2,x0
	cmp	x19,THREAD_PRIO_NORMAL
	ccmp	x0,THREAD_PRIO_HIGH,0,eq
	cset	x0,eq
	adr	x1,l_setprio
	bl	verdict
	mov	x0,0
	mov	x1,THREAD_PRIO_RT
	sys	SYS_THREAD_SET_PRIORITY
	mov	x2,x0
	cmn	x0,-E_PERM
	cset	x0,eq
	adr	x1,l_rt
	bl	verdict
	adr	x0,stop
	str	wzr,[x0]
	mov	x19,0
.cpr:	adr	x20,recs
	add	x20,x20,x19,lsl 6
	str	xzr,[x20,R_ITER]
	lsr	x4,x19,2			; blocs de 4 : normal, haut, normal, haut
	and	x4,x4,1				; (tourniquet des coeurs : chacun a
	str	x4,[x20,R_PRIO]			;  les deux sortes)
	adr	x0,w_spin
	mov	x1,x19
	bl	pile
	mov	x2,x20
	mov	x3,0
	adr	x5,n_prio
	sys	SYS_THREAD_CREATE_EX
	adr	x1,tids
	str	x0,[x1,x19,lsl 3]
	tbnz	x0,63,.cpr_ko
	add	x19,x19,1
	cmp	x19,N_PRIO
	b.lo	.cpr
.cpr_ko:
	mov	x2,x19
	cmp	x19,N_PRIO
	cset	x0,eq
	adr	x1,l_create_p
	bl	verdict
	mov	x0,2000
	sys	SYS_SLEEP
	bl	liste				; temps de chacun (ticks = ms)
	adr	x0,stop
	mov	w1,1
	str	w1,[x0]
	adr	x0,par_coeur			; par coeur : haut (somme, nombre),
	mov	x1,0				; normal (somme, nombre, minimum)
.z:	str	xzr,[x0,x1,lsl 3]
	add	x1,x1,1
	cmp	x1,4*8
	b.lo	.z
	mov	x19,0
.pp:	adr	x0,tids
	ldr	x0,[x0,x19,lsl 3]
	bl	cherche
	cbz	x0,.ppn
	ldr	x1,[x0,THL_CPU]
	and	x1,x1,3
	adr	x2,par_coeur
	add	x2,x2,x1,lsl 6			; 8 mots par coeur
	ldr	x3,[x0,THL_RUNTIME]
	ldr	x4,[x0,THL_PRIO]
	cbz	x4,.pnorm
	ldp	x5,x6,[x2]
	add	x5,x5,x3
	add	x6,x6,1
	stp	x5,x6,[x2]
	b	.ppn
.pnorm:	ldp	x5,x6,[x2,16]
	add	x5,x5,x3
	add	x6,x6,1
	stp	x5,x6,[x2,16]
	ldr	x5,[x2,32]			; minimum (0 : pas encore)
	cbz	x6,.pmin
	cmp	x6,1
	b.eq	.pmin
	cmp	x3,x5
	b.hs	.ppn
.pmin:	str	x3,[x2,32]
.ppn:	add	x19,x19,1
	cmp	x19,N_PRIO
	b.lo	.pp
	mov	x19,0				; coeurs ou les deux sortes tournaient :
	mov	x21,0				; somme des moyennes hautes (x21) et
	mov	x22,0				; normales (x22), minimum normal (x23)
	movl	x23,0x7FFFFFFF
	mov	x24,0				; coeurs comptes
.pc2:	adr	x2,par_coeur
	add	x2,x2,x19,lsl 6
	ldp	x5,x6,[x2]
	ldp	x7,x9,[x2,16]
	cbz	x6,.pcn
	cbz	x9,.pcn
	udiv	x5,x5,x6
	udiv	x7,x7,x9
	add	x21,x21,x5
	add	x22,x22,x7
	ldr	x5,[x2,32]
	cmp	x5,x23
	csel	x23,x5,x23,lo
	add	x24,x24,1
.pcn:	add	x19,x19,1
	cmp	x19,4
	b.lo	.pc2
	mov	x2,x24
	cmp	x24,1
	cset	x0,hs
	adr	x1,l_mixtes
	bl	verdict
	mov	x2,0				; avance des hauts : 100 x haut / normal
	cbz	x22,.av
	mov	x0,100
	mul	x2,x21,x0
	udiv	x2,x2,x22
.av:	cmp	x2,150
	cset	x0,hs
	adr	x1,l_avance
	bl	verdict
	mov	x2,x23				; pas de famine : chaque normal a eu
	cmp	x24,0				; au moins 2 % des 2 s
	csel	x2,xzr,x2,eq
	cmp	x2,40
	cset	x0,hs
	adr	x1,l_famine
	bl	verdict
	mov	x19,0
.jp:	adr	x0,tids
	ldr	x0,[x0,x19,lsl 3]
	sys	SYS_THREAD_JOIN
	add	x19,x19,1
	cmp	x19,N_PRIO
	b.lo	.jp

	; --- thread_kill ------------------------------------------------------------
	adr	x0,kw
	str	wzr,[x0]
	adr	x0,w_bloque
	mov	x1,0
	bl	pile
	mov	x2,0
	sys	SYS_THREAD_CREATE
	mov	x19,x0
	adr	x0,w_tourne
	mov	x1,1
	bl	pile
	mov	x2,0
	sys	SYS_THREAD_CREATE
	mov	x20,x0
	mov	x0,200
	sys	SYS_SLEEP
	mov	x0,x19
	mov	x1,77
	sys	SYS_THREAD_KILL
	mov	x21,x0
	mov	x0,x20
	mov	x1,78
	sys	SYS_THREAD_KILL
	orr	x21,x21,x0
	mov	x0,x19
	sys	SYS_THREAD_JOIN
	mov	x2,x0
	cmp	x0,77
	ccmp	x21,0,0,eq
	cset	x0,eq
	adr	x1,l_kill_b
	bl	verdict
	mov	x0,x20
	sys	SYS_THREAD_JOIN
	mov	x2,x0
	cmp	x0,78
	cset	x0,eq
	adr	x1,l_kill_t
	bl	verdict
	movl	x0,0x7FFFFFF0
	mov	x1,1
	sys	SYS_THREAD_KILL
	mov	x2,x0
	cmn	x0,-E_SRCH
	cset	x0,eq
	adr	x1,l_kill_srch
	bl	verdict

	; --- mutex ------------------------------------------------------------------
	adr	x0,compteur
	str	xzr,[x0]
	mov	x19,0
.cm:	adr	x0,w_mtx
	mov	x1,x19
	bl	pile
	mov	x2,0
	sys	SYS_THREAD_CREATE
	adr	x1,tids
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,N_MTX
	b.lo	.cm
	mov	x19,0
.jm:	adr	x0,tids
	ldr	x0,[x0,x19,lsl 3]
	sys	SYS_THREAD_JOIN
	add	x19,x19,1
	cmp	x19,N_MTX
	b.lo	.jm
	adr	x0,compteur
	ldr	x2,[x0]
	movl	x0,N_MTX*MTX_TOURS
	cmp	x2,x0
	cset	x0,eq
	adr	x1,l_mutex
	bl	verdict
	adr	x0,mtx				; trylock : pris -> E_BUSY, libre -> 0
	bl	mtx_lock
	adr	x0,mtx
	bl	mtx_trylock
	mov	x19,x0
	adr	x0,mtx
	bl	mtx_unlock
	adr	x0,mtx
	bl	mtx_trylock
	mov	x2,x0
	adr	x0,mtx
	bl	mtx_unlock
	cmn	x19,-E_BUSY
	ccmp	x2,0,0,eq
	cset	x0,eq
	mov	x2,x19
	adr	x1,l_trylock
	bl	verdict

	; --- condition : producteur (ici) et consommateur ------------------------
	adr	x0,plein
	str	xzr,[x0]
	adr	x0,w_conso
	mov	x1,0
	bl	pile
	mov	x2,0
	sys	SYS_THREAD_CREATE
	mov	x20,x0
	mov	x19,1
.prod:	adr	x0,mtx
	bl	mtx_lock
.pw:	adr	x0,plein
	ldr	x0,[x0]
	cbz	x0,.pput
	adr	x0,c_vide
	adr	x1,mtx
	bl	cnd_wait
	b	.pw
.pput:	adr	x0,valeur
	str	x19,[x0]
	adr	x0,plein
	mov	x1,1
	str	x1,[x0]
	adr	x0,c_plein
	bl	cnd_signal
	adr	x0,mtx
	bl	mtx_unlock
	add	x19,x19,1
	cmp	x19,N_PROD
	b.ls	.prod
	mov	x0,x20
	sys	SYS_THREAD_JOIN
	mov	x2,x0
	movl	x1,N_PROD*(N_PROD+1)/2
	cmp	x0,x1
	cset	x0,eq
	adr	x1,l_cond
	bl	verdict

	; --- attentes avec delai ------------------------------------------------------
	sys	SYS_TIME_MS
	mov	x19,x0
	adr	x0,mtx
	bl	mtx_lock
	adr	x0,c_rien
	adr	x1,mtx
	mov	x2,60
	bl	cnd_timedwait
	mov	x20,x0
	adr	x0,mtx
	bl	mtx_unlock
	sys	SYS_TIME_MS
	sub	x2,x0,x19
	mov	x3,50
	cmn	x20,-E_TIMEDOUT
	ccmp	x2,x3,0,eq			; (eq : x2 >= 50 ; sinon C = 0)
	cset	x0,hs
	adr	x1,l_cnd_delai
	bl	verdict
	sys	SYS_TIME_MS
	mov	x19,x0
	adr	x0,kw
	str	wzr,[x0]
	mov	x1,0
	mov	x2,100
	sys	SYS_FUTEX_WAIT
	mov	x20,x0
	sys	SYS_TIME_MS
	sub	x2,x0,x19
	mov	x3,90
	cmn	x20,-E_TIMEDOUT
	ccmp	x2,x3,0,eq
	cset	x0,hs
	adr	x1,l_futex_delai
	bl	verdict
	adr	x0,kw
	mov	x1,5				; valeur differente : tout de suite
	mov	x2,0
	sys	SYS_FUTEX_WAIT
	mov	x2,x0
	cmn	x0,-E_AGAIN
	cset	x0,eq
	adr	x1,l_futex_again
	bl	verdict

	; --- bilan ------------------------------------------------------------------
	adr	x0,m_bilan
	bl	puts
	adr	x0,n_pass
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_bilan2
	bl	puts
	adr	x0,n_fail
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_bilan3
	bl	puts
	adr	x0,n_fail
	ldr	x0,[x0]
	sys	SYS_EXIT

; x1 = numero de pile -> x1 = son sommet (garde x0, x2-x5)
pile:
	adr	x6,piles
	add	x1,x1,1
	mov	x7,STK
	madd	x1,x1,x7,x6
	ret

; thread_list de ce process dans liste_buf (liste_n entrees)
liste:
	prologue
	mov	x0,0
	adr	x1,liste_buf
	mov	x2,64
	sys	SYS_THREAD_LIST
	adr	x1,liste_n
	str	x0,[x1]
	epilogue

; x0 = tid -> x0 = son entree de liste_buf, 0 si absent
cherche:
	adr	x1,liste_n
	ldr	x2,[x1]
	adr	x1,liste_buf
.c:	cmp	x2,0
	b.le	.non
	ldr	x3,[x1,THL_TID]
	cmp	x3,x0
	b.eq	.oui
	add	x1,x1,THL_SIZE
	sub	x2,x2,1
	b	.c
.oui:	mov	x0,x1
	ret
.non:	mov	x0,0
	ret

; --- threads ----------------------------------------------------------------------
; x0 = fiche : relit TPIDR_EL0 pendant 1,5 s (et le change tous les 65 536
; tours) ; code de sortie = index x 7 + erreurs x 1000
w_tls:
	mov	x19,x0
	ldr	x20,[x19,R_TLS]
	mov	x21,0
	mov	x22,0
	sys	SYS_THREAD_SELF
	str	x0,[x19,R_TID]
	mrs	x1,tpidr_el0			; valeur de thread_create_ex
	cmp	x1,x20
	cinc	x21,x21,ne
	sys	SYS_TIME_MS
	add	x23,x0,1500
.l:	mrs	x1,tpidr_el0
	cmp	x1,x20
	cinc	x21,x21,ne
	add	x22,x22,1
	tst	x22,0xFFFF
	b.ne	.garde
	add	x20,x20,0x10
	msr	tpidr_el0,x20
.garde:	tst	x22,0xFFF
	b.ne	.l
	sys	SYS_TIME_MS
	cmp	x0,x23
	b.lo	.l
	str	x21,[x19,R_BAD]
	str	x22,[x19,R_ITER]
	ldr	x0,[x19,R_INDEX]
	mov	x1,7
	mul	x0,x0,x1
	mov	x1,1000
	madd	x0,x21,x1,x0
	sys	SYS_THREAD_EXIT

; x0 = fiche : tourne jusqu'a stop
w_spin:
	mov	x19,x0
	adr	x20,stop
	mov	x21,0
.l:	add	x21,x21,1
	tst	x21,0xFFF
	b.ne	.l
	ldr	w0,[x20]
	cbz	w0,.l
	str	x21,[x19,R_ITER]
	mov	x0,0
	sys	SYS_THREAD_EXIT

; bloque pour toujours dans un futex (thread_kill le reveille et le tue)
w_bloque:
	adr	x0,kw
	mov	x1,0
	mov	x2,0
	sys	SYS_FUTEX_WAIT
	adr	x0,kw
	ldr	w1,[x0]
	cbz	w1,w_bloque
	mov	x0,5
	sys	SYS_THREAD_EXIT

; tourne pour toujours sans appel systeme (tue a un retour d'interruption)
w_tourne:
	mov	x0,0
.l:	add	x0,x0,1
	b	.l

; MTX_TOURS increments du compteur sous le mutex (yield parfois, verrou tenu)
w_mtx:
	mov	x19,0
.l:	adr	x0,mtx
	bl	mtx_lock
	adr	x20,compteur
	ldr	x21,[x20]
	tst	x19,0xFF
	b.ne	.s
	sys	SYS_YIELD
.s:	add	x21,x21,1
	str	x21,[x20]
	adr	x0,mtx
	bl	mtx_unlock
	add	x19,x19,1
	movl	x0,MTX_TOURS
	cmp	x19,x0
	b.lo	.l
	mov	x0,0
	sys	SYS_THREAD_EXIT

; consommateur : N_PROD valeurs -> code de sortie = leur somme
w_conso:
	mov	x19,0				; somme
	mov	x20,0				; recues
.l:	adr	x0,mtx
	bl	mtx_lock
.w:	adr	x0,plein
	ldr	x0,[x0]
	cbnz	x0,.prend
	adr	x0,c_plein
	adr	x1,mtx
	bl	cnd_wait
	b	.w
.prend:	adr	x0,valeur
	ldr	x0,[x0]
	add	x19,x19,x0
	adr	x0,plein
	str	xzr,[x0]
	adr	x0,c_vide
	bl	cnd_signal
	adr	x0,mtx
	bl	mtx_unlock
	add	x20,x20,1
	cmp	x20,N_PROD
	b.lo	.l
	mov	x0,x19
	sys	SYS_THREAD_EXIT

; x0 = 1 si reussi, x1 = libelle, x2 = valeur : « threadtest : PASS libelle (valeur) »
verdict:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	adr	x1,n_pass
	adr	x2,m_pass
	cbnz	x0,.c
	adr	x1,n_fail
	adr	x2,m_fail
.c:	ldr	x3,[x1]
	add	x3,x3,1
	str	x3,[x1]
	mov	x21,x2
	adr	x0,m_tt
	bl	puts
	mov	x0,x21
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_par
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_par2
	bl	puts
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

m_debut		db 'threadtest : TPIDR_EL0, join, priorites, mutex et conditions (environ 6 s)',10,0
m_tt		db 'threadtest : ',0
m_pass		db 'PASS ',0
m_fail		db 'FAIL ',0
m_par		db ' (',0
m_par2		db ')',10,0
m_bilan		db 'threadtest : ',0
m_bilan2	db ' PASS, ',0
m_bilan3	db ' FAIL',10,0
n_main		db 'tt-main',0
n_tls		db 'tls-worker',0
n_prio		db 'prio-worker',0
l_tp0		db 'TPIDR_EL0 du thread principal a 0 au depart',0
l_self		db 'thread_self (tid)',0
l_setname	db 'thread_set_name',0
l_create	db 'thread_create_ex x 8 (TPIDR_EL0, nom)',0
l_coeurs	db 'threads sur plusieurs coeurs (thread_list)',0
l_noms		db 'noms des threads (thread_list)',0
l_nom_main	db 'nom du thread principal (thread_list)',0
l_join		db 'thread_join : codes de thread_exit',0
l_self_th	db 'thread_self dans chaque thread',0
l_tls		db 'TPIDR_EL0 garde sous preemption (lectures fausses)',0
l_tours		db 'tours de boucle (milliers)',0
l_tp_main	db 'TPIDR_EL0 du thread principal intact',0
l_rejoin	db 'thread_join d''un thread deja rejoint : son code',0
l_srch		db 'thread_join d''un tid inconnu : E_SRCH',0
l_deadlk	db 'thread_join de soi : E_DEADLK',0
l_setprio	db 'thread_set_priority rend l''ancienne',0
l_rt		db 'temps reel sans droits : E_PERM',0
l_create_p	db 'thread_create_ex x 16 (8 normaux, 8 hauts)',0
l_mixtes	db 'coeurs avec les deux priorites',0
l_avance	db 'priorite haute : plus de temps processeur (% du normal)',0
l_famine	db 'pas de famine : temps minimal d''un thread normal (ms)',0
l_kill_b	db 'thread_kill d''un thread bloque (code)',0
l_kill_t	db 'thread_kill d''un thread qui tourne (code)',0
l_kill_srch	db 'thread_kill d''un tid inconnu : E_SRCH',0
l_mutex		db 'mutex : compteur de 4 threads',0
l_trylock	db 'mtx_trylock : E_BUSY puis 0',0
l_cond		db 'condition : somme du consommateur',0
l_cnd_delai	db 'cnd_timedwait : E_TIMEDOUT apres le delai (ms)',0
l_futex_delai	db 'futex_wait : E_TIMEDOUT apres le delai (ms)',0
l_futex_again	db 'futex_wait d''une autre valeur : E_AGAIN',0
		align 8

segment readable writeable
kit_data
		align 8
n_pass		dd 0
n_fail		dd 0
tid_main	dd 0
compteur	dd 0
valeur		dd 0
plein		dd 0
liste_n		dd 0
tids		rb 8*N_PRIO
recs		rb REC*N_PRIO
par_coeur	rb 64*4
stop		dw 0
kw		dw 0
mtx		dw 0
c_plein		dw 0
c_vide		dw 0
c_rien		dw 0
		align 64
liste_buf	rb 64*THL_SIZE
		align 16
piles		rb STK*NSTK
