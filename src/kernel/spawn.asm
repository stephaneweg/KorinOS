; ============================================================================
;  spawn.asm - lancement d'un programme hors du noyau (docs/FICHIERS.md,
;  etapes 4 et 5) : appels 97 spawn_image, 98 proc_flags et 24 wait. Le
;  chemin, la lecture de l'executable et les tubes sont chez AppKit et vfs ;
;  le noyau recoit l'image, verifie les droits et cree le process
;  (proc_spawn_elf). Il ne lit plus aucun fichier.
; ============================================================================

; ---------------------------------------------------------------------------
;  Lancement hors du noyau (docs/FICHIERS.md, etape 4) : le programme (AppKit)
;  a lu l'executable par vfs et les services de fichiers ; le noyau ne voit
;  plus de chemin.
; ---------------------------------------------------------------------------
SPAWN_IMAGE_MAX	= 64*1024*1024
SPAWN_RIGHTS	= 1			; options : droits de SD0:/sys demandes
SPAWN_PARAMS	= 48			; nom, arguments, options, entree, sortie, erreur

; --- SYS_SPAWN_IMAGE spawn_image(image, taille, params) -> pid ------------------------------
; params (48 o) : +0 nom du process (0 : aucun), +8 ligne d'arguments (0 :
; aucune), +16 options (bit 0 : droits), +24 entree, +32 sortie, +40 erreur
; standard (poignees de tubes de vfs : 0 = celle du pere, -1 = aucune ; vfs
; les verifie a l'avis de naissance). Droits (D9) : accordes seulement si
; l'appelant les a lui-meme ; demandes sans les avoir : E_PERM (AppKit passe
; alors par init2). Image recopiee dans des pages du noyau, puis
; proc_spawn_elf (le dossier courant est chez vfs : avis de naissance).
sys_spawn_image:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,64			; nom (32 o), entrees / sorties (24 o)
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x20,64
	b.lo	.inval
	movl	x0,SPAWN_IMAGE_MAX
	cmp	x20,x0
	b.hi	.inval
	check_user x21,SPAWN_PARAMS,.fault
	check_user x19,x20,.fault
	ldp	x0,x1,[x21,24]			; entree, sortie, erreur
	stp	x0,x1,[sp,32]
	ldr	x0,[x21,40]
	str	x0,[sp,48]
	mov	x25,0				; droits
	ldr	x0,[x21,16]
	tbz	x0,0,.nodroits
	current_proc
	ldr	x0,[x1,P_DROITS]
	cbz	x0,.perm
	mov	x25,1
.nodroits:
	strb	wzr,[sp]			; nom
	ldr	x0,[x21]
	cbz	x0,.nonom
	mov	x1,sp
	mov	x2,32
	bl	copy_str_from_user
	tbnz	x0,63,.out
.nonom:	mov	x23,0				; arguments (une page)
	ldr	x22,[x21,8]
	cbz	x22,.noargs
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x23,x0
	mov	x0,x22
	mov	x1,x23
	mov	x2,ARGS_MAX
	bl	copy_str_from_user
	mov	x24,x0
	tbnz	x0,63,.libre
