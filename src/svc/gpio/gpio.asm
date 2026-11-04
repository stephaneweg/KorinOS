; ============================================================================
;  gpio - le connecteur a 40 broches du Pi 4 pour les programmes (service
;  " gpio ", GpioKit : docs/KITS.md, docs/BASIC.md). Lance par init2
;  (SD0:/etc/services.txt).
;
;  Une broche a un seul proprietaire, de son premier usage jusqu'a GP_M_FREE,
;  gp_release ou la fin du process (SVF_FINS) : elle redevient une entree sans
;  tirage. Reservees : 0 / 1 (EEPROM des cartes HAT), 14 / 15 (console serie),
;  et 2 / 3 / 4 tant que le pilote du tactile tourne (PocketTerm : GT911 sur
;  le bus I2C 1). Le reste du systeme n'est pas sur le connecteur (Bluetooth,
;  prise jack, LED).
;
;  * PWM : PWM0 (GPIO 12 / 18 canal 1, 13 / 19 canal 2) a l'horloge de 125 MHz
;    que partage PWM1, celui de la prise jack : jamais changee, mise en route
;    seulement si pwmaudio ne l'a pas fait.
;  * Fronts : releves entre deux requetes (GPLEV toutes les ms au plus) tant
;    qu'une broche en demande : l'interruption 145 est au tactile.
;  * I2C : le bus 1 en logiciel (svc/inc/i2c.inc), environ 100 kHz.
;  * SPI : SPI0 sans DMA, a l'horloge du coeur (500 MHz).
;
;  Sous QEMU (PWM, BSC, SPI non emules : un acces est une erreur de bus) les
;  broches restent, le reste rend GP_ENODEV ; GPIO_OP_INFO le dit (+8), et
;  GpioKit prend alors son simulateur, sauf gp_sim (0).
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../inc/i2c.inc'
include '../../inc/gpio.inc'
include '../../kits/gpiokit/gpiokit.inc'
include '../inc/board.inc'

SPI_PHYS	= 0xFE204000
PWM_PAGE	= 0xFE20C000
CM_PHYS		= 0xFE101000
CM_PWMCTL	= 0xA0
CM_PASSWD	= 0x5A000000
PWM_CTL		= 0x00
PWM_RNG1	= 0x10
PWM_DAT1	= 0x14
PWM_RNG2	= 0x20
PWM_DAT2	= 0x24
PWM_CLOCK	= 125000000
SPI_CS		= 0x00
SPI_FIFO	= 0x04
SPI_CLK		= 0x08
SPI_CORE	= 500000000
CS_CLEAR	= 3 shl 4
CS_TA		= 1 shl 7
EVQ		= 256			; fronts gardes (pour tous les process)
EV_SIZE		= 24			; GPE_SIZE, puis le pid
PIW_WANT	= PIW_REASON		; (dans pins : les fronts voulus ; GPIO_OP_INFO y met la raison)

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	bl	kits_open
	bl	board_pi5			; Pi 5 : connecteur sur le RP1, pas encore
	cbz	x0,.pi4
	adr	x0,m_pi5			; (GpioKit prend son simulateur)
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.pi4:	adr	x0,fw_rev			; QEMU : ni PWM, ni SPI
	sys	SYS_FW_CALL
	mov	x1,1
	tbnz	x0,63,.emul
	adr	x0,fw_rev
	ldr	w0,[x0,20]
	movl	w2,0x50000000
	cmp	w0,w2
	b.lo	.emul
	mov	x1,0
.emul:	adr	x0,emulated
	str	x1,[x0]
	movl	x0,GPIO_PHYS
	mov	x1,4096
	sys	SYS_DEV_MAP
	tbnz	x0,63,.fail
	adr	x1,gpio_va
	str	x0,[x1]
	adr	x1,emulated
	ldr	x1,[x1]
	cbnz	x1,.reg
	movl	x0,SPI_PHYS
	mov	x1,4096
	sys	SYS_DEV_MAP
	tbnz	x0,63,.fail
	adr	x1,spi_va
	str	x0,[x1]
	movl	x0,PWM_PAGE
	mov	x1,4096
	sys	SYS_DEV_MAP
	tbnz	x0,63,.fail
	adr	x1,pwm_va
	str	x0,[x1]
	movl	x0,CM_PHYS
	mov	x1,4096
	sys	SYS_DEV_MAP
	tbnz	x0,63,.fail
	adr	x1,cm_va
	str	x0,[x1]
