; ============================================================================
;  n64_core.inc - la Nintendo 64 : le R4300i (MIPS III, TLB, COP1), les interfaces
;  du RCP (MI, VI, AI, PI, SI et le PIF, RI, SP, DP), le demarrage fait ici (PIF
;  et IPL3), la cartouche, les taches du RSP a haut niveau (n64_gfx.inc,
;  n64_audio.inc), le recompilateur (n64_jit.inc). Ecrit d'apres le coeur C++
;  (tools/emu/n64ref), image pour image : tools/emu/ (docs/EMULATEURS.md).
;
;	n64_init (m)			avant tout le reste
;	n64_load (m, rom, octets)	-> 1 ; 0 : pas une ROM
;	n64_reset (m)
;	n64_frame (m)			une image : m->fb, ou la liste d'affichage
;	n64_set_pad (m, n, boutons, x, y)
;	n64_audio_rate (m, hz), n64_audio_read (m, lr, trames) -> trames
;	n64_snapshot (m, px, l, h, pas)
;	n64_sw_frame (m, px, l, h, pas)	-> 1 : le rendu logiciel (n64_soft.inc)
;	n64_jit_enable (m, code)		JIT_CODE_SIZE octets executables
;	m = N64_SIZE octets a zero. L'appelant fournit core_alloc (octets) -> x0.
;
;  Convention ordinaire : x0-x17 de passage, x19-x28 gardes. Dans le coeur
;  x19 = m (r [] au debut), x20 = cycles ; l'interprete (step) perd x21-x28,
;  a lui seul, sans les sauver : ceux qui l'appellent n'en attendent rien.
; ============================================================================

N64_RDRAM_SIZE	= 0x800000
N64_FBW		= 640
N64_FBH		= 576
N64_ABUF	= 8192
N64_CPU_HZ	= 93750000
N64_MAX_TEX	= 256
N64_TEX_PIXELS	= 8192
N64_MAXV	= 120000
N64_MAXB	= 4096
SRAM_SIZE	= 0x20000
EV_VI		= 0
EV_COMPARE	= 1
EV_PI		= 2
EV_SI		= 3
EV_AI		= 4
EV_SP		= 5
EV_DP		= 6
EV_N		= 7
MI_SP		= 1
MI_SI		= 2
MI_AI		= 4
MI_VI		= 8
MI_PI		= 16
MI_DP		= 32

virtual at 0
m_r:		rb 256
m_hi:		rb 8
m_lo:		rb 8
m_cycles:	rb 8
m_nextEvent:	rb 8
m_countBase:	rb 8
m_jitSite:	rb 8
m_jitTab:	rb 8
m_jitPage:	rb 8
m_jitRec:	rb 8
m_jitInfo:	rb 8
m_jitLinks:	rb 8
m_jitCode:	rb 8
m_jitCp:	rb 8
m_jitStart:	rb 8			; premier mot libre (octets depuis jitCode)
m_jitEnter:	rb 8
m_jitNorm:	rb 8
m_jitChain:	rb 8
m_jitSide:	rb 8
m_jitSlot:	rb 8
m_jitJr:	rb 8
m_jitCompiled:	rb 8
m_jitFlushes:	rb 8
m_jitBlocks:	rb 4
m_jitNlinks:	rb 4
m_rom:		rb 8
m_snap:		rb 8
m_evAt:		rb 7*8
m_viCyclesLine:	rb 8
m_cp0:		rb 256
m_fpr:		rb 256
m_pc:		rb 4
m_npc:		rb 4
m_curPc:	rb 4
m_fcr31:	rb 4
m_jitStep:	rb 4
m_romSize:	rb 4
m_cic:		rb 4
m_frames:	rb 4
m_fbW:		rb 4
m_fbH:		rb 4
m_lastTlb:	rb 4
m_tlbMissVector: rb 4
m_badVa:	rb 4
m_lastExcPc:	rb 4
m_lastExcCode:	rb 4
m_viLineNow:	rb 4
m_viLines:	rb 4
m_aiCount:	rb 4
m_aHead:	rb 4
m_aTail:	rb 4
m_outRate:	rb 4
m_resAcc:	rb 4
m_lastL:	rb 4
m_lastR:	rb 4
m_aIn:		rb 4
m_aOut:		rb 4
m_aCount:	rb 4
m_aLoop:	rb 4
m_eepromSize:	rb 4
m_saveType:	rb 4
m_spPc:		rb 4
m_aiFifo:	rb 16
m_mi:		rb 16
m_vi:		rb 56
m_ai:		rb 24
m_pi:		rb 52
m_si:		rb 28
m_ri:		rb 32
m_sp:		rb 32
m_dp:		rb 32
m_rdramReg:	rb 40
m_rspTasks:	rb 32
m_irqCount:	rb 24
m_lastTask:	rb 4
m_audioTasks:	rb 4
m_audioUnknown:	rb 4
m_excCount:	rb 128
m_padBtn:	rb 8
m_aEnvV:	rb 6
m_aEnvS:	rb 6
m_padX:		rb 4
m_padY:		rb 4
m_pifRam:	rb 64
m_llbit:	rb 1
m_inDelay:	rb 1
m_nextDelay:	rb 1
m_excPending:	rb 1
m_pal:		rb 1
m_frameDone:	rb 1
m_sramDirty:	rb 1
m_eepromDirty:	rb 1
m_useJit:	rb 1
m_mvpDirty:	rb 1
m_drawMain:	rb 1
m_dlEnd:	rb 1
m_title:	rb 21
m_pad1:		rb 3
m_tlb:		rb 512
; le graphisme
m_segment:	rb 64
m_mtxStack:	rb 640
m_proj:		rb 64
m_mvp:		rb 64
m_mtxSp:	rb 4
m_geom:		rb 4
m_texScaleS:	rb 4
m_texScaleT:	rb 4
m_texTile:	rb 4
m_texOn:	rb 4
m_numLights:	rb 4
m_vpScale:	rb 8
m_vpTrans:	rb 8
m_rdpHalf1:	rb 4
m_rdpHalf2:	rb 4
m_fogMul:	rb 4
m_fogOff:	rb 4
m_omH:		rb 4
m_omL:		rb 4
m_combH:	rb 4
m_combL:	rb 4
m_primC:	rb 16
m_envC:		rb 16
m_blendC:	rb 16
m_fogC:		rb 16
m_primW:	rb 4			; les memes, tels que la commande les donne (R G B A) : le flux GL
m_envW:		rb 4
m_blendW:	rb 4
m_fogW:		rb 4
m_primLod:	rb 4
m_fillColor:	rb 4
m_primDepth:	rb 4
m_cimg:		rb 4
m_cimgW:	rb 4
m_zimg:		rb 4
m_timg:		rb 4
m_timgW:	rb 4
m_timgSiz:	rb 4
m_timgFmt:	rb 4
m_tmemSerial:	rb 4
m_scissor:	rb 16
m_cimgSiz:	rb 4
m_rectTile:	rb 4
m_fbSnapW:	rb 4
m_fbSnapH:	rb 4
m_fbSnapSerial:	rb 4
m_fbRec:	rb 64
m_fbRecNext:	rb 4
m_gfxBuild:	rb 4
m_gfxReady:	rb 4
m_gfxSerial:	rb 4
m_texClock:	rb 4
m_pcDl:		rb 4
m_glOn:		rb 4			; 1 : les lots vont a n64gl (n64_gl.inc), plus au combineur par sommet
m_gfxFrame:	rb 2*GF_SIZE
m_lights:	rb 8*LI_SIZE
m_tiles:	rb 8*TI_SIZE
m_vtx:		rb 64*VX_SIZE
		align 8
m_texMemo:	rb 8*TM_SIZE
m_glS:		rb GB_SIZE		; l'etat du prochain lot GL
m_end:
end virtual

; une image de graphisme (la disposition du rendu)
GF_v		= 0
GF_nv		= 8
GF_b		= 16
GF_nb		= 24
GF_clear	= 28
GF_width	= 32
GF_height	= 36
GF_cimg		= 40
GF_SIZE		= 48
; un sommet, un lot (le GPU les lit)
GV_x		= 0
GV_s		= 16
GV_r		= 24
GV_r2		= 28
GV_SIZE		= 32
GB_first	= 0
GB_count	= 4
GB_tex		= 8
GB_flags	= 12
GB_m		= 16
GB_SIZE		= 80
; une texture
TX_px		= 0
TX_w		= 8
TX_h		= 12
TX_key		= 16
TX_lastUse	= 24
TX_dirty	= 28
TX_SIZE		= 32
LI_r		= 0
TM_w		= 20		; (les 5 premiers champs : ceux de la tuile, TI_fmt a TI_pal)
TM_h		= 24
TM_tlut		= 28
TM_serial	= 32
TM_id		= 36		; 0 : rien, sinon texture + 1
TM_key		= 40
TM_SIZE		= 64
LI_x		= 12
LI_SIZE		= 24
TI_fmt		= 0
TI_siz		= 4
TI_line		= 8
TI_tmem		= 12
TI_pal		= 16
TI_cmt		= 20
TI_maskt	= 24
TI_shiftt	= 28
TI_cms		= 32
TI_masks	= 36
TI_shifts	= 40
TI_sl		= 44
TI_tl		= 48
TI_sh		= 52
TI_th		= 56
TI_SIZE		= 60
VX_x		= 0
VX_y		= 4
VX_z		= 8
VX_w		= 12
VX_s		= 16
VX_t		= 20
VX_r		= 24
VX_g		= 28
VX_b		= 32
VX_a		= 36
VX_clip		= 40
VX_SIZE		= 44