.noargs:
	add	x0,x20,0xFFF			; l'image, dans des pages du noyau
	lsr	x26,x0,12
	mov	x0,x26
	bl	pmm_alloc_n
	mov	x24,E_NOMEM
	cbz	x0,.libre
	mov	x22,x0
	mov	x1,x19
	mov	x2,x20
	bl	mem_copy
	mov	x0,x22
	mov	x1,x20
	mov	x2,sp
	mov	x3,x23
	mov	x4,0				; (pas d'image prechargee)
	mov	x5,0				; (pas de tube du noyau)
	mov	x6,x25
	add	x7,sp,32
	bl	proc_spawn_elf
	mov	x24,x0
	cbnz	x24,.lance
	mov	x24,E_INVAL			; ELF refuse
.lance:	mov	x0,x22
	mov	x1,x26
	bl	pmm_free_n
.libre:	cbz	x23,.ret
	mov	x0,x23
	bl	pmm_free
.ret:	mov	x0,x24
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,64
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_WAIT wait(pid) -> code de sortie ---------------------------------------------
; Le thread attend dans l'ordonnanceur (TS_BLOCKED, TH_WAIT = WAIT_KEY | pid) :
; le faucheur reveille les threads du pere quand le fils est retire de la
; table (wait_reveil) ; un autre process qui attend ce pid regarde de
; lui-meme toutes les WAIT_POLL_MS. Pas interrompu par un signal (bit 63 :
; sig_raise le laisse, comme un join).
WAIT_KEY	= 3 shl 62			; (JOIN_KEY = 1 shl 63 : distinct)
WAIT_POLL_MS	= 20
sys_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
.w:	mov	x0,x19				; (emplacement repris : code perdu)
	bl	proc_slot
	cbz	x0,.inval
	ldr	x1,[x0,PT_PCB]
	cbz	x1,.gone
	mov	x0,WAIT_POLL_MS
	bl	svc_echeance
	mov	x21,x0
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,x19				; encore la ? (sous futex_lock : le
	bl	proc_slot			;  faucheur reveille apres l'avoir
	cbz	x0,.unl				;  retire, sous ce verrou)
	ldr	x1,[x0,PT_PCB]
	cbz	x1,.unl
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x1,[x0,TH_KILL]			; tue : ne pas s'endormir
	cbnz	x1,.intr
	movl	x1,WAIT_KEY
	orr	x1,x1,x19
	str	x1,[x0,TH_WAIT]
	str	x21,[x0,TH_WAKE]
	mov	x1,TS_BLOCKED
	bl	thread_park
	adr	x0,futex_lock
	bl	spin_unlock			; IRQ toujours masquees
	bl	schedule
	adr	x0,futex_lock
	bl	spin_lock
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	str	xzr,[x0,TH_WAIT]
.unl:	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	b	.w
.intr:	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,E_INTR
	b	.out
.gone:	ldr	x0,[x0,PT_CODE]
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB d'un process fini (faucheur : retire de la table, PT_CODE range) :
; les threads de son pere qui l'attendent (SYS_WAIT) repartent
wait_reveil:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x20,[x0,P_PID]
	ldr	x0,[x0,P_PARENT]
	cbz	x0,.out
	bl	proc_lookup			; (son PCB tient : c'est le faucheur
	cbz	x0,.out				;  qui les rend)
	mov	x19,x0
	movl	x0,WAIT_KEY
	orr	x20,x20,x0
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	add	x0,x19,P_LOCK			; (la liste des threads ne bouge pas)
	bl	spin_lock
	ldr	x21,[x19,P_THREADS]
.t:	cbz	x21,.fin
	ldr	x0,[x21,TH_STATE]
	cmp	x0,TS_BLOCKED
	b.ne	.n
	ldr	x0,[x21,TH_WAIT]
	cmp	x0,x20
	b.ne	.n
	str	xzr,[x21,TH_WAIT]
	str	xzr,[x21,TH_STATE]		; TS_READY
	ldr	x0,[x21,TH_CPU]
	bl	cpu_kick
.n:	ldr	x21,[x21,TH_PNEXT]
	b	.t
.fin:	add	x0,x19,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_GETPPID getppid() -> pid du pere (P_PARENT) ; 1 (init) si le pere est
; fini ou si le noyau l'a lance
sys_getppid:
	prologue
	current_proc
	ldr	x0,[x1,P_PARENT]
	cbz	x0,.init
	mov	x3,x0
	bl	proc_lookup
	cbz	x0,.init
	mov	x0,x3
	b	.out
.init:	mov	x0,1
.out:	epilogue

; --- SYS_PROC_FLAGS proc_flags() -> bits 0-2 : entree, sortie, erreur standard dans un
; tube de vfs (P_STDIO) ; bit 8 : droits de SD0:/sys (P_DROITS)
sys_proc_flags:
	current_proc
	ldr	x0,[x1,P_STDIO]
	ldr	x2,[x1,P_DROITS]
	cbz	x2,.out
	orr	x0,x0,0x100
.out:	ret
