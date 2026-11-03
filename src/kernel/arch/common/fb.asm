; ============================================================================
;  fb.asm - framebuffer 32 bpp via property tags, primitives de dessin
;  Ref : Circle lib/bcmframebuffer.cpp, lib/2dgraphics.cpp
; ============================================================================

; Resolution : FB_W x FB_H au demarrage (kernel.asm, fasmarm -d ECRAN_W=..
; -d ECRAN_H=..), puis « ecran=LxH » de SD0:/etc/options.txt (fb_set), et a
; chaud a la demande d'Elegant (SYS_WS_MODE, ws.asm). La taille obtenue est
; dans fb_w, fb_h ; le firmware met l'image a l'echelle de la sortie HDMI.

FB_MIN_W	= 640			; (Elegant : tampon arriere pour FB_MAX_W x FB_MAX_H)
FB_MIN_H	= 480
FB_MAX_W	= 2560
FB_MAX_H	= 1600

; x0 = largeur, x1 = hauteur -> x0 = 1 si cette taille est permise (largeur
; paire), 0 sinon
fb_size_ok:
	tbnz	x0,0,.non
	cmp	x0,FB_MIN_W
	b.lo	.non
	cmp	x0,FB_MAX_W
	b.hi	.non
	cmp	x1,FB_MIN_H
	b.lo	.non
	cmp	x1,FB_MAX_H
	b.hi	.non
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; -> x0 = adresse ARM du framebuffer, 0 si echec
fb_init:
	prologue
	adr	x0,fb_msg
	bl	mbox_call
	cbz	x0,.fail
	adr	x1,fb_alloc_base
	ldr	w0,[x1]
	cbz	w0,.fail
	and	w0,w0,0x3FFFFFFF		; adresse bus -> adresse ARM
	adr	x1,fb_base
	str	x0,[x1]
	adr	x2,fb_pitch_val
	ldr	w2,[x2]
	adr	x1,fb_pitch
	str	x2,[x1]
	adr	x2,fb_phys_w			; taille obtenue
	ldp	w2,w3,[x2]
	adr	x1,fb_w
	stp	x2,x3,[x1]
	epilogue
.fail:	mov	x0,0
	epilogue

; x0 = largeur, x1 = hauteur : nouveau framebuffer (le firmware l'adapte a
; l'ecran) -> x0 = adresse, 0 si echec (l'ancien reste alors en place)
fb_set:
	prologue
	stp	x0,x1,[sp,-16]!
	adr	x0,fb_msg			; message neuf : TOUTES les requetes
	adr	x1,fb_msg_modele		; a nouveau (profondeur, ordre des
	mov	x2,fb_msg_end - fb_msg		; pixels, decalage compris)
	bl	mem_copy
	ldp	x0,x1,[sp],16
	adr	x2,fb_phys_w
	stp	w0,w1,[x2]
	adr	x2,fb_virt_w
	stp	w0,w1,[x2]
	adr	x2,fb_base			; (en cas d'echec : l'ancien)
	ldr	x2,[x2]
	str	x2,[sp,-16]!
	bl	fb_init
	ldr	x1,[sp],16
	cbnz	x0,.out
	adr	x2,fb_base
	str	x1,[x2]
.out:	epilogue

; --- message property tags --------------------------------------------------
		align 16
fb_msg:
		dw fb_msg_end - fb_msg, 0
		dw 0x00048003, 8
fb_tag_phys	dw 0
fb_phys_w	dw FB_W, FB_H				; taille physique
		dw 0x00048004, 8
fb_tag_virt	dw 0
fb_virt_w	dw FB_W, FB_H				; taille virtuelle
		dw 0x00048009, 8, 0, 0, 0		; offset virtuel
		dw 0x00048005, 4, 0
fb_depth	dw 32					; profondeur 32 bpp (-> obtenue)
		dw 0x00048006, 4, 0, 0			; ordre 0 : pixel = 0x00RRGGBB (a valider sur materiel)
		dw 0x00040001, 8
fb_tag_alloc	dw 0					; allocation
fb_alloc_base	dw 4096					;  -> adresse bus
		dw 0					;  -> taille
		dw 0x00040008, 4
fb_tag_pitch	dw 0					; pitch
fb_pitch_val	dw 0
		dw 0					; tag de fin
fb_msg_end:

; le meme message, intact : fb_set le recopie avant chaque changement
		align 16
fb_msg_modele:
		dw fb_msg_end - fb_msg, 0
		dw 0x00048003, 8, 0, FB_W, FB_H
		dw 0x00048004, 8, 0, FB_W, FB_H
		dw 0x00048009, 8, 0, 0, 0
		dw 0x00048005, 4, 0, 32
		dw 0x00048006, 4, 0, 0
		dw 0x00040001, 8, 0, 4096, 0
		dw 0x00040008, 4, 0, 0
		dw 0
