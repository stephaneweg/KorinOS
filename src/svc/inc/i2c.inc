; ============================================================================
;  inc/i2c.inc - bus I2C 1 en logiciel sur GPIO 2 (SDA) / 3 (SCL), pour les
;  services de SD0:/sys (droit dev_map) : hwmanager (detection) et les
;  pilotes I2C (gt911). Un seul process a la fois pilote le bus : hwmanager
;  ne le touche plus une fois le pilote lance.
;
;	include '../inc/i2c.inc'	(apres kit.inc ; macros et constantes : le code
;				 est dans la macro i2c_code)
;	segment executable ... i2c_code
;	segment writeable ...  i2c_data
;	bl i2c_ouvrir			; -> x0 < 0 si les GPIO sont refuses
;	bl i2c1_xfer / i2c_present
;  Le compteur du processeur (cntpct_el0) est lisible en EL0 (noyau :
;  CNTKCTL_EL1, timer.asm).
; ============================================================================

GPIO_PHYS	= 0xFE200000		; BCM2711 (Pi 4)

BB_SDA		= 2
BB_SCL		= 3
BB_STRETCH_MS	= 10			; SCL retenu plus longtemps : bus bloque
I2C_E_ADDR	= 1			; adresse non acquittee (en ecriture)
I2C_E_DATA	= 2			; octet non acquitte
I2C_E_RADDR	= 3			; adresse non acquittee (en lecture)
I2C_E_SCL	= 4			; SCL retenu (etirement trop long)

macro i2c_code {

; -> x0 = 0, ou < 0 si les registres GPIO sont refuses (hors SD0:/sys)
i2c_ouvrir:
	prologue
	movl	x0,GPIO_PHYS
	mov	x1,4096
	sys	SYS_DEV_MAP
	tbnz	x0,63,.out
	adr	x1,gpio_va
	str	x0,[x1]
	bl	i2c1_init
	mov	x0,0
.out:	epilogue

; w0 = adresse 7 bits -> x0 = 1 si un peripherique l'acquitte (lecture d'un
; octet, sans registre : sans effet sur le GT911)
i2c_present:
	prologue
	sub	sp,sp,16
	mov	x1,0
	mov	x2,0
	mov	x3,sp
	mov	x4,1
	bl	i2c1_xfer
	cmp	x0,0
	cset	x0,eq
	add	sp,sp,16
	epilogue

