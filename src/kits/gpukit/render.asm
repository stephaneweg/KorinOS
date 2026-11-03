; ============================================================================
;  kits/gpukit/render.asm - textures et rendu de GpuKit (inclus par gpukit.asm)
;
;  La recette de kapi gpu_texture / gpu_texture_rect / gpu_render d'Onyx
;  (kernel/sys/v3d.cpp, valide sur Pi 4) cote application :
;  * une texture : ses texels RGBA8 (R en premier en memoire) ranges comme le
;    TMU lit le niveau 0 de cette taille (kern/v3d_tiling.h d'Onyx, d'apres
;    v3d_setup_slices de Mesa) : LT, UBLINEAR 1 / 2 colonnes, UIF (+ ub_pad,
;    XOR) ; son TEXTURE_SHADER_STATE (32 octets) garde dans sa fiche et copie
;    dans la liste indirecte a chaque lot qui la lit ;
;  * les textures vivent dans des « morceaux » : tampons partages sous 1 Go de
;    4 Mo (plus pour une grande texture), pages de 4 Ko distribuees par une
;    table de bits : 128 tampons seulement dans tout le systeme ;
;  * gk_render : un lot = ses uniforms (matrice, echelles), sampler et etat de
;    texture, shader state record (4 attributs : x y z w, s t, couleur,
;    couleur 2) dans la liste indirecte ; CFG_BITS, melange, GL_SHADER_STATE
;    et VERTEX_ARRAY_PRIMS dans la liste de binning.
;  Sans decoupe sur le processeur : un sommet hors de la bande de garde
;  (|x|, |y| <= 4 w, z >= -w, w > 0, apres la matrice) fait refuser l'image
;  (GK_E_INVAL) ; des coordonnees extremes bloquent le binner (Onyx).
; ============================================================================

; --- etat permanent : les shaders de gk_render (decalages dans WK_STATE) --------
ST_VS_CLIP	= 2048
ST_CS_CLIP	= ST_VS_CLIP + 1024
ST_FS_COLOR	= ST_CS_CLIP + 1024
ST_FS_TEX	= ST_FS_COLOR + 512
ST_FS_COLOR_AT	= ST_FS_TEX + 512
ST_FS_TEX_AT	= ST_FS_COLOR_AT + 512
ST_END		= ST_FS_TEX_AT + 512

; --- une texture (fiche dans WK_TEX) ----------------------------------------------
TX_CHUNK	= 0			; morceau + 1 (0 : fiche libre)
TX_PAGE		= 8			; premiere page dans le morceau
TX_NPAGES	= 16
TX_VA		= 24			; texels
TX_PHYS		= 32
TX_W		= 40
TX_H		= 48
TX_KIND		= 56			; TL_*
TX_PADH		= 64			; hauteur alignee
TX_XOR		= 72
TX_STATE	= 80			; TEXTURE_SHADER_STATE (32 octets)
TX_SIZE		= 128

TL_LT		= 0
TL_UB1		= 1
TL_UB2		= 2
TL_UIF		= 3

; --- les morceaux (gk_chunks) ------------------------------------------------------
GK_CHUNKS	= 16
CHUNK_PAGES	= 1024			; 4 Mo (une texture plus grande : sa taille)
CK_BUF		= 0
CK_VA		= 8
CK_PHYS		= 16
CK_PAGES	= 24			; 0 : morceau pas encore cree
CK_SIZE		= 32

; ---------------------------------------------------------------------------
;  Rangement des texels
; ---------------------------------------------------------------------------

; x0 = largeur, x1 = hauteur -> x9 = TL_*, x10 = largeur alignee, x11 =
; hauteur alignee, x12 = ub_pad, x13 = xor ; detruit x14, x15
tex_layout:
	mov	x12,0
	mov	x13,0
	cmp	x0,4
	b.ls	.lt
	cmp	x1,4
	b.ls	.lt
	add	x11,x1,7
	and	x11,x11,not 7
	cmp	x0,8
	b.hi	.ub2
	mov	x9,TL_UB1
	mov	x10,8
	ret
.ub2:	cmp	x0,16
	b.hi	.uif
	mov	x9,TL_UB2
	mov	x10,16
	ret
.lt:	mov	x9,TL_LT
	add	x10,x0,3
	and	x10,x10,not 3
	add	x11,x1,3
	and	x11,x11,not 3
	ret
.uif:	mov	x9,TL_UIF
	add	x10,x0,31
	and	x10,x10,not 31
	lsr	x14,x11,3			; hauteur en blocs UIF (Mesa v3d_get_ub_pad)
	and	x15,x14,31
	cbz	x15,.padded
	cmp	x15,6
	b.hs	.over
	cmp	x14,32
	b.lo	.padded
	mov	x12,6
	sub	x12,x12,x15
	b	.padded
.over:	cmp	x15,26
	b.ls	.padded
	mov	x12,32
	sub	x12,x12,x15
.padded:
	add	x11,x11,x12,lsl 3
	lsr	x14,x11,3
	tst	x14,31
	cset	x13,eq
	ret

; x0 = x, x1 = y, x9 = TL_*, x11 = hauteur alignee, x13 = xor -> x2 = decalage
; du texel (octets) ; detruit x3-x5
tex_offset:
	and	x2,x0,3				; dans l'utile 4 x 4 (64 octets)
	and	x3,x1,3
	lsl	x2,x2,2
	add	x2,x2,x3,lsl 4
	cmp	x9,TL_LT
	b.ne	.ub
	lsr	x3,x0,2				; une ligne d'utiles
	add	x3,x3,x1,lsr 2
	add	x2,x2,x3,lsl 6
	ret
.ub:	tst	x0,4
	b.eq	.x4
	add	x2,x2,64
.x4:	tst	x1,4
	b.eq	.y4
	add	x2,x2,128
.y4:	cmp	x9,TL_UIF
	b.eq	.uif
	lsr	x3,x1,3				; blocs de 8 x 8 en 1 ou 2 colonnes
	cmp	x9,TL_UB1
	b.eq	.one
	lsl	x3,x3,1
