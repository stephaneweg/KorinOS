; edge.inc - la table des aretes actives (r_edge.c) et les sommets des modeles (r_alias.c)

; edge_t
E_U		= 0
E_USTEP		= 4
E_PREV		= 8
E_NEXT		= 16
E_SURFS		= 24
E_NEXTREMOVE	= 32
; surf_t
S_NEXT		= 0
S_PREV		= 8
S_SPANS		= 16
S_KEY		= 24
S_LAST_U	= 28
S_SPANSTATE	= 32
S_INSUB		= 60
S_ZIO		= 64
S_ZIU		= 68
S_ZIV		= 72
S_SIZE		= 88
ES_SIZE		= 24

; x0 = edgestoadd, x1 = edgelist
public R_InsertNewEdges
R_InsertNewEdges:
.a:	ldr	x2,[x0,E_NEXT]
	ldr	w3,[x0,E_U]
.s:	ldr	w4,[x1,E_U]
	cmp	w4,w3
	b.ge	.add
	ldr	x1,[x1,E_NEXT]
	b	.s
.add:	ldr	x5,[x1,E_PREV]
	str	x1,[x0,E_NEXT]
	str	x5,[x0,E_PREV]
	str	x0,[x5,E_NEXT]
	str	x0,[x1,E_PREV]
	mov	x0,x2
	cbnz	x0,.a
	ret

; x0 = pedge
public R_RemoveEdges
R_RemoveEdges:
	ldp	x1,x2,[x0,E_PREV]
	str	x1,[x2,E_PREV]
	str	x2,[x1,E_NEXT]
	ldr	x0,[x0,E_NEXTREMOVE]
	cbnz	x0,R_RemoveEdges
	ret

; x0 = pedge
public R_StepActiveU
R_StepActiveU:
	la	x9,edge_aftertail
	la	x10,edge_tail
.l:	ldp	w1,w2,[x0,E_U]
	add	w1,w1,w2
	str	w1,[x0,E_U]
	ldr	x3,[x0,E_PREV]
	ldr	w4,[x3,E_U]
	cmp	w1,w4
	b.lt	.push
	ldr	x0,[x0,E_NEXT]
	b	.l
.push:	cmp	x0,x9
	b.eq	.out
	ldr	x5,[x0,E_NEXT]			; on la sort de la liste
	str	x3,[x5,E_PREV]
	str	x5,[x3,E_NEXT]
	ldr	x6,[x3,E_PREV]
.f:	ldr	w4,[x6,E_U]			; sa place
	cmp	w4,w1
	b.le	.put
	ldr	x6,[x6,E_PREV]
	b	.f
.put:	ldr	x7,[x6,E_NEXT]
	str	x7,[x0,E_NEXT]
	str	x6,[x0,E_PREV]
	str	x0,[x7,E_PREV]
	str	x0,[x6,E_NEXT]
	mov	x0,x5
	cmp	x0,x10
	b.ne	.l
.out:	ret

; un span sur la surface sr, de son last_u (dans w3) a iu (w2) ; x21 = span_p, w22 = current_iv
macro emit_span sr {
	stp	w3,w22,[x21,ES_U]
	sub	w2,w2,w3
	str	w2,[x21,ES_COUNT]
	ldr	x3,[sr,S_SPANS]
	str	x3,[x21,ES_NEXT]
	str	x21,[sr,S_SPANS]
	add	x21,x21,ES_SIZE
}

; deux modeles dans la meme feuille : le C trie sur 1/z ; d'abord proche -> near, sinon a la suite
macro zsort near {
	ldr	w2,[x24,E_U]
	movi32	w3,0xFFFFF
	sub	w2,w2,w3
	scvtf	s0,w2
	fcvt	d0,s0
	ldr	d1,[x26]			; 1.0 / 0x100000
	fmul	d0,d0,d1			; fu
	ldr	s1,[x27]			; fv
	ldr	s2,[x0,S_ZIO]
	ldr	s3,[x0,S_ZIV]
	fmul	s3,s1,s3
	fadd	s2,s2,s3
	fcvt	d2,s2
	ldr	s3,[x0,S_ZIU]
	fcvt	d3,s3
	fmul	d3,d0,d3
	fadd	d2,d2,d3			; newzi
	ldr	s3,[x1,S_ZIO]
	ldr	s4,[x1,S_ZIV]
	fmul	s4,s1,s4
	fadd	s3,s3,s4
	fcvt	d3,s3
	ldr	s4,[x1,S_ZIU]
	fcvt	d4,s4
	fmul	d4,d0,d4
	fadd	d3,d3,d4			; testzi
	ldr	d4,[x26,8]			; 0.99
	fmul	d4,d2,d4
	fcmp	d4,d3
	b.ge	near
	ldr	d4,[x26,16]			; 1.01
	fmul	d4,d2,d4
	fcmp	d4,d3
	b.lt	@f
	ldr	s4,[x0,S_ZIU]
	ldr	s5,[x1,S_ZIU]
	fcmp	s4,s5
	b.ge	near
@@:
}

