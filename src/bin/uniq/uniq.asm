; uniq - retire les lignes repetees : uniq [-c] [-d] [-u] [fichier] (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; bits : 1 -c, 2 -d, 4 -u
.o:	cmp	x21,x19
	b.hs	.go
	ldr	x24,[x20,x21,lsl 3]
	mov	x0,x24
	bl	t_est_opt
	cbz	x0,.go
	add	x21,x21,1
.l:	ldrb	w0,[x24,1]!
	cbz	w0,.o
	cmp	w0,'c'
	b.ne	.l2
	orr	x22,x22,1
	b	.l
.l2:	cmp	w0,'d'
	b.ne	.l3
	orr	x22,x22,2
	b	.l
.l3:	orr	x22,x22,4
	b	.l
.go:	ldr	x0,[x20,x21,lsl 3]
	cmp	x21,x19
	csel	x0,xzr,x0,hs
	bl	t_ouvrir
	tbnz	x0,63,.err
	bl	t_lire_debut
	mov	x23,0				; repetitions de la ligne gardee
.c:	bl	t_lire_ligne
	cbz	x0,.fin
	mov	x24,x0
	cbz	x23,.neuve
	adr	x1,garde
	bl	str_eq
	cbz	x0,.chg
	add	x23,x23,1
	b	.c
.chg:	bl	sort_ligne
.neuve:	adr	x0,garde
	mov	x1,x24
.cp:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.cp
	mov	x23,1
	b	.c
.fin:	cbz	x23,.q
	bl	sort_ligne
.q:	mov	x0,0
	bl	t_fin
.err:	ldr	x0,[x20,x21,lsl 3]
	bl	t_err_fic
	mov	x0,1
	bl	t_fin

sort_ligne:
	prologue
	tbz	x22,1,.pasd
	cmp	x23,1
	b.ls	.out
.pasd:	tbz	x22,2,.pasu
	cmp	x23,1
	b.ne	.out
.pasu:	tbz	x22,0,.l
	mov	x0,x23
	bl	t_putn
	mov	w0,' '
	bl	t_putc
.l:	adr	x0,garde
	bl	t_puts
	bl	t_nl
.out:	epilogue

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
garde	rb T_LIGNE