.one:	add	x3,x3,x0,lsr 3
	add	x2,x2,x3,lsl 8
	ret
.uif:	lsr	x3,x0,3				; mbX
	lsr	x4,x1,3				; mbY
	cbz	x13,.noxor
	tbz	x3,2,.noxor			; colonnes de 4 blocs impaires
	eor	x4,x4,0x10
.noxor:	lsl	x4,x4,2
	and	x5,x3,3
	add	x4,x4,x5			; mbY * 4 + mbX % 4
	lsr	x3,x3,2
	lsr	x5,x11,3
	mul	x3,x3,x5
	add	x4,x4,x3,lsl 2			; + mbX / 4 * mbH * 4
	add	x2,x2,x4,lsl 8
	ret

; x0 = fiche, x1 = x, x2 = y, x3 = l, x4 = h, x5 = pixels 0xAARRGGBB, x6 = pas
; (pixels) : le rectangle range dans les texels (R G B A en memoire), puis
; nettoye du cache sur les seuls octets touches
tex_store:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	mov	x22,x4
	mov	x23,x5
	mov	x24,x6
	ldr	x25,[x0,TX_VA]
	ldr	x9,[x0,TX_KIND]
	ldr	x11,[x0,TX_PADH]
	ldr	x13,[x0,TX_XOR]
	mov	x26,-1				; plus bas octet ecrit
	mov	x27,0				; plus haut + 1
	mov	x28,0				; ligne
.row:	mul	x15,x28,x24
	add	x15,x23,x15,lsl 2		; source de la ligne
	mov	x14,0
.utile:	add	x16,x19,x14			; x dans la texture
	and	x0,x16,not 3			; (debut de la ligne d'utile : 16 octets
	add	x1,x20,x28			;  contigus dans tous les rangements)
	bl	tex_offset
	cmp	x2,x26
	csel	x26,x2,x26,lo
	add	x3,x2,16
	cmp	x3,x27
	csel	x27,x3,x27,hi
	add	x2,x25,x2
	and	x3,x16,3
.px:	ldr	w4,[x15,x14,lsl 2]		; 0xAARRGGBB -> R G B A
	and	w5,w4,0xFF00FF00
	ubfx	w6,w4,16,8
	orr	w5,w5,w6
	and	w6,w4,0xFF
	orr	w5,w5,w6,lsl 16
	str	w5,[x2,x3,lsl 2]
	add	x14,x14,1
	cmp	x14,x21
	b.hs	.next
	add	x3,x3,1
	cmp	x3,4
	b.lo	.px
	b	.utile
.next:	add	x28,x28,1
	cmp	x28,x22
	b.lo	.row
	add	x0,x25,x26
	sub	x1,x27,x26
	bl	dc_clean
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = 32 octets, x1 = premier bit, x2 = bits, x3 = valeur : bits mis a 1
; (le reste deja a 0) ; detruit x4-x8
set_bits:
	mov	x4,0
.l:	lsrv	x5,x3,x4
	tbz	x5,0,.n
	add	x5,x1,x4
	lsr	x6,x5,3
	and	x7,x5,7
	ldrb	w8,[x0,x6]
	mov	w5,1
	lslv	w5,w5,w7
	orr	w8,w8,w5
	strb	w8,[x0,x6]
.n:	add	x4,x4,1
	cmp	x4,x2
	b.lo	.l
	ret

; x0 = fiche : son TEXTURE_SHADER_STATE (Mesa v3d_packet.xml, 4.2)
tex_make_state:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	add	x0,x19,TX_STATE
	stp	xzr,xzr,[x0]
	stp	xzr,xzr,[x0,16]
	ldr	x1,[x19,TX_PHYS]		; base (alignee : ses bits bas sont des drapeaux)
	str	w1,[x0]
	mov	x1,58				; largeur
	mov	x2,14
	ldr	x3,[x19,TX_W]
	bl	set_bits
	mov	x1,72				; hauteur
	mov	x2,14
	ldr	x3,[x19,TX_H]
	bl	set_bits
	mov	x1,86				; profondeur 1
	mov	x2,14
	mov	x3,1
	bl	set_bits
	mov	x1,100				; RGBA8
	mov	x2,7
	mov	x3,4
	bl	set_bits
	mov	x1,108				; swizzle R G B A
	mov	x2,3
	mov	x3,2
	bl	set_bits
	mov	x1,111
	mov	x2,3
	mov	x3,3
	bl	set_bits
	mov	x1,114
	mov	x2,3
	mov	x3,4
	bl	set_bits
	mov	x1,117
	mov	x2,3
	mov	x3,5
	bl	set_bits
	ldr	x3,[x19,TX_KIND]
	cmp	x3,TL_UIF
	b.ne	.done
	mov	x1,107				; etendu (niveau 0 strictement UIF)
	mov	x2,1
	mov	x3,1
	bl	set_bits
	ldp	x0,x1,[x19,TX_W]		; UB_PAD du niveau 0 (recalcule)
	bl	tex_layout
	add	x0,x19,TX_STATE
	mov	x1,128
	mov	x2,4
	mov	x3,x12
	bl	set_bits
	mov	x1,132				; XOR
	mov	x2,1
	ldr	x3,[x19,TX_XOR]
	bl	set_bits
	mov	x1,134				; niveau 0 strictement UIF
	mov	x2,1
	mov	x3,1
	bl	set_bits
.done:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Morceaux de textures
; ---------------------------------------------------------------------------

; x0 = morceau -> x0 = sa table de bits (pages occupees)
chunk_map:
	adr	x1,gk_wk
	ldr	x1,[x1,8]
	add	x1,x1,WK_TMAP
	add	x0,x1,x0,lsl 9
	ret

