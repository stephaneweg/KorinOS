; ============================================================================
;  v3d - service du GPU V3D 4.2 (VideoCore VI) du Pi 4 : SD0:/sys/svc/v3d,
;  inscrit sous « v3d » (docs/V3D.md, protocole : inc/v3d.inc)
;
;  Il possede le GPU : mise en route (firmware : domaine et horloge au
;  maximum ; reset ; ponts AXI), puis les jobs des applications l'un apres
;  l'autre (binning puis rendu, fin par scrutation des compteurs BFC / RFC
;  avec delai, demandes de memoire du binner servies par un pool de
;  debordement). Les listes sont faites par GpuKit dans le process de
;  l'application, dans un tampon partage a elle : le service verifie que le
;  tampon est bien a l'appelant (BUF_INFO) et que les zones y tiennent.
;  Un job qui ne finit pas : V3D_E_LOST, et le GPU est remis a zero (ponts AXI
;  fermes, reset, domaine coupe, puis power_on) ; le job suivant repart : tout
;  ce qu'un job demande est dans ses listes. Apres HANG_MAX blocages de suite,
;  sans job reussi entre deux, le GPU reste eteint jusqu'au redemarrage.
;  Recette de mise en route validee sur le PocketTerm (ancien pilote du
;  noyau, 30/10/2026) ; pas de MMU GPU. Fin d'une passe : compteur sonde 2 ms,
;  puis endormi sur l'interruption (FRDONE, FLDONE, OUTOMEM ; ligne V3D_IRQ),
;  ou en cedant le processeur si le noyau n'en donne pas (no_irq).
;
;  Sous QEMU (V3D non emule : un acces est une erreur de bus) ou avec
;  « no_gpu » dans SD0:/etc/options.txt : il sort aussitot (code 0 : init2
;  ne le relance pas) ; GpuKit ne trouve pas le service et dessine sur le CPU.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/v3d.inc'
include '../inc/board.inc'

PERI		= 0xFE000000
V3D_PHYS	= PERI + 0xC00000	; hub (+0), coeur 0 (+0x4000), ponts AXI (+0x11000)
V3D_MAP		= 0x12000
PM_PAGE		= PERI + 0x100000
PM_GRAFX	= 0x10C
PM_PASSWORD	= 0x5A000000
PM_V3DRSTN	= 1 shl 6
ASB_V3D_S_CTRL	= 0x11008		; (depuis V3D_PHYS)
ASB_V3D_M_CTRL	= 0x1100C
ASB_REQ_STOP	= 1

HUB_UIFCFG	= 0x04
HUB_IDENT1	= 0x0C
HUB_INT_MSK_SET	= 0x60
CORE0		= 0x4000
CTL_SLCACTL	= 0x024
CTL_L2TCACTL	= 0x030
CTL_L2TFLSTA	= 0x034
CTL_L2TFLEND	= 0x038
CTL_INT_STS	= 0x050
CTL_INT_CLR	= 0x058
CTL_INT_MSK_SET	= 0x060
CTL_INT_MSK_CLR	= 0x064
CLE_CT0CS	= 0x100
CLE_CT1CS	= 0x104
CLE_CT0CA	= 0x110
CLE_CT1CA	= 0x114
CLE_BFC		= 0x134
CLE_RFC		= 0x138
CLE_CT0QTS	= 0x15C
CLE_CT0QBA	= 0x160
CLE_CT1QBA	= 0x164
CLE_CT0QEA	= 0x168
CLE_CT1QEA	= 0x16C
CLE_CT0QMA	= 0x170
CLE_CT0QMS	= 0x174
ERR_ERRSTAT	= 0xF20
PTB_BPOA	= 0x308
PTB_BPOS	= 0x30C
INT_OUTOMEM	= 4
INTS		= 7			; FRDONE, FLDONE, OUTOMEM

TIMEOUT_MS	= 500
HANG_MAX	= 3
V3D_IRQ		= 106			; ligne du GIC (SPI 74)
V3D_IRQ_MS	= 4			; attente bornee : on relit le compteur
OVF_CHUNK	= 256*1024
OVF_CHUNKS	= 16
OVF_BYTES	= OVF_CHUNK*OVF_CHUNKS

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	mov	x0,0				; un job attend le GPU : priorite haute
	mov	x1,THREAD_PRIO_HIGH
	sys	SYS_THREAD_SET_PRIORITY
	adr	x0,s_no_opt			; no_gpu : rien
	bl	option_word
	cbz	x0,.fw
	adr	x0,m_no_opt
	b	.leave
