; gba_harness : le coeur GBA en assembleur sous qemu-aarch64 (Linux, mode utilisateur), pour le
; comparer au coeur C++ d'Onyx (tools/emu/ref_gba.cpp). Pour chaque image : somme de l'image et du
; son, trames, horloge, puis les registres et la somme de l'etat. Memes boutons des deux cotes.
;     qemu-aarch64 gba_harness rom.gba images [time]
format ELF64 executable 3 at 0x400000
processor cpu64_v8 +cpu64_fp +cpu64_simd
code64
entry start
macro prologue {
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
}
macro epilogue {
	ldp	x29,x30,[sp],16
	ret
}
macro movl reg*, val* {
	movz	reg,(val) and 0xFFFF
  if ((val) shr 16) and 0xFFFF <> 0
	movk	reg,((val) shr 16) and 0xFFFF,lsl 16
  end if
  if ((val) shr 32) and 0xFFFF <> 0
	movk	reg,((val) shr 32) and 0xFFFF,lsl 32
  end if
  if ((val) shr 48) and 0xFFFF <> 0
	movk	reg,((val) shr 48) and 0xFFFF,lsl 48
  end if
}
macro linux n {
	mov	x8,n
	svc	0
}

segment readable executable
start:
	mov	x19,sp
	ldr	x20,[x19]			; argc
	ldr	x21,[x19,16]			; rom
	ldr	x0,[x19,24]			; images
	bl	atoi
	mov	x22,x0
	; memoire
	mov	x0,0
	movl	x1,0x8000000
	mov	x2,3
	mov	x3,0x22
	mov	x4,-1
	mov	x5,0
	linux	222
	mov	x23,x0				; 128 Mo
	mov	x0,-100
	mov	x1,x21
	mov	x2,0
	mov	x3,0
	linux	56
	mov	x24,x0
	mov	x25,0				; octets
.rd:	mov	x0,x24
	add	x1,x23,x25
	movl	x2,0x4000000
	sub	x2,x2,x25
	linux	63
	cmp	x0,0
	b.le	.rdf
	add	x25,x25,x0
	b	.rd
.rdf:	movl	x26,0x4000000
	add	x26,x26,x23			; m
	mov	x0,x26
	mov	x1,44100
	bl	gba_audio_rate
	mov	x0,x26
	mov	x1,x23
	mov	x2,x25
	bl	gba_load
	cbz	x0,.fail
	adr	x0,quiet
	mov	x1,0
	cmp	x20,4
	cset	x1,hs
	str	x1,[x0]
	movl	x27,0x5000000
	add	x27,x27,x23			; image
	movl	x28,0x6000000
	add	x28,x28,x23			; son
	mov	x21,0				; numero
.f:	cmp	x21,x22
	b.hs	.end
	mov	w0,w21
	bl	buttons_of
	mov	x3,x0
	mov	x1,x27
	mov	x2,960
	mov	x0,x26
	bl	gba_frame
	mov	x0,x26
	mov	x1,x28
	mov	x2,8192
	bl	gba_audio_read
	mov	x24,x0
	adr	x0,quiet
	ldr	x0,[x0]
	cbnz	x0,.nx
	add	x0,x26,A_FB
	movl	x1,GBA_W*GBA_H*4
	movl	w2,0x811C9DC5
	bl	fnv
	mov	w19,w0
	mov	x0,x28
	lsl	x1,x24,2
	movl	w2,0x811C9DC5
	bl	fnv
	mov	w25,w0
	mov	x0,x21
	bl	out_dec
	mov	w0,' '
	bl	out_c
	mov	w0,w19
	bl	out_hex
	mov	w0,' '
	bl	out_c
	mov	w0,w25
	bl	out_hex
	mov	w0,' '
	bl	out_c
	mov	x0,x24
	bl	out_dec
	mov	w0,' '
	bl	out_c
	ldr	x0,[x26,a_now]
	bl	out_dec
	mov	w0,10
	bl	out_c
	bl	out_state
.nx:	add	x21,x21,1
	b	.f
.end:	bl	flush
	mov	x0,0
	linux	93
.fail:	mov	x0,1
	linux	93

; les registres et la somme de l'etat (x26 = m, x21 = numero d'image)
out_state:
	stp	x19,x30,[sp,-16]!
	mov	x0,x26
	mov	x1,64
	movl	w2,0x811C9DC5
	bl	fnv
	mov	w2,w0
	add	x0,x26,a_cpsr
	mov	x1,4
	bl	fnv
	mov	w2,w0
	tst	x21,15
	b.ne	.p
	add	x0,x26,A_EWRAM
	movl	x1,0x40000
	bl	fnv
	mov	w2,w0
	add	x0,x26,A_IWRAM
	movl	x1,0x8000
	bl	fnv
	mov	w2,w0
	add	x0,x26,A_VRAM
	movl	x1,0x18000
	bl	fnv
	mov	w2,w0
	add	x0,x26,A_PAL
	mov	x1,0x400
	bl	fnv
	mov	w2,w0
	add	x0,x26,A_OAM
	mov	x1,0x400
	bl	fnv
	mov	w2,w0
	add	x0,x26,A_SAVE
	movl	x1,0x20000
	bl	fnv
	mov	w2,w0