; x0 = pages -> x0 = morceau + 1 (0 : pas de memoire), x1 = premiere page
tex_alloc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.chunk:	cmp	x20,GK_CHUNKS
	b.hs	.none
	adr	x21,gk_chunks
	add	x21,x21,x20,lsl 5
	ldr	x22,[x21,CK_PAGES]
	cbz	x22,.new
	mov	x0,x20
	bl	chunk_map
	mov	x23,x0
	mov	x2,0				; page
	mov	x3,0				; pages libres de suite
.pg:	cmp	x2,x22
	b.hs	.next
	lsr	x4,x2,3
	ldrb	w5,[x23,x4]
	and	x6,x2,7
	lsrv	w5,w5,w6
	add	x2,x2,1
	tbnz	w5,0,.used
	add	x3,x3,1
	cmp	x3,x19
	b.lo	.pg
	sub	x1,x2,x19
	b	.take
.used:	mov	x3,0
	b	.pg
.next:	add	x20,x20,1
	b	.chunk
.new:	mov	x0,CHUNK_PAGES			; un nouveau morceau
	cmp	x19,x0
	csel	x22,x19,x0,hi
.create:
	lsl	x0,x22,12
	movl	x1,BUF_LOW
	orr	x0,x0,x1
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.smaller
	mov	x23,x0
	adr	x1,gk_binfo
	sys	SYS_BUF_INFO
	tbnz	x0,63,.drop
	adr	x1,gk_binfo
	ldr	x2,[x1,BI_PHYS]
	ldr	x3,[x1,BI_BYTES]
	add	x3,x3,x2
	movl	x4,LOW_LIMIT
	cmp	x3,x4
	b.hi	.drop
	movl	x0,BUF_VA_BASE
	mov	x1,BUF_SLOT
	madd	x0,x23,x1,x0
	stp	x23,x0,[x21,CK_BUF]
	stp	x2,x22,[x21,CK_PHYS]
	mov	x0,x20
	bl	chunk_map
	mov	x23,x0
	mov	x1,0
.take:	mov	x2,x1				; pages marquees occupees
	add	x3,x1,x19
.mark:	lsr	x4,x2,3
	ldrb	w5,[x23,x4]
	and	x6,x2,7
	mov	w7,1
	lslv	w7,w7,w6
	orr	w5,w5,w7
	strb	w5,[x23,x4]
	add	x2,x2,1
	cmp	x2,x3
	b.lo	.mark
	add	x0,x20,1
	b	.out
.drop:	mov	x0,x23
	sys	SYS_BUF_FREE
.smaller:
	cmp	x22,x19				; 4 Mo d'un bloc introuvables : juste
	b.ls	.none				; ce qu'il faut
	mov	x22,x19
	b	.create
.none:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fiche : ses pages rendues, la fiche libre
tex_release:
	ldr	x1,[x0,TX_CHUNK]
	cbz	x1,.r
	str	xzr,[x0,TX_CHUNK]
	ldr	x2,[x0,TX_PAGE]
	ldr	x3,[x0,TX_NPAGES]
	add	x3,x3,x2
	adr	x4,gk_wk
	ldr	x4,[x4,8]
	add	x4,x4,WK_TMAP
	sub	x1,x1,1
	add	x4,x4,x1,lsl 9
.l:	lsr	x5,x2,3
	ldrb	w6,[x4,x5]
	and	x7,x2,7
	mov	w8,1
	lslv	w8,w8,w7
	bic	w6,w6,w8
	strb	w6,[x4,x5]
	add	x2,x2,1
	cmp	x2,x3
	b.lo	.l
.r:	ret

; x0 = numero de texture -> x0 = sa fiche (0 : pas une texture du process)
tex_entry:
	cmp	x0,GK_MAX_TEXTURES
	b.hs	.no				; (negatif aussi : non signe)
	adr	x1,gk_wk
	ldr	x1,[x1,8]
	add	x1,x1,WK_TEX
	add	x0,x1,x0,lsl 7
	ldr	x1,[x0,TX_CHUNK]
	cbz	x1,.no
	ret
.no:	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  API : textures
; ---------------------------------------------------------------------------

; w0 = texture (< 0 : nouvelle), x1 = pixels (0 : liberer), w2 = l, w3 = h,
; w4 = pas -> texture, 0, GK_E_*
gk_texture:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sxtw	x19,w0
	mov	x20,x1
	sxtw	x21,w2
	sxtw	x22,w3
	sxtw	x23,w4
	bl	gk_lock
	bl	gk_ready
	cbz	x0,.nogpu
	mov	x24,0
	tbnz	x19,63,.args
	mov	x0,x19				; une texture du process ?
	bl	tex_entry
	cbz	x0,.inval
	mov	x24,x0
	cbnz	x20,.args
	bl	tex_release			; pixels 0 : liberee
	mov	x0,0
	b	.out
.args:	cbz	x20,.inval
	cmp	x21,0
	b.le	.inval
	cmp	x22,0
	b.le	.inval
	cmp	x21,GK_MAX_TEX
	b.gt	.inval
	cmp	x22,GK_MAX_TEX
	b.gt	.inval
	cmp	x23,x21
	b.lt	.inval
	cbnz	x24,.renew
	adr	x24,gk_wk			; une fiche libre
	ldr	x24,[x24,8]
	add	x24,x24,WK_TEX
	mov	x19,0
.find:	ldr	x0,[x24,TX_CHUNK]
	cbz	x0,.place
	add	x24,x24,TX_SIZE
	add	x19,x19,1
	cmp	x19,GK_MAX_TEXTURES
	b.lo	.find
	b	.nomem
.renew:	mov	x0,x24				; remplacee : nouvelles pages
	bl	tex_release
