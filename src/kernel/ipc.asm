; ============================================================================
;  ipc.asm - IPC entre process pour la couche POSIX (WebKit2 : docs/WEBKIT.md)
;
;  Trois mecanismes, appels 0x70-0x7D (src/inc/syscalls.inc, docs/ABI.md) :
;    * canaux locaux (socketpair AF_UNIX : SOCK_SEQPACKET, SOCK_STREAM,
;      SOCK_DGRAM) : deux bouts relies, chacun avec son anneau de reception ;
;    * passage de poignees dans un message (SCM_RIGHTS) ;
;    * objets de memoire partagee (memfd_create, shm_open) projetes par
;      SHM_MAP (mmap MAP_SHARED) dans l'arene de VM_MAP, dans plusieurs process.
;
;  Objets a poignees du noyau (pas des noeuds de vfs) : une poignee est une
;  entree (1..1023) de la table du process (P_HANDLES, 4 pages), qui tient une
;  reference sur l'objet. Une reference aussi : chaque poignee en vol dans un
;  message, chaque poignee de la liste donnee au prochain fils (P_HSPAWN),
;  chaque region qui projette un objet partage, le nom d'un objet (shm_open),
;  sauf un nom faible (IPC_SHM_WEAK : l'objet part avec sa derniere reference,
;  son nom avec lui), l'epingle d'un objet a nom faible (IPC_SHM_PIN).
;  A la derniere reference : un objet partage rend ses pages tout de suite ;
;  un bout de canal passe dans ipc_dead et ipc_reap le ferme hors de tout
;  verrou (son anneau peut porter d'autres bouts : pas de recursion).
;
;  Un canal = une page : verrou, type, deux bouts. Un bout = sa reference,
;  son pair, son etat (ferme, SHUT_RD, SHUT_WR) et son anneau de reception
;  (64 Ko, alloue au premier message : 1024 anneaux dans tout le systeme).
;  Un message = un enregistrement contigu dans l'anneau du receveur : en-tete
;  (octets, poignees, octets deja lus, poignees remises), les objets portes,
;  les donnees. SEQPACKET / DGRAM : un message entier ou rien (E_MSGSIZE si
;  plus grand que l'anneau) ; STREAM : ce qui tient (un envoi bloquant attend
;  la suite), la lecture traverse les messages mais s'arrete devant un message
;  qui porte des poignees (elles viennent avec son premier octet, comme Linux).
;  Fin : le pair ferme ou SHUT_WR -> 0 une fois l'anneau vide ; envoi vers un
;  pair ferme -> E_PIPE (pas de SIGPIPE). Envoyer le bout receveur par son
;  propre canal : E_INVAL (aucune fermeture ne pourrait le rendre) ; les
;  cycles plus longs ne sont pas ramasses.
;
;  Attente (send / recv bloquants, HANDLE_WAIT = poll) : ipc_sleep inscrit le
;  thread dans ipcw_table avec les canaux qu'il attend (TS_BLOCKED, TH_WAIT =
;  son entree, TH_WAKE = echeance) ; tout changement d'un canal appelle
;  ipc_wake (cle = la page du canal). ipc_seq avance a chaque reveil :
;  l'appelant le lit AVANT de regarder l'etat, ipc_sleep ne dort pas s'il a
;  change (pas de reveil perdu sans tenir les verrous de tous les canaux).
;  Le reveil passe par P_LOCK du process, comme futex_wake : un process qui
;  meurt (P_STATE) n'est jamais touche ; ses entrees sont effacees par
;  ipc_proc_release.
;
;  Memoire partagee : un objet = une page (en-tete, puis 504 pointeurs vers
;  des pages de 512 adresses de pages : 1008 Mo au plus), pages creees a zero
;  au premier acces (faute dans une region VMK_SHARED, vmspace.asm), jamais a
;  un process (PTE_SHARED : ni zap ni vm_destroy ne les rendent). Retrecir un
;  objet projete : E_BUSY. Sceaux de memfd (F_ADD_SEALS) : SHRINK, GROW,
;  WRITE (projection en ecriture refusee), SEAL.
;
;  Fin d'un process (vm_destroy) : ipc_proc_release ferme ses poignees et sa
;  liste de lancement ; ishm_vma_release rend les references de ses regions.
;  Lancement (proc_spawn_elf) : ipc_spawn_give met la liste du pere dans la
;  table du fils, chaque poignee avec sa marque (le numero de descripteur que
;  la couche POSIX lui donnera : HANDLE_LIST).
;
;  Verrous (tous en irqsave) : ICH_LOCK d'un canal -> table de poignees
;  (feuille) ; ICH_LOCK -> ipcw_lock -> P_LOCK ; P_VMLOCK -> ISH_LOCK ;
;  ICH_LOCK ou P_VMLOCK -> isn_lock -> pmm (ishm_destroy efface un nom faible).
;  Jamais l'inverse. Aucun n'est pris sous svc_lock.
; ============================================================================

IPC_E_PIPE	= -32
IPC_E_NOTSOCK	= -88
IPC_E_MSGSIZE	= -90
IPC_E_PROTO	= -93
IPC_E_NOBUFS	= -105
IPC_E_NAMELONG	= -36
IPC_E_NFILE	= -23
IPC_E_PERM	= -1
IPC_E_BUSY	= -16

; --- en-tete commun des objets ------------------------------------------------
IOB_TYPE	= 0			; (32 bits) IPC_KIND_CHAN (un bout) / IPC_KIND_SHM
IOB_REFS	= 8
IOB_NEXT	= 16			; chainage dans ipc_dead

