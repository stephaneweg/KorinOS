; date - la date et l'heure (l'horloge du noyau, heure locale) : date [-u] [+FORMAT]
; (docs/OUTILS.md, d'apres date.c d'Onyx). Sans format : 2026-10-04 21:47:03. Dans FORMAT :
; %Y annee, %m mois, %d jour, %H heure, %M minute, %S seconde, %y les deux derniers chiffres de
; l'annee, %F = %Y-%m-%d, %T = %H:%M:%S, %s secondes depuis 1970 (UTC), %n fin de ligne, %% un % ;
; le reste tel quel. -u : en temps universel.   date +%H:%M    date "+%d/%m/%Y"
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,2				; SYS_CLOCK_GET : 2 heure locale, 0 UTC
	adr	x22,s_def			; format
	mov	x23,1
.a:	cmp	x23,x19
	b.hs	.go
	ldr	x24,[x20,x23,lsl 3]
	add	x23,x23,1
	ldrb	w0,[x24]
	cmp	w0,'+'
	b.ne	.u
	add	x22,x24,1
	b	.a
.u:	mov	x0,x24
	adr	x1,s_u
	bl	str_eq
	cbz	x0,.usage
	mov	x21,0
	b	.a
.usage:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,2
	bl	t_fin
.go:	mov	x0,0
	sys	SYS_CLOCK_GET
	mov	x26,x0				; secondes UTC (%s)
	mov	x0,x21
	sys	SYS_CLOCK_GET
	bl	civil
.f:	ldrb	w0,[x22],1
	cbz	w0,.fin
	cmp	w0,'%'
	b.ne	.lit
	ldrb	w1,[x22]
	cbnz	w1,.pct
.lit:	bl	t_putc
	b	.f
.pct:	add	x22,x22,1
	adr	x27,tv
	cmp	w1,'Y'
	b.ne	.y
	ldr	x0,[x27]
	bl	t_putn
	b	.f
.y:	cmp	w1,'y'
	b.ne	.m
	ldr	x0,[x27]
	mov	x1,100
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	bl	two
	b	.f
.m:	mov	x2,1
	cmp	w1,'m'
	b.eq	.v
	mov	x2,2
	cmp	w1,'d'
	b.eq	.v
	mov	x2,3
	cmp	w1,'H'
	b.eq	.v
	mov	x2,4
	cmp	w1,'M'
	b.eq	.v
	mov	x2,5
	cmp	w1,'S'
	b.eq	.v
	cmp	w1,'F'
	b.ne	.t
	ldr	x0,[x27]
	bl	t_putn
	mov	w0,'-'
	bl	t_putc
	ldr	x0,[x27,8]
	bl	two
	mov	w0,'-'
	bl	t_putc
	ldr	x0,[x27,16]
	bl	two
	b	.f
.t:	cmp	w1,'T'
	b.ne	.s
	ldr	x0,[x27,24]
	bl	two
	mov	w0,':'
	bl	t_putc
	ldr	x0,[x27,32]
	bl	two
	mov	w0,':'
	bl	t_putc
	ldr	x0,[x27,40]
	bl	two
	b	.f
.s:	cmp	w1,'s'
	b.ne	.n
	mov	x0,x26
	bl	t_putn
	b	.f
.n:	cmp	w1,'n'
	b.ne	.pp
	bl	t_nl
	b	.f
.pp:	cmp	w1,'%'
	b.ne	.autre
	mov	w0,'%'
	bl	t_putc
	b	.f
.autre:	mov	w24,w1
	mov	w0,'%'
	bl	t_putc
	mov	w0,w24
	bl	t_putc
	b	.f
.v:	ldr	x0,[x27,x2,lsl 3]
	bl	two
	b	.f
.fin:	bl	t_nl
	mov	x0,0
	bl	t_fin

; x0 = 0..99 : deux chiffres
two:
	prologue
	str	x19,[sp,-16]!
	mov	x1,10
	udiv	x19,x0,x1
	msub	x19,x19,x1,x0			; unites
	udiv	x0,x0,x1
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0			; dizaines
	add	w0,w0,'0'
	bl	t_putc
	add	w0,w19,'0'
	bl	t_putc
	ldr	x19,[sp],16
	epilogue

; x0 = secondes depuis 1970 -> tv : annee, mois, jour, heure, minute, seconde
civil:
	movl	x1,86400
	udiv	x2,x0,x1			; jours
	msub	x3,x2,x1,x0			; secondes du jour
	adr	x9,tv
	mov	x4,3600
	udiv	x5,x3,x4
	str	x5,[x9,24]
	msub	x3,x5,x4,x3
	mov	x4,60
	udiv	x5,x3,x4
	str	x5,[x9,32]
	msub	x3,x5,x4,x3
	str	x3,[x9,40]
	movl	x4,719468			; jour -> date (depuis le 1er mars de l'an 0)
	add	x2,x2,x4
	movl	x4,146097
	udiv	x5,x2,x4			; ere
	msub	x6,x5,x4,x2			; jour de l'ere
	mov	x4,1460
	udiv	x7,x6,x4
	movl	x4,36524
	udiv	x8,x6,x4
	movl	x4,146096
	udiv	x10,x6,x4
	sub	x7,x6,x7
	add	x7,x7,x8
	sub	x7,x7,x10
	mov	x4,365
	udiv	x7,x7,x4			; annee de l'ere
	mov	x4,400
	mul	x8,x5,x4
	add	x8,x8,x7			; annee
	mov	x4,365
	mul	x10,x7,x4
	add	x10,x10,x7,lsr 2
	mov	x4,100
	udiv	x11,x7,x4
	sub	x10,x10,x11
	sub	x6,x6,x10			; jour de l'annee (depuis mars)
	mov	x4,5
	mul	x10,x6,x4
	add	x10,x10,2
	mov	x4,153
	udiv	x10,x10,x4			; mois depuis mars
	mov	x4,153
	mul	x11,x10,x4
	add	x11,x11,2
	mov	x4,5
	udiv	x11,x11,x4
	sub	x6,x6,x11
	add	x6,x6,1				; jour
	add	x11,x10,3
	sub	x4,x10,9
	cmp	x10,10
	csel	x10,x11,x4,lo			; mois 1..12
	cmp	x10,2
	b.hi	.an
	add	x8,x8,1
.an:	str	x8,[x9]
	str	x10,[x9,8]
	str	x6,[x9,16]
	ret

s_def	db '%F %T',0
s_u	db '-u',0
s_usage	db 'usage : date [-u] [+FORMAT]',0
	align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
	align 8
tv	rb 48
