; ============================================================================
;  ws.asm - mecanismes du serveur graphique en espace utilisateur
;  (chantier 4, docs/CHANTIER4-GUI.md ; modele d'Onyx : kapi_ws_ctl)
;
;  Le noyau ne fait que fournir des mecanismes ; fenetres, compositeur et
;  bureau sont dans un process (Elegant) :
;  * tampons partages (pixels) : comptes par references, a la meme adresse
;    dans tous les process (BUF_VA_BASE + n x BUF_SLOT), mappage reserve au
;    createur et au process qu'il autorise ;
;  * canal du serveur : le service nomme « ws » (svc.asm) ; un programme
;    envoie une requete (message de 512 octets) et attend la reponse ; le
;    serveur recoit le pid de l'appelant ; le noyau previent le serveur de la
;    fin d'un process (op SVC_OP_FIN = 0xFFFF, a = pid) ;
;  * evenements : une file par process (64 x 32 octets), remplie par le
;    serveur, lue par le programme ;
;  * ecran et entrees : le serveur mappe le framebuffer et lit la file des
;    entrees brutes ; tant qu'il a l'ecran, l'interface du noyau se tait, et
;    la reprend s'il disparait.
;  Un programme qui attend ses evenements dort (TH_WAIT = son PCB) : ws_post
;  le reveille.
;
;  Message (512 octets, ws_call / ws_next / ws_reply) :
;    +0 op (requete) / resultat (reponse), +8 a, +16 b, +24 c, +32 d,
;    +40 longueur des donnees, +44 pid de l'appelant (rempli par le noyau),
;    +48 donnees (464 octets)
; ============================================================================

BUF_MAX		= 128			; (P_BUFS : masque de 128 bits)
BUF_VA_BASE	= 0x6800000000		; L1[416..423] : 128 x 64 Mo
BUF_SLOT	= 0x4000000		; (une fenetre plein ecran et ses cadres)
BUF_SIZE_MAX	= 0x4000000

; --- tampon partage (BF_SIZE octets) ------------------------------------------
BF_USED		= 0
BF_PHYS		= 8
BF_BYTES	= 16
BF_REFS		= 24
BF_OWNER	= 32			; pid du createur
BF_GRANT	= 40			; pid autorise (0 : aucun)
BF_NOCACHE	= 48			; 1 : vue non cachee (BUF_NOCACHE)
BF_SIZE		= 56

WS_MSG		= 512
WS_MSG_DATA	= 48


WS_EVQ_N	= 64			; evenements par file
WS_EV		= 32
WS_EVQ_HDR	= 16			; tete, queue

FB_USER_VA	= 0x7000000000		; framebuffer dans le serveur

E_BUSY		= -16
E_PERM		= -1

; attributs : framebuffer (memoire du VideoCore, non cacheable), jamais libere
VM_RW_NC	= PTE_PAGE or (MAIR_NC shl 2) or PTE_SH_INNER or PTE_AF or PTE_NG \
		  or PTE_AP_EL0 or PTE_PXN or PTE_UXN or PTE_SHARED

macro ws_lock_take {
	adr	x0,ws_lock
	bl	spin_lock_irqsave
	adr	x1,ws_lock_daif
	str	x0,[x1]
}
macro ws_lock_drop {
	adr	x0,ws_lock_daif
	ldr	x1,[x0]
	adr	x0,ws_lock
	bl	spin_unlock_irqrestore
}

; -> x0 = PCB courant
ws_cur_proc:
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x0,[x0,TH_PROC]
	ret

; x0 = pid -> x0 = PCB vivant (0 sinon)
ws_proc_of:
	sub	x1,x0,1
	cmp	x1,MAX_PROCS
	b.hs	.none
	adr	x2,proc_table
	ldr	x0,[x2,x1,lsl 3]
	ret
.none:	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  Tampons partages
; ---------------------------------------------------------------------------

; --- buf_create(taille) -> numero (mappe chez l'appelant a
;        BUF_VA_BASE + numero x BUF_SLOT), < 0 si erreur ----------------------
; (bit 62 de la taille, BUF_LOW : pages sous 1 Go, pour un tampon dont
; l'adresse physique irait au materiel ; sinon zone haute de preference)
; (bit 61, BUF_NOCACHE : vue non cachee de partout, pour un tampon que le GPU lit et ecrit sans
; maintenance du cache, comme les BO de Linux en write-combine : docs/MESA.md)
BUF_LOW		= 1 shl 62
BUF_NOCACHE	= 1 shl 61
sys_buf_create:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ubfx	x22,x0,61,2			; bit 1 : BUF_LOW, bit 0 : BUF_NOCACHE
	and	x0,x0,not (BUF_LOW or BUF_NOCACHE)
	cbz	x0,.inval
	movl	x1,BUF_SIZE_MAX
	cmp	x0,x1
	b.hi	.inval
	add	x20,x0,0xFFF			; pages entieres
	and	x20,x20,not 0xFFF
	adr	x0,buf_mutex
	bl	mutex_lock
	mov	x19,0				; place libre
.find:	cmp	x19,BUF_MAX
	b.hs	.full
	adr	x0,buf_table
	mov	x1,BF_SIZE
	madd	x21,x19,x1,x0
	ldr	x0,[x21,BF_USED]
	cbz	x0,.got
	add	x19,x19,1
	b	.find
.got:	lsr	x0,x20,12
	tbz	x22,1,.any
	bl	pmm_alloc_low_n
	b	.alloc
.any:	bl	pmm_alloc_n
.alloc:	cbz	x0,.nomem
	str	x0,[x21,BF_PHYS]
	mov	x1,x20
	bl	mem_zero
	and	x0,x22,1
	str	x0,[x21,BF_NOCACHE]
	cbz	x0,.zeroed			; hors des caches : la vue non cachee
	ldr	x0,[x21,BF_PHYS]		; doit lire ces zeros
	mov	x1,x20
	bl	dcache_flush_range
.zeroed:
	str	x20,[x21,BF_BYTES]
	str	xzr,[x21,BF_REFS]
	str	xzr,[x21,BF_GRANT]
	bl	ws_cur_proc
	mov	x22,x0
	ldr	x0,[x22,P_PID]
	str	x0,[x21,BF_OWNER]
	mov	x0,1
	str	x0,[x21,BF_USED]
	mov	x0,x22				; mappage chez le createur
	mov	x1,x19
	bl	buf_map_into
	cbz	x0,.nomem
	mov	x0,x19
	b	.unlock
.full:
.nomem:	mov	x0,E_NOMEM
.unlock:
	mov	x19,x0
	adr	x0,buf_mutex
	bl	mutex_unlock
	mov	x0,x19
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- buf_grant(numero, pid) : autorise ce process a mapper le tampon (-1 : tous)
sys_buf_grant:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x19,BUF_MAX
	b.hs	.inval
	adr	x0,buf_table
	mov	x1,BF_SIZE
	madd	x19,x19,x1,x0
	ldr	x0,[x19,BF_USED]
	cbz	x0,.inval
	bl	ws_cur_proc			; le createur seulement
	ldr	x0,[x0,P_PID]
	ldr	x1,[x19,BF_OWNER]
	cmp	x0,x1
	b.ne	.perm
	str	x20,[x19,BF_GRANT]
	mov	x0,0
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- buf_map(numero) -> adresse du tampon chez l'appelant ------------------
sys_buf_map:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	cmp	x19,BUF_MAX
	b.hs	.inval
	adr	x0,buf_mutex
	bl	mutex_lock
	adr	x0,buf_table
	mov	x1,BF_SIZE
	madd	x21,x19,x1,x0
	mov	x20,E_INVAL
	ldr	x0,[x21,BF_USED]
	cbz	x0,.unlock
	bl	ws_cur_proc
	mov	x22,x0
	ldr	x0,[x22,P_PID]			; createur ou autorise
	ldr	x1,[x21,BF_OWNER]
	cmp	x0,x1
	b.eq	.ok
	ldr	x1,[x21,BF_GRANT]
	cmn	x1,1				; (-1 : tout process)
	b.eq	.ok
	cmp	x0,x1
	mov	x20,E_PERM
	b.ne	.unlock
.ok:	mov	x0,x22
	mov	x1,x19
	bl	buf_map_into
	mov	x20,E_NOMEM
	cbz	x0,.unlock
	movl	x20,BUF_VA_BASE
	mov	x1,BUF_SLOT
	madd	x20,x19,x1,x20
.unlock:
	adr	x0,buf_mutex
	bl	mutex_unlock
	mov	x0,x20
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- buf_free(numero) : demappe chez l'appelant ----------------------------
sys_buf_free:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x19,BUF_MAX
	b.hs	.inval
	adr	x0,buf_mutex
	bl	mutex_lock
	bl	ws_cur_proc
	mov	x1,x19
	mov	x2,1				; demapper
	bl	buf_release
	adr	x0,buf_mutex
	bl	mutex_unlock
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- buf_info(numero, sortie 32 o) -> 0 : adresse physique, octets, createur,
;        autorise (BI_*) ; pour un pilote qui donne le tampon au materiel (le
;        GPU V3D) sans le mapper. Ouvert a tous : le pilote verifie lui-meme a
;        qui est le tampon (BI_OWNER) ---------------------------------------------
sys_buf_info:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x19,BUF_MAX
	b.hs	.inval
	check_user x20,32,.fault
	adr	x0,buf_mutex
	bl	mutex_lock
	adr	x0,buf_table
	mov	x1,BF_SIZE
	madd	x1,x19,x1,x0
	ldr	x0,[x1,BF_USED]
	cbz	x0,.gone
	ldr	x2,[x1,BF_PHYS]
	ldr	x3,[x1,BF_BYTES]
	ldr	x4,[x1,BF_OWNER]
	ldr	x5,[x1,BF_GRANT]
	stp	x2,x3,[x20]
	stp	x4,x5,[x20,16]
	adr	x0,buf_mutex
	bl	mutex_unlock
	mov	x0,0
	b	.out
.gone:	adr	x0,buf_mutex
	bl	mutex_unlock
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = numero : mappe le tampon (s'il ne l'est pas deja) -> x0 = 1
; (sous buf_mutex)
buf_map_into:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	lsr	x0,x20,6			; mot du masque (128 bits)
	add	x22,x19,P_BUFS
	add	x22,x22,x0,lsl 3
	ldr	x0,[x22]
	lsr	x0,x0,x20			; (decalage modulo 64)
	tbnz	x0,0,.ok			; deja mappe
	adr	x0,buf_table
	mov	x1,BF_SIZE
	madd	x21,x20,x1,x0
	mov	x0,x19
	movl	x1,BUF_VA_BASE
	mov	x2,BUF_SLOT
	madd	x1,x20,x2,x1
	ldr	x2,[x21,BF_PHYS]
	ldr	x3,[x21,BF_BYTES]
	movl	x4,VM_RW or PTE_SHARED
	ldr	x5,[x21,BF_NOCACHE]
	cbz	x5,.attr
	movl	x4,VM_RW_NC
.attr:	bl	vm_map_phys
	cbz	x0,.out
	ldr	x0,[x21,BF_REFS]
	add	x0,x0,1
	str	x0,[x21,BF_REFS]
	ldr	x0,[x22]
	mov	x1,1
	lsl	x1,x1,x20
	orr	x0,x0,x1
	str	x0,[x22]
	dsb	ish
.ok:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = numero, x2 = 1 demapper (0 : process en fin de vie, son espace
; va disparaitre) : une reference de moins, pages liberees a la derniere
; (sous buf_mutex)
buf_release:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x22,x2
	lsr	x0,x20,6			; mot du masque (128 bits)
	add	x3,x19,P_BUFS
	add	x3,x3,x0,lsl 3
	ldr	x0,[x3]
	lsr	x4,x0,x20			; (decalage modulo 64)
	tbz	x4,0,.out			; pas mappe chez lui
	mov	x1,1
	lsl	x1,x1,x20
	bic	x0,x0,x1
	str	x0,[x3]
	adr	x0,buf_table
	mov	x1,BF_SIZE
	madd	x21,x20,x1,x0
	cbz	x22,.ref
	mov	x0,x19				; demappage (les pages ne sont pas a lui)
	movl	x1,BUF_VA_BASE
	mov	x2,BUF_SLOT
	madd	x1,x20,x2,x1
	ldr	x2,[x21,BF_BYTES]
	mov	x3,0
	bl	vm_unmap_range
.ref:	ldr	x0,[x21,BF_REFS]
	subs	x0,x0,1
	str	x0,[x21,BF_REFS]
	b.ne	.out
	ldr	x0,[x21,BF_PHYS]		; derniere reference : liberation
	ldr	x1,[x21,BF_BYTES]
	lsr	x1,x1,12
	bl	pmm_free_n
	str	xzr,[x21,BF_USED]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Canal du serveur
; ---------------------------------------------------------------------------

; --- ws_register() : l'appelant devient le serveur graphique --------------
; (le service nomme « ws », prevenu des fins de process : svc.asm)
sys_ws_register:
	prologue
	adr	x0,ws_nom
	mov	x1,SVF_FINS
	bl	svc_register_k
	tbnz	x0,63,.out
	adr	x1,ws_canal
	str	x0,[x1]
	current_proc
	ldr	x0,[x1,P_PID]
	adr	x1,ws_server
	str	x0,[x1]
	mov	x0,0
.out:	epilogue

; --- ws_call(message) -> resultat (le message recoit la reponse) ----------
sys_ws_call:
	prologue
	mov	x1,x0
	adr	x0,ws_server
	ldr	x2,[x0]
	cbz	x2,.nodev			; pas de serveur
	adr	x0,ws_canal
	ldr	x0,[x0]
	bl	sys_svc_call
	cmp	x0,E_PIPE			; serveur parti : comme s'il n'y en
	b.eq	.nodev				; avait pas (UIKit reessaie)
	cmp	x0,E_NOENT
	b.ne	.out
.nodev:	mov	x0,E_NODEV
.out:	epilogue

; --- ws_signal(bits) : svc_signal vers le serveur (le programme n'a pas son
;        canal ; bits WS_SIG_*, docs/ABI.md) -------------------------------------
sys_ws_signal:
	mov	x1,x0
	adr	x0,ws_server
	ldr	x0,[x0]
	cbz	x0,.nodev
	adr	x0,ws_canal
	ldr	x0,[x0]
	b	sys_svc_signal
.nodev:	mov	x0,E_NODEV
	ret

; --- ws_next(message, attente ms) -> numero de requete (0 : rien) ---------
sys_ws_next:
	mov	x2,x1
	mov	x1,x0
	adr	x0,ws_canal
	ldr	x0,[x0]
	b	sys_svc_next

; --- ws_reply(numero, message) : reponse a une requete prise --------------
sys_ws_reply:
	b	sys_svc_reply

; -> x0 = 1 si l'appelant est le serveur
ws_is_server:
	prologue
	bl	ws_cur_proc
	ldr	x0,[x0,P_PID]
	adr	x1,ws_server
	ldr	x1,[x1]
	cmp	x0,x1
	cset	x0,eq
	epilogue

; ---------------------------------------------------------------------------
;  Evenements des programmes
; ---------------------------------------------------------------------------

; --- ws_post(pid, evenement 32 o) : depose dans la file du programme -------
sys_ws_post:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,WS_EV
	mov	x19,x0
	mov	x20,x1
	check_user x20,WS_EV,.fault
	bl	ws_is_server
	cbz	x0,.perm
	mov	x0,sp				; copie (l'espace du serveur reste actif)
	mov	x1,x20
	mov	x2,WS_EV
	bl	mem_copy
	mov	x0,x19
	mov	x1,sp
	bl	ws_post_k
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,WS_EV
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid, x1 = evenement (noyau) -> x0 = 0, < 0 si erreur (file pleine :
; evenement perdu)
ws_post_k:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	bl	ws_proc_of
	cbz	x0,.inval
	mov	x19,x0
	ldr	x21,[x19,P_EVQ]			; file creee a la premiere utilisation
	cbnz	x21,.have
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x21,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	str	x21,[x19,P_EVQ]
.have:	ws_lock_take
	ldp	x1,x2,[x21]			; tete (ecriture), queue (lecture)
	sub	x3,x1,x2
	cmp	x3,WS_EVQ_N
	b.hs	.full
	and	x3,x1,WS_EVQ_N-1
	add	x0,x21,WS_EVQ_HDR
	add	x0,x0,x3,lsl 5
	ldp	x4,x5,[x20]
	stp	x4,x5,[x0]
	ldp	x4,x5,[x20,16]
	stp	x4,x5,[x0,16]
	add	x1,x1,1
	str	x1,[x21]
	mov	x22,0
	b	.unlock
.full:	mov	x22,E_AGAIN
.unlock:
	ws_lock_drop
	mov	x0,x19
	bl	ws_wake			; (file pleine aussi : il a de quoi lire)
	mov	x0,x22
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB : reveille ceux de ses threads qui attendent un evenement
ws_wake:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	add	x0,x19,P_LOCK			; (comme svc_reveil_service)
	bl	spin_lock_irqsave
	mov	x20,x0
	ldr	x21,[x19,P_THREADS]
.t:	cbz	x21,.fin
	ldr	x1,[x21,TH_STATE]
	cmp	x1,TS_SLEEPING
	b.ne	.n
	ldr	x1,[x21,TH_WAIT]
	cmp	x1,x19
	b.ne	.n
	str	xzr,[x21,TH_WAIT]
	str	xzr,[x21,TH_STATE]		; TS_READY
	ldr	x0,[x21,TH_CPU]
	bl	cpu_kick
.n:	ldr	x21,[x21,TH_PNEXT]
	b	.t
.fin:	add	x0,x19,P_LOCK
	mov	x1,x20
	bl	spin_unlock_irqrestore
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- ev_wait(evenement 32 o, attente ms) -> type (0 : aucun) --------------
; (le thread dort, sous ws_lock tant que la file est vide : ws_post le reveille)
sys_ev_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	check_user x19,WS_EV,.fault
	cmp	x20,0
	b.le	.now				; attente 0 : regarder seulement
	mov	x0,x20
	bl	svc_echeance
	mov	x20,x0
	b	.try
.now:	mov	x20,0
.try:	bl	ws_cur_proc
	mov	x22,x0
	ws_lock_take
	ldr	x21,[x22,P_EVQ]
	cbz	x21,.empty
	ldp	x1,x2,[x21]
	cmp	x1,x2
	b.eq	.empty
	and	x3,x2,WS_EVQ_N-1
	add	x0,x21,WS_EVQ_HDR
	add	x0,x0,x3,lsl 5
	ldp	x4,x5,[x0]
	stp	x4,x5,[x19]
	ldp	x4,x5,[x0,16]
	stp	x4,x5,[x19,16]
	add	x2,x2,1
	str	x2,[x21,8]
	ws_lock_drop
	ldr	w0,[x19]			; type
	b	.out
.empty:	mrs	x0,cntpct_el0
	cmp	x0,x20
	b.hs	.none
	this_cpu x1
	ldr	x2,[x1,PC_CURRENT]
	ldr	x0,[x2,TH_KILL]			; a tuer : pas de sommeil
	cbnz	x0,.none
	ldr	x0,[x2,TH_SIGPEND]		; un signal : il sera livre au retour
	ldr	x1,[x2,TH_SIGMASK]
	bic	x0,x0,x1
	cbnz	x0,.none
	str	x22,[x2,TH_WAIT]
	str	x20,[x2,TH_WAKE]
	mov	x0,x2
	mov	x1,TS_SLEEPING
	bl	thread_park
	adr	x0,ws_lock_daif
	ldr	x21,[x0]
	adr	x0,ws_lock
	bl	spin_unlock			; IRQ toujours masquees
	bl	schedule
	this_cpu x1
	ldr	x2,[x1,PC_CURRENT]
	str	xzr,[x2,TH_WAIT]
	msr	daif,x21
	b	.try
.none:	ws_lock_drop
	mov	x0,0
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Ecran et entrees (serveur seulement)
; ---------------------------------------------------------------------------

; --- ws_display(infos 40 o) : le serveur prend l'ecran ---------------------
; infos <- adresse du framebuffer chez le serveur, largeur, hauteur, pas,
; adresse physique (ARM) du framebuffer (le DMA 2D d'Elegant)
sys_ws_display:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	check_user x19,40,.fault
	bl	ws_is_server
	cbz	x0,.perm
	bl	ws_cur_proc
	mov	x20,x0
	ldr	x0,[x20,P_FLAGS_WS]
	cbnz	x0,.info			; deja mappe
	mov	x0,x20
	bl	ws_fb_map
	cbz	x0,.nomem
	mov	x0,1
	str	x0,[x20,P_FLAGS_WS]
.info:	adr	x0,ws_display_on		; la console du noyau se tait
	mov	w1,1
	str	w1,[x0]
	adr	x0,con_ready
	str	xzr,[x0]
	mov	x0,x19
	bl	ws_fb_info
	mov	x0,0
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- ws_mode(largeur, hauteur, infos 40 o) : nouvelle resolution, a chaud ---
; Le serveur a fini son transfert vers l'ecran (present_wait d'Elegant) et
; attend ici : l'ancien framebuffer quitte son espace, le firmware en donne un
; autre, mappe a la meme adresse. Infos : comme ws_display. Taille refusee par
; le firmware : la precedente est redemandee, infos remplies, E_IO.
sys_ws_mode:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x19,x2
	check_user x19,40,.fault
	bl	ws_is_server
	cbz	x0,.perm
	bl	ws_cur_proc
	mov	x20,x0
	ldr	x0,[x20,P_FLAGS_WS]		; (apres ws_display)
	cbz	x0,.inval
	mov	x0,x21
	mov	x1,x22
	bl	fb_size_ok
	cbz	x0,.inval
	bl	ws_fb_bytes
	mov	x2,x0
	mov	x0,x20
	movl	x1,FB_USER_VA
	mov	x3,0				; (memoire du VideoCore)
	bl	vm_unmap_range
	adr	x0,fb_w
	ldp	x0,x1,[x0]
	stp	x0,x1,[sp,-16]!
	mov	x0,x21
	mov	x1,x22
	bl	fb_set
	mov	x21,x0
	ldp	x0,x1,[sp],16
	cbnz	x21,.map
	bl	fb_set				; refusee : la precedente
.map:	mov	x0,x20
	bl	ws_fb_map
	cbz	x0,.nomem
	mov	x0,x19
	bl	ws_fb_info
	mov	x0,E_IO
	cmp	x21,0
	csel	x0,x0,xzr,eq
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = octets du framebuffer, pages entieres
ws_fb_bytes:
	adr	x0,fb_pitch
	ldr	x0,[x0]
	adr	x1,fb_h
	ldr	x1,[x1]
	mul	x0,x0,x1
	adr	x1,fb_base
	ldr	x1,[x1]
	and	x1,x1,0xFFF
	add	x0,x0,x1
	add	x0,x0,0xFFF
	and	x0,x0,not 0xFFF
	ret

; x0 = PCB du serveur : le framebuffer mappe a FB_USER_VA -> x0 = 1 si ok
ws_fb_map:
	prologue
	str	x0,[sp,-16]!
	bl	ws_fb_bytes
	mov	x3,x0
	ldr	x0,[sp],16
	movl	x1,FB_USER_VA
	adr	x2,fb_base
	ldr	x2,[x2]
	and	x2,x2,not 0xFFF
	movl	x4,VM_RW_NC
	bl	vm_map_phys
	epilogue

; x0 = infos (verifiees) <- adresse, largeur, hauteur, pas, adresse physique
ws_fb_info:
	adr	x1,fb_base
	ldr	x1,[x1]
	and	x2,x1,0xFFF
	movl	x3,FB_USER_VA
	add	x2,x2,x3
	str	x2,[x0]
	str	x1,[x0,32]			; +32 : adresse physique (DMA d'Elegant)
	adr	x1,fb_w
	ldp	x1,x2,[x1]
	stp	x1,x2,[x0,8]
	adr	x1,fb_pitch
	ldr	x1,[x1]
	str	x1,[x0,24]
	ret

; ---------------------------------------------------------------------------
;  Fin d'un process (faucheur, proc_on_exit)
; ---------------------------------------------------------------------------

; x0 = PCB du process termine
ws_proc_exit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,P_PID]
	adr	x0,buf_mutex			; tampons : references rendues
	bl	mutex_lock
	mov	x21,0
.b:	cmp	x21,BUF_MAX
	b.hs	.b_done
	mov	x0,x19
	mov	x1,x21
	mov	x2,0				; son espace va disparaitre
	bl	buf_release
	add	x21,x21,1
	b	.b
.b_done:
	adr	x0,buf_mutex
	bl	mutex_unlock
	ldr	x0,[x19,P_EVQ]			; file d'evenements
	cbz	x0,.evq
	bl	pmm_free
	str	xzr,[x19,P_EVQ]
.evq:	ws_lock_take
	adr	x0,ws_server
	ldr	x1,[x0]
	cmp	x1,x20
	b.ne	.unlock
.server_gone:					; le serveur disparait (ses requetes :
	str	xzr,[x0]			; svc_proc_exit) : ecran rendu au noyau
	adr	x0,ws_display_on
	ldr	w21,[x0]
	str	wzr,[x0]
	ws_lock_drop
	cbz	w21,.out
	bl	con_init			; la console reprend l'ecran
	adr	x0,msg_ws_gone
	bl	kputs
	b	.out
.unlock:
	ws_lock_drop
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

msg_ws_gone	db 'Interface        : le serveur graphique s''est arrete',10,0
		align 4

; ---------------------------------------------------------------------------
;  Appels restes au noyau : ecran, presse-papiers
; ---------------------------------------------------------------------------
CLIP_MAX	= 65536
TASKBAR_H	= 34			; barre d'Elegant (bureau)

; --- screen_info(tampon) : largeur, hauteur, hauteur de barre --------------
sys_screen_info:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	check_user x19,24,.fault
	adr	x0,fb_w
	ldp	x0,x1,[x0]
	stp	x0,x1,[x19]
	mov	x0,TASKBAR_H
	str	x0,[x19,16]
	mov	x0,0
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldr	x19,[sp],16
	epilogue

; --- clip_set(tampon, longueur) / clip_get(tampon, max) -> longueur ------
sys_clip_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,CLIP_MAX
	cmp	x20,x0
	csel	x20,x0,x20,hi
	check_user x19,x20,.fault
	adr	x0,clipboard
	ldr	x0,[x0]
	cbnz	x0,.have
	mov	x0,CLIP_MAX/PAGE_SIZE
	bl	pmm_alloc_n
	cbz	x0,.nomem
	adr	x1,clipboard
	str	x0,[x1]
.have:	mov	x1,x19
	mov	x2,x20
	bl	mem_copy
	adr	x0,clipboard_len
	str	x20,[x0]
	mov	x0,0
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x19,x20,[sp],16
	epilogue

sys_clip_get:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,clipboard_len
	ldr	x2,[x0]
	cmp	x1,x2
	csel	x20,x1,x2,lo
	check_user x19,x20,.fault
	adr	x1,clipboard
	ldr	x1,[x1]
	mov	x0,0
	cbz	x1,.out
	mov	x0,x19
	mov	x2,x20
	bl	mem_copy
	mov	x0,x20
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
		align 8
ws_lock_daif	dd 0
ws_server	dd 0				; pid du serveur (0 : aucun)
ws_canal	dd 0				; son service (svc.asm)
ws_nom		db 'ws',0
		align 8
ws_lock		dw 0
buf_mutex	dw 0
ws_display_on	dw 0				; 1 : le serveur a l'ecran
		align 8
; buf_table : dans le BSS (kernel.asm)
