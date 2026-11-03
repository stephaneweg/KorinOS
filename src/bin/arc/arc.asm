; arc - archives en ligne de commande, sur ArchiveKit (docs/OUTILS.md, docs/KITS.md)
;   arc l archive                  liste : taille, taille compressee, nom
;   arc t archive                  lit tout, verifie les sommes
;   arc x archive [dossier]        extrait tout (dossier courant par defaut)
;   arc c archive [-N] fichiers    cree (format d'apres l'extension : .zip .tar .tgz .tar.gz .gz ;
;                                  -0 stocke ... -9 ; un dossier est pris avec ce qu'il contient)
;   arc f                          formats connus
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../kits/archivekit/archivekit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/archivekit/archivekit.abi'		; SD0:/lib/archivekit.so

start:
	bl	t_debut
	cmp	x19,2
	b.lo	.usage
	ldr	x0,[x20,8]
	ldrb	w1,[x0]
	ldrb	w0,[x0,1]
	cbnz	w0,.usage
	cmp	w1,'f'
	b.eq	do_formats
	cmp	x19,3
	b.lo	.usage
	cmp	w1,'l'
	b.eq	do_list
	cmp	w1,'t'
	b.eq	do_test
	cmp	w1,'x'
	b.eq	do_extract
	cmp	w1,'c'
	b.eq	do_create
.usage:	adr	x0,m_usage
	bl	t_err_msg
	mov	x0,2
	bl	t_fin

; x0 = erreur, x1 = chemin ou 0 : le dit, code 1
fail:
	mov	x21,x1
	mov	x22,x0
	ldr	x0,[x20]
	bl	t_err
	adr	x0,m_sep
	bl	t_derr
	cbz	x21,.m
	mov	x0,x21
	bl	t_err
	adr	x0,m_sep
	bl	t_derr
.m:	mov	x0,x22
	bl	ar_strerror
	bl	t_err
	adr	x0,s_t_nl
	bl	t_err
	mov	x0,1
	bl	t_fin

; -> x21 = archive ouverte (argv[2])
open_arg:
	prologue
	ldr	x0,[x20,16]
	bl	ar_open
	tbz	x0,63,.ok
	ldr	x1,[x20,16]
	b	fail
.ok:	mov	x21,x0
	epilogue

do_list:
	bl	open_arg
	mov	x22,0
.e:	mov	x0,x21
	bl	ar_count
	cmp	x22,x0
	b.hs	.end
	mov	x0,x21
	mov	x1,x22
	adr	x2,info
	bl	ar_entry
	adr	x23,info
	ldr	x0,[x23,ARE_SIZE]
	bl	put_w
	ldr	x0,[x23,ARE_PACKED]
	bl	put_w
	ldr	x0,[x23,ARE_DIR]
	mov	w1,' '
	mov	w2,'d'
	cmp	x0,0
	csel	w0,w2,w1,ne
	bl	t_putc
	mov	w0,' '
	bl	t_putc
	ldr	x0,[x23,ARE_NAME]
	bl	t_puts
	bl	t_nl
	add	x22,x22,1
	b	.e
.end:	mov	x0,x22
	bl	t_putn
	adr	x0,m_entries
	bl	t_dire
	mov	x0,x21
	bl	ar_format
	bl	ar_format_name
	bl	t_puts
	bl	t_nl
	mov	x0,x21
	bl	ar_close
	mov	x0,0
	bl	t_fin

; x0 = nombre : sur 12 colonnes
put_w:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_dec
	mov	x2,sp
	sub	x2,x0,x2
	mov	x3,12
	sub	x3,x3,x2
	stp	x3,xzr,[sp,-16]!
.b:	ldr	x3,[sp]
	cmp	x3,0
	b.le	.t
	sub	x3,x3,1
	str	x3,[sp]
	mov	w0,' '
	bl	t_putc
	b	.b
.t:	add	sp,sp,16
	mov	x0,sp
	bl	t_puts
	mov	w0,' '
	bl	t_putc
	add	sp,sp,32
	epilogue

do_test:
	bl	open_arg
	mov	x22,0
	mov	x23,0				; mauvaises
.e:	mov	x0,x21
	bl	ar_count
	cmp	x22,x0
	b.hs	.end
	mov	x0,x21
	mov	x1,x22
	bl	ar_test
	cbz	x0,.n
	add	x23,x23,1
	mov	x24,x0
	mov	x0,x21
	mov	x1,x22
	bl	ar_name
	bl	t_puts
	adr	x0,m_sep
	bl	t_dire
	mov	x0,x24
	bl	ar_strerror
	bl	t_puts
	bl	t_nl
.n:	add	x22,x22,1
	b	.e
.end:	mov	x0,x22
	bl	t_putn
	adr	x0,m_tested
	bl	t_dire
	mov	x0,x23
	bl	t_putn
	adr	x0,m_bad
	bl	t_dire
	mov	x0,x21
	bl	ar_close
	cmp	x23,0
	cset	x0,ne
	bl	t_fin

do_extract:
	bl	open_arg
	adr	x22,s_dot
	cmp	x19,4
	b.lo	.go
	ldr	x22,[x20,24]
.go:	mov	x0,x21
	mov	x1,x22
	bl	ar_extract_all
	tbz	x0,63,.ok
	ldr	x1,[x20,16]
	b	fail
.ok:	bl	t_putn
	adr	x0,m_files
	bl	t_dire
	mov	x0,x21
	bl	ar_close
	mov	x0,0
	bl	t_fin

; x0 = chemin -> AR_FMT_* d'apres son extension (ZIP sinon)
fmt_of:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,e_tgz
	bl	str_ends
	mov	x20,AR_FMT_TGZ
	cbnz	x0,.yes
	mov	x0,x19
	adr	x1,e_targz
	bl	str_ends
	cbnz	x0,.yes
	mov	x0,x19
	adr	x1,e_tar
	bl	str_ends
	mov	x20,AR_FMT_TAR
	cbnz	x0,.yes
	mov	x0,x19
	adr	x1,e_gz
	bl	str_ends
	mov	x20,AR_FMT_GZIP
	cbnz	x0,.yes
	mov	x20,AR_FMT_ZIP
.yes:	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

do_create:
	ldr	x0,[x20,16]
	bl	fmt_of
	mov	x1,x0
	ldr	x0,[x20,16]
	bl	ar_create
	tbz	x0,63,.ok
	ldr	x1,[x20,16]
	b	fail
.ok:	mov	x21,x0
	mov	x22,3				; argument
	mov	x23,-1				; niveau
.a:	cmp	x22,x19
	b.hs	.end
	ldr	x24,[x20,x22,lsl 3]
	add	x22,x22,1
	ldrb	w0,[x24]
	cmp	w0,'-'
	b.ne	.f
	ldrb	w0,[x24,1]
	sub	w0,w0,'0'
	cmp	w0,9
	b.hi	.f
	mov	x23,x0
	b	.a
.f:	mov	x0,x21
	mov	x1,0
	mov	x2,x24
	mov	x3,x23
	bl	ar_add_file
	tbz	x0,63,.a
	mov	x1,x24
	b	fail
.end:	mov	x0,x21
	bl	ar_finish
	tbz	x0,63,.done
	ldr	x1,[x20,16]
	b	fail
.done:	bl	t_putn
	adr	x0,m_bytes
	bl	t_dire
	mov	x0,x21
	bl	ar_free
	mov	x0,0
	bl	t_fin

do_formats:
	adr	x0,fmts
	mov	x1,8
	bl	ar_formats
	mov	x21,x0
	adr	x22,fmts
.e:	cbz	x21,.end
	add	x0,x22,ARFI_NAME
	bl	t_puts
	adr	x0,m_sep
	bl	t_dire
	add	x0,x22,ARFI_EXT
	bl	t_puts
	bl	t_nl
	add	x22,x22,ARFI_SIZE
	sub	x21,x21,1
	b	.e
.end:	mov	x0,0
	bl	t_fin

m_usage		db 'arc l|t|x|c|f archive [dossier | [-N] fichiers]',0
m_sep		db ' : ',0
m_entries	db ' entrees, ',0
m_tested	db ' entrees lues, ',0
m_bad		db ' mauvaises',10,0
m_files		db ' fichiers extraits',10,0
m_bytes		db ' octets',10,0
s_dot		db '.',0
e_tgz		db '.tgz',0
e_targz		db '.tar.gz',0
e_tar		db '.tar',0
e_gz		db '.gz',0
		align	4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
		align	8
info		rb AR_ENTRY_SIZE
fmts		rb ARFI_SIZE*8
