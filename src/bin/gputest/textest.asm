; ============================================================================
;  gputest : textures et gk_render (inclus par gputest.asm)
; ============================================================================

FL_1		= 0x3F800000		; 1.0
FL_M1		= 0xBF800000		; -1.0
FL_H		= 0x3F000000		; 0.5
B_2D		= GK_Z_ALWAYS or GK_B_NOZWRITE or GK_B_NOMATRIX or (GK_WRAP_CLAMP shl GK_B_WRAP_S_SHIFT) or (GK_WRAP_CLAMP shl GK_B_WRAP_T_SHIFT)

; x0 = l, x1 = h : pattern (pas l) = (x * 7 + y * 131) & 0xFFFFFF | ((x + y) & 0xFF) << 24
mk_pattern:
	adr	x2,pattern
	mov	x4,0				; y
.y:	mov	x3,0				; x
.x:	mov	x5,7
	mul	x5,x3,x5
	mov	x6,131
	madd	x5,x4,x6,x5
	and	w5,w5,0xFFFFFF
	add	x6,x3,x4
	and	x6,x6,0xFF
	orr	w5,w5,w6,lsl 24
	str	w5,[x2],4
	add	x3,x3,1
	cmp	x3,x0
	b.lo	.x
	add	x4,x4,1
	cmp	x4,x1
	b.lo	.y
	ret

; x0 = valeur : 8 chiffres hexadecimaux a la console
print_hex32:
	prologue
	adr	x1,hexbuf
	mov	x2,28
.d:	lsrv	x3,x0,x2
	and	x3,x3,15
	cmp	x3,10
	add	x4,x3,'0'
	add	x5,x3,'A'-10
	csel	x3,x4,x5,lo
	strb	w3,[x1],1
	subs	x2,x2,4
	b.pl	.d
	strb	wzr,[x1]
	adr	x0,hexbuf
	bl	puts
	epilogue

; x0 = octets, x1 = nombre : en hexadecimal (par paquets de 256)
print_bytes:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.chunk:	cbz	x20,.done
	mov	x3,256
	cmp	x20,x3
	csel	x3,x20,x3,lo
	sub	x20,x20,x3
	adr	x1,hexbuf
.b:	ldrb	w4,[x19],1
	lsr	w5,w4,4
	and	w4,w4,15
	cmp	w5,10
	add	w6,w5,'0'
	add	w7,w5,'A'-10
	csel	w5,w6,w7,lo
	strb	w5,[x1],1
	cmp	w4,10
	add	w6,w4,'0'
	add	w7,w4,'A'-10
	csel	w4,w6,w7,lo
	strb	w4,[x1],1
	subs	x3,x3,1
	b.ne	.b
	strb	wzr,[x1]
	adr	x0,hexbuf
	bl	puts
	b	.chunk
.done:	ldp	x19,x20,[sp],16
	epilogue

; x0 = lot, x1 = premier sommet, x2 = nombre, x3 = texture, x4 = drapeaux,
; x5 = matrice (16 flottants)
put_batch:
	adr	x6,batches
	mov	x7,GKB_SIZE
	madd	x6,x0,x7,x6
	stp	w1,w2,[x6,GKB_FIRST]
	stp	w3,w4,[x6,GKB_TEX]
	mov	x7,0
.m:	ldr	x8,[x5,x7]
	add	x9,x6,GKB_MATRIX
	str	x8,[x9,x7]
	add	x7,x7,8
	cmp	x7,64
	b.lo	.m
	ret

; x0 = pixels, x1 = l, x2 = h, x3 = pas (pixels), w4 = clear, w5 = drapeaux
set_frame:
	adr	x6,frame
	str	x0,[x6,GKF_PIXELS]
	stp	w1,w2,[x6,GKF_W]
	stp	w3,w4,[x6,GKF_STRIDE]
	str	w5,[x6,GKF_FLAGS]
	ret

; ---------------------------------------------------------------------------
;  gputest lists
; ---------------------------------------------------------------------------
lists_mode:
	mov	x0,GK_DBG_DRY
	mov	x1,1
	bl	gk_debug
	mov	x19,0				; textures de chaque rangement
