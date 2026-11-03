; ============================================================================
;  gic.asm - controleur d'interruptions GIC-400 (GICv2) du BCM2711
;
;  Distributor : partage, initialise une fois (coeur 0).
;  CPU interface + SGI/PPI (0..31) : bancalises, a initialiser sur chaque coeur.
; ============================================================================

GICD_CTLR	= 0x000
GICD_TYPER	= 0x004
GICD_ISENABLER	= 0x100
GICD_ICENABLER	= 0x180
GICD_ICPENDR	= 0x280
GICD_ICACTIVER	= 0x380
GICD_IPRIORITYR	= 0x400
GICD_ITARGETSR	= 0x800
GICD_ICFGR	= 0xC00
GICD_SGIR	= 0xF00

GICC_CTLR	= 0x000
GICC_PMR	= 0x004
GICC_IAR	= 0x00C
GICC_EOIR	= 0x010

IRQ_PRIORITY	= 0xA0A0A0A0		; meme priorite pour tous (pas d'imbrication)
IRQ_SPURIOUS	= 1023

; Initialisation du distributor (une seule fois, coeur 0)
gic_init_dist:
	movl	x0,GICD_BASE
	str	wzr,[x0,GICD_CTLR]

	ldr	w1,[x0,GICD_TYPER]
	and	w1,w1,0x1F
	add	w1,w1,1				; nombre de groupes de 32 IRQ
	adr	x2,gic_nirq
	lsl	w3,w1,5
	str	w3,[x2]

	; desactiver / acquitter toutes les SPI (groupes 1..n-1)
	mov	w2,1
	mov	w4,-1
.grp:	cmp	w2,w1
	b.hs	.grp_done
	add	x5,x0,x2,lsl 2
	str	w4,[x5,GICD_ICENABLER]
	str	w4,[x5,GICD_ICPENDR]
	str	w4,[x5,GICD_ICACTIVER]
	add	x6,x0,x2,lsl 3			; ICFGR : 2 bits/IRQ -> 2 mots par groupe
	str	wzr,[x6,GICD_ICFGR]		; toutes les SPI en niveau
	str	wzr,[x6,GICD_ICFGR+4]
	add	w2,w2,1
	b	.grp
.grp_done:

	; priorites et cibles des SPI (32..n-1), 4 IRQ par mot
	movl	w4,IRQ_PRIORITY
	movl	w5,0x01010101			; cible : coeur 0
	mov	w2,32
.spi:	cmp	w2,w3
	b.hs	.spi_done
	add	x6,x0,x2
	str	w4,[x6,GICD_IPRIORITYR]
	str	w5,[x6,GICD_ITARGETSR]
	add	w2,w2,4
	b	.spi
.spi_done:
	mov	w1,1
	str	w1,[x0,GICD_CTLR]
	ret

; Initialisation de la partie bancalisee (a appeler sur chaque coeur)
gic_init_cpu:
	movl	x0,GICD_BASE
	mov	w1,-1
	movl	w2,0xFFFF0000
	str	w2,[x0,GICD_ICENABLER]		; PPI off
	mov	w2,0xFFFF
	str	w2,[x0,GICD_ISENABLER]		; SGI on (IPI)
	str	w1,[x0,GICD_ICPENDR]
	str	w1,[x0,GICD_ICACTIVER]
	movl	w1,IRQ_PRIORITY
	mov	x2,0
.prio:	add	x3,x0,x2
	str	w1,[x3,GICD_IPRIORITYR]
	add	x2,x2,4
	cmp	x2,32
	b.lo	.prio

	movl	x0,GICC_BASE
	mov	w1,0xF0
	str	w1,[x0,GICC_PMR]		; laisse passer les priorites < 0xF0
	mov	w1,1
	str	w1,[x0,GICC_CTLR]
	ret

; w0 = numero d'IRQ a activer
gic_enable_irq:
	movl	x1,GICD_BASE
	lsr	w2,w0,5
	add	x1,x1,x2,lsl 2
	and	w3,w0,31
	mov	w4,1
	lslv	w4,w4,w3
	str	w4,[x1,GICD_ISENABLER]
	ret

; Appele par le vecteur IRQ (x0 = trame sauvegardee)
irq_dispatch:
	prologue
	str	x19,[sp,-16]!
.next:	movl	x1,GICC_BASE
	ldr	w19,[x1,GICC_IAR]
	and	w0,w19,0x3FF
	cmp	w0,IRQ_SPURIOUS
	b.eq	.out
	cmp	w0,16
	b.lo	.sgi
	cmp	w0,IRQ_TIMER
	b.ne	.driver
	bl	timer_irq
	b	.eoi
.driver:
	bl	irq_fire			; une ligne tenue par un pilote (irq.asm)
	cbnz	x0,.eoi
	and	w0,w19,0x3FF
	b	.unknown
.sgi:	cmp	w0,SGI_MEMBARRIER
	b.ne	.resched
	bl	membarrier_ipi
	b	.eoi
.resched:
	this_cpu x0				; IPI de reordonnancement
	mov	x1,1
	str	x1,[x0,PC_NEED_RESCHED]
	b	.eoi
.unknown:					; IRQ sans gestionnaire : on la coupe
	bl	gic_disable_irq
.eoi:	movl	x1,GICC_BASE
	str	w19,[x1,GICC_EOIR]
	b	.next
.out:	ldr	x19,[sp],16
	epilogue

; w0 = numero d'IRQ a desactiver
gic_disable_irq:
	movl	x1,GICD_BASE
	lsr	w2,w0,5
	add	x1,x1,x2,lsl 2
	and	w3,w0,31
	mov	w4,1
	lslv	w4,w4,w3
	str	w4,[x1,GICD_ICENABLER]
	ret

; x0 = coeur cible, w1 = numero de SGI
gic_send_sgi:
	movl	x2,GICD_BASE
	mov	w3,1 shl 16
	lslv	w3,w3,w0
	orr	w3,w3,w1
	dsb	ish
	str	w3,[x2,GICD_SGIR]
	ret

; x0 = coeur cible : IPI SGI_RESCHED
gic_send_sgi_resched:
	movl	x1,GICD_BASE
	mov	w2,1 shl 16
	lslv	w2,w2,w0
						; SGI_RESCHED = 0 : identifiant deja nul
	dsb	ish				; publier l'etat avant l'IPI
	str	w2,[x1,GICD_SGIR]
	ret
