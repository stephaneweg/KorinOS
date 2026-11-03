; ============================================================================
;  arch/pi5/gpio.asm - broches du connecteur (banque 0 du RP1), meme interface
;  que arch/pi4/gpio.asm. Une broche en entree/sortie logicielle passe par
;  la fonction SYS_RIO (5) ; les fonctions ALTn du BCM2711 sont traduites
;  en fonctions a0..a5 du RP1. Feuilles : x0, x1, x9-x12 seulement.
;  NON TESTE SUR MATERIEL.
; ============================================================================

RP1_FUNC_RIO	= 5
PAD_OD		= 1 shl 7			; sortie desactivee
PAD_IE		= 1 shl 6			; entree activee
PAD_PUE		= 1 shl 3
PAD_PDE		= 1 shl 2

; w0 = broche, w1 = fonction (0 entree, 1 sortie, 4..7 ALT0..ALT3, 3 ALT4, 2 ALT5)
gpio_fsel:
	movl	x9,RP1_PADS_BANK0		; plot : entree active, sortie permise
	add	x9,x9,4
	ldr	w10,[x9,x0,lsl 2]
	orr	w10,w10,PAD_IE
	bic	w10,w10,PAD_OD
	str	w10,[x9,x0,lsl 2]
	mov	w10,1				; bit de la broche dans RIO
	lslv	w10,w10,w0
	movl	x9,RP1_RIO_BANK0
	cmp	w1,1
	b.hi	.alt
	add	x11,x9,RP1_SET			; activation de sortie (+4) : 1 sortie
	add	x12,x9,RP1_CLR
	cmp	w1,1
	csel	x11,x11,x12,eq
	str	w10,[x11,4]
	mov	w12,RP1_FUNC_RIO
	b	.func
.alt:	adr	x11,rp1_alt_map			; ALTn du BCM2711 -> a0..a5 du RP1
	and	w12,w1,7
	ldrb	w12,[x11,x12]
.func:	movl	x9,RP1_IO_BANK0			; controle (+4) : FUNCSEL bits 4:0
	add	x9,x9,4
	lsl	x11,x0,3
	ldr	w10,[x9,x11]
	bic	w10,w10,0x1F
	orr	w10,w10,w12
	str	w10,[x9,x11]
	dsb	sy
	ret

gpio_get:
	movl	x9,RP1_RIO_BANK0
	ldr	w10,[x9,8]			; entrees synchronisees
	lsrv	w0,w10,w0
	and	w0,w0,1
	ret

gpio_set:
	movl	x9,RP1_RIO_BANK0
	mov	w10,1
	lslv	w10,w10,w0
	add	x11,x9,RP1_SET
	add	x12,x9,RP1_CLR
	cmp	w1,0
	csel	x11,x11,x12,ne
	str	w10,[x11]			; registre de sortie (+0)
	ret

gpio_pull:
	movl	x9,RP1_PADS_BANK0
	add	x9,x9,4
	ldr	w10,[x9,x0,lsl 2]
	bic	w10,w10,PAD_PUE or PAD_PDE
	cmp	w1,1
	b.ne	.down
	orr	w10,w10,PAD_PUE
	b	.st
.down:	cmp	w1,2
	b.ne	.st
	orr	w10,w10,PAD_PDE
.st:	str	w10,[x9,x0,lsl 2]
	ret

; index : fonction BCM2711 (0-7) -> fonction RP1 (entree/sortie : sans objet)
rp1_alt_map	db 0, 0, 5, 4, 0, 1, 2, 3
		align 4
