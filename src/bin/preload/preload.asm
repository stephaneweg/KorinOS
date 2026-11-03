; preload - images des programmes (docs/ABI.md, « Images des executables ») :
;   preload                  liste les images : sorte (epinglee / passagere,
;                            droits), taille du fichier, pages, references,
;                            chemin
;   preload <chemin>...      epingle l'image de chaque programme (SYS_PRELOAD
;                            d'AppKit : lue une fois, gardee tant que le
;                            fichier ne change pas ; unload la retire)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	mov	x21,0				; code de sortie
	cbz	x19,.list
.mot:	ldrb	w0,[x19]			; chaque mot de la ligne
	cmp	w0,' '
	b.ne	.m1
	add	x19,x19,1
	b	.mot
.m1:	cbz	w0,.fin
	mov	x20,x19
.m2:	ldrb	w0,[x19]
	cbz	w0,.m3
	cmp	w0,' '
	b.eq	.m3
	add	x19,x19,1
	b	.m2
.m3:	mov	w22,w0
	strb	wzr,[x19]
	mov	x0,x20
	sys	SYS_PRELOAD
	mov	x23,x0
	adr	x0,m_pre
	bl	lang_puts
	mov	x0,x20
	bl	puts
	tbnz	x23,63,.err
	adr	x0,m_ok
	bl	lang_puts
	mov	x0,x20
	bl	preload_notify
	b	.suite
.err:	adr	x0,m_err
	bl	lang_puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x21,1
.suite:	cbz	w22,.fin
	add	x19,x19,1
	b	.mot
.fin:	mov	x0,x21
	sys	SYS_EXIT

.list:	adr	x0,liste
	mov	x1,LIST_MAX
	sys	SYS_IMAGE_LIST
	tbnz	x0,63,.lerr
	mov	x19,x0
	cbnz	x19,.head
	adr	x0,m_none
	bl	lang_puts
	b	.fin
.head:	adr	x0,m_head
	bl	lang_puts
	adr	x20,liste
.l:	cbz	x19,.fin
	ldr	x0,[x20,IML_KIND]
	adr	x1,m_pin
	tst	x0,IML_PIN
	b.ne	.k
	adr	x1,m_pass
.k:	str	x0,[sp,-16]!
	mov	x0,x1
	bl	lang_puts
	ldr	x0,[sp],16
	tst	x0,IML_SYS
	adr	x0,m_sp
	b.eq	.k2
	adr	x0,m_sys
.k2:	bl	puts
	ldr	x0,[x20,IML_FSIZE]
	lsr	x0,x0,10
	bl	print_dec
	adr	x0,m_ko
	bl	puts
	ldr	x0,[x20,IML_PAGES]
	bl	print_dec
	adr	x0,m_pages
	bl	puts
	ldr	x0,[x20,IML_REFS]
	sub	x0,x0,1
	bl	print_dec
	adr	x0,m_proc
	bl	puts
	add	x0,x20,IML_PATH
	bl	puts
	adr	x0,m_nl
	bl	puts
	add	x20,x20,IML_SIZE
	sub	x19,x19,1
	b	.l
.lerr:	adr	x0,m_lerr
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

LIST_MAX	= 128

include '../../inc/notify.inc'
include '../../inc/preload_notify.inc'

m_pre		db 'preload : ',0
m_ok		db ' : image ',0xC3,0xA9,'pingl',0xC3,0xA9,'e',10,0
m_err		db ' : erreur ',0
m_nl		db 10,0
m_none		db 'preload : aucune image',10,0
m_head		db 'sorte       droits  Ko    pages  process  chemin',10,0
m_pin		db 'epinglee  ',0
m_pass		db 'passagere ',0
m_sys		db '  sys   ',0
m_sp		db '        ',0
m_ko		db '  ',0
m_pages		db '  ',0
m_proc		db '  ',0
m_lerr		db 'preload : liste des images refus',0xC3,0xA9,'e',10,0
		align 8

segment readable writeable
kit_data
		align 8
liste		rb LIST_MAX * IML_SIZE
