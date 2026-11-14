#!/bin/sh
# build-iris : les bibliotheques de Mesa avec iris (et softpipe, le repli processeur) pour Korin PC (configure-iris.sh), puis libarmgl.a du PC
# (armgl.c avec ARMGL_IRIS, le faux libdrm de tools/mesa/libdrm-pc, client du service src/svc/intelgpu) et gltest
# relie en statique, avec UIKit (gltest --win) :
#   ~/armpi-mesa/out-x86_64/armgl/gltest      (gltest --dry : Mesa et le compilateur d'iris sans GPU)
#   build/pc/mesa/sdroot/bin/gltest           (sans symboles ; tools/pc/sysroot.sh le met dans SD0:/bin du PC)
# Avant : sh tools/pc/apps.sh (ou une de ses apps : build/pc/gcc/lib/libuikit.a et ses en-tetes)
#     sh tools/mesa/build-iris.sh
set -eu
. "$(dirname "$0")/env.sh"
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
POSIX=$ARMPI_ROOT/build/pc/posix
OUT=$A/out-x86_64
B=$OUT/build
G=$OUT/armgl
M=$ARMPI_ROOT/tools/mesa
LIBS_REL="src/mesa/libmesa.a src/mesa/libmesa_sse41.a src/gallium/auxiliary/libgallium.a
src/gallium/auxiliary/libgalliumvl_stub.a src/compiler/libcompiler.a src/compiler/nir/libnir.a
src/compiler/glsl/libglsl.a src/compiler/glsl/libglsl_util.a src/compiler/glsl/glcpp/libglcpp.a
src/compiler/spirv/libvtn.a src/compiler/isaspec/libisaspec.a
src/util/libmesa_util.a src/util/libmesa_util_simd.a src/util/libmesa_util_clflush.a
src/util/libmesa_util_clflushopt.a src/util/libxmlconfig.a src/util/libparson.a src/util/blake3/libblake3.a
src/c11/impl/libmesa_util_c11.a
src/gallium/drivers/softpipe/libsoftpipe.a src/gallium/drivers/iris/libiris.a src/gallium/drivers/iris/libiris_per_hw_ver90.a
src/gallium/drivers/iris/libiris_per_hw_ver110.a src/gallium/drivers/iris/libiris_per_hw_ver120.a
src/gallium/drivers/iris/libiris_per_hw_ver125.a src/gallium/drivers/iris/libiris_per_hw_ver200.a
src/gallium/drivers/iris/libiris_per_hw_ver300.a src/gallium/drivers/iris/libiris_per_hw_ver350.a
src/intel/blorp/libblorp.a src/intel/common/libintel_common.a src/intel/compiler/brw/libintel_compiler.a
src/intel/compiler/gen/libintel_compiler_gen.a src/intel/compiler/jay/libintel_compiler_jay.a
src/intel/compiler/libintel_compiler_nir.a src/intel/decoder/libintel_decoder.a
src/intel/decoder/libintel_decoder_gen.a src/intel/dev/libintel_dev.a src/intel/ds/libintel-driver-ds.a
src/intel/isl/libisl.a src/intel/isl/libisl_per_hw_ver40.a src/intel/isl/libisl_per_hw_ver50.a
src/intel/isl/libisl_per_hw_ver60.a src/intel/isl/libisl_per_hw_ver70.a src/intel/isl/libisl_per_hw_ver75.a
src/intel/isl/libisl_per_hw_ver80.a src/intel/isl/libisl_per_hw_ver90.a src/intel/isl/libisl_per_hw_ver110.a
src/intel/isl/libisl_per_hw_ver120.a src/intel/isl/libisl_per_hw_ver125.a src/intel/isl/libisl_per_hw_ver200.a
src/intel/isl/libisl_per_hw_ver300.a src/intel/isl/libisl_per_hw_ver350.a src/intel/isl/libisl_tiled_memcpy.a
src/intel/isl/libisl_tiled_memcpy_sse41.a src/intel/mda/libmda.a src/intel/perf/libintel_perf.a
src/intel/shaders/libintel_shaders.a
src/mesa/glapi/shared-glapi/libglapi.a src/mesa/glapi/glapi/libglapi_bridge.a"
cd "$B"
# (un dossier configure avant softpipe, 13/11 : le pilote ajoute sans tout refaire)
grep -q 'libsoftpipe\.a' build.ninja || meson configure -Dgallium-drivers=iris,softpipe .
PATH=$A/clc/bin:$PATH ninja -j "$JOBS" $LIBS_REL

