; n64_harness : le coeur N64 en assembleur sous qemu-aarch64 (Linux, mode utilisateur), pour le
; comparer au coeur C++ (tools/emu/ref_n64.cpp). Une ligne par image : numero, cycles, sommes des
; registres, de la RDRAM (toutes les 32 images), de l'image, pc, sommes du son, des sommets, des lots.
;     qemu-aarch64 n64_harness rom images [mode]
; mode : j le recompilateur, s le rendu logiciel et l'ecriture des images en RDRAM, comme l'application ;
; en 2e lettre, t : sans sommes ni sorties (time_n64.sh).
; g : le flux de Mesa (m_glOn), et des images pour tools/emu/n64gl_replay.c :
;     qemu-aarch64 n64_harness rom images g sortie premiere pas
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

FNV0	= 0x811C9DC5

segment readable executable
start:
	mov	x19,sp
	ldr	x20,[x19]			; argc
	ldr	x21,[x19,16]			; rom
	ldr	x0,[x19,24]			; images
	bl	atoi
	mov	x22,x0
	mov	x0,0
	movl	x1,0x10000000
	mov	x2,3
	mov	x3,0x22
	mov	x4,-1
	mov	x5,0
	linux	222
	mov	x23,x0				; 256 Mo
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
	movl	x0,0x6000000
	add	x0,x0,x23
	adr	x1,alloc_p
	str	x0,[x1]
	mov	x0,x26
	bl	n64_init
	mov	x0,x26
	mov	x1,44100
	bl	n64_audio_rate
	mov	x0,x26
	mov	x1,x23
	mov	x2,x25
	bl	n64_load
	cbz	x0,.fail
	cmp	x20,4
	b.lo	.nj
	ldr	x9,[x19,32]
	ldrb	w0,[x9]
	ldrb	w1,[x9,1]
	mov	x9,0
	cmp	w0,'s'
	b.eq	.ms
	cmp	w1,'s'
	b.ne	.mj
.ms:	mov	x9,1
.mj:	adr	x2,use_sw
	str	x9,[x2]
	cmp	w1,'t'
	cset	x1,eq
	adr	x2,quiet
	str	x1,[x2]
	cmp	w0,'g'
	b.ne	.ng0
	mov	w0,1
	str	w0,[x26,m_glOn]
	ldr	x0,[x19,48]
	bl	atoi
	adr	x1,gl_first
	str	x0,[x1]
	ldr	x0,[x19,56]
	bl	atoi
	adr	x1,gl_step
	str	x0,[x1]
	mov	x0,-100
	ldr	x1,[x19,40]
	movl	x2,0x241			; O_WRONLY O_CREAT O_TRUNC
	mov	x3,0x1A4
	linux	56
	adr	x1,gl_fd
	str	x0,[x1]
	b	.nj
.ng0:	cmp	w0,'j'
	b.ne	.nj
	mov	x0,0
	movl	x1,JIT_CODE_SIZE
	mov	x2,7
	mov	x3,0x22
	mov	x4,-1
	mov	x5,0
	linux	222
	mov	x1,x0
	mov	x0,x26
	bl	n64_jit_enable
.nj:	movl	x28,0xF000000
	add	x28,x28,x23			; son
	mov	x21,0				; numero
