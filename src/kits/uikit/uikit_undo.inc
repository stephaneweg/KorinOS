; ============================================================================
;  kits/uikit_undo.inc - UIKit : annuler / retablir (texte multiligne)
;
;  Un journal par widget (UW_UNDO) : UND_N operations de 32 octets (genre 1
;  insertion / 2 suppression, position, longueur, octets copies) ; « top » =
;  operations appliquees, au-dela : celles a retablir (effacees par une
;  nouvelle modification). La frappe d'un mot se fond en une operation, les
;  Retour arriere / Suppr successifs aussi. Les modifications passent toutes
;  par uk_tb_insert et uk_tb_delete, qui appellent uk_undo_rec.
; ============================================================================

UND_N		= 128
UND_COUNT	= 0
UND_TOP		= 8
UND_REC		= 16
UR_KIND		= 0
UR_POS		= 8
UR_LEN		= 16
UR_DATA		= 24

; x0 = widget, x1 = genre, x2 = octets (dans son texte), x3 = longueur
uk_undo_rec:
	ldr	x4,[x0,UW_TEXT]
	sub	x4,x2,x4
; x4 = position
uk_undo_rec_at:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	cbz	x22,.out
	adr	x0,uk_undo_busy
	ldr	x0,[x0]
	cbnz	x0,.out
	ldr	w0,[x19,UW_TYPE]
	cmp	w0,UT_AREA
	b.ne	.out
	ldr	x24,[x19,UW_UNDO]
	cbnz	x24,.have
	mov	x0,UND_REC+UND_N*32
	bl	uk_alloc
	cbz	x0,.out
	mov	x24,x0
	str	x24,[x19,UW_UNDO]
.have:	ldr	x25,[x24,UND_TOP]		; ce qui etait a retablir : oublie
.drop:	ldr	x0,[x24,UND_COUNT]
	cmp	x0,x25
	b.ls	.dropd
	sub	x0,x0,1
	str	x0,[x24,UND_COUNT]
	add	x1,x24,UND_REC
	add	x1,x1,x0,lsl 5
	ldr	x0,[x1,UR_DATA]
	bl	uk_free
	b	.drop
.dropd:	cbz	x25,.new			; fondre avec la precedente ?
	add	x26,x24,UND_REC
	sub	x0,x25,1
	add	x26,x26,x0,lsl 5
	ldr	x0,[x26,UR_KIND]
	cmp	x0,x20
	b.ne	.new
	cmp	x22,4				; (un caractere tape, efface)
	b.hi	.new
	ldr	x0,[x26,UR_LEN]
	cmp	x0,200
	b.hs	.new
	ldrb	w0,[x21]
	cmp	w0,10				; (une fin de ligne coupe)
	b.eq	.new
	ldr	x0,[x26,UR_POS]
	ldr	x1,[x26,UR_LEN]
	cmp	x20,1
	b.ne	.del
	add	x2,x0,x1			; insertion juste apres
	cmp	x2,x23
	b.ne	.new
	mov	x0,1				; ancien + nouveau
	b	.merge
.del:	add	x2,x23,x22			; Retour arriere : nouveau + ancien
	cmp	x2,x0
	b.ne	.del2
	str	x23,[x26,UR_POS]
	mov	x0,0
	b	.merge
.del2:	cmp	x23,x0				; Suppr : ancien + nouveau
	b.ne	.new
	mov	x0,1
.merge:	mov	x25,x0				; 1 : ancien d'abord
	ldr	x0,[x26,UR_LEN]
	add	x0,x0,x22
	bl	uk_alloc
	cbz	x0,.out
	stp	x0,x0,[sp,-16]!
	cbz	x25,.nf
	ldr	x1,[x26,UR_DATA]
	ldr	x2,[x26,UR_LEN]
	bl	uk_memcpy
	mov	x1,x21
	mov	x2,x22
	bl	uk_memcpy
	b	.mset
.nf:	mov	x1,x21
	mov	x2,x22
	bl	uk_memcpy
	ldr	x1,[x26,UR_DATA]
	ldr	x2,[x26,UR_LEN]
	bl	uk_memcpy
.mset:	ldr	x0,[x26,UR_DATA]
	bl	uk_free
	ldp	x0,x1,[sp],16
	str	x0,[x26,UR_DATA]
	ldr	x0,[x26,UR_LEN]
	add	x0,x0,x22
	str	x0,[x26,UR_LEN]
	b	.out
.new:	ldr	x0,[x24,UND_COUNT]		; plein : la plus ancienne part
	cmp	x0,UND_N
	b.lo	.room
	ldr	x0,[x24,UND_REC+UR_DATA]
	bl	uk_free
	add	x0,x24,UND_REC
	add	x1,x0,32
	mov	x2,(UND_N-1)*32
	bl	uk_memcpy
	mov	x0,UND_N-1
	str	x0,[x24,UND_COUNT]
	str	x0,[x24,UND_TOP]
.room:	mov	x0,x22
	bl	uk_alloc
	cbz	x0,.out
	mov	x25,x0
	mov	x1,x21
	mov	x2,x22
	bl	uk_memcpy
	ldr	x0,[x24,UND_TOP]
	add	x1,x24,UND_REC
	add	x1,x1,x0,lsl 5
	stp	x20,x23,[x1]
	stp	x22,x25,[x1,16]
	add	x0,x0,1
	str	x0,[x24,UND_TOP]
	str	x0,[x24,UND_COUNT]
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget : oublie son journal (nouveau texte, fin du widget)
uk_undo_clear:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x19,[x0,UW_UNDO]
	cbz	x19,.out
.l:	ldr	x20,[x19,UND_COUNT]
	cbz	x20,.z
	sub	x20,x20,1
	str	x20,[x19,UND_COUNT]
	add	x0,x19,UND_REC
	add	x0,x0,x20,lsl 5
	ldr	x0,[x0,UR_DATA]
	bl	uk_free
	b	.l
.z:	str	xzr,[x19,UND_TOP]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = 0 annuler / 1 retablir -> x0 = 1 si fait
uk_undo_step:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x22,x1
	ldr	x20,[x19,UW_UNDO]
	mov	x0,0
	cbz	x20,.out
	ldr	x1,[x20,UND_TOP]
	cbnz	x22,.redo
	cbz	x1,.out
	sub	x1,x1,1				; annuler la derniere
	str	x1,[x20,UND_TOP]
	b	.rec
.redo:	ldr	x2,[x20,UND_COUNT]
	cmp	x1,x2
	b.hs	.out
	add	x2,x1,1
	str	x2,[x20,UND_TOP]
.rec:	add	x21,x20,UND_REC
	add	x21,x21,x1,lsl 5
	adr	x0,uk_undo_busy
	mov	x1,1
	str	x1,[x0]
	ldr	x0,[x21,UR_KIND]		; insertion annulee / suppression
	cmp	x0,1				; retablie : on retire ; sinon on remet
	cset	x0,eq
	eor	x0,x0,x22
	cbz	x0,.put
	mov	x0,x19
	ldr	x1,[x21,UR_POS]
	ldr	x2,[x21,UR_LEN]
	add	x2,x1,x2
	bl	uk_tb_delete
	b	.done
.put:	ldr	x0,[x21,UR_POS]
	str	x0,[x19,UW_VALUE]
	str	x0,[x19,UW_ANCHOR]
	mov	x0,x19
	ldr	x1,[x21,UR_DATA]
	ldr	x2,[x21,UR_LEN]
	bl	uk_tb_insert
.done:	adr	x0,uk_undo_busy
	str	xzr,[x0]
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
