; exfattest - exerce le pilote exFAT (/sd2) depuis EL0 : listes, lecture de
; fichiers contigus (NoFatChain) et chaines, recherche insensible a la casse
; (accents compris), ecriture, conversion contigu -> chaine, troncature,
; mkdir, unlink. Code de sortie = nombre d'echecs.
; Ecrit sur SD1: : ne tourne que sur la partition de build/sd.img, reconnue a
; son gros.bin (tools/exfatdata.py), jamais sur la partition personnelle.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

PAT_MUL		= 0x9E3779B1		; le motif : tools/exfatdata.py le calcule aussi

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,p_gros			; la partition d'essai ?
	adr	x1,statbuf
	sys	SYS_STAT
	cbnz	x0,.refuse
	adr	x0,statbuf
	ldr	x0,[x0]
	movl	x1,300000
	cmp	x0,x1
	b.eq	.go
.refuse:
	adr	x0,m_refuse
	bl	puts
	mov	x0,100
	sys	SYS_EXIT
.go:	adr	x0,p_root
	bl	list_dir
	adr	x0,p_sd2
	bl	list_dir
	adr	x0,p_dossier
	bl	list_dir

	adr	x0,p_lisez			; lecture simple
	bl	cat_file
	adr	x0,p_lisez_up			; meme fichier, autre casse
	bl	cat_file
	adr	x0,p_profond_up			; "DOSSIER ÉTÉ/SOUS/PROFOND.TXT"
	bl	cat_file
	adr	x0,p_long
	bl	cat_file

	adr	x0,p_gros			; contigu
	mov	x1,7
	mov	x2,0
	movl	x3,300000
	bl	check_pattern
	adr	x0,p_chaine			; chaine FAT fragmentee
	mov	x1,13
	mov	x2,5
	movl	x3,50000
	bl	check_pattern

	bl	write_test
	bl	convert_test
	bl	dir_test

	adr	x0,p_sd2
	bl	list_dir
	adr	x0,m_done
	bl	puts
	adr	x0,errors
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,errors
	ldr	x0,[x0]
	sys	SYS_EXIT

; x0 = chemin : affiche le repertoire
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
	mov	x19,x0
	adr	x0,m_cat
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

; x0 = chemin, x1 = a, x2 = b, x3 = taille attendue : octet[i] = les 8 bits hauts
; de (i*a + b) * PAT_MUL sur 32 bits (ne se repete pas d'un cluster a l'autre)
check_pattern:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x23,x1
	mov	x24,x2
	mov	x25,x3
	mov	x26,x0
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
	madd	x3,x3,x23,x24
	movl	w4,PAT_MUL
	mul	w3,w3,w4
	lsr	w3,w3,24
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
	adr	x0,m_chk
	bl	puts
	mov	x0,x26
	bl	puts
	adr	x0,m_chk2
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_chk3
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	cmp	x20,x25
	b.ne	.fail
	cbnz	x21,.fail
	b	.out
.fail:	bl	fail
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; fichier a nom long accentue, 5000 lignes (plusieurs clusters), reecriture
; du debut par seek, verification de taille et relecture
write_test:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,p_new
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x20,0
.w:	mov	x0,x19
	adr	x1,line
	mov	x2,line_len
	sys	SYS_WRITE
	cmp	x0,line_len
	b.ne	.fail
	add	x20,x20,1
	movl	x0,5000
	cmp	x20,x0
	b.lo	.w
	mov	x0,x19
	mov	x1,0
	mov	x2,0
	sys	SYS_SEEK
	mov	x0,x19
	adr	x1,first
	mov	x2,first_len
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_new
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
	adr	x0,p_new
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	adr	x1,buf
	mov	x2,first_len+20
	sys	SYS_READ
	adr	x1,buf
	strb	wzr,[x1,x0]
	adr	x0,m_head
	bl	puts
	adr	x0,buf
	bl	puts
	adr	x0,m_nl
	bl	puts
	mov	x0,x19
	sys	SYS_CLOSE
	b	.out
.fail:	bl	fail
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = fd, x1 = premier octet (rang), x2 = nombre : le motif de check_pattern,
; a = 3, b = 0
write_pattern:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
.chunk:	cbz	x21,.done
	mov	x22,1000
	cmp	x21,x22
	csel	x22,x21,x22,lo
	adr	x1,buf
	mov	x2,0
.f:	add	x3,x20,x2
	add	x3,x3,x3,lsl 1
	movl	w4,PAT_MUL
	mul	w3,w3,w4
	lsr	w3,w3,24
	strb	w3,[x1,x2]
	add	x2,x2,1
	cmp	x2,x22
	b.lo	.f
	mov	x0,x19
	mov	x2,x22
	sys	SYS_WRITE
	cmp	x0,x22
	b.ne	.bad
	add	x20,x20,x22
	sub	x21,x21,x22
	b	.chunk