.f:	cmp	x21,x22
	b.hs	.end
	lsr	w0,w21,5
	movl	w1,2654435761
	mul	w0,w0,w1
	lsr	w0,w0,16
	mov	w2,0
	tst	x21,0x100
	csel	w2,w0,wzr,ne
	mov	x0,x26
	mov	x1,0
	mov	w3,0
	mov	w4,0
	adr	x5,gl_fd			; (mode g : Start et A a tour de role, jusqu'au jeu)
	ldr	x5,[x5]
	cbz	x5,.nop
	cmp	x21,300
	b.lo	.nop
	mov	x5,3000
	cmp	x21,x5
	b.hs	.nop
	ubfx	x5,x21,0,6
	cmp	x5,20
	b.hs	.pa
	mov	w2,0x1000
	b	.nop
.pa:	sub	x5,x5,32
	cmp	x5,20
	b.hs	.nop
	mov	w2,0x8000
.nop:	bl	n64_set_pad
	mov	x0,x26
	bl	n64_frame
	mov	w27,0
	adr	x0,use_sw
	ldr	x0,[x0]
	cbz	x0,.ns
	adr	x0,sw_serial
	ldr	w1,[x0]
	ldr	w2,[x0,4]
	ldr	w3,[x26,m_gfxSerial]
	cmp	w3,w1
	b.eq	.s1
	mov	w1,w3
	mov	w2,0
	b	.s2
.s1:	cmp	w2,1000
	b.hs	.s2
	add	w2,w2,1
.s2:	str	w1,[x0]
	str	w2,[x0,4]
	cmp	w2,30
	b.hs	.ns
	ldr	w1,[x0,8]
	cmp	w1,w3
	b.eq	.ns
	mov	x0,x26
	movl	x1,0xF100000
	add	x1,x1,x23
	mov	x2,640
	mov	x3,480
	mov	x4,640
	bl	n64_sw_frame
	cbz	x0,.ns
	adr	x0,sw_serial
	ldr	w1,[x26,m_gfxSerial]
	str	w1,[x0,8]
	mov	x0,x26
	movl	x1,0xF100000
	add	x1,x1,x23
	mov	x2,640
	mov	x3,480
	mov	x4,640
	bl	n64_snapshot
	movl	x0,0xF100000
	add	x0,x0,x23
	movl	x1,640*480*4
	movl	w2,FNV0
	bl	fnv
	mov	w27,w0
.ns:	adr	x0,gl_fd
	ldr	x0,[x0]
	cbz	x0,.ns1
	bl	gl_dump
.ns1:	adr	x0,sw_hash
	str	w27,[x0]
	movl	w24,FNV0
.au:	mov	x0,x26
	mov	x1,x28
	mov	x2,8192
	bl	n64_audio_read
	cbz	x0,.aue
	mov	x1,x0
	lsl	x1,x1,2
	mov	x0,x28
	mov	w2,w24
	bl	fnv
	mov	w24,w0
	b	.au
.aue:	adr	x0,quiet
	ldr	x0,[x0]
	cbnz	x0,.nx
	mov	x0,x21
	bl	out_dec
	mov	w0,' '
	bl	out_c
	ldr	x0,[x26,m_cycles]
	bl	out_dec
	mov	w0,' '
	bl	out_c
	add	x0,x26,m_fpr
	mov	x1,256
	movl	w2,FNV0
	bl	fnv
	mov	w2,w0
	mov	x0,x26
	mov	x1,256
	bl	fnv
	bl	out_hex
	mov	w0,' '
	bl	out_c
	mov	w0,0
	tst	x21,31
	b.ne	.nr
	add	x0,x26,M_RDRAM
	movl	x1,N64_RDRAM_SIZE
	movl	w2,FNV0
	bl	fnv
.nr:	bl	out_hex
	mov	w0,' '
	bl	out_c
	ldr	w1,[x26,m_fbW]
	ldr	w2,[x26,m_fbH]
	mul	x1,x1,x2
	lsl	x1,x1,2
	movl	x0,M_FB
	add	x0,x26,x0
	movl	w2,FNV0
	bl	fnv
	bl	out_hex
	mov	w0,' '
	bl	out_c
	ldr	w0,[x26,m_pc]
	bl	out_dec
	mov	w0,' '
	bl	out_c
	mov	w0,w24
	bl	out_hex
	mov	w0,' '
	bl	out_c
	ldr	w0,[x26,m_gfxReady]
	mov	x25,0
	mov	w27,0
	movl	w24,FNV0
	movl	w27,FNV0
	tbnz	w0,31,.ng
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	add	x25,x26,x0
	add	x25,x25,m_gfxFrame
	ldr	x0,[x25,GF_v]
	ldrsw	x1,[x25,GF_nv]
	lsl	x1,x1,5
	mov	w2,w24
	bl	fnv
	mov	w24,w0
	ldr	x0,[x25,GF_b]
	ldrsw	x1,[x25,GF_nb]
	mov	x2,GB_SIZE
	mul	x1,x1,x2
	mov	w2,w27
	bl	fnv
	mov	w27,w0
.ng:	mov	w0,w24
	bl	out_hex
	mov	w0,' '
	bl	out_c
	mov	w0,w27
	bl	out_hex
	mov	w0,' '
	bl	out_c
	adr	x0,sw_hash
	ldr	w0,[x0]
	bl	out_hex
	mov	w0,10
	bl	out_c
.nx:	add	x21,x21,1
	b	.f
.end:	bl	flush
	mov	x0,0
	linux	93
.fail:	mov	x0,1
	linux	93

; x1 = donnees, x2 = octets
gl_write:
	adr	x0,gl_fd
	ldr	x0,[x0]
	linux	64
	ret

; x21 = image, x26 = m : tete, sommets, lots, table des textures, pixels de celles qui existent
gl_dump:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,gl_first
	ldr	x1,[x0]
	subs	x1,x21,x1
	b.lo	.r
	ldr	x2,[x0,8]
	udiv	x3,x1,x2
	msub	x3,x3,x2,x1
	cbnz	x3,.r
	ldr	w0,[x26,m_gfxReady]
	tbnz	w0,31,.r
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	add	x22,x26,x0
	add	x22,x22,m_gfxFrame
	sub	sp,sp,32
	movl	w0,0x44474C4E			; 'NLGD'
	str	w0,[sp]
	ldr	w0,[x22,GF_clear]
	str	w0,[sp,4]
	ldr	w0,[x22,GF_width]
	str	w0,[sp,8]
	ldr	w0,[x22,GF_height]
	str	w0,[sp,12]
	ldr	w0,[x22,GF_nv]
	str	w0,[sp,16]
	ldr	w0,[x22,GF_nb]
	str	w0,[sp,20]
	str	w21,[sp,24]
	mov	x1,sp
	mov	x2,32
	bl	gl_write
	add	sp,sp,32
	ldr	x1,[x22,GF_v]
	ldrsw	x2,[x22,GF_nv]
	lsl	x2,x2,5
	bl	gl_write
	ldr	x1,[x22,GF_b]
	ldrsw	x2,[x22,GF_nb]
	mov	x0,GB_SIZE
	mul	x2,x2,x0
	bl	gl_write
	movl	x23,M_TEXS
	add	x23,x26,x23
	mov	x1,x23
	mov	x2,N64_MAX_TEX*TX_SIZE
	bl	gl_write
	mov	w24,0
.t:	ldr	x1,[x23,TX_px]
	ldr	w2,[x23,TX_w]
	ldr	w3,[x23,TX_h]
	mul	w2,w2,w3
	lsl	w2,w2,2
	cbz	w2,.n
	bl	gl_write
.n:	add	x23,x23,TX_SIZE
	add	w24,w24,1
	cmp	w24,N64_MAX_TEX
	b.lo	.t
.r:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

core_alloc:
	adr	x1,alloc_p
	ldr	x2,[x1]
	add	x0,x0,15
	and	x0,x0,0xFFFFFFFFFFFFFFF0
	add	x0,x2,x0
	str	x0,[x1]
	mov	x0,x2
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

; par mots, comme ref_n64.cpp
fnv:	movl	w3,16777619
	add	x1,x0,x1
.l:	cmp	x0,x1
	b.hs	.f
	ldr	w4,[x0],4
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
out_hex:
	stp	x19,x30,[sp,-16]!
	stp	x20,x21,[sp,-16]!
	mov	w19,w0
	mov	w20,28
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

if defined N64_TRACE
; une ligne par instruction : pc, cycles, sommes des registres et des flottants, hi, lo
n64_trace:
	sub	sp,sp,176
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
	movl	x0,TR_FROM
	cmp	x20,x0
	b.lo	.s
	mov	w0,'T'
	bl	out_c
	mov	w0,' '
	bl	out_c
	ldr	w0,[x19,m_pc]
	bl	out_hex
	mov	w0,' '
	bl	out_c
	mov	x0,x20
	bl	out_dec
	mov	w0,' '
	bl	out_c
	mov	x0,x19
	mov	x1,256
	movl	w2,FNV0
	bl	fnv
	bl	out_hex
	mov	w0,' '
	bl	out_c
	add	x0,x19,m_fpr
	mov	x1,256
	movl	w2,FNV0
	bl	fnv
	bl	out_hex
	mov	w0,' '
	bl	out_c
	ldr	w0,[x19,m_hi]
	bl	out_hex
	mov	w0,' '
	bl	out_c
	ldr	w0,[x19,m_lo]
	bl	out_hex
	mov	w0,10
	bl	out_c
.s:	ldp	x0,x1,[sp]
	ldp	x2,x3,[sp,16]
	ldp	x4,x5,[sp,32]
	ldp	x6,x7,[sp,48]
	ldp	x8,x9,[sp,64]
	ldp	x10,x11,[sp,80]
	ldp	x12,x13,[sp,96]
	ldp	x14,x15,[sp,112]
	ldp	x16,x17,[sp,128]
	ldr	x30,[sp,144]
	add	sp,sp,176
	ret
end if

include '../../src/apps/n64emu/n64_core.inc'

segment readable writeable
alloc_p: dd 0
use_sw:	dd 0
quiet:	dd 0
sw_serial: dw 0,1000,0xFFFFFFFF
sw_hash: dd 0
obuf_n:	dd 0
obuf:	rb 4096
		align 8
gl_fd:		dd 0
gl_first:	dd 0
gl_step:	dd 1
