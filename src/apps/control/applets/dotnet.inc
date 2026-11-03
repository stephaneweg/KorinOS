; applets/dotnet.inc - applet Assemblys .NET du panneau de configuration : les
; DLL de SD0:/dotnet, une case a cocher par DLL. Cochee, elle est gardee en
; memoire et ses pages partagees entre les programmes (docs/DOTNET.md). Appliquer
; ecrit SD0:/etc/dllpreload.txt et lance dllpreload, qui accorde la memoire au
; fichier et le refera a chaque demarrage.

DN_MAX	= 200				; assemblys
DN_NOM	= 64
DN_LIB	= 80
DN_FILE	= DN_MAX * DN_NOM

dn_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,ap_colw			; (noms longs : une seule colonne)
	mov	x1,AP_COLS*COL_W
	str	x1,[x0]
	adr	x0,dn_n
	str	xzr,[x0]
	adr	x0,dn_p_file			; la liste
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x19,0
	tbnz	x0,63,.lu
	mov	x20,x0
	adr	x1,dn_file
	mov	x2,DN_FILE-1
	sys	SYS_READ
	cmp	x0,0
	csel	x19,x0,xzr,gt
	mov	x0,x20
	sys	SYS_CLOSE
.lu:	adr	x0,dn_file
	strb	wzr,[x0,x19]
	adr	x0,dn_p_dir			; les DLL
	mov	x1,O_READ
	sys	SYS_OPEN
	adr	x2,dn_s_absent
	tbnz	x0,63,.msg
	mov	x19,x0
.d:	mov	x0,x19
	adr	x1,ap_dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.df
	bl	dn_add
	b	.d
.df:	mov	x0,x19
	sys	SYS_CLOSE
	bl	dn_sort
	mov	x19,0
.r:	adr	x0,dn_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.note
	adr	x20,dn_noms
	mov	x0,DN_NOM
	madd	x20,x19,x0,x20			; son nom
	adr	x21,dn_libs
	mov	x0,DN_LIB
	madd	x21,x19,x0,x21			; son libelle
	mov	x0,x20
	bl	dn_label
	mov	x0,x20
	bl	dn_listed
	mov	x3,x0
	mov	x0,x21
	mov	x1,R_CASE
	mov	x2,0
	mov	x4,x19
	bl	applet_row
	add	x19,x19,1
	b	.r
.note:	adr	x2,dn_s_note
.msg:	adr	x0,ap_msg
	str	x2,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ap_dirent : retenue si c'est une DLL
dn_add:
	prologue
	adr	x0,ap_dirent
	bl	str_len
	adr	x1,dn_n
	ldr	x2,[x1]
	cmp	x2,DN_MAX
	b.hs	.out
	cmp	x0,DN_NOM
	b.hs	.out
	cmp	x0,5
	b.lo	.out
	adr	x3,ap_dirent
	add	x3,x3,x0
	ldur	w4,[x3,-4]			; « .dll », aux majuscules pres
	movl	w5,0x20202000
	orr	w4,w4,w5
	movl	w5,'.' or ('d' shl 8) or ('l' shl 16) or ('l' shl 24)
	cmp	w4,w5
	b.ne	.out
	add	x3,x2,1
	str	x3,[x1]
	adr	x0,dn_noms
	mov	x3,DN_NOM
	madd	x0,x2,x3,x0
	adr	x1,ap_dirent
	bl	str_cat
.out:	epilogue

; x0 = nom a son rang dans dn_noms, x1 = celui d'apres -> x0 = 1 s'il faut echanger
dn_after:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.hi	.oui
	b.lo	.non
	cbnz	w2,.c
.non:	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; dn_noms par ordre alphabetique (insertion)
dn_sort:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,1
.i:	adr	x0,dn_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	mov	x20,x19
.j:	cbz	x20,.n
	adr	x8,dn_noms
	mov	x0,DN_NOM
	madd	x9,x20,x0,x8			; celui-ci
	sub	x8,x9,DN_NOM			; le precedent
	mov	x0,x8
	mov	x1,x9
	bl	dn_after
	cbz	x0,.n
	mov	x0,0
.s:	ldr	x1,[x8,x0]
	ldr	x2,[x9,x0]
	str	x2,[x8,x0]
	str	x1,[x9,x0]
	add	x0,x0,8
	cmp	x0,DN_NOM
	b.lo	.s
	sub	x20,x20,1
	b	.j
