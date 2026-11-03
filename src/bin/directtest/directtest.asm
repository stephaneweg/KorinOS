; ============================================================================
;  directtest - la lecture directe (FS_LIRE_D, docs/ABI.md) rend-elle les bons
;  octets ? Lit un fichier par petits morceaux (la reference), puis le relit
;  par gros morceaux, a diverses positions, vers une region jamais touchee :
;  chaque octet doit etre le meme, et rien ne doit deborder apres la fin.
;	directtest [chemin]
;  Sortie : une ligne par echec, puis « directtest : PASS / FAIL (n) ».
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

REF_MAX		= 0x200000		; on regarde les 2 premiers Mo
REGION		= 0x300000
PETIT		= 4096
GARDE		= 64			; octets relus apres la fin

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,.def
	ldrb	w0,[x19]
	cbnz	w0,.go
.def:	adr	x19,p_defaut
.go:	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x20,x0
	mov	x0,0
	movl	x1,REGION
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.err
	mov	x21,x0				; la reference
	mov	x22,0
.ref:	mov	x0,x20
	add	x1,x21,x22
	mov	x2,PETIT
	sys	SYS_READ
	tbnz	x0,63,.err
	cbz	x0,.lu
	add	x22,x22,x0
	movl	x1,REF_MAX
	cmp	x22,x1
	b.lo	.ref
.lu:	adr	x0,m_ref
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x23,essais
.e:	ldr	x24,[x23]			; position
	ldr	x25,[x23,8]			; octets
	ldr	x26,[x23,16]			; decalage du tampon
	cmn	x24,1
	b.eq	.bilan
	add	x23,x23,24
	cmp	x24,x22				; le fichier est trop court
	b.hs	.e
	mov	x0,x20
	mov	x1,x24
	mov	x2,0
	sys	SYS_SEEK
	mov	x0,0
	movl	x1,REGION
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.err
	mov	x27,x0
	mov	x0,x20
	add	x1,x27,x26
	mov	x2,x25
	sys	SYS_READ
	mov	x28,x0
	sub	x1,x22,x24			; attendu (la reference peut s'arreter avant le fichier)
	cmp	x25,x1
	csel	x5,x25,x1,lo
	cmp	x28,x5
	b.lo	.fauxn
	movl	x1,REF_MAX
	cmp	x22,x1
	b.hs	.cmpr
	cmp	x28,x5
	b.ne	.fauxn
.cmpr:	add	x0,x27,x26
	add	x1,x21,x24
	mov	x2,0
.cmp:	cmp	x2,x5
	b.hs	.garde
	ldrb	w3,[x0,x2]
	ldrb	w4,[x1,x2]
	cmp	w3,w4
	b.ne	.faux
	add	x2,x2,1
	b	.cmp
.garde:	cmp	x5,x28			; (seulement si on a tout compare)
	b.ne	.bon
	add	x3,x2,GARDE
.g:	cmp	x2,x3				; rien apres la fin
	b.hs	.bon
	ldrb	w4,[x0,x2]
	cbnz	w4,.faux
	add	x2,x2,1
	b	.g
.bon:	mov	x0,x27
	movl	x1,REGION
	sys	SYS_VM_UNMAP
	b	.e
.fauxn:	adr	x0,m_n
	b	.dit
.faux:	adr	x0,m_oct
.dit:	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_sep
	bl	puts
	mov	x0,x25
	bl	print_dec
	adr	x0,m_sep
	bl	puts
	mov	x0,x28
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,echecs
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	b	.bon
.bilan:	adr	x0,echecs
	ldr	x19,[x0]
	cbnz	x19,.fail
	adr	x0,m_pass
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.fail:	adr	x0,m_fail
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_fin
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
.err:	mov	x19,x0
	adr	x0,m_err
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,2
	sys	SYS_EXIT

	align 8
essais	dd 0, 65536, 0				; position, octets, decalage du tampon
	dd 0, 1048576, 0			; une requete pleine
	dd 512, 200000, 0			; bouts de secteur a la fin
	dd 1000, 300000, 0			; position hors secteur
	dd 4096, 131072, 8			; tampon a cheval, aligne sur 8
	dd 65536, 1500000, 2048			; plusieurs requetes
	dd 1, 70000, 4
	dd 0, 70000, 1				; tampon non aligne : autre chemin
	dd 511, 524288, 512
	dd 100, 3000000, 0			; plus que le fichier
	dd -1, 0, 0

p_defaut db 'SD0:/bin/fasmarm',0
m_ref	db 'directtest : reference ',0
m_n	db 'directtest : ECHEC taille, position ',0
m_oct	db 'directtest : ECHEC octets, position ',0
m_sep	db ' / ',0
m_pass	db 'directtest : PASS',10,0
m_fail	db 'directtest : FAIL (',0
m_fin	db ')',10,0
m_err	db 'directtest : erreur ',0
m_nl	db 10,0
	align 8

segment readable writeable
kit_data
	align 8
echecs	dd 0
