; uname - quel systeme c'est : uname [-a] [-s] [-n] [-r] [-v] [-m] [-p]
; (docs/OUTILS.md, d'apres uname.c d'Onyx)
;   -s  le nom du systeme (Korin) : par defaut
;   -n  le nom de la machine (hostname = de SD0:/etc/system.ini ; korin sinon)
;   -r  la version du systeme installe (le paquet armpi, qui porte le noyau : SD0:/var/pkg/db/armpi.ini)
;   -v  le niveau d'ABI des kits (SD0:/etc/pkg/abi) et la date du noyau (SD0:/kernel8.img)
;   -m  la machine (aarch64)
;   -p  le paquet du systeme et sa version (armpi 2026.11.19)
;   -a  tout, puis les coeurs et la memoire
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

FILE_CAP	= 4096
VAL_CAP		= 128

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,0				; bits : 1 s, 2 n, 4 r, 8 v, 16 m, 32 p, 64 a
	mov	x23,1
.a:	cmp	x23,x19
	b.hs	.go
	ldr	x24,[x20,x23,lsl 3]
	add	x23,x23,1
	ldrb	w0,[x24]
	cmp	w0,'-'
	b.ne	.usage
.l:	ldrb	w0,[x24,1]!
	cbz	w0,.a
	adr	x1,s_lettres
	mov	x2,1
.c:	ldrb	w3,[x1],1
	cbz	w3,.usage
	cmp	w0,w3
	b.eq	.ok
	lsl	x2,x2,1
	b	.c
.ok:	orr	x21,x21,x2
	b	.l
.usage:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,1
	bl	t_fin
.go:	tbz	x21,6,.d
	orr	x21,x21,63
.d:	tst	x21,63
	b.ne	.lire
	mov	x21,1
.lire:	mov	x22,1				; 1 : premier champ
	adr	x0,s_armpi			; version du paquet du systeme
	adr	x1,s_version
	mov	w2,'='
	adr	x3,version
	bl	file_value
	tbz	x21,0,.n
	bl	sep
	adr	x0,s_korin
	bl	t_puts
.n:	tbz	x21,1,.r
	bl	sep
	adr	x0,s_system
	adr	x1,s_hostname
	mov	w2,'='
	adr	x3,val
	bl	file_value
	adr	x0,val
	ldrb	w1,[x0]
	cbnz	w1,.np
	adr	x0,s_host_def
.np:	bl	t_puts
.r:	tbz	x21,2,.v
	bl	sep
	bl	put_version
.v:	tbz	x21,3,.m
	bl	sep
	adr	x0,s_abi_mot
	bl	t_puts
	adr	x0,s_abi
	adr	x1,val
	mov	x2,VAL_CAP
	bl	read_file
	adr	x0,val
	bl	trim
	adr	x0,val
	ldrb	w1,[x0]
	cbnz	w1,.vp
	adr	x0,s_q
.vp:	bl	t_puts
	adr	x0,msg				; date du noyau (V_STAT de vfs : d)
	mov	x1,V_STAT
	mov	x2,0
	mov	x3,0
	adr	x4,s_kernel
	mov	x5,0
	bl	t_msg
	adr	x0,msg
	bl	t_vfs
	tbnz	x0,63,.m
	adr	x1,msg
	ldr	x0,[x1,MSG_D]			; secondes UTC (0 : aucune)
	cbz	x0,.m
	mov	x24,x0
	adr	x0,s_noyau_du
	bl	t_dire
	mov	x0,1
	sys	SYS_CLOCK_GET			; decalage de l'heure locale
	add	x0,x24,x0
	bl	put_date
.m:	tbz	x21,4,.p
	bl	sep
	adr	x0,s_machine
	bl	t_puts
.p:	tbz	x21,5,.aa
	bl	sep
	adr	x0,s_armpi_nom
	bl	t_puts
	mov	w0,' '
	bl	t_putc
	bl	put_version
