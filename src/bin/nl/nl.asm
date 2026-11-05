; nl - numerote les lignes : nl [-b a] [fichiers] (docs/OUTILS.md, d'apres nl.c d'Onyx)
; Chaque ligne precedee de son numero (6 colonnes) et d'une tabulation ; une ligne vide
; n'est pas numerotee (-b a, -ba : toutes le sont). Sans fichier : l'entree standard ;
; plusieurs fichiers : numerotes comme un seul texte.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1				; premier fichier
	mov	x22,0				; 1 : toutes les lignes
	mov	x23,0				; numero
	mov	x25,0				; code de sortie
	cmp	x19,2
	b.lo	.go
	ldr	x24,[x20,8]
	mov	x0,x24
	adr	x1,s_ba
	bl	str_eq
	cbz	x0,.b
	mov	x22,1
	mov	x21,2
	b	.go
.b:	mov	x0,x24
	adr	x1,s_b
	bl	str_eq
	cbz	x0,.go
	cmp	x19,3
	b.lo	.usage
	ldr	x0,[x20,16]
	ldrb	w0,[x0]
	cmp	w0,'a'
	cset	x22,eq
	mov	x21,3
	b	.go
.usage:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,2
	bl	t_fin
.go:	cmp	x21,x19
	b.lo	.a
	mov	x0,0
	bl	un_fichier
	mov	x0,x25
	bl	t_fin
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	bl	un_fichier
	b	.a
.fin:	mov	x0,x25
	bl	t_fin

; x0 = chemin (0 : l'entree standard)
un_fichier:
	prologue
	stp	x24,x26,[sp,-16]!
	mov	x24,x0
	bl	t_ouvrir
	tbnz	x0,63,.err
	mov	x26,x0
	bl	t_lire_debut
.l:	bl	t_lire_ligne
	cbz	x0,.f
	mov	x24,x0
	cbnz	x1,.num
	cbz	x22,.txt
.num:	add	x23,x23,1
	mov	x0,x23
	mov	x1,6
	bl	put_w
	mov	w0,9
	bl	t_putc
.txt:	mov	x0,x24
	bl	t_puts
	bl	t_nl
	b	.l
.f:	mov	x0,x26
	bl	t_fermer
	b	.out
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x25,1
.out:	ldp	x24,x26,[sp],16
	epilogue

; x0 = nombre, x1 = largeur : aligne a droite
put_w:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x1
	mov	x1,sp
	bl	fmt_dec
	mov	x1,sp
	sub	x0,x0,x1			; chiffres
	sub	x19,x19,x0
.b:	cmp	x19,0
	b.le	.t
	mov	w0,' '
	bl	t_putc
	sub	x19,x19,1
	b	.b
.t:	mov	x0,sp
	bl	t_puts
	add	sp,sp,32
	ldr	x19,[sp],16
	epilogue

s_ba	db '-ba',0
s_b	db '-b',0
s_usage	db 'usage : nl [-b a] [fichiers]',0
	align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
