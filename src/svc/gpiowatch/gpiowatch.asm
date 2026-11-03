; ============================================================================
;  gpiowatch - fronts de chaque broche GPIO, une ligne par seconde (40 s), en
;  lisant GPLEV en boucle. Sert a trouver une broche inconnue : celle qui bouge
;  quand on agit sur le materiel (la broche INT du GT911, overlay de Waveshare :
;  GPIO 4, confirme). Dans SD0:/sys/svc pour le droit dev_map ; ne change aucune
;  broche, mais occupe un coeur (le pointeur peut saccader). Lance depuis telnet,
;  la sortie part dans le journal du noyau (SD0:/bin/journal, puis journal.txt).
;    gpio4 : fonction N		(GPFSEL : 0 entree, 1 sortie, 2.. alternative)
;    t=3 gpio4=1 2:5120 3:4800 4:12	(seconde, niveau, broche:fronts)
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

GPIO_PHYS	= 0xFE200000		; BCM2711 (Pi 4)
GP_FSEL0	= 0x00
GP_LEV0		= 0x34
GP_LEV1		= 0x38
WATCH_S		= 40
PIN_N		= 58

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	bl	kits_open
	movl	x0,GPIO_PHYS
	mov	x1,4096
	sys	SYS_DEV_MAP
	tbnz	x0,63,.ko
	mov	x20,x0
	mrs	x19,cntfrq_el0
	adr	x1,line				; la fonction de GPIO 4
	adr	x0,m_fsel
	bl	put_str
	ldr	w0,[x20,GP_FSEL0]
	ubfx	w0,w0,12,3
	bl	put_dec
	bl	line_out
	mov	x21,0				; seconde
.sec:	adr	x0,counts
	mov	x1,PIN_N
.zero:	str	wzr,[x0],4
	subs	x1,x1,1
	b.ne	.zero
	mrs	x22,cntpct_el0
	add	x22,x22,x19
	ldr	w23,[x20,GP_LEV0]
	ldr	w24,[x20,GP_LEV1]
.loop:	ldr	w25,[x20,GP_LEV0]
	ldr	w26,[x20,GP_LEV1]
	eor	w0,w25,w23
	eor	w2,w26,w24
	orr	w3,w0,w2
	cbz	w3,.same
	mov	w23,w25
	mov	w24,w26
	adr	x1,counts
	bl	count_bits
	mov	w0,w2
	adr	x1,counts+32*4
	bl	count_bits
.same:	mrs	x0,cntpct_el0
	cmp	x0,x22
	b.lo	.loop
	mov	x0,x21
	ubfx	w2,w23,4,1
	bl	report
	add	x21,x21,1
	cmp	x21,WATCH_S
	b.lo	.sec
	mov	x0,0
	sys	SYS_EXIT
.ko:	adr	x0,m_ko
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; w0 = masque, x1 = compteurs : +1 pour chaque bit (garde w2)
count_bits:
.bit:	cbz	w0,.out
	rbit	w3,w0
	clz	w3,w3
	ldr	w4,[x1,x3,lsl 2]
	add	w4,w4,1
	str	w4,[x1,x3,lsl 2]
	sub	w3,w0,1
	and	w0,w0,w3
	b	.bit
.out:	ret

; x0 = seconde, w2 = niveau de GPIO 4 : la ligne, les broches qui ont bouge
report:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x2,[sp,-16]!
	mov	x19,x0
	adr	x1,line
	adr	x0,m_t
	bl	put_str
	mov	x0,x19
	bl	put_dec
	adr	x0,m_lev
	bl	put_str
	ldr	x0,[sp],16
	bl	put_dec
	mov	x19,0
	adr	x20,counts
.pin:	ldr	w2,[x20,x19,lsl 2]
	cbz	w2,.next
	mov	w0,' '
	strb	w0,[x1],1
	mov	x0,x19
	bl	put_dec
	mov	w0,':'
	strb	w0,[x1],1
	ldr	w0,[x20,x19,lsl 2]
	bl	put_dec
.next:	add	x19,x19,1
	cmp	x19,PIN_N
	b.lo	.pin
	bl	line_out
	ldp	x19,x20,[sp],16
	epilogue

; x1 = fin de la ligne : l'affiche
line_out:
	prologue
	mov	w0,10
	strb	w0,[x1],1
	strb	wzr,[x1]
	adr	x0,line
	bl	puts
	epilogue

; x0 = chaine, x1 = destination -> x1 = fin
put_str:
.c:	ldrb	w2,[x0],1
	cbz	w2,.e
	strb	w2,[x1],1
	b	.c
.e:	ret

; x0 = valeur, x1 = destination -> x1 = fin
put_dec:
	sub	sp,sp,32
	mov	x2,sp
	mov	x6,sp
	mov	x3,10
.div:	udiv	x4,x0,x3
	msub	x5,x4,x3,x0
	add	w5,w5,'0'
	strb	w5,[x2],1
	mov	x0,x4
	cbnz	x0,.div
.rev:	ldrb	w5,[x2,-1]!
	strb	w5,[x1],1
	cmp	x2,x6
	b.ne	.rev
	add	sp,sp,32
	ret

m_fsel		db 'gpio4 : fonction ',0
m_t		db 't=',0
m_lev		db ' gpio4=',0
m_ko		db 'gpiowatch : registres GPIO refuses (a lancer depuis SD0:/sys)',10,0

segment readable writeable
kit_data
		align 8
counts		rd PIN_N
line		rb 1024