M_DMEM		= 0x8000
M_IMEM		= 0x9000
M_TMEM		= 0xA000
M_ADMEM		= 0xB000
M_ABOOK		= 0xC000
M_EEPROM	= 0xD000
M_TEXS		= 0xE000		; 256 GTexture
M_RSLUT		= 0x10000
M_ABUF		= 0x11000
M_SRAM		= 0x20000
M_RDRAM		= 0x100000
M_FB		= 0x900000
M_SNAP		= 0xA60000
M_FV0		= 0xBC0000
M_FV1		= 0xF80000
M_FB0		= 0x1340000
M_FB1		= 0x1360000
M_TEXPX		= 0x1380000
M_PIC		= 0x1380000 + N64_MAX_TEX*N64_TEX_PIXELS*4
M_ZB		= M_PIC + N64_FBW*N64_FBH*4
N64_SIZE	= M_ZB + N64_FBW*N64_FBH*4
assert m_end <= M_DMEM
assert m_nextDelay < 4096

macro adm reg, ofs {
	movl	reg,ofs
	add	reg,x19,reg
}
macro jtab idx, tab {
	adr	x16,tab
	ldrsw	x17,[x16,idx,lsl 2]
	add	x16,x16,x17
	br	x16
}
macro save_all {
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
}
macro rest_all {
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
}
macro save_callee {
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
}
macro rest_callee {
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
}
macro xlate_slow2 wr,cont {
	str	x30,[sp,-16]!
	mov	w1,wr
	bl	translate
	ldr	x30,[sp],16
	cbnz	x0,cont
	strb	wzr,[x19,m_excPending]
	add	sp,sp,16
	mov	x0,0
	ret
}
macro rdram_base reg {
	add	reg,x19,M_RDRAM
}

; ---------------------------------------------------------------------------
;  Entrees
; ---------------------------------------------------------------------------

n64_init:				; (m)
	movl	x1,M_FV0
	add	x1,x0,x1
	str	x1,[x0,m_gfxFrame+GF_v]
	movl	x1,M_FV1
	add	x1,x0,x1
	str	x1,[x0,m_gfxFrame+GF_SIZE+GF_v]
	movl	x1,M_FB0
	add	x1,x0,x1
	str	x1,[x0,m_gfxFrame+GF_b]
	movl	x1,M_FB1
	add	x1,x0,x1
	str	x1,[x0,m_gfxFrame+GF_SIZE+GF_b]
	movl	x1,M_TEXS
	add	x1,x0,x1
	movl	x2,M_TEXPX
	add	x2,x0,x2
	mov	w3,N64_MAX_TEX
.t:	str	x2,[x1,TX_px]
	add	x1,x1,TX_SIZE
	add	x2,x2,N64_TEX_PIXELS*4
	subs	w3,w3,1
	b.ne	.t
	mov	w1,6102
	str	w1,[x0,m_cic]
	ret

n64_audio_rate:				; (m, hz)
	mov	w2,48000
	cmp	w1,0
	csel	w1,w1,w2,gt
	str	w1,[x0,m_outRate]
	ret

n64_set_pad:				; (m, n, boutons, x, y)
	cmp	x1,4
	b.hs	.r
	add	x5,x0,m_padBtn
	strh	w2,[x5,x1,lsl 1]
	add	x5,x0,m_padX
	strb	w3,[x5,x1]
	add	x5,x0,m_padY
	strb	w4,[x5,x1]
.r:	ret

; (m, lr, trames max) -> trames
n64_audio_read:
	movl	x3,M_ABUF
	add	x3,x0,x3
	ldr	w4,[x0,m_aTail]
	ldr	w5,[x0,m_aHead]
	mov	x6,0
.l:	cmp	x6,x2
	b.hs	.f
	cmp	w4,w5
	b.eq	.f
	ldr	w7,[x3,x4,lsl 2]
	str	w7,[x1],4
	add	w4,w4,1
	and	w4,w4,N64_ABUF-1
	add	x6,x6,1
	b	.l
.f:	str	w4,[x0,m_aTail]
	mov	x0,x6
	ret

n64_reset:				; (m)
	prologue
	save_all
	mov	x19,x0
	bl	do_reset
	str	x20,[x19,m_cycles]
	rest_all
	epilogue

n64_frame:				; (m)
	prologue
	save_all
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	stp	d12,d13,[sp,-16]!
	stp	d14,d15,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,m_cycles]
	strb	wzr,[x19,m_frameDone]
	movl	x0,N64_CPU_HZ
	add	x0,x20,x0
	str	x0,[sp,-16]!			; guard
.l:	ldrb	w0,[x19,m_frameDone]
	cbnz	w0,.f
	ldr	x0,[sp]
	cmp	x20,x0
	b.hs	.f
	ldrb	w0,[x19,m_useJit]
	cbz	w0,.i
	bl	jit_run
	b	.e
.i:	ldr	x0,[x19,m_nextEvent]
	cmp	x20,x0
	b.hs	.e
	bl	step
	b	.i
.e:	bl	events
	b	.l
.f:	add	sp,sp,16
	ldrb	w0,[x19,m_frameDone]
	cbnz	w0,.g
	bl	vi_output
.g:	ldr	w0,[x19,m_frames]
	add	w0,w0,1
	str	w0,[x19,m_frames]
	str	x20,[x19,m_cycles]
	ldp	d14,d15,[sp],16
	ldp	d12,d13,[sp],16
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	rest_all
	epilogue

; x0 = debut, x1 = octets (multiple de 8)
mem_zero:
	str	xzr,[x0],8
	subs	x1,x1,8
	b.ne	mem_zero
	ret

; x0 -> crc32 (x1 octets), x0 = debut
crc32_of:
	mvn	w2,wzr
	movl	w5,0xEDB88320
.l:	cbz	x1,.f
	ldrb	w3,[x0],1
	eor	w2,w2,w3
	mov	w4,8
.b:	and	w3,w2,1
	neg	w3,w3
	and	w3,w3,w5
	eor	w2,w3,w2,lsr 1
	subs	w4,w4,1
	b.ne	.b
	sub	x1,x1,1
	b	.l
.f:	mvn	w0,w2
	ret

; w0 = adresse dans la ROM -> w0 = l'octet
rom_byte:
	ldr	x1,[x19,m_rom]
	and	x2,x0,0xFFFFFFFC
	ldr	w2,[x1,x2]
	and	w0,w0,3
	mov	w1,3
	sub	w0,w1,w0
	lsl	w0,w0,3
	lsr	w0,w2,w0
	and	w0,w0,0xFF
	ret

; (m, d, size) -> 1 / 0
n64_load:
	prologue
	save_all
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	movl	x0,0x101000
	cmp	x22,x0
	b.lo	.no
	ldr	w0,[x21]
	rev	w0,w0
	mov	w23,0				; order
	movl	w1,0x80371240
	cmp	w0,w1
	b.eq	.ok
	mov	w23,1
	movl	w1,0x37804012
	cmp	w0,w1
	b.eq	.ok
	mov	w23,2
	movl	w1,0x40123780
	cmp	w0,w1
	b.ne	.no
.ok:	add	x24,x22,3
	and	x24,x24,0xFFFFFFFC		; romSize
	str	w24,[x19,m_romSize]
	mov	x0,x24
	bl	core_alloc
	str	x0,[x19,m_rom]
	mov	x1,0				; copie, puis les mots dans l'ordre de l'hote
.c:	cmp	x1,x22
	b.hs	.cz
	ldrb	w2,[x21,x1]
	strb	w2,[x0,x1]
	add	x1,x1,1
	b	.c
.cz:	cmp	x1,x24
	b.hs	.sw
	strb	wzr,[x0,x1]
	add	x1,x1,1
	b	.cz
.sw:	mov	x1,0
.w:	cmp	x1,x24
	b.hs	.hdr
	ldr	w2,[x0,x1]
	cmp	w23,2
	b.eq	.n
	cbz	w23,.z
	rev16	w2,w2
.z:	rev	w2,w2
.n:	str	w2,[x0,x1]
	add	x1,x1,4
	b	.w
.hdr:	add	x21,x19,m_title
	mov	x22,0
.tt:	add	w0,w22,0x20
	bl	rom_byte
	strb	w0,[x21,x22]
	add	x22,x22,1
	cmp	x22,20
	b.lo	.tt
	strb	wzr,[x21,20]
	mov	x22,19
.tz:	ldrb	w0,[x21,x22]
	cmp	w0,' '
	b.eq	.t0
	cbnz	w0,.t1
.t0:	strb	wzr,[x21,x22]
	subs	x22,x22,1
	b.hs	.tz
