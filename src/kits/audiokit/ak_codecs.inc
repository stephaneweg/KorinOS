; ============================================================================
;  kits/audiokit/ak_codecs.inc - les formats audio d'AudioKit (docs/SON.md 9)
;
;  Code sans appel systeme, commun au kit (audiokit.asm) et au banc d'essai
;  du PC (tools/audiotest/akhost.asm, QEMU virt + semihosting). L'hote
;  fournit :
;	ak_alloc	x0 = octets -> x0 = bloc a zero (0 : echec)
;	ak_free		x0 = bloc
;  Ecrit d'apres les normes (WAV ; FLAC : RFC 9639 ; MP3 : ISO/IEC 11172-3
;  et 13818-3 ; Ogg : RFC 3533 ; Vorbis I ; Opus : RFC 6716 et 7845).
; ============================================================================

include 'audiokit.inc'

; trace de mise au point (banc du PC seulement : rien dans le kit)
macro akdbg msg*, reg* {
 if defined AKHOST
  local ..s, ..n
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
	mov	x1,reg
	mrs	x2,nzcv
	str	x2,[sp,152]
	adr	x0,..s
	bl	h_dbg
	ldr	x2,[sp,152]
	msr	nzcv,x2
	ldr	x30,[sp,144]
	ldp	x16,x17,[sp,128]
	ldp	x14,x15,[sp,112]
	ldp	x12,x13,[sp,96]
	ldp	x10,x11,[sp,80]
	ldp	x8,x9,[sp,64]
	ldp	x6,x7,[sp,48]
	ldp	x4,x5,[sp,32]
	ldp	x2,x3,[sp,16]
	ldp	x0,x1,[sp]
	add	sp,sp,176
	b	..n
..s	db msg,0
	align	4
..n:
 end if
}
include 'ak_dec.inc'		; decodeur generique : source, detection, lecture, etiquettes
include 'ak_bits.inc'		; lecteurs de bits
include 'ak_mdct.inc'		; FFT, IMDCT
include 'ak_wav.inc'		; WAV
include 'ak_flac.inc'		; FLAC
include 'ak_mp3.inc'		; MP3
include 'ak_ogg.inc'		; Ogg (Vorbis, Opus)
include 'ak_vorbis.inc'		; Vorbis
include 'ak_opus.inc'		; Opus : couche Opus, Ogg Opus
include 'ak_celt.inc'		; Opus : CELT
include 'ak_silk.inc'		; Opus : SILK
include 'ak_enc.inc'		; encodeur generique, WAV
include 'ak_mp3enc.inc'		; encodeur MP3
include 'ak_opusenc.inc'		; encodeur Opus
include 'ak_fm.inc'		; synthese FM
include 'ak_synth.inc'		; synthe MIDI : banque FM, SoundFont 2
include 'ak_midi.inc'		; lecture .mid, .fms
if defined AKHOST
ak_sf_default:
	mov	x0,0
	ret
end if