; --- broches GPIO du BCM2711 (repris d'arch/pi4/gpio.asm, registres mappes) ---
; fonctions feuilles : n'utilisent que x0, x1, x9-x12
GP_FSEL0	= 0x00
GP_SET0		= 0x1C
GP_CLR0		= 0x28
GP_LEV0		= 0x34
GP_PUPPDN0	= 0xE4			; specifique BCM2711 : 2 bits par broche

; w0 = broche, w1 = fonction (0 entree, 1 sortie)
gpio_fsel:
	adr	x9,gpio_va
	ldr	x9,[x9]
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

; w0 = broche -> w0 = niveau (0/1)
gpio_get:
	adr	x9,gpio_va
	ldr	x9,[x9]
	lsr	w10,w0,5			; GPLEV0 / GPLEV1
	add	x9,x9,x10,lsl 2
	ldr	w10,[x9,GP_LEV0]
	and	w11,w0,31
	lsrv	w0,w10,w11
	and	w0,w0,1
	ret

; w0 = broche, w1 = niveau a imposer (sortie)
gpio_set:
	adr	x9,gpio_va
	ldr	x9,[x9]
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

; w0 = broche, w1 = 0 aucun, 1 tirage haut, 2 tirage bas
gpio_pull:
	adr	x9,gpio_va
	ldr	x9,[x9]
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

; ---------------------------------------------------------------------------
;  Bus I2C 1 en logiciel sur GPIO 2 (SDA) / 3 (SCL), ~100 kHz
;
;  Le controleur BSC du SoC gere mal l'etirement d'horloge du GT911 (le
;  transfert reste « actif » indefiniment) : on pilote les lignes nous-memes.
;  Ligne a 1 = broche en entree (tirage haut de la carte), a 0 = sortie basse.
;  SCL relache : on attend qu'il remonte (le GT911 peut le retenir).
; ---------------------------------------------------------------------------


; codes d'echec (i2c_last_s, pour le journal)

; Broches en entree (lignes relachees), sorties preparees a 0, bus debloque
i2c1_init:
	prologue
	mov	w0,BB_SDA			; tirage haut sur les deux lignes
	mov	w1,1
	bl	gpio_pull
	mov	w0,BB_SCL
	mov	w1,1
	bl	gpio_pull
	mov	w0,BB_SDA			; niveau de sortie : 0
	mov	w1,0
	bl	gpio_set
	mov	w0,BB_SCL
	mov	w1,0
	bl	gpio_set
	mov	w0,BB_SDA
	mov	w1,0
	bl	bb_dir
	mov	w0,BB_SCL
	mov	w1,0
	bl	bb_dir
	mrs	x0,cntfrq_el0			; demi-periode : 5 us
	movl	x1,200000
	udiv	x0,x0,x1
	adr	x1,bb_half
	str	x0,[x1]
	bl	bb_recover
	epilogue

; w0 = broche (2 ou 3), w1 = 1 sortie basse / 0 entree (ligne relachee)
bb_dir:
	b	gpio_fsel			; (fonction 1 sortie, 0 entree)

; attente d'une demi-periode
bb_delay:
	adr	x9,bb_half
	ldr	x9,[x9]
	mrs	x10,cntpct_el0
	add	x10,x10,x9
.w:	isb
	mrs	x11,cntpct_el0
	cmp	x11,x10
	b.lo	.w
	ret

; -> w0 = niveau de SDA (0/1)
bb_sda_in:
	mov	w0,BB_SDA
	b	gpio_get

bb_sda_low:
	mov	w0,BB_SDA
	mov	w1,1
	b	bb_dir
bb_sda_rel:
	mov	w0,BB_SDA
	mov	w1,0
	b	bb_dir
bb_scl_low:
	mov	w0,BB_SCL
	mov	w1,1
	b	bb_dir

; relache SCL et attend qu'il remonte -> x0 = 0, -1 si retenu trop longtemps
bb_scl_rel:
	prologue
	str	x19,[sp,-16]!
	mov	w0,BB_SCL
	mov	w1,0
	bl	bb_dir
	mrs	x19,cntpct_el0			; echeance
	mrs	x0,cntfrq_el0
	mov	x1,1000 / BB_STRETCH_MS
	udiv	x0,x0,x1
	add	x19,x19,x0
.w:	mov	w0,BB_SCL
	bl	gpio_get
	cbnz	w0,.up
	mrs	x0,cntpct_el0
	cmp	x0,x19
	b.lo	.w
	mov	x0,-1
	b	.out
.up:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; condition START (ou START repete : SCL peut etre bas)
bb_start:
	prologue
	bl	bb_sda_rel
	bl	bb_delay
	bl	bb_scl_rel
	bl	bb_delay
	bl	bb_sda_low
	bl	bb_delay
	bl	bb_scl_low
	bl	bb_delay
	epilogue

; condition STOP
bb_stop:
	prologue
	bl	bb_sda_low
	bl	bb_delay
	bl	bb_scl_rel
	bl	bb_delay
	bl	bb_sda_rel
	bl	bb_delay
	epilogue

; w0 = octet -> x0 = 0 acquitte, 1 non acquitte, -1 SCL retenu
bb_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	mov	x20,7
.bit:	lsrv	w0,w19,w20
	tbz	w0,0,.zero
	bl	bb_sda_rel
	b	.clk
.zero:	bl	bb_sda_low
.clk:	bl	bb_delay
	bl	bb_scl_rel
	tbnz	x0,63,.out
	bl	bb_delay
	bl	bb_scl_low
	subs	x20,x20,1
	b.pl	.bit
	bl	bb_sda_rel			; bit d'acquittement (lu)
	bl	bb_delay
	bl	bb_scl_rel
	tbnz	x0,63,.out
	bl	bb_delay
	bl	bb_sda_in
	mov	w19,w0				; 0 : acquitte
	bl	bb_scl_low
	bl	bb_delay
	mov	x0,x19
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = 1 acquitter (d'autres octets suivent) -> x0 = octet, -1 si SCL retenu
bb_read:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w21,w0
	mov	w19,0
	mov	x20,8
	bl	bb_sda_rel
.bit:	bl	bb_delay
	bl	bb_scl_rel
	tbnz	x0,63,.out
	bl	bb_delay
	bl	bb_sda_in
	orr	w19,w0,w19,lsl 1
	bl	bb_scl_low
	subs	x20,x20,1
	b.ne	.bit
	cbz	w21,.nack			; acquittement (ou non, au dernier octet)
	bl	bb_sda_low
	b	.ack
.nack:	bl	bb_sda_rel
.ack:	bl	bb_delay
	bl	bb_scl_rel
	tbnz	x0,63,.out
	bl	bb_delay
	bl	bb_scl_low
	bl	bb_sda_rel
	mov	x0,x19
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Deblocage : 9 impulsions d'horloge (un esclave coupe en plein octet libere
; SDA), puis STOP
bb_recover:
	prologue
	str	x19,[sp,-16]!
	bl	bb_sda_rel
	mov	x19,9
.p:	bl	bb_scl_low
	bl	bb_delay
	bl	bb_scl_rel
	bl	bb_delay
	subs	x19,x19,1
	b.ne	.p
	bl	bb_stop
	ldr	x19,[sp],16
	epilogue

; w0 = adresse 7 bits, x1 = octets a ecrire, x2 = leur nombre, x3 = tampon de
; lecture, x4 = octets a lire -> x0 = 0 si ok, -1 sinon (code : i2c_last_s)
; (ecriture puis lecture : START repete)
i2c1_xfer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	w19,w0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	cbz	x21,.read
	bl	bb_start
	lsl	w0,w19,1			; adresse, ecriture
	bl	bb_write
	mov	x24,I2C_E_ADDR
	cbnz	x0,.err
.w:	cbz	x21,.read
	ldrb	w0,[x20],1
	bl	bb_write
	mov	x24,I2C_E_DATA
	cbnz	x0,.err
	sub	x21,x21,1
	b	.w
.read:	cbz	x23,.done
	bl	bb_start			; (repete)
	lsl	w0,w19,1			; adresse, lecture
	orr	w0,w0,1
	bl	bb_write
	mov	x24,I2C_E_RADDR
	cbnz	x0,.err
.r:	cmp	x23,1
	cset	w0,ne				; acquitter sauf le dernier
	bl	bb_read
	mov	x24,I2C_E_SCL
	tbnz	x0,63,.err
	strb	w0,[x22],1
	subs	x23,x23,1
	b.ne	.r
.done:	bl	bb_stop
	mov	x0,0
	b	.out
.err:	tbz	x0,63,.code			; SCL retenu : code dedie
	mov	x24,I2C_E_SCL
.code:	adr	x0,i2c_last_s
	str	w24,[x0]
	bl	bb_stop
	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
}

macro i2c_data {
		align 8
gpio_va		dd 0
bb_half		dd 0
i2c_last_s	dw 0
		align 8
}
