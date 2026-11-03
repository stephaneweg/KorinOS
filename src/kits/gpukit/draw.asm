; ============================================================================
;  kits/gpukit/draw.asm - gk_draw : une image 3D complete (docs/V3D.md)
;
;  gk_render prend des sommets deja dans la bande de garde et s'arrete sans
;  GPU. gk_draw fait le reste sur le processeur, pour un programme qui ne veut
;  penser ni a l'un ni a l'autre :
;  * chaque triangle transforme par la matrice de son lot, rejete s'il sort de
;    l'image, decoupe contre le plan proche et la bande de garde (la recette
;    de v3d_clip.h d'Onyx) ;
;  * l'image donnee a gk_render ; s'il refuse (pas de GPU, GPU perdu), le
;    rasteriseur du processeur ci-dessous, memes faces, memes melanges 0 a 4,
;    meme test d'alpha : resultat de bas3d.h d'Onyx, a l'arrondi pres.
;
;  Le rasteriseur va par lignes (pas de fonctions d'arete par pixel), avec les
;  gradients du triangle : le plan z seul quand le test le rejette, une
;  division par pixel seulement si la couleur ou la texture varient, et une
;  boucle a part pour les triangles unis et opaques, l'essentiel d'un decor en
;  low poly. Ordre des canaux interne B G R A : celui des pixels et des
;  textures en memoire ; seules les couleurs des sommets (R G B A) sont
;  retournees.
; ============================================================================

GD_CAP		= GK_MAX_VERTS			; sommets apres decoupe
GD_ROOM		= 21				; un triangle decoupe : 21 sommets au plus
GD_VERT_BYTES	= (GD_CAP + GD_ROOM)*GKV_SIZE
GD_BAT_BYTES	= GK_MAX_BATCHES*GKB_SIZE
GD_POLY_MAX	= 10				; sommets d'un polygone en decoupe
GD_PV		= 64				; un sommet de polygone : 16 flottants (x y z w,
						; s t 0 0, r g b a, r2 g2 b2 a2)
F32_FOUR	= 0x40800000
F32_255		= 0x437F0000
F32_EPS		= 0x3727C5AC			; 1e-5
F32_INV255	= 0x3B808081			; 1 / 255

; x0 -> 0 si pas de memoire ; region a zero a la demande
gd_map:
	mov	x1,x0
	mov	x0,0
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	mov	x8,SYS_VM_MAP
	svc	0
	cmp	x0,0
	csel	x0,xzr,x0,lt
	ret

; ---------------------------------------------------------------------------
;  API : gk_draw
; ---------------------------------------------------------------------------

; x0 = cible GKF_*, x1 = sommets, w2 = nombre, x3 = lots, w4 = nombre, x5 = textures
; GKT_* (GKB_TEX : numero dans cette table, < 0 : aucune) -> 1 GPU, 0 CPU, GK_E_*
gk_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	mov	x22,x3
	mov	w23,w4
	mov	x24,x5
	mov	x28,GK_E_INVAL
	ldr	x0,[x19,GKF_PIXELS]
	cbz	x0,.out
	ldrsw	x1,[x19,GKF_W]
	ldrsw	x2,[x19,GKF_H]
	ldrsw	x3,[x19,GKF_STRIDE]
	cmp	x1,0
	b.le	.out
	cmp	x2,0
	b.le	.out
	cmp	x3,x1
	b.lt	.out
	cmp	x23,GK_MAX_BATCHES
	b.hi	.out
	cbz	x21,.v
	cbz	x20,.out
.v:	cbz	x23,.lock
	cbz	x22,.out
.lock:	adr	x9,gd_lk
.try:	ldaxr	w10,[x9]
	cbnz	w10,.busy
	mov	w10,1
	stxr	w11,w10,[x9]
	cbnz	w11,.try
	b	.mine
.busy:	clrex
	mov	x8,SYS_YIELD
	svc	0
	b	.lock
.mine:	mov	x28,GK_E_NOMEM
	adr	x25,gd_verts
	ldr	x0,[x25]
	cbnz	x0,.have
	movl	x0,GD_VERT_BYTES
	bl	gd_map
	cbz	x0,.done
	str	x0,[x25]
	movl	x0,GD_BAT_BYTES
	bl	gd_map
	cbz	x0,.done
	str	x0,[x25,8]
.have:	bl	gd_clip				; -> x25 sommets, x26 lots
	; le GPU, si toutes les textures y sont
	adr	x9,gd_verts
	ldp	x1,x3,[x9]
	mov	x10,x3
	mov	x11,x26
.ids:	cbz	x11,.gpu
	ldrsw	x12,[x10,GKB_MATRIX]		; (la table : gardee la pour le CPU)
	mov	w13,-1
	tbnz	x12,63,.idset
	mov	x13,GKT_SIZE
	madd	x13,x12,x13,x24
	ldr	w13,[x13,GKT_ID]
	tbnz	w13,31,.cpu
.idset:	str	w13,[x10,GKB_TEX]
	add	x10,x10,GKB_SIZE
	sub	x11,x11,1
	b	.ids
.gpu:	mov	x0,x19
	mov	w2,w25
	mov	w4,w26
	bl	gk_render
	mov	x28,1
	cbz	x0,.done
.cpu:	bl	gd_soft
	mov	x28,x0
.done:	adr	x9,gd_lk
	stlr	wzr,[x9]
.out:	mov	x0,x28
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  La decoupe (x19-x24 : arguments de gk_draw) -> x25 sommets, x26 lots dans
;  gd_verts, gd_bat, en espace de decoupe (GK_B_NOMATRIX) ; numero de texture
;  de la table dans GKB_MATRIX. Ce qui depasse GD_CAP est laisse.
; ---------------------------------------------------------------------------

; v4 = sommet -> v7 = (x > w, y > w, z > w, 0), v20 = (x < -w, y < -w, z < -w, w < 0),
; w0 = 1 s'il est dans la bande de garde
macro gd_codes r {
	dup	v21.4s,r#.s[3]
	fcmgt	v7.4s,r#.4s,v21.4s
	fneg	v22.4s,v21.4s
	fcmgt	v20.4s,v22.4s,r#.4s
	fmul	v23.4s,v21.4s,v30.4s		; (4 w, 4 w, w, w)
	fabs	v24.4s,r#.4s			; (|x|, |y|, -z, epsilon)
	fneg	v25.4s,r#.4s
	ins	v24.s[2],v25.s[2]
	ins	v24.s[3],v31.s[0]
	fcmge	v24.4s,v23.4s,v24.4s		; (NaN : non)
	uminv	s24,v24.4s
	fmov	w0,s24
}

gd_clip:
	prologue
	movl	w0,F32_FOUR			; v30 = (4, 4, 1, 1)
	dup	v30.4s,w0
	fmov	s0,1.0
	ins	v30.s[2],v0.s[0]
	ins	v30.s[3],v0.s[0]
	movl	w0,F32_EPS
	fmov	s31,w0
	adr	x9,gd_verts
	ldp	x12,x16,[x9]			; x12 sortie, x16 lot de sortie
	mov	x17,x12
	movl	x13,GD_CAP*GKV_SIZE
	add	x13,x13,x12			; limite (GD_ROOM de marge)
	mov	x26,0
	mov	x27,0				; lot d'entree
.batch:	cmp	x27,x23
	b.hs	.end
	mov	x0,GKB_SIZE
	madd	x11,x27,x0,x22
	add	x27,x27,1
	ldr	w2,[x11,GKB_FIRST]
	ldr	w3,[x11,GKB_COUNT]
	add	x4,x2,x3			; [first, first + count) dans les sommets
	cmp	x4,x21
	csel	x4,x21,x4,hi
	cmp	x2,x4
	b.hs	.batch
	sub	x28,x4,x2
	mov	x0,3
	udiv	x28,x28,x0			; x28 triangles entiers
	cbz	x28,.batch
	add	x10,x20,x2,lsl 5
	ldr	w15,[x11,GKB_FLAGS]
	add	x0,x11,GKB_MATRIX
	tbz	w15,17,.mat
	adr	x0,gd_ident
.mat:	ld4	{v16.4s,v17.4s,v18.4s,v19.4s},[x0]	; (colonnes)
	mov	x14,x12				; debut du lot en sortie
.tri:	cmp	x12,x13
	b.hs	.bend
	ldp	q1,q2,[x10]
	fmul	v4.4s,v16.4s,v1.s[0]
	fmla	v4.4s,v17.4s,v1.s[1]
	fmla	v4.4s,v18.4s,v1.s[2]
	fmla	v4.4s,v19.4s,v1.s[3]
	stp	q4,q2,[x12]
	ldp	q1,q2,[x10,32]
	fmul	v5.4s,v16.4s,v1.s[0]
	fmla	v5.4s,v17.4s,v1.s[1]
	fmla	v5.4s,v18.4s,v1.s[2]
	fmla	v5.4s,v19.4s,v1.s[3]
	stp	q5,q2,[x12,32]
	ldp	q1,q2,[x10,64]
	fmul	v6.4s,v16.4s,v1.s[0]
	fmla	v6.4s,v17.4s,v1.s[1]
	fmla	v6.4s,v18.4s,v1.s[2]
	fmla	v6.4s,v19.4s,v1.s[3]
	stp	q6,q2,[x12,64]
	add	x10,x10,96
	gd_codes v4
	mov	w1,w0
	mov	v26.16b,v7.16b
	mov	v27.16b,v20.16b
	gd_codes v5
	and	w1,w1,w0
	and	v26.16b,v26.16b,v7.16b
	and	v27.16b,v27.16b,v20.16b
	gd_codes v6
	and	w1,w1,w0
	and	v26.16b,v26.16b,v7.16b		; tous dehors du meme cote : rien a dessiner
	and	v27.16b,v27.16b,v20.16b
	orr	v26.16b,v26.16b,v27.16b
	umaxv	s26,v26.4s
	fmov	w0,s26
	cbnz	w0,.next
	cbz	w1,.slow
	add	x12,x12,96
	b	.next
.slow:	bl	gd_clip_tri
	add	x12,x12,x0,lsl 5
.next:	subs	x28,x28,1
	b.ne	.tri
.bend:	cmp	x12,x14				; le lot de sortie (vide : pas de lot)
	b.eq	.batch
	sub	x0,x14,x17
	lsr	x0,x0,5
	str	w0,[x16,GKB_FIRST]
	sub	x0,x12,x14
	lsr	x0,x0,5
	str	w0,[x16,GKB_COUNT]
	ldrsw	x0,[x11,GKB_TEX]
	cbnz	x24,.tset
	mov	x0,-1				; (pas de table : pas de texture)
.tset:	str	w0,[x16,GKB_TEX]
	str	w0,[x16,GKB_MATRIX]
	orr	w0,w15,1 shl 17
	str	w0,[x16,GKB_FLAGS]
	add	x16,x16,GKB_SIZE
	add	x26,x26,1
	b	.batch
.end:	sub	x25,x12,x17
	lsr	x25,x25,5
	epilogue

; x12 = 3 sommets GKV_* -> x0 = sommets ecrits en x12 (eventail). Ne touche que x0-x8, x9,
; v0-v7, v20-v29. La recette de V3DClipTriangle : plan par plan, un polygone de 10 sommets.
gd_clip_tri:
	sub	sp,sp,2*GD_POLY_MAX*GD_PV + 16
	str	x30,[sp]
	add	x4,sp,16			; x4 polygone A, x5 polygone B
	add	x5,x4,GD_POLY_MAX*GD_PV
	mov	x0,x12
	mov	x1,x4
	bl	gd_tofloat
	add	x0,x12,32
	add	x1,x4,GD_PV
	bl	gd_tofloat
	add	x0,x12,64
	add	x1,x4,2*GD_PV
	bl	gd_tofloat
	mov	x6,3				; x6 sommets de A
	mov	x7,0				; x7 plan
.plane:	cmp	x7,6
	b.hs	.fan
	cmp	x6,3
	b.lo	.none
	mov	x8,0				; x8 sommets de B
	mov	x3,0				; x3 = k
.edge:	cmp	x3,x6
	b.hs	.swap
	mov	x0,GD_PV
	madd	x1,x3,x0,x4			; a
	add	x2,x3,1
	cmp	x2,x6
	csel	x2,xzr,x2,hs
	madd	x2,x2,x0,x4			; b
	mov	x0,x1
	bl	gd_dist
	fmov	s26,s0
	mov	x0,x2
	bl	gd_dist
	fmov	s27,s0				; s26 = da, s27 = db
	fcmp	s26,0.0				; ina (NaN : dehors)
	cset	w9,ge
	b.lt	.noa
	cmp	x8,GD_POLY_MAX
	b.hs	.noa
	mov	x0,GD_PV
	madd	x0,x8,x0,x5
	ldp	q0,q1,[x1]
	ldp	q2,q3,[x1,32]
	stp	q0,q1,[x0]
	stp	q2,q3,[x0,32]
	add	x8,x8,1
.noa:	fcmp	s27,0.0
	cset	w0,ge
	cmp	w0,w9
	b.eq	.nextk
	cmp	x8,GD_POLY_MAX
	b.hs	.nextk
	fcmp	s26,s26				; (NaN : pas d'intersection)
	b.vs	.nextk
	fcmp	s27,s27
	b.vs	.nextk
	fsub	s0,s26,s27
	fdiv	s0,s26,s0			; t
	ldp	q20,q21,[x1]
	ldp	q22,q23,[x1,32]
	ldp	q24,q25,[x2]
	ldp	q28,q29,[x2,32]
	fsub	v24.4s,v24.4s,v20.4s
	fsub	v25.4s,v25.4s,v21.4s
	fsub	v28.4s,v28.4s,v22.4s
	fsub	v29.4s,v29.4s,v23.4s
	fmla	v20.4s,v24.4s,v0.s[0]
	fmla	v21.4s,v25.4s,v0.s[0]
	fmla	v22.4s,v28.4s,v0.s[0]
	fmla	v23.4s,v29.4s,v0.s[0]
	; exactement sur le plan (arrondi des flottants)
	dup	s1,v20.s[3]			; w
	fmul	s2,s1,v30.s[0]			; 4 w
	cmp	x7,1
	b.lo	.p0
	b.eq	.p1
	cmp	x7,3
	b.lo	.p2
	b.eq	.p3
	cmp	x7,4
	b.eq	.p4
	fneg	s2,s2
.p4:	ins	v20.s[1],v2.s[0]
	b	.ponp
.p3:	fneg	s2,s2
.p2:	ins	v20.s[0],v2.s[0]
	b	.ponp
.p1:	fcmp	s1,s31
	b.ge	.ponp
	ins	v20.s[3],v31.s[0]
	b	.ponp
.p0:	fneg	s1,s1
	ins	v20.s[2],v1.s[0]
.ponp:	mov	x0,GD_PV
	madd	x0,x8,x0,x5
	stp	q20,q21,[x0]
	stp	q22,q23,[x0,32]
	add	x8,x8,1
.nextk:	add	x3,x3,1
	b	.edge
.swap:	mov	x0,x4
	mov	x4,x5
	mov	x5,x0
	mov	x6,x8
	add	x7,x7,1
	b	.plane
.fan:	cmp	x6,3
	b.lo	.none
	mov	x3,1				; k
	mov	x8,x12				; sortie
.ftri:	add	x0,x3,1
	cmp	x0,x6
	b.hs	.fdone
	mov	x0,x4
	mov	x1,x8
	bl	gd_tobytes
	mov	x0,GD_PV
	madd	x0,x3,x0,x4
	add	x1,x8,32
	bl	gd_tobytes
	add	x0,x3,1
	mov	x1,GD_PV
	madd	x0,x0,x1,x4
	add	x1,x8,64
	bl	gd_tobytes
	add	x8,x8,96
	add	x3,x3,1
	b	.ftri
.fdone:	sub	x0,x8,x12
	lsr	x0,x0,5
	b	.ret
.none:	mov	x0,0
.ret:	ldr	x30,[sp]
	add	sp,sp,2*GD_POLY_MAX*GD_PV + 16
	ret

; x0 = sommet GKV_*, x1 = sommet de polygone
gd_tofloat:
	ldp	q0,q1,[x0]
	str	q0,[x1]
	ldr	d0,[x0,GKV_S]
	str	q0,[x1,16]			; (s t 0 0 : le haut de v0 est a zero)
	ext	v1.16b,v1.16b,v1.16b,8		; les couleurs
	uxtl	v2.8h,v1.8b
	uxtl	v3.4s,v2.4h
	uxtl2	v2.4s,v2.8h
	ucvtf	v3.4s,v3.4s
	ucvtf	v2.4s,v2.4s
	stp	q3,q2,[x1,32]
	ret

; x0 = sommet de polygone, x1 = sommet GKV_*
gd_tobytes:
	ldp	q0,q1,[x0]
	ldp	q2,q3,[x0,32]
	stp	q0,q1,[x1]			; x y z w s t + 8 octets remplaces
	fmov	s0,0.5
	dup	v0.4s,v0.s[0]
	fadd	v2.4s,v2.4s,v0.4s
	fadd	v3.4s,v3.4s,v0.4s
	fcvtzu	v2.4s,v2.4s			; (sature : 0 sous zero)
	fcvtzu	v3.4s,v3.4s
	uqxtn	v2.4h,v2.4s
	uqxtn2	v2.8h,v3.4s
	uqxtn	v2.8b,v2.8h
	str	d2,[x1,GKV_RGBA]
	ret

; x0 = sommet de polygone, x7 = plan -> s0 = distance signee
gd_dist:
	ldp	s0,s1,[x0]			; x y
	ldp	s2,s3,[x0,8]			; z w
	cbnz	x7,.n1
	fadd	s0,s2,s3			; proche
	ret
.n1:	cmp	x7,1
	b.ne	.n2
	fsub	s0,s3,s31			; devant l'oeil
	ret
.n2:	fmul	s3,s3,v30.s[0]			; 4 w
	cmp	x7,2
	b.ne	.n3
	fsub	s0,s3,s0
	ret
.n3:	cmp	x7,3
	b.ne	.n4
	fadd	s0,s3,s0
	ret
.n4:	cmp	x7,4
	b.ne	.n5
	fsub	s0,s3,s1
	ret
.n5:	fadd	s0,s3,s1
	ret

; ---------------------------------------------------------------------------
;  Le rasteriseur du processeur : les lots de gd_clip dans la cible
;  (x19 = cible, x24 = textures, x25 = sommets, x26 = lots)
; ---------------------------------------------------------------------------

; une fiche de sommet du triangle sur la pile
TV_X		= 0
TV_Y		= 4
TV_P		= 16				; z, 1/w, s/w, t/w
TV_C		= 32				; B G R A / w
TV_D		= 48				; couleur ajoutee / w
TV_SIZE		= 64
; gd_tri : la pile
TF_V		= 0				; 3 fiches
TF_DPX		= 3*TV_SIZE			; gradients
TF_DCX		= TF_DPX + 16
TF_DDX		= TF_DPX + 32
TF_DPY		= TF_DPX + 48
TF_DCY		= TF_DPX + 64
TF_DDY		= TF_DPX + 80
TF_FLAT		= TF_DPX + 96			; couleur d'un triangle uni (B G R A)
TF_E		= TF_FLAT + 16			; aretes : Xa Ya Xb Yb, pentes a-c a-b b-c
TF_SIZE		= (TF_E + 28 + 15) and not 15

gd_soft:
	prologue
	ldr	x20,[x19,GKF_PIXELS]
	ldrsw	x21,[x19,GKF_W]
	ldrsw	x22,[x19,GKF_H]
	ldrsw	x23,[x19,GKF_STRIDE]
	mul	x0,x21,x22			; le tampon de profondeur
	lsl	x0,x0,2
	mov	x27,x0
	adr	x9,gd_zb
	ldr	x1,[x9,8]
	cmp	x0,x1
	b.ls	.zok
	ldr	x0,[x9]
	cbz	x0,.znew
	mov	x8,SYS_VM_UNMAP
	svc	0
.znew:	mov	x0,x27
	bl	gd_map
	adr	x9,gd_zb
	str	x0,[x9]
	str	x27,[x9,8]
	cbnz	x0,.zok
	str	xzr,[x9,8]
	mov	x0,GK_E_NOMEM
	b	.ret0
.zok:	ldr	x9,[x9]
	movl	w1,F32_ONE
	add	x2,x9,x27
	dup	v0.4s,w1
.zf:	str	q0,[x9],16			; (arrondi a 16 : dans la page)
	cmp	x9,x2
	b.lo	.zf
	ldr	w0,[x19,GKF_FLAGS]
	tbnz	w0,0,.keep			; GK_F_KEEP
	ldr	w1,[x19,GKF_CLEAR]
	and	w1,w1,0xFFFFFF
	mov	x2,x20
	mov	x3,x22
.cy:	mov	x4,0
.cx:	str	w1,[x2,x4,lsl 2]
	add	x4,x4,1
	cmp	x4,x21
	b.lo	.cx
	add	x2,x2,x23,lsl 2
	subs	x3,x3,1
	b.ne	.cy
.keep:	adr	x9,gd_verts
	ldp	x16,x27,[x9]			; x16 sommets, x27 lot
.batch:	cbz	x26,.done
	sub	x26,x26,1
	ldr	w0,[x27,GKB_FIRST]
	ldr	w28,[x27,GKB_COUNT]
	add	x17,x16,x0,lsl 5		; x17 sommet
	ldr	w25,[x27,GKB_FLAGS]
	ldrsw	x0,[x27,GKB_MATRIX]		; la texture
	mov	x3,0
	tbnz	x0,63,.notex
	mov	x1,GKT_SIZE
	madd	x3,x0,x1,x24
.notex:	adr	x9,gd_tex
	str	x3,[x9]
	add	x27,x27,GKB_SIZE
.tris:	cmp	w28,3
	b.lo	.batch
	bl	gd_tri
	add	x17,x17,96
	sub	w28,w28,3
	b	.tris
.done:	mov	x0,0
.ret0:	epilogue

; x17 = 3 sommets en espace de decoupe, x20-x23 cible, x25 drapeaux du lot, gd_tex
gd_tri:
	sub	sp,sp,TF_SIZE
	str	x30,[sp,-16]!
	add	x10,sp,16			; x10 les fiches
	mov	x0,x17
	mov	x1,x10
	bl	gd_vert
	add	x0,x17,32
	add	x1,x10,TV_SIZE
	bl	gd_vert
	add	x0,x17,64
	add	x1,x10,2*TV_SIZE
	bl	gd_vert
	; l'aire : < 0 = face avant
	ldp	s0,s1,[x10,TV_X]
	ldp	s2,s3,[x10,TV_SIZE+TV_X]
	ldp	s4,s5,[x10,2*TV_SIZE+TV_X]
	fsub	s2,s2,s0			; e1x
	fsub	s3,s3,s1			; e1y
	fsub	s4,s4,s0			; e2x
	fsub	s5,s5,s1			; e2y
	fmul	s6,s2,s5
	fmsub	s6,s4,s3,s6			; s6 = e1x e2y - e2x e1y
	fcmp	s6,0.0
	b.eq	.ret
	b.vs	.ret
	cset	w0,mi				; 1 avant
	tbz	w25,4,.nob			; GK_B_CULL_BACK
	cbz	w0,.ret
.nob:	tbz	w25,5,.nof			; GK_B_CULL_FRONT
	cbnz	w0,.ret
.nof:	fmov	s7,1.0
	fdiv	s6,s7,s6			; 1 / aire
	; les gradients (trois vecteurs de 4)
	mov	x0,0
.grad:	add	x1,x10,TV_P
	add	x1,x1,x0
	ldr	q16,[x1]			; A0
	ldr	q17,[x1,TV_SIZE]
	ldr	q18,[x1,2*TV_SIZE]
	fsub	v17.4s,v17.4s,v16.4s		; d1
	fsub	v18.4s,v18.4s,v16.4s		; d2
	fmul	v19.4s,v17.4s,v5.s[0]
	fmls	v19.4s,v18.4s,v3.s[0]		; d1 e2y - d2 e1y
	fmul	v19.4s,v19.4s,v6.s[0]
	add	x2,x10,TF_DPX
	str	q19,[x2,x0]
	fmul	v19.4s,v18.4s,v2.s[0]
	fmls	v19.4s,v17.4s,v4.s[0]		; d2 e1x - d1 e2x
	fmul	v19.4s,v19.4s,v6.s[0]
	add	x2,x10,TF_DPY
	str	q19,[x2,x0]
	add	x0,x0,16
	cmp	x0,48
	b.lo	.grad
	; x12 = 1 triangle uni : une seule couleur, sans texture
	adr	x9,gd_tex
	ldr	x13,[x9]
	mov	x12,0
	cbnz	x13,.notflat
	ldr	w0,[x17,GKV_RGBA]
	ldr	w1,[x17,32+GKV_RGBA]
	cmp	w0,w1
	b.ne	.notflat
	ldr	w1,[x17,64+GKV_RGBA]
	cmp	w0,w1
	b.ne	.notflat
	ldr	w0,[x17,GKV_RGBA2]
	ldr	w1,[x17,32+GKV_RGBA2]
	cmp	w0,w1
	b.ne	.notflat
	ldr	w1,[x17,64+GKV_RGBA2]
	cmp	w0,w1
	b.ne	.notflat
	mov	x12,1
	mov	x0,x17				; couleur + ajoutee, bornee
	add	x1,x10,TF_FLAT
	bl	gd_flat
.notflat:
	; tri selon y : x1, x2, x3 = fiches du haut, du milieu, du bas
	mov	x1,x10
	add	x2,x10,TV_SIZE
	add	x3,x10,2*TV_SIZE
	ldr	s0,[x1,TV_Y]
	ldr	s1,[x2,TV_Y]
	fcmp	s0,s1
	b.le	.s1
	mov	x0,x1
	mov	x1,x2
	mov	x2,x0
.s1:	ldr	s0,[x2,TV_Y]
	ldr	s1,[x3,TV_Y]
	fcmp	s0,s1
	b.le	.s2
	mov	x0,x2
	mov	x2,x3
	mov	x3,x0
.s2:	ldr	s0,[x1,TV_Y]
	ldr	s1,[x2,TV_Y]
	fcmp	s0,s1
	b.le	.s3
	mov	x0,x1
	mov	x1,x2
	mov	x2,x0
.s3:	ldp	s16,s17,[x1,TV_X]		; s16 Xa, s17 Ya
	ldp	s18,s19,[x2,TV_X]		; s18 Xb, s19 Yb
	ldp	s20,s21,[x3,TV_X]		; s20 Xc, s21 Yc
	fmov	s31,0.5
	fsub	s22,s21,s17			; Yc - Ya
	fcmp	s22,0.0
	b.le	.ret				; (plat, ou NaN)
	fsub	s0,s20,s16
	fdiv	s22,s0,s22			; s22 pente a-c
	fmov	s23,wzr				; s23 pente a-b
	fsub	s0,s19,s17
	fcmp	s0,0.0
	b.le	.ab0
	fsub	s1,s18,s16
	fdiv	s23,s1,s0
.ab0:	fmov	s24,wzr				; s24 pente b-c
	fsub	s0,s21,s19
	fcmp	s0,0.0
	b.le	.bc0
	fsub	s1,s20,s18
	fdiv	s24,s1,s0
.bc0:	fsub	s0,s17,s31			; les lignes
	fcvtps	w13,s0
	cmp	w13,0
	csel	w13,wzr,w13,lt
	fsub	s0,s21,s31
	fcvtps	w14,s0
	cmp	w14,w22
	csel	w14,w22,w14,gt
	add	x0,x10,TF_E
	stp	s16,s17,[x0]
	stp	s18,s19,[x0,8]
	stp	s22,s23,[x0,16]
	str	s24,[x0,24]
	bl	gd_lines
.ret:	ldr	x30,[sp],16
	add	sp,sp,TF_SIZE
	ret

; x0 = sommet GKV_*, x1 = fiche
gd_vert:
	ldp	s0,s1,[x0,GKV_X]		; x y
	ldp	s2,s3,[x0,GKV_Z]		; z w
	fmov	s4,1.0
	fdiv	s3,s4,s3			; 1 / w
	fmov	s4,0.5
	ucvtf	s6,w21
	fmul	s6,s6,s4			; W / 2
	fmov	s7,1.0
	fmul	s5,s0,s3
	fadd	s5,s5,s7
	fmul	s5,s5,s6			; X = (x / w + 1) W / 2
	ucvtf	s6,w22
	fmul	s6,s6,s4
	fmul	s0,s1,s3
	fsub	s0,s7,s0
	fmul	s0,s0,s6			; Y = (1 - y / w) H / 2
	stp	s5,s0,[x1,TV_X]
	fmul	s2,s2,s3			; Z = z / w / 2 + 1 / 2
	fmul	s2,s2,s4
	fadd	s2,s2,s4
	ldp	s0,s1,[x0,GKV_S]
	fmul	s0,s0,s3
	fmul	s1,s1,s3
	stp	s2,s3,[x1,TV_P]
	stp	s0,s1,[x1,TV_P+8]
	ldr	w2,[x0,GKV_RGBA]		; R G B A -> B G R A
	rev	w4,w2				; (A B G R en octets)
	ror	w4,w4,8				; (B G R A)
	fmov	s0,w4
	ldr	w2,[x0,GKV_RGBA2]
	rev	w4,w2
	ror	w4,w4,8
	fmov	s1,w4
	uxtl	v0.8h,v0.8b
	uxtl	v0.4s,v0.4h
	ucvtf	v0.4s,v0.4s
	fmul	v0.4s,v0.4s,v3.s[0]
	uxtl	v1.8h,v1.8b
	uxtl	v1.4s,v1.4h
	ucvtf	v1.4s,v1.4s
	fmul	v1.4s,v1.4s,v3.s[0]
	stp	q0,q1,[x1,TV_C]
	ret

; x0 = sommet GKV_*, x1 = sortie : couleur B G R A (0..255) d'un triangle uni
gd_flat:
	ldr	w2,[x0,GKV_RGBA]
	rev	w4,w2
	ror	w4,w4,8
	fmov	s0,w4
	ldr	w2,[x0,GKV_RGBA2]
	rev	w4,w2
	ror	w4,w4,8
	fmov	s1,w4
	uqadd	v0.8b,v0.8b,v1.8b
	uxtl	v0.8h,v0.8b
	uxtl	v0.4s,v0.4h
	ucvtf	v0.4s,v0.4s
	str	q0,[x1]
	ret

; Les lignes du triangle : x10 fiches (TF_E : aretes), x12 uni, w13 / w14 lignes, s31 = 1/2
; (x0-x15 et v0-v31 libres, x16, x17, x19-x28 gardes)
gd_lines:
	str	x30,[sp,-16]!
	ubfx	w15,w25,0,3			; test de profondeur : 0 ou 1 LESS
	cmp	w15,1
	csinc	w15,w15,wzr,hi			; (0 -> 1)
	; v25.b : Z ecrit ?
	mov	w11,0
	tbnz	w25,3,.nozw			; GK_B_NOZWRITE
	cmp	w15,7
	cset	w11,ne
.nozw:	ubfx	w0,w25,GK_B_BLEND_SHIFT,4
	cmp	w0,5
	csel	w0,wzr,w0,hs			; (au-dela de 4 : opaque)
	bfi	w11,w0,4,4			; w11 : bit 0 Z ecrit, 4-7 melange
	ldr	q30,[x10,TF_DPX]		; dPdx (les autres : .span)
	; le point de reference : le sommet 0
	ldp	s28,s29,[x10,TV_X]		; s28 X0, s29 Y0
.line:	cmp	w13,w14
	b.ge	.end
	ucvtf	s0,w13
	fadd	s0,s0,s31			; py
	add	x0,x10,TF_E
	ldp	s4,s5,[x0]			; Xa Ya
	ldp	s6,s7,[x0,8]			; Xb Yb
	ldp	s2,s3,[x0,16]			; pentes a-c, a-b
	fsub	s1,s0,s5			; py - Ya
	fmul	s2,s1,s2
	fadd	s2,s2,s4			; bord a-c
	fcmp	s0,s7
	b.ge	.low
	fmul	s3,s1,s3
	fadd	s3,s3,s4			; bord a-b
	b	.both
.low:	fsub	s3,s0,s7
	ldr	s4,[x0,24]
	fmul	s3,s3,s4
	fadd	s3,s3,s6			; bord b-c
.both:	fcmp	s2,s3
	b.le	.ord
	fmov	s1,s2
	fmov	s2,s3
	fmov	s3,s1
.ord:	fsub	s2,s2,s31
	fcvtps	w6,s2				; x6 = premier pixel
	cmp	w6,0
	csel	w6,wzr,w6,lt
	fsub	s3,s3,s31
	fcvtps	w7,s3				; x7 = fin
	cmp	w7,w21
	csel	w7,w21,w7,gt
	sxtw	x6,w6
	sxtw	x7,w7
	cmp	x6,x7
	b.ge	.nline
	sxtw	x0,w13				; les lignes
	mul	x8,x0,x23
	add	x8,x20,x8,lsl 2			; x8 pixels
	mul	x9,x0,x21
	adr	x1,gd_zb
	ldr	x1,[x1]
	add	x9,x1,x9,lsl 2			; x9 profondeur
	; la valeur au premier pixel : A0 + dx dA/dx + dy dA/dy
	fsub	s1,s0,s29			; dy
	ucvtf	s2,w6
	fadd	s2,s2,s31
	fsub	s2,s2,s28			; dx
	ldr	q0,[x10,TV_P]
	ldr	q3,[x10,TF_DPY]
	fmla	v0.4s,v30.4s,v2.s[0]
	fmla	v0.4s,v3.4s,v1.s[0]
	cbnz	x12,.nocol
	ldr	q3,[x10,TV_C]
	ldr	q4,[x10,TF_DCX]
	ldr	q5,[x10,TF_DCY]
	fmla	v3.4s,v4.4s,v2.s[0]
	fmla	v3.4s,v5.4s,v1.s[0]
	ldr	q7,[x10,TV_D]
	ldr	q6,[x10,TF_DDX]
	ldr	q5,[x10,TF_DDY]
	fmla	v7.4s,v6.4s,v2.s[0]
	fmla	v7.4s,v5.4s,v1.s[0]
.nocol:	stp	x13,x14,[sp,-16]!
	tbnz	w25,18,.gen			; (test d'alpha)
	cmp	x12,0
	ccmp	w11,16,2,ne			; uni et opaque ?
	b.hs	.gen
	bl	gd_span_flat
	b	.spand
.gen:	bl	gd_span
.spand:	ldp	x13,x14,[sp],16
.nline:	add	w13,w13,1
	b	.line
.end:	ldr	x30,[sp],16
	ret

; Un triangle uni et opaque : x6..x7, x8 pixels, x9 profondeur, s0 = z, v30 : dz/dx,
; w15 test, w11 bit 0 Z ecrit, x10 fiches
gd_span_flat:
	ldr	q1,[x10,TF_FLAT]
	fcvtzu	v1.4s,v1.4s
	xtn	v1.4h,v1.4s
	xtn	v1.8b,v1.8h
	fmov	w0,s1
	and	w0,w0,0xFFFFFF
	cmp	w15,7
	b.eq	.fill				; pas de test : un remplissage
	cmp	w15,1
	b.ne	gd_span_flat_z
	tbz	w11,0,.nz
.lz:	ldr	s1,[x9,x6,lsl 2]
	fcmp	s0,s1
	b.pl	.lzn
	str	w0,[x8,x6,lsl 2]
	str	s0,[x9,x6,lsl 2]
.lzn:	fadd	s0,s0,s30
	add	x6,x6,1
	cmp	x6,x7
	b.lo	.lz
	ret
.nz:	ldr	s1,[x9,x6,lsl 2]
	fcmp	s0,s1
	b.pl	.nzn
	str	w0,[x8,x6,lsl 2]
.nzn:	fadd	s0,s0,s30
	add	x6,x6,1
	cmp	x6,x7
	b.lo	.nz
	ret
.fill:	str	w0,[x8,x6,lsl 2]
	add	x6,x6,1
	cmp	x6,x7
	b.lo	.fill
	ret

; le test de profondeur rare (pas LESS) -> drapeaux Z : ne = refuse ; s0 z, s1 tampon, w15
; (z hors de 0..1 : refuse, comme bas3d.h). Ne touche ni x6-x15 ni v0, v3, v7.
gd_ztest:
	fmov	s2,1.0
	fcmp	s0,s2
	b.gt	.no
	fcmp	s0,0.0
	b.mi	.no
	cmp	w15,7
	b.eq	.yes
	cmp	w15,1
	b.eq	.lt
	cmp	w15,2
	b.eq	.eq
	cmp	w15,3
	b.eq	.le
	cmp	w15,4
	b.eq	.gt
	cmp	w15,5
	b.eq	.ne
	fcmp	s0,s1				; 6 : >=
	b.ge	.yes
	b	.no
.lt:	fcmp	s0,s1
	b.mi	.yes
	b	.no
.eq:	fcmp	s0,s1
	b.eq	.yes
	b	.no
.le:	fcmp	s0,s1
	b.ls	.yes
	b	.no
.gt:	fcmp	s0,s1
	b.gt	.yes
	b	.no
.ne:	fcmp	s0,s1
	b.ne	.yes
.no:	mov	w0,1
	cmp	w0,0
	ret
.yes:	cmp	w0,w0
	ret

gd_span_flat_z:
	mov	w4,w0
	mov	x5,x30
.px:	ldr	s1,[x9,x6,lsl 2]
	bl	gd_ztest
	b.ne	.n
	str	w4,[x8,x6,lsl 2]
	tbz	w11,0,.n
	str	s0,[x9,x6,lsl 2]
.n:	fadd	s0,s0,s30
	add	x6,x6,1
	cmp	x6,x7
	b.lo	.px
	br	x5

; Le cas general : x6..x7, x8 pixels, x9 profondeur, v0 = P, v3 = C, v7 = D, x12 uni, w15
; test, w11 (bit 0 Z ecrit, 4-7 melange), x10 fiches, x25 drapeaux
gd_span:
	str	x30,[sp,-16]!
	ldr	q4,[x10,TF_DCX]			; v4, v5 gradients en x de C, D
	ldr	q5,[x10,TF_DDX]
	movl	w0,F32_255
	dup	v27.4s,w0			; v27 = 255
	movl	w0,F32_INV255
	dup	v26.4s,w0			; v26 = 1 / 255
	fmov	s25,0.5
	dup	v25.4s,v25.s[0]
	ldr	q24,[x10,TF_FLAT]		; v24 couleur d'un triangle uni
	fmov	s23,1.0
	movi	v22.4s,0
	adr	x5,gd_tex
	ldr	x5,[x5]				; x5 texture
.px:	ldr	s1,[x9,x6,lsl 2]
	cmp	w15,1
	b.ne	.zrare
	fcmp	s0,s1
	b.pl	.next
	b	.ok
.zrare:	bl	gd_ztest
	b.ne	.next
.ok:	cbz	x12,.interp
	mov	v1.16b,v24.16b			; uni
	b	.color
.interp:
	dup	s2,v0.s[1]			; 1 / w
	fdiv	s2,s23,s2			; w
	fmul	v1.4s,v3.4s,v2.s[0]		; couleur
	cbz	x5,.notex
	dup	s16,v0.s[2]
	dup	s17,v0.s[3]
	fmul	s16,s16,s2
	fmul	s17,s17,s2
	stp	s2,s23,[sp,-16]!
	bl	gd_sample			; -> v18, B G R A, 0..255
	ldp	s2,s23,[sp],16
	fmul	v18.4s,v18.4s,v26.4s
	fmul	v1.4s,v1.4s,v18.4s
.notex:	fmul	v2.4s,v7.4s,v2.s[0]
	fadd	v1.4s,v1.4s,v2.4s		; la couleur ajoutee
	fmin	v1.4s,v1.4s,v27.4s
.color:	tbz	w25,18,.noat			; GK_B_ALPHATEST
	ubfx	w0,w25,GK_B_AT_SHIFT,8
	ucvtf	s2,w0
	dup	s16,v1.s[3]
	fcmp	s16,s2
	b.mi	.next
.noat:	ubfx	w0,w11,4,4
	cbz	w0,.opaque
	ldr	s16,[x8,x6,lsl 2]
	uxtl	v16.8h,v16.8b
	uxtl	v16.4s,v16.4h
	ucvtf	v16.4s,v16.4s			; destination
	dup	s17,v1.s[3]
	fmul	s17,s17,v26.s[0]		; fa
	fsub	s18,s23,s17			; 1 - fa
	cmp	w0,2
	b.eq	.add
	b.hi	.m34
	fmul	v1.4s,v1.4s,v17.s[0]		; 1 : alpha
	fmla	v1.4s,v16.4s,v18.s[0]
	b	.opaque
.add:	fmul	v1.4s,v1.4s,v17.s[0]
	fadd	v1.4s,v1.4s,v16.4s
	b	.opaque
.m34:	cmp	w0,3
	b.ne	.pre
	fmul	v1.4s,v1.4s,v16.4s		; 3 : multiplie
	fmul	v1.4s,v1.4s,v26.4s
	b	.opaque
.pre:	fmla	v1.4s,v16.4s,v18.s[0]		; 4 : premultiplie
.opaque:
	fmax	v1.4s,v1.4s,v22.4s
	fmin	v1.4s,v1.4s,v27.4s
	fadd	v1.4s,v1.4s,v25.4s
	fcvtzu	v1.4s,v1.4s
	xtn	v1.4h,v1.4s
	xtn	v1.8b,v1.8h
	fmov	w0,s1
	and	w0,w0,0xFFFFFF
	str	w0,[x8,x6,lsl 2]
	tbz	w11,0,.next
	str	s0,[x9,x6,lsl 2]
.next:	fadd	v0.4s,v0.4s,v30.4s
	cbnz	x12,.nstep
	fadd	v3.4s,v3.4s,v4.4s
	fadd	v7.4s,v7.4s,v5.4s
.nstep:	add	x6,x6,1
	cmp	x6,x7
	b.lo	.px
	ldr	x30,[sp],16
	ret

; s16 = s, s17 = t, x5 = fiche GKT_*, x25 drapeaux -> v18 = texel B G R A (0..255, flottants).
; Ne touche que x0-x4, x13, x14, v16-v21
gd_sample:
	mov	x14,x30
	ldr	x13,[x5,GKT_PIXELS]
	ldp	w1,w2,[x5,GKT_W]
	ucvtf	s18,w1
	ucvtf	s19,w2
	fmul	s16,s16,s18			; u
	fmul	s17,s17,s19			; v
	tbnz	w25,12,.lin			; GK_B_LINEAR
	fcvtms	w3,s16
	fcvtms	w4,s17
	bl	gd_texel
	mov	v18.16b,v19.16b
	br	x14
.lin:	fmov	s18,0.5
	fsub	s16,s16,s18
	fsub	s17,s17,s18
	fcvtms	w3,s16
	fcvtms	w4,s17
	scvtf	s18,w3
	scvtf	s19,w4
	fsub	s16,s16,s18			; fx
	fsub	s17,s17,s19			; fy
	fmov	s20,1.0
	fsub	s21,s20,s17			; 1 - fy
	fsub	s20,s20,s16			; 1 - fx
	movi	v18.4s,0
	bl	gd_texel			; (x, y)
	fmul	s2,s20,s21			; (s2 : w perdu, garde par gd_span)
	fmla	v18.4s,v19.4s,v2.s[0]
	add	w3,w3,1
	bl	gd_texel			; (x + 1, y)
	fmul	s2,s16,s21
	fmla	v18.4s,v19.4s,v2.s[0]
	add	w4,w4,1
	bl	gd_texel			; (x + 1, y + 1)
	fmul	s2,s16,s17
	fmla	v18.4s,v19.4s,v2.s[0]
	sub	w3,w3,1
	bl	gd_texel			; (x, y + 1)
	fmul	s2,s20,s17
	fmla	v18.4s,v19.4s,v2.s[0]
	br	x14

; w3, w4 = texel, w1, w2 = taille, x13 pixels, x25 -> v19 (flottants). Garde x1-x5
gd_texel:
	stp	x3,x4,[sp,-16]!
	stp	x30,xzr,[sp,-16]!
	mov	w0,w3
	ubfx	w3,w25,GK_B_WRAP_S_SHIFT,2
	mov	w4,w1
	bl	gd_wrap_x
	str	x0,[sp,8]			; x
	ldr	x0,[sp,24]			; y
	ubfx	w3,w25,GK_B_WRAP_T_SHIFT,2
	mov	w4,w2
	bl	gd_wrap_x
	mul	w0,w0,w1
	ldp	x30,x3,[sp],16
	add	w0,w0,w3
	ldr	s19,[x13,x0,lsl 2]
	uxtl	v19.8h,v19.8b
	uxtl	v19.4s,v19.4h
	ucvtf	v19.4s,v19.4s
	ldp	x3,x4,[sp],16
	ret

; w0 = i, w4 = n, w3 = mode -> w0 (0..n-1)
gd_wrap_x:
	cmp	w3,1
	b.eq	.clamp
	b.hi	.mirror
	sub	w3,w4,1				; repete
	tst	w4,w3
	b.ne	.mod
	and	w0,w0,w3
	ret
.mod:	sdiv	w3,w0,w4
	msub	w0,w3,w4,w0
	tbz	w0,31,.r
	add	w0,w0,w4
.r:	ret
.clamp:	cmp	w0,0
	csel	w0,wzr,w0,lt
	sub	w3,w4,1
	cmp	w0,w3
	csel	w0,w3,w0,gt
	ret
.mirror:
	lsl	w3,w4,1				; p = 2 n
	stp	x5,x6,[sp,-16]!
	sdiv	w5,w0,w3
	msub	w0,w5,w3,w0
	tbz	w0,31,.m1
	add	w0,w0,w3
.m1:	cmp	w0,w4
	b.lt	.m2
	sub	w0,w3,w0
	sub	w0,w0,1
.m2:	ldp	x5,x6,[sp],16
	ret

		align 16
gd_ident	dw F32_ONE,0,0,0
		dw 0,F32_ONE,0,0
		dw 0,0,F32_ONE,0
		dw 0,0,0,F32_ONE
