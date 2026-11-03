#!/bin/sh
# Le controle de check.sh controle-t-il ? Une erreur volontaire dans chaque routine A64 doit changer les sommes.
# Dans WSL, depuis la racine du depot, apres mk_harness.sh ; ni le depot ni asm_quake ne sont touches.
#   sh tools/quake/mutate.sh [dossier des donnees]
W=build/quake
D=${1:-$W/data}
T=$W/mut
rm -rf $T
mkdir -p $T
qemu-aarch64 $W/ref_quake "$D" 2000 320 200 'volume 8; map lq_e0m1; +attack; +right; +forward' 2>/dev/null > $T/ref1.txt
qemu-aarch64 $W/ref_quake "$D" 3000 320 200 playdemo demo2 2>/dev/null > $T/ref2.txt
LIBS=$(ls $W/asm/*.o)
mut () { # nom, fichier, motif sed
	cp src/apps/quake/asm/* $T/
	sed -i "$3" "$T/$2"
	if cmp -s "$T/$2" "src/apps/quake/asm/$2"; then echo "?    $1 : motif sans effet"; return; fi
	(cd $T && ../../../tools/fasmarm/fasmarm.exe quake_a64.asm a.o >/dev/null)
	python3 -I tools/quake/fixreloc.py $T/a.o > /dev/null
	aarch64-linux-gnu-gcc -static $LIBS $T/a.o -lm -o $T/q
	qemu-aarch64 $T/q "$D" 2000 320 200 'volume 8; map lq_e0m1; +attack; +right; +forward' 2>/dev/null > $T/m1.txt
	qemu-aarch64 $T/q "$D" 3000 320 200 playdemo demo2 2>/dev/null > $T/m2.txt
	if cmp -s $T/ref1.txt $T/m1.txt && cmp -s $T/ref2.txt $T/m2.txt; then echo "RATE $1"; else echo "vu   $1 (image $(cmp $T/ref1.txt $T/m1.txt | sed 's/.*line //'), demo $(cmp $T/ref2.txt $T/m2.txt | sed 's/.*line //'))"; fi
}
mut D_DrawSpans8 span.inc 's/asr\tw16,w16,3/asr\tw16,w16,2/'
mut D_DrawSpans8.last span.inc 's/sdiv\tw17,w17,w10/asr\tw17,w17,1/'
mut D_DrawZSpans span.inc 's/shl\tv25.4s,v25.4s,3/shl\tv25.4s,v25.4s,2/'
mut D_DrawZSpans.tail span.inc 's/lsr\tw9,w7,16/lsr\tw9,w7,15/'
mut D_DrawTurbulent8Span span.inc 's/ubfx\tw9,w9,16,6/ubfx\tw9,w9,16,5/'
mut R_DrawSurfaceBlock8 surf.inc 's/lsr\tw13,w13,sh/asr\tw13,w13,sh+1/'
mut D_PolysetDrawSpans8 poly.inc 's/asr\tw29,w14,16/asr\tw29,w14,15/'
mut D_DrawParticle poly.inc 's/strb\tw10,\[x5,w13,sxtw\]/strb\twzr,[x5,w13,sxtw]/'
mut Paint8 mix.inc '0,/lsl\tw3,w3,3/s//lsl\tw3,w3,2/'
mut Paint16 mix.inc '/^SND_PaintChannelFrom16/,/^Snd_Write/s/sshr\tv1.4s,v1.4s,8/sshr\tv1.4s,v1.4s,7/'
mut Blast mix.inc 's/sqxtn\tv0.4h,v0.4s/xtn\tv0.4h,v0.4s/'
mut R_InsertNewEdges edge.inc '0,/cmp\tw4,w3/s//cmp\tw3,w4/'
mut R_StepActiveU edge.inc 's/cmp\tx0,x10/cmp\tx0,x9/'
mut R_GenerateSpans.iu edge.inc '0,/asr	w4,w4,20/s//asr	w4,w4,19/'
mut R_GenerateSpans.top edge.inc 's/b.lt	.newtop/b.le	.newtop/'
mut R_GenerateSpans.zsort edge.inc 's/dd 0.99/dd 0.5/'
mut Alias.xy edge.inc 's/fadd	v4.2s,v4.2s,v20.2s/fsub	v4.2s,v4.2s,v20.2s/'
mut Alias.light edge.inc 's/fmul	s0,s24,s0/fadd	s0,s24,s0/'