.t1:	ldr	x0,[x19,m_rom]
	ldr	w0,[x0,0x3C]
	ubfx	w0,w0,8,8			; region
	adr	x1,pal_codes
.r:	ldrb	w2,[x1],1
	cbz	w2,.rn
	cmp	w0,w2
	b.ne	.r
	mov	w2,1
.rn:	strb	w2,[x19,m_pal]
	sub	sp,sp,0x1000
	mov	x21,0
.ip:	add	w0,w21,0x40
	bl	rom_byte
	strb	w0,[sp,x21]
	add	x21,x21,1
	cmp	x21,0xFC0
	b.lo	.ip
	mov	x0,sp
	mov	x1,0xFC0
	bl	crc32_of
	add	sp,sp,0x1000
	mov	w1,6102
	movl	w2,0x6170A4A1
	cmp	w0,w2
	b.ne	.c1
	mov	w1,6101
.c1:	movl	w2,0x0B050EE0
	cmp	w0,w2
	b.ne	.c2
	mov	w1,6103
.c2:	movl	w2,0x98BC2C86
	cmp	w0,w2
	b.ne	.c3
	mov	w1,6105
.c3:	movl	w2,0xACC8580A
	cmp	w0,w2
	b.ne	.c4
	mov	w1,6106
.c4:	str	w1,[x19,m_cic]
	bl	do_reset
	str	x20,[x19,m_cycles]
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	rest_all
	epilogue
pal_codes:
	db	'DFIPSUXY',0
	align 4

n64_lines_of:				; -> w0 = cycles par ligne, de w1 = viLines
	movl	w0,1562500
	ldrb	w2,[x19,m_pal]
	cbz	w2,.a
	movl	w0,1875000
.a:	udiv	w0,w0,w1
	ret

do_reset:
	prologue
	mov	x20,0
	bl	jit_flush
	mov	x0,x19
	mov	x1,256
	bl	mem_zero
	str	xzr,[x19,m_hi]
	str	xzr,[x19,m_lo]
	add	x0,x19,m_cp0
	mov	x1,512
	bl	mem_zero
	str	wzr,[x19,m_fcr31]
	strb	wzr,[x19,m_llbit]
	add	x0,x19,m_tlb
	mov	x1,512
	bl	mem_zero
	str	wzr,[x19,m_lastTlb]
	str	wzr,[x19,m_tlbMissVector]
	str	wzr,[x19,m_badVa]
	add	x0,x19,M_RDRAM
	mov	x1,N64_RDRAM_SIZE
	bl	mem_zero
	add	x0,x19,M_DMEM
	mov	x1,0x2000
	bl	mem_zero
	add	x0,x19,m_pifRam
	mov	x1,64
	bl	mem_zero
	add	x0,x19,m_mi
	mov	x1,312
	bl	mem_zero
	movl	w0,0x02020102
	str	w0,[x19,m_mi+4]
	mov	w0,1
	str	w0,[x19,m_sp+16]
	str	wzr,[x19,m_spPc]
	mov	w0,0x0E
	str	w0,[x19,m_ri]
	mov	w0,0x40
	str	w0,[x19,m_ri+4]
	mov	w0,0x14
	str	w0,[x19,m_ri+12]
	movl	w0,0x00063634
	str	w0,[x19,m_ri+16]
	mov	w0,0x7F
	str	w0,[x19,m_pi+8]
	str	w0,[x19,m_pi+12]
	str	xzr,[x19,m_countBase]
	str	wzr,[x19,m_frames]
	mov	x0,-1
	mov	x1,0
	add	x2,x19,m_evAt
.ev:	str	x0,[x2,x1,lsl 3]
	add	x1,x1,1
	cmp	x1,EV_N
	b.lo	.ev
	str	x0,[x19,m_nextEvent]
	strb	wzr,[x19,m_excPending]
	strb	wzr,[x19,m_inDelay]
	strb	wzr,[x19,m_nextDelay]
	str	wzr,[x19,m_aiCount]
	add	x0,x19,m_rspTasks
	mov	x1,56
	bl	mem_zero
	str	wzr,[x19,m_lastTask]
	str	wzr,[x19,m_audioTasks]
	str	wzr,[x19,m_audioUnknown]
	str	wzr,[x19,m_resAcc]
	str	wzr,[x19,m_lastL]
	str	wzr,[x19,m_lastR]
	str	wzr,[x19,m_aIn]
	str	wzr,[x19,m_aOut]
	str	wzr,[x19,m_aCount]
	str	wzr,[x19,m_aLoop]
	add	x0,x19,M_ADMEM
	mov	x1,0x1000
	bl	mem_zero
	add	x0,x19,M_ABOOK
	mov	x1,512
	bl	mem_zero
	add	x0,x19,m_aEnvV
	str	xzr,[x0]
	str	wzr,[x0,8]
	add	x0,x19,m_excCount
	mov	x1,128
	bl	mem_zero
	str	wzr,[x19,m_viLineNow]
	ldrb	w2,[x19,m_pal]
	mov	w1,263
	cbz	w2,.l
	mov	w1,313
.l:	str	w1,[x19,m_viLines]
	bl	n64_lines_of
	str	x0,[x19,m_viCyclesLine]
	strb	wzr,[x19,m_frameDone]
	mov	w0,320
	str	w0,[x19,m_fbW]
	mov	w0,240
	str	w0,[x19,m_fbH]
	movl	x0,M_FB
	add	x0,x19,x0
	movl	x1,N64_FBW*N64_FBH*4
	bl	mem_zero
	movl	x0,0x34000000
	str	x0,[x19,m_cp0+12*8]
	movl	x0,0x7006E463
	str	x0,[x19,m_cp0+16*8]
	mov	x0,0xB22
	str	x0,[x19,m_cp0+15*8]
	mov	w0,EV_VI
	ldr	x1,[x19,m_viCyclesLine]
	bl	schedule
	bl	schedule_compare
	bl	gfx_init
	ldr	x0,[x19,m_rom]
	cbz	x0,.f
	bl	boot_hle
.f:	epilogue

; ce que le PIF et l'IPL3 de la cartouche auraient fait
boot_ipl:
	dw	0x3C0DBFC0,0x8DA807FC,0x25AD07C0,0x31080080,0x5500FFFC,0x3C0DBFC0,0x8DA80024,0x3C0BB000

macro rdw off, val {
	movl	w0,val
	movl	x1,off
	add	x2,x19,M_RDRAM
	str	w0,[x2,x1]
}

boot_hle:
	ldr	x4,[x19,m_rom]
	ldr	w0,[x4,8]			; entry
	ldr	w5,[x19,m_cic]
	movl	w1,6103
	cmp	w5,w1
	b.ne	.a
	sub	w0,w0,0x100000
.a:	add	w1,w1,3				; 6106
	cmp	w5,w1
	b.ne	.b
	sub	w0,w0,0x200000
.b:	and	w6,w0,0x1FFFFFFF		; dst
	ldr	w7,[x19,m_romSize]
	add	x8,x19,M_RDRAM
	mov	w1,0				; i
.c:	add	w2,w6,w1
	cmp	w2,N64_RDRAM_SIZE
	b.hs	.cn
	add	w3,w1,0x1000
	cmp	w3,w7
	b.hs	.cn
	ldr	w3,[x4,x3]
	str	w3,[x8,x2]
.cn:	add	w1,w1,4
	cmp	w1,0x100000
	b.lo	.c
	add	x1,x19,M_DMEM
	mov	x2,0
.d:	ldr	w3,[x4,x2]
	str	w3,[x1,x2]
	add	x2,x2,4
	cmp	x2,0x1000
	b.lo	.d
	ldrb	w3,[x19,m_pal]
	eor	w3,w3,1				; tv
	mov	w9,0x3F				; seed
	mov	w10,2
	movl	w1,6103
	cmp	w5,w1
	b.ne	.s1
	mov	w9,0x78
.s1:	add	w1,w1,2
	cmp	w5,w1
	b.ne	.s2
	mov	w9,0x91
	mov	w10,5
.s2:	add	w1,w1,1
	cmp	w5,w1
	b.ne	.s3
	mov	w9,0x85
.s3:	movl	x1,0x300
	str	w3,[x8,x1]
	str	wzr,[x8,0x304]
	movl	w0,0xB0000000
	str	w0,[x8,0x308]
	str	wzr,[x8,0x30C]
	str	w10,[x8,0x310]
	str	wzr,[x8,0x314]
	mov	w0,N64_RDRAM_SIZE
	str	w0,[x8,0x318]
	cmp	w10,5
	b.ne	.s4
	str	w0,[x8,0x3F0]
	adr	x1,boot_ipl
	add	x2,x19,M_IMEM
	mov	x10,0
.s5:	ldr	w0,[x1,x10]
	str	w0,[x2,x10]
	add	x10,x10,4
	cmp	x10,32
	b.lo	.s5
