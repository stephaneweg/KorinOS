; df - la place des volumes : df [volume-ou-chemin ...]   (docs/OUTILS.md, d'apres df.c d'Onyx)
; Sans argument : chaque volume monte (VFS_VOLUMES de vfs). Une ligne chacun : le volume, son
; systeme de fichiers, sa taille, la place prise et libre, son etiquette (FS_INFO de son
; service, « fs.<volume> »). Un chemin : le volume qui le porte (relatif : le dossier courant).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

VE_TAILLE	= 56			; VFS_VOLUMES : nom 16, source 24, service 16

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x25,0				; code de sortie
	cmp	x19,2
	b.hs	.args
	adr	x0,vmsg				; tous les volumes montes
	mov	x1,VFS_VOLUMES
	mov	x2,0
	mov	x3,0
	adr	x4,s_vide
	mov	x5,0
	bl	t_msg
	adr	x0,vmsg
	bl	t_vfs
	tbnz	x0,63,.novfs
	adr	x0,vmsg
	ldr	x21,[x0,MSG_B]
	cmp	x21,FS_DONNEES/VE_TAILLE
	b.ls	.n
	mov	x21,FS_DONNEES/VE_TAILLE
.n:	add	x22,x0,MSG_DATA
.v:	cbz	x21,.fin
	mov	x0,x22
	mov	x1,1
	bl	show
	add	x22,x22,VE_TAILLE
	sub	x21,x21,1
	b	.v
.novfs:	adr	x0,s_novfs
	bl	t_err_msg
	mov	x0,1
	bl	t_fin
.args:	mov	x21,1
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	bl	vol_of
	adr	x0,vname
	mov	x1,0
	bl	show
	orr	x25,x25,x0
	b	.a
.fin:	mov	x0,x25
	bl	t_fin

; x0 = chemin -> vname : le nom de son volume, en minuscules (« sd0 »)
vol_of:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x1,x0				; un « : » avant le premier « / » ?
.c:	ldrb	w2,[x1],1
	cbz	w2,.cwd
	cmp	w2,'/'
	b.eq	.cwd
	cmp	w2,':'
	b.ne	.c
	b	.copie
.cwd:	adr	x0,cwd				; relatif ou « /... » : le volume courant
	mov	x1,256
	sys	SYS_GETCWD
	adr	x19,cwd
.copie:	adr	x20,vname
	mov	x2,0
.k:	ldrb	w3,[x19,x2]
	cbz	w3,.z
	cmp	w3,':'
	b.eq	.z
	cmp	w3,'/'
	b.eq	.z
	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.st
	add	w3,w3,32
.st:	strb	w3,[x20,x2]
	add	x2,x2,1
	cmp	x2,14
	b.lo	.k
.z:	strb	wzr,[x20,x2]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom du volume (minuscules), x1 = 1 : se taire s'il ne repond pas -> x0 = 0, 1 (absent)
show:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x23,x1
	adr	x0,svcname			; « fs.<nom> »
	adr	x1,s_fs
	mov	x2,x19
	bl	cat2
	adr	x0,svcname
	mov	x1,200
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.absent
	mov	x21,x0
	adr	x22,imsg
	mov	x0,x22
	mov	x1,MSG_TAILLE
.z:	subs	x1,x1,8
	str	xzr,[x0,x1]
	b.ne	.z
	mov	x0,FS_INFO
	str	x0,[x22]
	mov	x0,x21
	mov	x1,x22
	sys	SYS_SVC_CALL
	tbnz	x0,63,.absent
	adr	x0,upname			; « SD0: »
	mov	x1,x19
.u:	ldrb	w2,[x1],1
	cbz	w2,.uf
	sub	w3,w2,'a'
	cmp	w3,25
	b.hi	.us
	sub	w2,w2,32
.us:	strb	w2,[x0],1
	b	.u
.uf:	mov	w2,':'
	strb	w2,[x0],1
	strb	wzr,[x0]
	adr	x0,upname
	bl	t_puts
	adr	x0,upname
	bl	str_len
	mov	x1,8
	sub	x0,x1,x0
	bl	pad
	ldr	x0,[x22,MSG_D]			; type
	adr	x1,s_types
	cmp	x0,3
	b.ls	.ty
	mov	x0,0