.tex:	adr	x0,tex_dims
	add	x0,x0,x19,lsl 2
	ldrh	w20,[x0]
	ldrh	w21,[x0,2]
	mov	x0,x20
	mov	x1,x21
	bl	mk_pattern
	mov	w0,-1
	adr	x1,pattern
	mov	w2,w20
	mov	w3,w21
	mov	w4,w20
	bl	gk_texture
	mov	x22,x0
	adr	x0,m_vtex
	bl	puts
	mov	x0,x22
	bl	print_dec
	tbnz	x22,63,.texnl
	mov	x0,GK_DBG_TEX
	mov	x1,x22
	adr	x2,dbg_out
	bl	gk_debug
	adr	x0,m_sp
	bl	puts
	adr	x0,dbg_out
	ldr	x0,[x0,GKD_TEX_PHYS]
	bl	print_hex32
	adr	x0,m_sp
	bl	puts
	adr	x0,dbg_out
	ldr	x0,[x0,GKD_TEX_BYTES]
	bl	print_dec
	adr	x0,m_sp
	bl	puts
	adr	x1,dbg_out				; FNV-1a des texels
	ldp	x2,x3,[x1,GKD_TEX_VA]
	movl	w0,0x811C9DC5
	movl	w5,0x01000193
.h:	ldrb	w4,[x2],1
	eor	w0,w0,w4
	mul	w0,w0,w5
	subs	x3,x3,1
	b.ne	.h
	bl	print_hex32
.texnl:	adr	x0,m_nl
	bl	puts
	add	x19,x19,1
	cmp	x19,TEX_DIMS
	b.lo	.tex

	; ---- image A : 300 x 200 dans un tampon partage, 5 lots
	movl	x0,300*200*4
	bl	gk_alloc
	mov	x23,x0
	mov	x0,0				; quad texture 0, premultiplie, bilineaire
	mov	x1,0
	mov	x2,6
	mov	x3,0
	movl	x4,B_2D or GK_B_LINEAR or (GK_BLEND_PREMUL shl GK_B_BLEND_SHIFT)
	adr	x5,m_ident
	bl	put_batch
	mov	x0,1				; couleur, matrice, profondeur LESS
	mov	x1,6
	mov	x2,3
	mov	x3,-1
	mov	x4,0
	adr	x5,m_half
	bl	put_batch
	mov	x0,2				; texture 1, test d'alpha, MULCOL, sans dos
	mov	x1,9
	mov	x2,3
	mov	x3,1
	movl	x4,GK_B_NOMATRIX or GK_B_CULL_BACK or GK_B_ALPHATEST or (128 shl GK_B_AT_SHIFT) or (GK_BLEND_MULCOL shl GK_B_BLEND_SHIFT)
	adr	x5,m_ident
	bl	put_batch
	mov	x0,3				; vide
	mov	x1,0
	mov	x2,0
	mov	x3,-1
	mov	x4,0
	adr	x5,m_ident
	bl	put_batch
	mov	x0,4				; texture 2, GREATER, premultiplie
	mov	x1,0
	mov	x2,3
	mov	x3,2
	movl	x4,GK_B_NOMATRIX or GK_Z_GREATER or GK_B_CULL_FRONT or (GK_BLEND_PREMUL shl GK_B_BLEND_SHIFT)
	adr	x5,m_ident
	bl	put_batch
	mov	x0,x23
	mov	x1,300
	mov	x2,200
	mov	x3,300
	movl	w4,0x00112233
	mov	w5,0
	bl	set_frame
	mov	x0,5
	bl	run_frame

	; ---- image B : 130 x 70 au milieu d'un tampon (pas 160), KEEP + ALPHA
	movl	x0,160*80*4
	bl	gk_alloc
	movl	x1,(5*160+7)*4
	add	x0,x0,x1
	mov	x1,130
	mov	x2,70
	mov	x3,160
	movl	w4,0x80FF0000
	mov	w5,GK_F_KEEP or GK_F_ALPHA
	bl	set_frame
	mov	x0,0				; texture 4, DSTOUT, repete / miroir
	mov	x1,0
	mov	x2,6
	mov	x3,4
	movl	x4,GK_Z_ALWAYS or GK_B_NOMATRIX or (GK_WRAP_MIRROR shl GK_B_WRAP_T_SHIFT) or (GK_BLEND_DSTOUT shl GK_B_BLEND_SHIFT)
	adr	x5,m_ident
	bl	put_batch
	mov	x0,1
	bl	run_frame

	; ---- image C : 64 x 64 hors d'un tampon partage (tampon d'etape), KEEP
	adr	x0,plain
	mov	x1,64
	mov	x2,64
	mov	x3,64
	mov	w4,0
	mov	w5,GK_F_KEEP
	bl	set_frame
	mov	x0,0				; couleur, test d'alpha, ADD
	mov	x1,9
	mov	x2,3
	mov	x3,-1
	movl	x4,GK_Z_ALWAYS or GK_B_NOMATRIX or GK_B_ALPHATEST or (10 shl GK_B_AT_SHIFT) or (GK_BLEND_ADD shl GK_B_BLEND_SHIFT)
	adr	x5,m_ident
	bl	put_batch
	mov	x0,1
	bl	run_frame

	; ---- un sommet hors de la bande de garde : refuse
	mov	x0,0
	mov	x1,12
	mov	x2,3
	mov	x3,-1
	mov	x4,GK_B_NOMATRIX
	adr	x5,m_ident
	bl	put_batch
	adr	x0,frame
	adr	x1,verts
	mov	w2,15
	adr	x3,batches
	mov	w4,1
	bl	gk_render
	mov	x19,x0
	adr	x0,m_vbad
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,m_vfin
	bl	puts
	mov	x0,0
	sys	SYS_EXIT