.s4:	movl	w0,0xA4000040
	sxtw	x0,w0
	str	x0,[x19,11*8]
	str	xzr,[x19,19*8]
	str	x3,[x19,20*8]
	str	xzr,[x19,21*8]
	str	x9,[x19,22*8]
	str	xzr,[x19,23*8]
	movl	w0,0xA4001FF0
	sxtw	x0,w0
	str	x0,[x19,29*8]
	movl	w0,0xA4001550
	sxtw	x0,w0
	str	x0,[x19,31*8]
	ldr	w0,[x4]				; hdr
	and	w1,w0,0xFF
	str	w1,[x19,m_pi+20]
	ubfx	w1,w0,8,8
	str	w1,[x19,m_pi+24]
	ubfx	w1,w0,16,4
	str	w1,[x19,m_pi+28]
	ubfx	w1,w0,20,2
	str	w1,[x19,m_pi+32]
	movl	x0,0xB000007C
	str	x0,[x19,m_cp0+13*8]
	movl	x0,0xFFFFFFFF
	str	x0,[x19,m_cp0+17*8]
	add	w0,w6,0x100000
	str	w0,[x19,m_pi]
	movl	w0,0x1000000C
	str	w0,[x19,m_pi+4]
	ldr	w0,[x4,8]
	movl	w1,6103
	cmp	w5,w1
	b.ne	.p1
	sub	w0,w0,0x100000
.p1:	add	w1,w1,3
	cmp	w5,w1
	b.ne	.p2
	sub	w0,w0,0x200000
.p2:	str	w0,[x19,m_pc]
	add	w0,w0,4
	str	w0,[x19,m_npc]
	ret

; w0 = ev, x1 = delta
schedule:
	add	x1,x20,x1
	add	x2,x19,m_evAt
	str	x1,[x2,x0,lsl 3]
	ldr	x2,[x19,m_nextEvent]
	cmp	x1,x2
	b.hs	.r
	str	x1,[x19,m_nextEvent]
.r:	ret

events:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,0				; e
.l:	add	x0,x19,m_evAt
	ldr	x1,[x0,x21,lsl 3]
	cmp	x1,x20
	b.hi	.n
	mov	x1,-1
	str	x1,[x0,x21,lsl 3]
	jtab	x21,.t
.t:	dw	.vi-.t,.cmp-.t,.pi-.t,.si-.t,.ai-.t,.sp-.t,.dp-.t
.vi:	bl	vi_line
	mov	w0,EV_VI
	ldr	x1,[x19,m_viCyclesLine]
	bl	schedule
	b	.n
.cmp:	ldr	x0,[x19,m_cp0+13*8]
	orr	x0,x0,0x8000
	str	x0,[x19,m_cp0+13*8]
	bl	check_interrupts
	bl	schedule_compare
	b	.n
.pi:	ldr	w0,[x19,m_pi+16]
	and	w0,w0,0xFFFFFFFC
	str	w0,[x19,m_pi+16]
	mov	w0,MI_PI
	bl	raise_mi
	b	.n
.si:	ldr	w0,[x19,m_si+24]
	and	w0,w0,0xFFFFFFFE
	orr	w0,w0,0x1000
	str	w0,[x19,m_si+24]
	mov	w0,MI_SI
	bl	raise_mi
	b	.n
.ai:	ldr	w0,[x19,m_aiCount]
	cbz	w0,.a1
	ldr	w1,[x19,m_aiFifo+8]
	str	w1,[x19,m_aiFifo]
	ldr	w1,[x19,m_aiFifo+12]
	str	w1,[x19,m_aiFifo+4]
	sub	w0,w0,1
	str	w0,[x19,m_aiCount]
.a1:	mov	w0,MI_AI
	bl	raise_mi
	ldr	w0,[x19,m_aiCount]
	cbz	w0,.n
	bl	ai_push
	b	.n
.sp:	mov	w0,MI_SP
	bl	raise_mi
	b	.n
.dp:	mov	w0,MI_DP
	bl	raise_mi
.n:	add	x21,x21,1
	cmp	x21,EV_N
	b.lo	.l
	mov	x0,-1
	mov	x1,0
	add	x2,x19,m_evAt
.m:	ldr	x3,[x2,x1,lsl 3]
	cmp	x3,x0
	csel	x0,x3,x0,lo
	add	x1,x1,1
	cmp	x1,EV_N
	b.lo	.m
	str	x0,[x19,m_nextEvent]
	ldp	x21,x22,[sp],16
	epilogue

raise_mi:				; w0 = bits
	mov	x1,0
	add	x2,x19,m_irqCount
.l:	lsr	w3,w0,w1
	tbz	w3,0,.n
	ldr	w3,[x2,x1,lsl 2]
	add	w3,w3,1
	str	w3,[x2,x1,lsl 2]
.n:	add	x1,x1,1
	cmp	x1,6
	b.lo	.l
	ldr	w1,[x19,m_mi+8]
	orr	w1,w1,w0
	str	w1,[x19,m_mi+8]
	b	check_interrupts

lower_mi:				; w0 = bits
	ldr	w1,[x19,m_mi+8]
	bic	w1,w1,w0
	str	w1,[x19,m_mi+8]
	b	check_interrupts

; ---- le VI ----

vi_line:
	ldr	w0,[x19,m_viLineNow]
	add	w0,w0,1
	str	w0,[x19,m_viLineNow]
	ldr	w1,[x19,m_vi+24]
	and	w1,w1,0x3FF			; vsync
	mov	w2,263
	ldrb	w3,[x19,m_pal]
	cbz	w3,.a
	mov	w2,313
.a:	cbz	w1,.b
	add	w2,w1,1
	lsr	w2,w2,1				; lines
.b:	ldr	w1,[x19,m_viLines]
	cmp	w2,w1
	b.eq	.c
	cmp	w2,100
	b.le	.c
	str	w2,[x19,m_viLines]
	mov	w1,w2
	mov	x4,x30
	bl	n64_lines_of
	mov	x30,x4
	str	x0,[x19,m_viCyclesLine]
.c:	ldr	w0,[x19,m_viLineNow]
	ldr	w1,[x19,m_viLines]
	cmp	w0,w1
	b.lt	.d
	str	wzr,[x19,m_viLineNow]
	str	x30,[sp,-16]!
	bl	vi_output
	ldr	x30,[sp],16
	mov	w0,1
	strb	w0,[x19,m_frameDone]
.d:	ldr	w0,[x19,m_viLineNow]
	lsl	w0,w0,1
	ldr	w1,[x19,m_vi+12]
	and	w1,w1,0x3FE
	cmp	w0,w1
	b.ne	.r
	mov	w0,MI_VI
	b	raise_mi
.r:	ret

vi_output:
	ldr	w0,[x19,m_vi]
	and	w0,w0,3				; type
	ldr	w1,[x19,m_vi+4]
	and	w1,w1,0xFFFFFF			; origin
	ldr	w2,[x19,m_vi+8]
	and	w2,w2,0xFFF			; width
	ldr	w3,[x19,m_vi+36]
	ubfx	w4,w3,16,10			; hs
	and	w3,w3,0x3FF			; he
	ldr	w5,[x19,m_vi+40]
	ubfx	w6,w5,16,10			; vs
	and	w5,w5,0x3FF			; ve
	ldr	w7,[x19,m_vi+48]
	and	w7,w7,0xFFF			; xs
	ldr	w8,[x19,m_vi+52]
	and	w8,w8,0xFFF			; ys
	mov	w9,0				; w
	cmp	w3,w4
	b.ls	.a
	sub	w9,w3,w4
	mul	w9,w9,w7
	lsr	w9,w9,10
.a:	mov	w10,0				; h
	cmp	w5,w6
	b.ls	.b
	sub	w10,w5,w6
	lsr	w10,w10,1
	mul	w10,w10,w8
	lsr	w10,w10,10
.b:	add	w10,w10,7
	and	w10,w10,0xFFFFFFF0
	cmp	w0,2
	b.lo	.blank
	cmp	w9,0
	b.le	.blank
	cmp	w10,0
	b.le	.blank
	cbnz	w2,.go
.blank:	ldr	w0,[x19,m_fbW]
	ldr	w1,[x19,m_fbH]
	mul	w0,w0,w1
	movl	x1,M_FB
	add	x1,x19,x1
.z:	cbz	w0,.r
	str	wzr,[x1],4
	sub	w0,w0,1
	b	.z
.r:	ret
.go:	mov	w3,N64_FBW
	cmp	w9,w3
	csel	w9,w3,w9,gt
	mov	w3,N64_FBH
	cmp	w10,w3
	csel	w10,w3,w10,gt
	str	w9,[x19,m_fbW]
	str	w10,[x19,m_fbH]
	movl	x11,M_FB
	add	x11,x19,x11			; sortie
	add	x12,x19,M_RDRAM
	mov	w13,0				; y
.y:	mov	w14,0				; x
	mul	w15,w13,w2			; y * width
.x:	add	w3,w15,w14
	cmp	w0,2
	b.ne	.t32
	add	w3,w1,w3,lsl 1			; a
	add	w4,w3,1
	mov	w5,0
	cmp	w4,N64_RDRAM_SIZE
	b.hs	.p2
	eor	w3,w3,2
	ldrh	w5,[x12,x3]
