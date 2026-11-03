; fstest - exerce le systeme de fichiers depuis EL0 : listes, lecture,
; creation (nom long accentue), ecriture, relecture, seek, mkdir, unlink
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,p_root			; listes
	bl	list_dir
	adr	x0,p_sd
	bl	list_dir
	adr	x0,p_docs
	bl	list_dir

	adr	x0,m_cat			; lecture d'un petit fichier
	bl	puts
	adr	x0,p_readme
	bl	cat_file

	bl	big_check			; 300 000 octets verifies
	bl	write_test
	bl	dir_test

	adr	x0,m_done
	bl	puts
	adr	x0,errors
	ldr	x0,[x0]
	sys	SYS_EXIT

; x0 = chemin : affiche le contenu du repertoire
list_dir:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,m_ls
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_nl
	bl	puts
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
.next:	mov	x0,x19
	adr	x1,dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.end
	adr	x0,m_indent
	bl	puts
	adr	x0,dirent
	bl	puts
	adr	x1,dirent
	ldr	x0,[x1,DIRENT_ATTR]
	tst	x0,ATTR_DIR
	b.eq	.file
	adr	x0,m_dir
	bl	puts
	b	.next
.file:	adr	x0,m_sp
	bl	puts
	adr	x1,dirent
	ldr	x0,[x1,DIRENT_SIZE]
	bl	print_dec
	adr	x0,m_bytes
	bl	puts
	b	.next
.end:	mov	x0,x19
	sys	SYS_CLOSE
	b	.out
.fail:	bl	fail
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin : affiche le fichier
cat_file:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
.r:	mov	x0,x19
	adr	x1,buf
	mov	x2,255
	sys	SYS_READ
	cmp	x0,0
	b.le	.eof
	adr	x1,buf
	strb	wzr,[x1,x0]
	mov	x0,x1
	bl	puts
	b	.r
.eof:	mov	x0,x19
	sys	SYS_CLOSE
	b	.out
.fail:	bl	fail
.out:	ldp	x19,x20,[sp],16
	epilogue

; lit gros.bin par blocs de 1000 o et verifie octet[i] = (i*7) & 255
big_check:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,p_big
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x20,0				; position
	mov	x21,0				; erreurs
.r:	mov	x0,x19
	adr	x1,buf
	mov	x2,1000
	sys	SYS_READ
	cmp	x0,0
	b.le	.eof
	mov	x22,x0
	adr	x1,buf
	mov	x2,0
.v:	add	x3,x20,x2
	mov	x4,7
	mul	x3,x3,x4
	and	x3,x3,255
	ldrb	w4,[x1,x2]
	cmp	w3,w4
	cinc	x21,x21,ne
	add	x2,x2,1
	cmp	x2,x22
	b.lo	.v
	add	x20,x20,x22
	b	.r
.eof:	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,m_big
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_big2
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	movl	x0,300000
	cmp	x20,x0
	b.ne	.fail
	cbnz	x21,.fail
	b	.out
.fail:	bl	fail
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; cree un fichier a nom long, y ecrit 3 fois le message (dont 1 apres seek),
; le relit
write_test:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,p_new
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x20,0
.w:	mov	x0,x19				; 5000 lignes : plusieurs clusters
	adr	x1,line
	mov	x2,line_len
	sys	SYS_WRITE
	cmp	x0,line_len
	b.ne	.fail
	add	x20,x20,1
	movl	x0,5000
	cmp	x20,x0
	b.lo	.w
	mov	x0,x19				; reecriture du debut
	mov	x1,0
	mov	x2,0
	sys	SYS_SEEK
	mov	x0,x19
	adr	x1,first
	mov	x2,first_len
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_new			; taille attendue
	adr	x1,statbuf
	sys	SYS_STAT
	cbnz	x0,.fail
	adr	x0,m_size
	bl	puts
	adr	x0,statbuf
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,statbuf
	ldr	x0,[x0]
	movl	x1,5000*line_len
	cmp	x0,x1
	b.ne	.fail
	adr	x0,p_new			; relecture du debut
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	adr	x1,buf
	mov	x2,line_len*2
	sys	SYS_READ
	adr	x1,buf
	strb	wzr,[x1,x0]
	adr	x0,m_head
	bl	puts
	adr	x0,buf
	bl	puts
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_docs
	bl	list_dir
	b	.out
.fail:	bl	fail
.out:	ldp	x19,x20,[sp],16
	epilogue

; mkdir, fichier dedans, unlink refuse sur repertoire non vide, puis nettoyage
dir_test:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,p_dir
	sys	SYS_MKDIR
	cbnz	x0,.fail
	adr	x0,p_dir
	sys	SYS_CHDIR			; chemins relatifs
	cbnz	x0,.fail
	adr	x0,p_rel
	mov	x1,O_WRITE or O_CREATE
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	adr	x1,first
	mov	x2,first_len
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_dot
	bl	list_dir
	adr	x0,p_dir
	sys	SYS_UNLINK			; non vide : doit echouer (-39)
	cmn	x0,39
	b.ne	.fail
	adr	x0,p_rel
	sys	SYS_UNLINK
	cbnz	x0,.fail
	adr	x0,p_up
	sys	SYS_CHDIR
	adr	x0,p_dir
	sys	SYS_UNLINK
	cbnz	x0,.fail
	adr	x0,p_sd
	bl	list_dir
	b	.out
.fail:	bl	fail
.out:	ldp	x19,x20,[sp],16
	epilogue

fail:
	prologue
	adr	x0,errors
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	adr	x0,m_fail
	bl	puts
	epilogue


segment readable writeable
kit_data

p_root		db '/',0
p_sd		db 'SD0:/',0
p_docs		db 'SD0:/docs',0
p_readme	db 'SD0:/README.TXT',0
p_big		db 'SD0:/docs/notes/gros.bin',0
p_new		db 'SD0:/docs/Fichier cr',0xC3,0xA9,0xC3,0xA9,' par ARMPI.txt',0
p_dir		db 'SD0:/essai',0
p_rel		db 'dedans.txt',0
p_dot		db '.',0
p_up		db '..',0
m_ls		db '--- ',0
m_indent	db '    ',0
m_dir		db '/',10,0
m_sp		db '  ',0
m_bytes		db ' o',10,0
m_cat		db '--- SD0:/README.TXT :',10,0
m_big		db 'fstest : gros.bin lu ',0
m_big2		db ' octets, erreurs ',0
m_size		db 'fstest : fichier ',0xC3,0xA9,'crit, taille ',0
m_head		db 'fstest : relu : ',0
m_done		db 'fstest : termin',0xC3,0xA9,10,0
m_fail		db 'fstest : *** ECHEC ***',10,0
m_nl		db 10,0
first		db 'PREMIERE LIGNE REECRITE PAR SEEK',10
first_len	= $ - first
line		db 'Ligne de test ecrite par fstest depuis EL0.',10
line_len	= $ - line
		align 8
errors		dd 0
statbuf		dd 0, 0
dirent		rb DIRENT_BYTES
buf		rb 1024
