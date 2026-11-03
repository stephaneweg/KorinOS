; ============================================================================
;  quake_a64.asm - les boucles de Quake en assembleur A64 (docs/QUAKE.md)
;
;  Le meme travail, au pixel pres, que le C d'id (third_party/quake), que ces routines remplacent quand
;  ARMPI_ASM vaut 1 ; tools/quake/check.sh le compare image par image. Pour rester identique, tout flottant
;  suit l'ordre du C, en fmul puis fadd, sans fmadd (le C est construit en -ffp-contract=off).
;
;  Sous GPL version 2, comme le moteur dont ces routines suivent le C : third_party/quake/gnu.txt.
;  FASMARM ne sait pas reloger vers une autre unite que par un mot de 64 bits : les variables du C sont lues
;  par un mot d'adresse (pool), dont tools/quake/fixreloc.py corrige le type de relocation.
; ============================================================================
format ELF64
processor cpu64_v8 +cpu64_fp +cpu64_simd
code64

; pool sym, ... : les mots d'adresse des variables du C
macro pool [sym] {
	forward
	extrn sym
	p_#sym dd sym
}
; la adresse d'une variable du C ; lw, lx : sa valeur
macro la reg, sym {
	adr	reg,p_#sym
	ldr	reg,[reg]
}
macro lw reg, xtmp, sym {
	adr	xtmp,p_#sym
	ldr	xtmp,[xtmp]
	ldr	reg,[xtmp]
}
macro movi32 reg, val {
	movz	reg,(val) and 0xFFFF
	movk	reg,((val) shr 16) and 0xFFFF,lsl 16
}
; d = clamp comme le C : if (d > hi) d = hi; else if (d < lo) d = lo;  (hi l'emporte, meme sous lo)
macro clamp d, lo, hi, t {
	cmp	d,lo
	csel	t,lo,d,lt
	cmp	d,hi
	csel	d,hi,t,gt
}

section '.text' executable align 16

include 'span.inc'
include 'surf.inc'
include 'poly.inc'
include 'mix.inc'
include 'edge.inc'

	align 8
pool cacheblock, d_viewbuffer, screenwidth, cachewidth, sadjust, tadjust, bbextents, bbextentt
pool d_sdivzstepu, d_tdivzstepu, d_zistepu, d_sdivzstepv, d_tdivzstepv, d_zistepv
pool d_sdivzorigin, d_tdivzorigin, d_ziorigin
pool d_pzbuffer, d_zwidth
pool r_turb_pbase, r_turb_pdest, r_turb_s, r_turb_t, r_turb_sstep, r_turb_tstep, r_turb_turb, r_turb_spancount
pool pbasesource, prowdestbase, r_lightptr, r_lightwidth, r_numvblocks, sourcetstep, surfrowbytes, r_sourcemax, r_stepback, vid
pool d_aspancount, errorterm, erroradjustup, erroradjustdown, d_countextrastep, ubasestep
pool r_zistepx, r_lstepx, a_ststepxwhole, a_sstepxfrac, a_tstepxfrac, acolormap, r_affinetridesc
pool r_origin, r_pright, r_pup, r_ppn, xcenter, ycenter, d_vrectbottom_particle, d_vrectright_particle, d_vrecty, d_vrectx
pool d_scantable, d_pix_shift, d_pix_min, d_pix_max, d_y_aspect_shift
pool snd_p, snd_out, snd_vol, snd_linear_count, paintbuffer
pool edge_aftertail, edge_tail, edge_head, edge_head_u_shift20, surfaces, span_p, current_iv, fv, r_bmodelactive, R_CleanupSpan
pool r_apverts, r_anumverts, aliastransform, aliasxcenter, aliasycenter, r_plightvec, r_shadelight, r_ambientlight, r_avertexnormals