.place:	mov	x0,x21
	mov	x1,x22
	bl	tex_layout
	stp	x21,x22,[x24,TX_W]
	stp	x9,x11,[x24,TX_KIND]
	str	x13,[x24,TX_XOR]
	mul	x0,x10,x11			; octets -> pages
	lsl	x0,x0,2
	add	x0,x0,4095
	lsr	x0,x0,12
	str	x0,[x24,TX_NPAGES]
	bl	tex_alloc
	cbz	x0,.nomem
	str	x1,[x24,TX_PAGE]
	sub	x2,x0,1
	adr	x3,gk_chunks
	add	x3,x3,x2,lsl 5
	ldp	x4,x5,[x3,CK_VA]		; adresse, physique
	add	x4,x4,x1,lsl 12
	add	x5,x5,x1,lsl 12
	stp	x4,x5,[x24,TX_VA]
	str	x0,[x24,TX_CHUNK]
	mov	x0,x24
	bl	tex_make_state
	mov	x0,x24
	mov	x1,0
	mov	x2,0
	mov	x3,x21
	mov	x4,x22
	mov	x5,x20
	mov	x6,x23
	bl	tex_store
	mov	x0,x19
	b	.out
.nogpu:	mov	x0,GK_E_NOGPU
	b	.out
.inval:	mov	x0,GK_E_INVAL
	b	.out
.nomem:	mov	x0,GK_E_NOMEM
.out:	mov	x19,x0
	bl	gk_unlock
	mov	x0,x19
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = texture, x1 = sortie (48 o) <- adresse physique, octets, TL_*, largeur, hauteur,
; adresse des texels -> 0, GK_E_INVAL. Pour qu'un autre pilote du GPU y rende (Mesa :
; docs/MESA.md). Le rangement est celui de v3d_setup_slices.
gk_texture_info:
	sxtw	x2,w0
	cmp	x2,0
	b.lt	.bad
	cmp	x2,GK_MAX_TEXTURES
	b.hs	.bad
	adr	x3,gk_wk
	ldr	x3,[x3,8]
	cbz	x3,.bad
	add	x3,x3,WK_TEX
	mov	x4,TX_SIZE
	madd	x3,x2,x4,x3
	ldr	x4,[x3,TX_CHUNK]
	cbz	x4,.bad
	ldr	x4,[x3,TX_PHYS]
	str	x4,[x1]
	ldr	x4,[x3,TX_NPAGES]
	lsl	x4,x4,12
	str	x4,[x1,8]
	ldr	x4,[x3,TX_KIND]
	str	x4,[x1,16]
	ldp	x4,x5,[x3,TX_W]
	stp	x4,x5,[x1,24]
	ldr	x4,[x3,TX_VA]
	str	x4,[x1,40]
	mov	x0,0
	ret
.bad:	mov	x0,GK_E_INVAL
	ret

; w0 = texture, w1 = x, w2 = y, w3 = l, w4 = h, x5 = pixels, w6 = pas -> 0, GK_E_*
gk_texture_rect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	sxtw	x19,w0
	sxtw	x20,w1
	sxtw	x21,w2
	sxtw	x22,w3
	sxtw	x23,w4
	mov	x24,x5
	sxtw	x25,w6
	bl	gk_lock
	bl	gk_ready
	mov	x9,GK_E_NOGPU
	cbz	x0,.done
	mov	x0,x19
	bl	tex_entry
	mov	x9,GK_E_INVAL
	cbz	x0,.done
	mov	x19,x0
	cbz	x24,.done
	cmp	x22,0
	b.le	.done
	cmp	x23,0
	b.le	.done
	tbnz	x20,63,.done
	tbnz	x21,63,.done
	cmp	x25,x22
	b.lt	.done
	ldp	x0,x1,[x19,TX_W]
	add	x2,x20,x22
	cmp	x2,x0
	b.gt	.done
	add	x2,x21,x23
	cmp	x2,x1
	b.gt	.done
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	x4,x23
	mov	x5,x24
	mov	x6,x25
	bl	tex_store
	mov	x9,0
.done:	mov	x19,x9
	bl	gk_unlock
	mov	x0,x19
	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  API : gk_render
; ---------------------------------------------------------------------------

; x0 = cible GKF_*, x1 = sommets, w2 = nombre, x3 = lots, w4 = nombre -> 0, GK_E_*
gk_render:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2				; (non signes)
	mov	x22,x3
	mov	w23,w4
	bl	gk_lock
	bl	gk_ready
	mov	x28,GK_E_NOGPU
	cbz	x0,.done
	mov	x28,GK_E_INVAL
	ldr	x24,[x19,GKF_PIXELS]		; la cible
	ldrsw	x25,[x19,GKF_W]
	ldrsw	x26,[x19,GKF_H]
	ldrsw	x27,[x19,GKF_STRIDE]
	cbz	x24,.done
	tst	x24,3
	b.ne	.done
	cmp	x25,0
	b.le	.done
	cmp	x26,0
	b.le	.done
	cmp	x25,GK_MAX_W
	b.gt	.done
	cmp	x26,GK_MAX_H
	b.gt	.done
	cmp	x27,x25
	b.lt	.done
	movl	x0,GK_MAX_VERTS
	cmp	x21,x0
	b.hi	.done
	cmp	x23,GK_MAX_BATCHES
	b.hi	.done
	cbz	x21,.v_ok
	cbz	x20,.done
.v_ok:	cbz	x23,.b_ok
	cbz	x22,.done
.b_ok:	bl	gk_check_batches		; lots et sommets
	cbz	x0,.done
	; la cible : directement si le GPU l'atteint, sinon par le tampon d'etape
	mov	x0,x24
	sub	x1,x26,1
	mul	x1,x1,x27
	add	x1,x1,x25
	lsl	x1,x1,2
	bl	gk_phys
	adr	x9,gk_job
	cbz	x0,.stage
	str	x0,[x9,JB_TGT]
	lsl	x1,x27,2
	str	x1,[x9,JB_STRIDE]
	mov	x0,x24				; (adresse virtuelle de la cible)
	b	.tgt