; gputest r L H DRAPEAUX [PREMIER [NOMBRE]] : un seul lot de couleur (essais sur le Pi : un job
; bloque eteint le GPU jusqu'au redemarrage) ; affiche le code et le pixel du centre
render_mode:
	mov	x19,x0
	add	x19,x19,1
	bl	next_dec
	mov	x20,x0				; l
	bl	next_dec
	mov	x21,x0				; h
	bl	next_dec
	mov	x22,x0				; drapeaux du lot
	mov	x23,0
	mov	x24,6
	ldrb	w0,[x19]
	cbz	w0,.go
	bl	next_dec
	mov	x23,x0
	bl	next_dec
	mov	x24,x0
.go:	mul	x0,x20,x21
	lsl	x0,x0,2
	bl	gk_alloc
	mov	x25,x0
	mov	x1,x20
	mov	x2,x21
	mov	x3,x20
	movl	w4,0x000000FF
	mov	w5,0
	bl	set_frame
	mov	x0,0
	mov	x1,x23
	mov	x2,x24
	mov	x3,-1
	mov	x4,x22
	adr	x5,m_ident
	bl	put_batch
	adr	x0,frame
	adr	x1,verts
	mov	w2,12
	adr	x3,batches
	mov	w4,1
	bl	gk_render
	mov	x26,x0
	adr	x0,m_trender
	bl	puts
	mov	x0,x26
	bl	print_dec
	adr	x0,m_sp
	bl	puts
	lsr	x1,x21,1
	mul	x1,x1,x20
	add	x1,x1,x20,lsr 1
	ldr	w0,[x25,x1,lsl 2]
	bl	print_hex32
	adr	x0,m_nl
	bl	puts
	mov	x0,0
	sys	SYS_EXIT

; x19 = ligne : le nombre suivant -> x0, x19 apres lui
next_dec:
	prologue
.sp:	ldrb	w0,[x19]
	cmp	w0,' '
	b.ne	.n
	add	x19,x19,1
	b	.sp
.n:	mov	x0,x19
	bl	parse_dec
.f:	ldrb	w1,[x19]
	cbz	w1,.r
	cmp	w1,' '
	b.eq	.r
	add	x19,x19,1
	b	.f
.r:	epilogue

; x0 = lots : gk_render de frame / verts / batches, resultat et listes
run_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x4,x0
	adr	x0,frame
	adr	x1,verts
	mov	w2,12
	adr	x3,batches
	bl	gk_render
	mov	x19,x0
	adr	x0,m_vr
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	cbnz	x19,.out
	mov	x0,GK_DBG_LISTS
	adr	x1,dbg_out
	bl	gk_debug
	adr	x19,dbg_out
	adr	x0,m_vtgt
	bl	puts
	ldr	x0,[x19,GKD_TGT]
	bl	print_hex32
	adr	x0,m_nl
	bl	puts
	adr	x0,m_vwk
	bl	puts
	ldr	x0,[x19,GKD_WK_PHYS]
	bl	print_hex32
	adr	x0,m_nl
	bl	puts
	adr	x0,m_vbcl
	bl	puts
	ldp	x0,x1,[x19,GKD_BCL]
	bl	print_bytes
	adr	x0,m_nl
	bl	puts
	adr	x0,m_vind
	bl	puts
	ldp	x0,x1,[x19,GKD_IND]
	bl	print_bytes
	adr	x0,m_nl
	bl	puts
	adr	x0,m_vrcl
	bl	puts
	ldp	x0,x1,[x19,GKD_RCL]
	bl	print_bytes
	adr	x0,m_nl
	bl	puts
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Essais sur le GPU
; ---------------------------------------------------------------------------

; x0 = a, x1 = b (0x00RRGGBB), x2 = ecart permis -> x0 = 1 si chaque canal est proche
px_near:
	mov	x6,0
.c:	lsrv	w3,w0,w6
	and	w3,w3,0xFF
	lsrv	w4,w1,w6
	and	w4,w4,0xFF
	subs	w3,w3,w4
	cneg	w3,w3,mi
	cmp	w3,w2
	b.hi	.no
	add	x6,x6,8
	cmp	x6,24
	b.lo	.c
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = pixels (pas 64), x1 = x, x2 = y, w3 = attendu -> x0 = 1 si proche (1)
tpx_is:
	lsl	x2,x2,6
	add	x2,x2,x1
	ldr	w0,[x0,x2,lsl 2]
	mov	w1,w3
	mov	x2,1
	b	px_near

; x0 = cible 64 x 64 (pas 64), w1 = drapeaux GK_F_*, w2 = texture, w3 = drapeaux
; du lot -> x0 = resultat de gk_render
render64:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w2
	mov	w20,w3
	mov	w5,w1
	mov	x1,64
	mov	x2,64
	mov	x3,64
	mov	w4,0x0000FF
	bl	set_frame
	mov	x0,0
	mov	x1,0
	mov	x2,6
	sxtw	x3,w19
	mov	w4,w20
	adr	x5,m_ident
	bl	put_batch
	adr	x0,frame
	adr	x1,verts
	mov	w2,12
	adr	x3,batches
	mov	w4,1
	bl	gk_render
	mov	x19,x0
	cbz	x19,.ok
	adr	x0,m_trender			; le code d'erreur, pour comprendre
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.ok:	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pixels 64 x 64 : 1 si chacun vaut le motif (rgb, a 1 pres)
same_as_pattern:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x20,pattern
	mov	x21,0
.l:	ldr	w0,[x19,x21,lsl 2]
	ldr	w1,[x20,x21,lsl 2]
	and	w1,w1,0xFFFFFF
	mov	x2,1
	bl	px_near
	cbz	x0,.out
	add	x21,x21,1
	cmp	x21,64*64
	b.lo	.l
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

tex_tests:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x0,64				; texture 64 x 64 opaque
	mov	x1,64
	bl	mk_pattern
	adr	x0,pattern
	mov	x1,0
.op:	ldr	w2,[x0,x1,lsl 2]
	orr	w2,w2,0xFF000000
	str	w2,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,64*64
	b.lo	.op
	mov	w0,-1
	adr	x1,pattern
	mov	w2,64
	mov	w3,64
	mov	w4,64
	bl	gk_texture
	sxtw	x19,w0
	cmn	x19,1				; GK_E_NOGPU : rien a essayer
	b.ne	.gpu
	adr	x0,m_tnogpu
	bl	puts
	b	.out
.gpu:	tbz	x19,63,.made
	mov	x0,0
	adr	x1,t_tcreate
	mov	x2,0
	bl	report
	b	.out
.made:	movl	x0,64*64*4			; la cible
	bl	gk_alloc
	mov	x20,x0

	; ---- 0. couleur seule (sans texture) : VS_CLIP, FS_COLOR
	mov	x0,x20
	mov	w1,0
	mov	w2,-1
	movl	w3,B_2D
	bl	render64
	mov	x22,x0
	mov	x0,x20
	mov	x1,32
	mov	x2,32
	movl	w3,0xFFFFFF
	bl	tpx_is
	cmp	x22,0
	csel	x0,x0,xzr,eq
	adr	x1,t_tcolor
	cmp	x22,0
	cset	x2,eq
	bl	report

	; ---- 1. recopie au texel pres
	mov	x0,x20
	mov	w1,0
	mov	w2,w19
	movl	w3,B_2D
	bl	render64
	mov	x22,x0
	mov	x0,0
	cbnz	x22,.r1
	mov	x0,x20
	bl	same_as_pattern
.r1:	adr	x1,t_tcopy
	cmp	x22,0
	cset	x2,eq
	bl	report

	; ---- 2. melange premultiplie par-dessus du rouge
	adr	x0,pattern			; 16 x 16 de vert a 50 % (premultiplie)
	mov	x1,0
	movl	w2,0x80008000
.half:	str	w2,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,16*16
	b.lo	.half
	mov	w0,-1
	adr	x1,pattern
	mov	w2,16
	mov	w3,16
	mov	w4,16
	bl	gk_texture
	sxtw	x21,w0
	mov	x0,x20
	mov	x1,256
	mov	x2,0
	mov	x3,0
	mov	x4,64
	mov	x5,64
	movl	w6,0xFF0000
	bl	gk_fill
	mov	x0,x20
	mov	w1,GK_F_KEEP
	mov	w2,w21
	movl	w3,B_2D or GK_B_LINEAR or (GK_BLEND_PREMUL shl GK_B_BLEND_SHIFT)
	bl	render64
	mov	x22,x0
	mov	x0,0
	cbnz	x22,.r2
	mov	x0,x20
	mov	x1,32
	mov	x2,32
	movl	w3,0x7F8000
	lsl	x2,x2,6
	add	x2,x2,x1
	ldr	w0,[x0,x2,lsl 2]
	mov	w1,w3
	mov	x2,2
	bl	px_near
.r2:	adr	x1,t_tblend
	cmp	x22,0
	cset	x2,eq
	bl	report

	; ---- 3. rectangle remplace (8 x 8 bleu en 8, 8)
	adr	x0,blue8
	mov	x1,0
	movl	w2,0xFF0000FF
.bl:	str	w2,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,64
	b.lo	.bl
	mov	x0,64				; (le motif de la texture 1)
	mov	x1,64
	bl	mk_pattern
	mov	w0,w19
	mov	w1,8
	mov	w2,8
	mov	w3,8
	mov	w4,8
	adr	x5,blue8
	mov	w6,8
	bl	gk_texture_rect
	mov	x22,x0
	cbnz	x22,.r3fail
	mov	x0,x20
	mov	w1,0
	mov	w2,w19
	movl	w3,B_2D
	bl	render64
	mov	x22,x0
	cbnz	x22,.r3fail
	mov	x0,x20
	bl	check_rect
	b	.r3
.r3fail:
	mov	x0,0
.r3:	adr	x1,t_trect
	cmp	x22,0
	cset	x2,eq
	bl	report

	; ---- 4. cible hors d'un tampon partage (tampon d'etape)
	adr	x0,plain
	mov	w1,0
	mov	w2,w19
	movl	w3,B_2D
	bl	render64
	mov	x22,x0
	mov	x0,0
	cbnz	x22,.r4
	adr	x0,plain
	bl	check_rect
