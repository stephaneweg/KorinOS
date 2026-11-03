; ============================================================================
;  kits/appkit/sync.inc - verrous (mutex) et variables de condition d'AppKit,
;  sur les futex du noyau (SYS_FUTEX_WAIT, SYS_FUTEX_WAKE ; docs/KITS.md).
;
;  * Mutex : un mot de 32 bits a 0 (libre), aligne sur 4 octets. 1 : pris,
;    2 : pris et quelqu'un attend (on ne reveille qu'alors : un verrou sans
;    concurrence ne fait aucun appel systeme). Pas recursif, pas de
;    proprietaire : le rendre depuis un autre thread est permis.
;  * Condition : un mot de 32 bits a 0 (compteur de signaux). cnd_wait rend
;    le mutex, attend un signal, puis le reprend ; reveils parasites
;    possibles (comme POSIX) : a appeler dans une boucle sur la condition.
;  * Fonctions AAPCS ; elles ne touchent ni x16-x18 ni les registres FP.
; ============================================================================

E_BUSY_SYNC	= -16

; x0 = &mutex : attend qu'il soit libre et le prend
mtx_lock:
	mov	x9,x0
	mov	w10,1
.t:	ldaxr	w11,[x9]			; libre : 0 -> 1, sans appel
	cbnz	w11,.lent
	stxr	w12,w10,[x9]
	cbnz	w12,.t
	ret
.lent:	clrex
.x:	mov	w10,2				; pris : on le marque 2 (attente) ;
.xc:	ldaxr	w11,[x9]			; si on l'a trouve libre, il est a
	stxr	w12,w10,[x9]			; nous (marque 2 : au pire un reveil
	cbnz	w12,.xc				; de trop au rendu)
	cbz	w11,.pris
	mov	x0,x9
	mov	x1,2
	mov	x2,0
	mov	x8,SYS_FUTEX_WAIT
	svc	0
	b	.x
.pris:	ret

; x0 = &mutex -> x0 = 0 (pris), E_BUSY (deja pris)
mtx_trylock:
	mov	w10,1
.t:	ldaxr	w11,[x0]
	cbnz	w11,.occupe
	stxr	w12,w10,[x0]
	cbnz	w12,.t
	mov	x0,0
	ret
.occupe:
	clrex
	mov	x0,E_BUSY_SYNC
	ret

; x0 = &mutex : le rend (reveille un thread s'il en attend un)
mtx_unlock:
	mov	x9,x0
.u:	ldxr	w11,[x9]
	stlxr	w12,wzr,[x9]
	cbnz	w12,.u
	cmp	w11,2
	b.ne	.fin
	mov	x0,x9
	mov	x1,1
	mov	x8,SYS_FUTEX_WAKE
	svc	0
.fin:	ret

; x0 = &condition, x1 = &mutex (pris) : rend le mutex, attend un signal, le
; reprend -> x0 = 0
cnd_wait:
	mov	x2,0
; x0 = &condition, x1 = &mutex (pris), x2 = attente ms (0 : sans fin) ->
; x0 = 0 (signal, ou reveil parasite), E_TIMEDOUT ; le mutex est repris
cnd_timedwait:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldar	w22,[x19]			; compteur avant de rendre le mutex
	mov	x0,x20
	bl	mtx_unlock
	mov	x0,x19
	mov	w1,w22
	mov	x2,x21
	mov	x8,SYS_FUTEX_WAIT
	svc	0
	mov	x21,0
	cmn	x0,-E_TIMEDOUT
	b.ne	.r
	mov	x21,E_TIMEDOUT
.r:	mov	x0,x20
	bl	mtx_lock
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; x0 = &condition : reveille un thread qui attend
cnd_signal:
	mov	x1,1
	b	cnd_wake
; x0 = &condition : reveille tous ceux qui attendent
cnd_broadcast:
	mov	x1,0x7FFFFFFF
; x0 = &condition, x1 = nombre
cnd_wake:
	mov	x9,x0
.a:	ldaxr	w11,[x9]
	add	w11,w11,1
	stlxr	w12,w11,[x9]
	cbnz	w12,.a
	mov	x0,x9
	mov	x8,SYS_FUTEX_WAKE
	svc	0
	ret