.stage:	mul	x0,x25,x26
	lsl	x0,x0,2
	bl	gk_stage_get
	mov	x28,GK_E_NOMEM
	cbz	x0,.done
	adr	x9,gk_job
	str	x1,[x9,JB_TGT]
	lsl	x1,x25,2
	str	x1,[x9,JB_STRIDE]
	ldr	w2,[x19,GKF_FLAGS]
	tbz	w2,0,.tgt			; GK_F_KEEP : les pixels d'abord
	mov	x3,x24
	mov	x4,x0
	mov	x5,x26
.in:	mov	x6,0
.inx:	ldr	w7,[x3,x6,lsl 2]
	str	w7,[x4,x6,lsl 2]
	add	x6,x6,1
	cmp	x6,x25
	b.lo	.inx
	add	x3,x3,x27,lsl 2
	add	x4,x4,x25,lsl 2
	subs	x5,x5,1
	b.ne	.in
.tgt:	str	x0,[sp,-16]!			; cible du GPU (virtuelle)
	adr	x9,gk_job
	ldr	w0,[x19,GKF_CLEAR]
	str	x0,[x9,JB_CLEAR]
	stp	x25,x26,[x9,JB_W]
	ldr	w2,[x19,GKF_FLAGS]
	and	x2,x2,GK_F_KEEP or GK_F_ALPHA
	str	x2,[x9,JB_FLAGS]
	mov	x0,0				; GK_F_KEEP : chargee depuis elle-meme
	tbz	w2,0,.nokeep
	ldr	x0,[x9,JB_TGT]
.nokeep:
	ldr	x1,[x9,JB_STRIDE]
	stp	x0,x1,[x9,JB_SRC]
	str	xzr,[x9,JB_N]
	stp	x23,x22,[x9,JB_NB]
	adr	x0,gk_wk			; les sommets
	ldr	x0,[x0,8]
	add	x0,x0,WK_VERTS
	lsl	x1,x21,5
	mov	x2,0
	cbz	x1,.copied
.cp:	add	x5,x20,x2
	ldp	x3,x4,[x5]
	add	x5,x0,x2
	stp	x3,x4,[x5]
	add	x2,x2,16
	cmp	x2,x1
	b.lo	.cp
	bl	dc_clean
.copied:
	ldr	x0,[sp],16
	str	x0,[sp,-16]!
	mov	x1,0
	bl	gk_submit
	ldr	x9,[sp],16
	cbz	x0,.failed
	mov	x28,0
	cmp	x9,x24				; tampon d'etape : vers la cible
	b.eq	.done
	mov	x3,x9
	mov	x4,x24
	mov	x5,x26
.out:	mov	x6,0
.outx:	ldr	w7,[x3,x6,lsl 2]
	str	w7,[x4,x6,lsl 2]
	add	x6,x6,1
	cmp	x6,x25
	b.lo	.outx
	add	x3,x3,x25,lsl 2
	add	x4,x4,x27,lsl 2
	subs	x5,x5,1
	b.ne	.out
	b	.done
.failed:
	adr	x0,gk_state			; perdu ou refuse
	ldr	x0,[x0]
	cmp	x0,ST_CPU
	mov	x28,GK_E_LOST
	mov	x0,GK_E_INVAL
	csel	x28,x28,x0,eq
.done:	bl	gk_unlock
	mov	x0,x28
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = octets -> x0 = tampon d'etape (a zero la premiere fois), x1 = physique ;
; 0 : pas de memoire
gk_stage_get:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x20,gk_stage
	ldp	x0,x1,[x20]
	cbz	x0,.new
	cmp	x1,x19
	b.hs	.have
	bl	gk_free				; trop petit
	stp	xzr,xzr,[x20]
.new:	mov	x0,x19
	bl	gk_alloc
	cbz	x0,.none
	str	x0,[x20]
	mov	x1,x19
	bl	gk_phys
	cbz	x0,.lost
	stp	x19,x0,[x20,8]
.have:	ldr	x0,[x20]
	ldr	x1,[x20,16]
	b	.out
.lost:	ldr	x0,[x20]
	bl	gk_free
	str	xzr,[x20]
.none:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; Lots de gk_render (x20 = sommets, x21 = nombre, x22 = lots, x23 = nombre) :
; chaque lot dans les sommets, sa texture au process, son melange connu, ses
; sommets dans la bande de garde -> x0 = 1 bons
gk_check_batches:
	stp	d8,d9,[sp,-16]!
	movl	w9,0x40800000			; 4.0
	fmov	s8,w9
	mov	x10,0				; lot
.batch:	cmp	x10,x23
	b.hs	.good
	mov	x9,GKB_SIZE
	madd	x11,x10,x9,x22
	ldr	w12,[x11,GKB_COUNT]
	cbz	w12,.next
	ldr	w13,[x11,GKB_FIRST]
	add	x14,x13,x12
	cmp	x14,x21
	b.hi	.bad
	ldr	w15,[x11,GKB_FLAGS]
	ubfx	w9,w15,GK_B_BLEND_SHIFT,4
	cmp	w9,GK_BLEND_LAST
	b.hi	.bad
	ldrsw	x9,[x11,GKB_TEX]
	tbnz	x9,63,.verts			; -1 : pas de texture
	cmp	x9,GK_MAX_TEXTURES
	b.hs	.bad
	adr	x16,gk_wk
	ldr	x16,[x16,8]
	add	x16,x16,WK_TEX
	add	x16,x16,x9,lsl 7
	ldr	x9,[x16,TX_CHUNK]
	cbz	x9,.bad