.r4:	adr	x1,t_tstage
	cmp	x22,0
	cset	x2,eq
	bl	report

	; ---- 5. sommet hors de la bande de garde : GK_E_INVAL
	mov	x0,0
	mov	x1,12
	mov	x2,3
	mov	x3,-1
	mov	x4,GK_B_NOMATRIX
	adr	x5,m_ident
	bl	put_batch
	adr	x0,frame
	adr	x1,verts
	mov	w2,15
	adr	x3,batches
	mov	w4,1
	bl	gk_render
	cmn	x0,2
	cset	x0,eq
	adr	x1,t_tguard
	mov	x2,0
	bl	report

	mov	w0,w19				; liberees
	mov	x1,0
	bl	gk_texture
	mov	w0,w21
	mov	x1,0
	bl	gk_texture
	mov	x0,x20
	bl	gk_free
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pixels 64 x 64 : le bleu en (8..15, 8..15), le motif ailleurs -> 1 bon
check_rect:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x1,10
	mov	x2,10
	movl	w3,0x0000FF
	bl	tpx_is
	mov	x20,x0
	mov	x0,x19
	mov	x1,15
	mov	x2,15
	movl	w3,0x0000FF
	bl	tpx_is
	and	x20,x20,x0
	adr	x0,pattern			; (16, 16) et (7, 7) : le motif
	ldr	w3,[x0,(16*64+16)*4]
	and	w3,w3,0xFFFFFF
	mov	x0,x19
	mov	x1,16
	mov	x2,16
	bl	tpx_is
	and	x20,x20,x0
	adr	x0,pattern
	ldr	w3,[x0,(7*64+7)*4]
	and	w3,w3,0xFFFFFF
	mov	x0,x19
	mov	x1,7
	mov	x2,7
	bl	tpx_is
	and	x0,x20,x0
	ldp	x19,x20,[sp],16
	epilogue

		align 4