; --- canal (une page) et ses deux bouts -----------------------------------------
ICH_LOCK	= 0
ICH_TYPE	= 8			; IPC_SOCK_*
ICH_LIVE	= 16			; bouts pas encore fermes
ICH_PID		= 24			; pid du createur (SO_PEERCRED)
ICH_EP0		= 64
ICH_EP1		= 192
IEP_CHAN	= 24
IEP_PEER	= 32
IEP_RING	= 40			; anneau de reception (0 : pas encore)
IEP_HEAD	= 48
IEP_TAIL	= 56
IEP_USED	= 64			; octets pris (sauts de fin d'anneau compris)
IEP_STATE	= 72			; IEPS_*
IEP_QUEUED	= 80			; octets de donnees en attente
IEP_NREC	= 88			; messages en attente
IEPB_CLOSED	= 0			; (bits de IEP_STATE)
IEPB_SHUT_RD	= 1
IEPB_SHUT_WR	= 2
IRING_PAGES	= 16
IRING_SIZE	= IRING_PAGES * 4096
IRING_BUDGET	= 1024			; anneaux dans tout le systeme (64 Mo)
IPC_OUT_ROOM	= 8192			; place libre pour dire POLLOUT
; enregistrement d'un message dans l'anneau
IRH_LEN		= 0			; (32 bits) octets de donnees ; IRH_WRAP : saut au debut
IRH_NH		= 4			; (32 bits) poignees portees (objets, 8 o chacun)
IRH_OFF		= 8			; (32 bits) octets deja lus (STREAM)
IRH_DONE	= 12			; (32 bits) 1 : poignees deja remises
IRH_SIZE	= 16

; --- table des poignees d'un process (P_HANDLES) -----------------------------------
IHT_LOCK	= 0
IHT_PAGES	= 4
IHT_MAX		= IHT_PAGES * 4096 / 16	; entree 0 : en-tete ; poignees 1..1023
IHE_OBJ		= 0
IHE_TAG		= 8			; (32 bits) marque (HANDLE_INHERIT -> HANDLE_LIST)
; liste de lancement (P_HSPAWN, une page) : +0 nombre, entrees de 16 o {objet, marque}
ISP_FIRST	= 16

; --- objet de memoire partagee (une page) ------------------------------------------
ISH_LOCK	= 24
ISH_SIZE	= 32
ISH_SEALS	= 40			; (32 bits) IPC_SEAL_*
ISH_FLAGS	= 44			; (32 bits) IPC_SHM_SEALABLE, IPC_SHM_PINNED, ISHF_WEAK
ISH_MAPS	= 48			; regions qui le projettent (tous les process)
ISH_ID		= 56
ISH_DIR		= 64			; pages de 512 adresses de pages
ISH_DIR_N	= (4096 - ISH_DIR) / 8	; 504
ISH_MAX		= ISH_DIR_N * 512 * 4096
ISHF_WEAK	= 4			; (ISH_FLAGS, sous isn_lock) son nom ne le tient pas

; --- noms (shm_open) ----------------------------------------------------------------
ISN_MAX		= 256
ISN_OBJ		= 0
ISN_NAME	= 8
ISN_NAME_MAX	= 56			; 55 caracteres + 0
ISN_SIZE	= 64

; --- threads qui attendent (ipcw_table) ----------------------------------------------
IW_MAX		= 64
IW_TCB		= 0
IW_PCB		= 8
IW_N		= 16			; cles (0 : tout reveil)
IW_KEYS		= 24
IW_KEYS_N	= 13
IW_SIZE		= 128

; ===========================================================================
;  Petits outils
; ===========================================================================

; x0 = adresse d'un compteur 64 bits, x1 = ajout -> x0 = nouvelle valeur
iatomic_add:
.l:	ldaxr	x2,[x0]
	add	x2,x2,x1
	stlxr	w3,x2,[x0]
	cbnz	w3,.l
	mov	x0,x2
	ret

; x0 = adresse, x1 = nouvelle valeur -> x0 = ancienne (echange atomique)
ixchg:
.l:	ldaxr	x2,[x0]
	stlxr	w3,x1,[x0]
	cbnz	w3,.l
	mov	x0,x2
	ret

; x0 = attente ms (0 : aucune) -> x0 = echeance CNTPCT (0 : aucune)
ipc_deadline:
	cbz	x0,.out
	mrs	x1,cntfrq_el0
	mul	x0,x0,x1
	mov	x1,1000
	udiv	x0,x0,x1
	mrs	x1,cntpct_el0
	add	x0,x0,x1
.out:	ret

; x0 = dst, x1 = src, x2 = octets (par mots de 8 quand les deux s'y pretent)
icopy:
	cbz	x2,.done
	orr	x3,x0,x1
	tst	x3,7
	b.ne	.b
.w:	cmp	x2,8
	b.lo	.b
	ldr	x3,[x1],8
	str	x3,[x0],8
	sub	x2,x2,8
	b	.w
.b:	cbz	x2,.done
	ldrb	w3,[x1],1
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.b
.done:	ret

; ===========================================================================
;  References
; ===========================================================================

; x0 = objet : une reference de plus (ne touche que x2, x3)
iobj_ref:
	add	x2,x0,IOB_REFS
.l:	ldaxr	x3,[x2]
	add	x3,x3,1
	stlxr	w4,x3,[x2]
	cbnz	w4,.l
	ret

; (sous isn_lock) x0 = objet d'un nom, faible peut-etre : ses references sont
; peut-etre a 0, ishm_destroy attend alors ce verrou pour le rendre
; -> x0 = 1 : une reference de plus ; 0 : il meurt
iobj_ref_live:
	add	x2,x0,IOB_REFS
.l:	ldaxr	x3,[x2]
	cbz	x3,.dead
	add	x3,x3,1
	stlxr	w4,x3,[x2]
	cbnz	w4,.l
	mov	x0,1
	ret
.dead:	clrex
	mov	x0,0
	ret

; x0 = objet : une reference de moins. A la derniere : un objet partage est
; rendu tout de suite (permis sous P_VMLOCK : seul pmm_lock est pris), un bout
; de canal passe dans ipc_dead (ipc_reap le ferme)
iobj_unref:
	cbz	x0,.ret
	add	x2,x0,IOB_REFS
.l:	ldaxr	x1,[x2]
	sub	x1,x1,1
	stlxr	w3,x1,[x2]
	cbnz	w3,.l
	cbz	x1,.last
.ret:	ret
.last:	ldr	w1,[x0,IOB_TYPE]
	cmp	w1,IPC_KIND_SHM
	b.eq	ishm_destroy
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,ipcd_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	adr	x2,ipc_dead
	ldr	x3,[x2]
	str	x3,[x19,IOB_NEXT]
	str	x19,[x2]
	adr	x0,ipcd_lock
	bl	spin_unlock_irqrestore
	ldr	x19,[sp],16
	epilogue

; Ferme les bouts de canaux sans reference (hors de tout verrou)
ipc_reap:
	prologue
	str	x19,[sp,-16]!
.pop:	adr	x0,ipcd_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	adr	x2,ipc_dead
	ldr	x19,[x2]
	cbz	x19,.none
	ldr	x3,[x19,IOB_NEXT]
	str	x3,[x2]
.none:	adr	x0,ipcd_lock
	bl	spin_unlock_irqrestore
	cbz	x19,.out
	mov	x0,x19
	bl	iep_destroy
	b	.pop
.out:	ldr	x19,[sp],16
	epilogue

; x0 = objet -> x0 = sa sorte pour l'appelant : IPC_KIND_* | type << 8
iobj_info:
	ldr	w1,[x0,IOB_TYPE]
	cmp	w1,IPC_KIND_CHAN
	b.ne	.k
	ldr	x2,[x0,IEP_CHAN]
	ldr	x2,[x2,ICH_TYPE]
	orr	x1,x1,x2,lsl 8
.k:	mov	x0,x1
	ret

; ===========================================================================
;  Table des poignees
; ===========================================================================

; x0 = PCB -> x0 = sa table (creee au premier besoin ; 0 : plus de memoire)
iht_of:
	ldr	x1,[x0,P_HANDLES]
	cbz	x1,.new
	mov	x0,x1
	ret
.new:	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,IHT_PAGES
	bl	pmm_alloc_n
	cbz	x0,.out
	mov	x20,x0
	mov	x1,IHT_PAGES * 4096
	bl	mem_zero
	dmb	ishst
	add	x2,x19,P_HANDLES
.cas:	ldaxr	x0,[x2]
	cbnz	x0,.lost
	stlxr	w3,x20,[x2]
	cbnz	w3,.cas
	mov	x0,x20
	b	.out
.lost:	clrex					; un autre thread l'a faite
	str	x0,[sp,-16]!
	mov	x0,x20
	mov	x1,IHT_PAGES
	bl	pmm_free_n
	ldr	x0,[sp],16
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = objet, x2 = marque -> x0 = poignee (la reference passe a la
; table), E_MFILE (la reference reste a l'appelant)
iht_install:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	bl	iht_of
	cbz	x0,.full
	mov	x19,x0
	bl	spin_lock_irqsave		; (verrou en tete de table)
	mov	x22,x0
	mov	x3,1
.f:	cmp	x3,IHT_MAX
	b.hs	.unl_full
	add	x4,x19,x3,lsl 4
	ldr	x5,[x4,IHE_OBJ]
	cbz	x5,.put
	add	x3,x3,1
	b	.f
.put:	str	x20,[x4,IHE_OBJ]
	str	w21,[x4,IHE_TAG]
	mov	x20,x3
	mov	x0,x19
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x20
	b	.out
.unl_full:
	mov	x0,x19
	mov	x1,x22
	bl	spin_unlock_irqrestore
.full:	mov	x0,E_MFILE
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = poignee, x2 = 0 : l'objet avec une reference de plus ;
; 1 : retire de la table (la reference de la table passe a l'appelant)
; -> x0 = objet (0 : poignee invalide)
iht_lookup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x20,x1
	mov	x23,x2
	ldr	x19,[x0,P_HANDLES]
	mov	x21,0
	cbz	x19,.out
	sub	x1,x20,1
	cmp	x1,IHT_MAX - 1
	b.hs	.out
	mov	x0,x19
	bl	spin_lock_irqsave
	mov	x22,x0
	add	x4,x19,x20,lsl 4
	ldr	x21,[x4,IHE_OBJ]
	cbz	x21,.unl
	cbz	x23,.ref
	str	xzr,[x4,IHE_OBJ]
	str	wzr,[x4,IHE_TAG]
	b	.unl
.ref:	mov	x0,x21
	bl	iobj_ref
.unl:	mov	x0,x19
	mov	x1,x22
	bl	spin_unlock_irqrestore
.out:	mov	x0,x21
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = poignee -> x0 = objet avec une reference (0 : invalide)
iht_get:
	mov	x2,0
	b	iht_lookup

; x0 = page d'une liste de lancement : ses references rendues, page rendue
ilist_release:
	cbz	x0,.ret
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19]
.e:	cbz	x20,.free
	sub	x20,x20,1
	add	x0,x19,x20,lsl 4
	ldr	x0,[x0,ISP_FIRST]
	bl	iobj_unref
	b	.e
.free:	mov	x0,x19
	bl	pmm_free
	ldp	x19,x20,[sp],16
	epilogue
.ret:	ret

; ===========================================================================
;  Attente et reveil
; ===========================================================================

; x0 = cle (page d'un canal) : reveille les threads qui l'attendent (et ceux
; qui attendent tout) ; ipc_seq avance
ipc_wake:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,ipcw_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	adr	x0,ipc_seq
	ldr	x1,[x0]
	add	x1,x1,1
	stlr	x1,[x0]
	adr	x20,ipcw_table
	mov	x21,0
.e:	cmp	x21,IW_MAX
	b.hs	.done
	ldr	x0,[x20,IW_TCB]
	cbz	x0,.next
	ldr	x1,[x20,IW_N]
	cbz	x1,.wake
	add	x2,x20,IW_KEYS
.k:	ldr	x3,[x2],8
	cmp	x3,x19
	b.eq	.wake
	subs	x1,x1,1
	b.ne	.k
	b	.next
.wake:	mov	x0,x20
	bl	ipcw_kick
.next:	add	x20,x20,IW_SIZE
	add	x21,x21,1
	b	.e
.done:	adr	x0,ipcw_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sous ipcw_lock, IRQ masquees) x0 = entree : son thread est reveille s'il
; dort encore dessus (P_LOCK : comme futex_wake ; un process en fin de vie
; n'est pas touche)
ipcw_kick:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,IW_PCB]
	add	x0,x20,P_LOCK
	bl	spin_lock
	ldr	x0,[x20,P_STATE]
	cbnz	x0,.unl
	ldr	x1,[x19,IW_TCB]
	ldr	x0,[x1,TH_STATE]
	cmp	x0,TS_BLOCKED
	b.ne	.unl
	ldr	x0,[x1,TH_WAIT]
	cmp	x0,x19
	b.ne	.unl
	str	xzr,[x1,TH_WAIT]
	str	xzr,[x1,TH_STATE]		; TS_READY
	ldr	x0,[x1,TH_CPU]
	bl	cpu_kick
.unl:	add	x0,x20,P_LOCK
	bl	spin_unlock
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = ipc_seq (a lire AVANT de regarder l'etat des canaux)
ipc_seq_now:
	adr	x0,ipc_seq
	ldar	x0,[x0]
	ret

; x0 = cles (tableau noyau), x1 = nombre (0 ou plus de IW_KEYS_N : tout
; reveil), x2 = ipc_seq lu avant de regarder l'etat, x3 = echeance (0 : sans)
; -> x0 = 0 (reveil, ou l'etat a pu changer : regarder a nouveau),
; E_TIMEDOUT, E_INTR (thread tue)
ipc_sleep:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x23,x3
	cbz	x23,.lock
	mrs	x0,cntpct_el0
	cmp	x0,x23
	b.hs	.late
.lock:	adr	x0,ipcw_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	adr	x0,ipc_seq
	ldr	x0,[x0]
	cmp	x0,x21
	b.ne	.changed
	this_cpu x0
	ldr	x24,[x0,PC_CURRENT]
	ldr	x0,[x24,TH_KILL]
	cbnz	x0,.intr
	mov	x0,x24
	bl	sig_ready
	cbnz	x0,.intr
	adr	x1,ipcw_table
	mov	x2,0
.f:	cmp	x2,IW_MAX
	b.hs	.full
	ldr	x3,[x1,IW_TCB]
	cbz	x3,.got
	add	x1,x1,IW_SIZE
	add	x2,x2,1
	b	.f
.got:	str	x24,[x1,IW_TCB]
	ldr	x3,[x24,TH_PROC]
	str	x3,[x1,IW_PCB]
	cmp	x20,IW_KEYS_N
	csel	x20,xzr,x20,hi
	str	x20,[x1,IW_N]
	add	x4,x1,IW_KEYS
	mov	x2,0
.ck:	cmp	x2,x20
	b.hs	.ckd
	ldr	x3,[x19,x2,lsl 3]
	str	x3,[x4,x2,lsl 3]
	add	x2,x2,1
	b	.ck
.ckd:	mov	x19,x1				; (l'entree)
	str	x19,[x24,TH_WAIT]
	str	x23,[x24,TH_WAKE]		; 0 : sans delai (sched.asm)
	mov	x0,x24
	mov	x1,TS_BLOCKED
	bl	thread_park
	adr	x0,ipcw_lock
	bl	spin_unlock			; IRQ toujours masquees
	bl	schedule
	adr	x0,ipcw_lock			; reveil (TH_WAIT = 0) ou echeance
	bl	spin_lock			; (TH_WAIT encore pose)
	ldr	x0,[x24,TH_WAIT]
	mov	x20,0
	cbz	x0,.woke
	str	xzr,[x24,TH_WAIT]
	mov	x20,E_TIMEDOUT
.woke:	str	xzr,[x19,IW_TCB]
	ldr	x0,[x24,TH_KILL]
	cbnz	x0,.wintr
	mov	x0,x24
	bl	sig_ready
	cbz	x0,.ret
.wintr:	mov	x20,E_INTR
	b	.ret
.full:	adr	x0,ipcw_lock			; table pleine : courte attente, puis
	mov	x1,x22				; l'appelant regarde a nouveau
	bl	spin_unlock_irqrestore
	mov	x0,2
	bl	thread_sleep
	mov	x0,0
	b	.out
.changed:
	mov	x20,0
	b	.ret
.intr:	mov	x20,E_INTR
.ret:	adr	x0,ipcw_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x20
	b	.out
.late:	mov	x0,E_TIMEDOUT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = cle : une seule cle, sur la pile ; x1 = ipc_seq lu, x2 = echeance
ipc_sleep1:
	prologue
	str	x0,[sp,-16]!
	mov	x3,x2
	mov	x2,x1
	mov	x0,sp
	mov	x1,1
	bl	ipc_sleep
	add	sp,sp,16
	epilogue

; ===========================================================================
;  Anneaux de reception (sous ICH_LOCK)
; ===========================================================================

; x0 = bout receveur (anneau alloue), x1 = taille d'un enregistrement
; (multiple de 8) -> x0 = sa position (-1 : pas de place) ; un saut en fin
; d'anneau est pose s'il faut repartir du debut
iring_reserve:
	ldr	x2,[x0,IEP_USED]
	cbnz	x2,.busy
	str	xzr,[x0,IEP_HEAD]
	str	xzr,[x0,IEP_TAIL]
	movl	x3,IRING_SIZE
	cmp	x1,x3
	b.hi	.no
	mov	x0,0
	ret
.busy:	ldr	x3,[x0,IEP_HEAD]
	ldr	x4,[x0,IEP_TAIL]
	cmp	x4,x3
	b.lo	.mid				; [queue, tete[
	b.eq	.no				; pleine
	movl	x5,IRING_SIZE			; [queue, fin[ puis [0, tete[
	sub	x6,x5,x4
	cmp	x1,x6
	b.hi	.wrap
	mov	x0,x4
	ret
.wrap:	cmp	x1,x3
	b.hi	.no
	ldr	x7,[x0,IEP_RING]		; saut : la fin de l'anneau est perdue
	movn	w8,0
	str	w8,[x7,x4]
	add	x2,x2,x6
	str	x2,[x0,IEP_USED]
	str	xzr,[x0,IEP_TAIL]
	mov	x0,0
	ret
.mid:	sub	x6,x3,x4
	cmp	x1,x6
	b.hi	.no
	mov	x0,x4
	ret
.no:	mov	x0,-1
	ret

; x0 = bout receveur -> x0 = plus grand enregistrement qui tient maintenant
iring_room:
	ldr	x2,[x0,IEP_USED]
	movl	x5,IRING_SIZE
	cbnz	x2,.busy
	mov	x0,x5
	ret
.busy:	ldr	x3,[x0,IEP_HEAD]
	ldr	x4,[x0,IEP_TAIL]
	cmp	x4,x3
	b.lo	.mid
	b.eq	.none
	sub	x6,x5,x4
	cmp	x6,x3
	csel	x0,x6,x3,hs
	ret
.mid:	sub	x0,x3,x4
	ret
.none:	mov	x0,0
	ret

; x0 = bout receveur, x1 = position, x2 = taille, x3 = octets de donnees
iring_commit:
	add	x1,x1,x2
	movl	x4,IRING_SIZE
	cmp	x1,x4
	csel	x1,xzr,x1,hs
	str	x1,[x0,IEP_TAIL]
	ldr	x4,[x0,IEP_USED]
	add	x4,x4,x2
	str	x4,[x0,IEP_USED]
	ldr	x4,[x0,IEP_NREC]
	add	x4,x4,1
	str	x4,[x0,IEP_NREC]
	ldr	x4,[x0,IEP_QUEUED]
	add	x4,x4,x3
	str	x4,[x0,IEP_QUEUED]
	ret

; x0 = bout (IEP_NREC > 0) -> x0 = enregistrement en tete (saut passe)
iring_head:
	ldr	x1,[x0,IEP_RING]
	ldr	x2,[x0,IEP_HEAD]
	ldr	w3,[x1,x2]
	cmn	w3,1
	b.ne	.ok
	movl	x4,IRING_SIZE
	sub	x4,x4,x2
	ldr	x5,[x0,IEP_USED]
	sub	x5,x5,x4
	str	x5,[x0,IEP_USED]
	str	xzr,[x0,IEP_HEAD]
	mov	x2,0
.ok:	add	x0,x1,x2
	ret

; x0 = enregistrement -> x0 = sa taille dans l'anneau
irec_size:
	ldr	w1,[x0,IRH_LEN]
	ldr	w2,[x0,IRH_NH]
	add	x1,x1,x2,lsl 3
	add	x1,x1,IRH_SIZE + 7
	and	x0,x1,not 7
	ret

; x0 = bout : retire l'enregistrement en tete (ses poignees remises ou rendues)
iring_pop:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	iring_head
	bl	irec_size
	ldr	x1,[x19,IEP_HEAD]
	add	x1,x1,x0
	movl	x2,IRING_SIZE
	cmp	x1,x2
	csel	x1,xzr,x1,hs
	str	x1,[x19,IEP_HEAD]
	ldr	x2,[x19,IEP_USED]
	sub	x2,x2,x0
	str	x2,[x19,IEP_USED]
	ldr	x2,[x19,IEP_NREC]
	sub	x2,x2,1
	str	x2,[x19,IEP_NREC]
	cbnz	x2,.out
	str	xzr,[x19,IEP_USED]		; vide : on repart du debut
	str	xzr,[x19,IEP_HEAD]
	str	xzr,[x19,IEP_TAIL]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = anneau (0 : aucun), x1 = tete, x2 = octets pris : les poignees des
; messages jamais lus sont rendues, puis l'anneau (hors verrou)
iring_purge:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbz	x19,.out
.r:	cbz	x21,.free
	add	x22,x19,x20
	ldr	w1,[x22,IRH_LEN]
	cmn	w1,1
	b.ne	.rec
	movl	x2,IRING_SIZE			; saut au debut
	sub	x2,x2,x20
	cmp	x2,x21
	b.hi	.free
	sub	x21,x21,x2
	mov	x20,0
	b	.r
.rec:	ldr	w23,[x22,IRH_DONE]
	cbnz	w23,.adv
	ldr	w23,[x22,IRH_NH]
	add	x24,x22,IRH_SIZE
.h:	cbz	x23,.adv
	ldr	x0,[x24],8
	bl	iobj_unref
	sub	x23,x23,1
	b	.h
.adv:	mov	x0,x22
	bl	irec_size
	cmp	x0,x21
	b.hi	.free				; (garde-fou)
	add	x20,x20,x0
	sub	x21,x21,x0
	movl	x1,IRING_SIZE
	cmp	x20,x1
	csel	x20,xzr,x20,hs
	b	.r
.free:	mov	x0,x19
	mov	x1,IRING_PAGES
	bl	pmm_free_n
	adr	x0,ipc_rings
	mov	x1,-1
	bl	iatomic_add
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = bout : son anneau de reception alloue s'il ne l'est pas -> x0 = 1
; (0 : plus de memoire ou budget epuise)
iring_alloc:
	ldr	x1,[x0,IEP_RING]
	cbz	x1,.new
	mov	x0,1
	ret
.new:	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,ipc_rings
	mov	x1,1
	bl	iatomic_add
	cmp	x0,IRING_BUDGET
	b.hi	.over
	mov	x0,IRING_PAGES
	bl	pmm_alloc_n
	cbz	x0,.over
	str	x0,[x19,IEP_RING]
	str	xzr,[x19,IEP_HEAD]
	str	xzr,[x19,IEP_TAIL]
	str	xzr,[x19,IEP_USED]
	mov	x0,1
	b	.out
.over:	adr	x0,ipc_rings
	mov	x1,-1
	bl	iatomic_add
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; ===========================================================================
;  Bouts de canaux
; ===========================================================================

; (sous ICH_LOCK) x0 = bout -> x0 = bits IPC_POLL* prets
iep_poll:
	ldr	x1,[x0,IEP_PEER]
	ldr	x2,[x0,IEP_STATE]		; soi
	ldr	x3,[x1,IEP_STATE]		; pair
	mov	x4,0
	ldr	x5,[x0,IEP_NREC]
	cbnz	x5,.in
	tbnz	x3,IEPB_CLOSED,.in		; fin de l'entree : pair ferme,
	tbnz	x3,IEPB_SHUT_WR,.in		; pair SHUT_WR, soi SHUT_RD
	tbz	x2,IEPB_SHUT_RD,.noin
.in:	orr	x4,x4,IPC_POLLIN
.noin:	tbnz	x3,IEPB_CLOSED,.out_ok		; un envoi echouerait tout de suite
	tbnz	x3,IEPB_SHUT_RD,.out_ok
	tbnz	x2,IEPB_SHUT_WR,.out_ok
	ldr	x5,[x1,IEP_RING]
	cbz	x5,.out_ok
	ldr	x5,[x1,IEP_USED]
	movl	x6,IRING_SIZE - IPC_OUT_ROOM
	cmp	x5,x6
	b.hi	.hup
.out_ok:
	orr	x4,x4,IPC_POLLOUT
.hup:	tbz	x3,IEPB_CLOSED,.h2
	orr	x4,x4,IPC_POLLHUP
.h2:	tbz	x2,IEPB_SHUT_RD,.out
	tbz	x2,IEPB_SHUT_WR,.out
	orr	x4,x4,IPC_POLLHUP
.out:	mov	x0,x4
	ret

; x0 = bout sans reference : ferme (le pair voit la fin), messages en
; attente rendus avec ce qu'ils portaient, page du canal rendue avec le
; dernier bout
iep_destroy:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,IEP_CHAN]
	mov	x0,x20
	bl	spin_lock_irqsave
	mov	x22,x0
	ldr	x0,[x19,IEP_STATE]
	orr	x0,x0,1 shl IEPB_CLOSED
	str	x0,[x19,IEP_STATE]
	ldr	x21,[x19,IEP_RING]
	ldr	x23,[x19,IEP_HEAD]
	ldr	x24,[x19,IEP_USED]
	str	xzr,[x19,IEP_RING]
	str	xzr,[x19,IEP_USED]
	str	xzr,[x19,IEP_NREC]
	str	xzr,[x19,IEP_QUEUED]
	ldr	x19,[x20,ICH_LIVE]
	sub	x19,x19,1
	str	x19,[x20,ICH_LIVE]
	mov	x0,x20
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x20				; le pair voit la fin
	bl	ipc_wake
	mov	x0,x21
	mov	x1,x23
	mov	x2,x24
	bl	iring_purge
	cbnz	x19,.out
	mov	x0,x20				; second bout : le canal
	bl	pmm_free
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = poignee -> x0 = bout (reference), x1 = PCB ; x0 = E_BADF ou
; IPC_E_NOTSOCK (rien de pris)
iep_from:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	current_proc
	mov	x20,x1
	mov	x0,x20
	mov	x1,x19
	bl	iht_get
	cbz	x0,.badf
	ldr	w1,[x0,IOB_TYPE]
	cmp	w1,IPC_KIND_CHAN
	b.eq	.ok
	bl	iobj_unref
	mov	x0,IPC_E_NOTSOCK
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.ok:	mov	x1,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = iovecs (EL0), x1 = nombre -> x0 = octets en tout (chaque tampon
; verifie, pages peuplees), E_INVAL, E_FAULT
iov_check:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0
	cbz	x20,.ok
	cmp	x20,IPC_MAX_IOV
	b.hi	.inval
	lsl	x1,x20,4
	check_user x19,x1,.fault
.e:	cbz	x20,.ok
	ldp	x0,x1,[x19],16
	sub	x20,x20,1
	cbz	x1,.e
	add	x21,x21,x1
	lsr	x2,x21,31
	cbnz	x2,.inval
	check_user x0,x1,.fault
	b	.e
.ok:	mov	x0,x21
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon noyau, x1 = iovecs (verifiees), x2 = nombre, x3 = decalage dans
; les donnees des iovecs, x4 = octets, x5 = 0 : des iovecs vers le noyau ;
; 1 : du noyau vers les iovecs
iov_copy:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
.v:	cbz	x23,.out
	cbz	x21,.out
	ldp	x0,x1,[x20],16			; adresse, taille
	sub	x21,x21,1
	cmp	x22,x1
	b.hs	.skip
	add	x0,x0,x22
	sub	x1,x1,x22
	mov	x22,0
	cmp	x1,x23
	csel	x2,x1,x23,lo
	sub	x23,x23,x2
	cbz	x24,.in
	mov	x1,x19				; noyau -> EL0
	add	x19,x19,x2
	bl	icopy
	b	.v
.in:	mov	x1,x0				; EL0 -> noyau
	mov	x0,x19
	add	x19,x19,x2
	bl	icopy
	b	.v
.skip:	sub	x22,x22,x1
	b	.v
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ===========================================================================
;  Appels : poignees
; ===========================================================================

; --- SYS_HANDLE_CLOSE handle_close(poignee) -> 0 -----------------------------
sys_handle_close:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	current_proc
	mov	x0,x1
	mov	x1,x19
	mov	x2,1
	bl	iht_lookup
	cbz	x0,.badf
	bl	iobj_unref
	bl	ipc_reap
	mov	x0,0
	b	.out
.badf:	mov	x0,E_BADF
.out:	ldr	x19,[sp],16
	epilogue

; --- SYS_HANDLE_INFO handle_info(poignee, sortie 64 o) -> 0 -------------------
sys_handle_info:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	check_user x20,IPCI_SIZEOF,.fault
	current_proc
	mov	x0,x1
	mov	x1,x19
	bl	iht_get
	cbz	x0,.badf
	mov	x19,x0
	mov	x0,x20
	mov	x1,IPCI_SIZEOF
	bl	mem_zero			; (adresse EL0 verifiee, peuplee)
	ldr	w0,[x19,IOB_TYPE]
	str	w0,[x20,IPCI_KIND]
	cmp	w0,IPC_KIND_CHAN
	b.ne	.shm
	ldr	x21,[x19,IEP_CHAN]
	ldr	x0,[x21,ICH_TYPE]
	str	w0,[x20,IPCI_TYPE]
	ldr	x0,[x21,ICH_PID]
	str	x0,[x20,IPCI_PID]
	movl	x0,IRING_SIZE
	str	x0,[x20,IPCI_CAP]
	mov	x0,x21
	bl	spin_lock_irqsave
	mov	x22,x0
	ldr	x0,[x19,IEP_QUEUED]
	str	x0,[x20,IPCI_SIZE]
	ldr	x1,[x19,IEP_PEER]
	ldr	x1,[x1,IEP_STATE]
	ldr	x2,[x19,IEP_STATE]
	mov	x3,0
	tbz	x1,IEPB_CLOSED,.s1
	orr	x3,x3,IPCS_PEER_CLOSED
.s1:	tbz	x1,IEPB_SHUT_WR,.s2
	orr	x3,x3,IPCS_PEER_SHUT_WR
.s2:	tbz	x2,IEPB_SHUT_RD,.s3
	orr	x3,x3,IPCS_SHUT_RD
.s3:	tbz	x2,IEPB_SHUT_WR,.s4
	orr	x3,x3,IPCS_SHUT_WR
.s4:	str	w3,[x20,IPCI_STATE]
	mov	x0,x21
	mov	x1,x22
	bl	spin_unlock_irqrestore
	b	.done
.shm:	ldr	x0,[x19,ISH_SIZE]
	str	x0,[x20,IPCI_SIZE]
	ldr	w0,[x19,ISH_SEALS]
	str	w0,[x20,IPCI_STATE]
	ldr	w0,[x19,ISH_FLAGS]
	str	w0,[x20,IPCI_FLAGS]
	ldr	x0,[x19,ISH_ID]
	str	x0,[x20,IPCI_PID]
.done:	mov	x0,x19
	bl	iobj_unref
	bl	ipc_reap
	mov	x0,0
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_HANDLE_WAIT handle_wait(entrees, n, attente ms) -> pretes -----------
; Entrees de 8 o : poignee (32 bits), evenements IPC_POLL* (16 bits), prets
; (16 bits, ecrits). Attente < 0 : sans fin ; 0 : regarder seulement.
; Poignee invalide : IPC_POLLNVAL ; objet partage : toujours pret.
sys_handle_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,IW_KEYS_N * 8 + 8		; cles (canaux attendus)
	mov	x19,x0				; entrees
	mov	x20,x1				; n
	mov	x21,x2				; attente
	cmp	x20,IPC_WAIT_MAX
	b.hi	.inval
	lsl	x1,x20,3
	check_user x19,x1,.fault
	mov	x22,0				; echeance
	cmp	x21,0
	b.le	.go
	mov	x0,x21
	bl	ipc_deadline
	mov	x22,x0
.go:	current_proc
	mov	x23,x1				; PCB
.again:	bl	ipc_seq_now
	mov	x24,x0				; ipc_seq lu avant l'etat
	mov	x25,0				; pretes
	mov	x26,0				; cles (bit 32 : trop)
	mov	x0,0
.e:	cmp	x0,x20
	b.hs	.scanned
	str	x0,[sp,IW_KEYS_N * 8]
	add	x3,x19,x0,lsl 3
	ldr	w1,[x3]
	mov	x0,x23
	bl	iht_get
	ldr	x9,[sp,IW_KEYS_N * 8]
	add	x3,x19,x9,lsl 3
	mov	x4,IPC_POLLNVAL
	cbz	x0,.put
	str	x0,[sp,-16]!			; (l'objet)
	ldr	w1,[x0,IOB_TYPE]
	cmp	w1,IPC_KIND_CHAN
	b.eq	.chan
	mov	x4,IPC_POLLIN or IPC_POLLOUT	; objet partage : pret
	b	.rel
.chan:	ldr	x1,[x0,IEP_CHAN]		; la cle
	and	x2,x26,0xFF
	cmp	x2,IW_KEYS_N
	b.hs	.many
	add	x5,sp,16
	str	x1,[x5,x2,lsl 3]
	add	x26,x26,1
	b	.poll
.many:	orr	x26,x26,1 shl 32
.poll:	mov	x0,x1
	bl	spin_lock_irqsave
	str	x0,[sp,8]
	ldr	x0,[sp]
	bl	iep_poll
	mov	x4,x0
	ldr	x0,[sp]
	ldr	x0,[x0,IEP_CHAN]
	ldr	x1,[sp,8]
	str	x4,[sp,8]
	bl	spin_unlock_irqrestore
	ldr	x4,[sp,8]
.rel:	str	x4,[sp,8]
	ldr	x0,[sp]
	bl	iobj_unref
	ldr	x4,[sp,8]
	add	sp,sp,16
	ldr	x9,[sp,IW_KEYS_N * 8]
	add	x3,x19,x9,lsl 3
	ldrh	w5,[x3,4]			; evenements demandes (+ ERR, HUP)
	orr	w5,w5,IPC_POLLERR or IPC_POLLHUP
	and	w4,w4,w5
.put:	strh	w4,[x3,6]
	cbz	w4,.n
	add	x25,x25,1
.n:	add	x0,x9,1
	b	.e
.scanned:
	bl	ipc_reap
	cbnz	x25,.ready
	cbz	x21,.ready			; attente 0 : regarder seulement
	mov	x0,sp
	and	x1,x26,0xFF
	tst	x26,1 shl 32
	csel	x1,xzr,x1,ne			; trop de canaux : tout reveil
	cbnz	x1,.sl
	cbz	x20,.sl0
.sl:	mov	x2,x24
	mov	x3,x22
	bl	ipc_sleep
	cbz	x0,.again
	cmp	x0,E_TIMEDOUT
	b.eq	.ready
	b	.out
.sl0:	mov	x0,x21				; rien a attendre : le delai seul
	cmp	x0,0
	b.le	.inval
	bl	thread_sleep
.ready:	mov	x0,x25
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,IW_KEYS_N * 8 + 8
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_HANDLE_LIST handle_list(sortie, max, effacer) -> n --------------------
; Entrees de 16 o : poignee, marque, sorte (IPC_KIND_* | type << 8), 0.
; effacer = 1 : les marques sont remises a 0 (la couche POSIX les lit une fois,
; au demarrage : les poignees donnees par le pere a leurs descripteurs).
sys_handle_list:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x20,IHT_MAX
	b.hi	.inval
	lsl	x1,x20,4
	cbz	x1,.chk
	check_user x19,x1,.fault
.chk:	current_proc
	ldr	x22,[x1,P_HANDLES]
	mov	x23,0				; n
	cbz	x22,.done
	mov	x0,x22
	bl	spin_lock_irqsave
	mov	x24,x0
	mov	x9,1
.e:	cmp	x9,IHT_MAX
	b.hs	.unl
	cmp	x23,x20
	b.hs	.unl
	add	x10,x22,x9,lsl 4
	ldr	x0,[x10,IHE_OBJ]
	cbz	x0,.n
	bl	iobj_info
	add	x11,x19,x23,lsl 4
	str	w9,[x11]
	ldr	w1,[x10,IHE_TAG]
	str	w1,[x11,4]
	str	w0,[x11,8]
	str	wzr,[x11,12]
	cbz	x21,.keep
	str	wzr,[x10,IHE_TAG]
.keep:	add	x23,x23,1
.n:	add	x9,x9,1
	b	.e
.unl:	mov	x0,x22
	mov	x1,x24
	bl	spin_unlock_irqrestore
.done:	mov	x0,x23
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_HANDLE_INHERIT handle_inherit(entrees, n) -> 0 ---------------------------
; Entrees de 8 o : poignee, marque (32 bits chacune). Les objets sont pris
; tout de suite ; le prochain process lance par ce process (SPAWN_IMAGE,
; SPAWN_PRELOADED) les recoit dans sa table avec leur marque. n = 0 :
; l'ancienne liste est rendue (lancement echoue).
sys_handle_inherit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x20,IPC_INHERIT_MAX
	b.hi	.inval
	current_proc
	mov	x21,x1
	add	x0,x21,P_HSPAWN			; l'ancienne liste
	mov	x1,0
	bl	ixchg
	bl	ilist_release
	mov	x0,0
	cbz	x20,.reap
	lsl	x1,x20,3
	check_user x19,x1,.fault
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x22,x0
	str	xzr,[x22]
.e:	ldr	x9,[x22]
	cmp	x9,x20
	b.hs	.set
	add	x10,x19,x9,lsl 3
	ldr	w1,[x10]
	mov	x0,x21
	bl	iht_get
	cbz	x0,.badf
	ldr	x9,[x22]
	add	x11,x22,x9,lsl 4
	str	x0,[x11,ISP_FIRST]
	add	x10,x19,x9,lsl 3
	ldr	w1,[x10,4]
	str	x1,[x11,ISP_FIRST + 8]
	add	x9,x9,1
	str	x9,[x22]
	b	.e
.set:	add	x0,x21,P_HSPAWN
	mov	x1,x22
	bl	ixchg
	bl	ilist_release			; (une liste posee entre-temps)
	mov	x0,0
	b	.reap
.badf:	mov	x0,x22
	bl	ilist_release
	mov	x0,E_BADF
.reap:	str	x0,[sp,-16]!
	bl	ipc_reap
	ldr	x0,[sp],16
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (proc_spawn_elf) x0 = PCB du fils : la liste de lancement du pere (le process
; courant) passe dans sa table
ipc_spawn_give:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x0,[x0,TH_PROC]
	cbz	x0,.out
	add	x0,x0,P_HSPAWN
	mov	x1,0
	bl	ixchg
	cbz	x0,.out
	mov	x20,x0
	mov	x21,0
.e:	ldr	x9,[x20]
	cmp	x21,x9
	b.hs	.free
	add	x22,x20,x21,lsl 4
	mov	x0,x19
	ldr	x1,[x22,ISP_FIRST]
	ldr	x2,[x22,ISP_FIRST + 8]
	bl	iht_install
	tbz	x0,63,.n
	ldr	x0,[x22,ISP_FIRST]		; table pleine : rendue
	bl	iobj_unref
.n:	add	x21,x21,1
	b	.e
.free:	mov	x0,x20
	bl	pmm_free
	bl	ipc_reap
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (vm_destroy) x0 = PCB d'un process fini : ses poignees fermees, sa liste
; de lancement rendue, ses entrees d'attente effacees
ipc_proc_release:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	add	x0,x19,P_HANDLES
	mov	x1,0
	bl	ixchg
	cbz	x0,.list
	mov	x20,x0
	mov	x21,1
.h:	cmp	x21,IHT_MAX
	b.hs	.hfree
	add	x0,x20,x21,lsl 4
	ldr	x0,[x0,IHE_OBJ]
	bl	iobj_unref
	add	x21,x21,1
	b	.h
.hfree:	mov	x0,x20
	mov	x1,IHT_PAGES
	bl	pmm_free_n
.list:	add	x0,x19,P_HSPAWN
	mov	x1,0
	bl	ixchg
	bl	ilist_release
	adr	x0,ipcw_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	adr	x20,ipcw_table
	mov	x21,0
.w:	cmp	x21,IW_MAX
	b.hs	.wd
	ldr	x0,[x20,IW_PCB]
	cmp	x0,x19
	b.ne	.wn
	str	xzr,[x20,IW_TCB]
	str	xzr,[x20,IW_PCB]
.wn:	add	x20,x20,IW_SIZE
	add	x21,x21,1
	b	.w
.wd:	adr	x0,ipcw_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	bl	ipc_reap
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ===========================================================================
;  Appels : canaux
; ===========================================================================

; --- SYS_CHAN_CREATE chan_create(type, sortie 2 x 32 bits) -> 0 ---------------
sys_chan_create:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x19,IPC_SOCK_STREAM
	b.eq	.t
	cmp	x19,IPC_SOCK_DGRAM
	b.eq	.t
	cmp	x19,IPC_SOCK_SEQPACKET
	b.ne	.proto
.t:	check_user x20,8,.fault
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x21,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	str	x19,[x21,ICH_TYPE]
	mov	x0,2
	str	x0,[x21,ICH_LIVE]
	current_proc
	mov	x22,x1
	ldr	x0,[x22,P_PID]
	str	x0,[x21,ICH_PID]
	add	x0,x21,ICH_EP0
	add	x1,x21,ICH_EP1
	mov	w2,IPC_KIND_CHAN
	mov	x3,1
	str	w2,[x0,IOB_TYPE]
	str	x3,[x0,IOB_REFS]
	str	x21,[x0,IEP_CHAN]
	str	x1,[x0,IEP_PEER]
	str	w2,[x1,IOB_TYPE]
	str	x3,[x1,IOB_REFS]
	str	x21,[x1,IEP_CHAN]
	str	x0,[x1,IEP_PEER]
	mov	x0,x22
	add	x1,x21,ICH_EP0
	mov	x2,0
	bl	iht_install
	tbnz	x0,63,.fail2
	mov	x19,x0
	mov	x0,x22
	add	x1,x21,ICH_EP1
	mov	x2,0
	bl	iht_install
	tbnz	x0,63,.fail1
	str	w19,[x20]
	str	w0,[x20,4]
	mov	x0,0
	b	.out
.fail1:	mov	x20,x0				; la premiere poignee reprise
	mov	x0,x22
	mov	x1,x19
	mov	x2,1
	bl	iht_lookup
	bl	iobj_unref
	add	x0,x21,ICH_EP1
	bl	iobj_unref
	b	.failr
.fail2:	mov	x20,x0
	add	x0,x21,ICH_EP0
	bl	iobj_unref
	add	x0,x21,ICH_EP1
	bl	iobj_unref
.failr:	bl	ipc_reap
	mov	x0,x20
	b	.out
.proto:	mov	x0,IPC_E_PROTO
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = bout (une reference, rendue ici), x1 = sa poignee, x2 = ipc_seq lu,
; x3 = echeance : attend un changement de son canal SANS garder le bout (un
; thread tue pendant qu'il dort -- son process tue -- ne doit garder aucune
; reference : le pair verrait jamais la fin), puis le reprend par sa poignee
; -> x0 = resultat d'ipc_sleep, x1 = bout repris (reference) ou E_BADF /
; IPC_E_NOTSOCK (poignee fermee ou reprise entre-temps)
iep_sleep:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	ldr	x19,[x19,IEP_CHAN]		; (la cle : la page du canal)
	bl	iobj_unref
	bl	ipc_reap
	mov	x0,x19
	mov	x1,x21
	mov	x2,x22
	bl	ipc_sleep1
	mov	x21,x0
	mov	x0,x20
	bl	iep_from
	mov	x1,x0
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sys_chan_send : x19 = bout, variables sur la pile de l'appelant, a +16 ici)
iep_sleep_send:
	prologue
	add	x9,sp,16
	mov	x0,x19
	ldr	x1,[x9,SND_H]
	ldr	x2,[x9,SND_SEQ]
	ldr	x3,[x9,SND_WAKE]
	bl	iep_sleep
	epilogue

; --- SYS_CHAN_SEND chan_send(poignee, message, drapeaux, attente ms) -> octets --
; Les iovecs rassemblees en un message dans l'anneau du pair, avec les
; poignees (prises a l'appelant : leurs objets gagnent une reference).
; SEQPACKET / DGRAM : entier ou E_AGAIN / attente, E_MSGSIZE au-dela de
; l'anneau ; STREAM : par morceaux (sans attente : ce qui tient). E_PIPE : le
; pair est ferme (ou SHUT_RD), ou soi SHUT_WR.
SND_IOV		= 0
SND_IOVCNT	= 8
SND_HANDLES	= 16
SND_NH		= 24
SND_TOTAL	= 32
SND_WAKE	= 40			; echeance
SND_FLAGS	= 48
SND_SEQ		= 56
SND_DAIF	= 64
SND_I		= 72			; poignee en cours (boucle), resultat d'une attente
SND_H		= 80			; la poignee du bout
SND_LOCALS	= 96
sys_chan_send:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,SND_LOCALS
	mov	x23,x1				; message
	str	x2,[sp,SND_FLAGS]
	str	x0,[sp,SND_H]
	mov	x0,x3
	bl	ipc_deadline
	str	x0,[sp,SND_WAKE]
	ldr	x0,[sp,SND_H]
	bl	iep_from
	tbnz	x0,63,.ret			; (rien de pris)
	mov	x19,x0				; bout
	mov	x22,x1				; PCB
	ldr	x20,[x19,IEP_CHAN]
	check_user x23,IPCM_SIZE,.fault
	ldr	x0,[x23,IPCM_IOV]
	str	x0,[sp,SND_IOV]
	ldr	w1,[x23,IPCM_IOVCNT]
	str	x1,[sp,SND_IOVCNT]
	ldr	x0,[x23,IPCM_HANDLES]
	str	x0,[sp,SND_HANDLES]
	ldr	w0,[x23,IPCM_NH]
	str	x0,[sp,SND_NH]
	cmp	x0,IPC_MAX_HANDLES
	b.hi	.inval
	lsl	x1,x0,3
	cbz	x1,.hok
	ldr	x0,[sp,SND_HANDLES]
	check_user x0,x1,.fault
.hok:	ldr	x0,[sp,SND_IOV]
	ldr	x1,[sp,SND_IOVCNT]
	bl	iov_check
	tbnz	x0,63,.res
	str	x0,[sp,SND_TOTAL]
	mov	x21,0				; octets envoyes
	ldr	x1,[x20,ICH_TYPE]
	cmp	x1,IPC_SOCK_STREAM
	b.ne	.pk
	cbz	x0,.res				; STREAM : 0 octet, rien
	b	.again
.pk:	ldr	x1,[sp,SND_NH]			; un paquet : tient-il dans un anneau ?
	lsl	x1,x1,3
	add	x1,x1,x0
	add	x1,x1,IRH_SIZE + 7
	and	x1,x1,not 7
	movl	x2,IRING_SIZE
	cmp	x1,x2
	b.hi	.msgsize
.again:	bl	ipc_seq_now
	str	x0,[sp,SND_SEQ]
	mov	x0,x20
	bl	spin_lock_irqsave
	str	x0,[sp,SND_DAIF]
	ldr	x24,[x19,IEP_PEER]
	ldr	x0,[x24,IEP_STATE]
	tbnz	x0,IEPB_CLOSED,.pipe
	tbnz	x0,IEPB_SHUT_RD,.pipe
	ldr	x0,[x19,IEP_STATE]
	tbnz	x0,IEPB_SHUT_WR,.pipe
	mov	x0,x24
	bl	iring_alloc
	cbz	x0,.nobufs
	ldr	x1,[x20,ICH_TYPE]
	cmp	x1,IPC_SOCK_STREAM
	b.ne	.whole
	; --- STREAM : un morceau ; les poignees avec le premier ---
	mov	x28,0
	cbnz	x21,.st1
	ldr	x28,[sp,SND_NH]
.st1:	mov	x0,x24
	bl	iring_room
	lsl	x1,x28,3
	add	x1,x1,IRH_SIZE
	add	x2,x1,8				; au moins un octet (arrondi)
	cmp	x0,x2
	b.lo	.full
	sub	x0,x0,x1			; octets qui tiennent
	ldr	x2,[sp,SND_TOTAL]
	sub	x2,x2,x21
	cmp	x2,x0
	csel	x27,x2,x0,lo
	b	.size
.whole:	ldr	x28,[sp,SND_NH]
	ldr	x27,[sp,SND_TOTAL]
.size:	lsl	x26,x28,3
	add	x26,x26,x27
	add	x26,x26,IRH_SIZE + 7
	and	x26,x26,not 7
	mov	x0,x24
	mov	x1,x26
	bl	iring_reserve
	tbnz	x0,63,.full
	mov	x25,x0				; position
	ldr	x0,[x24,IEP_RING]
	add	x23,x0,x25			; l'enregistrement
	str	w27,[x23,IRH_LEN]
	str	w28,[x23,IRH_NH]
	str	xzr,[x23,IRH_OFF]		; (et IRH_DONE)
	mov	x9,0				; poignees -> objets
.hd:	cmp	x9,x28
	b.hs	.data
	str	x9,[sp,SND_I]
	ldr	x10,[sp,SND_HANDLES]
	add	x10,x10,x9,lsl 3
	ldr	w1,[x10]
	mov	x0,x22
	bl	iht_get
	ldr	x9,[sp,SND_I]
	cbz	x0,.badh
	cmp	x0,x24				; le bout receveur par son canal
	b.eq	.selfh
	add	x10,x23,IRH_SIZE
	str	x0,[x10,x9,lsl 3]
	add	x9,x9,1
	b	.hd
.selfh:	bl	iobj_unref
	ldr	x9,[sp,SND_I]
	mov	x27,E_INVAL
	b	.u
.badh:	mov	x27,E_BADF
.u:	cbz	x9,.uend			; objets deja pris : rendus
	sub	x9,x9,1
	str	x9,[sp,SND_I]
	add	x10,x23,IRH_SIZE
	ldr	x0,[x10,x9,lsl 3]
	bl	iobj_unref
	ldr	x9,[sp,SND_I]
	b	.u
.uend:	mov	x0,x20
	ldr	x1,[sp,SND_DAIF]
	bl	spin_unlock_irqrestore
	mov	x0,x27
	b	.res
.data:	add	x0,x23,IRH_SIZE
	add	x0,x0,x28,lsl 3
	ldr	x1,[sp,SND_IOV]
	ldr	x2,[sp,SND_IOVCNT]
	mov	x3,x21
	mov	x4,x27
	mov	x5,0
	bl	iov_copy
	mov	x0,x24
	mov	x1,x25
	mov	x2,x26
	mov	x3,x27
	bl	iring_commit
	mov	x0,x20
	ldr	x1,[sp,SND_DAIF]
	bl	spin_unlock_irqrestore
	mov	x0,x20
	bl	ipc_wake
	add	x21,x21,x27
	ldr	x0,[sp,SND_TOTAL]
	cmp	x21,x0
	b.hs	.sent
	ldr	x1,[sp,SND_FLAGS]		; STREAM, reste : la suite (ou ce qui
	tbnz	x1,0,.sent			; est parti, sans attente)
	b	.again
.sent:	mov	x0,x21
	b	.res
.full:	mov	x0,x20				; pas de place
	ldr	x1,[sp,SND_DAIF]
	bl	spin_unlock_irqrestore
	ldr	x1,[sp,SND_FLAGS]
	tbz	x1,0,.wait
	mov	x0,E_AGAIN
	cbz	x21,.res
	mov	x0,x21
	b	.res
.wait:	bl	iep_sleep_send			; (le bout rendu pendant l'attente)
	tbnz	x1,63,.gone
	mov	x19,x1				; le bout, repris par sa poignee
	ldr	x20,[x19,IEP_CHAN]
	cbz	x0,.again
	cmp	x0,E_TIMEDOUT
	b.ne	.werr
	mov	x0,E_AGAIN			; (SO_SNDTIMEO : EAGAIN)
.werr:	cbz	x21,.res
	mov	x0,x21
	b	.res
.gone:	mov	x0,x1				; poignee fermee pendant l'attente
	cbz	x21,.ret
	mov	x0,x21
	b	.ret
.pipe:	mov	x0,x20
	ldr	x1,[sp,SND_DAIF]
	bl	spin_unlock_irqrestore
	mov	x0,IPC_E_PIPE
	cbz	x21,.res
	mov	x0,x21
	b	.res
.nobufs:
	mov	x0,x20
	ldr	x1,[sp,SND_DAIF]
	bl	spin_unlock_irqrestore
	mov	x0,IPC_E_NOBUFS
	b	.res
.msgsize:
	mov	x0,IPC_E_MSGSIZE
	b	.res
.inval:	mov	x0,E_INVAL
	b	.res
.fault:	mov	x0,E_FAULT
.res:	mov	x21,x0				; resultat ; le bout rendu
	mov	x0,x19
	bl	iobj_unref
	bl	ipc_reap
	mov	x0,x21
.ret:	add	sp,sp,SND_LOCALS
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_CHAN_RECV chan_recv(poignee, message, drapeaux, attente ms) -> octets --
; SEQPACKET / DGRAM : un message (coupe a la place des iovecs : IPC_TRUNC) ;
; STREAM : les octets de plusieurs messages, sans passer un message qui porte
; des poignees. Poignees : ajoutees a la table de l'appelant, ecrites dans
; IPCM_HANDLES (IPCM_NH : place en entree, nombre en sortie) ; sans place ou
; table pleine : rendues, IPC_CTRUNC. Fin (pair ferme ou SHUT_WR, soi SHUT_RD,
; anneau vide) : 0. Sans message : E_AGAIN (IPC_NOWAIT, delai) ou attente.
RCV_IOV		= 0
RCV_IOVCNT	= 8
RCV_HANDLES	= 16
RCV_HROOM	= 24
RCV_ROOM	= 32
RCV_WAKE	= 40
RCV_FLAGS	= 48
RCV_SEQ		= 56
RCV_DAIF	= 64
RCV_MSG		= 72
RCV_GOT		= 80			; poignees remises
RCV_MFLAGS	= 88
RCV_H		= 96			; la poignee du bout
RCV_LOCALS	= 112
sys_chan_recv:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,RCV_LOCALS
	str	x1,[sp,RCV_MSG]
	str	x2,[sp,RCV_FLAGS]
	str	x0,[sp,RCV_H]
	mov	x0,x3
	bl	ipc_deadline
	str	x0,[sp,RCV_WAKE]
	ldr	x0,[sp,RCV_H]
	bl	iep_from
	tbnz	x0,63,.ret
	mov	x19,x0
	mov	x22,x1
	ldr	x20,[x19,IEP_CHAN]
	ldr	x23,[sp,RCV_MSG]
	check_user x23,IPCM_SIZE,.fault
	ldr	x0,[x23,IPCM_IOV]
	str	x0,[sp,RCV_IOV]
	ldr	w1,[x23,IPCM_IOVCNT]
	str	x1,[sp,RCV_IOVCNT]
	ldr	x0,[x23,IPCM_HANDLES]
	str	x0,[sp,RCV_HANDLES]
	ldr	w0,[x23,IPCM_NH]
	mov	x1,IPC_MAX_HANDLES
	cmp	x0,x1
	csel	x0,x1,x0,hi
	str	x0,[sp,RCV_HROOM]
	lsl	x1,x0,3
	cbz	x1,.hok
	ldr	x0,[sp,RCV_HANDLES]
	check_user x0,x1,.fault
.hok:	ldr	x0,[sp,RCV_IOV]
	ldr	x1,[sp,RCV_IOVCNT]
	bl	iov_check
	tbnz	x0,63,.res
	str	x0,[sp,RCV_ROOM]
.again:	str	xzr,[sp,RCV_GOT]
	str	xzr,[sp,RCV_MFLAGS]
	bl	ipc_seq_now
	str	x0,[sp,RCV_SEQ]
	mov	x0,x20
	bl	spin_lock_irqsave
	str	x0,[sp,RCV_DAIF]
	ldr	x0,[x19,IEP_STATE]
	tbnz	x0,IEPB_SHUT_RD,.eof
	ldr	x0,[x19,IEP_NREC]
	cbnz	x0,.have
	ldr	x24,[x19,IEP_PEER]
	ldr	x0,[x24,IEP_STATE]
	tbnz	x0,IEPB_CLOSED,.eof
	tbnz	x0,IEPB_SHUT_WR,.eof
	mov	x0,x20				; vide
	ldr	x1,[sp,RCV_DAIF]
	bl	spin_unlock_irqrestore
	ldr	x1,[sp,RCV_FLAGS]
	mov	x0,E_AGAIN
	tbnz	x1,0,.res
	mov	x0,x19				; le bout rendu pendant l'attente,
	ldr	x1,[sp,RCV_H]			; repris par sa poignee apres
	ldr	x2,[sp,RCV_SEQ]
	ldr	x3,[sp,RCV_WAKE]
	bl	iep_sleep
	tbnz	x1,63,.gone
	mov	x19,x1
	ldr	x20,[x19,IEP_CHAN]
	cbz	x0,.again
	cmp	x0,E_TIMEDOUT
	b.ne	.res
	mov	x0,E_AGAIN
	b	.res
.gone:	mov	x0,x1				; poignee fermee pendant l'attente
	b	.ret
.eof:	mov	x0,x20
	ldr	x1,[sp,RCV_DAIF]
	bl	spin_unlock_irqrestore
	mov	x21,0
	b	.out_msg
.have:	mov	x21,0				; octets copies
	ldr	x1,[x20,ICH_TYPE]
	cmp	x1,IPC_SOCK_STREAM
	b.eq	.stream
	; --- un paquet ---
	mov	x0,x19
	bl	iring_head
	mov	x23,x0
	ldr	w24,[x23,IRH_LEN]
	ldr	x2,[sp,RCV_ROOM]
	cmp	x24,x2
	csel	x21,x24,x2,lo
	b.ls	.pcopy
	mov	x0,IPC_TRUNC
	str	x0,[sp,RCV_MFLAGS]
.pcopy:	ldr	w0,[x23,IRH_NH]
	add	x0,x23,x0,lsl 3
	add	x0,x0,IRH_SIZE
	ldr	x1,[sp,RCV_IOV]
	ldr	x2,[sp,RCV_IOVCNT]
	mov	x3,0
	mov	x4,x21
	mov	x5,1
	bl	iov_copy
	ldr	x0,[sp,RCV_FLAGS]
	tbnz	x0,1,.unlock			; IPC_PEEK : rien ne bouge
	mov	x0,x23
	bl	irecv_handles
	ldr	x0,[x19,IEP_QUEUED]
	sub	x0,x0,x24
	str	x0,[x19,IEP_QUEUED]
	mov	x0,x19
	bl	iring_pop
	b	.unlock
	; --- STREAM ---
.stream:
	ldr	x0,[x19,IEP_NREC]
	cbz	x0,.unlock
	ldr	x2,[sp,RCV_ROOM]
	cmp	x21,x2
	b.hs	.unlock
	mov	x0,x19
	bl	iring_head
	mov	x23,x0
	ldr	w0,[x23,IRH_NH]
	cbz	w0,.sdata
	ldr	w1,[x23,IRH_DONE]
	cbnz	w1,.sdata
	cbnz	x21,.unlock			; des poignees : seulement en tete
	ldr	x0,[sp,RCV_FLAGS]
	tbnz	x0,1,.sdata
	mov	x0,x23
	bl	irecv_handles
	mov	w0,1
	str	w0,[x23,IRH_DONE]
.sdata:	ldr	w24,[x23,IRH_LEN]
	ldr	w25,[x23,IRH_OFF]
	sub	x26,x24,x25			; reste dans ce message
	ldr	x2,[sp,RCV_ROOM]
	sub	x2,x2,x21
	cmp	x26,x2
	csel	x26,x26,x2,lo
	ldr	w0,[x23,IRH_NH]
	add	x0,x23,x0,lsl 3
	add	x0,x0,IRH_SIZE
	add	x0,x0,x25
	ldr	x1,[sp,RCV_IOV]
	ldr	x2,[sp,RCV_IOVCNT]
	mov	x3,x21
	mov	x4,x26
	mov	x5,1
	bl	iov_copy
	add	x21,x21,x26
	ldr	x0,[sp,RCV_FLAGS]
	tbnz	x0,1,.unlock			; IPC_PEEK : le premier message seul
	add	x25,x25,x26
	str	w25,[x23,IRH_OFF]
	ldr	x0,[x19,IEP_QUEUED]
	sub	x0,x0,x26
	str	x0,[x19,IEP_QUEUED]
	cmp	x25,x24
	b.lo	.unlock				; message pas fini : la place est pleine
	mov	x0,x19
	bl	iring_pop
	b	.stream
.unlock:
	mov	x0,x20
	ldr	x1,[sp,RCV_DAIF]
	bl	spin_unlock_irqrestore
	mov	x0,x20				; de la place pour l'envoyeur
	bl	ipc_wake
.out_msg:
	ldr	x23,[sp,RCV_MSG]
	ldr	x0,[sp,RCV_GOT]
	str	w0,[x23,IPCM_NH]
	ldr	x0,[sp,RCV_MFLAGS]
	str	w0,[x23,IPCM_FLAGS]
	mov	x0,x21
	b	.res
.fault:	mov	x0,E_FAULT
.res:	mov	x21,x0
	mov	x0,x19
	bl	iobj_unref
	bl	ipc_reap
	mov	x0,x21
.ret:	add	sp,sp,RCV_LOCALS
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sys_chan_recv, sous ICH_LOCK ; sp de l'appelant = ses variables, a +16
; ici) x0 = enregistrement : ses poignees passent a la table de l'appelant
; (x22 = PCB) et dans son tableau ; sans place : rendues, IPC_CTRUNC
irecv_handles:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x23,[sp,-16]!
	mov	x19,x0
	add	x23,sp,48			; variables de sys_chan_recv
	mov	x20,0
.h:	ldr	w0,[x19,IRH_NH]
	cmp	x20,x0
	b.hs	.out
	add	x21,x19,IRH_SIZE
	ldr	x21,[x21,x20,lsl 3]		; l'objet
	ldr	x0,[x23,RCV_GOT]
	ldr	x1,[x23,RCV_HROOM]
	cmp	x0,x1
	b.hs	.drop
	mov	x0,x22
	mov	x1,x21
	mov	x2,0
	bl	iht_install
	tbnz	x0,63,.drop
	ldr	x1,[x23,RCV_GOT]
	ldr	x2,[x23,RCV_HANDLES]
	add	x2,x2,x1,lsl 3
	str	w0,[x2]
	mov	x0,x21
	bl	iobj_info
	ldr	x1,[x23,RCV_GOT]
	ldr	x2,[x23,RCV_HANDLES]
	add	x2,x2,x1,lsl 3
	str	w0,[x2,4]
	add	x1,x1,1
	str	x1,[x23,RCV_GOT]
	b	.n
.drop:	mov	x0,x21
	bl	iobj_unref
	ldr	x0,[x23,RCV_MFLAGS]
	orr	x0,x0,IPC_CTRUNC
	str	x0,[x23,RCV_MFLAGS]
.n:	add	x20,x20,1
	b	.h
.out:	mov	w0,1
	str	w0,[x19,IRH_DONE]
	ldp	x21,x23,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_CHAN_SHUTDOWN chan_shutdown(poignee, IPC_SHUT_RD | IPC_SHUT_WR) -> 0 ----
; Reveille tout ce qui attend sur le canal (WebKit : la fin d'une connexion
; reveille son thread de surveillance) ; ensuite recv -> 0, send -> E_PIPE.
sys_chan_shutdown:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x1
	bl	iep_from
	tbnz	x0,63,.out
	mov	x19,x0
	ldr	x20,[x19,IEP_CHAN]
	mov	x0,x20
	bl	spin_lock_irqsave
	mov	x22,x0
	ldr	x0,[x19,IEP_STATE]
	tbz	x21,0,.w
	orr	x0,x0,1 shl IEPB_SHUT_RD
.w:	tbz	x21,1,.s
	orr	x0,x0,1 shl IEPB_SHUT_WR
.s:	str	x0,[x19,IEP_STATE]
	mov	x0,x20
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x20
	bl	ipc_wake
	mov	x0,x19
	bl	iobj_unref
	bl	ipc_reap
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ===========================================================================
;  Memoire partagee
; ===========================================================================

; x0 = IPC_SHM_SEALABLE ou 0 -> x0 = objet neuf (une reference, taille 0), 0
ishm_new:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	pmm_alloc
	cbz	x0,.out
	str	x0,[sp,-16]!
	mov	x1,PAGE_SIZE
	bl	mem_zero
	ldr	x0,[sp],16
	mov	w1,IPC_KIND_SHM
	str	w1,[x0,IOB_TYPE]
	mov	x1,1
	str	x1,[x0,IOB_REFS]
	str	w19,[x0,ISH_FLAGS]
	mov	x19,x0
	adr	x0,ipc_shm_ids
	mov	x1,1
	bl	iatomic_add
	str	x0,[x19,ISH_ID]
	mov	x0,x19
.out:	ldr	x19,[sp],16
	epilogue

; x0 = objet sans reference : son nom faible, ses pages, ses tables, lui
ishm_destroy:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,isn_lock
	bl	spin_lock_irqsave
	mov	x20,x0
	ldr	w0,[x19,ISH_FLAGS]
	tbz	w0,2,.named			; pas ISHF_WEAK
	adr	x1,isn_table
	mov	x2,0
.s:	cmp	x2,ISN_MAX
	b.hs	.named
	ldr	x3,[x1,ISN_OBJ]
	cmp	x3,x19
	b.eq	.clear
	add	x1,x1,ISN_SIZE
	add	x2,x2,1
	b	.s
.clear:	str	xzr,[x1,ISN_OBJ]
	strb	wzr,[x1,ISN_NAME]
.named:	adr	x0,isn_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
	mov	x20,0
.d:	cmp	x20,ISH_DIR_N
	b.hs	.self
	add	x0,x19,ISH_DIR
	ldr	x21,[x0,x20,lsl 3]
	cbz	x21,.dn
	mov	x22,0
.p:	ldr	x0,[x21,x22,lsl 3]
	cbz	x0,.pn
	bl	pmm_free
.pn:	add	x22,x22,1
	cmp	x22,512
	b.lo	.p
	mov	x0,x21
	bl	pmm_free
.dn:	add	x20,x20,1
	b	.d
.self:	mov	x0,x19
	bl	pmm_free
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = objet (l'appelant y tient une reference), x1 = 1 : epingler, 0 : relacher
; -> x0 = 0, E_INVAL (pas de nom faible). Une epingle = une reference sans process
ishm_pin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,isn_lock
	bl	spin_lock_irqsave
	mov	x21,x0
	mov	x22,E_INVAL
	ldr	w0,[x19,ISH_FLAGS]
	tbz	w0,2,.unl			; pas ISHF_WEAK
	mov	x22,0
	cbz	x20,.off
	tbnz	w0,1,.unl			; deja IPC_SHM_PINNED
	orr	w0,w0,IPC_SHM_PINNED
	str	w0,[x19,ISH_FLAGS]
	mov	x0,x19
	bl	iobj_ref
	b	.unl
.off:	tbz	w0,1,.unl
	and	w0,w0,0xFFFFFFFD
	str	w0,[x19,ISH_FLAGS]
	mov	x0,x19
	bl	iobj_unref			; (pas la derniere : celle de l'appelant)
.unl:	adr	x0,isn_lock
	mov	x1,x21
	bl	spin_unlock_irqrestore
	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = objet : une region de plus le projette (reference comprise)
ishm_mapped:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	iobj_ref
	add	x0,x19,ISH_MAPS
	mov	x1,1
	bl	iatomic_add
	ldr	x19,[sp],16
	epilogue

; x0 = objet : une region de moins (sa reference rendue)
ishm_unmapped:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	add	x0,x19,ISH_MAPS
	mov	x1,-1
	bl	iatomic_add
	mov	x0,x19
	bl	iobj_unref
	ldr	x19,[sp],16
	epilogue

; (vm_destroy) x0 = PCB : chaque region VMK_SHARED rend sa reference
ishm_vma_release:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.r:	ldr	x1,[x19,P_VMA_N]
	cmp	x20,x1
	b.hs	.out
	ldr	x1,[x19,P_VMA]
	add	x1,x1,x20,lsl 5
	add	x20,x20,1
	ldrb	w2,[x1,VMA_KIND]
	cmp	w2,VMK_SHARED
	b.ne	.r
	ldr	x0,[x1,24]
	bl	ishm_unmapped
	b	.r
.out:	ldp	x19,x20,[sp],16
	epilogue

; (sous P_VMLOCK, ou rien) x0 = objet, x1 = page dans l'objet -> x0 = adresse
; physique de la page (creee a zero au premier besoin) ; 0 : au-dela de la
; taille de l'objet, ou plus de memoire
ishm_page:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	add	x0,x19,ISH_LOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x21,0
	ldr	x0,[x19,ISH_SIZE]
	add	x0,x0,0xFFF
	lsr	x0,x0,12
	cmp	x20,x0
	b.hs	.unl
	lsr	x1,x20,9
	add	x2,x19,ISH_DIR
	add	x2,x2,x1,lsl 3			; entree du repertoire
	ldr	x21,[x2]
	cbnz	x21,.leaf
	str	x2,[sp,-16]!
	bl	pmm_alloc
	ldr	x2,[sp],16
	cbz	x0,.unl0
	str	x0,[x2]
	mov	x21,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
.leaf:	and	x1,x20,511
	add	x20,x21,x1,lsl 3		; entree de la page
	ldr	x21,[x20]
	cbnz	x21,.unl
	bl	pmm_alloc
	cbz	x0,.unl0
	mov	x21,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	dmb	ishst
	str	x21,[x20]
	b	.unl
.unl0:	mov	x21,0
.unl:	add	x0,x19,ISH_LOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = objet, x1 = nouvelle taille -> x0 = 0, E_INVAL (trop grand), IPC_E_PERM
; (sceau), IPC_E_BUSY (retrecir un objet projete). Retrecir : pages au-dela
; rendues, fin de la derniere mise a zero.
ishm_resize:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	movl	x0,ISH_MAX
	cmp	x20,x0
	b.hi	.inval
	add	x0,x19,ISH_LOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	ldr	x21,[x19,ISH_SIZE]
	ldr	w0,[x19,ISH_SEALS]
	mov	x23,0
	cmp	x20,x21
	b.eq	.set
	b.lo	.shrink
	tbz	w0,2,.set			; IPC_SEAL_GROW
	mov	x23,IPC_E_PERM
	b	.unl
.shrink:
	tbz	w0,1,.s1			; IPC_SEAL_SHRINK
	mov	x23,IPC_E_PERM
	b	.unl
.s1:	ldr	x0,[x19,ISH_MAPS]
	cbz	x0,.s2
	mov	x23,IPC_E_BUSY
	b	.unl
.s2:	add	x24,x20,0xFFF			; premiere page entiere au-dela
	lsr	x24,x24,12
	add	x21,x21,0xFFF
	lsr	x21,x21,12			; pages de l'ancienne taille
.fp:	cmp	x24,x21
	b.hs	.tail
	lsr	x1,x24,9
	add	x2,x19,ISH_DIR
	ldr	x2,[x2,x1,lsl 3]
	add	x24,x24,1
	cbz	x2,.fp
	sub	x1,x24,1
	and	x1,x1,511
	add	x2,x2,x1,lsl 3
	ldr	x0,[x2]
	cbz	x0,.fp
	str	xzr,[x2]
	bl	pmm_free
	b	.fp
.tail:	ands	x1,x20,0xFFF			; fin de la page partielle a zero
	b.eq	.set
	lsr	x2,x20,12
	lsr	x3,x2,9
	add	x4,x19,ISH_DIR
	ldr	x4,[x4,x3,lsl 3]
	cbz	x4,.set
	and	x2,x2,511
	ldr	x4,[x4,x2,lsl 3]
	cbz	x4,.set
	add	x4,x4,x1
	mov	x2,PAGE_SIZE
	sub	x2,x2,x1
.z:	strb	wzr,[x4],1
	subs	x2,x2,1
	b.ne	.z
.set:	str	x20,[x19,ISH_SIZE]
.unl:	add	x0,x19,ISH_LOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x23
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = poignee -> x0 = objet partage (reference), x1 = PCB ; x0 = E_BADF,
; E_INVAL (pas un objet partage)
ishm_from:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	current_proc
	mov	x20,x1
	mov	x0,x20
	mov	x1,x19
	bl	iht_get
	cbz	x0,.badf
	ldr	w1,[x0,IOB_TYPE]
	cmp	w1,IPC_KIND_SHM
	b.eq	.ok
	bl	iobj_unref
	bl	ipc_reap
	mov	x0,E_INVAL
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.ok:	mov	x1,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_SHM_CREATE shm_create(taille, drapeaux) -> poignee ---------------------
sys_shm_create:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	and	x0,x1,IPC_SHM_SEALABLE
	bl	ishm_new
	cbz	x0,.nomem
	mov	x20,x0
	cbz	x19,.inst
	mov	x1,x19
	bl	ishm_resize
	cbnz	x0,.fail
.inst:	current_proc
	mov	x0,x1
	mov	x1,x20
	mov	x2,0
	bl	iht_install
	tbz	x0,63,.out
.fail:	mov	x19,x0
	mov	x0,x20
	bl	iobj_unref
	mov	x0,x19
	b	.out
.nomem:	mov	x0,E_NOMEM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom (EL0), x1 = tampon noyau de ISN_NAME_MAX -> x0 = longueur,
; IPC_E_NAMELONG, E_INVAL (vide), E_FAULT
ishm_name:
	prologue
	mov	x2,ISN_NAME_MAX
	bl	copy_str_from_user
	cmp	x0,E_INVAL
	b.ne	.l
	mov	x0,IPC_E_NAMELONG
	b	.out
.l:	cbnz	x0,.out
	mov	x0,E_INVAL
.out:	epilogue

; x0 = nom noyau -> x0 = entree de isn_table qui le porte (0 : aucune) ;
; sous isn_lock
isn_find:
	adr	x1,isn_table
	mov	x2,0
.e:	cmp	x2,ISN_MAX
	b.hs	.none
	ldr	x3,[x1,ISN_OBJ]
	cbz	x3,.n
	ldr	x4,[x3,IOB_REFS]		; (nom faible d'un objet qui meurt)
	cbz	x4,.n
	mov	x4,0
.c:	add	x5,x1,ISN_NAME
	ldrb	w6,[x5,x4]
	ldrb	w7,[x0,x4]
	cmp	w6,w7
	b.ne	.n
	add	x4,x4,1
	cbnz	w6,.c
	mov	x0,x1
	ret
.n:	add	x1,x1,ISN_SIZE
	add	x2,x2,1
	b	.e
.none:	mov	x0,0
	ret

; --- SYS_SHM_OPEN shm_open(nom, IPC_SHM_CREAT | EXCL | TRUNC) -> poignee ---------
; Noms globaux (256 au plus, 55 caracteres) ; l'objet vit tant qu'il a un nom
; ou une reference. IPC_SHM_WEAK, a la creation : le nom ne tient pas l'objet,
; et part avec lui.
sys_shm_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,ISN_NAME_MAX + 8
	mov	x19,x1				; drapeaux
	mov	x1,sp
	bl	ishm_name
	tbnz	x0,63,.out
	adr	x0,isn_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,sp
	bl	isn_find
	cbz	x0,.new
	tst	x19,IPC_SHM_EXCL
	b.eq	.old
	tst	x19,IPC_SHM_CREAT
	b.eq	.old
	mov	x21,E_EXIST
	b	.unl
.old:	ldr	x20,[x0,ISN_OBJ]
	mov	x0,x20
	bl	iobj_ref_live
	mov	x21,0
	cbnz	x0,.unl
.new:	mov	x21,E_NOENT
	tst	x19,IPC_SHM_CREAT
	b.eq	.unl
	adr	x1,isn_table			; une place libre
	mov	x2,0
.f:	cmp	x2,ISN_MAX
	b.hs	.nfile
	ldr	x3,[x1,ISN_OBJ]
	cbz	x3,.slot
	add	x1,x1,ISN_SIZE
	add	x2,x2,1
	b	.f
.nfile:	mov	x21,IPC_E_NFILE
	b	.unl
.slot:	str	x1,[sp,ISN_NAME_MAX]
	mov	x0,0
	tst	x19,IPC_SHM_WEAK
	b.eq	.mk
	mov	x0,ISHF_WEAK
.mk:	bl	ishm_new
	mov	x21,E_NOMEM
	cbz	x0,.unl
	mov	x20,x0
	tst	x19,IPC_SHM_WEAK
	b.ne	.nm
	bl	iobj_ref			; (le nom, et la poignee)
.nm:	ldr	x1,[sp,ISN_NAME_MAX]
	str	x20,[x1,ISN_OBJ]
	add	x0,x1,ISN_NAME
	mov	x1,sp
	mov	x2,ISN_NAME_MAX
.cp:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	sub	x2,x2,1
	cbz	w3,.cpd
	cbnz	x2,.cp
.cpd:	mov	x21,0
.unl:	adr	x0,isn_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x21
	cbnz	x21,.out
	tst	x19,IPC_SHM_TRUNC
	b.eq	.inst
	mov	x0,x20
	mov	x1,0
	bl	ishm_resize
.inst:	current_proc
	mov	x0,x1
	mov	x1,x20
	mov	x2,0
	bl	iht_install
	tbz	x0,63,.out
	mov	x21,x0
	mov	x0,x20
	bl	iobj_unref
	mov	x0,x21
.out:	add	sp,sp,ISN_NAME_MAX + 8
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_SHM_UNLINK shm_unlink(nom) -> 0 -------------------------------------------
; Le nom part ; un nom faible emporte l'epingle de son objet.
sys_shm_unlink:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,ISN_NAME_MAX + 8
	mov	x1,sp
	bl	ishm_name
	tbnz	x0,63,.out
	adr	x0,isn_lock
	bl	spin_lock_irqsave
	mov	x20,x0
	mov	x0,sp
	bl	isn_find
	mov	x19,0
	mov	x21,E_NOENT
	cbz	x0,.unl
	mov	x21,0
	ldr	x19,[x0,ISN_OBJ]
	str	xzr,[x0,ISN_OBJ]
	strb	wzr,[x0,ISN_NAME]
	ldr	w1,[x19,ISH_FLAGS]
	tbz	w1,2,.unl			; (pas ISHF_WEAK : la reference du nom)
	and	w2,w1,0xFFFFFFF9		; ni ISHF_WEAK ni IPC_SHM_PINNED
	str	w2,[x19,ISH_FLAGS]
	tbnz	w1,1,.unl			; (la reference de l'epingle)
	mov	x19,0
.unl:	adr	x0,isn_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
	cbz	x19,.res
	mov	x0,x19
	bl	iobj_unref
.res:	mov	x0,x21
.out:	add	sp,sp,ISN_NAME_MAX + 8
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_SHM_CTL shm_ctl(poignee, op, valeur) -> 0 ---------------------------------
; IPC_SHM_SET_SIZE : ftruncate ; IPC_SHM_ADD_SEALS : F_ADD_SEALS (objet cree
; IPC_SHM_SEALABLE, pas encore IPC_SEAL_SEAL : sinon IPC_E_PERM) ; IPC_SHM_PIN,
; IPC_SHM_UNPIN : l'objet d'un nom faible reste sans process, ou plus
sys_shm_ctl:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	bl	ishm_from
	tbnz	x0,63,.out
	mov	x19,x0
	cmp	x20,IPC_SHM_SET_SIZE
	b.ne	.seal
	mov	x1,x21
	bl	ishm_resize
	b	.res
.seal:	cmp	x20,IPC_SHM_PIN
	mov	x1,1
	b.eq	.pin
	cmp	x20,IPC_SHM_UNPIN
	mov	x1,0
	b.ne	.seals
.pin:	mov	x0,x19
	bl	ishm_pin
	b	.res
.seals:	cmp	x20,IPC_SHM_ADD_SEALS
	mov	x0,E_INVAL
	b.ne	.res
	add	x0,x19,ISH_LOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x20,IPC_E_PERM
	ldr	w0,[x19,ISH_FLAGS]
	tbz	w0,0,.sunl			; pas IPC_SHM_SEALABLE
	ldr	w0,[x19,ISH_SEALS]
	tbnz	w0,0,.sunl			; deja IPC_SEAL_SEAL
	and	x1,x21,0xF
	orr	w0,w0,w1
	str	w0,[x19,ISH_SEALS]
	mov	x20,0
.sunl:	add	x0,x19,ISH_LOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x20
.res:	mov	x20,x0
	mov	x0,x19
	bl	iobj_unref
	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_SHM_MAP shm_map(poignee, adresse, taille, droits, drapeaux, decalage) ---
; -> adresse. Une region VMK_SHARED dans l'arene de VM_MAP (placee comme
; VM_MAP : indication, VM_FIXED, VM_FIXED_NOREPLACE), peuplee a la demande
; avec les pages de l'objet. Decalage aligne sur une page ; une page au-dela
; de la taille de l'objet fait une faute (le process est tue, comme SIGBUS).
; VM_COW : memes pages, en copie sur ecriture (PTE_COW, vmspace.asm) ; meme
; avec PROT_WRITE ou mprotect, l'objet ne change pas (mmap d'un fichier).
sys_shm_map:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x20,x1				; adresse
	mov	x21,x2				; taille
	mov	x22,x3				; droits
	mov	x23,x4				; drapeaux
	mov	x25,x5				; decalage
	bl	ishm_from
	tbnz	x0,63,.out
	mov	x26,x0				; objet (reference)
	mov	x24,E_INVAL
	cbz	x21,.err
	tst	x22,not 7
	b.ne	.err
	tst	x25,0xFFF
	b.ne	.err
	lsr	x25,x25,12			; page de depart dans l'objet
	lsr	x0,x25,24
	cbnz	x0,.err
	tbnz	x23,21,.sw			; (VM_COW : l'objet n'est pas ecrit)
	ldr	w0,[x26,ISH_SEALS]
	tbz	w0,3,.sw			; IPC_SEAL_WRITE : pas d'ecriture
	mov	x24,IPC_E_PERM
	tbnz	x22,1,.err
.sw:	mov	x24,E_NOMEM
	movl	x0,VM_ARENA_END - VM_ARENA_BASE
	cmp	x21,x0
	b.hi	.err
	add	x21,x21,0xFFF
	and	x21,x21,not 0xFFF
	current_proc
	mov	x19,x1
	movl	x0,VM_FIXED or VM_FIXED_NOREPLACE
	tst	x23,x0
	b.eq	.lock
	mov	x24,E_INVAL
	tst	x20,0xFFF
	b.ne	.err
	mov	x0,x20
	mov	x1,x21
	bl	vm_in_arena
	cbz	x0,.err
.lock:	add	x0,x19,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x24,x0				; (DAIF)
	movl	x0,VM_FIXED or VM_FIXED_NOREPLACE
	tst	x23,x0
	b.eq	.gap
	tbz	x23,20,.repl			; VM_FIXED_NOREPLACE
	mov	x0,x19
	mov	x1,x20
	add	x2,x20,x21
	bl	vma_overlaps
	mov	x1,E_EXIST_VM
	cbnz	x0,.unlock_err
	b	.ins
.repl:	mov	x0,x19				; VM_FIXED : l'ancien contenu part
	mov	x1,x20
	add	x2,x20,x21
	bl	vm_unmap_locked
	mov	x1,E_NOMEM
	cbz	x0,.unlock_err
	b	.ins
.gap:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	vm_find_gap
	mov	x1,E_NOMEM
	cbz	x0,.unlock_err
	mov	x20,x0
.ins:	mov	x0,x19
	mov	x1,x20
	bl	vma_lower
	mov	x1,x0
	mov	x0,x19
	mov	x2,x20
	add	x3,x20,x21
	lsl	x4,x25,8			; sorte : VMK_SHARED | page << 8
	orr	x4,x4,VMK_SHARED
	lsl	x4,x4,32
	orr	x4,x4,x22
	tbz	x23,21,.ins5
	orr	x4,x4,VMP_COW
.ins5:	mov	x5,x26
	bl	vma_insert_at5
	mov	x1,E_NOMEM
	cbz	x0,.unlock_err
	add	x0,x26,ISH_MAPS			; la reference passe a la region
	mov	x1,1
	bl	iatomic_add
	add	x0,x19,P_VMLOCK
	mov	x1,x24
	bl	spin_unlock_irqrestore
	mov	x0,x20
	b	.out
.unlock_err:
	mov	x20,x1
	add	x0,x19,P_VMLOCK
	mov	x1,x24
	bl	spin_unlock_irqrestore
	mov	x24,x20
.err:	mov	x0,x26
	bl	iobj_unref
	mov	x0,x24
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
