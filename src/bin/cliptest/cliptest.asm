; cliptest - essai du presse-papiers (clipd, ui_clip_* d'UIKit, sans fenetre) :
; une image, des fichiers coupes, une copie a deux representations, 12 textes
; (l'anneau en garde 10), relus et affiches sur la console.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'

start:
	bl	kits_open
	adr	x0,s_svc
	movl	x1,30000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x0,g_px				; une image de 64 x 48
	mov	x1,0
	movl	w2,0x00336699
.p:	str	w2,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,64*48
	b.lo	.p
	mov	x19,0				; 12 textes
.t:	adr	x0,g_txt
	adr	x1,s_copy
	strb	wzr,[x0]
	bl	str_cat
	mov	x1,x0
	mov	x0,x19
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,g_txt
	mov	x1,-1
	bl	ui_clip_set
	add	x19,x19,1
	cmp	x19,12
	b.lo	.t
	adr	x0,s_rtf			; deux representations
	adr	x1,s_rich
	mov	x2,11
	mov	x3,0
	bl	ui_clip_put
	mov	x19,x0
	adr	x0,s_text
	adr	x1,s_plain
	mov	x2,5
	mov	x3,x19
	bl	ui_clip_put
	adr	x0,s_l1
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,g_buf
	mov	x1,256
	bl	ui_clip_get
	adr	x0,s_l2
	bl	puts
	adr	x0,g_buf
	bl	puts
	adr	x0,s_fmts
	adr	x1,g_buf
	mov	x2,255
	bl	ui_clip_take
	adr	x2,g_buf
	strb	wzr,[x2,x0]
	adr	x0,s_l3
	bl	puts
	adr	x0,g_buf
	bl	puts
	adr	x0,s_files
	mov	x1,1
	bl	ui_clip_set_files
	adr	x0,g_buf
	mov	x1,256
	bl	ui_clip_get_files
	mov	x19,x1
	adr	x0,s_l4
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,s_nl
	bl	puts
	adr	x0,g_buf
	bl	puts
	adr	x0,g_px
	mov	x1,64
	mov	x2,48
	bl	ui_clip_set_image
	adr	x0,g_px2
	mov	x1,64*48*4
	bl	ui_clip_get_image
	mov	x19,x0
	mov	x20,x1
	adr	x0,s_l5
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,s_nl
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,s_nl
	bl	puts
	adr	x0,g_px2
	ldr	w0,[x0,4*(64*48-1)]
	bl	print_dec
	adr	x0,s_nl
	bl	puts
quit:	mov	x0,0
	sys	SYS_EXIT

s_svc	db 'clipboard',0
s_copy	db 'copie ',0
s_rtf	db 'rtf',0
s_text	db 'text',0
s_rich	db '{\rtf1 a b}',0
s_plain	db 'a b c',0
s_fmts	db 'rtf',0,'text',0,0
s_files	db 'SD0:/home/documents/lettre.txt',10,'SD0:/home/music/chanson.mp3',10,0
s_l1	db 'id ',0
s_l2	db 10,'texte : ',0
s_l3	db 10,'rtf : ',0
s_l4	db 10,'fichiers, coupes : ',0
s_l5	db 10,'image : ',0
s_nl	db 10,0
	align 8

segment readable writeable
kit_data
	align 8
g_txt	rb 32
g_buf	rb 256
g_px	rw 64*48
g_px2	rw 64*48