; textures de l'essai des listes : un rangement chacune (LT, UB1, UB2, UIF,
; UIF + XOR, ub_pad > 0 avec XOR, ub_pad > 0 sans XOR)
tex_dims	dh 100, 70,  7, 20,  13, 9,  3, 5,  40, 256,  24, 232,  20, 264
TEX_DIMS	= 7
m_ident		dw FL_1, 0, 0, 0,  0, FL_1, 0, 0,  0, 0, FL_1, 0,  0, 0, 0, FL_1
m_half		dw FL_H, 0, 0, 0,  0, FL_H, 0, 0,  0, 0, FL_1, 0,  0, 0, 0, FL_1
; sommets : x y z w, s t, couleur, couleur 2
verts:		dw FL_M1, FL_1, FL_H, FL_1, 0, 0		; quad (2 triangles) : tout l'ecran
		db 255, 255, 255, 255, 0, 0, 0, 0
		dw FL_1, FL_1, FL_H, FL_1, FL_1, 0
		db 255, 255, 255, 255, 0, 0, 0, 0
		dw FL_1, FL_M1, FL_H, FL_1, FL_1, FL_1
		db 255, 255, 255, 255, 0, 0, 0, 0
		dw FL_M1, FL_1, FL_H, FL_1, 0, 0
		db 255, 255, 255, 255, 0, 0, 0, 0
		dw FL_1, FL_M1, FL_H, FL_1, FL_1, FL_1
		db 255, 255, 255, 255, 0, 0, 0, 0
		dw FL_M1, FL_M1, FL_H, FL_1, 0, FL_1
		db 255, 255, 255, 255, 0, 0, 0, 0
		dw 0, FL_1, 0, FL_1, 0, 0			; triangle
		db 255, 0, 0, 255, 0, 0, 0, 0
		dw FL_M1, FL_M1, 0, FL_1, 0, FL_1
		db 0, 255, 0, 255, 0, 0, 0, 0
		dw FL_1, FL_M1, 0, FL_1, FL_1, FL_1
		db 0, 0, 255, 255, 0, 0, 0, 0
		dw FL_M1, 0, 0, FL_1, 0, 0			; triangle (moitie gauche)
		db 255, 255, 255, 128, 0, 0, 0, 0
		dw 0, FL_1, 0, FL_1, FL_1, 0
		db 255, 255, 255, 128, 16, 16, 16, 0
		dw 0, FL_M1, 0, FL_1, FL_1, FL_1
		db 255, 255, 255, 128, 0, 0, 0, 0
		dw FL_M1, FL_M1, 0, FL_1, 0, 0		; hors de la bande de garde
		db 255, 255, 255, 255, 0, 0, 0, 0
		dw 0x42C80000, 0, 0, FL_1, 0, 0		; (x = 100)
		db 255, 255, 255, 255, 0, 0, 0, 0
		dw FL_1, FL_1, 0, FL_1, 0, 0
		db 255, 255, 255, 255, 0, 0, 0, 0

