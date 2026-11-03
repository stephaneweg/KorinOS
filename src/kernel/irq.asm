; ============================================================================
;  irq.asm - les interruptions du materiel vers les pilotes (appels 0x49-0x4C,
;  docs/ABI.md ; programmes de SD0:/sys)
;
;  Un pilote prend une ligne du GIC (IRQ_BIND). Quand elle se leve, le noyau la
;  masque (lignes en niveau : le pilote doit d'abord calmer son materiel), puis
;  * ligne liee a un service : leve ses bits dans le mot de signaux du service
;    (SV_SIGNALS, svc.asm) ; le pilote la voit par SVC_NEXT, et la demasque
;    par IRQ_ACK ;
;  * sinon : reveille le thread qui attend dans IRQ_WAIT, qui la demasque a
;    l'appel suivant.
;  Deux pilotes peuvent tenir une ligne s'ils l'ont tous deux demande
;  (IRQ_SHARED : sur le Pi 4, l'EMMC2 de la carte et l'Arasan du Wi-Fi) : elle
;  les reveille tous les deux, chacun regarde son materiel. Sans ce drapeau une
;  ligne tenue rend E_BUSY : un pilote qui ne peut pas effacer l'interruption
;  de l'autre tournerait a vide.
;  Un process qui finit rend ses lignes, masquees. Option « no_irq » de
;  SD0:/etc/options.txt : IRQ_BIND rend E_NOSYS, les pilotes sondent.
;
;  Verrous : irq_lock (irqsave) est une feuille ; irq_fire le relache avant de
;  prendre svc_lock.
; ============================================================================

IRQ_FIRST	= 32			; premiere SPI (0-31 : noyau)
IRQ_MAX		= 256
IRQ_SD		= 158			; EMMC2 : refusee avec no_sd_dma (sdhci.asm)
IE_PID		= 0			; process qui la tient (0 : libre)
IE_SVC		= 8			; entree du service (0 : IRQ_WAIT)
IE_BITS		= 16			; bits a lever
IE_FIRED	= 24			; 1 : levee depuis le dernier IRQ_WAIT
IE_TCB		= 32			; thread dans IRQ_WAIT
IE_SHARED	= 40			; 1 : ligne partageable
IE_SIZE		= 48

macro irq_prendre {			; -> x28 = DAIF (x28 sauve par l'appelant)
	adr	x0,irq_lock
	bl	spin_lock_irqsave
	mov	x28,x0
}
macro irq_rendre {
	adr	x0,irq_lock
	mov	x1,x28
	bl	spin_unlock_irqrestore
}

; x0 = numero de ligne -> x0 = son entree (0 : hors de la plage)
irq_entree:
	sub	x1,x0,IRQ_FIRST
	cmp	x1,IRQ_MAX-IRQ_FIRST
	b.hs	.non
	mov	x2,IE_SIZE
	adr	x1,irq_table
	madd	x0,x0,x2,x1
	ret
.non:	mov	x0,0
	ret

; x0 = entree -> x0 = l'entree du second pilote de cette ligne
irq_second:
	adr	x1,irq_table
	sub	x0,x0,x1
	adr	x1,irq_table2
	add	x0,x0,x1
	ret

; (sous irq_lock) x0 = entree de la ligne, x1 = pid -> x0 = celle de ce pid
; parmi les deux, 0 si aucune
irq_mien:
	ldr	x2,[x0,IE_PID]
	cmp	x2,x1
	b.eq	.oui
	adr	x2,irq_table
	sub	x0,x0,x2
	adr	x2,irq_table2
	add	x0,x0,x2
	ldr	x2,[x0,IE_PID]
	cmp	x2,x1
	b.eq	.oui
	mov	x0,0
.oui:	ret

; (irq_dispatch, IRQ masquees) w0 = numero de ligne -> x0 = 1 si un pilote la
; tient : masquee, pilotes prevenus ; 0 sinon
irq_fire:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	w20,w0
	bl	irq_entree
	cbz	x0,.out
	mov	x19,x0
	mov	w0,w20
	bl	gic_disable_irq
	mov	x0,x19
	bl	irq_fire_un
	mov	x21,x0
	mov	x0,x19
	bl	irq_second
	bl	irq_fire_un
	orr	x0,x0,x21
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree : son pilote est prevenu -> x0 = 1 s'il y en a un
irq_fire_un:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,irq_lock
	bl	spin_lock
	mov	x0,0
	ldr	x1,[x19,IE_PID]
	cbz	x1,.rendre
	mov	x0,1
	str	x0,[x19,IE_FIRED]
	ldr	x2,[x19,IE_TCB]
	cbz	x2,.rendre
	str	xzr,[x19,IE_TCB]
	ldr	x1,[x2,TH_STATE]
	cmp	x1,TS_SLEEPING
	b.ne	.rendre
	str	xzr,[x2,TH_WAIT]
	mov	x1,WAKE_BOOST
	str	x1,[x2,TH_BOOST]
	str	xzr,[x2,TH_STATE]		; TS_READY
	ldr	x0,[x2,TH_CPU]
	bl	cpu_kick
	mov	x0,1
.rendre:
	mov	x20,x0
	adr	x0,irq_lock
	bl	spin_unlock
	mov	x0,x20
	cbz	x0,.out
	ldr	x2,[x19,IE_SVC]
	cbz	x2,.out
	ldr	x3,[x19,IE_BITS]
	add	x1,x2,SV_SIGNALS
.or:	ldaxr	x0,[x1]
	orr	x4,x0,x3
	stlxr	w5,x4,[x1]
	cbnz	w5,.or
	cbnz	x0,.vu				; deja leves : le service est prevenu
	mov	x19,x2
	adr	x0,svc_lock
	bl	spin_lock
	mov	x0,x19
	bl	svc_reveil_service
	adr	x0,svc_lock
	bl	spin_unlock
.vu:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- irq_bind(ligne, canal (-1 : IRQ_WAIT), bits) -> 0 ---------------------
; (le canal : un service du process ; la ligne est demasquee, sauf pour IRQ_WAIT)
sys_irq_bind:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,irq_off
	ldr	x0,[x0]
	cbnz	x0,.nosys
	cmp	x19,IRQ_SD			; (la carte : attente ; le Wi-Fi : service)
	b.ne	.droits
	cmn	x20,1
	b.ne	.droits
	adr	x0,irq_sd_off
	ldr	x0,[x0]
	cbnz	x0,.nosys
.droits:
	current_proc
	ldr	x0,[x1,P_DROITS]
	cbz	x0,.perm
	ldr	x22,[x1,P_PID]
	mov	x0,x19
	bl	irq_entree
	cbz	x0,.inval
	mov	x27,x0
	cmn	x20,1
	b.eq	.mode
	cmp	x20,SVC_MAX
	b.hs	.inval
	mov	x0,x20
	bl	svc_entree
	ldr	x1,[x0,SV_PID]
	cmp	x1,x22
	b.ne	.perm
	mov	x20,x0
	b	.lock
.mode:	mov	x20,0
.lock:	irq_prendre
	lsr	x23,x21,63			; IRQ_SHARED
	and	x21,x21,0x7FFFFFFFFFFFFFFF
	ldr	x0,[x27,IE_PID]
	cbz	x0,.libre
	cmp	x0,x22
	b.eq	.libre
	cbz	x23,.prise			; tenue : a deux, s'ils le veulent tous deux
	ldr	x0,[x27,IE_SHARED]
	cbz	x0,.prise
	mov	x0,x27
	bl	irq_second
	mov	x27,x0
	ldr	x0,[x27,IE_PID]
	cbz	x0,.libre
	cmp	x0,x22
	b.eq	.libre
.prise:	irq_rendre
	mov	x0,E_BUSY
	b	.out
.libre:	stp	x22,x20,[x27,IE_PID]		; pid, service
	stp	x21,xzr,[x27,IE_BITS]		; bits, pas encore levee
	str	xzr,[x27,IE_TCB]
	str	x23,[x27,IE_SHARED]
	cbz	x20,.unl
	mov	w0,w19
	bl	gic_enable_irq
.unl:	irq_rendre
	mov	x0,0
	b	.out
.nosys:	mov	x0,E_NOSYS
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- irq_ack(ligne) -> 0 : le pilote a calme son materiel, ligne demasquee --
sys_irq_ack:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	bl	irq_entree
	cbz	x0,.inval
	mov	x20,x0
	current_proc
	ldr	x27,[x1,P_PID]
	irq_prendre
	mov	x0,x20
	mov	x1,x27
	bl	irq_mien
	cbz	x0,.pas
	str	xzr,[x0,IE_FIRED]
	mov	w0,w19
	bl	gic_enable_irq
	irq_rendre
	mov	x0,0
	b	.out
.pas:	irq_rendre
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- irq_wait(ligne, attente ms) -> 1 levee (ligne masquee), 0 delai, E_INTR ---
; (ligne demasquee ici : le pilote a calme son materiel avant de rappeler)
sys_irq_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	irq_entree
	cbz	x0,.inval
	mov	x21,x0
	current_proc
	mov	x22,x0
	ldr	x27,[x1,P_PID]
	mov	x0,x20
	bl	svc_echeance
	mov	x20,x0
	irq_prendre
	mov	x0,x21
	mov	x1,x27
	bl	irq_mien
	cbz	x0,.pas
	mov	x21,x0
	mov	w0,w19				; (irq_fire attend irq_lock)
	bl	gic_enable_irq
.try:	ldr	x0,[x21,IE_FIRED]
	cbnz	x0,.levee
	ldr	x0,[x22,TH_KILL]
	cbnz	x0,.intr
	mov	x0,x22
	bl	sig_ready
	cbnz	x0,.intr
	mrs	x0,cntpct_el0
	cmp	x0,x20
	b.hs	.delai
	str	x22,[x21,IE_TCB]
	str	x21,[x22,TH_WAIT]
	str	x20,[x22,TH_WAKE]
	mov	x0,x22
	mov	x1,TS_SLEEPING
	bl	thread_park
	adr	x0,irq_lock
	bl	spin_unlock			; IRQ toujours masquees
	bl	schedule
	adr	x0,irq_lock
	bl	spin_lock
	str	xzr,[x21,IE_TCB]
	str	xzr,[x22,TH_WAIT]
	b	.try
.levee:	str	xzr,[x21,IE_FIRED]
	mov	x19,1
	b	.rendre
.intr:	mov	x19,E_INTR
	b	.rendre
.delai:	mov	x19,0
.rendre:
	irq_rendre
	mov	x0,x19
	b	.out
.pas:	irq_rendre
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- irq_unbind(ligne) -> 0 : ligne masquee et rendue --------------------------
sys_irq_unbind:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	bl	irq_entree
	cbz	x0,.inval
	mov	x20,x0
	current_proc
	ldr	x27,[x1,P_PID]
	irq_prendre
	mov	x0,x20
	mov	x1,x27
	bl	irq_mien
	cbz	x0,.pas
	str	xzr,[x0,IE_PID]
	mov	x0,x20
	bl	irq_ligne_libre
	cbz	x0,.garde
	mov	w0,w19
	bl	gic_disable_irq
.garde:	irq_rendre
	mov	x0,0
	b	.out
.pas:	irq_rendre
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree de la ligne -> x0 = 1 si plus personne ne la tient
irq_ligne_libre:
	ldr	x1,[x0,IE_PID]
	cbnz	x1,.non
	adr	x1,irq_table
	sub	x0,x0,x1
	adr	x1,irq_table2
	add	x0,x0,x1
	ldr	x1,[x0,IE_PID]
	cbnz	x1,.non
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = PCB du process termine (faucheur) : ses lignes, masquees et rendues
irq_proc_exit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	ldr	x19,[x0,P_PID]
	irq_prendre
	mov	x20,IRQ_FIRST
	mov	x21,IE_SIZE
	adr	x22,irq_table
.l:	madd	x27,x20,x21,x22
	mov	x0,x27
	mov	x1,x19
	bl	irq_mien
	cbz	x0,.n
	str	xzr,[x0,IE_PID]
	str	xzr,[x0,IE_TCB]
	mov	x0,x27
	bl	irq_ligne_libre
	cbz	x0,.n
	mov	w0,w20
	bl	gic_disable_irq
.n:	add	x20,x20,1
	cmp	x20,IRQ_MAX
	b.lo	.l
	irq_rendre
	ldp	x27,x28,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; option no_sd_dma
irq_sd_disable:
	adr	x0,irq_sd_off
	mov	x1,1
	str	x1,[x0]
	ret

; option no_irq
irq_disable:
	adr	x0,irq_off
	mov	x1,1
	str	x1,[x0]
	ret

		align 8
irq_lock	dd 0
irq_off		dd 0
irq_sd_off	dd 0