.verts:	cmp	x13,x14				; ses sommets
	b.hs	.next
	add	x16,x20,x13,lsl 5
	ldp	s0,s1,[x16,GKV_X]
	ldp	s2,s3,[x16,GKV_Z]
	tbnz	w15,17,.clip			; GK_B_NOMATRIX
	add	x17,x11,GKB_MATRIX
	ldp	s16,s17,[x17]			; x' = M0 . v
	ldp	s18,s19,[x17,8]
	fmul	s4,s16,s0
	fmadd	s4,s17,s1,s4
	fmadd	s4,s18,s2,s4
	fmadd	s4,s19,s3,s4
	ldp	s16,s17,[x17,16]
	ldp	s18,s19,[x17,24]
	fmul	s5,s16,s0
	fmadd	s5,s17,s1,s5
	fmadd	s5,s18,s2,s5
	fmadd	s5,s19,s3,s5
	ldp	s16,s17,[x17,32]
	ldp	s18,s19,[x17,40]
	fmul	s6,s16,s0
	fmadd	s6,s17,s1,s6
	fmadd	s6,s18,s2,s6
	fmadd	s6,s19,s3,s6
	ldp	s16,s17,[x17,48]
	ldp	s18,s19,[x17,56]
	fmul	s7,s16,s0
	fmadd	s7,s17,s1,s7
	fmadd	s7,s18,s2,s7
	fmadd	s3,s19,s3,s7
	fmov	s0,s4
	fmov	s1,s5
	fmov	s2,s6
.clip:	fcmp	s3,0.0				; w > 0 (NaN : refuse)
	b.le	.bad
	fmul	s4,s3,s8			; |x|, |y| <= 4 w
	fabs	s5,s0
	fcmp	s5,s4
	b.hi	.bad
	fabs	s5,s1
	fcmp	s5,s4
	b.hi	.bad
	fneg	s5,s3				; z >= -w
	fcmp	s2,s5
	b.lt	.bad
	add	x13,x13,1
	b	.verts
.next:	add	x10,x10,1
	b	.batch
.good:	mov	x0,1
	ldp	d8,d9,[sp],16
	ret
.bad:	mov	x0,0
	ldp	d8,d9,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Listes de gk_render (appelees par gk_build : x27 = curseur, x28 = gk_job,
;  x19 = l, x20 = h, x23 = l/2, x24 = h/2)
; ---------------------------------------------------------------------------

; x27 aligne sur x0 (puissance de 2) avec des zeros
cl_align:
	sub	x1,x0,1
.l:	tst	x27,x1
	b.eq	.r
	strb	wzr,[x27],1
	b	.l
.r:	ret

; x0 = adresse virtuelle dans le tampon de travail -> son adresse physique
; au curseur, ou x0 | x1 (drapeaux des bits bas)
cl_addr_or:
	adr	x2,gk_wk
	ldr	x3,[x2,8]
	ldr	x2,[x2,16]
	sub	x0,x0,x3
	add	x0,x0,x2
	orr	x0,x0,x1
	b	cl_u32

; la matrice du lot x21 (identite avec GK_B_NOMATRIX), puis echelles x / y
cl_matrix:
	str	x30,[sp,-16]!
	ldr	w0,[x21,GKB_FLAGS]
	add	x10,x21,GKB_MATRIX
	adr	x9,identity
	tst	w0,GK_B_NOMATRIX
	csel	x10,x9,x10,ne
	mov	x11,0
.m:	ldr	w0,[x10,x11,lsl 2]
	bl	cl_u32
	add	x11,x11,1
	cmp	x11,16
	b.lo	.m
	lsl	x0,x23,8			; x : (l / 2) * 256
	bl	int_to_f32
	bl	cl_u32
	lsl	x0,x24,8			; y : -(h / 2) * 256
	neg	x0,x0
	bl	int_to_f32
	bl	cl_u32
	ldr	x30,[sp],16
	ret

; un attribut : x0 = decalage dans le sommet, w1 = type (vec_size | type << 2 |
; normalise), w2 = valeurs lues (CS | VS << 4)
cl_attr:
	str	x30,[sp,-16]!
	stp	x1,x2,[sp,-16]!
	adr	x1,gk_wk
	ldr	x1,[x1,16]
	add	x1,x1,WK_VERTS
	add	w0,w0,w1
	bl	cl_u32
	ldp	x1,x2,[sp],16
	strb	w1,[x27],1
	strb	w2,[x27],1
	mov	w0,0				; diviseur d'instance
	bl	cl_u16
	mov	w0,GKV_SIZE
	bl	cl_u32
	movl	w0,0xFFFFFF
	bl	cl_u32
	ldr	x30,[sp],16
	ret

; Liste indirecte : uniforms, sampler, etat de texture et shader state record
; de chaque lot ; leur adresse physique dans WK_RECS (0 : lot vide)
gk_ind_batches:
	str	x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x22,0				; lot
.batch:	ldr	x0,[x28,JB_NB]
	cmp	x22,x0
	b.hs	.end
	ldr	x21,[x28,JB_BATCHES]
	mov	x0,GKB_SIZE
	madd	x21,x22,x0,x21
	adr	x0,gk_wk
	ldr	x0,[x0,8]
	add	x0,x0,WK_RECS
	ldr	w1,[x21,GKB_COUNT]
	str	wzr,[x0,x22,lsl 2]
	cbz	w1,.next
	mov	x0,4
	bl	cl_align
	mov	x25,x27				; uniforms du vertex shader
	bl	cl_matrix
	movl	w0,F32_HALF			; echelle et decalage z
	bl	cl_u32
	movl	w0,F32_HALF
	bl	cl_u32
	mov	x26,x27				; uniforms du coordinate shader
	bl	cl_matrix
	stp	x25,x26,[sp,-16]!
	ldr	w9,[x21,GKB_FLAGS]
	ldrsw	x10,[x21,GKB_TEX]
	mov	x25,x27				; uniforms du fragment shader
	tbnz	x10,63,.notex
	mov	x0,32				; sampler (32 octets)
	bl	cl_align
	mov	x26,x27
	stp	xzr,xzr,[x27]
	stp	xzr,xzr,[x27,16]
	mov	x0,x27
	ldr	w9,[x21,GKB_FLAGS]
	mov	w3,1				; le plus proche (sans GK_B_LINEAR)
	tst	w9,GK_B_LINEAR
	csel	w3,wzr,w3,ne
	mov	x1,0				; mag
	mov	x2,1
	bl	set_bits
	mov	x1,1				; min
	mov	x2,1
	bl	set_bits
	mov	x1,2				; mip le plus proche
	mov	x2,1
	mov	x3,1
	bl	set_bits
	ldr	w9,[x21,GKB_FLAGS]
	ubfx	w3,w9,GK_B_WRAP_S_SHIFT,2
	adr	x4,wrap_mode
	ldrb	w3,[x4,x3]
	mov	x1,48
	mov	x2,3
	bl	set_bits
	ldr	w9,[x21,GKB_FLAGS]
	ubfx	w3,w9,GK_B_WRAP_T_SHIFT,2
	adr	x4,wrap_mode
	ldrb	w3,[x4,x3]
	mov	x1,51
	mov	x2,3
	bl	set_bits
	mov	x1,54				; r : au bord
	mov	x2,3
	mov	x3,1
	bl	set_bits
	add	x27,x27,32
	ldrsw	x0,[x21,GKB_TEX]		; l'etat de la texture (32 octets)
	adr	x1,gk_wk
	ldr	x1,[x1,8]
	add	x1,x1,WK_TEX
	add	x1,x1,x0,lsl 7
	mov	x25,x27
	ldp	x2,x3,[x1,TX_STATE]
	stp	x2,x3,[x27],16
	ldp	x2,x3,[x1,TX_STATE+16]
	stp	x2,x3,[x27],16
	mov	x0,x25				; p0 : etat, 2 mots (RG, BA en f16)
	mov	x25,x27
	mov	x1,3
	bl	cl_addr_or
	mov	x0,x26				; p1 : sampler, sortie 16 bits
	bl	cl_addr
