#!/bin/sh
# build-armgl : libarmgl.a (winsys + frontal) et gltest, relies en statique a Mesa.
#     sh tools/mesa/build-armgl.sh
set -eu
. "$(dirname "$0")/env.sh"
G=$OUT/armgl
B=$OUT/build
M=$ARMPI_ROOT/tools/mesa
mkdir -p "$G"
# les memes options que Mesa : celles de v3d_screen.c, sans la sortie ; les -I sont relatifs a $B
CF=$(python3 - "$B/compile_commands.json" <<'PY'
import json, shlex, sys
for e in json.load(open(sys.argv[1])):
    if e['file'].endswith('v3d_screen.c'):
        a = shlex.split(e['command'])[1:]
        o = []
        skip = False
        for x in a:
            if skip:
                skip = False
            elif x in ('-o', '-MF', '-MQ', '-c'):
                skip = x != '-c'
            elif x != '-MD' and not x.endswith('v3d_screen.c') and not x.startswith('-W') and ' ' not in x and '"' not in x:
                o.append(x)
        print(' '.join(o))
        break
PY
)
INC="-I$M/armgl -I$M/libdrm -I$SRC/src/mesa -I$SRC/src/mesa/glapi -I$B/src/mesa -I$B/src/compiler -I$B/src/compiler/nir"
cd "$B"
${TC}gcc $INC $CF -c "$M/libdrm/armpi_drm.c" -o "$G/armpi_drm.o"
${TC}gcc $INC $CF -c "$M/armgl/armgl.c" -o "$G/armgl.o"
${TC}gcc $INC $CF -c "$M/armgl/armegl.c" -o "$G/armegl.o"
rm -f "$G/libarmgl.a"
${TC}ar rcs "$G/libarmgl.a" "$G/armpi_drm.o" "$G/armgl.o" "$G/armegl.o"
${TC}gcc -specs=$POSIX/lib/posix.specs -mcpu=cortex-a72 -O2 -I"$M/armgl" -c "$M/armgl/gltest.c" -o "$G/gltest.o"

# les archives de Mesa s'appellent l'une l'autre : un seul groupe
LIBS=""
for a in src/mesa/libmesa.a src/gallium/auxiliary/libgallium.a src/compiler/libcompiler.a src/compiler/nir/libnir.a \
	src/compiler/glsl/libglsl.a src/compiler/glsl/libglsl_util.a src/compiler/glsl/glcpp/libglcpp.a src/compiler/spirv/libvtn.a \
	src/util/libmesa_util.a src/util/libmesa_util_simd.a src/util/libxmlconfig.a src/util/libparson.a \
	src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/broadcom/cle/libbroadcom_cle.a \
	src/broadcom/compiler/libbroadcom_compiler.a src/broadcom/qpu/libbroadcom_qpu.a src/broadcom/libbroadcom_v3d.a \
	src/broadcom/libbroadcom-v42.a src/broadcom/libbroadcom-v71.a src/broadcom/libv3d_neon.a \
	src/broadcom/perfcntrs/libbroadcom_perfcntrs.a src/broadcom/perfcntrs/libv3d-perfcntrs-v42.a \
	src/broadcom/perfcntrs/libv3d-perfcntrs-v71.a src/gallium/drivers/v3d/libv3d.a \
	src/gallium/drivers/v3d/libv3d-v42.a src/gallium/drivers/v3d/libv3d-v71.a \
	src/mesa/glapi/shared-glapi/libglapi.a src/mesa/glapi/glapi/libglapi_bridge.a \
	src/gallium/auxiliary/libgalliumvl_stub.a; do
	LIBS="$LIBS $B/$a"
done
echo "$LIBS" > "$G/libs.txt"
if ! ${TC}g++ -specs=$POSIX/lib/posix.specs -Wl,--gc-sections "$G/gltest.o" -Wl,--start-group "$G/libarmgl.a" $LIBS \
	"$WK/lib/libz.a" -Wl,--end-group -o "$G/gltest" 2> "$G/link.log"; then
	grep -E "undefined reference" "$G/link.log" | sed 's/.*undefined reference to//' | sort | uniq -c | sort -rn | head -50
	tail -3 "$G/link.log"
	exit 1
fi
${TC}strip -o "$G/gltest.s" "$G/gltest"

# gltri : la fenetre, sur le relais C d'UIKit (tools/webkit/build-ui.sh)
U=$HOME/armpi-webkit-build/uikit
${TC}gcc -specs=$POSIX/lib/posix.specs -mcpu=cortex-a72 -O2 -I"$M/armgl" -I"$ARMPI_ROOT/src/kits/uikit/include" -I"$U" \
	-c "$M/armgl/gltri.c" -o "$G/gltri.o"
${TC}g++ -specs=$POSIX/lib/posix.specs -Wl,--gc-sections "$G/gltri.o" "$U/libuikit_c.a" -Wl,--start-group "$G/libarmgl.a" $LIBS \
	"$WK/lib/libz.a" -Wl,--end-group -o "$G/gltri"
${TC}strip -o "$G/gltri.s" "$G/gltri"
ls -l "$G/gltest.s" "$G/gltri.s"

# pour la carte d'essai et celle du Pi
mkdir -p "$ARMPI_ROOT/build/mesa/sdroot/bin"
cp "$G/gltest.s" "$ARMPI_ROOT/build/mesa/sdroot/bin/gltest"
cp "$G/gltri.s" "$ARMPI_ROOT/build/mesa/sdroot/bin/gltri"
