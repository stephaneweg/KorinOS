; ============================================================================
;  gamesnd.inc - petits effets sonores des jeux, sur les voix FM d'AudioKit
;  (docs/SON.md) : gs_note joue une note d'une duree donnee, gs_later la joue
;  plus tard (une melodie), gs_seq joue une table. Sans son (pas de service,
;  sortie prise), tout reste silencieux et le jeu tourne.
;
;	kit_use '../../kits/audiokit/audiokit.abi'  et  gs_data dans les donnees ;
;	bl gs_open au debut, bl gs_tick a chaque image, bl gs_close a la fin.
; ============================================================================

GS_NV		= 8			; voix
GS_Q		= 24			; notes en attente
GS_QE		= 32			; +0 quand, +8 hz, +16 ms, +24 onde, +28 volume

; entree d'une table de gs_seq : attente (ms), hz, duree (ms), onde, volume
macro gs_ent delay, hz, ms, wave, vol {
	dh	delay, hz, ms
	db	wave, vol
}
macro gs_end { dh 0, 0, 0, 0 }

; ouvre le synthe ; 0 dans gs_fm s'il n'y a pas de son
gs_open:
	prologue
	str	x19,[sp,-16]!
	mov	x0,GS_NV
	bl	ak_fm_new
	cmp	x0,0
	b.le	.out
	mov	x19,x0
	mov	x1,0
	bl	ak_fm_live
	tbnz	x0,63,.free
	adr	x0,gs_fm
	str	x19,[x0]
	b	.out
.free:	mov	x0,x19
	bl	ak_fm_free
.out:	ldr	x19,[sp],16
	epilogue

gs_close:
	prologue
	adr	x1,gs_fm
	ldr	x0,[x1]
	cbz	x0,.out
	str	xzr,[x1]
	bl	ak_fm_free			; (arrete le thread de sortie)
.out:	epilogue

; x0 = hz, x1 = ms, x2 = onde AKW_*, x3 = volume 0..255
gs_note:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,gs_fm
	ldp	x20,x4,[x19]			; synthe, muet
	cbz	x20,.out
	cbnz	x4,.out
	mov	x4,x3
	mov	x3,x2
	mov	x5,1000
	mul	x2,x0,x5
	ldr	x21,[x19,16]			; la voix suivante
	add	x5,x21,1
	and	x5,x5,GS_NV-1
	str	x5,[x19,16]
	mov	x22,x1
	mov	x0,x20
	mov	x1,x21
	bl	ak_fm_start
	sys	SYS_TIME_MS
	add	x0,x0,x22
	orr	x0,x0,1
	adr	x1,gs_end_t
	str	x0,[x1,x21,lsl 3]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = attente (ms), x1 = hz, x2 = ms, x3 = onde, x4 = volume
gs_later:
	stp	x29,x30,[sp,-16]!
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	str	x0,[sp,-16]!
	adr	x5,gs_fm
	ldp	x6,x7,[x5]
	cbz	x6,.out
	cbnz	x7,.out
	ldr	x6,[x5,24]			; notes en attente
	cmp	x6,GS_Q
	b.hs	.out
	sys	SYS_TIME_MS
	ldr	x6,[sp]
	add	x0,x0,x6
	adr	x5,gs_fm
	ldr	x6,[x5,24]
	add	x7,x6,1
	str	x7,[x5,24]
	adr	x5,gs_q
	add	x5,x5,x6,lsl 5
	str	x0,[x5]
	ldp	x1,x2,[sp,32]
	stp	x1,x2,[x5,8]
	ldp	x3,x4,[sp,16]
	stp	w3,w4,[x5,24]
.out:	add	sp,sp,48
	ldp	x29,x30,[sp],16
	ret

; x0 = table (gs_ent ..., gs_end) : toutes ses notes
gs_seq:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
.n:	ldrh	w1,[x19,2]
	cbz	w1,.out
	ldrh	w0,[x19]
	ldrh	w2,[x19,4]
	ldrb	w3,[x19,6]
	ldrb	w4,[x19,7]
	bl	gs_later
	add	x19,x19,8
	b	.n
.out:	ldr	x19,[sp],16
	epilogue

; a chaque image : les notes dues partent, les notes finies s'arretent
gs_tick:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,gs_fm
	ldr	x0,[x19]
	cbz	x0,.out
	sys	SYS_TIME_MS
	mov	x21,x0
	mov	x20,0
.q:	ldr	x0,[x19,24]
	cmp	x20,x0
	b.hs	.v
	adr	x1,gs_q
	add	x22,x1,x20,lsl 5
	ldr	x1,[x22]
	cmp	x21,x1
	b.lo	.nq
	ldp	x0,x1,[x22,8]
	ldp	w2,w3,[x22,24]
	bl	gs_note
	ldr	x0,[x19,24]			; la derniere prend sa place
	sub	x0,x0,1
	str	x0,[x19,24]
	adr	x1,gs_q
	add	x1,x1,x0,lsl 5
	ldp	x2,x3,[x1]
	stp	x2,x3,[x22]
	ldp	x2,x3,[x1,16]
	stp	x2,x3,[x22,16]
	b	.q
.nq:	add	x20,x20,1
	b	.q
.v:	mov	x20,0
.vl:	adr	x22,gs_end_t
	ldr	x0,[x22,x20,lsl 3]
	cbz	x0,.vn
	cmp	x21,x0
	b.lo	.vn
	str	xzr,[x22,x20,lsl 3]
	ldr	x0,[x19]
	mov	x1,x20
	bl	ak_fm_stop
.vn:	add	x20,x20,1
	cmp	x20,GS_NV
	b.lo	.vl
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Son on/off -> x0 = 1 si le son reste actif
gs_toggle:
	prologue
	adr	x1,gs_fm
	ldr	x2,[x1,8]
	eor	x2,x2,1
	str	x2,[x1,8]
	str	xzr,[x1,24]
	cbz	x2,.on
	ldr	x0,[x1]
	cbz	x0,.on
	mov	x1,-1
	bl	ak_fm_stop
.on:	adr	x1,gs_fm
	ldr	x0,[x1,8]
	eor	x0,x0,1
	epilogue

; le gain et la perte d'une partie (les quatre notes d'Onyx)
gs_tab_win:
	gs_ent	0,523,110,AKW_SQUARE,90
	gs_ent	120,659,110,AKW_SQUARE,90
	gs_ent	240,784,110,AKW_SQUARE,90
	gs_ent	360,1047,360,AKW_SQUARE,90
	gs_end
gs_tab_lose:
	gs_ent	0,392,150,AKW_TRIANGLE,120
	gs_ent	160,330,150,AKW_TRIANGLE,120
	gs_ent	320,262,150,AKW_TRIANGLE,120
	gs_ent	480,196,150,AKW_TRIANGLE,120
	gs_end
	align 4

macro gs_data {
		align 8
gs_fm		dd 0			; synthe (0 : pas de son)
gs_mute		dd 0
gs_next		dd 0
gs_nq		dd 0
gs_end_t	rd GS_NV		; fin de chaque voix (ms), 0 : libre
gs_q		rb GS_Q*GS_QE
}