.notex:	ldr	w9,[x21,GKB_FLAGS]
	tbz	w9,18,.noat			; GK_B_ALPHATEST : le seuil / 255
	ubfx	w0,w9,GK_B_AT_SHIFT,8
	scvtf	s0,w0
	movl	w1,0x437F0000			; 255.0
	fmov	s1,w1
	fdiv	s0,s0,s1
	fmov	w0,s0
	bl	cl_u32
	b	.rec
.noat:	ldrsw	x10,[x21,GKB_TEX]
	tbz	x10,63,.rec
	mov	w0,0				; (au moins un uniform)
	bl	cl_u32
.rec:	mov	x0,32
	bl	cl_align
	ldp	x10,x11,[sp],16			; uniforms vertex, coordonnees
	adr	x2,gk_wk			; adresse physique du record
	ldr	x3,[x2,8]
	ldr	x2,[x2,16]
	sub	x1,x27,x3
	add	x1,x1,x2
	add	x3,x3,WK_RECS
	str	w1,[x3,x22,lsl 2]
	stp	x10,x11,[sp,-16]!
	cl8	0x02				; decoupe
	cl8	0x10				; centre de pixel reel (w2)
	cl8	0x04				; pas de varyings implicites point/ligne
	cl8	10				; 10 varyings : s t r g b a r2 g2 b2 a2
	cl8	1				; segments VPM : coordonnees 1, 1
	cl8	1
	cl8	2				;   vertex 2, 2
	cl8	2
	adr	x0,gk_wk			; attributs par defaut
	ldr	x10,[x0,16]
	mov	w0,w10
	bl	cl_u32
	ldr	w9,[x21,GKB_FLAGS]		; fragment : 4 voies, NaN
	ldrsw	x10,[x21,GKB_TEX]
	mov	x0,ST_FS_COLOR
	mov	x1,ST_FS_TEX
	tst	x10,x10
	csel	x0,x1,x0,ge
	tbz	w9,18,.fs
	add	x0,x0,ST_FS_COLOR_AT - ST_FS_COLOR
.fs:	adr	x1,gk_wk
	ldr	x1,[x1,16]
	add	w0,w0,w1
	add	w0,w0,5			; (adresse alignee sur 8)
	bl	cl_u32
	mov	x0,x25
	bl	cl_addr
	ldp	x10,x11,[sp],16
	stp	x10,x11,[sp,-16]!
	adr	x0,gk_wk			; vertex : 4 voies, section finale, NaN
	ldr	x0,[x0,16]
	add	w0,w0,ST_VS_CLIP
	orr	w0,w0,7
	bl	cl_u32
	ldr	x0,[sp]
	bl	cl_addr
	adr	x0,gk_wk			; coordonnees
	ldr	x0,[x0,16]
	add	w0,w0,ST_CS_CLIP
	orr	w0,w0,7
	bl	cl_u32
	ldp	x10,x11,[sp],16
	mov	x0,x11
	bl	cl_addr
	mov	x0,GKV_X			; x y z w : 4 flottants
	mov	w1,0x08
	mov	w2,0x44
	bl	cl_attr
	mov	x0,GKV_S			; s t : 2 flottants (vertex shader)
	mov	w1,0x0A
	mov	w2,0x20
	bl	cl_attr
	mov	x0,GKV_RGBA			; couleur : 4 octets normalises
	mov	w1,0x50
	mov	w2,0x40
	bl	cl_attr
	mov	x0,GKV_RGBA2			; couleur 2
	mov	w1,0x50
	mov	w2,0x40
	bl	cl_attr
.next:	add	x22,x22,1
	b	.batch
.end:	ldp	x25,x26,[sp],16
	ldp	x21,x22,[sp],16
	ldr	x30,[sp],16
	ret

; Liste de binning : l'etat et les triangles de chaque lot
gk_bcl_batches:
	str	x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x22,0
	mov	x26,-1				; melange precedent
