; ============================================================================
;  arch/pi4/gpio.asm - broches GPIO du BCM2711 (interface commune aux cartes)
;
;  Fonctions feuilles : n'utilisent que x0, x1, x9-x12 (appelees en boucle
;  serree par l'I2C logiciel de touch.asm).
;    gpio_fsel  w0 = broche, w1 = fonction (0 entree, 1 sortie, 4 ALT0,
;               5 ALT1, 6 ALT2, 7 ALT3, 3 ALT4, 2 ALT5)
;    gpio_get   w0 = broche -> w0 = niveau (0/1)
;    gpio_set   w0 = broche, w1 = niveau a imposer (sortie)
;    gpio_pull  w0 = broche, w1 = 0 aucun, 1 tirage haut, 2 tirage bas
; ============================================================================

GP_FSEL0	= 0x00
GP_SET0		= 0x1C
GP_CLR0		= 0x28
GP_LEV0		= 0x34
GP_PUPPDN0	= 0xE4			; specifique BCM2711 : 2 bits par broche

gpio_fsel:
	movl	x9,GPIO_BASE
	mov	w10,10				; registre GPFSELn = broche / 10
	udiv	w11,w0,w10
	msub	w12,w11,w10,w0			; broche % 10
	add	x9,x9,x11,lsl 2
	mov	w10,3
	mul	w12,w12,w10			; position : 3 bits par broche
	ldr	w10,[x9,GP_FSEL0]
	mov	w11,7
	lslv	w11,w11,w12
	bic	w10,w10,w11
	and	w11,w1,7
	lslv	w11,w11,w12
	orr	w10,w10,w11
	str	w10,[x9,GP_FSEL0]
	dsb	sy
	ret

gpio_get:
	movl	x9,GPIO_BASE
	lsr	w10,w0,5			; GPLEV0 / GPLEV1
	add	x9,x9,x10,lsl 2
	ldr	w10,[x9,GP_LEV0]
	and	w11,w0,31
	lsrv	w0,w10,w11
	and	w0,w0,1
	ret

gpio_set:
	movl	x9,GPIO_BASE
	lsr	w10,w0,5
	add	x9,x9,x10,lsl 2
	and	w11,w0,31
	mov	w10,1
	lslv	w10,w10,w11
	cbz	w1,.clr
	str	w10,[x9,GP_SET0]
	ret
.clr:	str	w10,[x9,GP_CLR0]
	ret

gpio_pull:
	movl	x9,GPIO_BASE
	lsr	w10,w0,4			; 16 broches par registre
	add	x9,x9,x10,lsl 2
	and	w12,w0,15
	lsl	w12,w12,1
	ldr	w10,[x9,GP_PUPPDN0]
	mov	w11,3
	lslv	w11,w11,w12
	bic	w10,w10,w11
	and	w11,w1,3
	lslv	w11,w11,w12
	orr	w10,w10,w11
	str	w10,[x9,GP_PUPPDN0]
	ret