.p2:	ubfx	w3,w5,11,5			; R
	ubfx	w4,w5,6,5			; G
	ubfx	w5,w5,1,5			; B
	lsl	w6,w3,19
	orr	w6,w6,w3,lsl 14
	and	w6,w6,0xFF0000
	lsl	w7,w4,11
	orr	w7,w7,w4,lsl 6
	and	w7,w7,0xFF00
	orr	w6,w6,w7
	lsl	w7,w5,3
	orr	w7,w7,w5,lsr 2
	and	w7,w7,0xFF
	orr	w6,w6,w7
	b	.st
.t32:	add	w3,w1,w3,lsl 2
	add	w4,w3,3
	mov	w6,0
	cmp	w4,N64_RDRAM_SIZE
	b.hs	.st
	and	w3,w3,0xFFFFFFFC
	ldr	w6,[x12,x3]
	lsr	w6,w6,8
.st:	str	w6,[x11],4
	add	w14,w14,1
	cmp	w14,w9
	b.lt	.x
	add	w13,w13,1
	cmp	w13,w10
	b.lt	.y
	ret

; ---- la cartouche ----

; w0 = pa -> w0
cart_read:
	movl	w17,0x10000000
	sub	w1,w0,w17
	movl	w2,0x0FC00000
	cmp	w1,w2
	b.hs	.s
	ldr	w2,[x19,m_romSize]
	cmp	w1,w2
	b.hs	.op
	ldr	x2,[x19,m_rom]
	and	x1,x1,0xFFFFFFFC
	ldr	w0,[x2,x1]
	ret
.op:	and	w0,w1,0xFFFF
	orr	w0,w0,w1,lsl 16
	ret
.s:	movl	w17,0x08000000
	sub	w1,w0,w17
	cmp	w1,SRAM_SIZE
	b.hs	.z
	add	x2,x19,M_SRAM
	ldrb	w0,[x2,x1]
	add	x2,x2,1
	ldrb	w3,[x2,x1]
	add	x2,x2,1
	ldrb	w4,[x2,x1]
	add	x2,x2,1
	ldrb	w5,[x2,x1]
	lsl	w0,w0,24
	orr	w0,w0,w3,lsl 16
	orr	w0,w0,w4,lsl 8
	orr	w0,w0,w5
	ret
.z:	mov	w0,0
	ret

; w0 = a -> w0 : la demi-parole du PI
pi_half:
	movl	w17,0x10000000
	sub	w1,w0,w17
	movl	w2,0x0FC00000
	cmp	w1,w2
	b.hs	.s
	ldr	w2,[x19,m_romSize]
	cmp	w1,w2
	b.hs	.op
	ldr	x2,[x19,m_rom]
	and	x3,x1,0xFFFFFFFC
	ldr	w0,[x2,x3]
	tbnz	w1,1,.lo
	lsr	w0,w0,16
.lo:	and	w0,w0,0xFFFF
	ret
.op:	and	w0,w1,0xFFFF
	ret
.s:	movl	w17,0x08000000
	sub	w1,w0,w17
	cmp	w1,SRAM_SIZE
	b.hs	.z
	ldr	w2,[x19,m_saveType]
	cbnz	w2,.t
	mov	w2,1
	str	w2,[x19,m_saveType]
.t:	add	x2,x19,M_SRAM
	ldrb	w0,[x2,x1]
	add	x2,x2,1
	ldrb	w3,[x2,x1]
	orr	w0,w3,w0,lsl 8
	ret
.z:	mov	w0,0
	ret

; w0 = len -> w0 : le temps d'un DMA du PI
pi_duration:
	ldr	w1,[x19,m_pi+4]			; cart
	mov	w2,0				; d2
	movl	w17,0x05000000
	sub	w3,w1,w17
	movl	w17,0x1000000
	cmp	w3,w17
	b.lo	.d2
	movl	w17,0x08000000
	sub	w3,w1,w17
	movl	w17,0x8000000
	cmp	w3,w17
	b.hs	.d1
.d2:	mov	w2,1
.d1:	add	x3,x19,m_pi+20
	cbz	w2,.l
	add	x3,x19,m_pi+36
.l:	ldr	w4,[x3]
	and	w4,w4,0xFF			; lat
	ldr	w5,[x3,4]
	and	w5,w5,0xFF			; pwd
	ldr	w6,[x3,8]
	and	w6,w6,15			; pgs
	ldr	w7,[x3,12]
	and	w7,w7,3				; rls
	orr	w0,w0,1
	add	w0,w0,1				; len
	add	w6,w6,2				; shift
	mov	w8,1
	lsl	w8,w8,w6			; pageSize
	sub	w9,w8,1				; pageMask
	add	w10,w1,w0
	sub	w10,w10,2			; last
	lsr	w11,w10,w6
	lsr	w12,w1,w6
	sub	w13,w11,w12
	add	w13,w13,1			; pages
	mov	w14,0				; buffers
	mov	w15,0				; partial
	cmp	w11,w12
	b.ne	.m
	cmp	w0,128
	b.ne	.pa
	mov	w14,1
	b	.c
.pa:	mov	w15,w0
	b	.c
.m:	tst	w1,w9
	b.ne	.m1
	add	w14,w14,1
	b	.m2
.m1:	and	w2,w1,w9
	sub	w2,w8,w2
	add	w15,w15,w2
.m2:	add	w2,w10,2
	tst	w2,w9
	b.ne	.m3
	add	w14,w14,1
	b	.m4
.m3:	and	w2,w10,w9
	add	w2,w2,2
	add	w15,w15,w2
.m4:	cmp	w13,2
	b.ls	.c
	sub	w2,w13,2
	mul	w2,w2,w8
	lsr	w2,w2,7
	add	w14,w14,w2
.c:	add	w2,w4,15
	mul	w2,w2,w13
	add	w3,w5,1
	add	w3,w3,w7
	add	w3,w3,1
	mul	w3,w3,w0
	lsr	w3,w3,1
	add	w2,w2,w3
	mov	w3,28
	mul	w3,w3,w14
	add	w2,w2,w3
	add	w2,w2,w15
	add	w0,w2,w2,lsl 1
	ret

; w0 = toRdram
pi_dma:
	prologue
	save_callee
	sub	sp,sp,144
	add	x21,x19,M_RDRAM
	mov	w26,w0
	cbz	w0,.up
	ldr	w0,[x19,m_pi]
	and	w0,w0,0xFFFFFF
	ldr	w1,[x19,m_pi+12]
	and	w1,w1,0xFFFFFF
	add	w1,w1,16
	bl	jit_wrote
	ldr	w22,[x19,m_pi+12]
	and	w22,w22,0xFFFFFF
	add	w22,w22,1			; length
	mov	w23,128				; maxBlock
	mov	w24,1				; firstBlock
	ldr	w0,[x19,m_pi+12]
	and	w0,w0,0xFFFFFF
	bl	pi_duration
	mov	w25,w0				; dur
.blk:	cmp	w22,0
	b.le	.end
	ldr	w0,[x19,m_pi]
	and	w27,w0,7			; misalign
	and	w1,w0,0x7FF
	mov	w2,0x800
	sub	w28,w2,w1			; distEnd
	sub	w1,w23,w27
	cmp	w1,w28
	csel	w1,w1,w28,lt			; blockLen
	cmp	w22,w1
	csel	w9,w22,w1,lt			; curLen
	mov	w10,0				; i
.rd:	cmp	w10,w9
	b.ge	.wr
	ldr	w0,[x19,m_pi+4]
	stp	x9,x10,[sp,-16]!
	bl	pi_half
	ldp	x9,x10,[sp],16
	add	x1,sp,8
	lsr	w2,w0,8
	strb	w2,[x1,x10]
	add	x1,x1,1
	strb	w0,[x1,x10]
	ldr	w0,[x19,m_pi+4]
	add	w0,w0,2
	str	w0,[x19,m_pi+4]
	sub	w22,w22,2
	add	w10,w10,2
	b	.rd
.wr:	sub	w2,w9,w27			; n
	cbz	w24,.ow
	mov	w0,127
	sub	w0,w0,w27
	cmp	w9,w0
	b.lt	.nw
.ow:	tbz	w2,0,.nw
	add	w2,w2,1
.nw:	mov	w3,0
	ldr	w4,[x19,m_pi]
.w:	cmp	w3,w2
	b.ge	.we
	cmp	w4,N64_RDRAM_SIZE
	b.hs	.ws
	add	x5,sp,8
	ldrb	w5,[x5,x3]
	eor	w6,w4,3
	strb	w5,[x21,x6]
.ws:	add	w4,w4,1
	and	w4,w4,0xFFFFFF
	add	w3,w3,1
	b	.w
.we:	add	w4,w4,7
	and	w4,w4,0xFFFFF8
	str	w4,[x19,m_pi]
	mov	w0,127
	cmp	w9,8
	b.gt	.l127
	sub	w0,w0,w27
.l127:	str	w0,[x19,m_pi+12]
	mov	w24,0
	mov	w23,128
	cmp	w28,8
	b.ge	.blk
	sub	w23,w23,w27
	b	.blk
.end:	ldr	w0,[x19,m_pi+16]
	orr	w0,w0,1
	str	w0,[x19,m_pi+16]
	mov	w0,EV_PI
	mov	x1,x25
	bl	schedule
	b	.out
