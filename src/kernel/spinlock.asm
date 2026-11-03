; ============================================================================
;  spinlock.asm - verrous tournants SMP (LL/SC : le Cortex-A72 n'a pas LSE)
;
;  Un verrou = un mot de 32 bits (0 libre, 1 pris), en RAM cacheable
;  (les exclusifs ne fonctionnent pas sur de la memoire non cacheable/Device).
;  Attente economique : wfe, reveille par l'evenement emis lors de la
;  liberation (stlr vide le moniteur exclusif local des autres coeurs).
;
;  Regle du noyau : tout verrou partage avec un chemin d'IRQ ou avec
;  l'ordonnanceur se prend avec spin_lock_irqsave (pas de preemption pendant
;  qu'on le tient, donc pas d'interblocage sur un meme coeur).
; ============================================================================

; x0 = adresse du verrou        (ecrase w1, w2)
spin_lock:
	mov	w2,1
	sevl
.wait:	wfe
.try:	ldaxr	w1,[x0]
	cbnz	w1,.wait
	stxr	w1,w2,[x0]
	cbnz	w1,.try
	ret

; x0 = adresse du verrou
spin_unlock:
	stlr	wzr,[x0]
	ret

; x0 = adresse du verrou -> x0 = DAIF d'avant (a rendre a spin_unlock_irqrestore)
spin_lock_irqsave:
	mrs	x3,daif
	msr	daifset,2
	mov	w2,1
	sevl
.wait:	wfe
.try:	ldaxr	w1,[x0]
	cbnz	w1,.wait
	stxr	w1,w2,[x0]
	cbnz	w1,.try
	mov	x0,x3
	ret

; x0 = adresse du verrou, x1 = DAIF rendu par spin_lock_irqsave
spin_unlock_irqrestore:
	stlr	wzr,[x0]
	msr	daif,x1
	ret

; x0 = adresse d'un compteur 64 bits -> x0 = nouvelle valeur (increment atomique)
atomic_inc:
.try:	ldaxr	x1,[x0]
	add	x1,x1,1
	stlxr	w2,x1,[x0]
	cbnz	w2,.try
	mov	x0,x1
	ret

; ---------------------------------------------------------------------------
;  Mutex noyau "dormant" : attente en cedant le processeur (thread_yield).
;  Pour les sections longues (systeme de fichiers, E/S) ou l'on ne peut pas
;  couper les IRQ. Un mutex = un mot de 32 bits (0 libre, 1 pris).
; ---------------------------------------------------------------------------

; x0 = adresse du mutex
mutex_lock:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
.try:	ldaxr	w1,[x19]
	cbnz	w1,.busy
	mov	w2,1
	stxr	w1,w2,[x19]
	cbnz	w1,.try
	ldr	x19,[sp],16
	epilogue
.busy:	clrex
	bl	thread_yield
	b	.try

; x0 = adresse du mutex
mutex_unlock:
	stlr	wzr,[x0]
	ret
