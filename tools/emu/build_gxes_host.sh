#!/bin/sh
# Construit build/emu/gxes_host (tools/emu/gxes_host.cpp) : natif x86-64 dans WSL, llvmpipe du systeme, les fichiers
# d'Onyx lus sur place (gxgl.cpp, gc_jit_x64.cpp). Depuis la racine du depot :  sh tools/emu/build_gxes_host.sh
W=build/emu
O=$W/gxhost
mkdir -p $O/inc/GL
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx}
MESA=$HOME/armpi-mesa/src/mesa-26.2.0/include
S=src/apps/gcemu
# gxgl.cpp demande Windows : le meme code, _WIN32 remplace ; gl.h sans glext.h, ses pointeurs sous leurs noms
{
	echo '#define glActiveTexture glActiveTexture_h'
	echo '#define glBlendEquation glBlendEquation_h'
	echo '#define GL_GLEXT_LEGACY'
	echo '#include_next <GL/gl.h>'
	echo '#undef glActiveTexture'
	echo '#undef glBlendEquation'
} > $O/inc/GL/gl.h
sed 's/#ifdef _WIN32/#ifdef __linux__/; s/APIENTRY \*T_/*T_/' $ONYX/pc/NintendoEMU/core/gxgl.cpp > $O/gxgl.cpp
cp tools/emu/glshim/windows.h $O/inc/
CF="-O2 -w -std=gnu++17 -Dprivate=public -I$S -I$O/inc -I$MESA"
for f in $S/gc/*.cpp $S/gxes_rec.cpp $S/gxes_gl.cpp tools/emu/gxes_host.cpp $O/gxgl.cpp $ONYX/user/Emulators/gc/gc_jit_x64.cpp; do
	o=$O/$(basename $f .cpp).o
	[ $o -nt $f ] && [ $o -nt $S/gc/gc.h ] && [ $o -nt $S/gxes.h ] && continue
	g++ $CF -c $f -o $o &
done
wait
L=/usr/lib/x86_64-linux-gnu
ls $L/libGL.so.1 $L/libEGL.so.1 > /dev/null || exit 1
g++ $O/*.o $L/libEGL.so.1 $L/libGL.so.1 -lm -o $W/gxes_host && ls -l $W/gxes_host