.bad:	bl	fail
.done:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; A (1 cluster) puis B colle derriere, puis A grandit : plus de place
; contigue -> conversion en chaine FAT. Verification, troncature, nettoyage.
convert_test:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,p_a
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x1,0
	mov	x2,4000
	bl	write_pattern
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_b
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x1,0
	mov	x2,6000
	bl	write_pattern
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_a				; A += 20000 octets
	mov	x1,O_WRITE or O_APPEND
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x1,4000
	mov	x2,20000
	bl	write_pattern
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_a
	mov	x1,3
	mov	x2,0
	movl	x3,24000
	bl	check_pattern
	adr	x0,p_b
	mov	x1,3
	mov	x2,0
	movl	x3,6000
	bl	check_pattern
	adr	x0,p_a				; troncature puis 100 octets
	mov	x1,O_WRITE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x1,0
	mov	x2,100
	bl	write_pattern
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_a
	mov	x1,3
	mov	x2,0
	mov	x3,100
	bl	check_pattern
	adr	x0,p_b				; la troncature ne lui a rien pris
	mov	x1,3
	mov	x2,0
	movl	x3,6000
	bl	check_pattern
	adr	x0,p_b
	sys	SYS_UNLINK
	cbnz	x0,.fail
	b	.out
.fail:	bl	fail
.out:	ldp	x19,x20,[sp],16
	epilogue

; mkdir, fichier dedans (chemin relatif), unlink refuse sur repertoire non vide
dir_test:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,p_dir
	sys	SYS_MKDIR
	cbnz	x0,.fail
	adr	x0,p_dir
	sys	SYS_CHDIR
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
	adr	x0,p_rel
	bl	cat_file
	adr	x0,p_dir
	sys	SYS_UNLINK			; non vide : -39 attendu
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
	adr	x0,p_dir2			; un repertoire conserve, pour fsck
	sys	SYS_MKDIR
	cbnz	x0,.fail
	adr	x0,p_in2
	mov	x1,O_WRITE or O_CREATE
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	adr	x1,line
	mov	x2,line_len
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
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
p_sd2		db 'SD1:/',0
p_dossier	db 'SD1:/Dossier ',0xC3,0xA9,'t',0xC3,0xA9,0
p_lisez		db 'SD1:/Lisez-moi exFAT.txt',0
p_lisez_up	db 'SD1:/LISEZ-MOI EXFAT.TXT',0
p_profond_up	db 'SD1:/DOSSIER ',0xC3,0x89,'T',0xC3,0x89,'/SOUS/PROFOND.TXT',0
p_long		db 'SD1:/Dossier ',0xC3,0xA9,'t',0xC3,0xA9,'/Un nom de fichier tr',0xC3,0xA8,'s tr',0xC3,0xA8,'s long pour tester les entr',0xC3,0xA9,'es exFAT.txt',0
p_gros		db 'SD1:/gros.bin',0
p_chaine	db 'SD1:/chaine fragmentee.bin',0
p_new		db 'SD1:/',0xC3,0x89,'crit par ARMPI sur exFAT.txt',0
p_a		db 'SD1:/A.bin',0
p_b		db 'SD1:/B.bin',0
p_dir		db 'SD1:/nouveau dossier',0
p_dir2		db 'SD1:/Cr',0xC3,0xA9,0xC3,0xA9,' par ARMPI',0
p_in2		db 'SD1:/Cr',0xC3,0xA9,0xC3,0xA9,' par ARMPI/fichier.txt',0
p_rel		db 'dedans.txt',0
p_dot		db '.',0
p_up		db '..',0
m_ls		db '--- ',0
m_indent	db '    ',0
m_dir		db '/',10,0
m_sp		db '  ',0
m_bytes		db ' o',10,0
m_cat		db '--- cat ',0
m_chk		db 'exfattest : ',0
m_chk2		db ' : ',0
m_chk3		db ' octets, erreurs ',0
m_size		db 'exfattest : fichier ',0xC3,0xA9,'crit, taille ',0
m_head		db 'exfattest : relu : ',0
m_done		db 'exfattest : termin',0xC3,0xA9,', ',0xC3,0xA9,'checs = ',0
m_fail		db 'exfattest : *** ECHEC ***',10,0
m_refuse	db 'exfattest : SD1: n',39,'est pas la partition d',39,'essai, rien n',39,'est ecrit',10,0
m_nl		db 10,0
first		db 'PREMIERE LIGNE REECRITE PAR SEEK',10
first_len	= $ - first
line		db 'Ligne de test ecrite par exfattest depuis EL0.',10
line_len	= $ - line
		align 8
errors		dd 0
statbuf		dd 0, 0
dirent		rb DIRENT_BYTES
buf		rb 1024