.ty:	ldrb	w0,[x1,x0]
	add	x0,x1,x0
	str	x0,[sp,-16]!
	bl	t_puts
	ldr	x0,[sp],16
	bl	str_len
	mov	x1,7
	sub	x0,x1,x0
	bl	pad
	adr	x0,s_taille
	bl	t_dire
	ldr	x0,[x22,MSG_A]
	bl	put_size
	ldr	x0,[x22,MSG_B]
	tbnz	x0,63,.inconnu
	adr	x0,s_pris
	bl	t_dire
	ldp	x0,x1,[x22,MSG_A]
	subs	x0,x0,x1
	csel	x0,x0,xzr,hs
	bl	put_size
	adr	x0,s_libre
	bl	t_dire
	ldr	x0,[x22,MSG_B]
	bl	put_size
	b	.etiq
.inconnu:
	adr	x0,s_libre_q
	bl	t_dire
.etiq:	ldrb	w0,[x22,MSG_DATA]
	cbz	w0,.ram
	adr	x0,s_ouv
	bl	t_puts
	add	x0,x22,MSG_DATA
	strb	wzr,[x0,63]
	bl	t_puts
	adr	x0,s_ferm
	bl	t_puts
.ram:	ldr	x0,[x22,MSG_D]
	cmp	x0,FS_T_RAM
	b.ne	.nl
	adr	x0,s_ram
	bl	t_dire
.nl:	bl	t_nl
	mov	x0,0
	b	.out
.absent:
	mov	x0,1
	cbnz	x23,.out
	adr	x0,s_pasvol
	bl	t_dire_err
	mov	x0,x19
	bl	t_err
	adr	x0,s_t_nl
	bl	t_err
	mov	x0,1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte litteral : « df: » puis le texte traduit, sur l'erreur standard
t_dire_err:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x20]
	bl	t_err
	adr	x0,s_t_sep
	bl	t_err
	mov	x0,x19
	bl	t_derr
	ldr	x19,[sp],16
	epilogue

; x0 = destination, x1, x2 = chaines a mettre bout a bout
cat2:
.a:	ldrb	w3,[x1],1
	cbz	w3,.b
	strb	w3,[x0],1
	b	.a
.b:	ldrb	w3,[x2],1
	strb	w3,[x0],1
	cbnz	w3,.b
	ret

; x0 = nombre de blancs (<= 0 : aucun ; au moins un apres un nom trop long)
pad:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cmp	x19,1
	b.ge	.b
	mov	x19,1
.b:	mov	w0,' '
	bl	t_putc
	subs	x19,x19,1
	b.gt	.b
	ldr	x19,[sp],16
	epilogue

; x0 = octets : « 512 Ko », « 37,5 Mo », « 29,7 Go », « 1,8 To »
put_size:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x21,s_ko
	mov	x20,1024
	mov	x0,1
	lsl	x1,x0,40
	cmp	x19,x1
	b.lo	.go
	adr	x21,s_to
	mov	x20,x1
	b	.calc
.go:	lsl	x1,x0,30
	cmp	x19,x1
	b.lo	.mo
	adr	x21,s_go
	mov	x20,x1
	b	.calc
.mo:	lsl	x1,x0,20
	cmp	x19,x1
	b.lo	.calc
	adr	x21,s_mo
	mov	x20,x1
.calc:	udiv	x22,x19,x20			; entier
	mov	x0,x22
	bl	t_putn
	cmp	x20,1024
	b.ls	.u
	cmp	x22,100
	b.hs	.u
	adr	x0,s_virg
	bl	t_dire
	msub	x0,x22,x20,x19			; reste
	mov	x1,10
	mul	x0,x0,x1
	udiv	x0,x0,x20
	bl	t_putn
.u:	mov	x0,x21
	bl	t_dire
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_vide		db 0
s_fs		db 'fs.',0
s_novfs		db 'pas de service vfs',0
s_pasvol	db 'pas de volume ',0
s_types		db 4, 6, 12, 18
		db '?',0, 'FAT32',0, 'exFAT',0, 'RAM',0
s_taille	db 'taille ',0
s_pris		db ', utilis',0xC3,0xA9,' ',0
s_libre		db ', libre ',0
s_libre_q	db ', place libre inconnue',0
s_ouv		db '  ',0xC2,0xAB,' ',0
s_ferm		db ' ',0xC2,0xBB,0
s_ram		db ' (en m',0xC3,0xA9,'moire : perdu au red',0xC3,0xA9,'marrage)',0
s_virg		db ',',0
s_ko		db ' Ko',0
s_mo		db ' Mo',0
s_go		db ' Go',0
s_to		db ' To',0
	align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
	align 8
vmsg	rb MSG_TAILLE
imsg	rb MSG_TAILLE
cwd	rb 256
vname	rb 16
svcname	rb 32
upname	rb 24