.up:	ldr	w22,[x19,m_pi+8]
	orr	w22,w22,1
	add	w22,w22,1			; len
	mov	w0,0
	ldr	w1,[x19,m_pi+4]
	ldr	w2,[x19,m_pi]
	add	x3,x19,M_SRAM
.ul:	cmp	w0,w22
	b.hs	.ue
	add	w4,w1,w0
	movl	w17,0x08000000
	sub	w4,w4,w17
	cmp	w4,SRAM_SIZE
	b.hs	.un
	add	w5,w2,w0
	cmp	w5,N64_RDRAM_SIZE
	b.hs	.un
	eor	w5,w5,3
	ldrb	w5,[x21,x5]
	strb	w5,[x3,x4]
	mov	w5,1
	strb	w5,[x19,m_sramDirty]
	str	w5,[x19,m_saveType]
.un:	add	w0,w0,1
	b	.ul
.ue:	ldr	w0,[x19,m_pi+16]
	orr	w0,w0,1
	str	w0,[x19,m_pi+16]
	ldr	w0,[x19,m_pi+8]
	bl	pi_duration
	mov	x1,x0
	mov	w0,EV_PI
	bl	schedule
.out:	add	sp,sp,144
	rest_callee
	epilogue

; ---- le SI, le PIF ----

si_dma:					; w0 = toRdram
	str	x30,[sp,-16]!
	ldr	w1,[x19,m_si]
	and	w1,w1,0xFFFFF8			; dram
	add	x2,x19,M_RDRAM
	add	x3,x19,m_pifRam
	mov	x4,0
.l:	add	w5,w1,w4
	cbz	w0,.up
	cmp	w5,N64_RDRAM_SIZE
	b.hs	.n
	eor	w5,w5,3
	ldrb	w6,[x3,x4]
	strb	w6,[x2,x5]
	b	.n
.up:	mov	w6,0
	cmp	w5,N64_RDRAM_SIZE
	b.hs	.p
	eor	w5,w5,3
	ldrb	w6,[x2,x5]
.p:	strb	w6,[x3,x4]
.n:	add	x4,x4,1
	cmp	x4,64
	b.lo	.l
	cbnz	w0,.w
	bl	pif_process
	b	.d
.w:	mov	w0,w1
	mov	w1,64
	bl	jit_wrote
.d:	ldr	w0,[x19,m_si+24]
	orr	w0,w0,1
	str	w0,[x19,m_si+24]
	mov	w0,EV_SI
	mov	x1,6000
	ldr	x30,[sp],16
	b	schedule

pif_process:
	add	x0,x19,m_pifRam
	ldrb	w1,[x0,63]			; ctl
	tbz	w1,3,.a
	bic	w2,w1,8
	strb	w2,[x0,63]
.a:	tbz	w1,0,.r
	ldrb	w2,[x0,63]
	and	w2,w2,0xFE
	strb	w2,[x0,63]
	mov	w1,0				; i
	mov	w2,0				; ch
.l:	cmp	w1,63
	b.ge	.r
	cmp	w2,5
	b.ge	.r
	ldrb	w3,[x0,x1]			; t
	cbnz	w3,.n0
	add	w2,w2,1
	add	w1,w1,1
	b	.l
.n0:	cmp	w3,0xFD
	b.eq	.sk
	cmp	w3,0xFE
	b.eq	.r
	cmp	w3,0xFF
	b.ne	.tx
.sk:	add	w1,w1,1
	b	.l
.tx:	and	w3,w3,0x3F			; tx
	add	w4,w1,1
	cmp	w4,63
	b.ge	.r
	ldrb	w5,[x0,x4]
	and	w5,w5,0x3F			; rx
	add	x6,x0,x1
	add	x6,x6,2				; cmd
	add	x7,x6,x3			; res
	add	w8,w1,2
	add	w8,w8,w3
	add	w8,w8,w5
	cmp	w8,64
	b.gt	.r
	mov	w9,1				; present
	cmp	w2,4
	b.ge	.cart
	cbz	w2,.c0
	mov	w9,0
	b	.done
.c0:	ldrb	w10,[x6]
	cbz	w10,.id
	cmp	w10,0xFF
	b.eq	.id
	cmp	w10,1
	b.ne	.c2
	ldrh	w11,[x19,m_padBtn]
	lsr	w12,w11,8
	strb	w12,[x7]
	strb	w11,[x7,1]
	ldrb	w11,[x19,m_padX]
	strb	w11,[x7,2]
	ldrb	w11,[x19,m_padY]
	strb	w11,[x7,3]
	b	.done
.id:	mov	w10,5
	strb	w10,[x7]
	strb	wzr,[x7,1]
	mov	w10,2
	strb	w10,[x7,2]
	b	.done
.c2:	cmp	w10,2
	b.ne	.c3
	mov	x11,0
.c2l:	cmp	x11,x5
	b.hs	.done
	strb	wzr,[x7,x11]
	add	x11,x11,1
	b	.c2l
.c3:	cmp	w10,3
	b.ne	.done
	cbz	w5,.done
	strb	wzr,[x7]
	b	.done
.cart:	ldr	w11,[x19,m_eepromSize]
	cbnz	w11,.c5
	mov	w9,0
	b	.done
.c5:	ldrb	w10,[x6]
	cbz	w10,.ei
	cmp	w10,0xFF
	b.ne	.c6
.ei:	strb	wzr,[x7]
	mov	w10,0x80
	mov	w12,0xC0
	cmp	w11,512
	csel	w10,w12,w10,gt
	strb	w10,[x7,1]
	strb	wzr,[x7,2]
	b	.done
.c6:	add	x13,x19,M_EEPROM
	ldrb	w12,[x6,1]
	lsl	w12,w12,3			; b
	cmp	w10,4
	b.ne	.c7
	mov	w14,0
.c6l:	add	w15,w12,w14
	mov	w16,0
	cmp	w15,w11
	b.ge	.c6s
	ldrb	w16,[x13,x15]
.c6s:	strb	w16,[x7,x14]
	add	w14,w14,1
	cmp	w14,8
	b.lo	.c6l
	b	.done
.c7:	cmp	w10,5
	b.ne	.done
	mov	w14,0
.c7l:	add	w15,w12,w14
	cmp	w15,w11
	b.ge	.c7n
	add	x16,x6,x14
	ldrb	w16,[x16,2]
	strb	w16,[x13,x15]
.c7n:	add	w14,w14,1
	cmp	w14,8
	b.lo	.c7l
	mov	w14,1
	strb	w14,[x19,m_eepromDirty]
	cbz	w5,.done
	strb	wzr,[x7]
.done:	cbnz	w9,.pr
	ldrb	w5,[x0,x4]
	orr	w5,w5,0x80
	strb	w5,[x0,x4]
.pr:	add	w1,w1,2
	add	w1,w1,w3
	ldrb	w5,[x0,x4]
	and	w5,w5,0x3F
	add	w1,w1,w5
	add	w2,w2,1
	b	.l
.r:	ret

; ---- l'AI ----

ai_push:
	stp	x21,x22,[sp,-16]!
	str	x30,[sp,-16]!
	ldr	w21,[x19,m_aiFifo+4]		; len
	bl	ai_rate
	mov	w22,w0				; rate
	ldr	w0,[x19,m_aiFifo]
	mov	w1,w21
	mov	w2,w22
	bl	ai_output
	lsr	w0,w21,2			; samples
	movl	x1,N64_CPU_HZ
	mul	x0,x0,x1
	udiv	x1,x0,x22
	add	x1,x1,1
	mov	w0,EV_AI
	ldr	x30,[sp],16
	ldp	x21,x22,[sp],16
	b	schedule

; ---- le SP ----

; w0 = toRdram
sp_dma:
	prologue
	save_callee
	mov	w21,w0
	ldr	w0,[x19,m_sp+8]
	cbz	w21,.v
	ldr	w0,[x19,m_sp+12]
.v:	and	w22,w0,0xFF8
	add	w22,w22,8			; len
	ubfx	w23,w0,12,8
	add	w23,w23,1			; count
	ubfx	w24,w0,20,12
	and	w24,w24,0xFF8			; skip
	ldr	w25,[x19,m_sp]
	and	w25,w25,0x1FF8			; mem
	ldr	w26,[x19,m_sp+4]
	and	w26,w26,0xFFFFF8		; dram
	add	x27,x19,M_DMEM
	tbz	w25,12,.c
	add	x27,x19,M_IMEM
.c:	add	x28,x19,M_RDRAM
.cl:	mov	w0,0				; i
.il:	cmp	w0,w22
	b.hs	.ie
	and	w1,w25,0xFFF
	add	w1,w1,w0
	and	w1,w1,0xFFC			; o
	add	w2,w26,w0			; d
	cmp	w2,N64_RDRAM_SIZE
	b.hs	.ie
	cbz	w21,.tm
	ldr	w3,[x27,x1]
	and	x2,x2,0xFFFFFFFC
	str	w3,[x28,x2]
	b	.in
.tm:	and	x2,x2,0xFFFFFFFC
	ldr	w3,[x28,x2]
	str	w3,[x27,x1]
.in:	add	w0,w0,4
	b	.il
.ie:	cbz	w21,.nw
	mov	w0,w26
	mov	w1,w22
	bl	jit_wrote