.fw:	adr	x0,fw_rev			; QEMU : V3D non emule
	sys	SYS_FW_CALL
	tbnz	x0,63,.emul
	adr	x0,fw_rev
	ldr	w0,[x0,20]
	movl	w1,0x50000000
	cmp	w0,w1
	b.hs	.real
.emul:	adr	x0,m_emul
.leave:	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.real:	bl	board_pi5			; Pi 5 : V3D 7.1, pas encore (docs/PI5.md)
	cbz	x0,.pi4
	adr	x0,m_pi5
	b	.leave
.pi4:	movl	x0,V3D_PHYS			; registres
	movl	x1,V3D_MAP
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	adr	x1,v3d
	str	x0,[x1]
	movl	x0,PM_PAGE
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.nomap
	adr	x1,pm
	str	x0,[x1]
	movl	x0,OVF_BYTES			; pool de debordement du binner
	adr	x1,ovf_phys			; (non cache : le CPU n'y touche pas)
	sys	SYS_DMA_ALLOC
	cmp	x0,0
	b.le	.nomem
	bl	power_on
	cbz	x0,.nopower
	adr	x0,v3d
	ldr	x19,[x0]
	ldr	w20,[x19,HUB_IDENT1]
	and	w0,w20,0xF			; version : 4 (V3D 4.2)
	cmp	w0,4
	b.ne	.noident
	ubfx	w0,w20,4,4
	adr	x1,gpu_rev
	str	x0,[x1]
	mov	w0,-1				; hub : tout masque
	str	w0,[x19,HUB_INT_MSK_SET]
	add	x1,x19,CORE0
	mov	w0,0xFFFFFFF8			; coeur : tout sauf FRDONE/FLDONE/OUTOMEM
	str	w0,[x1,CTL_INT_MSK_SET]
	mov	w0,INTS
	str	w0,[x1,CTL_INT_CLR]
	str	w0,[x1,CTL_INT_MSK_CLR]
	adr	x0,gpu_state
	mov	x1,1
	str	x1,[x0]
	adr	x0,m_ok
	bl	puts
	adr	x0,gpu_rev
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_ok2
	bl	puts
	adr	x0,s_name			; le service
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,.noreg
	mov	x19,x0
	mov	x0,V3D_IRQ			; interruption (sinon : on sonde)
	mov	x1,-1
	mov	x2,0
	sys	SYS_IRQ_BIND
	tbnz	x0,63,.loop
	adr	x0,gpu_irq
	mov	x1,1
	str	x1,[x0]
.loop:	mov	x0,x19
	adr	x1,msg
	mov	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,.loop
	cbz	x0,.loop			; rien (ou avis du noyau)
	mov	x20,x0
	adr	x21,msg
	ldr	x0,[x21]
	cmp	x0,V3D_OP_INFO
	b.eq	.info
	cmp	x0,V3D_OP_RUN
	b.eq	.run
	cmp	x0,V3D_OP_SUBMIT
	b.eq	.run
	cmp	x0,V3D_OP_HEALTH
	b.eq	.health
	mov	x0,V3D_E_INVAL
	b	.reply
.health:
	adr	x0,gpu_state
	ldr	x0,[x0]
	str	x0,[x21,V3DH_STATE]
	adr	x0,jobs
	ldr	x0,[x0]
	str	x0,[x21,V3DH_JOBS]
	adr	x0,resets
	ldr	x0,[x0]
	str	x0,[x21,V3DH_RESETS]
	adr	x0,fw_temp
	bl	fw_ask
	str	x0,[x21,V3DH_TEMP]
	adr	x0,fw_v3dclk
	bl	fw_ask
	str	x0,[x21,V3DH_V3D_HZ]
	adr	x0,fw_armclk
	bl	fw_ask
	str	x0,[x21,V3DH_ARM_HZ]
	adr	x0,fw_throttled
	str	wzr,[x0,20]			; (0 : garde les bits collants)
	bl	fw_ask
	adr	x0,fw_throttled
	ldr	w0,[x0,20]
	str	x0,[x21,V3DH_THROTTLED]
	mov	x0,1
	b	.reply
.info:	adr	x0,gpu_state
	ldr	x0,[x0]
	str	x0,[x21,V3DI_STATE]
	mov	x1,42
	str	x1,[x21,V3DI_VERSION]
	adr	x1,gpu_rev
	ldr	x1,[x1]
	str	x1,[x21,V3DI_REV]
	adr	x1,jobs
	ldp	x1,x2,[x1]
	stp	x1,x2,[x21,V3DI_JOBS]
	cmp	x0,1
	b.ne	.noregs
	adr	x1,v3d				; les registres d'identite, pour Mesa
	ldr	x1,[x1]
	ldr	w2,[x1,HUB_UIFCFG]
	ldr	w3,[x1,HUB_IDENT1]
	stp	x2,x3,[x21,V3DI_UIFCFG]
	ldr	w2,[x1,HUB_IDENT1+4]
	ldr	w3,[x1,HUB_IDENT1+8]
	stp	x2,x3,[x21,V3DI_UIFCFG+16]
	add	x1,x1,CORE0
	ldr	w2,[x1]
	ldr	w3,[x1,4]
	stp	x2,x3,[x21,V3DI_UIFCFG+32]
	ldr	w2,[x1,8]
	str	x2,[x21,V3DI_UIFCFG+48]
.noregs:
	cmp	x0,1
	cset	x0,eq
	b	.reply
.run:	mov	x0,x21
	bl	run_job
.reply:	mov	x1,x0				; resultat dans x0 de SVC_CALL :
	mov	x0,x20				; recopie en tete du message
	str	x1,[x21]
	adr	x1,msg
	sys	SYS_SVC_REPLY
	b	.loop

.nomap:	adr	x0,m_nomap
	b	.quit
.nomem:	adr	x0,m_nomem
	b	.quit
.nopower:
	adr	x0,m_power
	b	.quit
.noident:
	adr	x0,m_ident
	b	.quit
.noreg:	adr	x0,m_noreg
.quit:	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = message du firmware (une valeur en +24) -> x0 = cette valeur
fw_ask:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	str	wzr,[x19,4]
	str	wzr,[x19,16]
	sys	SYS_FW_CALL
	ldr	w0,[x19,24]
	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Mise en route : firmware (domaine 10, horloge 5 au maximum), reset, ponts
;  AXI maitre puis esclave -> x0 = 1 si ok
; ---------------------------------------------------------------------------
power_on:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,fw_domain			; SET_DOMAIN_STATE (10, actif)
	sys	SYS_FW_CALL
	adr	x0,fw_clk			; SET_CLOCK_STATE (5, active)
	sys	SYS_FW_CALL
	adr	x0,fw_maxclk			; GET_MAX_CLOCK_RATE (5)
	sys	SYS_FW_CALL
	tbnz	x0,63,.noclk
	adr	x0,fw_maxclk
	ldr	w0,[x0,24]
	cbz	w0,.noclk
	adr	x1,fw_setclk			; SET_CLOCK_RATE (5, maximum, 0)
	str	w0,[x1,24]
	mov	x0,x1
	sys	SYS_FW_CALL
.noclk:	adr	x0,pm				; sortie du reset
	ldr	x19,[x0]
	ldr	w0,[x19,PM_GRAFX]
	and	w0,w0,0xFFFFFF
	movl	w1,PM_PASSWORD or PM_V3DRSTN
	orr	w0,w0,w1
	str	w0,[x19,PM_GRAFX]
	adr	x0,v3d
	ldr	x19,[x0]
	movl	x1,ASB_V3D_M_CTRL
	add	x19,x19,x1
	bl	asb_open
	cbz	x0,.out
	adr	x0,v3d
	ldr	x19,[x0]
	movl	x1,ASB_V3D_S_CTRL
	add	x19,x19,x1
	bl	asb_open
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Apres un job sans fin : ponts AXI fermes, reset, domaine coupe, puis la mise
;  en route de toujours -> x0 = 1 si le GPU repart (0 : a laisser eteint)
; ---------------------------------------------------------------------------
gpu_recover:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,hangs
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	cmp	x1,HANG_MAX
	b.hi	.no
	adr	x0,v3d
	ldr	x19,[x0]
	movl	x1,ASB_V3D_M_CTRL
	add	x19,x19,x1
	bl	asb_close
	adr	x0,v3d
	ldr	x19,[x0]
	movl	x1,ASB_V3D_S_CTRL
	add	x19,x19,x1
	bl	asb_close
	adr	x0,pm				; en reset
	ldr	x19,[x0]
	ldr	w0,[x19,PM_GRAFX]
	and	w0,w0,0xFFFFFF
	mov	w1,PM_V3DRSTN
	bic	w0,w0,w1
	movl	w1,PM_PASSWORD
	orr	w0,w0,w1
	str	w0,[x19,PM_GRAFX]
	adr	x0,fw_domoff			; domaine coupe
	str	wzr,[x0,4]
	str	wzr,[x0,16]
	sys	SYS_FW_CALL
	mov	x0,10
	sys	SYS_SLEEP
	adr	x0,fw_domain			; (le firmware a ecrit dans ces messages)
	bl	fw_fresh
	adr	x0,fw_clk
	bl	fw_fresh
	adr	x0,fw_maxclk
	bl	fw_fresh
	adr	x0,fw_setclk
	bl	fw_fresh
	bl	power_on
	cbz	x0,.no
	adr	x0,v3d
	ldr	x19,[x0]
	ldr	w0,[x19,HUB_IDENT1]
	and	w0,w0,0xF
	cmp	w0,4
	b.ne	.no
	mov	w0,-1				; les masques de la mise en route
	str	w0,[x19,HUB_INT_MSK_SET]
	add	x1,x19,CORE0
	mov	w0,0xFFFFFFF8
	str	w0,[x1,CTL_INT_MSK_SET]
	mov	w0,INTS
	str	w0,[x1,CTL_INT_CLR]
	str	w0,[x1,CTL_INT_MSK_CLR]
	adr	x0,resets
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	adr	x0,m_reset
	bl	puts
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = message du firmware : pret a repartir
fw_fresh:
	str	wzr,[x0,4]
	str	wzr,[x0,16]
	ret

; x19 = registre du pont : ferme (100 ms au plus : un GPU bloque peut ne pas repondre)
asb_close:
	prologue
	ldr	w0,[x19]
	and	w0,w0,0xFFFFFF
	orr	w0,w0,ASB_REQ_STOP
	movl	w1,PM_PASSWORD
	orr	w0,w0,w1
	str	w0,[x19]
	sys	SYS_TIME_MS
	add	x20,x0,100
.ack:	ldr	w0,[x19]
	tbnz	w0,1,.out			; ACK = bit 1
	sys	SYS_TIME_MS
	cmp	x0,x20
	b.lo	.ack
.out:	epilogue

; x19 = registre du pont -> x0 = 1 quand ACK tombe (100 ms au plus)
asb_open:
	prologue
	ldr	w0,[x19]
	and	w0,w0,0xFFFFFF
	and	w0,w0,not ASB_REQ_STOP
	movl	w1,PM_PASSWORD
	orr	w0,w0,w1
	str	w0,[x19]
	sys	SYS_TIME_MS
	add	x20,x0,100
.ack:	ldr	w0,[x19]
	tbz	w0,1,.acked			; ACK = bit 1
	sys	SYS_TIME_MS
	cmp	x0,x20
	b.lo	.ack
	mov	x0,0
	b	.out
.acked:	mov	x0,1
.out:	epilogue

; ---------------------------------------------------------------------------
;  Un job : x0 = message (V3DR_*) -> x0 = 0 / V3D_E_*
; ---------------------------------------------------------------------------
run_job:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	adr	x0,gpu_state
	ldr	x0,[x0]
	cmp	x0,1
	b.eq	.on
	mov	x0,V3D_E_LOST
	b	.out
.on:	ldr	x0,[x21]
	cmp	x0,V3D_OP_SUBMIT
	b.ne	.own
	mov	x23,0				; Mesa : adresses physiques, sous 1 Go
	movl	x24,0x40000000
	ldr	x0,[x21,V3DR_BCL]
	cmp	x0,x24
	b.hs	.inval
	b	.rcl
.own:	ldr	x0,[x21,V3DR_BUF]		; le tampon de travail
	adr	x1,binfo
	sys	SYS_BUF_INFO
	tbnz	x0,63,.inval
	adr	x22,binfo
	ldr	x0,[x22,BI_OWNER]		; a l'appelant (pid en +44 du message)
	ldr	w1,[x21,44]
	cmp	x0,x1
	b.ne	.inval
	ldr	x23,[x22,BI_PHYS]
	ldr	x24,[x22,BI_BYTES]
	add	x0,x21,V3DR_BCL			; chaque zone dans le tampon
	bl	zone_ok
	cbz	x0,.inval
.rcl:	add	x0,x21,V3DR_RCL
	bl	zone_ok
	cbz	x0,.inval
	ldr	x0,[x21,V3DR_RCL_LEN]
	cbz	x0,.inval
	add	x0,x21,V3DR_TALLOC
	bl	zone_ok
	cbz	x0,.inval
	add	x0,x21,V3DR_TSTATE
	bl	zone_ok
	cbz	x0,.inval
	ldr	x0,[x21,V3DR_TALLOC]
	tst	x0,0xFFF
	b.ne	.inval
	ldr	x0,[x21,V3DR_TSTATE]
	tst	x0,0xFF
	b.ne	.inval

	mrs	x0,cntpct_el0
	adr	x1,t0
	str	x0,[x1]
	adr	x0,v3d
	ldr	x19,[x0]
	add	x19,x19,CORE0
	ldr	x0,[x21,V3DR_BCL_LEN]
	cbz	x0,.render			; pas de binning (aucune primitive)
	; ---- binning
	bl	inval_caches
	adr	x0,chunk
	str	xzr,[x0]
	mov	w0,INTS
	str	w0,[x19,CTL_INT_CLR]
	str	w0,[x19,CTL_INT_MSK_CLR]
	ldr	w20,[x19,CLE_BFC]
	and	w20,w20,0xFF
	ldr	x0,[x21,V3DR_TALLOC]
	add	x0,x0,x23
	str	w0,[x19,CLE_CT0QMA]
	ldr	x0,[x21,V3DR_TALLOC_LEN]
	str	w0,[x19,CLE_CT0QMS]
	ldr	x0,[x21,V3DR_TSTATE]
	add	x0,x0,x23
	orr	w0,w0,2
	str	w0,[x19,CLE_CT0QTS]
	ldr	x0,[x21,V3DR_BCL]
	add	x0,x0,x23
	str	w0,[x19,CLE_CT0QBA]
	ldr	x1,[x21,V3DR_BCL_LEN]
	add	x0,x0,x1
	dsb	sy
	str	w0,[x19,CLE_CT0QEA]		; demarre le binner
	mov	x0,CLE_BFC
	mov	x1,x20
	bl	wait_counter
	cbnz	x0,.fail
.render:
	bl	inval_caches
	ldr	w20,[x19,CLE_RFC]
	and	w20,w20,0xFF
	ldr	x0,[x21,V3DR_RCL]
	add	x0,x0,x23
	str	w0,[x19,CLE_CT1QBA]
	ldr	x1,[x21,V3DR_RCL_LEN]
	add	x0,x0,x1
	dsb	sy
	str	w0,[x19,CLE_CT1QEA]		; demarre le rendu
	mov	x0,CLE_RFC
	mov	x1,x20
	bl	wait_counter
	cbnz	x0,.fail
	mrs	x0,cntpct_el0			; duree (us)
	adr	x1,t0
	ldr	x1,[x1]
	sub	x0,x0,x1
	movl	x1,1000000
	mul	x0,x0,x1
	mrs	x1,cntfrq_el0
	udiv	x0,x0,x1
	str	x0,[x21,V3DR_US]
	adr	x1,jobs
	ldp	x2,x3,[x1]
	add	x2,x2,1
	add	x3,x3,x0
	stp	x2,x3,[x1]
	adr	x1,hangs
	str	xzr,[x1]
	mov	x0,0
	b	.out
.fail:	adr	x0,m_timeout
	bl	puts
	ldr	w0,[x19,CLE_CT0CS]
	bl	print_dec
	adr	x0,m_sp
	bl	puts
	ldr	w0,[x19,CLE_CT1CS]
	bl	print_dec
	adr	x0,m_sp
	bl	puts
	ldr	w0,[x19,CTL_INT_STS]
	bl	print_dec
	adr	x0,m_sp				; ou les listes se sont arretees
	bl	puts
	ldr	w0,[x19,CLE_CT0CA]
	bl	print_dec
	adr	x0,m_sp
	bl	puts
	ldr	w0,[x19,CLE_CT1CA]
	bl	print_dec
	adr	x0,m_sp
	bl	puts
	mov	x1,ERR_ERRSTAT
	ldr	w0,[x19,x1]
	bl	print_dec
	adr	x0,m_sp				; debut du tampon de travail
	bl	puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	bl	gpu_recover
	cbnz	x0,.lost
	adr	x0,gpu_state
	mov	x1,-1
	str	x1,[x0]
	adr	x0,m_dead
	bl	puts
.lost:	mov	x0,V3D_E_LOST
	b	.out
.inval:	mov	x0,V3D_E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = &{decalage, octets} ; x24 = taille du tampon -> x0 = 1 si la zone y
; tient (octets 0 : rien a verifier)
zone_ok:
	ldp	x1,x2,[x0]
	cbz	x2,.ok
	cmp	x1,x24
	b.hs	.no
	sub	x3,x24,x1
	cmp	x2,x3
	b.hi	.no
.ok:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = registre compteur (BFC/RFC), x1 = ancienne valeur (8 bits), x19 = coeur
; -> x0 = 0 quand le compteur a bouge, -1 apres TIMEOUT_MS ; sert les
; demandes de memoire du binner. Les premieres 2 ms en boucle serree, puis en
; cedant le processeur.
wait_counter:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	sys	SYS_TIME_MS
	add	x22,x0,TIMEOUT_MS
	add	x23,x0,2
.w:	ldr	w0,[x19,x20]
	and	w0,w0,0xFF
	cmp	w0,w21
	b.ne	.done
	ldr	w0,[x19,CTL_INT_STS]
	tbz	w0,2,.nomem			; OUTOMEM : un bloc du pool
	bl	give_chunk
.nomem:	sys	SYS_TIME_MS
	cmp	x0,x22
	b.hs	.late
	cmp	x0,x23
	b.lo	.w
	adr	x0,gpu_irq
	ldr	x0,[x0]
	cbz	x0,.yield
	mov	x0,V3D_IRQ
	mov	x1,V3D_IRQ_MS
	sys	SYS_IRQ_WAIT
	cmp	x0,1
	b.ne	.w
	mov	w0,INTS and not INT_OUTOMEM	; (OUTOMEM : give_chunk, ci-dessus)
	str	w0,[x19,CTL_INT_CLR]
	b	.w
.yield:	sys	SYS_YIELD
	b	.w
.late:	mov	x0,-1
	b	.out
.done:	mov	x0,0
.out:	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

give_chunk:
	adr	x0,chunk
	ldr	x1,[x0]
	cmp	x1,OVF_CHUNKS
	b.hs	.none
	add	x2,x1,1
	str	x2,[x0]
	adr	x0,ovf_phys
	ldr	x0,[x0]
	mov	x2,OVF_CHUNK
	madd	x0,x1,x2,x0
	str	w0,[x19,PTB_BPOA]
	str	w2,[x19,PTB_BPOS]
	mov	w0,INT_OUTOMEM
	str	w0,[x19,CTL_INT_CLR]
	ret
.none:	mov	w0,INT_OUTOMEM			; plus rien : ne plus le signaler
	str	w0,[x19,CTL_INT_MSK_SET]
	ret

; vide les caches du GPU (L2T puis SLC) avant chaque passe ; x19 = coeur
inval_caches:
	str	wzr,[x19,CTL_L2TFLSTA]
	mov	w0,-1
	str	w0,[x19,CTL_L2TFLEND]
	mov	w0,1				; L2TFLS, mode "flush"
	str	w0,[x19,CTL_L2TCACTL]
	mov	w0,-1
	str	w0,[x19,CTL_SLCACTL]
	ret

; x0 = mot -> x0 = 1 s'il commence une ligne de SD0:/etc/options.txt
option_word:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x0,p_options
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	mov	x19,x0
	adr	x1,optbuf
	mov	x2,1023
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.non
	adr	x0,optbuf
	strb	wzr,[x0,x20]
.ligne:	mov	x2,0
.c:	ldrb	w3,[x21,x2]
	cbz	w3,.fin
	ldrb	w4,[x0,x2]
	cmp	w3,w4
	b.ne	.suiv
	add	x2,x2,1
	b	.c
.fin:	ldrb	w4,[x0,x2]
	cmp	w4,' '
	b.ls	.oui
.suiv:	ldrb	w3,[x0],1
	cbz	w3,.non
	cmp	w3,10
	b.ne	.suiv
	b	.ligne
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

board_code

s_name		db V3D_SVC_NAME,0
s_no_opt	db 'no_gpu',0
p_options	db 'SD0:/etc/options.txt',0
m_no_opt	db 'v3d : no_gpu (options.txt) : rien',10,0
m_emul		db 'v3d : pas de V3D (QEMU) : rendu sur le CPU',10,0
m_pi5		db 'v3d : Pi 5 (V3D 7.1) pas encore pris : rendu sur le CPU',10,0
m_ok		db 'v3d : GPU V3D 4.2 pr',0xC3,0xAA,'t (r',0xC3,0xA9,'vision ',0
m_ok2		db ', scrutation, sans MMU GPU)',10,0
m_nomap		db 'v3d : registres inaccessibles (dev_map)',10,0
m_nomem		db 'v3d : pas de m',0xC3,0xA9,'moire DMA',10,0
m_power		db 'v3d : mise sous tension impossible',10,0
m_ident		db 'v3d : identification inattendue (pas un V3D 4.2)',10,0
m_noreg		db 'v3d : inscription du service impossible',10,0
m_reset		db 'v3d : GPU remis a zero',10,0
m_dead		db 'v3d : GPU laisse eteint',10,0
m_timeout	db 'v3d : job non termin',0xC3,0xA9,' (CT0CS CT1CS INT en d',0xC3,0xA9,'cimal) ',0
m_sp		db ' ',0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
board_data
		align 16
fw_rev		dw 28, 0, 0x00000001, 4, 0, 0, 0	; GET_FIRMWARE_REVISION
		align 16
fw_domain	dw 32, 0, 0x00038030, 8, 0, 10, 1, 0	; SET_DOMAIN_STATE
fw_clk		dw 32, 0, 0x00038001, 8, 0, 5, 1, 0	; SET_CLOCK_STATE
fw_maxclk	dw 32, 0, 0x00030004, 8, 0, 5, 0, 0	; GET_MAX_CLOCK_RATE
		align 16
fw_setclk	dw 36, 0, 0x00038002, 12, 0, 5, 0, 0, 0	; SET_CLOCK_RATE
		align 16
fw_domoff	dw 32, 0, 0x00038030, 8, 0, 10, 0, 0	; SET_DOMAIN_STATE (coupe)
		align 16
fw_temp		dw 32, 0, 0x00030006, 8, 0, 0, 0, 0	; GET_TEMPERATURE
		align 16
fw_v3dclk	dw 32, 0, 0x00030002, 8, 0, 5, 0, 0	; GET_CLOCK_RATE (V3D)
		align 16
fw_armclk	dw 32, 0, 0x00030002, 8, 0, 3, 0, 0	; GET_CLOCK_RATE (ARM)
		align 16
fw_throttled	dw 32, 0, 0x00030046, 4, 0, 0, 0, 0	; GET_THROTTLED
		align 8
v3d		dd 0
pm		dd 0
ovf_phys	dd 0
gpu_state	dd 0			; 1 pret, -1 eteint apres HANG_MAX blocages
hangs		dd 0			; blocages sans job reussi depuis
resets		dd 0			; remises a zero depuis le demarrage
gpu_rev		dd 0
gpu_irq		dd 0				; 1 : interruption tenue
jobs		dd 0, 0			; jobs faits, temps GPU (us)
chunk		dd 0
t0		dd 0
binfo		rb BI_SIZE
msg		rb 512
optbuf		rb 1024