m_vtex		db 'V3DT TEX ',0
m_sp		db ' ',0
m_vr		db 'V3DT R ',0
m_vtgt		db 'V3DT TGT ',0
m_vwk		db 'V3DT WK ',0
m_vbcl		db 'V3DT BCL ',0
m_vind		db 'V3DT IND ',0
m_vrcl		db 'V3DT RCL ',0
m_vbad		db 'V3DT BAD ',0
m_vfin		db 'V3DT FIN',10,0
m_trender	db 'gputest : gk_render -> ',0
m_tnogpu	db 'gputest : textures et gk_render : pas de GPU (GK_E_NOGPU)',10,0
t_tcolor	db 'lot sans texture 64 x 64',0
t_tcreate	db 'gk_texture',0
t_tcopy		db 'texture 64 x 64 au texel pr',0xC3,0xA8,'s',0
t_tblend	db 'm',0xC3,0xA9,'lange pr',0xC3,0xA9,'multipli',0xC3,0xA9,' sur du rouge',0
t_trect		db 'rectangle remplac',0xC3,0xA9,' (gk_texture_rect)',0
t_tstage	db 'cible hors tampon partag',0xC3,0xA9,' (tampon d',0x27,0xC3,0xA9,'tape)',0
t_tguard	db 'sommet hors de la bande de garde refus',0xC3,0xA9,0
		align 8