.n:	add	x19,x19,1
	b	.i
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom, x21 = libelle a remplir : sa taille, puis le nom sans .dll
dn_label:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,dn_path
	adr	x1,dn_p_dir2
	bl	str_cat
	mov	x1,x19
	bl	str_cat
	adr	x1,dn_st
	str	xzr,[x1]
	adr	x0,dn_path
	bl	f_stat
	mov	x0,x21
	adr	x1,dn_st
	ldr	x1,[x1]
	movl	x2,0x100000
	adr	x3,dn_s_mo
	cmp	x1,x2
	b.hs	.mo
	add	x1,x1,1023
	lsr	x1,x1,10
	adr	x3,dn_s_ko
	b	.ec
.mo:	lsr	x1,x1,20
.ec:	mov	x2,1000				; en decimal, sans zeros devant
	mov	x4,0				; un chiffre ecrit
	mov	x6,10
.ch:	udiv	x5,x1,x2
	msub	x1,x5,x2,x1
	orr	x4,x4,x5
	cmp	x2,1
	b.eq	.w
	cbz	x4,.sk
.w:	add	w5,w5,'0'
	strb	w5,[x0],1
.sk:	cmp	x2,1
	b.eq	.fin
	udiv	x2,x2,x6
	b	.ch
.fin:	mov	x1,x3
	bl	tr_cat
	mov	x1,x19
	bl	str_cat
	sub	x0,x0,4
	strb	wzr,[x0]
	ldr	x19,[sp],16
	epilogue

; x0 = nom -> x0 = 1 s'il est dans dn_file
dn_listed:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	adr	x19,dn_file
.l:	ldrb	w0,[x19]
	cbz	w0,.non
	mov	x20,x19
.f:	ldrb	w0,[x20]
	cbz	w0,.lu
	cmp	w0,10
	b.eq	.lu
	add	x20,x20,1
	b	.f
.lu:	mov	x0,x19
	mov	x1,x20
	bl	pl_trim
	mov	x19,x20
	ldrb	w2,[x19]
	cbz	w2,.c0
	add	x19,x19,1
.c0:	sub	x2,x1,x0
	cbz	x2,.l
	mov	x3,0
.c:	ldrb	w4,[x21,x3]
	cmp	x3,x2
	b.hs	.bout
	ldrb	w5,[x0,x3]
	cmp	w4,w5
	b.ne	.l
	add	x3,x3,1
	b	.c
.bout:	cbnz	w4,.l
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Appliquer : le fichier reecrit, dllpreload lance
dn_appliquer:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,dn_n
	ldr	x0,[x0]
	adr	x2,dn_s_absent
	cbz	x0,.msg
	adr	x20,dn_file
	strb	wzr,[x20]
	mov	x19,0
.r:	adr	x0,nrows
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.ecrit
	mov	x0,x19
	add	x19,x19,1
	bl	row_ptr
	ldrb	w1,[x0,8]
	cmp	w1,R_CASE
	b.ne	.r
	ldrb	w1,[x0,10]
	cbz	w1,.r
	ldr	x1,[x0,16]
	adr	x0,dn_noms
	mov	x2,DN_NOM
	madd	x1,x1,x2,x0
	mov	x0,x20
	bl	str_cat
	adr	x1,s_crlf
	bl	str_cat
	mov	x20,x0
	b	.r
.ecrit:	adr	x0,dn_p_file
	adr	x1,dn_file
	bl	file_write
	adr	x0,dn_p_tool
	adr	x1,dn_s_vide
	sys	SYS_SPAWN
	adr	x2,dn_s_fait
.msg:	adr	x0,ap_msg
	str	x2,[x0]
	ldp	x19,x20,[sp],16
	epilogue

dn_titre	db 'Assemblys .NET',0
dn_s_liste	db '.NET',0
dn_s_ko		db ' Ko  ',0
dn_s_mo		db ' Mo  ',0
dn_s_vide	db 0
dn_s_note	db 'Coch',0xC3,0xA9,'es : gard',0xC3,0xA9,'es en m',0xC3,0xA9,'moire.',0
dn_s_fait	db 'Chargement en cours.',0
dn_s_absent	db '.NET n',39,'est pas sur cette carte.',0
dn_p_file	db 'SD0:/etc/dllpreload.txt',0
dn_p_dir	db 'SD0:/dotnet',0
dn_p_dir2	db 'SD0:/dotnet/',0
dn_p_tool	db 'SD0:/dotnet/dllpreload',0
		align 4

macro dn_data {
		align 8
dn_n		dd 0				; assemblys
dn_st		rb 32				; f_stat
dn_path		rb 128
dn_noms		rb DN_MAX * DN_NOM
dn_libs		rb DN_MAX * DN_LIB
dn_file		rb DN_FILE
		align 8
}