.p:	mov	w19,w2
	ldr	w0,[x26,a_r+15*4]
	bl	out_hex
	ldr	w0,[x26,a_cpsr]
	bl	out_hex
	mov	w0,' '
	bl	out_c
	mov	w0,w19
	bl	out_hex
	mov	w0,10
	bl	out_c
	ldp	x19,x30,[sp],16
	ret

; w0 = numero d'image -> boutons (le meme calcul que ref_snes.cpp)
buttons_of:
	movl	w1,2654435761
	lsr	w2,w0,5
	mul	w2,w2,w1
	lsr	w2,w2,22
	and	w0,w2,0x3FF
	ret

atoi:	mov	x1,0
.l:	ldrb	w2,[x0],1
	cbz	w2,.f
	mov	x3,10
	mul	x1,x1,x3
	sub	w2,w2,'0'
	add	x1,x1,x2
	b	.l
.f:	mov	x0,x1
	ret

fnv:	movl	w3,16777619
	add	x1,x0,x1
.l:	cmp	x0,x1
	b.hs	.f
	ldrb	w4,[x0],1
	eor	w2,w2,w4
	mul	w2,w2,w3
	b	.l
.f:	mov	w0,w2
	ret

out_c:	adr	x1,obuf_n
	ldr	x2,[x1]
	adr	x3,obuf
	strb	w0,[x3,x2]
	add	x2,x2,1
	str	x2,[x1]
	cmp	x2,3900
	b.hs	flush
	ret
flush:	adr	x3,obuf_n
	ldr	x2,[x3]
	str	xzr,[x3]
	adr	x1,obuf
	mov	x0,1
	linux	64
	ret
out_hex2:
	mov	w1,4
	b	out_hexn
out_hex4:
	mov	w1,12
	b	out_hexn
out_hex:
	mov	w1,28
out_hexn:
	stp	x19,x30,[sp,-16]!
	stp	x20,x21,[sp,-16]!
	mov	w19,w0
	mov	w20,w1
.l:	lsr	w0,w19,w20
	and	w0,w0,15
	add	w1,w0,'0'
	add	w2,w0,'a'-10
	cmp	w0,10
	csel	w0,w2,w1,hs
	bl	out_c
	subs	w20,w20,4
	b.hs	.l
	ldp	x20,x21,[sp],16
	ldp	x19,x30,[sp],16
	ret
out_dec:
	stp	x19,x30,[sp,-16]!
	sub	sp,sp,32
	mov	x19,0
	mov	x1,10
.d:	udiv	x2,x0,x1
	msub	x3,x2,x1,x0
	add	w3,w3,'0'
	strb	w3,[sp,x19]
	add	x19,x19,1
	mov	x0,x2
	cbnz	x0,.d
.o:	sub	x19,x19,1
	ldrb	w0,[sp,x19]
	bl	out_c
	cbnz	x19,.o
	add	sp,sp,32
	ldp	x19,x30,[sp],16
	ret

if defined GBA_TRACE
; une ligne par instruction : les 16 registres, CPSR, now, cycle
gba_trace:
	sub	sp,sp,240
	stp	x0,x1,[sp]
	stp	x2,x3,[sp,16]
	stp	x4,x5,[sp,32]
	stp	x6,x7,[sp,48]
	stp	x8,x9,[sp,64]
	stp	x10,x11,[sp,80]
	stp	x12,x13,[sp,96]
	stp	x14,x15,[sp,112]
	stp	x16,x17,[sp,128]
	str	x30,[sp,144]
	str	x22,[sp,152]
	mov	x22,0
.l:	ldr	w0,[x19,x22,lsl 2]
	bl	out_hex
	add	x22,x22,1
	cmp	x22,16
	b.lo	.l
	ldr	x22,[sp,152]
	mov	w0,w22
	bl	out_hex
	mov	x0,x20
	bl	out_dec
	ldrh	w0,[x19,A_IO+0x202]
	bl	out_hex
	ldr	w0,[x19,a_vcount]
	bl	out_hex
	mov	w0,10
	bl	out_c
	ldp	x0,x1,[sp]
	ldp	x2,x3,[sp,16]
	ldp	x4,x5,[sp,32]
	ldp	x6,x7,[sp,48]
	ldp	x8,x9,[sp,64]
	ldp	x10,x11,[sp,80]
	ldp	x12,x13,[sp,96]
	ldp	x14,x15,[sp,112]
	ldp	x16,x17,[sp,128]
	ldr	x30,[sp,144]
	add	sp,sp,240
	ret
end if

include '../../src/apps/gbaemu/gba_core.inc'

segment readable writeable
quiet:	dd 0
obuf_n:	dd 0
obuf:	rb 4096