.batch:	ldr	x0,[x28,JB_NB]
	cmp	x22,x0
	b.hs	.end
	ldr	x21,[x28,JB_BATCHES]
	mov	x0,GKB_SIZE
	madd	x21,x22,x0,x21
	adr	x0,gk_wk
	ldr	x0,[x0,8]
	add	x0,x0,WK_RECS
	ldr	w25,[x0,x22,lsl 2]
	cbz	w25,.next
	ldr	w9,[x21,GKB_FLAGS]
	cl8	96				; CFG_BITS
	ubfx	w1,w9,5,1			; faces : avant sans CULL_FRONT,
	eor	w1,w1,1				;   arriere sans CULL_BACK
	ubfx	w2,w9,4,1
	eor	w2,w2,1
	orr	w0,w1,w2,lsl 1
	orr	w0,w0,4				; sens horaire
	strb	w0,[x27],1
	and	w1,w9,GK_B_ZFUNC		; profondeur (0 : LESS = 1)
	cmp	w1,0
	mov	w2,1
	csel	w1,w2,w1,eq
	lsl	w0,w1,4
	tst	w9,GK_B_NOZWRITE		; ecriture sauf NOZWRITE ou ALWAYS
	b.ne	.noz
	cmp	w1,7
	b.eq	.noz
	orr	w0,w0,0x80
.noz:	strb	w0,[x27],1
	ubfx	w10,w9,GK_B_BLEND_SHIFT,4
	cmp	w10,0
	mov	w0,8				; melange
	csel	w0,w0,wzr,ne
	strb	w0,[x27],1
	cbz	w10,.shader
	cmp	x10,x26
	b.eq	.shader
	mov	x26,x10
	cl8	83				; BLEND_ENABLES (cible 0)
	cl8	1
	cl8	84				; BLEND_CFG
	adr	x1,blend_factors
	mov	x2,6
	madd	x1,x10,x2,x1
	ldrb	w2,[x1,5]			; alpha : mode, source, destination
	ldrb	w3,[x1,3]
	orr	w0,w2,w3,lsl 4
	ldrb	w3,[x1,4]
	orr	w0,w0,w3,lsl 8
	ldrb	w3,[x1,2]			; couleur : mode, source, destination
	orr	w0,w0,w3,lsl 12
	ldrb	w3,[x1,0]
	orr	w0,w0,w3,lsl 16
	ldrb	w3,[x1,1]
	orr	w0,w0,w3,lsl 20
	orr	w0,w0,0x0F000000		; cibles 0-3
	bl	cl_u32
.shader:
	cl8	64				; GL_SHADER_STATE (record, 4 attributs)
	orr	w0,w25,4
	bl	cl_u32
	cl8	36				; VERTEX_ARRAY_PRIMS (triangles)
	cl8	4
	ldr	w0,[x21,GKB_COUNT]
	bl	cl_u32
	ldr	w0,[x21,GKB_FIRST]
	bl	cl_u32
.next:	add	x22,x22,1
	b	.batch
.end:	ldp	x25,x26,[sp],16
	ldp	x21,x22,[sp],16
	ldr	x30,[sp],16
	ret

; ---------------------------------------------------------------------------
;  gk_debug (essais)
; ---------------------------------------------------------------------------
; x0 = GK_DBG_*, x1, x2 -> 0, GK_E_*
gk_debug:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	gk_lock
	mov	x22,0
	cmp	x19,GK_DBG_DRY
	b.ne	.lists
	adr	x0,gk_dry
	str	x20,[x0]
	b	.done
.lists:	bl	gk_ready
	mov	x22,GK_E_NOGPU
	cbz	x0,.done
	mov	x22,GK_E_INVAL
	cmp	x19,GK_DBG_LISTS
	b.ne	.tex
	adr	x9,gk_wk
	ldr	x10,[x9,8]
	adr	x11,gk_sizes
	add	x0,x10,WK_BCL
	ldr	x1,[x11]
	stp	x0,x1,[x20,GKD_BCL]
	add	x0,x10,WK_RCL
	ldr	x1,[x11,8]
	stp	x0,x1,[x20,GKD_RCL]
	add	x0,x10,WK_IND
	ldr	x1,[x11,16]
	stp	x0,x1,[x20,GKD_IND]
	ldr	x0,[x9,16]
	adr	x1,gk_job
	ldr	x1,[x1,JB_TGT]
	stp	x0,x1,[x20,GKD_WK_PHYS]
	mov	x22,0
	b	.done
.tex:	cmp	x19,GK_DBG_TEX
	b.ne	.done
	mov	x0,x20
	bl	tex_entry
	cbz	x0,.done
	ldr	x1,[x0,TX_VA]
	ldr	x2,[x0,TX_NPAGES]
	lsl	x2,x2,12
	stp	x1,x2,[x21,GKD_TEX_VA]
	ldr	x1,[x0,TX_PHYS]
	str	x1,[x21,GKD_TEX_PHYS]
	mov	x22,0
.done:	bl	gk_unlock
	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

		align 4
identity	dw F32_ONE, 0, 0, 0,  0, F32_ONE, 0, 0,  0, 0, F32_ONE, 0,  0, 0, 0, F32_ONE
wrap_mode	db 0, 1, 2, 0			; repete, bord, miroir
; facteurs de melange (couleur source, destination, equation, puis alpha) :
; 0 zero, 1 un, 3 1 - couleur source, 4 couleur destination, 6 alpha source,
; 7 1 - alpha source, 9 1 - alpha destination ; equations 0 +, 2 d - s, 4 max
blend_factors	db 1, 0, 0, 1, 0, 0		; (aucun)
		db 6, 7, 0, 1, 7, 0		; ALPHA
		db 6, 1, 0, 6, 1, 0		; ADD
		db 4, 0, 0, 4, 0, 0		; MUL
		db 1, 7, 0, 1, 7, 0		; PREMUL
		db 4, 7, 0, 0, 1, 0		; MULCOL
		db 9, 1, 0, 1, 7, 0		; UNDER
		db 1, 3, 0, 1, 7, 0		; SCREEN
		db 1, 1, 0, 1, 7, 0		; PLUS
		db 1, 1, 2, 0, 1, 0		; RSUB
		db 1, 1, 4, 1, 7, 0		; LIGHTEN
		db 0, 6, 0, 0, 6, 0		; DSTIN
		db 0, 7, 0, 0, 7, 0		; DSTOUT
		align 8
