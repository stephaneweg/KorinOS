; hexdump - les octets d'un fichier : la position, 16 octets en hexadecimal, les memes en texte
;   hexdump [-s POSITION] [-n NOMBRE] [fichier]     (docs/OUTILS.md, d'apres hexdump.c d'Onyx)
; -s : saute POSITION octets d'abord ; -n : s'arrete apres NOMBRE octets (decimal, ou 0x... ).
; Sans fichier : l'entree standard.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; a sauter
	mov	x23,-1				; nombre (-1 : tout)
.o:	add	x0,x21,1
	cmp	x0,x19
	b.hs	.go
	ldr	x24,[x20,x21,lsl 3]
	ldrb	w0,[x24]
	cmp	w0,'-'
	b.ne	.go
	ldrb	w25,[x24,1]
	cbz	w25,.go
	ldrb	w0,[x24,2]
	cbnz	w0,.usage
	add	x0,x21,1
	ldr	x0,[x20,x0,lsl 3]
	bl	parse_num
	tbnz	x1,63,.usage
	add	x21,x21,2
	cmp	w25,'s'
	b.ne	.n
	mov	x22,x0
	b	.o
.n:	cmp	w25,'n'
	b.ne	.usage
	mov	x23,x0
	b	.o
.usage:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,2
	bl	t_fin
.go:	mov	x24,0
	cmp	x21,x19
	b.hs	.ouv
	ldr	x24,[x20,x21,lsl 3]
.ouv:	mov	x0,x24
	bl	t_ouvrir
	tbz	x0,63,.ok
	mov	x0,x24
	cbnz	x0,.e
	adr	x0,s_stdin
.e:	bl	t_err_fic
	mov	x0,1
	bl	t_fin
.ok:	mov	x26,x0
	bl	t_lire_debut
	mov	x25,0				; position
.saut:	cmp	x25,x22
	b.hs	.ligne
	bl	t_getc
	tbnz	x0,63,.ligne
	add	x25,x25,1
	b	.saut
.ligne:	mov	x27,0				; octets de la ligne
	adr	x28,rangee
.lb:	cmp	x27,16
	b.hs	.aff
	cbz	x23,.aff
	bl	t_getc
	tbnz	x0,63,.aff
	strb	w0,[x28,x27]
	add	x27,x27,1
	cmp	x23,0
	b.le	.lb
	sub	x23,x23,1
	b	.lb
.aff:	cbz	x27,.fin
	mov	x0,x25
	mov	x1,8
	bl	hex
	mov	w0,' '
	bl	t_putc
	mov	w0,' '
	bl	t_putc
	mov	x24,0
.h:	cmp	x24,16
	b.hs	.txt
	cmp	x24,x27
	b.hs	.vide
	ldrb	w0,[x28,x24]
	mov	x1,2
	bl	hex
	b	.esp
.vide:	mov	w0,' '
	bl	t_putc
	mov	w0,' '
	bl	t_putc
.esp:	mov	w0,' '
	bl	t_putc
	cmp	x24,7
	b.ne	.hn
	mov	w0,' '
	bl	t_putc
.hn:	add	x24,x24,1
	b	.h
.txt:	mov	w0,' '
	bl	t_putc
	mov	w0,'|'
	bl	t_putc
	mov	x24,0
.t:	cmp	x24,x27
	b.hs	.tf
	ldrb	w0,[x28,x24]
	cmp	w0,32
	b.lo	.pt
	cmp	w0,127
	b.lo	.tc
.pt:	mov	w0,'.'
.tc:	bl	t_putc
	add	x24,x24,1
	b	.t
.tf:	mov	w0,'|'
	bl	t_putc
	bl	t_nl
	add	x25,x25,x27
	cmp	x27,16
	b.eq	.ligne
.fin:	mov	x0,x25
	mov	x1,8
	bl	hex
	bl	t_nl
	mov	x0,x26
	bl	t_fermer
	mov	x0,0
	bl	t_fin

; x0 = valeur, x1 = chiffres
hex:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	sub	x20,x1,1
.c:	lsl	x1,x20,2
	lsr	x0,x19,x1
	and	x0,x0,15
	adr	x1,s_hex
	ldrb	w0,[x1,x0]
	bl	t_putc
	subs	x20,x20,1
	b.pl	.c
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte (decimal, ou 0x hexadecimal) -> x0 = valeur, x1 = 0 (ou -1 : pas un nombre)
parse_num:
	mov	x2,x0
	mov	x0,0
	mov	x1,-1
	ldrb	w3,[x2]
	cmp	w3,'0'
	b.ne	.dec
	ldrb	w3,[x2,1]
	orr	w3,w3,0x20
	cmp	w3,'x'
	b.ne	.dec
	add	x2,x2,2
.hx:	ldrb	w3,[x2],1
	cbz	w3,.r
	sub	w4,w3,'0'
	cmp	w4,9
	b.ls	.hd
	orr	w3,w3,0x20
	sub	w4,w3,'a'
	cmp	w4,5
	b.hi	.non
	add	w4,w4,10
.hd:	lsl	x0,x0,4
	add	x0,x0,x4
	mov	x1,0
	b	.hx
.dec:	ldrb	w3,[x2],1
	cbz	w3,.r
	sub	w4,w3,'0'
	cmp	w4,9
	b.hi	.non
	mov	x5,10
	mul	x0,x0,x5
	add	x0,x0,x4
	mov	x1,0
	b	.dec
.non:	mov	x1,-1
.r:	ret

s_hex	db '0123456789abcdef'
s_usage	db 'usage : hexdump [-s POSITION] [-n NOMBRE] [fichier]',0
s_stdin	db '-',0
	align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
rangee	rb 16