public R_GenerateSpans
R_GenerateSpans:
	stp	x29,x30,[sp,-80]!
	stp	x19,x20,[sp,16]
	stp	x21,x22,[sp,32]
	stp	x23,x24,[sp,48]
	stp	x25,x26,[sp,64]
	stp	x27,x28,[sp,-16]!
	la	x19,surfaces
	ldr	x19,[x19]
	add	x20,x19,S_SIZE			; &surfaces[1]
	la	x21,span_p
	ldr	x21,[x21]
	lw	w22,x9,current_iv
	mov	w23,0				; r_bmodelactive
	adr	x26,edge_cst
	la	x27,fv
	mov	w28,S_SIZE
	stp	x20,x20,[x20,S_NEXT]
	lw	w9,x10,edge_head_u_shift20
	str	w9,[x20,S_LAST_U]
	la	x24,edge_head
	ldr	x24,[x24,E_NEXT]
	la	x25,edge_tail
.edge:	cmp	x24,x25
	b.eq	.done
	ldrh	w9,[x24,E_SURFS]
	cbz	w9,.lead
	; R_TrailingEdge
	umull	x9,w9,w28
	add	x0,x19,x9
	ldr	w10,[x0,S_SPANSTATE]
	subs	w10,w10,1
	str	w10,[x0,S_SPANSTATE]
	b.ne	.tend
	ldr	w10,[x0,S_INSUB]
	cbz	w10,.t1
	sub	w23,w23,1
.t1:	ldr	x10,[x20,S_NEXT]
	cmp	x0,x10
	b.ne	.t2
	ldr	w4,[x24,E_U]
	asr	w4,w4,20			; iu
	ldr	w3,[x0,S_LAST_U]
	cmp	w4,w3
	b.le	.t3
	mov	w2,w4
	emit_span x0
.t3:	ldr	x3,[x0,S_NEXT]
	str	w4,[x3,S_LAST_U]
.t2:	ldp	x3,x4,[x0,S_NEXT]
	str	x3,[x4,S_NEXT]
	str	x4,[x3,S_PREV]
.tend:
.lead:	ldrh	w9,[x24,E_SURFS+2]
	cbz	w9,.nx
	; R_LeadingEdge
	umull	x9,w9,w28
	add	x0,x19,x9
	ldr	w10,[x0,S_SPANSTATE]
	add	w10,w10,1
	str	w10,[x0,S_SPANSTATE]
	cmp	w10,1
	b.ne	.nx
	ldr	w10,[x0,S_INSUB]
	cbz	w10,.l1
	add	w23,w23,1
.l1:	ldr	x1,[x20,S_NEXT]			; surf2
	ldr	w11,[x0,S_KEY]
	ldr	w12,[x1,S_KEY]
	cmp	w11,w12
	b.lt	.newtop
	cbz	w10,.cont
	cmp	w11,w12
	b.ne	.cont
	zsort	.newtop
.cont:	ldr	x1,[x1,S_NEXT]
	ldr	w12,[x1,S_KEY]
	cmp	w11,w12
	b.gt	.cont
	b.ne	.gotpos
	cbz	w10,.cont
	zsort	.gotpos
	b	.cont
.newtop:
	ldr	w4,[x24,E_U]
	asr	w4,w4,20			; iu
	ldr	w3,[x1,S_LAST_U]
	cmp	w4,w3
	b.le	.lt
	mov	w2,w4
	emit_span x1
