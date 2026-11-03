; ============================================================================
;  n64emu - l'emulateur Nintendo 64, en assembleur (docs/EMULATEURS.md).
;	n64emu [rom.z64]
;  Le coeur : n64_core.inc. Les listes d'affichage sont rendues par Mesa quand
;  ce programme est relie a Mesa (gl/, tools/emu/build-n64.sh), sinon par GpuKit ;
;  sans GPU (QEMU), par le rendu logiciel (n64_soft.inc). Le R4300 passe par le
;  recompilateur (n64_jit.inc) ; le menu CPU revient a l'interprete.
;  Relie a Mesa : assemble avec -d N64_BASE=..., au-dessus du C (posix.ld prend
;  l'adresse ordinaire) ; gl/n64gl_armpi.c entre par gl_start.
; ============================================================================
include '../../inc/armpi.inc'
match =N64_BASE, N64_BASE { N64_BASE equ 0x4000000000 }
app_header N64_BASE
include '../../inc/kit.inc'
include '../../inc/pad.inc'
include '../../kits/emukit/emukit.inc'
include '../../kits/gpukit/gpukit.inc'

SND_HZ		= 44100
OUT_W		= 640
OUT_H		= 480
ID_INTERP	= 100
ID_FILTER	= 101
GLA_init	= 0			; struct n64gl_api
GLA_frame	= 8
GLA_load	= 16
GLA_command	= 24
GLA_nosave	= 32
GLA_rom		= 40
GLA_romsize	= 48
GLA_tick	= 56

BTN_A		= 0x8000
BTN_B		= 0x4000
BTN_Z		= 0x2000
BTN_START	= 0x1000
BTN_DUP		= 0x0800
BTN_DDOWN	= 0x0400
BTN_DLEFT	= 0x0200
BTN_DRIGHT	= 0x0100
BTN_L		= 0x0020
BTN_R		= 0x0010
BTN_CUP		= 0x0008
BTN_CDOWN	= 0x0004
BTN_CLEFT	= 0x0002
BTN_CRIGHT	= 0x0001

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/emukit/emukit.abi'
kit_use '../../kits/gpukit/gpukit.abi'

gl_start:				; x1 = struct n64gl_api (juste avant start : e_entry - 8)
	adr	x2,g_gl
	str	x1,[x2]
start:
	mov	x19,x0				; arguments
	bl	kits_open
	adr	x0,m_bad				; (ui_message ne traduit pas son texte)
	bl	lang_tr
	adr	x1,desc
	str	x0,[x1,EMD_ERROR]
	mov	x0,0				; la machine, a zero
	movl	x1,N64_SIZE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.fail
	mov	x20,x0
	adr	x1,desc
	str	x0,[x1,EMD_CTX]
	bl	n64_init
	mov	x0,x20
	mov	x1,SND_HZ
	bl	n64_audio_rate
	mov	x0,0				; le code du recompilateur
	movl	x1,JIT_CODE_SIZE
	mov	x2,PROT_READ or PROT_WRITE or PROT_EXEC
	mov	x3,0
	sys	SYS_VM_MAP
	adr	x2,m_nojit
	tbnz	x0,63,.say
	mov	x1,x0
	mov	x0,x20
	bl	n64_jit_enable
	adr	x1,g_jit
	mov	x0,1
	str	x0,[x1]
	adr	x2,m_jit
.say:	mov	x0,x2
	bl	say
	adr	x0,g_gl
	ldr	x4,[x0]
	cbz	x4,.gk
	adr	x0,g_bufs			; Mesa d'abord : il fait lui-meme les images
	mov	x1,OUT_W
	mov	x2,OUT_H
	mov	x3,N64_FBW*4
	ldr	x4,[x4,GLA_init]
	blr	x4
	cbz	x0,.gk
	mov	w0,1
	str	w0,[x20,m_glOn]
	adr	x1,g_gpu
	str	x0,[x1]
	adr	x0,m_mesa
	bl	say
	b	.run0
.gk:	mov	x0,0
	bl	gk_info
	cmp	x0,1
	b.ne	.run0
	adr	x1,g_gpu
	str	x0,[x1]
	adr	x20,g_tex			; pas encore de textures
	mov	x1,0
	mov	w2,-1
.t:	str	w2,[x20,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,N64_MAX_TEX
	b.lo	.t
	adr	x20,g_bufs
	mov	x21,0
.b:	movl	x0,N64_FBW*N64_FBH*4
	bl	gk_alloc
	str	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	cmp	x21,3
	b.lo	.b
.run0:	adr	x0,g_gl
	ldr	x0,[x0]
	cbz	x0,.ui
	ldr	x1,[x0,GLA_rom]
	cbz	x1,.ui
	bl	bench
	sys	SYS_EXIT
.ui:	adr	x0,desc
	mov	x1,0
	cbz	x19,.run
	ldrb	w2,[x19]
	cbz	w2,.run
	mov	x1,x19
.run:	bl	emu_run
	sys	SYS_EXIT
.fail:	mov	x0,1
	sys	SYS_EXIT

; --bench (gl/n64gl_armpi.c) : la machine et son rendu sans fenetre, cb_frame comme sous EmuKit, Start et A a
; tour de role comme tools/emu/n64_harness.asm ; apres chaque image, GLA_tick (image, pixels) -> 0 : fini
bench:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	adr	x22,g_bufs
	mov	x21,0
.b:	ldr	x0,[x22,x21,lsl 3]
	cbnz	x0,.b1
	movl	x0,N64_FBW*N64_FBH*4
	bl	core_alloc
	str	x0,[x22,x21,lsl 3]
.b1:	add	x21,x21,1
	cmp	x21,3
	b.lo	.b
	adr	x0,desc
	ldr	x19,[x0,EMD_CTX]
	adr	x20,g_gl
	ldr	x20,[x20]
	mov	x0,x19
	ldr	x1,[x20,GLA_rom]
	ldr	x2,[x20,GLA_romsize]
	bl	cb_load
	cbz	x0,.out
	mov	x21,0				; image
.l:	mov	x3,0
	cmp	x21,300
	b.lo	.k
	mov	x0,3000
	cmp	x21,x0
	b.hs	.k
	ubfx	x0,x21,0,6
	cmp	x0,20
	b.hs	.k1
	mov	x3,PAD_START
	b	.k
.k1:	sub	x0,x0,32
	cmp	x0,20
	b.hs	.k
	mov	x3,PAD_A
.k:	mov	x0,3
	udiv	x1,x21,x0
	msub	x1,x1,x0,x21
	ldr	x23,[x22,x1,lsl 3]
	mov	x1,x23
	mov	x0,x19
	movl	x2,N64_FBW*4
	mov	x4,0
	bl	cb_frame
	mov	x0,x19
	adr	x1,bench_snd
	mov	x2,4096
	bl	n64_audio_read
	mov	x0,x21
	mov	x1,x23
	ldr	x2,[x20,GLA_tick]
	blr	x2
	add	x21,x21,1
	cbnz	x0,.l
.out:	mov	x0,0
	ldp	x29,x30,[sp],16
	ret

; (m, rom, octets) -> 1 : la cadence suit la region de la ROM, 50 Hz en PAL
cb_load:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	n64_load
	cbz	x0,.out
	ldrb	w0,[x19,m_pal]
	movl	x1,60000
	movl	x2,50000
	cmp	w0,0
	csel	x1,x2,x1,ne
	adr	x0,desc
	str	x1,[x0,EMD_FPS]
	ldr	w0,[x19,m_glOn]
	cbz	w0,.ok
	bl	emu_rom_path
	adr	x1,g_gl
	ldr	x1,[x1]
	ldr	x1,[x1,GLA_load]
	blr	x1
.ok:	mov	x0,1
.out:	ldr	x19,[sp],16
	epilogue

; x0 = texte termine par 0 : la sortie standard
say:	mov	x1,0
.l:	ldrb	w2,[x0,x1]
	cbz	w2,.p
	add	x1,x1,1
	b	.l
.p:	sys	SYS_PRINT
	ret

; (octets) -> memoire a zero
core_alloc:
	mov	x1,x0
	mov	x0,0
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	ret

; (m, px, l, h, pas) -> 1 : l'image rendue par le GPU
gpu_frame:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	adr	x0,g_gpu
	ldr	x0,[x0]
	cbz	x0,.no
	ldr	w0,[x19,m_gfxReady]
	tbnz	w0,31,.no
	str	x1,[sp]				; { px, w, h, stride, clear, flags }
	str	w2,[sp,8]
	str	w3,[sp,12]
	str	w4,[sp,16]
	str	wzr,[sp,24]
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	movl	x1,m_gfxFrame
	add	x20,x19,x1
	add	x20,x20,x0			; F
	ldr	w0,[x20,GF_clear]
	str	w0,[sp,20]
	ldr	w0,[x19,m_glOn]
	cbz	w0,.gk
	ldr	w0,[x20,GF_width]		; struct n64gl_frame
	str	w0,[sp,24]
	ldr	w0,[x20,GF_height]
	str	w0,[sp,28]
	ldr	x0,[x20,GF_v]
	str	x0,[sp,32]
	ldr	w0,[x20,GF_nv]
	str	w0,[sp,40]
	ldr	w0,[x20,GF_nb]
	str	w0,[sp,44]
	ldr	x0,[x20,GF_b]
	str	x0,[sp,48]
	movl	x0,M_TEXS
	add	x0,x19,x0
	str	x0,[sp,56]
	adr	x1,g_gl
	ldr	x1,[x1]
	ldr	x1,[x1,GLA_frame]
	mov	x0,sp
	blr	x1
	cbnz	x0,.gl1
	str	wzr,[x19,m_glOn]		; le GPU est perdu : le logiciel, des la prochaine liste
	adr	x0,g_gpu
	str	xzr,[x0]
	ldr	w0,[x19,m_gfxBuild]
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	add	x0,x19,x0
	add	x0,x0,m_gfxFrame
	str	wzr,[x0,GF_nv]			; (la liste en cours est au format de Mesa)
	str	wzr,[x0,GF_nb]
.gl1:	mov	x0,1
	b	.out
.gk:	movl	x21,M_TEXS
	add	x21,x19,x21			; T
	adr	x22,g_tex
	mov	w23,0
.tx:	ldrb	w0,[x21,TX_dirty]
	cbz	w0,.tn
	ldr	x1,[x21,TX_px]
	ldr	w2,[x21,TX_w]
	ldr	w3,[x21,TX_h]
	cmp	w2,0
	b.le	.tn
	cmp	w3,0
	b.le	.tn
	cbz	x1,.tn
	mul	w4,w2,w3
	cmp	w4,N64_TEX_PIXELS
	b.gt	.tn
	mov	w4,w2
	ldr	w0,[x22,x23,lsl 2]
	bl	gk_texture
	tbz	x0,63,.ts
	ldr	w1,[x22,x23,lsl 2]
	tbnz	w1,31,.ts
	mov	w0,-1
	ldr	x1,[x21,TX_px]
	ldr	w2,[x21,TX_w]
	ldr	w3,[x21,TX_h]
	mov	w4,w2
	bl	gk_texture
.ts:	str	w0,[x22,x23,lsl 2]
	strb	wzr,[x21,TX_dirty]
.tn:	add	x21,x21,TX_SIZE
	add	w23,w23,1
	cmp	w23,N64_MAX_TEX
	b.lo	.tx
	ldr	x24,[x20,GF_b]			; les lots, texture du GPU
	ldr	w25,[x20,GF_nb]
	adr	x26,g_batches
	mov	w21,0				; nb
.bt:	cbz	w25,.go
	sub	w25,w25,1
	ldr	w0,[x24,GB_tex]
	mov	w1,-1
	tbnz	w0,31,.b1
	ldr	w1,[x22,x0,lsl 2]
	tbnz	w1,31,.bn
.b1:	mov	x0,x26
	mov	x2,0
.bc:	ldr	x3,[x24,x2]
	str	x3,[x0,x2]
	add	x2,x2,8
	cmp	x2,GB_SIZE
	b.lo	.bc
	str	w1,[x26,GB_tex]
	add	x26,x26,GB_SIZE
	add	w21,w21,1
.bn:	add	x24,x24,GB_SIZE
	b	.bt
.go:	mov	x0,sp
	ldr	x1,[x20,GF_v]
	ldr	w2,[x20,GF_nv]
	adr	x3,g_batches
	mov	w4,w21
	bl	gk_render
	cmp	x0,0
	cset	x0,eq
	b	.out
.no:	mov	x0,0
.out:	add	sp,sp,64
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; (m, pixels, pas, boutons PAD_*, drapeaux)
cb_frame:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	w24,0				; b
	adr	x5,pad_map
.m:	ldr	w6,[x5],4
	cbz	w6,.f
	ldr	w7,[x5],4
	tst	x22,x6
	b.eq	.m
	orr	w24,w24,w7
	b	.m
.f:	mov	w25,0				; x
	mov	w26,0				; y
	mov	x0,3				; (le stick d'une manette branchee)
	str	x0,[sp,8]
.ax:	ldr	x0,[sp,8]
	mov	x1,sp
	bl	emu_pad_axes
	ldrsh	w0,[sp]
	ldrsh	w1,[sp,2]
	orr	w2,w0,w1
	cbz	w2,.a1
	mov	w2,80
	mul	w0,w0,w2
	mov	w3,1000
	sdiv	w25,w0,w3
	mul	w1,w1,w2
	neg	w1,w1
	sdiv	w26,w1,w3
.a1:	ldrsh	w0,[sp,4]
	mov	w1,500
	cmp	w0,w1
	b.le	.a2
	orr	w24,w24,BTN_CRIGHT
.a2:	cmn	w0,w1
	b.ge	.a3
	orr	w24,w24,BTN_CLEFT
.a3:	ldrsh	w0,[sp,6]
	cmp	w0,w1
	b.le	.a4
	orr	w24,w24,BTN_CDOWN
.a4:	cmn	w0,w1
	b.ge	.a5
	orr	w24,w24,BTN_CUP
.a5:	ldr	x0,[sp,8]
	subs	x0,x0,1
	str	x0,[sp,8]
	b.hs	.ax
	orr	w0,w25,w26			; (au clavier : les fleches font aussi le stick)
	cbnz	w0,.s
	tst	x22,PAD_RIGHT
	b.eq	.k1
	mov	w25,80
.k1:	tst	x22,PAD_LEFT
	b.eq	.k2
	mov	w25,-80
.k2:	tst	x22,PAD_UP
	b.eq	.k3
	mov	w26,80
.k3:	tst	x22,PAD_DOWN
	b.eq	.s
	mov	w26,-80
.s:	mov	x0,x19
	mov	x1,0
	mov	w2,w24
	mov	w3,w25
	mov	w4,w26
	bl	n64_set_pad
	mov	x0,x19
	bl	n64_frame
	tbnz	x23,0,.out			; EMF_SKIP
	adr	x0,g_serial
	ldr	w1,[x0]
	ldr	w2,[x0,4]
	ldr	w3,[x19,m_gfxSerial]
	cmp	w3,w1
	b.eq	.g1
	mov	w1,w3
	mov	w2,0
	b	.g2
.g1:	cmp	w2,1000
	b.hs	.g2
	add	w2,w2,1
.g2:	str	w1,[x0]
	str	w2,[x0,4]
	cmp	w2,30
	b.hs	.fb
	ldr	w1,[x0,8]			; un jeu a 20 images/s : deux VI sur trois n'ont rien de neuf
	cmp	w1,w3
	b.ne	.draw
	bl	emu_frame_same
	b	.out
.draw:	lsr	x22,x21,2			; stride
	mov	x0,x19
	mov	x1,x20
	mov	x2,OUT_W
	mov	x3,OUT_H
	mov	x4,x22
	bl	gpu_frame
	cbnz	x0,.sn
	mov	x0,x19
	mov	x1,x20
	mov	x2,OUT_W
	mov	x3,OUT_H
	mov	x4,x22
	bl	n64_sw_frame
	cbz	x0,.fb
.sn:	adr	x0,g_serial
	ldr	w1,[x19,m_gfxSerial]
	str	w1,[x0,8]
	mov	x0,x19
	mov	x1,x20
	mov	x2,OUT_W
	mov	x3,OUT_H
	mov	x4,x22
	bl	n64_snapshot
	mov	x0,OUT_W
	mov	x1,OUT_H
	bl	emu_frame_size
	b	.out
.fb:	ldr	w24,[x19,m_fbW]
	ldr	w25,[x19,m_fbH]
	cmp	w24,0
	b.le	.out
	cmp	w25,0
	b.le	.out
	movl	x1,M_FB
	add	x1,x19,x1
	mov	x2,x20
	mov	w3,w25
.yl:	mov	w4,w24
	mov	x5,x2
.xl:	ldr	w6,[x1],4
	str	w6,[x5],4
	subs	w4,w4,1
	b.ne	.xl
	add	x2,x2,x21
	subs	w3,w3,1
	b.ne	.yl
	mov	w0,w24
	mov	w1,w25
	bl	emu_frame_size
.out:	add	sp,sp,16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; (m, &pointeur) -> octets a ecrire
cb_save:
	adr	x2,g_gl				; (--nosave : rien n'est ecrit a cote de la ROM)
	ldr	x2,[x2]
	cbz	x2,.go
	ldr	x2,[x2,GLA_nosave]
	cbz	x2,.go
	mov	x0,0
	ret
.go:	ldrb	w2,[x0,m_sramDirty]
	cbz	w2,.e
	strb	wzr,[x0,m_sramDirty]
	add	x2,x0,M_SRAM
	str	x2,[x1]
	mov	x0,0x8000
	ret
.e:	ldrb	w2,[x0,m_eepromDirty]
	cbz	w2,.n
	strb	wzr,[x0,m_eepromDirty]
	add	x2,x0,M_EEPROM
	str	x2,[x1]
	ldrsw	x0,[x0,m_eepromSize]
	ret
.n:	mov	x0,0
	ret

; (m, donnees, octets)
cb_restore:
	cmp	x2,512
	b.eq	.e
	cmp	x2,2048
	b.eq	.e
	add	x3,x0,M_SRAM
	mov	x4,SRAM_SIZE
	cmp	x2,x4
	csel	x2,x4,x2,hs
	b	.c
.e:	str	w2,[x0,m_eepromSize]
	mov	w4,3
	mov	w5,2
	cmp	x2,512
	csel	w4,w5,w4,eq
	str	w4,[x0,m_saveType]
	add	x3,x0,M_EEPROM
.c:	cbz	x2,.r
	ldrb	w4,[x1],1
	strb	w4,[x3],1
	sub	x2,x2,1
	b	.c
.r:	ret

; (m, numero)
cb_cmd:
	cmp	x1,ID_FILTER
	b.ne	.i
	ldr	w2,[x0,m_glOn]
	cbz	w2,.r
	adr	x2,g_filter
	ldr	x0,[x2]
	eor	x0,x0,1
	str	x0,[x2]
	adr	x2,g_gl
	ldr	x2,[x2]
	ldr	x2,[x2,GLA_command]
	br	x2
.i:	cmp	x1,ID_INTERP
	b.ne	.r
	adr	x1,g_jit
	ldr	x1,[x1]
	cbz	x1,.r
	ldrb	w1,[x0,m_useJit]
	eor	w1,w1,1
	strb	w1,[x0,m_useJit]
	adr	x0,m_jit2
	adr	x2,m_int
	cmp	w1,0
	csel	x0,x2,x0,eq
	b	say
.r:	ret

include 'n64_core.inc'

pad_map:
	dw	PAD_A, BTN_A
	dw	PAD_X, BTN_B
	dw	PAD_L2 or PAD_R2, BTN_Z
	dw	PAD_L, BTN_L
	dw	PAD_R, BTN_R
	dw	PAD_START, BTN_START
	dw	PAD_UP, BTN_DUP
	dw	PAD_DOWN, BTN_DDOWN
	dw	PAD_LEFT, BTN_DLEFT
	dw	PAD_RIGHT, BTN_DRIGHT
	dw	PAD_Y, BTN_CUP
	dw	PAD_B, BTN_CRIGHT
	dw	0

m_bad		db 'Not a Nintendo 64 ROM.',0
s_name		db 'Nintendo 64',0
s_ext		db '*.z64;*.n64;*.v64',0
s_menu		db 'MCPU',10,'I100',9,'Recompiler / Interpreter',10,'I101',9,'GPU filter: 3-point / bilinear',10,0
m_mesa		db 'n64emu : rendu par Mesa',10,0
m_jit		db 'n64emu : recompilateur actif',10,0
m_nojit		db 'n64emu : interprete seul (pas de memoire executable)',10,0
m_jit2		db 'n64emu : recompilateur',10,0
m_int		db 'n64emu : interprete',10,0
		align 4

segment readable writeable
kit_data
		align 8
desc:		dd s_name
		dd s_ext
		dd OUT_W, OUT_H
		dd N64_FBW, N64_FBH
		dd 60000
		dd SND_HZ
		dd 0				; ctx
		dd cb_load
		dd cb_frame
		dd n64_audio_read
		dd n64_reset
		dd cb_save
		dd cb_restore
		dd s_menu
		dd cb_cmd
		dd 0
		dd g_bufs
		dd 1
		dd m_bad
g_bufs:		dd 0, 0, 0
g_jit:		dd 0
g_gpu:		dd 0
g_gl:		dd 0
g_filter:	dd 1
bench_snd:	rb 4096*4
g_serial:	dw 0, 1000, 0xFFFFFFFF		; liste, VI depuis, derniere liste dessinee
g_tex:		rb N64_MAX_TEX*4
		align 8
g_batches:	rb N64_MAXB*GB_SIZE
