; ============================================================================
;  svc.asm - services nommes (docs/MICRO-NOYAU.md, 3.3-1) : un programme de
;  SD0:/sys s'enregistre sous un nom (vfs, netd, ws...) ; les autres l'ouvrent
;  par ce nom et lui envoient des requetes.
;
;  Message (512 octets, comme ws.asm) : +0 op (requete) / resultat (reponse),
;  +8 a, +16 b, +24 c, +32 d, +40 longueur des donnees, +44 pid de l'appelant
;  (rempli par le noyau), +48 donnees (464 octets). Les gros transferts passent
;  par les tampons partages (appels 70-73).
;
;  Attentes : le thread s'endort et c'est la reponse (ou la requete, pour le
;  service) qui le reveille aussitot ; une echeance borne l'attente.
;  Fin d'un process : ses requetes sont abandonnees ; s'il tenait des
;  services, leurs requetes en cours echouent (E_PIPE) ; les services
;  enregistres avec SVF_FINS recoivent l'op SVC_OP_FIN (a = pid termine,
;  b = son code de sortie). Creation d'un process : ceux enregistres avec
;  SVF_NAISSANCES recoivent SVC_OP_NAISSANCE (a = fils, b = pere, 0 : le
;  noyau) avant que son premier thread soit executable ; apres un fork
;  (fork.asm), SVC_OP_FORK (a = fils, b = pere) : le fils tient tout ce que
;  tenait le pere (memes numeros), son thread ne tourne qu'apres l'avis.
;
;  Signaux d'un service : un mot de 64 bits (SV_SIGNALS). SVC_SIGNAL y fait un
;  OU sans bloquer ni allouer et ne peut pas deborder ; le service n'est
;  reveille que si le mot etait a 0. SVC_NEXT rend alors l'op SVC_OP_SIGNAL
;  (a = bits, remis a 0), sans reponse, avant les requetes en file.
;
;  Memoire : un service SVF_MEMORY garde de la memoire qu'il peut rendre (le
;  cache de fatfs). Sous MEM_LOW_PAGES libres, le faucheur lui leve
;  SVC_SIG_LOWMEM (mem_watch) ; un thread qui n'a plus de page en fait autant et
;  attend le resultat (mem_wait) au lieu d'echouer.
;
;  Table des requetes (SR_MAX places pour tous les services, avis compris) :
;  un avis du noyau garde sa place jusqu'a ce que le service le prenne
;  (SVC_NEXT). Une rafale de fork + exit (forktest : 5 avis par fork, une
;  douzaine par fin) peut la remplir plus vite que les services ne la vident.
;  Alors l'appel d'un programme attend une place (SVC_ATTENTE_MS a la fois,
;  jusqu'a son echeance) au lieu de rendre E_BUSY tout de suite, et il laisse
;  les SR_RESERVE dernieres places ; un avis attend aussi (SVC_AVIS_MS au plus)
;  au lieu d'etre perdu, et laisse les SR_RESERVE_AVIS dernieres : le fork en
;  rafale ralentit, les fils gardent les fichiers du pere, les services
;  apprennent chaque fin. Un process qui a des droits (services, pilotes)
;  n'attend jamais une place et prend les dernieres : c'est lui qui vide la
;  file (fatfs appelle d'autres services en traitant une requete ; s'il
;  attendait derriere ses propres avis, rien ne bougerait plus).
; ============================================================================

SVC_MAX		= 64
SVC_NOM		= 32
SV_NOM		= 0			; nom (32 octets)
SV_PID		= 32			; process qui le sert (0 : libre)
SV_SIGNALS	= 40			; bits leves par SVC_SIGNAL (les threads en
					; attente portent TH_WAIT = l'entree)
SV_FLAGS	= 48
SV_SIZE		= 64
SVF_FINS	= 1			; prevenu des fins de process
SVF_NAISSANCES	= 2			; prevenu des naissances (docs/FICHIERS.md, D2)
SVF_MEMORY	= 4			; prevenu quand la memoire manque (mem_watch, mem_wait)
SVF_POST	= 8			; peut deposer des evenements chez les programmes
					; (WS_POST, comme le serveur graphique : busd, docs/BUS.md)

SR_MAX		= 128			; requetes en cours (avis compris)
SR_ETAT		= 0			; 0 libre, 1 en file, 2 prise, 3 repondue
SR_SVC		= 8
SR_PID		= 16			; appelant (0 : noyau, ou appelant disparu)
SR_THREAD	= 24			; thread de l'appelant (qui attend)
SR_ORDRE	= 32			; ordre d'arrivee
SR_MSG		= 48
SR_SIZE		= SR_MSG + WS_MSG

SVC_OP_FIN	= 0xFFFF		; (noyau -> service) a = pid termine
SVC_OP_NAISSANCE = 0xFFFE		; (noyau -> service) a = fils, b = pere
SVC_OP_FORK	= 0xFFFC		; (noyau -> service) a = fils, b = pere (fork)
SVC_PID_NOYAU	= 0x7FFFFFFF		; appelant : un thread du noyau
SVC_CALL_MS	= 30000			; delai d'une requete
SR_RESERVE	= 24			; places que les appels des programmes laissent libres
SR_RESERVE_AVIS	= 8			; celles que les avis laissent aux services
SVC_ATTENTE_MS	= 2			; table pleine : nouvel essai toutes les 2 ms
SVC_AVIS_MS	= 2000			; un avis attend une place au plus ce delai
E_PIPE		= -32

macro svc_prendre {			; -> x28 = DAIF (x28 sauve par l'appelant)
	adr	x0,svc_lock
	bl	spin_lock_irqsave
	mov	x28,x0
}
macro svc_rendre {
	adr	x0,svc_lock
	mov	x1,x28
	bl	spin_unlock_irqrestore
}

; x0 = delai en ms -> x0 = echeance (compteur CNTPCT)
svc_echeance:
	mrs	x1,cntfrq_el0
	mul	x0,x0,x1
	mov	x1,1000
	udiv	x0,x0,x1
	mrs	x1,cntpct_el0
	add	x0,x0,x1
	ret

; (sous svc_lock, x28 = DAIF) x0 = echeance : endort le thread courant
; jusqu'a son reveil (svc_reveil) ou l'echeance, puis reprend le verrou
svc_dormir:
	prologue
	this_cpu x1
	ldr	x2,[x1,PC_CURRENT]
	str	x0,[x2,TH_WAKE]
	mov	x0,x2
	mov	x1,TS_SLEEPING
	bl	thread_park
	adr	x0,svc_lock
	bl	spin_unlock			; IRQ toujours masquees
	bl	schedule
	adr	x0,svc_lock
	bl	spin_lock			; (IRQ masquees : DAIF inchange)
	epilogue

; (sous svc_lock) x0 = thread (0 : aucun) : le reveille s'il dort
svc_reveil:
	cbz	x0,.out
	ldr	x1,[x0,TH_STATE]
	cmp	x1,TS_SLEEPING
	b.ne	.out
	mov	x1,WAKE_BOOST
	str	x1,[x0,TH_BOOST]
	str	xzr,[x0,TH_STATE]		; TS_READY
	ldr	x0,[x0,TH_CPU]
	b	cpu_kick
.out:	ret

; (sous svc_lock) x0 = requete : reveille son appelant. Son thread peut etre
; mort et rendu depuis un moment (proc_kill, schedule) alors que SR_THREAD le
; nomme encore jusqu'a svc_proc_exit : il n'est sur que s'il est encore dans la
; liste de son process (proc_thread_gone l'en retire avant de le rendre)
svc_reveil_appelant:
	prologue
	stp	x19,x20,[sp,-16]!
	ldp	x19,x20,[x0,SR_PID]
	movl	x0,SVC_PID_NOYAU
	cmp	x19,x0
	b.ne	.proc
	mov	x0,x20
	bl	svc_reveil
	b	.out
.proc:	mov	x0,x19
	bl	ws_proc_of
	cbz	x0,.out
	mov	x19,x0
	add	x0,x19,P_LOCK
	bl	spin_lock
	ldr	x2,[x19,P_THREADS]
.t:	cbz	x2,.unl
	cmp	x2,x20
	b.eq	.vivant
	ldr	x2,[x2,TH_PNEXT]
	b	.t
.vivant:
	mov	x0,x2
	bl	svc_reveil
.unl:	add	x0,x19,P_LOCK
	bl	spin_unlock
.out:	ldp	x19,x20,[sp],16
	epilogue

; (sous svc_lock) x0 = entree d'un service : reveille l'un des threads de
; son process qui attendent une requete (TH_WAIT = l'entree, svc_next)
svc_reveil_service:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,SV_PID]
	bl	ws_proc_of
	cbz	x0,.out
	str	x20,[sp,-16]!
	mov	x20,x0
	add	x0,x20,P_LOCK			; (la liste de ses threads ne bouge pas)
	bl	spin_lock
	ldr	x2,[x20,P_THREADS]
.t:	cbz	x2,.fin
	ldr	x1,[x2,TH_STATE]
	cmp	x1,TS_SLEEPING
	b.ne	.n
	ldr	x1,[x2,TH_WAIT]
	cmp	x1,x19
	b.ne	.n
	str	xzr,[x2,TH_WAIT]
	mov	x0,x2
	bl	svc_reveil
	b	.fin
.n:	ldr	x2,[x2,TH_PNEXT]
	b	.t
.fin:	add	x0,x20,P_LOCK
	bl	spin_unlock
	ldr	x20,[sp],16
.out:	ldr	x19,[sp],16
	epilogue

; x0 = numero de service -> x0 = son entree
svc_entree:
	adr	x1,svc_table
	mov	x2,SV_SIZE
	madd	x0,x0,x2,x1
	ret

; x0 = nom (noyau) -> x0 = numero du service vivant de ce nom (-1 : aucun)
; (sous svc_lock)
svc_cherche:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.s:	mov	x0,x20
	bl	svc_entree
	ldr	x1,[x0,SV_PID]
	cbz	x1,.n
	mov	x1,x19
	bl	str_equal
	cbnz	x0,.got
.n:	add	x20,x20,1
	cmp	x20,SVC_MAX
	b.lo	.s
	mov	x0,-1
	b	.out
.got:	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; (sous svc_lock) x0 = places a laisser libres (SR_RESERVE : un appel ; 0 : un
; avis du noyau) -> x0 = requete libre (0 : aucune au-dela de la reserve),
; numerotee dans l'ordre (touche x0-x5)
svc_requete:
	mov	x5,x0
	adr	x0,svc_requetes
	mov	x1,SR_MAX
	mov	x3,0				; places libres
	mov	x4,0				; la premiere
.s:	ldr	x2,[x0,SR_ETAT]
	cbnz	x2,.n
	add	x3,x3,1
	cbnz	x4,.n
	mov	x4,x0
.n:	add	x0,x0,SR_SIZE
	subs	x1,x1,1
	b.ne	.s
	mov	x0,0
	cmp	x3,x5
	b.hi	.got
	ret
.got:	mov	x0,x4
	adr	x1,svc_ordre
	ldr	x2,[x1]
	add	x2,x2,1
	str	x2,[x1]
	str	x2,[x0,SR_ORDRE]
	ret

; --- svc_register(nom, drapeaux) -> numero du service ---------------------
; (programmes de SD0:/sys seulement ; drapeau 1 : prevenu des fins de process)
sys_svc_register:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,SVC_NOM
	mov	x19,x1
	mov	x1,sp
	mov	x2,SVC_NOM
	bl	copy_str_from_user
	tbnz	x0,63,.out
	mov	x0,E_INVAL
	ldrb	w1,[sp]
	cbz	w1,.out
	mov	x0,sp
	mov	x1,x19
	bl	svc_register_k
.out:	add	sp,sp,SVC_NOM
	ldr	x19,[sp],16
	epilogue

; x0 = nom (noyau), x1 = drapeaux : le process courant tient ce service
; (s'il a les droits) -> x0 = numero du service, < 0 si erreur
svc_register_k:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x21,x0
	mov	x20,x1
	current_proc
	ldr	x0,[x1,P_DROITS]
	cbz	x0,.perm
	ldr	x19,[x1,P_PID]
	svc_prendre
	mov	x0,x21
	bl	svc_cherche
	tbnz	x0,63,.libre
	mov	x27,x0				; deja tenu : par lui ?
	bl	svc_entree
	ldr	x1,[x0,SV_PID]
	cmp	x1,x19
	b.ne	.busy
	str	x20,[x0,SV_FLAGS]
	mov	x0,x27
	b	.unlock
.libre:	mov	x27,0				; une place libre
.l:	mov	x0,x27
	bl	svc_entree
	ldr	x1,[x0,SV_PID]
	cbz	x1,.take
	add	x27,x27,1
	cmp	x27,SVC_MAX
	b.lo	.l
	mov	x0,E_NOMEM
	b	.unlock
.take:	stp	x19,xzr,[x0,SV_PID]		; pid, aucune attente
	str	x20,[x0,SV_FLAGS]
	mov	x1,x21
	mov	x2,SVC_NOM-1
.cp:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	cbz	w3,.cpf
	subs	x2,x2,1
	b.ne	.cp
	strb	wzr,[x0]
.cpf:	mov	x0,x27
	b	.unlock
.busy:	mov	x0,E_BUSY
.unlock:
	mov	x19,x0
	svc_rendre
	mov	x0,x19
	b	.out
.perm:	mov	x0,E_PERM
.out:	ldp	x27,x28,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- svc_open(nom, attente ms) -> numero du service (canal) ---------------
; (attend qu'il s'enregistre, au plus ce delai ; E_NOENT sinon)
sys_svc_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,SVC_NOM
	mov	x20,x1
	mov	x1,sp
	mov	x2,SVC_NOM
	bl	copy_str_from_user
	tbnz	x0,63,.out
	bl	time_ms
	add	x20,x0,x20			; fin de l'attente
.try:	svc_prendre
	mov	x0,sp
	bl	svc_cherche
	mov	x19,x0
	svc_rendre
	tbz	x19,63,.got
	bl	time_ms
	cmp	x0,x20
	b.hs	.noent
	mov	x0,10				; (un enregistrement est rare)
	bl	thread_sleep
	b	.try
.got:	mov	x0,x19
	b	.out
.noent:	mov	x0,E_NOENT
.out:	add	sp,sp,SVC_NOM
	ldp	x27,x28,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- svc_call(canal, message) -> resultat (le message recoit la reponse) --
sys_svc_call:
	prologue
	check_user x1,WS_MSG,.fault
	bl	svc_call_k
	epilogue
.fault:	mov	x0,E_FAULT
	epilogue

; x0 = canal, x1 = message (memoire du noyau ou du process courant) : appel
; d'un service, au nom du process courant (svc_call ; un thread du noyau :
; SVC_PID_NOYAU) -> resultat, reponse dans le message. La memoire du programme
; ne se touche jamais sous svc_lock : une faute y tuerait le thread, verrou
; tenu, et tous les services avec lui (la pile du noyau sert de relais).
svc_call_k:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,WS_MSG
	mov	x19,x0
	mov	x20,x1
	cmp	x19,SVC_MAX
	b.hs	.inval
	current_proc
	mov	x25,0				; places a laisser (0 : un service, un
	movl	x21,SVC_PID_NOYAU		; pilote, le noyau : ni reserve ni attente)
	cbz	x1,.qui				; (thread du noyau, sans process : il
	ldr	x21,[x1,P_PID]			; attend sa reponse comme un autre)
	ldr	x0,[x1,P_DROITS]
	cbnz	x0,.qui
	mov	x25,SR_RESERVE			; un programme
.qui:	this_cpu x0
	ldr	x24,[x0,PC_CURRENT]
	mov	x0,x19
	bl	svc_entree
	mov	x23,x0
	mov	x0,SVC_CALL_MS
	bl	svc_echeance
	mov	x27,x0
	mov	x0,sp
	mov	x1,x20
	mov	x2,WS_MSG
	bl	mem_copy
	svc_prendre
.place:	mov	x0,E_NOENT
	ldr	x1,[x23,SV_PID]
	cbz	x1,.unlock
	mov	x0,x25
	bl	svc_requete
	mov	x22,x0
	cbnz	x22,.prise
	mov	x0,E_BUSY			; table pleine (avis d'une rafale de
	cbz	x25,.unlock			; fork) : un programme attend une
	mrs	x1,cntpct_el0			; place jusqu'a l'echeance de l'appel
	cmp	x1,x27
	b.hs	.unlock
	mov	x0,SVC_ATTENTE_MS
	bl	svc_echeance
	cmp	x0,x27
	csel	x0,x27,x0,hi
	bl	svc_dormir
	b	.place
.prise:	str	x19,[x22,SR_SVC]
	stp	x21,x24,[x22,SR_PID]		; appelant, son thread
	add	x0,x22,SR_MSG
	mov	x1,sp
	mov	x2,WS_MSG
	bl	mem_copy
	str	w21,[x22,SR_MSG+44]
	mov	x0,1				; en file : le service est reveille
	str	x0,[x22,SR_ETAT]
	mov	x0,x23
	bl	svc_reveil_service
.wait:	ldr	x0,[x22,SR_ETAT]
	cmp	x0,3
	b.eq	.done
	mrs	x0,cntpct_el0
	cmp	x0,x27
	b.hs	.timeout
	mov	x0,x27
	bl	svc_dormir
	b	.wait
.done:	mov	x0,sp				; reponse -> relais -> programme
	add	x1,x22,SR_MSG
	mov	x2,WS_MSG
	bl	mem_copy
	ldr	x19,[x22,SR_MSG]
	str	xzr,[x22,SR_ETAT]		; place rendue
	svc_rendre
	mov	x0,x20
	mov	x1,sp
	mov	x2,WS_MSG
	bl	mem_copy
	mov	x0,x19
	b	.out
.timeout:					; abandon : la reponse ira nulle part
	stp	xzr,xzr,[x22,SR_PID]
	ldr	x0,[x22,SR_ETAT]
	cmp	x0,1
	b.ne	.orphan
	str	xzr,[x22,SR_ETAT]		; encore en file : retiree
.orphan:
	mov	x0,E_TIMEDOUT
.unlock:
	mov	x19,x0
	svc_rendre
	mov	x0,x19
	b	.out
.inval:	mov	x0,E_INVAL
.out:	add	sp,sp,WS_MSG
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = canal, x1 = message (noyau) : avis sans reponse (pid 0 : le service le
; traite sans repondre) -> 0 ou erreur
svc_poster_k:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x19,SVC_MAX
	b.hs	.inval
	mov	x0,x19
	bl	svc_entree
	mov	x21,x0
	svc_prendre
	mov	x22,E_NOENT
	ldr	x0,[x21,SV_PID]
	cbz	x0,.unlock
	mov	x0,0				; (sans attendre : l'appelant ne dort pas)
	bl	svc_requete
	mov	x22,E_BUSY
	cbz	x0,.unlock
	mov	x22,x0
	str	x19,[x22,SR_SVC]
	stp	xzr,xzr,[x22,SR_PID]
	add	x0,x22,SR_MSG
	mov	x1,x20
	mov	x2,WS_MSG
	bl	mem_copy
	str	wzr,[x22,SR_MSG+44]
	mov	x0,1
	str	x0,[x22,SR_ETAT]
	mov	x0,x21
	bl	svc_reveil_service
	mov	x22,0
.unlock:
	svc_rendre
	mov	x0,x22
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom (noyau) -> x0 = numero du service vivant (-1 : aucun)
svc_par_nom:
	prologue
	stp	x27,x28,[sp,-16]!
	str	x19,[sp,-16]!
	mov	x19,x0
	svc_prendre
	mov	x0,x19
	bl	svc_cherche
	mov	x19,x0
	svc_rendre
	mov	x0,x19
	ldr	x19,[sp],16
	ldp	x27,x28,[sp],16
	epilogue

; --- svc_next(canal, message, attente ms) -> numero de requete (0 : rien) -
; (le service seulement ; le message recoit la requete, pid en +44 ; une
; requete du noyau - pid 0, SVC_OP_FIN - n'attend pas de reponse)
sys_svc_next:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,WS_MSG			; (relais : svc_call_k)
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	check_user x20,WS_MSG,.fault
	cmp	x19,SVC_MAX
	b.hs	.inval
	mov	x0,x19
	bl	svc_entree
	mov	x23,x0
	current_proc
	ldr	x24,[x1,P_PID]
	mov	x0,x21
	bl	svc_echeance
	mov	x27,x0
	svc_prendre
	ldr	x0,[x23,SV_PID]
	cmp	x0,x24
	mov	x0,E_PERM
	b.ne	.unlock
.try:	ldr	x0,[x23,SV_SIGNALS]		; des signaux d'abord
	cbz	x0,.file
	add	x1,x23,SV_SIGNALS
.take:	ldaxr	x0,[x1]
	stlxr	w2,xzr,[x1]
	cbnz	w2,.take
	mov	x1,SVC_OP_SIGNAL
	stp	x1,x0,[x20]			; op, a = bits
	stp	xzr,xzr,[x20,16]
	stp	xzr,xzr,[x20,32]		; d, longueur, pid
	mov	x0,SR_MAX+1			; (numero sans requete : pas de reponse)
	b	.unlock
.file:	adr	x21,svc_requetes		; la plus ancienne en file
	mov	x22,0				; (trouvee)
	mov	x24,0
.s:	ldr	x0,[x21,SR_ETAT]
	cmp	x0,1
	b.ne	.sn
	ldr	x0,[x21,SR_SVC]
	cmp	x0,x19
	b.ne	.sn
	cbz	x22,.pick
	ldr	x0,[x21,SR_ORDRE]
	ldr	x1,[x22,SR_ORDRE]
	cmp	x0,x1
	b.hs	.sn
.pick:	mov	x22,x21
.sn:	add	x21,x21,SR_SIZE
	add	x24,x24,1
	cmp	x24,SR_MAX
	b.lo	.s
	cbnz	x22,.got
	mrs	x0,cntpct_el0			; rien : attendre
	cmp	x0,x27
	b.hs	.none
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	str	x23,[x0,TH_WAIT]		; (plusieurs threads peuvent attendre)
	mov	x0,x27
	bl	svc_dormir
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	str	xzr,[x0,TH_WAIT]
	ldr	x0,[x23,SV_PID]			; (service perdu pendant l'attente ?)
	cbz	x0,.none
	b	.try
.got:	mov	x0,2				; prise
	str	x0,[x22,SR_ETAT]
	mov	x0,sp
	add	x1,x22,SR_MSG
	mov	x2,WS_MSG
	bl	mem_copy
	ldr	x0,[x22,SR_PID]			; du noyau : sans reponse
	cbnz	x0,.id
	str	xzr,[x22,SR_ETAT]
.id:	adr	x0,svc_requetes
	sub	x0,x22,x0
	mov	x1,SR_SIZE
	udiv	x0,x0,x1
	add	x19,x0,1
	svc_rendre
	mov	x0,x20
	mov	x1,sp
	mov	x2,WS_MSG
	bl	mem_copy
	mov	x0,x19
	b	.out
.none:	mov	x0,0
.unlock:
	mov	x19,x0
	svc_rendre
	mov	x0,x19
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,WS_MSG
	ldp	x27,x28,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si le process courant tient un service SVF_POST (ws_post permis)
; (lecture seule de la table, sans svc_lock : un service ne change pas de
; process en cours de route)
svc_may_post:
	prologue
	stp	x19,x20,[sp,-16]!
	current_proc
	mov	x0,0
	cbz	x1,.out
	ldr	x19,[x1,P_PID]
	mov	x20,0
.s:	mov	x0,x20
	bl	svc_entree
	ldr	x1,[x0,SV_PID]
	cmp	x1,x19
	b.ne	.n
	ldr	x1,[x0,SV_FLAGS]
	tbnz	x1,3,.yes			; SVF_POST
.n:	add	x20,x20,1
	cmp	x20,SVC_MAX
	b.lo	.s
	mov	x0,0
	b	.out
.yes:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; Leve SVC_SIG_LOWMEM chez les services SVF_MEMORY -> x0 = 1 si rien n'est a
; attendre : aucun autre qu'eux, ou le process courant en est un (il ne peut pas
; a la fois attendre et rendre)
svc_low_memory:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	current_proc
	mov	x21,0
	cbz	x1,.moi
	ldr	x21,[x1,P_PID]
.moi:	mov	x22,1
	mov	x27,0				; 1 : le process courant en est un
	mov	x19,0
	svc_prendre
.s:	mov	x0,x19
	bl	svc_entree
	mov	x20,x0
	ldr	x0,[x20,SV_PID]
	cbz	x0,.n
	ldr	x1,[x20,SV_FLAGS]
	tbz	x1,2,.n				; SVF_MEMORY
	cmp	x0,x21
	b.ne	.sig
	mov	x27,1
	b	.n
.sig:	mov	x22,0
	add	x1,x20,SV_SIGNALS
.or:	ldaxr	x0,[x1]
	orr	x2,x0,SVC_SIG_LOWMEM
	stlxr	w3,x2,[x1]
	cbnz	w3,.or
	cbnz	x0,.n				; deja leve : il est prevenu
	mov	x0,x20
	bl	svc_reveil_service
.n:	add	x19,x19,1
	cmp	x19,SVC_MAX
	b.lo	.s
	svc_rendre
	orr	x0,x22,x27
	ldp	x27,x28,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- svc_signal(canal, bits) : leve des bits du service, sans bloquer -------
sys_svc_signal:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x1
	cmp	x0,SVC_MAX
	b.hs	.inval
	bl	svc_entree
	mov	x20,x0
	ldr	x0,[x20,SV_PID]
	cbz	x0,.noent
	cbz	x19,.ok
	add	x1,x20,SV_SIGNALS
.or:	ldaxr	x0,[x1]
	orr	x2,x0,x19
	stlxr	w3,x2,[x1]
	cbnz	w3,.or
	cbnz	x0,.ok				; deja leves : le service est prevenu
	svc_prendre				; (SVC_NEXT regarde le mot sous ce verrou
	mov	x0,x20				;  avant de dormir : pas de reveil perdu)
	bl	svc_reveil_service
	svc_rendre
.ok:	mov	x0,0
	b	.out
.noent:	mov	x0,E_NOENT
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- svc_reply(numero, message) : reponse a une requete prise ------------
sys_svc_reply:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,WS_MSG			; (relais : svc_call_k)
	mov	x19,x0
	mov	x20,x1
	check_user x20,WS_MSG,.fault
	current_proc				; ses mappages (mapcaller.asm) finissent ici,
	mov	x0,x19				; avant que l'appelant reparte
	bl	mc_release
	sub	x19,x19,1
	cmp	x19,SR_MAX
	b.hs	.inval
	adr	x0,svc_requetes
	mov	x1,SR_SIZE
	madd	x19,x19,x1,x0
	current_proc
	ldr	x21,[x1,P_PID]
	mov	x0,sp
	mov	x1,x20
	mov	x2,WS_MSG
	bl	mem_copy
	svc_prendre
	mov	x22,E_INVAL
	ldr	x0,[x19,SR_ETAT]
	cmp	x0,2
	b.ne	.unlock
	ldr	x0,[x19,SR_SVC]			; par le process du service
	bl	svc_entree
	ldr	x0,[x0,SV_PID]
	mov	x22,E_PERM
	cmp	x0,x21
	b.ne	.unlock
	mov	x22,0
	ldr	x0,[x19,SR_PID]
	cbz	x0,.drop			; appelant disparu
	add	x0,x19,SR_MSG
	mov	x1,sp
	mov	x2,WS_MSG
	bl	mem_copy
	dmb	ish
	mov	x0,3				; repondue : l'appelant est reveille
	str	x0,[x19,SR_ETAT]
	mov	x0,x19
	bl	svc_reveil_appelant
	b	.unlock
.drop:	str	xzr,[x19,SR_ETAT]
.unlock:
	svc_rendre
	mov	x0,x22
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,WS_MSG
	ldp	x27,x28,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- svc_forward(numero, canal, message) : une requete prise passe a un autre
; service, avec ce message ; il repondra lui-meme a l'appelant (vfs -> fatfs).
; Le message garde le pid de l'appelant (+44) ; la requete reprend sa place
; en file (nouvel ordre d'arrivee), l'echeance de l'appelant ne change pas.
; Service absent : E_NOENT, la requete reste prise (au premier de repondre).
; Appelant disparu : la requete est rendue, sans rien transmettre.
sys_svc_forward:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,WS_MSG			; (relais : svc_call_k)
	mov	x19,x0
	mov	x23,x1
	mov	x20,x2
	check_user x20,WS_MSG,.fault
	cmp	x23,SVC_MAX
	b.hs	.inval
	current_proc				; ses mappages finissent ici (SVC_REPLY)
	mov	x0,x19
	bl	mc_release
	sub	x19,x19,1
	cmp	x19,SR_MAX
	b.hs	.inval
	adr	x0,svc_requetes
	mov	x1,SR_SIZE
	madd	x19,x19,x1,x0
	current_proc
	ldr	x21,[x1,P_PID]
	mov	x0,x23
	bl	svc_entree
	mov	x24,x0
	mov	x0,sp
	mov	x1,x20
	mov	x2,WS_MSG
	bl	mem_copy
	svc_prendre
	mov	x22,E_INVAL
	ldr	x0,[x19,SR_ETAT]
	cmp	x0,2
	b.ne	.unlock
	ldr	x0,[x19,SR_SVC]			; par le process du service
	bl	svc_entree
	ldr	x0,[x0,SV_PID]
	mov	x22,E_PERM
	cmp	x0,x21
	b.ne	.unlock
	mov	x22,0
	ldr	x0,[x19,SR_PID]
	cbz	x0,.drop			; appelant disparu
	mov	x22,E_NOENT
	ldr	x1,[x24,SV_PID]
	cbz	x1,.unlock
	mov	x22,0
	add	x0,x19,SR_MSG
	mov	x1,sp
	mov	x2,WS_MSG
	bl	mem_copy
	ldr	x0,[x19,SR_PID]
	str	w0,[x19,SR_MSG+44]
	str	x23,[x19,SR_SVC]
	adr	x1,svc_ordre
	ldr	x2,[x1]
	add	x2,x2,1
	str	x2,[x1]
	str	x2,[x19,SR_ORDRE]
	dmb	ish
	mov	x0,1				; en file chez l'autre service
	str	x0,[x19,SR_ETAT]
	mov	x0,x24
	bl	svc_reveil_service
	b	.unlock
.drop:	str	xzr,[x19,SR_ETAT]
.unlock:
	svc_rendre
	mov	x0,x22
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,WS_MSG
	ldp	x27,x28,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB du process termine (faucheur, comme ws_proc_exit)
svc_proc_exit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	ldr	x1,[x0,P_EXIT_CODE]		; (son code de sortie, pour l'avis)
	str	x1,[sp,-16]!
	ldr	x19,[x0,P_PID]
	svc_prendre
	adr	x20,svc_requetes		; ses requetes : sans destinataire
	mov	x21,SR_MAX
.q:	ldr	x0,[x20,SR_PID]
	cmp	x0,x19
	b.ne	.qn
	stp	xzr,xzr,[x20,SR_PID]
	ldr	x0,[x20,SR_ETAT]
	cmp	x0,2
	b.eq	.qn				; prise : liberee a la reponse
	str	xzr,[x20,SR_ETAT]
.qn:	add	x20,x20,SR_SIZE
	subs	x21,x21,1
	b.ne	.q
	mov	x20,0				; ses services : fermes
.sv:	mov	x0,x20
	bl	svc_entree
	ldr	x1,[x0,SV_PID]
	cmp	x1,x19
	b.ne	.svn
	stp	xzr,xzr,[x0,SV_PID]
	adr	x21,svc_requetes		; leurs requetes echouent
	mov	x22,SR_MAX
.r:	ldr	x0,[x21,SR_ETAT]
	cmp	x0,1
	b.lo	.rn
	cmp	x0,2
	b.hi	.rn
	ldr	x0,[x21,SR_SVC]
	cmp	x0,x20
	b.ne	.rn
	ldr	x0,[x21,SR_PID]
	cbz	x0,.rfree
	mov	x0,E_PIPE
	str	x0,[x21,SR_MSG]
	mov	x0,3
	str	x0,[x21,SR_ETAT]
	mov	x0,x21
	bl	svc_reveil_appelant
	b	.rn
.rfree:	str	xzr,[x21,SR_ETAT]
.rn:	add	x21,x21,SR_SIZE
	subs	x22,x22,1
	b.ne	.r
.svn:	add	x20,x20,1
	cmp	x20,SVC_MAX
	b.lo	.sv
	mov	x0,SVC_AVIS_MS			; (table pleine : attendre une place)
	bl	svc_echeance
	mov	x22,x0
	mov	x20,0				; les services qui veulent le savoir
.f:	mov	x0,x20
	bl	svc_entree
	mov	x21,x0
	ldr	x0,[x21,SV_PID]
	cbz	x0,.fn
	ldr	x0,[x21,SV_FLAGS]
	tbz	x0,0,.fn			; SVF_FINS
	mov	x0,SR_RESERVE_AVIS
	bl	svc_requete
	cbnz	x0,.fplace
	mov	x0,x22
	bl	svc_avis_attendre
	cbnz	x0,.f				; (le meme service, s'il est encore la)
	b	.fn				; (echeance passee : avis perdu)
.fplace:
	str	x20,[x0,SR_SVC]
	stp	xzr,xzr,[x0,SR_PID]
	mov	x1,SVC_OP_FIN
	stp	x1,x19,[x0,SR_MSG]		; op, a = pid, b = code de sortie
	ldr	x1,[sp]
	str	x1,[x0,SR_MSG+16]
	str	wzr,[x0,SR_MSG+40]
	str	wzr,[x0,SR_MSG+44]
	mov	x1,1
	str	x1,[x0,SR_ETAT]
	mov	x0,x21
	bl	svc_reveil_service
.fn:	add	x20,x20,1
	cmp	x20,SVC_MAX
	b.lo	.f
	svc_rendre
	add	sp,sp,16
	ldp	x27,x28,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid du fils, x1 = pid du pere (0 : le noyau), x2 = entrees / sorties
; du fils (3 x 8 o, 0 : toutes heritees) : avis de naissance aux services
; SVF_NAISSANCES (proc_spawn_elf, avant le premier thread ; la file etant
; dans l'ordre d'arrivee, l'avis precede toute requete du fils). Message :
; a = fils, b = pere, c = entree, d = sortie, donnees (8 o) = erreur.
; -> x0 = avis perdus (file pleine)
svc_naissance:
	mov	x3,0
	mov	x4,0
	mov	x5,0
	cbz	x2,.sio
	ldp	x3,x4,[x2]
	ldr	x5,[x2,16]
.sio:	mov	x2,x1
	mov	x1,x0
	movl	x0,SVC_OP_NAISSANCE
	mov	x6,8
	b	svc_avis_naissances

; x0 = pid du fils, x1 = pid du pere : avis de fork aux services
; SVF_NAISSANCES (fork.asm, avant que le thread du fils soit executable).
; Message : a = fils, b = pere, sans donnees. -> x0 = avis perdus
svc_fork_avis:
	mov	x2,x1
	mov	x1,x0
	movl	x0,SVC_OP_FORK
	mov	x3,0
	mov	x4,0
	mov	x5,0
	mov	x6,0
					; (suite : svc_avis_naissances)

; x0 = op, x1 = a, x2 = b, x3 = c, x4 = d, x5 = 8 o de donnees, x6 = leur
; longueur (0 ou 8) : un avis du noyau (SR_PID = 0, sans reponse) a chaque
; service SVF_NAISSANCES -> x0 = avis perdus (plus de place dans la file)
svc_avis_naissances:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64
	stp	x0,x1,[sp]
	stp	x2,x3,[sp,16]
	stp	x4,x5,[sp,32]
	str	x6,[sp,48]
	mov	x22,0				; perdus
	mov	x0,SVC_AVIS_MS			; (table pleine : attendre une place)
	bl	svc_echeance
	mov	x19,x0
	svc_prendre
	mov	x20,0
.f:	mov	x0,x20
	bl	svc_entree
	mov	x21,x0
	ldr	x0,[x21,SV_PID]
	cbz	x0,.fn
	ldr	x0,[x21,SV_FLAGS]
	tbz	x0,1,.fn			; SVF_NAISSANCES
	mov	x0,SR_RESERVE_AVIS
	bl	svc_requete
	cbnz	x0,.place
	mov	x0,x19
	bl	svc_avis_attendre
	cbnz	x0,.f				; (le meme service, s'il est encore la)
	add	x22,x22,1			; (echeance passee : avis perdu)
	b	.fn
.place:	str	x20,[x0,SR_SVC]
	stp	xzr,xzr,[x0,SR_PID]
	ldp	x1,x2,[sp]
	stp	x1,x2,[x0,SR_MSG]		; op, a
	ldp	x1,x2,[sp,16]
	stp	x1,x2,[x0,SR_MSG+16]		; b, c
	ldp	x1,x2,[sp,32]
	str	x1,[x0,SR_MSG+32]		; d
	str	x2,[x0,SR_MSG+48]		; donnees
	ldr	x1,[sp,48]
	str	w1,[x0,SR_MSG+40]
	str	wzr,[x0,SR_MSG+44]
	mov	x1,1
	str	x1,[x0,SR_ETAT]
	mov	x0,x21
	bl	svc_reveil_service
.fn:	add	x20,x20,1
	cmp	x20,SVC_MAX
	b.lo	.f
	svc_rendre
	mov	x0,x22
	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sous svc_lock, x28 = DAIF) x0 = echeance : plus de place pour un avis du
; noyau ; dort SVC_ATTENTE_MS (verrou rendu le temps de dormir : les services
; vident la file) -> x0 = 1 : reessayer, 0 : echeance passee (avis perdu)
svc_avis_attendre:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mrs	x0,cntpct_el0
	cmp	x0,x19
	b.hs	.perdu
	mov	x0,SVC_ATTENTE_MS
	bl	svc_echeance
	cmp	x0,x19
	csel	x0,x19,x0,hi
	bl	svc_dormir
	mov	x0,1
	b	.out
.perdu:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

		align 8
svc_lock	dw 0
		align 8
svc_ordre	dd 0
		align 4