.lt:	str	w4,[x0,S_LAST_U]
.gotpos:
	ldr	x3,[x1,S_PREV]
	str	x1,[x0,S_NEXT]
	str	x3,[x0,S_PREV]
	str	x0,[x3,S_NEXT]
	str	x0,[x1,S_PREV]
.nx:	ldr	x24,[x24,E_NEXT]
	b	.edge
.done:	la	x9,span_p
	str	x21,[x9]
	la	x9,r_bmodelactive
	str	w23,[x9]
	la	x9,R_CleanupSpan
	blr	x9
	ldp	x27,x28,[sp],16
	ldp	x19,x20,[sp,16]
	ldp	x21,x22,[sp,32]
	ldp	x23,x24,[sp,48]
	ldp	x25,x26,[sp,64]
	ldp	x29,x30,[sp],80
	ret

	align 8
edge_cst	dd 0x3EB0000000000000		; 1.0 / 0x100000
		dd 0.99
		dd 1.01

; x0 = fv, x1 = pstverts. Les trois lignes de la matrice dans trois voies : chaque voie fait les memes
; operations, dans le meme ordre, que le C.
public R_AliasTransformAndProjectFinalVerts
R_AliasTransformAndProjectFinalVerts:
	la	x2,r_apverts
	ldr	x2,[x2]
	lw	w3,x9,r_anumverts
	cmp	w3,0
	b.le	.out
	la	x9,aliastransform
	ldp	q0,q1,[x9]
	ldr	q2,[x9,32]
	zip1	v4.4s,v0.4s,v2.4s
	zip1	v5.4s,v1.4s,v1.4s
	zip1	v16.4s,v4.4s,v5.4s		; colonne 0
	zip2	v17.4s,v4.4s,v5.4s
	zip2	v4.4s,v0.4s,v2.4s
	zip2	v5.4s,v1.4s,v1.4s
	zip1	v18.4s,v4.4s,v5.4s
	zip2	v19.4s,v4.4s,v5.4s		; colonne 3
	la	x9,aliasxcenter
	ldr	s20,[x9]
	la	x9,aliasycenter
	ld1	{v20.s}[1],[x9]
	la	x9,r_plightvec
	ldp	s21,s22,[x9]
	ldr	s23,[x9,8]
	la	x9,r_shadelight
	ldr	s24,[x9]
	lw	w12,x9,r_ambientlight
	la	x10,r_avertexnormals
	fmov	d25,1.0
.v:	ldrb	w4,[x2]
	ldrb	w5,[x2,1]
	ldrb	w6,[x2,2]
	ldrb	w7,[x2,3]
	add	x2,x2,4
	scvtf	s0,w4
	scvtf	s1,w5
	scvtf	s2,w6
	fmul	v4.4s,v16.4s,v0.s[0]
	fmul	v5.4s,v17.4s,v1.s[0]
	fadd	v4.4s,v4.4s,v5.4s
	fmul	v5.4s,v18.4s,v2.s[0]
	fadd	v4.4s,v4.4s,v5.4s
	fadd	v4.4s,v4.4s,v19.4s
	dup	s5,v4.s[2]
	fcvt	d5,s5
	fdiv	d5,d25,d5
	fcvt	s5,d5				; zi
	fcvtzs	w8,s5
	str	w8,[x0,20]
	fmul	v4.2s,v4.2s,v5.s[0]
	fadd	v4.2s,v4.2s,v20.2s
	fcvtzs	v4.2s,v4.2s
	str	d4,[x0]
	ldp	w4,w5,[x1]			; onseam, s
	ldr	w6,[x1,8]
	add	x1,x1,12
	stp	w5,w6,[x0,8]
	str	w4,[x0,24]
	add	x7,x7,x7,lsl 1
	add	x7,x10,x7,lsl 2
	ldp	s0,s1,[x7]
	ldr	s2,[x7,8]
	fmul	s0,s0,s21
	fmul	s1,s1,s22
	fadd	s0,s0,s1
	fmul	s2,s2,s23
	fadd	s0,s0,s2			; lightcos
	mov	w4,w12
	fcmp	s0,0.0
	b.pl	.nl
	b.vs	.nl
	fmul	s0,s24,s0
	fcvtzs	w5,s0
	adds	w4,w4,w5
	csel	w4,wzr,w4,mi
.nl:	str	w4,[x0,16]
	add	x0,x0,32
	subs	w3,w3,1
	b.ne	.v
.out:	ret