.aa:	tbz	x21,6,.fin
	adr	x0,info
	sys	SYS_SYSINFO
	tbnz	x0,63,.fin
	adr	x0,s_par
	bl	t_puts
	adr	x24,info
	ldr	x0,[x24]
	bl	t_putn
	adr	x0,s_coeurs
	bl	t_dire
	ldr	x0,[x24,16]			; Ko
	lsr	x0,x0,10
	bl	t_putn
	adr	x0,s_mo
	bl	t_dire
.fin:	bl	t_nl
	mov	x0,0
	bl	t_fin

; un blanc avant chaque champ sauf le premier
sep:
	cbnz	x22,.prem
	mov	w0,' '
	b	t_putc
.prem:	mov	x22,0
	ret

put_version:
	adr	x0,version
	ldrb	w1,[x0]
	cbnz	w1,.p
	adr	x0,s_q
.p:	b	t_puts

; x0 = secondes (heure locale) : AAAA-MM-JJ HH:MM
put_date:
	prologue
	stp	x19,x20,[sp,-16]!
	movl	x1,86400
	udiv	x2,x0,x1
	msub	x19,x2,x1,x0			; secondes du jour
	movl	x4,719468
	add	x2,x2,x4
	movl	x4,146097
	udiv	x5,x2,x4
	msub	x6,x5,x4,x2
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
	udiv	x7,x7,x4
	mov	x4,400
	mul	x8,x5,x4
	add	x8,x8,x7
	mov	x4,365
	mul	x10,x7,x4
	add	x10,x10,x7,lsr 2
	mov	x4,100
	udiv	x11,x7,x4
	sub	x10,x10,x11
	sub	x6,x6,x10
	mov	x4,5
	mul	x10,x6,x4
	add	x10,x10,2
	mov	x4,153
	udiv	x10,x10,x4
	mov	x4,153
	mul	x11,x10,x4
	add	x11,x11,2
	mov	x4,5
	udiv	x11,x11,x4
	sub	x6,x6,x11
	add	x6,x6,1
	add	x11,x10,3
	sub	x4,x10,9
	cmp	x10,10
	csel	x10,x11,x4,lo
	cmp	x10,2
	b.hi	.an
	add	x8,x8,1
.an:	adr	x9,dt
	stp	x8,x10,[x9]
	str	x6,[x9,16]
	mov	x0,x8
	bl	t_putn
	mov	w0,'-'
	bl	t_putc
	adr	x9,dt
	ldr	x0,[x9,8]
	bl	two
	mov	w0,'-'
	bl	t_putc
	adr	x9,dt
	ldr	x0,[x9,16]
	bl	two
	mov	w0,' '
	bl	t_putc
	mov	x1,3600
	udiv	x0,x19,x1
	bl	two
	mov	w0,':'
	bl	t_putc
	mov	x1,3600
	udiv	x0,x19,x1
	msub	x0,x0,x1,x19
	mov	x1,60
	udiv	x0,x0,x1
	bl	two
	ldp	x19,x20,[sp],16
	epilogue

; x0 = 0..99
two:
	prologue
	str	x19,[sp,-16]!
	mov	x1,10
	udiv	x19,x0,x1
	msub	x2,x19,x1,x0
	str	x2,[sp,8]
	add	w0,w19,'0'
	bl	t_putc
	ldr	x0,[sp,8]
	add	w0,w0,'0'
	bl	t_putc
	ldr	x19,[sp],16
	epilogue

; x0 = chemin, x1 = tampon, x2 = capacite -> x0 = octets lus (0 : rien), tampon termine par 0
read_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	sub	x21,x2,1
	strb	wzr,[x20]
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x22,0
	tbnz	x0,63,.out
	mov	x19,x0
.r:	sub	x2,x21,x22
	cbz	x2,.f
	mov	x0,x19
	add	x1,x20,x22
	sys	SYS_READ
	cmp	x0,0
	b.le	.f
	add	x22,x22,x0
	b	.r
.f:	mov	x0,x19
	sys	SYS_CLOSE