.nw:	and	w0,w25,0x1000
	add	w1,w25,w22
	and	w1,w1,0xFFF
	orr	w25,w0,w1
	add	w26,w26,w22
	add	w26,w26,w24
	subs	w23,w23,1
	b.ne	.cl
	str	w25,[x19,m_sp]
	str	w26,[x19,m_sp+4]
	mov	w0,0xFF8
	str	w0,[x19,m_sp+8]
	str	w0,[x19,m_sp+12]
	rest_callee
	epilogue

sp_status_write:			; w0 = v
	prologue
	stp	x21,x22,[sp,-16]!
	mov	w21,w0
	ldr	w22,[x19,m_sp+16]		; s
	and	w8,w22,1			; wasHalted
	tbz	w21,0,.a
	bic	w22,w22,1
.a:	tbz	w21,1,.b
	orr	w22,w22,1
.b:	tbz	w21,2,.c
	bic	w22,w22,2
.c:	str	w22,[x19,m_sp+16]
	stp	x8,xzr,[sp,-16]!
	tbz	w21,3,.d
	mov	w0,MI_SP
	bl	lower_mi
.d:	tbz	w21,4,.e
	mov	w0,MI_SP
	bl	raise_mi
.e:	ldp	x8,x1,[sp],16
	tbz	w21,5,.f
	bic	w22,w22,0x20
.f:	tbz	w21,6,.g
	orr	w22,w22,0x20
.g:	tbz	w21,7,.h
	bic	w22,w22,0x40
.h:	tbz	w21,8,.i
	orr	w22,w22,0x40
.i:	mov	w0,0				; k
.s:	lsl	w1,w0,1
	add	w2,w1,9
	lsr	w2,w21,w2
	mov	w3,0x80
	lsl	w3,w3,w0
	tbz	w2,0,.s1
	bic	w22,w22,w3
.s1:	add	w2,w1,10
	lsr	w2,w21,w2
	tbz	w2,0,.s2
	orr	w22,w22,w3
.s2:	add	w0,w0,1
	cmp	w0,8
	b.lo	.s
	str	w22,[x19,m_sp+16]
	cbz	w8,.r
	tbnz	w22,0,.r
	bl	run_rsp
.r:	ldp	x21,x22,[sp],16
	epilogue

; la tache du RSP, faite tout de suite, a haut niveau
run_rsp:
	prologue
	str	x21,[sp,-16]!
	add	x0,x19,M_DMEM
	ldr	w21,[x0,0xFC0]			; type
	str	w21,[x19,m_lastTask]
	mov	w0,0
	cmp	w21,8
	csel	w0,w21,w0,lo
	add	x1,x19,m_rspTasks
	ldr	w2,[x1,x0,lsl 2]
	add	w2,w2,1
	str	w2,[x1,x0,lsl 2]
	ldr	w0,[x19,m_sp+16]
	mov	w1,0x203
	orr	w0,w0,w1
	str	w0,[x19,m_sp+16]
	tbz	w0,6,.a
	mov	w0,EV_SP
	mov	x1,1000
	bl	schedule
.a:	cmp	w21,1
	b.ne	.b
	bl	gfx_task
	mov	w0,EV_DP
	mov	x1,2000
	bl	schedule
	b	.r
.b:	cmp	w21,2
	b.ne	.r
	bl	audio_task
.r:	ldr	x21,[sp],16
	epilogue

; ---- les registres ----

; une lecture de 8 ou 16 bits : w0 = pa -> w0
read_sub:
	movl	w17,0x05000000
	sub	w1,w0,w17
	movl	w2,0x1AC00000
	cmp	w1,w2
	b.hs	.io
	stp	x21,x30,[sp,-16]!
	and	w21,w0,0xFFFFFFFE
	mov	w0,w21
	bl	pi_half
	mov	w1,w0
	add	w0,w21,2
	mov	w21,w1
	bl	pi_half
	orr	w0,w0,w21,lsl 16
	ldp	x21,x30,[sp],16
	ret
.io:	and	w0,w0,0xFFFFFFFC

; w0 = pa -> w0
read_io:
	and	w0,w0,0xFFFFFFFC
	movl	w1,0x03F00000
	cmp	w0,w1
	b.lo	.z
	mov	w1,0x04000000
	cmp	w0,w1
	b.hs	.a
	ubfx	w1,w0,2,3
	add	x2,x19,m_rdramReg
	ldr	w0,[x2,x1,lsl 2]
	ret
.z:	mov	w0,0
	ret
.a:	movl	w1,0x04040000
	cmp	w0,w1
	b.hs	.b
	add	x1,x19,M_DMEM
	tbz	w0,12,.dm
	add	x1,x19,M_IMEM
.dm:	and	w0,w0,0xFFC
	ldr	w0,[x1,x0]
	ret
.b:	lsr	w1,w0,20
	sub	w2,w1,0x40
	cmp	w2,9
	b.hs	.def
	jtab	x2,.t
.t:	dw	.r0-.t,.r1-.t,.def-.t,.r3-.t,.r4-.t,.r5-.t,.r6-.t,.r7-.t,.r8-.t
.r0:	movl	w1,0x04080000
	cmp	w0,w1
	b.ne	.r01
	ldr	w0,[x19,m_spPc]
	and	w0,w0,0xFFC
	ret
.r01:	ubfx	w1,w0,2,3
	cmp	w1,7
	b.ne	.r02
	ldr	w0,[x19,m_sp+28]
	mov	w1,1
	str	w1,[x19,m_sp+28]
	ret
.r02:	cmp	w1,5
	b.eq	.z
	cmp	w1,6
	b.eq	.z
	add	x2,x19,m_sp
	ldr	w0,[x2,x1,lsl 2]
	ret
.r1:	ubfx	w1,w0,2,3
	add	x2,x19,m_dp
	ldr	w0,[x2,x1,lsl 2]
	ret
.r3:	ubfx	w1,w0,2,2
	add	x2,x19,m_mi
	ldr	w0,[x2,x1,lsl 2]
	ret
.r4:	ubfx	w1,w0,2,4
	cmp	w1,4
	b.ne	.r41
	ldr	w0,[x19,m_viLineNow]
	lsl	w0,w0,1
	ldr	w1,[x19,m_vi]
	tbz	w1,6,.r40
	ldr	w1,[x19,m_frames]
	and	w1,w1,1
	add	w0,w0,w1
.r40:	ret
.r41:	cmp	w1,14
	b.hs	.z
	add	x2,x19,m_vi
	ldr	w0,[x2,x1,lsl 2]
	ret
.r5:	ubfx	w1,w0,2,3
	cmp	w1,3
	b.ne	.r51
	ldr	w1,[x19,m_aiCount]
	movl	w0,0x01100000
	cmp	w1,2
	b.lt	.r50
	movl	w2,0x80000001
	orr	w0,w0,w2
.r50:	cmp	w1,1
	b.lt	.r5e
	orr	w0,w0,0x40000000
.r5e:	ret
.r51:	cmp	w1,1
	b.ne	.r52
	ldr	w1,[x19,m_aiCount]
	cbz	w1,.z
	ldr	w0,[x19,m_aiFifo+4]
	ret
.r52:	cmp	w1,6
	b.hs	.z
	add	x2,x19,m_ai
	ldr	w0,[x2,x1,lsl 2]
	ret
.r6:	ubfx	w1,w0,2,4
	cmp	w1,4
	b.ne	.r61
	ldr	w0,[x19,m_pi+16]
	and	w0,w0,7
	ldr	w1,[x19,m_mi+8]
	tbz	w1,4,.r60
	orr	w0,w0,8
.r60:	ret
.r61:	cmp	w1,13
	b.hs	.z
	add	x2,x19,m_pi
	ldr	w0,[x2,x1,lsl 2]
	ret
.r7:	ubfx	w1,w0,2,3
	add	x2,x19,m_ri
	ldr	w0,[x2,x1,lsl 2]
	ret
.r8:	ubfx	w1,w0,2,3
	cmp	w1,6
	b.ne	.r81
	ldr	w0,[x19,m_si+24]
	and	w0,w0,1
	ldr	w1,[x19,m_mi+8]
	tbz	w1,1,.r80
	orr	w0,w0,0x1000
.r80:	ret
.r81:	cmp	w1,7
	b.hs	.z
	add	x2,x19,m_si
	ldr	w0,[x2,x1,lsl 2]
	ret
.def:	movl	w1,0x1FC007C0
	sub	w2,w0,w1
	cmp	w2,0x40
	b.hs	cart_read
	add	x1,x19,m_pifRam
	ldr	w0,[x1,x2]
	rev	w0,w0
	ret

; w0 = pa, w1 = v, w2 = masque
write_io:
	and	w0,w0,0xFFFFFFFC
	movl	w3,0x03F00000
	cmp	w0,w3
	b.lo	.ret
	mov	w3,0x04000000
	cmp	w0,w3
	b.hs	.a
	ubfx	w3,w0,2,3
	add	x4,x19,m_rdramReg
	str	w1,[x4,x3,lsl 2]