.reg:	adr	x0,s_gpio
	mov	x1,SVF_FINS
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,.fail
	adr	x1,chan
	str	x0,[x1]
.loop:	bl	poll_edges
	adr	x0,chan
	ldr	x0,[x0]
	adr	x1,msg
	adr	x2,n_edges			; des fronts a relever : 1 ms
	ldr	x2,[x2]
	mov	x3,1000
	mov	x4,1
	cmp	x2,0
	csel	x2,x3,x4,eq
	sys	SYS_SVC_NEXT
	tbnz	x0,63,.fail
	cbz	x0,.loop
	mov	x19,x0
	bl	dispatch
	cbz	x0,.loop
	mov	x0,x19
	adr	x1,msg
	sys	SYS_SVC_REPLY
	b	.loop
.fail:	mov	x0,1
	sys	SYS_EXIT

dispatch:
	prologue
	adr	x9,msg
	ldr	x0,[x9]
	movl	x1,SVC_OP_FIN
	cmp	x0,x1
	b.eq	.fin
	movl	x1,SVC_OP_NAISSANCE
	cmp	x0,x1
	b.eq	.avis
	movl	x1,SVC_OP_SIGNAL
	cmp	x0,x1
	b.eq	.avis
	cmp	x0,GPIO_OP_MAX
	b.hi	.inval
	cbz	x0,.inval
	adr	x1,op_table
	ldrsw	x2,[x1,x0,lsl 2]
	add	x2,x1,x2
	ldr	x0,[x9,8]
	ldr	x1,[x9,16]
	ldr	x3,[x9,24]
	ldr	w4,[x9,44]			; (pid de l'appelant)
	blr	x2
	b	.rep
.inval:	mov	x0,GP_EINVAL
.rep:	adr	x9,msg
	str	x0,[x9]
	mov	x0,1
	epilogue
.fin:	ldr	x0,[x9,WM_A]
	bl	release_pid
.avis:	mov	x0,0
	epilogue

		align 4
op_table:
	dw	0, op_info - op_table, op_mode - op_table, op_write - op_table, op_read - op_table
	dw	op_readall - op_table, op_pwm - op_table, op_edges - op_table, op_events - op_table
	dw	op_release - op_table, op_i2c_open - op_table, op_i2c_close - op_table
	dw	op_i2c_xfer - op_table, op_spi_open - op_table, op_spi_close - op_table, op_spi_xfer - op_table

; --- les broches --------------------------------------------------------------
; x0 = broche -> x0 = sa ligne dans pins
pin_row:
	adr	x1,pins
	add	x0,x1,x0,lsl 4
	ret

; x0 = broche -> x0 = 1 si elle peut faire du PWM
is_pwm:
	cmp	x0,12
	b.eq	.y
	cmp	x0,13
	b.eq	.y
	cmp	x0,18
	b.eq	.y
	cmp	x0,19
	b.eq	.y
	mov	x0,0
	ret
.y:	mov	x0,1
	ret

; x0 = broche -> x0 = 0 libre de droits, 1 EEPROM, 2 console, 3 tactile
reserved_of:
	cmp	x0,1
	b.ls	.eeprom
	cmp	x0,14
	b.eq	.console
	cmp	x0,15
	b.eq	.console
	cmp	x0,4
	b.hi	.no
	prologue
	bl	touch_runs
	cmp	x0,0
	mov	x1,3
	csel	x0,x1,xzr,ne
	epilogue
.eeprom:mov	x0,1
	ret
.console:
	mov	x0,2
	ret
.no:	mov	x0,0
	ret

; -> x0 = 1 si le pilote gt911 tourne (la liste des process, relue chaque seconde)
touch_runs:
	prologue
	stp	x19,x20,[sp,-16]!
	sys	SYS_TIME_MS
	mov	x19,x0
	adr	x20,touch_t
	ldr	x1,[x20]
	cbz	x1,.look
	sub	x1,x0,x1
	cmp	x1,1000
	b.lo	.keep
.look:	str	x19,[x20]
	adr	x0,plist
	mov	x1,64
	sys	SYS_PROCLIST
	cmp	x0,0
	b.le	.none
	adr	x1,plist
	mov	x3,x0
.p:	adr	x0,s_gt911
	add	x2,x1,24
.c:	ldrb	w4,[x0],1
	ldrb	w5,[x2],1
	cmp	w4,w5
	b.ne	.n
	cbnz	w4,.c
	mov	x0,1
	b	.st
.n:	add	x1,x1,64
	subs	x3,x3,1
	b.ne	.p
.none:	mov	x0,0
.st:	str	x0,[x20,8]
.keep:	ldr	x0,[x20,8]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = broche, x4 = pid -> x0 = 0 : elle est a lui, ou libre et prise ; GP_E*
take_pin:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x4
	cmp	x0,GPIO_PINS
	b.hs	.inval
	bl	reserved_of
	cbnz	x0,.perm
	mov	x0,x19
	bl	pin_row
	ldr	w1,[x0,PIW_OWNER]
	cbz	w1,.free
	cmp	w1,w20
	b.ne	.busy
.free:	str	w20,[x0,PIW_OWNER]
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
	b	.out
.perm:	mov	x0,GP_EPERM
	b	.out
.busy:	mov	x0,GP_EBUSY
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = broche : entree sans tirage, a personne, plus de fronts
free_pin:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	pwm_stop_of
	mov	w0,w19
	mov	w1,0
	bl	gpio_fsel
	mov	w0,w19
	mov	w1,0
	bl	gpio_pull
	mov	x0,x19
	bl	pin_row
	ldrb	w1,[x0,PIW_FLAGS]
	tbz	w1,2,.e
	adr	x2,n_edges
	ldr	x3,[x2]
	sub	x3,x3,1
	str	x3,[x2]
.e:	strb	wzr,[x0,PIW_FLAGS]
	strb	wzr,[x0,PIW_MODE]
	strb	wzr,[x0,PIW_WANT]
	str	wzr,[x0,PIW_OWNER]
	str	wzr,[x0,PIW_FREQ]
	str	wzr,[x0,PIW_DUTY]
	ldr	x19,[sp],16
	epilogue

; x0 = broche : son canal PWM arrete si elle en avait un
pwm_stop_of:
	adr	x1,pins
	add	x1,x1,x0,lsl 4
	ldrb	w1,[x1,PIW_MODE]
	cmp	w1,GP_M_PWM
	b.ne	.r
	adr	x1,pwm_va
	ldr	x1,[x1]
	cbz	x1,.r
	mov	w2,0xFF				; PWEN1, MSEN1 : les 8 bits du canal
	cmp	x0,12
	b.eq	.c
	cmp	x0,18
	b.eq	.c
	lsl	w2,w2,8
.c:	ldr	w3,[x1,PWM_CTL]
	bic	w3,w3,w2
	str	w3,[x1,PWM_CTL]
.r:	ret

; w0 = pid : tout ce qu'il tient est rendu
release_pid:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w20,w0
	mov	x19,0
.l:	mov	x0,x19
	bl	pin_row
	ldr	w1,[x0,PIW_OWNER]
	cmp	w1,w20
	b.ne	.n
	mov	x0,x19
	bl	free_pin
.n:	add	x19,x19,1
	cmp	x19,GPIO_PINS
	b.lo	.l
	adr	x0,i2c_owner
	ldr	w1,[x0]
	cmp	w1,w20
	b.ne	.s
	str	wzr,[x0]
.s:	adr	x0,spi_owner
	ldr	w1,[x0]
	cmp	w1,w20
	b.ne	.q
	str	wzr,[x0]
.q:	adr	x0,ev_q				; ses fronts en attente
	mov	x1,EVQ
.d:	ldr	w2,[x0,GPE_SIZE]
	cmp	w2,w20
	b.ne	.dn
	str	wzr,[x0,GPE_SIZE]
.dn:	add	x0,x0,EV_SIZE
	subs	x1,x1,1
	b.ne	.d
	ldp	x19,x20,[sp],16
	epilogue

op_release:
	prologue
	mov	w0,w4
	bl	release_pid
	mov	x0,0
	epilogue

; +8 : emule ; +48 : les broches, niveaux a jour, reservees marquees
op_info:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,msg
	adr	x1,emulated
	ldr	x1,[x1]
	str	x1,[x0,8]
	bl	levels
	mov	x20,x0
	mov	x19,0
.l:	mov	x0,x19
	bl	reserved_of
	mov	x9,x0
	mov	x0,x19
	bl	pin_row
	mov	x1,x0
	adr	x3,msg + GPIO_MSG_DATA
	add	x3,x3,x19,lsl 4
	ldp	x4,x5,[x1]
	stp	x4,x5,[x3]
	ldrb	w4,[x3,PIW_FLAGS]
	cbz	x9,.nr
	orr	w4,w4,GP_F_RESERVED
.nr:	mov	x0,x19
	bl	is_pwm
	cbz	x0,.np
	orr	w4,w4,GP_F_PWM
.np:	strb	w4,[x3,PIW_FLAGS]
	strb	w9,[x3,PIW_REASON]
	lsr	x5,x20,x19
	and	w5,w5,1
	strb	w5,[x3,PIW_LEVEL]
	add	x19,x19,1
	cmp	x19,GPIO_PINS
	b.lo	.l
	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = bits des niveaux (GPIO 0..27)
levels:
	adr	x1,gpio_va
	ldr	x1,[x1]
	ldr	w0,[x1,GP_LEV0]
	and	w0,w0,0xFFFFFFF
	ret

op_readall:
	b	levels

op_read:
	cmp	x0,GPIO_PINS
	b.hs	.inval
	adr	x1,gpio_va
	ldr	x1,[x1]
	ldr	w1,[x1,GP_LEV0]
	lsr	w0,w1,w0
	and	x0,x0,1
	ret
.inval:	mov	x0,GP_EINVAL
	ret

; x0 = broche, x1 = mode, x4 = pid
op_mode:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x1,GP_M_ALT0 + 5
	b.hi	.inval
	cmp	x1,GP_M_PWM			; (gp_pwm, gp_i2c_open, gp_spi_open les mettent)
	b.eq	.inval
	cmp	x1,GP_M_I2C
	b.eq	.inval
	cmp	x1,GP_M_SPI
	b.eq	.inval
	bl	take_pin
	cbnz	x0,.out
	mov	x0,x19
	cbnz	x20,.set
	bl	free_pin
	mov	x0,0
	b	.out
.set:	bl	pwm_stop_of
	mov	x0,x19
	bl	pin_row
	strb	w20,[x0,PIW_MODE]
	mov	w0,w19				; le tirage
	mov	w1,0
	cmp	x20,GP_M_PULLUP
	b.ne	.pd
	mov	w1,1
.pd:	cmp	x20,GP_M_PULLDOWN
	b.ne	.pu
	mov	w1,2
.pu:	bl	gpio_pull
	cmp	x20,GP_M_OUT
	b.ne	.alt
	mov	w0,w19				; une sortie commence basse
	mov	w1,0
	bl	gpio_set
	mov	w1,1
	b	.fs
.alt:	mov	w1,0
	cmp	x20,GP_M_ALT0
	b.lo	.fs
	sub	x2,x20,GP_M_ALT0
	adr	x3,alt_code
	ldrb	w1,[x3,x2]
.fs:	mov	w0,w19
	bl	gpio_fsel
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = broche, x1 = niveau, x4 = pid
op_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x0,GPIO_PINS
	b.hs	.inval
	bl	pin_row
	ldr	w1,[x0,PIW_OWNER]
	cmp	w1,w4
	b.ne	.perm
	ldrb	w1,[x0,PIW_MODE]
	cmp	w1,GP_M_OUT
	b.ne	.inval
	mov	w0,w19
	cmp	x20,0
	cset	w1,ne
	bl	gpio_set
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
	b	.out
.perm:	mov	x0,GP_EPERM
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = broche, x1 = Hz, x3 = rapport /10000, x4 = pid
op_pwm:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x3
	mov	x22,x4
	adr	x2,pwm_va
	ldr	x2,[x2]
	cbz	x2,.nodev
	bl	is_pwm
	cbz	x0,.inval
	cbz	x20,.inval
	movl	x2,1000000
	cmp	x20,x2
	b.hi	.inval
	mov	x2,10000
	cmp	x21,x2
	b.hi	.inval
	cmp	x19,12				; 12 et 18, 13 et 19 partagent un canal
	mov	x0,18
	b.eq	.oth
	cmp	x19,18
	mov	x0,12
	b.eq	.oth
	cmp	x19,13
	mov	x0,19
	b.eq	.oth
	mov	x0,13
.oth:	bl	pin_row
	ldrb	w1,[x0,PIW_MODE]
	cmp	w1,GP_M_PWM
	b.eq	.busy
	mov	x0,x19
	mov	x4,x22
	bl	take_pin
	cbnz	x0,.out
	bl	pwm_clock
	movl	x0,PWM_CLOCK			; plage et donnee du canal
	udiv	x0,x0,x20
	mul	x1,x0,x21
	mov	x2,10000
	udiv	x1,x1,x2
	adr	x2,pwm_va
	ldr	x2,[x2]
	cmp	x19,12
	b.eq	.ch1
	cmp	x19,18
	b.eq	.ch1
	str	w0,[x2,PWM_RNG2]
	str	w1,[x2,PWM_DAT2]
	mov	w4,((1 shl 7) or 1) shl 8	; MSEN2, PWEN2
	b	.en
.ch1:	str	w0,[x2,PWM_RNG1]
	str	w1,[x2,PWM_DAT1]
	mov	w4,(1 shl 7) or 1		; MSEN1, PWEN1
.en:	ldr	w3,[x2,PWM_CTL]
	orr	w3,w3,w4
	str	w3,[x2,PWM_CTL]
	mov	w0,w19				; ALT0 (12, 13), ALT5 (18, 19)
	mov	w1,4
	cmp	x19,14
	b.lo	.fn
	mov	w1,2
.fn:	bl	gpio_fsel
	mov	x0,x19
	bl	pin_row
	mov	w1,GP_M_PWM
	strb	w1,[x0,PIW_MODE]
	str	w20,[x0,PIW_FREQ]
	str	w21,[x0,PIW_DUTY]
	mov	x0,0
	b	.out
.nodev:	mov	x0,GP_ENODEV
	b	.out
.busy:	mov	x0,GP_EBUSY
	b	.out
.inval:	mov	x0,GP_EINVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; l'horloge PWM : PLLD / 6 = 125 MHz si personne ne l'a mise en route (BUSY)
pwm_clock:
	adr	x0,cm_va
	ldr	x0,[x0]
	add	x0,x0,CM_PWMCTL
	ldr	w1,[x0]
	tbnz	w1,7,.r
	movl	w1,CM_PASSWD or (6 shl 12)
	str	w1,[x0,4]
	movl	w1,CM_PASSWD or 6
	str	w1,[x0]
	movl	w1,CM_PASSWD or 6 or (1 shl 4)
	str	w1,[x0]
.r:	ret

; --- les fronts -----------------------------------------------------------------
; x0 = broche, x1 = fronts voulus, x4 = pid : la broche doit etre a lui
op_edges:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x0,GPIO_PINS
	b.hs	.inval
	cmp	x1,GP_BOTH
	b.hi	.inval
	bl	pin_row
	ldr	w1,[x0,PIW_OWNER]
	cmp	w1,w4
	b.ne	.perm
	strb	w20,[x0,PIW_WANT]
	ldrb	w1,[x0,PIW_FLAGS]
	tbnz	w1,2,.was
	cbz	x20,.done
	orr	w1,w1,GP_F_EDGES
	strb	w1,[x0,PIW_FLAGS]
	adr	x2,n_edges
	ldr	x3,[x2]
	add	x3,x3,1
	str	x3,[x2]
	b	.done
.was:	cbnz	x20,.done
	bic	w1,w1,GP_F_EDGES
	strb	w1,[x0,PIW_FLAGS]
	adr	x2,n_edges
	ldr	x3,[x2]
	sub	x3,x3,1
	str	x3,[x2]
.done:	bl	levels				; (le releve repart d'ici)
	adr	x1,last_lev
	str	x0,[x1]
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
	b	.out
.perm:	mov	x0,GP_EPERM
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = microsecondes (l'horloge des fronts et de gp_now_us)
now_us:
	mrs	x0,cntpct_el0
	mrs	x1,cntfrq_el0
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0
	movl	x4,1000000
	mul	x2,x2,x4
	mul	x3,x3,x4
	udiv	x3,x3,x1
	add	x0,x2,x3
	ret

; (a chaque tour) les broches dont on veut les fronts, comparees au dernier releve
poll_edges:
	adr	x0,n_edges
	ldr	x0,[x0]
	cbz	x0,.none
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	bl	levels
	mov	x19,x0
	adr	x1,last_lev
	ldr	x20,[x1]
	str	x19,[x1]
	eor	x20,x20,x19
	mov	x21,0
.l:	lsr	x0,x20,x21
	tbz	x0,0,.n
	mov	x0,x21
	bl	pin_row
	ldrb	w1,[x0,PIW_FLAGS]
	tbz	w1,2,.n
	lsr	x2,x19,x21
	and	x2,x2,1				; niveau
	mov	x3,GP_FALLING
	mov	x4,GP_RISING
	cmp	x2,0
	csel	x3,x4,x3,ne
	ldrb	w4,[x0,PIW_WANT]
	tst	w4,w3
	b.eq	.n
	ldr	w5,[x0,PIW_OWNER]
	adr	x6,ev_head
	ldp	x7,x8,[x6]			; tete, queue
	add	x9,x7,1
	and	x9,x9,EVQ - 1
	cmp	x9,x8
	b.ne	.put
	mov	x7,1				; pleine : le suivant le dira
	str	x7,[x6,16]
	b	.n
.put:	adr	x10,ev_q
	mov	x11,EV_SIZE
	mul	x11,x11,x7
	add	x10,x10,x11
	strb	w21,[x10,GPE_PIN]
	strb	w3,[x10,GPE_EDGE]
	strb	w2,[x10,GPE_LEVEL]
	ldr	x11,[x6,16]
	strb	w11,[x10,GPE_LOST]
	str	xzr,[x6,16]
	str	w5,[x10,GPE_SIZE]
	str	x9,[x6]
	mov	x12,x10
	bl	now_us
	str	x0,[x12,GPE_US]
.n:	add	x21,x21,1
	cmp	x21,GPIO_PINS
	b.lo	.l
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.none:	ret

; x0 = places, x4 = pid -> n ; +48 : ses fronts (la file se resserre sur ceux des autres)
op_events:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	mov	x20,x4
	cmp	x0,GPIO_MSG_EVENTS
	mov	x1,GPIO_MSG_EVENTS
	csel	x9,x0,x1,lo
	adr	x10,msg + GPIO_MSG_DATA
	adr	x6,ev_head
	ldp	x7,x8,[x6]			; x8 = queue
	mov	x11,x8				; lecture
	mov	x12,x8				; ecriture (ceux qui restent)
.l:	cmp	x11,x7
	b.eq	.done
	adr	x13,ev_q
	mov	x14,EV_SIZE
	mul	x15,x14,x11
	add	x15,x13,x15
	ldr	w2,[x15,GPE_SIZE]
	cbz	w2,.next			; (process parti)
	cmp	w2,w20
	b.ne	.keep
	cmp	x19,x9
	b.hs	.keep
	ldp	x2,x3,[x15]
	stp	x2,x3,[x10],GPE_SIZE
	add	x19,x19,1
	b	.next
.keep:	mul	x14,x14,x12
	add	x13,x13,x14
	ldp	x2,x3,[x15]
	ldr	x4,[x15,16]
	stp	x2,x3,[x13]
	str	x4,[x13,16]
	add	x12,x12,1
	and	x12,x12,EVQ - 1
.next:	add	x11,x11,1
	and	x11,x11,EVQ - 1
	b	.l
.done:	str	x12,[x6]			; (la nouvelle tete, la queue ne bouge pas)
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; --- I2C : le bus 1 en logiciel --------------------------------------------------
; x4 = pid
op_i2c_open:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x4
	adr	x0,emulated
	ldr	x0,[x0]
	cbnz	x0,.nodev
	adr	x19,i2c_owner
	ldr	w0,[x19]
	cbz	w0,.free
	cmp	w0,w20
	b.ne	.busy
.free:	mov	x0,2
	mov	x4,x20
	bl	take_pin
	cbnz	x0,.out
	mov	x0,3
	mov	x4,x20
	bl	take_pin
	cbnz	x0,.out
	str	w20,[x19]
	mov	x0,2
	bl	pin_row
	mov	w1,GP_M_I2C
	strb	w1,[x0,PIW_MODE]
	strb	w1,[x0,PIW_SIZE + PIW_MODE]
	bl	i2c1_init
	mov	x0,0
	b	.out
.nodev:	mov	x0,GP_ENODEV
	b	.out
.busy:	mov	x0,GP_EBUSY
.out:	ldp	x19,x20,[sp],16
	epilogue

op_i2c_close:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x4
	adr	x19,i2c_owner
	ldr	w0,[x19]
	cmp	w0,w20
	b.ne	.ok
	str	wzr,[x19]
	mov	x0,2
	bl	free_pin
	mov	x0,3
	bl	free_pin
.ok:	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse, x1 = octets ecrits, x3 = octets lus, x4 = pid ; +48 dans les deux sens
op_i2c_xfer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x3
	adr	x2,i2c_owner
	ldr	w2,[x2]
	cmp	w2,w4
	b.ne	.perm
	mov	x2,GPIO_MSG_BYTES
	cmp	x1,x2
	b.hi	.inval
	cmp	x3,x2
	b.hi	.inval
	adr	x1,msg + GPIO_MSG_DATA
	adr	x3,msg_rd
	mov	x2,x19
	mov	x4,x20
	bl	i2c1_xfer
	cbnz	x0,.eio
	adr	x0,msg_rd
	adr	x1,msg + GPIO_MSG_DATA
	mov	x2,x20
	cbz	x2,.w
.c:	ldrb	w3,[x0],1
	strb	w3,[x1],1
	subs	x2,x2,1
	b.ne	.c
.w:	cmp	x20,0
	csel	x0,x20,x19,ne
	b	.out
.perm:	mov	x0,GP_EPERM
	b	.out
.inval:	mov	x0,GP_EINVAL
	b	.out
.eio:	mov	x0,GP_EIO
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- SPI0 sans DMA -----------------------------------------------------------------
; x0 = Hz (0 : 1 MHz), x1 = mode, x4 = pid
op_spi_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x4
	adr	x0,spi_va
	ldr	x0,[x0]
	cbz	x0,.nodev
	cmp	x20,3
	b.hi	.inval
	adr	x22,spi_owner
	ldr	w0,[x22]
	cbz	w0,.free
	cmp	w0,w21
	b.ne	.busy
.free:	mov	x23,7				; GPIO 7..11
.t:	mov	x0,x23
	mov	x4,x21
	bl	take_pin
	cbnz	x0,.out
	add	x23,x23,1
	cmp	x23,12
	b.lo	.t
	str	w21,[x22]
	mov	x23,7
.f:	mov	x0,x23
	bl	pin_row
	mov	w1,GP_M_SPI
	strb	w1,[x0,PIW_MODE]
	mov	w0,w23
	mov	w1,4				; ALT0
	bl	gpio_fsel
	add	x23,x23,1
	cmp	x23,12
	b.lo	.f
	cbnz	x19,.hz
	movl	x19,1000000
.hz:	movl	x0,SPI_CORE
	udiv	x0,x0,x19
	add	x0,x0,1
	and	x0,x0,not 1			; un diviseur pair
	cmp	x0,2
	mov	x1,2
	csel	x0,x1,x0,lo
	mov	x1,65534
	cmp	x0,x1
	csel	x0,x1,x0,hi
	adr	x1,spi_va
	ldr	x1,[x1]
	str	w0,[x1,SPI_CLK]
	adr	x1,spi_mode
	lsl	x20,x20,2			; CPHA, CPOL
	str	x20,[x1]
	mov	x0,0
	b	.out
.nodev:	mov	x0,GP_ENODEV
	b	.out
.inval:	mov	x0,GP_EINVAL
	b	.out
.busy:	mov	x0,GP_EBUSY
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

op_spi_close:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x4
	adr	x19,spi_owner
	ldr	w0,[x19]
	cmp	w0,w20
	b.ne	.ok
	str	wzr,[x19]
	mov	x19,7
.l:	mov	x0,x19
	bl	free_pin
	add	x19,x19,1
	cmp	x19,12
	b.lo	.l
.ok:	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

; x0 = CE, x1 = octets (+48 envoyes puis recus), x4 = pid
op_spi_xfer:
	adr	x2,spi_owner
	ldr	w2,[x2]
	cmp	w2,w4
	b.ne	.perm
	cmp	x0,1
	b.hi	.inval
	mov	x2,GPIO_MSG_BYTES
	cmp	x1,x2
	b.hi	.inval
	mov	x9,x1
	adr	x2,spi_va
	ldr	x2,[x2]
	adr	x3,spi_mode
	ldr	x3,[x3]
	orr	x3,x3,x0
	orr	w4,w3,CS_CLEAR
	orr	w4,w4,CS_TA
	str	w4,[x2,SPI_CS]
	adr	x5,msg + GPIO_MSG_DATA		; reception
	mov	x6,x5				; envoi
	mov	x7,x9				; reste a envoyer
	mov	x8,x9				; reste a recevoir
.l:	ldr	w4,[x2,SPI_CS]
	cbz	x7,.rx
	tbz	w4,18,.rx			; TXD
	ldrb	w10,[x6],1
	str	w10,[x2,SPI_FIFO]
	sub	x7,x7,1
.rx:	tbz	w4,17,.chk			; RXD
	cbz	x8,.chk
	ldr	w10,[x2,SPI_FIFO]
	strb	w10,[x5],1
	sub	x8,x8,1
.chk:	orr	x10,x7,x8
	cbnz	x10,.l
.wd:	ldr	w4,[x2,SPI_CS]
	tbz	w4,16,.wd			; DONE
	str	w3,[x2,SPI_CS]			; TA a 0
	mov	x0,x9
	ret
.perm:	mov	x0,GP_EPERM
	ret
.inval:	mov	x0,GP_EINVAL
	ret

i2c_code
board_code

s_gpio		db 'gpio',0
m_pi5		db 'gpio : Pi 5 (connecteur sur le RP1) pas encore pris',10,0
s_gt911		db 'gt911',0
alt_code	db 4, 5, 6, 7, 3, 2		; GPFSEL de ALT0..ALT5
		align 4

segment readable writeable
kit_data
board_data
i2c_data
		align 8
chan		dd 0
emulated	dd 0
spi_va		dd 0
pwm_va		dd 0
cm_va		dd 0
n_edges		dd 0
last_lev	dd 0
touch_t		dd 0, 0
ev_head		dd 0, 0, 0			; tete, queue, fronts perdus
i2c_owner	dd 0
spi_owner	dd 0
spi_mode	dd 0
		align 16
fw_rev		dw 28, 0, 0x00000001, 4, 0, 0, 0	; GET_FIRMWARE_REVISION
		align 16
pins		rb GPIO_PINS * PIW_SIZE
ev_q		rb EVQ * EV_SIZE
plist		rb 64 * 64
msg		rb 512
msg_rd		rb 512