.out:	strb	wzr,[x20,x22]
	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte : retire les blancs et fins de ligne de la fin (et s'arrete a la premiere fin de ligne)
trim:
	mov	x1,x0
.c:	ldrb	w2,[x1]
	cbz	w2,.b
	cmp	w2,10
	b.eq	.z
	cmp	w2,13
	b.eq	.z
	add	x1,x1,1
	b	.c
.z:	strb	wzr,[x1]
.b:	cmp	x1,x0
	b.ls	.r
	ldurb	w2,[x1,-1]
	cmp	w2,' '
	b.eq	.s
	cmp	w2,9
	b.ne	.r
.s:	sub	x1,x1,1
	strb	wzr,[x1]
	b	.b
.r:	ret

; x0 = fichier, x1 = cle, w2 = separateur ('='), x3 = sortie (VAL_CAP) : la valeur de « cle = valeur »
; (blancs autour), "" si absente
file_value:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	w20,w2
	mov	x21,x3
	strb	wzr,[x21]
	adr	x1,fbuf
	mov	x2,FILE_CAP
	bl	read_file
	adr	x0,fbuf
	mov	x1,x19
	mov	w2,w20
	mov	x3,x21
	bl	value_of
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte (lignes), x1 = cle, w2 = separateur, x3 = sortie (VAL_CAP)
value_of:
	strb	wzr,[x3]
.ligne:	ldrb	w4,[x0]
	cbz	w4,.r
	mov	x5,x0				; blancs du debut
.bl:	ldrb	w4,[x5]
	cmp	w4,' '
	b.eq	.bl1
	cmp	w4,9
	b.ne	.cle
.bl1:	add	x5,x5,1
	b	.bl
.cle:	mov	x6,0
.k:	ldrb	w7,[x1,x6]
	cbz	w7,.kfin
	ldrb	w4,[x5,x6]
	cmp	w4,w7
	b.ne	.suite
	add	x6,x6,1
	b	.k
.kfin:	add	x5,x5,x6
.b2:	ldrb	w4,[x5]
	cmp	w4,' '
	b.eq	.b2a
	cmp	w4,9
	b.ne	.sep
.b2a:	add	x5,x5,1
	b	.b2
.sep:	cmp	w4,w2
	b.ne	.suite
	add	x5,x5,1
.b3:	ldrb	w4,[x5]
	cmp	w4,' '
	b.eq	.b3a
	cmp	w4,9
	b.ne	.val
.b3a:	add	x5,x5,1
	b	.b3
.val:	mov	x6,0
.v:	ldrb	w4,[x5,x6]
	cbz	w4,.vf
	cmp	w4,10
	b.eq	.vf
	cmp	w4,13
	b.eq	.vf
	cmp	x6,VAL_CAP-1
	b.hs	.vf
	strb	w4,[x3,x6]
	add	x6,x6,1
	b	.v
.vf:	strb	wzr,[x3,x6]
	mov	x0,x3
	b	trim
.suite:	ldrb	w4,[x0]				; ligne suivante
	cbz	w4,.r
	add	x0,x0,1
	cmp	w4,10
	b.ne	.suite
	b	.ligne
.r:	ret

s_lettres	db 'snrvmpa',0
s_usage		db 'usage : uname [-a] [-s] [-n] [-r] [-v] [-m] [-p]',0
s_korin		db 'Korin',0
s_host_def	db 'korin',0
s_machine	db 'aarch64',0
s_armpi_nom	db 'armpi',0
s_armpi		db 'SD0:/var/pkg/db/armpi.ini',0
s_version	db 'version',0
s_system	db 'SD0:/etc/system.ini',0
s_hostname	db 'hostname',0
s_abi		db 'SD0:/etc/pkg/abi',0
s_abi_mot	db 'abi ',0
s_kernel	db 'SD0:/kernel8.img',0
s_noyau_du	db ', noyau du ',0
s_q		db '?',0
s_par		db ' (',0
s_coeurs	db ' coeurs, ',0
s_mo		db ' Mo de m',0xC3,0xA9,'moire)',0
	align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
	align 8
msg	rb MSG_TAILLE
info	rb 80
dt	rb 24
version	rb VAL_CAP
val	rb VAL_CAP
fbuf	rb FILE_CAP