# armgl du PC : les memes options que Mesa, celles d'iris_screen.c
mkdir -p "$G"
CF=$(python3 - "$B/compile_commands.json" <<'PY'
import json, shlex, sys
for e in json.load(open(sys.argv[1])):
    if e['file'].endswith('iris_screen.c'):
        a = shlex.split(e['command'])[1:]
        o = []
        skip = False
        for x in a:
            if skip:
                skip = False
            elif x in ('-o', '-MF', '-MQ', '-c'):
                skip = x != '-c'
            elif x != '-MD' and not x.endswith('iris_screen.c') and not x.startswith('-W') and ' ' not in x and '"' not in x:
                o.append(x)
        print(' '.join(o))
        break
PY
)
INC="-I$M/armgl -I$M/libdrm-pc -I$SRC/src/mesa -I$SRC/src/mesa/glapi -I$SRC/src/gallium/drivers -I$B/src/mesa
-I$B/src/compiler -I$B/src/compiler/nir -I$B/src/gallium/drivers/iris -I$M/libdrm -I$ARMPI_ROOT/src/svc/intelgpu"
PCG=$ARMPI_ROOT/build/pc/gcc
[ -f "$PCG/lib/libuikit.a" ] || { echo "manque $PCG/lib/libuikit.a : sh tools/pc/apps.sh calculator (les kits du PC)"; exit 1; }
${TC}gcc $INC $CF -c "$M/libdrm-pc/korin_i915.c" -o "$G/korin_i915.o"
${TC}gcc $INC $CF -DARMGL_IRIS -fvisibility=default -c "$M/armgl/armgl.c" -o "$G/armgl.o"
${TC}gcc $INC $CF -DARMGL_IRIS -fvisibility=default -c "$M/armgl/armegl.c" -o "$G/armegl.o"
rm -f "$G/libarmgl.a"
${TC}ar rcs "$G/libarmgl.a" "$G/korin_i915.o" "$G/armgl.o" "$G/armegl.o"
${TC}gcc -specs=$POSIX/lib/posix.specs -O2 -DKORIN_PC -I"$M/armgl" -I"$PCG/include/kits" 	-I"$ARMPI_ROOT/src/kits/uikit/include" -c "$M/armgl/gltest.c" -o "$G/gltest.o"
LIBS=""
for a in $LIBS_REL; do LIBS="$LIBS $B/$a"; done
echo "$LIBS" > "$G/libs.txt"
if ! ${TC}g++ -specs=$POSIX/lib/posix.specs -Wl,--gc-sections "$G/gltest.o" -Wl,--start-group "$G/libarmgl.a" $LIBS \
	-Wl,--end-group "$PCG/lib/libuikit.a" -o "$G/gltest" 2> "$G/link.log"; then
	grep -E "undefined reference" "$G/link.log" | sed 's/.*undefined reference to//' | sort | uniq -c | sort -rn | head -60
	tail -3 "$G/link.log"
	exit 1
fi
${TC}strip -o "$G/gltest.s" "$G/gltest"
mkdir -p "$ARMPI_ROOT/build/pc/mesa/sdroot/bin"
cp "$G/gltest.s" "$ARMPI_ROOT/build/pc/mesa/sdroot/bin/gltest"
ls -l "$G/gltest" "$G/gltest.s"
# GlKit du PC (le kit partage, libglkit.a, gltest lie a GlKit a la place du statique) : build-glkit-pc.sh
sh "$M/build-glkit-pc.sh"