.ret:	ret
.a:	movl	w3,0x04040000
	cmp	w0,w3
	b.hs	.b
	add	x3,x19,M_DMEM
	tbz	w0,12,.dm
	add	x3,x19,M_IMEM
.dm:	and	w0,w0,0xFFC
	ldr	w4,[x3,x0]
	bic	w4,w4,w2
	and	w5,w1,w2
	orr	w4,w4,w5
	str	w4,[x3,x0]
	ret
.b:	lsr	w3,w0,20
	sub	w4,w3,0x40
	cmp	w4,9
	b.hs	.def
	jtab	x4,.t
.t:	dw	.w0-.t,.w1-.t,.def-.t,.w3-.t,.w4-.t,.w5-.t,.w6-.t,.w7-.t,.w8-.t
.w0:	movl	w3,0x04080000
	cmp	w0,w3
	b.ne	.w01
	and	w1,w1,0xFFC
	str	w1,[x19,m_spPc]
	ret
.w01:	ubfx	w3,w0,2,3
	cbnz	w3,.w02
	and	w1,w1,0x1FF8
	str	w1,[x19,m_sp]
	ret
.w02:	cmp	w3,1
	b.ne	.w03
	and	w1,w1,0xFFFFF8
	str	w1,[x19,m_sp+4]
	ret
.w03:	cmp	w3,2
	b.ne	.w04
	str	w1,[x19,m_sp+8]
	mov	w0,0
	b	sp_dma
.w04:	cmp	w3,3
	b.ne	.w05
	str	w1,[x19,m_sp+12]
	mov	w0,1
	b	sp_dma
.w05:	cmp	w3,4
	b.ne	.w06
	mov	w0,w1
	b	sp_status_write
.w06:	cmp	w3,7
	b.ne	.ret
	str	wzr,[x19,m_sp+28]
	ret
.w1:	ubfx	w3,w0,2,3
	cmp	w3,3
	b.ne	.w11
	ldr	w4,[x19,m_dp+12]
	tbz	w1,0,.d1
	bic	w4,w4,1
.d1:	tbz	w1,1,.d2
	orr	w4,w4,1
.d2:	tbz	w1,2,.d3
	bic	w4,w4,2
.d3:	tbz	w1,3,.d4
	orr	w4,w4,2
.d4:	tbz	w1,4,.d5
	bic	w4,w4,4
.d5:	tbz	w1,5,.d6
	orr	w4,w4,4
.d6:	str	w4,[x19,m_dp+12]
	ret
.w11:	cbnz	w3,.w12
	and	w1,w1,0xFFFFF8
	str	w1,[x19,m_dp]
	str	w1,[x19,m_dp+8]
	ret
.w12:	cmp	w3,1
	b.ne	.ret
	and	w1,w1,0xFFFFF8
	str	w1,[x19,m_dp+4]
	str	w1,[x19,m_dp+8]
	mov	w0,MI_DP
	b	raise_mi
.w3:	ubfx	w3,w0,2,2
	cbnz	w3,.w31
	ldr	w4,[x19,m_mi]
	bfxil	w4,w1,0,7
	tbz	w1,7,.m1
	bic	w4,w4,0x80
.m1:	tbz	w1,8,.m2
	orr	w4,w4,0x80
.m2:	tbz	w1,9,.m3
	bic	w4,w4,0x100
.m3:	tbz	w1,10,.m4
	orr	w4,w4,0x100
.m4:	tbz	w1,12,.m5
	bic	w4,w4,0x200
.m5:	tbz	w1,13,.m6
	orr	w4,w4,0x200
.m6:	str	w4,[x19,m_mi]
	tbz	w1,11,.ret
	mov	w0,MI_DP
	b	lower_mi
.w31:	cmp	w3,3
	b.ne	.ret
	ldr	w4,[x19,m_mi+12]
	mov	w5,0				; k
.mk:	lsl	w6,w5,1
	lsr	w6,w1,w6
	mov	w7,1
	lsl	w7,w7,w5
	tbz	w6,0,.mk1
	bic	w4,w4,w7
.mk1:	tbz	w6,1,.mk2
	orr	w4,w4,w7
.mk2:	add	w5,w5,1
	cmp	w5,6
	b.lo	.mk
	str	w4,[x19,m_mi+12]
	b	check_interrupts
.w4:	ubfx	w3,w0,2,4
	cmp	w3,4
	b.ne	.w41
	mov	w0,MI_VI
	b	lower_mi
.w41:	cmp	w3,14
	b.hs	.ret
	add	x4,x19,m_vi
	str	w1,[x4,x3,lsl 2]
	ret
.w5:	ubfx	w3,w0,2,3
	cbnz	w3,.w51
	and	w1,w1,0xFFFFF8
	str	w1,[x19,m_ai]
	ret
.w51:	cmp	w3,1
	b.ne	.w52
	and	w1,w1,0x3FFF8			; len
	cbz	w1,.ret
	ldr	w4,[x19,m_aiCount]
	cmp	w4,2
	b.ge	.ret
	add	x5,x19,m_aiFifo
	add	x5,x5,x4,lsl 3
	ldr	w6,[x19,m_ai]
	str	w6,[x5]
	str	w1,[x5,4]
	add	w4,w4,1
	str	w4,[x19,m_aiCount]
	cmp	w4,1
	b.eq	ai_push
	ret
.w52:	cmp	w3,3
	b.ne	.w53
	mov	w0,MI_AI
	b	lower_mi
.w53:	cmp	w3,6
	b.hs	.ret
	add	x4,x19,m_ai
	str	w1,[x4,x3,lsl 2]
	ret
.w6:	ubfx	w3,w0,2,4
	cmp	w3,4
	b.ne	.w61
	mov	w4,w1
	tbz	w4,0,.p1
	ldr	w5,[x19,m_pi+16]
	bic	w5,w5,4
	str	w5,[x19,m_pi+16]
.p1:	tbz	w4,1,.ret
	mov	w0,MI_PI
	b	lower_mi
.w61:	ldr	w4,[x19,m_pi+16]
	tst	w4,3
	b.eq	.w62
	orr	w4,w4,4
	str	w4,[x19,m_pi+16]
	ret
.w62:	cbnz	w3,.w63
	and	w1,w1,0xFFFFFE
	str	w1,[x19,m_pi]
	ret
.w63:	cmp	w3,1
	b.ne	.w64
	and	w1,w1,0xFFFFFFFE
	str	w1,[x19,m_pi+4]
	ret
.w64:	cmp	w3,2
	b.ne	.w65
	and	w1,w1,0xFFFFFF
	str	w1,[x19,m_pi+8]
	mov	w0,0
	b	pi_dma
.w65:	cmp	w3,3
	b.ne	.w66
	and	w1,w1,0xFFFFFF
	str	w1,[x19,m_pi+12]
	mov	w0,1
	b	pi_dma
.w66:	cmp	w3,13
	b.hs	.ret
	and	w1,w1,0xFF
	add	x4,x19,m_pi
	str	w1,[x4,x3,lsl 2]
	ret
.w7:	ubfx	w3,w0,2,3
	add	x4,x19,m_ri
	str	w1,[x4,x3,lsl 2]
	ret
.w8:	ubfx	w3,w0,2,3
	cbnz	w3,.w81
	and	w1,w1,0xFFFFF8
	str	w1,[x19,m_si]
	ret
.w81:	cmp	w3,1
	b.ne	.w82
	mov	w0,1
	b	si_dma
.w82:	cmp	w3,4
	b.ne	.w83
	mov	w0,0
	b	si_dma
.w83:	cmp	w3,6
	b.ne	.ret
	mov	w0,MI_SI
	b	lower_mi
.def:	movl	w3,0x1FC007C0
	sub	w3,w0,w3
	cmp	w3,0x40
	b.hs	.sr
	add	x4,x19,m_pifRam
	mov	w5,0
.pf:	lsl	w6,w5,3
	mov	w7,0xFF000000
	lsr	w7,w7,w6
	tst	w2,w7
	b.eq	.pfn
	mov	w7,24
	sub	w7,w7,w6
	lsr	w7,w1,w7
	add	w6,w3,w5
	strb	w7,[x4,x6]
.pfn:	add	w5,w5,1
	cmp	w5,4
	b.lo	.pf
	cmp	w3,0x3C
	b.eq	pif_process
	ret
.sr:	movl	w17,0x08000000
	sub	w3,w0,w17
	cmp	w3,SRAM_SIZE
	b.hs	.ret
	add	x4,x19,M_SRAM
	mov	w5,0
.sf:	lsl	w6,w5,3
	mov	w7,0xFF000000
	lsr	w7,w7,w6
	tst	w2,w7
	b.eq	.sfn
	mov	w7,24
	sub	w7,w7,w6
	lsr	w7,w1,w7
	add	w6,w3,w5
	strb	w7,[x4,x6]
.sfn:	add	w5,w5,1
	cmp	w5,4
	b.lo	.sf
	mov	w5,1
	strb	w5,[x19,m_sramDirty]
	str	w5,[x19,m_saveType]
	ret

include 'n64_cpu.inc'
include 'n64_audio.inc'
include 'n64_gfx.inc'
include 'n64_jit.inc'
include 'n64_soft.inc'
