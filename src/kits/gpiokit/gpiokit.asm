; ============================================================================
;  kits/gpiokit/gpiokit.asm - GpioKit : le connecteur a 40 broches du Pi
;  -> SD0:/lib/gpiokit.so (docs/KITS.md, docs/BASIC.md ; d'apres le GPIOKit
;  d'Onyx, prefixe gp_ : gk_ est a GpuKit)
;
;  Deux cotes, comme dans Onyx :
;  * le connecteur : chaque appel est une requete au service " gpio " ;
;  * le simulateur : une carte en memoire, a ce process : les entrees mises a
;    la main (gp_sim_input), les ondes du PWM, un bus I2C avec un ecran SSD1306
;    a 0x3C et un capteur BME280 a 0x76 (l'etalonnage de sa fiche technique),
;    SPI rebouclee. Pris d'office sans service ou sous QEMU ; gp_sim le choisit.
;  Variables par process (segment inscriptible du kit).
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
include '../../inc/gpio.inc'
include 'gpiokit.inc'

SP_MODE		= 0			; une broche simulee
SP_OUT		= 1
SP_EDGES	= 2
SP_LAST		= 3
SP_EXT		= 4			; octet signe : -1 rien ne la tient
SP_FREQ		= 8
SP_DUTY		= 12
SP_SIZE		= 16
SIMQ		= 256			; fronts gardes

segment readable executable
include 'gpiokit.abi'

; --- le service ------------------------------------------------------------------
; -> x0 = 1 si le simulateur sert ; premier appel : le service dit s'il y a un connecteur
sim_on:
	adr	x1,g_sim
	ldr	x0,[x1]
	cmp	x0,0
	b.lt	.first
	cmp	x0,1
	cset	x0,eq
	ret
.first:	prologue
	bl	svc_chan
	cbz	x0,.sim				; pas de service
	bl	info_call
	cbnz	x0,.sim
	adr	x0,g_msg
	ldr	x0,[x0,8]
	cbnz	x0,.sim				; QEMU
	adr	x1,g_sim
	str	xzr,[x1]
	mov	x0,0
	epilogue
.sim:	bl	sim_reset
	adr	x1,g_sim
	mov	x0,1
	str	x0,[x1]
	epilogue

; -> x0 = 1 canal ouvert, 0 pas de service
svc_chan:
	adr	x1,g_chan
	ldr	x0,[x1]
	cbz	x0,.open
	mov	x0,1
	ret
.open:	prologue
	adr	x0,s_gpio
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.no
	adr	x1,g_chan
	str	x0,[x1]
	mov	x0,1
	epilogue
.no:	mov	x0,0
	epilogue

; x0 = op, x1 = a, x2 = b, x3 = c -> x0 = resultat
hw:
	prologue
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	bl	svc_chan
	mov	x4,x0
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	cbz	x4,.nodev
	adr	x4,g_msg
	str	x0,[x4]
	str	x1,[x4,8]
	str	x2,[x4,16]
	str	x3,[x4,24]
	adr	x0,g_chan
	ldr	x0,[x0]
	mov	x1,x4
	sys	SYS_SVC_CALL
	tbnz	x0,63,.nodev
	adr	x0,g_msg
	ldr	x0,[x0]
	epilogue
.nodev:	mov	x0,GP_ENODEV
	epilogue

info_call:
	mov	x0,GPIO_OP_INFO
	mov	x1,0
	mov	x2,0
	mov	x3,0
	b	hw

; x0 = source, x1 = destination, x2 = octets
copy:
	cbz	x2,.r
.l:	ldrb	w3,[x0],1
	strb	w3,[x1],1
	subs	x2,x2,1
	b.ne	.l
.r:	ret

; --- l'horloge ----------------------------------------------------------------------
gp_now_us:
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

; --- ce qui pilote les broches --------------------------------------------------------
gp_available:
	prologue
	bl	sim_on
	cbnz	x0,.s
	mov	x0,1
	epilogue
.s:	mov	x0,2
	epilogue

gp_sim:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	sim_on
	cbz	x19,.hw
	mov	x0,1
	adr	x1,g_sim
	str	x0,[x1]
	bl	sim_reset
	mov	x0,2
	b	.out
.hw:	adr	x1,g_sim			; le connecteur, s'il y a un service
	str	xzr,[x1]
	bl	svc_chan
	cbnz	x0,.out
	adr	x1,g_sim
	mov	x0,1
	str	x0,[x1]
	mov	x0,2
.out:	ldp	x19,x20,[sp],16
	epilogue

gp_error:
	adr	x1,s_err0
	cbz	x0,.r
	adr	x1,s_eperm
	cmp	x0,GP_EPERM
	b.eq	.r
	adr	x1,s_eio
	cmp	x0,GP_EIO
	b.eq	.r
	adr	x1,s_enomem
	cmp	x0,GP_ENOMEM
	b.eq	.r
	adr	x1,s_efault
	cmp	x0,GP_EFAULT
	b.eq	.r
	adr	x1,s_ebusy
	cmp	x0,GP_EBUSY
	b.eq	.r
	adr	x1,s_enodev
	cmp	x0,GP_ENODEV
	b.eq	.r
	adr	x1,s_einval
	cmp	x0,GP_EINVAL
	b.eq	.r
	adr	x1,s_failed
	cmp	x0,0
	b.lt	.r
	adr	x1,s_err0
.r:	mov	x0,x1
	ret

gp_release:
	prologue
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_RELEASE
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	hw
	epilogue
.s:	bl	sim_free_all
	mov	x0,0
	epilogue

; --- les broches ----------------------------------------------------------------------
; x0 = broche -> x0 = sa ligne simulee
sim_row:
	adr	x1,g_pins
	add	x0,x1,x0,lsl 4
	ret

; x0 = broche -> x0 = 1 reservee (EEPROM, console)
sim_reserved:
	cmp	x0,1
	b.ls	.y
	cmp	x0,14
	b.eq	.y
	cmp	x0,15
	b.eq	.y
	mov	x0,0
	ret
.y:	mov	x0,1
	ret

; x0 = broche -> x0 = niveau
sim_level:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	sim_row
	mov	x20,x0
	ldrb	w1,[x20,SP_MODE]
	ldrsb	w2,[x20,SP_EXT]
	cmp	w1,GP_M_OUT
	b.ne	.pwm
	ldrb	w0,[x20,SP_OUT]
	b	.out
.pwm:	cmp	w1,GP_M_PWM
	b.ne	.bus
	ldr	w1,[x20,SP_DUTY]
	mov	x0,0
	cbz	w1,.out
	mov	x0,1
	mov	w2,10000
	cmp	w1,w2
	b.hs	.out
	ldr	w1,[x20,SP_FREQ]		; la periode, en us
	mov	w2,1
	cmp	w1,0
	csel	w1,w2,w1,eq
	movl	x2,1000000
	udiv	x2,x2,x1
	mov	x1,1
	cmp	x2,0
	csel	x2,x1,x2,eq
	stp	x2,xzr,[sp,-16]!
	bl	gp_now_us
	ldp	x2,xzr,[sp],16
	udiv	x1,x0,x2
	msub	x1,x1,x2,x0			; x1 = temps dans la periode
	ldr	w3,[x20,SP_DUTY]
	mul	x3,x3,x2
	mov	x4,10000
	udiv	x3,x3,x4
	cmp	x1,x3
	cset	x0,lo
	b	.out
.bus:	cmp	w1,GP_M_I2C
	b.eq	.one
	cmp	w1,GP_M_SPI
	b.eq	.one
	cmp	w1,GP_M_PULLUP
	b.ne	.pd
	tbz	w2,31,.ext
.one:	mov	x0,1
	b	.out
.pd:	cmp	w1,GP_M_PULLDOWN
	b.ne	.def
	tbz	w2,31,.ext
	mov	x0,0
	b	.out
.def:	mov	x0,x19				; (la ligne au repos de l'UART, de l'EEPROM)
	bl	sim_reserved
	cbnz	x0,.one
	ldrsb	w2,[x20,SP_EXT]
	tbz	w2,31,.ext
	mov	x0,0
	b	.out
.ext:	sxtw	x0,w2
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = broche : un front si son niveau a change
sim_edge_check:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	bl	sim_row
	mov	x20,x0
	mov	x0,x19
	bl	sim_level
	mov	x21,x0
	ldrb	w1,[x20,SP_LAST]
	cmp	x21,x1
	b.eq	.out
	strb	w21,[x20,SP_LAST]
	mov	x2,GP_FALLING
	cbz	x21,.e
	mov	x2,GP_RISING
.e:	ldrb	w1,[x20,SP_EDGES]
	tst	w1,w2
	b.eq	.out
	adr	x3,g_qhead
	ldp	x4,x5,[x3]			; tete, queue
	add	x6,x4,1
	and	x6,x6,SIMQ - 1
	cmp	x6,x5
	b.ne	.put
	mov	x4,1
	str	x4,[x3,16]			; perdus
	b	.out
.put:	adr	x7,g_q
	add	x7,x7,x4,lsl 4
	strb	w19,[x7,GPE_PIN]
	strb	w2,[x7,GPE_EDGE]
	strb	w21,[x7,GPE_LEVEL]
	ldr	x8,[x3,16]
	strb	w8,[x7,GPE_LOST]
	str	wzr,[x7,4]
	str	xzr,[x3,16]
	str	x6,[x3]
	mov	x19,x7
	bl	gp_now_us
	str	x0,[x19,GPE_US]
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = broche : libre
sim_free:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	sim_row
	mov	x20,x0
	strb	wzr,[x20,SP_MODE]
	strb	wzr,[x20,SP_OUT]
	strb	wzr,[x20,SP_EDGES]
	str	wzr,[x20,SP_FREQ]
	str	wzr,[x20,SP_DUTY]
	mov	x0,x19
	bl	sim_level
	strb	w0,[x20,SP_LAST]
	ldp	x19,x20,[sp],16
	epilogue

sim_free_all:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
.l:	mov	x0,x19
	bl	sim_free
	add	x19,x19,1
	cmp	x19,GP_PINS
	b.lo	.l
	adr	x0,g_i2c
	str	xzr,[x0]
	str	xzr,[x0,8]
	ldr	x19,[sp],16
	epilogue

; la carte a l'allumage : tout libre, rien ne tient les entrees
sim_reset:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
.l:	mov	x0,x19
	bl	sim_row
	mov	w1,-1
	strb	w1,[x0,SP_EXT]
	add	x19,x19,1
	cmp	x19,GP_PINS
	b.lo	.l
	bl	sim_free_all
	adr	x0,g_qhead
	stp	xzr,xzr,[x0]
	str	xzr,[x0,16]
	adr	x0,g_col_hi
	mov	x1,127
	str	x1,[x0]
	mov	x1,7
	str	x1,[x0,8]
	ldr	x19,[sp],16
	epilogue

gp_mode:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_MODE
	mov	x1,x19
	mov	x2,x20
	mov	x3,0
	bl	hw
	b	.out
.s:	cmp	x19,GP_PINS
	b.hs	.inval
	cmp	x20,GP_M_ALT0 + 5
	b.hi	.inval
	mov	x0,x19
	bl	sim_reserved
	cbnz	x0,.perm
	cmp	x20,GP_M_PWM
	b.eq	.inval
	cmp	x20,GP_M_I2C
	b.eq	.inval
	cmp	x20,GP_M_SPI
	b.eq	.inval
	mov	x0,x19
	bl	sim_free
	mov	x0,x19
	bl	sim_row
	strb	w20,[x0,SP_MODE]
	mov	x0,x19
	bl	sim_edge_check
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
	b	.out
.perm:	mov	x0,GP_EPERM
.out:	ldp	x19,x20,[sp],16
	epilogue

gp_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_WRITE
	mov	x1,x19
	mov	x2,x20
	mov	x3,0
	bl	hw
	b	.out
.s:	cmp	x19,GP_PINS
	b.hs	.inval
	mov	x0,x19
	bl	sim_row
	ldrb	w1,[x0,SP_MODE]
	cmp	w1,GP_M_OUT
	b.ne	.inval
	cmp	x20,0
	cset	w1,ne
	strb	w1,[x0,SP_OUT]
	mov	x0,x19
	bl	sim_edge_check
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

gp_read:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_READ
	mov	x1,x19
	mov	x2,0
	mov	x3,0
	bl	hw
	b	.out
.s:	cmp	x19,GP_PINS
	b.hs	.inval
	mov	x0,x19
	bl	sim_level
	b	.out
.inval:	mov	x0,GP_EINVAL
.out:	ldr	x19,[sp],16
	epilogue

gp_toggle:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	gp_read
	tbnz	x0,63,.out
	eor	x1,x0,1
	mov	x0,x19
	str	x1,[sp,8]
	bl	gp_write
	tbnz	x0,63,.out
	ldr	x0,[sp,8]
.out:	ldr	x19,[sp],16
	epilogue

gp_read_all:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_READALL
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	hw
	b	.out
.s:	mov	x19,0
	mov	x20,0
.l:	mov	x0,x19
	bl	sim_level
	lsl	x0,x0,x19
	orr	x20,x20,x0
	add	x19,x19,1
	cmp	x19,GP_PINS
	b.lo	.l
	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = sortie, x1 = places
gp_info:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	cmp	x1,GP_PINS
	mov	x2,GP_PINS
	csel	x20,x1,x2,lo
	bl	sim_on
	mov	x22,x0
	cbnz	x22,.go
	bl	info_call
	tbnz	x0,63,.out
.go:	mov	x21,0
.l:	cmp	x21,x20
	b.hs	.done
	mov	x0,x19				; a zero
	mov	x1,GPI_SIZE
.z:	strb	wzr,[x0],1
	subs	x1,x1,1
	b.ne	.z
	strb	w21,[x19,GPI_PIN]
	cbnz	x22,.sim
	adr	x0,g_msg + GPIO_MSG_DATA
	add	x0,x0,x21,lsl 4
	ldrb	w1,[x0,PIW_MODE]
	strb	w1,[x19,GPI_MODE]
	ldrb	w1,[x0,PIW_LEVEL]
	strb	w1,[x19,GPI_LEVEL]
	ldrb	w1,[x0,PIW_FLAGS]
	strb	w1,[x19,GPI_FLAGS]
	ldr	w1,[x0,PIW_OWNER]
	str	w1,[x19,GPI_OWNER]
	ldr	w1,[x0,PIW_FREQ]
	str	w1,[x19,GPI_FREQ]
	ldr	w1,[x0,PIW_DUTY]
	str	w1,[x19,GPI_DUTY]
	ldrb	w1,[x0,PIW_REASON]
	b	.reason
.sim:	mov	x0,x21
	bl	sim_row
	ldrb	w1,[x0,SP_MODE]
	strb	w1,[x19,GPI_MODE]
	ldr	w1,[x0,SP_FREQ]
	str	w1,[x19,GPI_FREQ]
	ldr	w1,[x0,SP_DUTY]
	str	w1,[x19,GPI_DUTY]
	ldrb	w2,[x0,SP_EDGES]
	mov	w3,0
	cbz	w2,.nf
	mov	w3,GP_F_EDGES
.nf:	ldrb	w1,[x0,SP_MODE]
	mov	x0,x21
	bl	gp_is_pwm
	cbz	x0,.np
	orr	w3,w3,GP_F_PWM
.np:	strb	w3,[x19,GPI_FLAGS]
	mov	x0,x21
	bl	sim_level
	strb	w0,[x19,GPI_LEVEL]
	mov	x0,x21
	bl	sim_reserved
	mov	x1,0
	cbz	x0,.reason
	ldrb	w2,[x19,GPI_FLAGS]
	orr	w2,w2,GP_F_RESERVED
	strb	w2,[x19,GPI_FLAGS]
	mov	x1,1
	cmp	x21,1
	b.ls	.reason
	mov	x1,2
.reason:cbz	x1,.next
	adr	x0,s_r1
	cmp	x1,1
	b.eq	.cp
	adr	x0,s_r2
	cmp	x1,2
	b.eq	.cp
	adr	x0,s_r3
.cp:	add	x1,x19,GPI_REASON
	mov	x2,23
	bl	copy
.next:	add	x19,x19,GPI_SIZE
	add	x21,x21,1
	b	.l
.done:	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = broche -> x0 = 1 si elle peut faire du PWM (garde x1..x3)
gp_is_pwm:
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

gp_mode_name:
	cmp	x0,GP_M_ALT0 + 5
	b.ls	.ok
	mov	x0,GP_M_ALT0 + 6
.ok:	adr	x1,mode_tab
	ldrh	w2,[x1,x0,lsl 1]
	add	x0,x1,x2
	ret

; --- PWM ---------------------------------------------------------------------------------
gp_pwm:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_PWM
	mov	x1,x19
	mov	x2,x20
	mov	x3,x21
	bl	hw
	b	.out
.s:	mov	x0,x19
	bl	gp_is_pwm
	cbz	x0,.inval
	cbz	x20,.inval
	movl	x1,1000000
	cmp	x20,x1
	b.hi	.inval
	mov	x1,10000
	cmp	x21,x1
	b.hi	.inval
	cmp	x19,12				; l'autre broche du canal
	mov	x0,18
	b.eq	.oth
	cmp	x19,18
	mov	x0,12
	b.eq	.oth
	cmp	x19,13
	mov	x0,19
	b.eq	.oth
	mov	x0,13
.oth:	bl	sim_row
	ldrb	w1,[x0,SP_MODE]
	cmp	w1,GP_M_PWM
	b.eq	.busy
	mov	x0,x19
	bl	sim_row
	ldrb	w1,[x0,SP_MODE]
	cmp	w1,GP_M_PWM
	b.eq	.set
	mov	x0,x19
	bl	sim_free
.set:	mov	x0,x19
	bl	sim_row
	mov	w1,GP_M_PWM
	strb	w1,[x0,SP_MODE]
	str	w20,[x0,SP_FREQ]
	str	w21,[x0,SP_DUTY]
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
	b	.out
.busy:	mov	x0,GP_EBUSY
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

gp_servo:
	mov	x2,500				; 50 Hz : 20 000 us
	cmp	x1,x2
	csel	x1,x2,x1,lo
	mov	x2,2500
	cmp	x1,x2
	csel	x1,x2,x1,hi
	lsr	x2,x1,1				; (us / 20000 en 1/10000)
	mov	x1,50
	b	gp_pwm

; --- les fronts ----------------------------------------------------------------------------
gp_edges:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_EDGES
	mov	x1,x19
	mov	x2,x20
	mov	x3,0
	bl	hw
	b	.out
.s:	cmp	x19,GP_PINS
	b.hs	.inval
	cmp	x20,GP_BOTH
	b.hi	.inval
	mov	x0,x19
	bl	sim_row
	strb	w20,[x0,SP_EDGES]
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = sortie, x1 = places, x2 = attente ms
gp_events:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,1000
	cmp	x21,x0
	csel	x21,x0,x21,hi
	sys	SYS_TIME_MS
	add	x21,x21,x0			; la fin de l'attente
.again:	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_EVENTS
	mov	x1,x20
	mov	x2,0
	mov	x3,0
	bl	hw
	tbnz	x0,63,.out
	cbz	x0,.wait
	mov	x22,x0
	adr	x0,g_msg + GPIO_MSG_DATA
	mov	x1,x19
	lsl	x2,x22,4
	bl	copy
	mov	x0,x22
	b	.out
.s:	adr	x3,g_qhead
	ldp	x4,x5,[x3]
	mov	x0,0
.l:	cmp	x5,x4
	b.eq	.sd
	cmp	x0,x20
	b.hs	.sd
	adr	x7,g_q
	add	x7,x7,x5,lsl 4
	ldp	x8,x9,[x7]
	stp	x8,x9,[x19],GPE_SIZE
	add	x5,x5,1
	and	x5,x5,SIMQ - 1
	add	x0,x0,1
	b	.l
.sd:	str	x5,[x3,8]
	cbnz	x0,.out
.wait:	sys	SYS_TIME_MS
	cmp	x0,x21
	b.hs	.none
	mov	x0,2
	sys	SYS_SLEEP
	b	.again
.none:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- I2C -------------------------------------------------------------------------------------
gp_i2c_open:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_I2C_OPEN
	mov	x1,x19
	mov	x2,0
	mov	x3,0
	bl	hw
	b	.out
.s:	adr	x0,g_i2c
	mov	x1,1
	str	x1,[x0]
	mov	x0,2
	bl	sim_row
	mov	w1,GP_M_I2C
	strb	w1,[x0,SP_MODE]
	strb	w1,[x0,SP_SIZE + SP_MODE]
	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

gp_i2c_close:
	prologue
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_I2C_CLOSE
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	hw
	epilogue
.s:	adr	x0,g_i2c
	str	xzr,[x0]
	mov	x0,2
	bl	sim_free
	mov	x0,3
	bl	sim_free
	mov	x0,0
	epilogue

; x0 = adresse, x1 = ecrit, x2 = octets, x3 = lu, x4 = octets -> octets lus (rien a lire : ecrits) / GP_E*
gp_i2c_write_read:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	bl	sim_on
	cbnz	x0,.s
	mov	x2,GPIO_MSG_BYTES
	cmp	x21,x2
	b.hi	.inval
	cmp	x23,x2
	b.hi	.inval
	mov	x0,x20
	adr	x1,g_msg + GPIO_MSG_DATA
	mov	x2,x21
	bl	copy
	mov	x0,GPIO_OP_I2C_XFER
	mov	x1,x19
	mov	x2,x21
	mov	x3,x23
	bl	hw
	tbnz	x0,63,.out
	cbz	x23,.out
	mov	x20,x0
	adr	x0,g_msg + GPIO_MSG_DATA
	mov	x1,x22
	mov	x2,x23
	bl	copy
	mov	x0,x20
	b	.out
.s:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	x4,x23
	bl	sim_i2c
	b	.out
.inval:	mov	x0,GP_EINVAL
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

gp_i2c_write:
	mov	x3,0
	mov	x4,0
	b	gp_i2c_write_read

gp_i2c_read:
	mov	x3,x1
	mov	x4,x2
	mov	x1,0
	mov	x2,0
	b	gp_i2c_write_read

gp_i2c_reg_read:
	prologue
	sub	sp,sp,16
	strb	w1,[sp]
	mov	x1,sp
	mov	x2,1
	add	x3,sp,1
	mov	x4,1
	bl	gp_i2c_write_read
	tbnz	x0,63,.out
	ldrb	w0,[sp,1]
.out:	add	sp,sp,16
	epilogue

gp_i2c_reg_write:
	prologue
	sub	sp,sp,16
	strb	w1,[sp]
	strb	w2,[sp,1]
	mov	x1,sp
	mov	x2,2
	bl	gp_i2c_write
	tbnz	x0,63,.out
	mov	x0,0
.out:	add	sp,sp,16
	epilogue

; x0 = 16 o : chaque adresse 0x08..0x77 essayee par une lecture d'un octet
gp_i2c_scan:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,xzr,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	stp	xzr,xzr,[x19]
	mov	x20,0x08
	mov	x21,0
.l:	mov	x0,x20
	mov	x1,sp
	mov	x2,1
	bl	gp_i2c_read
	cmp	x0,GP_EPERM
	b.eq	.out
	cmp	x0,GP_ENODEV
	b.eq	.out
	tbnz	x0,63,.n
	lsr	x1,x20,3
	and	x2,x20,7
	mov	w3,1
	lsl	w3,w3,w2
	ldrb	w4,[x19,x1]
	orr	w4,w4,w3
	strb	w4,[x19,x1]
	add	x21,x21,1
.n:	add	x20,x20,1
	cmp	x20,0x78
	b.lo	.l
	mov	x0,x21
.out:	add	sp,sp,16
	ldp	x21,xzr,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

gp_i2c_guess:
	adr	x1,g_port
	cmp	x0,0x20
	b.lo	.none
	cmp	x0,0x27
	b.ls	.r
	adr	x1,g_oled
	cmp	x0,0x3C
	b.eq	.r
	cmp	x0,0x3D
	b.eq	.r
	adr	x1,g_ina
	cmp	x0,0x40
	b.eq	.r
	adr	x1,g_adc
	cmp	x0,0x48
	b.lo	.none
	cmp	x0,0x4B
	b.ls	.r
	adr	x1,g_eep
	cmp	x0,0x50
	b.lo	.none
	cmp	x0,0x57
	b.ls	.r
	adr	x1,g_am
	cmp	x0,0x5C
	b.eq	.r
	adr	x1,g_rtc
	cmp	x0,0x68
	b.eq	.r
	adr	x1,g_mpu
	cmp	x0,0x69
	b.eq	.r
	adr	x1,g_bme
	cmp	x0,0x76
	b.eq	.r
	cmp	x0,0x77
	b.eq	.r
.none:	adr	x1,s_none
.r:	mov	x0,x1
	ret

; --- l'ecran et le capteur simules ----------------------------------------------------------
; w0 = octet de donnees
oled_data:
	adr	x1,g_col
	ldp	x2,x3,[x1]			; colonne, page
	cmp	x3,8
	b.hs	.adv
	cmp	x2,128
	b.hs	.adv
	lsl	x4,x3,7
	add	x4,x4,x2
	adr	x5,g_oled_mem
	strb	w0,[x5,x4]
.adv:	add	x2,x2,1
	adr	x6,g_col_hi
	ldr	x4,[x6]
	cmp	x2,x4
	b.ls	.st
	ldr	x2,[x6,16]			; colonne basse
	add	x3,x3,1
	ldr	x4,[x6,8]
	cmp	x3,x4
	b.ls	.st
	ldr	x3,[x6,24]			; page basse
.st:	stp	x2,x3,[x1]
	ret

; w0 = octet de commande
oled_cmd:
	adr	x1,g_col
	adr	x6,g_col_hi
	adr	x7,g_ocmd
	ldp	x2,x3,[x7]			; commande, arguments attendus
	cbz	x3,.new
	cmp	x2,0x21
	b.ne	.p22
	cmp	x3,2
	b.ne	.c2
	str	x0,[x7,16]
	b	.dec
.c2:	ldr	x4,[x7,16]
	and	x4,x4,127
	str	x4,[x6,16]
	and	x5,x0,127
	str	x5,[x6]
	str	x4,[x1]
	b	.dec
.p22:	cmp	x2,0x22
	b.ne	.dec
	cmp	x3,2
	b.ne	.p2
	str	x0,[x7,16]
	b	.dec
.p2:	ldr	x4,[x7,16]
	and	x4,x4,7
	str	x4,[x6,24]
	and	x5,x0,7
	str	x5,[x6,8]
	str	x4,[x1,8]
.dec:	sub	x3,x3,1
	str	x3,[x7,8]
	ret
.new:	str	x0,[x7]
	cmp	x0,0x21
	b.eq	.two
	cmp	x0,0x22
	b.eq	.two
	adr	x4,oled_one			; les commandes a un argument
.o:	ldrb	w5,[x4],1
	cbz	w5,.more
	cmp	w5,w0
	b.ne	.o
	mov	x3,1
	str	x3,[x7,8]
	ret
.two:	mov	x3,2
	str	x3,[x7,8]
	ret
.more:	sub	x4,x0,0xB0
	cmp	x4,7
	b.hi	.lo
	str	x4,[x1,8]
	ret
.lo:	cmp	x0,0x0F
	b.hi	.hi
	ldr	x4,[x1]
	and	x4,x4,0xF0
	orr	x4,x4,x0
	str	x4,[x1]
	ret
.hi:	sub	x4,x0,0x10
	cmp	x4,15
	b.hi	.r
	ldr	x5,[x1]
	and	x5,x5,0x0F
	orr	x5,x5,x4,lsl 4
	str	x5,[x1]
.r:	ret

; les registres du capteur (une fois)
bme_init:
	adr	x0,g_bme_reg
	ldrb	w1,[x0,0xD0]
	cmp	w1,0x60
	b.eq	.r
	mov	w1,0x60
	strb	w1,[x0,0xD0]
	adr	x1,bme_cal
	add	x2,x0,0x88
	mov	x3,24				; T1..3, P1..9 : 16 bits, poids faible d'abord
.c:	ldrb	w4,[x1],1
	strb	w4,[x2],1
	subs	x3,x3,1
	b.ne	.c
	mov	w4,75
	strb	w4,[x0,0xA1]			; H1
	add	x2,x0,0xE1
	mov	x3,7
.h:	ldrb	w4,[x1],1
	strb	w4,[x2],1
	subs	x3,x3,1
	b.ne	.h
.r:	ret

; environ 25 degres, 1006,5 hPa, 45 % : la temperature derive un peu avec le temps
bme_sample:
	prologue
	bl	gp_now_us
	movl	x1,1000000
	udiv	x0,x0,x1
	mov	x1,40
	udiv	x2,x0,x1
	msub	x2,x2,x1,x0			; s % 40
	cmp	x2,20
	b.hs	.down
	mov	x3,x2				; (s % 20) x 80
	b	.m
.down:	mov	x3,40
	sub	x3,x3,x2
.m:	mov	x4,80
	mul	x3,x3,x4
	movl	x4,519888
	add	x3,x3,x4			; t
	movl	x4,415148			; p
	adr	x0,g_bme_reg
	lsr	x5,x4,12
	strb	w5,[x0,0xF7]
	lsr	x5,x4,4
	strb	w5,[x0,0xF8]
	and	x5,x4,15
	lsl	x5,x5,4
	strb	w5,[x0,0xF9]
	lsr	x5,x3,12
	strb	w5,[x0,0xFA]
	lsr	x5,x3,4
	strb	w5,[x0,0xFB]
	and	x5,x3,15
	lsl	x5,x5,4
	strb	w5,[x0,0xFC]
	mov	x5,27500 shr 8			; h
	strb	w5,[x0,0xFD]
	mov	x5,27500 and 0xFF
	strb	w5,[x0,0xFE]
	epilogue

; x0 = adresse, x1 = ecrit, x2 = octets, x3 = lu, x4 = octets -> octets / GP_EIO
sim_i2c:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	mov	x22,x4
	cmp	x0,0x3C
	b.eq	.oled
	cmp	x0,0x76
	b.eq	.bme
	mov	x0,GP_EIO
	b	.out
.oled:	cbz	x22,.ow
	mov	x1,x21				; (son etat : 0)
	mov	x2,x22
.z:	strb	wzr,[x1],1
	subs	x2,x2,1
	b.ne	.z
	mov	x0,x22
	b	.out
.ow:	mov	x0,0
	cbz	x20,.out
	ldrb	w21,[x19]			; l'octet de controle : donnees ou commandes
	mov	x22,1
.ol:	cmp	x22,x20
	b.hs	.od
	ldrb	w0,[x19,x22]
	tbz	w21,6,.ocmd
	bl	oled_data
	b	.onx
.ocmd:	bl	oled_cmd
.onx:	add	x22,x22,1
	b	.ol
.od:	mov	x0,x20
	b	.out
.bme:	bl	bme_init
	cbz	x20,.brd
	ldrb	w0,[x19]
	adr	x1,g_bme_cur
	str	x0,[x1]
	adr	x2,g_bme_reg
	mov	x3,1
.bw:	cmp	x3,x20
	b.hs	.brd
	ldrb	w4,[x19,x3]
	add	x5,x0,x3
	sub	x5,x5,1
	and	x5,x5,255
	strb	w4,[x2,x5]
	add	x3,x3,1
	b	.bw
.brd:	cbz	x22,.bdone
	bl	bme_sample
	adr	x1,g_bme_cur
	ldr	x0,[x1]
	adr	x2,g_bme_reg
	mov	x3,0
.br:	add	x4,x0,x3
	and	x4,x4,255
	ldrb	w5,[x2,x4]
	strb	w5,[x21,x3]
	add	x3,x3,1
	cmp	x3,x22
	b.lo	.br
	mov	x0,x22
	b	.out
.bdone:	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SPI ---------------------------------------------------------------------------------------
gp_spi_open:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_SPI_OPEN
	mov	x1,x19
	mov	x2,x20
	mov	x3,0
	bl	hw
	b	.out
.s:	cmp	x20,3
	b.hi	.inval
	adr	x0,g_i2c
	mov	x1,1
	str	x1,[x0,8]
	mov	x19,7
.l:	mov	x0,x19
	bl	sim_row
	mov	w1,GP_M_SPI
	strb	w1,[x0,SP_MODE]
	add	x19,x19,1
	cmp	x19,12
	b.lo	.l
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

gp_spi_close:
	prologue
	str	x19,[sp,-16]!
	bl	sim_on
	cbnz	x0,.s
	mov	x0,GPIO_OP_SPI_CLOSE
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	hw
	b	.out
.s:	adr	x0,g_i2c
	str	xzr,[x0,8]
	mov	x19,7
.l:	mov	x0,x19
	bl	sim_free
	add	x19,x19,1
	cmp	x19,12
	b.lo	.l
	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = CE, x1 = envoi, x2 = reception, x3 = octets : par morceaux au service ; simule : rebouclee
gp_spi_transfer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x24,x3
	cmp	x19,1
	b.hi	.inval
	bl	sim_on
	cbnz	x0,.s
.part:	cbz	x22,.done
	mov	x23,GPIO_MSG_BYTES
	cmp	x22,x23
	csel	x23,x22,x23,lo
	adr	x1,g_msg + GPIO_MSG_DATA
	cbnz	x20,.tx
	mov	x2,x23
	mov	x3,x1
.zz:	strb	wzr,[x3],1
	subs	x2,x2,1
	b.ne	.zz
	b	.call
.tx:	mov	x0,x20
	mov	x2,x23
	bl	copy
	add	x20,x20,x23
.call:	mov	x0,GPIO_OP_SPI_XFER
	mov	x1,x19
	mov	x2,x23
	mov	x3,0
	bl	hw
	tbnz	x0,63,.out
	cbz	x21,.nrx
	adr	x0,g_msg + GPIO_MSG_DATA
	mov	x1,x21
	mov	x2,x23
	bl	copy
	add	x21,x21,x23
.nrx:	sub	x22,x22,x23
	b	.part
.s:	adr	x0,g_i2c
	ldr	x0,[x0,8]
	cbz	x0,.perm
	mov	x3,0
.sl:	cmp	x3,x22
	b.hs	.done
	mov	w4,0
	cbz	x20,.sb
	ldrb	w4,[x20,x3]
.sb:	cbz	x21,.sn
	strb	w4,[x21,x3]
.sn:	add	x3,x3,1
	b	.sl
.done:	mov	x0,x24
	b	.out
.inval:	mov	x0,GP_EINVAL
	b	.out
.perm:	mov	x0,GP_EPERM
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- le connecteur tel qu'il est imprime sur la carte ------------------------------------------
gp_header_gpio:
	sub	x1,x0,1
	cmp	x1,39
	b.hi	.no
	adr	x1,header
	ldrsb	x0,[x1,x0]
	tbz	x0,63,.r
.no:	mov	x0,-1
.r:	ret

gp_header_pin:
	adr	x1,header
	mov	x2,1
.l:	ldrsb	x3,[x1,x2]
	cmp	x3,x0
	b.eq	.y
	add	x2,x2,1
	cmp	x2,40
	b.ls	.l
	mov	x0,-1
	ret
.y:	mov	x0,x2
	ret

; (x0 = broche du connecteur) ; la table des textes garde un tampon par process
gp_header_label:
	sub	x1,x0,1
	cmp	x1,39
	b.hi	.none
	adr	x1,header
	ldrsb	x2,[x1,x0]
	tbnz	x2,63,.pwr
	adr	x3,g_label			; « GPIOn »
	mov	x4,x3
	mov	w5,'G'
	strb	w5,[x4],1
	mov	w5,'P'
	strb	w5,[x4],1
	mov	w5,'I'
	strb	w5,[x4],1
	mov	w5,'O'
	strb	w5,[x4],1
	cmp	x2,10
	b.lo	.d
	mov	x5,10
	udiv	x6,x2,x5
	msub	x2,x6,x5,x2
	add	w6,w6,'0'
	strb	w6,[x4],1
.d:	add	w2,w2,'0'
	strb	w2,[x4],1
	strb	wzr,[x4]
	mov	x0,x3
	ret
.pwr:	cmp	x2,-2
	adr	x1,s_gnd
	b.eq	.t
	cmp	x0,1
	adr	x1,s_3v3
	b.eq	.t
	cmp	x0,17
	b.eq	.t
	adr	x1,s_5v
.t:	mov	x0,x1
	ret
.none:	adr	x0,s_none
	ret

gp_gpio_function:
	cmp	x0,19
	b.hi	.none
	adr	x1,func_tab
	ldrh	w2,[x1,x0,lsl 1]
	add	x0,x1,x2
	ret
.none:	adr	x0,s_none
	ret

; --- le simulateur vu du programme ------------------------------------------------------------
gp_sim_input:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x19,GP_PINS
	b.hs	.inval
	bl	sim_on				; (a son premier appel : la carte remise a zero)
	mov	x0,x19
	bl	sim_row
	mov	w1,-1
	tbnz	x20,63,.st
	cmp	x20,0
	cset	w1,ne
.st:	strb	w1,[x0,SP_EXT]
	mov	x0,x19
	bl	sim_edge_check
	mov	x0,0
	b	.out
.inval:	mov	x0,GP_EINVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

gp_sim_display:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	sim_on
	cbz	x0,.out
	adr	x0,g_oled_mem
	mov	x1,x19
	mov	x2,1024
	bl	copy
	mov	x0,1
.out:	ldr	x19,[sp],16
	epilogue

; --- les textes -------------------------------------------------------------------------------
s_gpio		db 'gpio',0
s_none		db 0
s_err0		db 'done',0
s_eperm		db 'the system uses this pin, or it is not this program''s',0
s_eio		db 'no answer on the bus',0
s_enomem	db 'out of memory',0
s_efault	db 'a bad buffer',0
s_ebusy		db 'another program has it',0
s_enodev	db 'no GPIO on this system (use the simulator)',0
s_einval	db 'not possible on this pin, or a value out of range',0
s_failed	db 'failed',0
s_r1		db 'HAT ID EEPROM',0
s_r2		db 'serial console',0
s_r3		db 'touch screen',0
s_gnd		db 'GND',0
s_3v3		db '3V3',0
s_5v		db '5V',0
g_port		db 'MCP23017 / PCF8574 port expander',0
g_oled		db 'SSD1306 / SH1106 OLED display',0
g_ina		db 'INA219 / HTU21D / PCA9685',0
g_adc		db 'ADS1115 ADC / TMP102',0
g_eep		db 'AT24C EEPROM',0
g_am		db 'AM2320 / BH1750',0
g_rtc		db 'DS3231 / DS1307 clock, MPU-6050',0
g_mpu		db 'MPU-6050 (AD0 high)',0
g_bme		db 'BME280 / BMP280 sensor',0
oled_one	db 0x20, 0x81, 0x8D, 0xA8, 0xD3, 0xD5, 0xD9, 0xDA, 0xDB, 0
header		db 0
		db -1, -1,  2, -1,  3, -2,  4, 14, -2, 15
		db 17, 18, 27, -2, 22, 23, -1, 24, 10, -2
		db  9, 25, 11,  8, -2,  7,  0,  1,  5, -2
		db  6, 12, 13, -2, 19, 16, 26, 20, -2, 21
		align 2
bme_cal		dh 27504, 26435, -1000			; T1..3 (fiche BST-BME280-DS002)
		dh 36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000	; P1..9
		db 362 and 0xFF, 362 shr 8, 0		; H2, H3
		db 313 shr 4, (313 and 15) or ((50 and 15) shl 4), 50 shr 4	; H4, H5
		db 30					; H6
		align 2
mode_tab	dh m0 - mode_tab, m1 - mode_tab, m2 - mode_tab, m3 - mode_tab, m4 - mode_tab
		dh m5 - mode_tab, m6 - mode_tab, m7 - mode_tab, m8 - mode_tab, m9 - mode_tab
		dh m10 - mode_tab, m11 - mode_tab, m12 - mode_tab, m13 - mode_tab, m14 - mode_tab
m0		db 'free',0
m1		db 'input',0
m2		db 'pull-up',0
m3		db 'pull-down',0
m4		db 'output',0
m5		db 'PWM',0
m6		db 'I2C',0
m7		db 'SPI',0
m8		db 'ALT0',0
m9		db 'ALT1',0
m10		db 'ALT2',0
m11		db 'ALT3',0
m12		db 'ALT4',0
m13		db 'ALT5',0
m14		db '?',0
		align 2
func_tab	dh f_0 - func_tab, f_1 - func_tab, f_2 - func_tab, f_3 - func_tab, f_4 - func_tab
		dh f_n - func_tab, f_n - func_tab, f_7 - func_tab, f_8 - func_tab, f_9 - func_tab
		dh f_10 - func_tab, f_11 - func_tab, f_12 - func_tab, f_13 - func_tab, f_14 - func_tab
		dh f_15 - func_tab, f_n - func_tab, f_n - func_tab, f_12 - func_tab, f_13 - func_tab
f_n		db 0
f_0		db 'ID_SD',0
f_1		db 'ID_SC',0
f_2		db 'SDA1',0
f_3		db 'SCL1',0
f_4		db 'GPCLK0',0
f_7		db 'SPI0 CE1',0
f_8		db 'SPI0 CE0',0
f_9		db 'SPI0 MISO',0
f_10		db 'SPI0 MOSI',0
f_11		db 'SPI0 SCLK',0
f_12		db 'PWM0',0
f_13		db 'PWM1',0
f_14		db 'TXD0',0
f_15		db 'RXD0',0
		align 4

segment readable writeable
		align 8
g_sim		dd -1			; -1 pas encore decide, 0 le connecteur, 1 le simulateur
g_chan		dd 0
g_qhead		dd 0, 0, 0		; tete, queue, fronts perdus
g_i2c		dd 0, 0			; bus ouverts (simulateur) : I2C, SPI
g_col		dd 0, 0			; colonne, page du SSD1306
g_col_hi	dd 127, 7, 0, 0		; colonne haute, page haute, colonne basse, page basse
g_ocmd		dd 0, 0, 0		; sa commande, arguments attendus, premier argument
g_bme_cur	dd 0
g_label		rb 16
g_pins		rb GP_PINS * SP_SIZE
g_q		rb SIMQ * GPE_SIZE
g_oled_mem	rb 1024
g_bme_reg	rb 256
g_msg		rb 512
