; screenshot - capture de l'ecran compose (docs/RESEAU.md 5.3, etape 10) dans
; un fichier BMP 32 bits (op WS_OP_SNAPSHOT d'Elegant : tel qu'a l'ecran,
; curseur compris).
;   screenshot [fichier]       (RAM0:/screen.bmp par defaut)
; Puis, par exemple, ftp : RETR /RAM0/screen.bmp.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

BLOC		= 65536

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	adr	x20,p_defaut
	cbz	x19,.ws
	ldrb	w0,[x19]
	cbz	w0,.ws
	mov	x20,x19
.ws:	adr	x0,msg				; l'ecran entier (generation 0)
	mov	x1,WS_OP_SNAPSHOT
	str	x1,[x0,WM_OP]
	str	xzr,[x0,WM_B]
	str	wzr,[x0,WM_LEN]
	sys	SYS_WS_CALL
	tbnz	x0,63,.ko_ws
	adr	x9,msg
	ldp	x21,x22,[x9,WM_A]		; largeur, hauteur
	ldr	x0,[x9,WM_D]			; le tampon
	sys	SYS_BUF_MAP
	tbnz	x0,63,.ko_ws
	mov	x23,x0
	adr	x0,bmp				; en-tete BMP (54 octets)
	mov	w1,'B'
	strb	w1,[x0]
	mov	w1,'M'
	strb	w1,[x0,1]
	mul	x24,x21,x22
	lsl	x24,x24,2			; octets des pixels
	add	x1,x24,54			; taille du fichier
	bl	put32_ua
	mov	w1,54				; decalage des pixels
	add	x2,x0,10
	bl	put32
	mov	w1,40				; BITMAPINFOHEADER
	add	x2,x0,14
	bl	put32
	mov	w1,w21				; largeur
	add	x2,x0,18
	bl	put32
	neg	w1,w22				; hauteur negative : de haut en bas
	add	x2,x0,22
	bl	put32
	mov	w1,1				; plans, 32 bits
	strh	w1,[x0,26]
	mov	w1,32
	strh	w1,[x0,28]
	mov	w1,0				; BI_RGB
	add	x2,x0,30
	bl	put32
	mov	w1,w24
	add	x2,x0,34
	bl	put32
	mov	w1,2835				; 72 ppp
	add	x2,x0,38
	bl	put32
	add	x2,x0,42
	bl	put32
	mov	w1,0
	add	x2,x0,46
	bl	put32
	add	x2,x0,50
	bl	put32
	mov	x0,x20				; le fichier
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.ko_f
	mov	x19,x0
	adr	x1,bmp
	mov	x2,54
	sys	SYS_WRITE
	tbnz	x0,63,.ko_w
	mov	x25,0				; pixels : B, G, R, 0 = 0x00RRGGBB
.w:	cmp	x25,x24
	b.hs	.fin
	sub	x2,x24,x25
	mov	x3,BLOC
	cmp	x2,x3
	csel	x2,x2,x3,ls
	mov	x0,x19
	add	x1,x23,x25
	sys	SYS_WRITE
	cmp	x0,0
	b.le	.ko_w
	add	x25,x25,x0
	b	.w
.fin:	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,m_ok				; "screenshot : 1024x768 -> RAM0:/screen.bmp"
	bl	lang_puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_x
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_fleche
	bl	puts
	mov	x0,x20
	bl	puts
	adr	x0,m_nl
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.ko_w:	mov	x0,x19
	sys	SYS_CLOSE
.ko_f:	adr	x0,m_ko_f
	bl	lang_puts
	mov	x0,2
	sys	SYS_EXIT
.ko_ws:	adr	x0,m_ko_ws
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

; w1 = valeur, x2 = adresse (non alignee) : 4 octets petit-boutistes
put32:
	strb	w1,[x2]
	lsr	w3,w1,8
	strb	w3,[x2,1]
	lsr	w3,w1,16
	strb	w3,[x2,2]
	lsr	w3,w1,24
	strb	w3,[x2,3]
	ret

; (taille du fichier en bmp + 2 : w1)
put32_ua:
	add	x2,x0,2
	b	put32

p_defaut	db 'RAM0:/screen.bmp',0
m_ok		db 'screenshot : ',0
m_x		db 'x',0
m_fleche	db ' -> ',0
m_nl		db 10,0
m_ko_f		db 'screenshot : fichier impossible a ',0xC3,0xA9,'crire',10,0
m_ko_ws		db 'screenshot : Elegant ne r',0xC3,0xA9,'pond pas',10,0
		align 8

segment readable writeable
kit_data
		align 8
msg		rb WS_MSG
bmp		rb 64
