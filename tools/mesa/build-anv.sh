#!/bin/sh
# build-anv : les bibliotheques d'anv (le pilote Vulkan d'Intel de Mesa) pour Korin PC (configure-anv.sh), et vktest
# relie en statique avec le faux libdrm du PC (tools/mesa/libdrm-pc/korin_i915.c, client du service intelgpu) :
#   ~/armpi-mesa/out-x86_64/anv/vktest          (avec symboles)
#   build/pc/mesa/sdroot/bin/vktest             (sans ; tools/pc/sysroot.sh le met dans /bin du PC)
# Pas de chargeur Vulkan : vktest appelle vk_icdGetInstanceProcAddr d'anv directement. docs/etude/VULKAN.md.
#     sh tools/mesa/build-anv.sh
set -eu
. "$(dirname "$0")/env.sh"
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
POSIX=$ARMPI_ROOT/build/pc/posix
OUT=$A/out-x86_64
B=$OUT/build-vk
V=$OUT/anv
M=$ARMPI_ROOT/tools/mesa
[ -f "$B/build.ninja" ] || { echo "d'abord : sh tools/mesa/configure-anv.sh"; exit 1; }
LIBS_REL="src/intel/vulkan/libanv_common.a src/intel/vulkan/libanv_per_hw_ver90.a
src/intel/vulkan/libanv_per_hw_ver110.a src/intel/vulkan/libanv_per_hw_ver120.a src/intel/vulkan/libanv_per_hw_ver125.a
src/intel/vulkan/libanv_per_hw_ver200.a src/intel/vulkan/libanv_per_hw_ver300.a src/intel/vulkan/libanv_per_hw_ver350.a
src/intel/vulkan/liblibanv_drirc.a
src/vulkan/runtime/libvulkan_runtime.a src/vulkan/runtime/libvulkan_lite_runtime.a src/vulkan/runtime/libvulkan_instance.a
src/vulkan/util/libvulkan_util.a src/vulkan/wsi/libvulkan_wsi.a
src/intel/blorp/libblorp.a src/intel/common/libintel_common.a src/intel/compiler/brw/libintel_compiler.a
src/intel/compiler/gen/libintel_compiler_gen.a src/intel/compiler/jay/libintel_compiler_jay.a
src/intel/compiler/libintel_compiler_nir.a src/intel/decoder/libintel_decoder.a src/intel/decoder/libintel_decoder_gen.a
src/intel/dev/libintel_dev.a src/intel/ds/libintel-driver-ds.a src/intel/isl/libisl.a
src/intel/isl/libisl_per_hw_ver40.a src/intel/isl/libisl_per_hw_ver50.a src/intel/isl/libisl_per_hw_ver60.a
src/intel/isl/libisl_per_hw_ver70.a src/intel/isl/libisl_per_hw_ver75.a src/intel/isl/libisl_per_hw_ver80.a
src/intel/isl/libisl_per_hw_ver90.a src/intel/isl/libisl_per_hw_ver110.a src/intel/isl/libisl_per_hw_ver120.a
src/intel/isl/libisl_per_hw_ver125.a src/intel/isl/libisl_per_hw_ver200.a src/intel/isl/libisl_per_hw_ver300.a
src/intel/isl/libisl_per_hw_ver350.a src/intel/isl/libisl_tiled_memcpy.a src/intel/isl/libisl_tiled_memcpy_sse41.a
src/intel/mda/libmda.a src/intel/perf/libintel_perf.a src/intel/shaders/libintel_shaders.a
src/compiler/libcompiler.a src/compiler/nir/libnir.a src/compiler/spirv/libvtn.a
src/util/libmesa_util.a src/util/libmesa_util_simd.a src/util/libmesa_util_clflush.a src/util/libmesa_util_clflushopt.a
src/util/libxmlconfig.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a"
GEM=src/intel/vulkan/libvulkan_intel.so.p/anv_gem.c.o
cd "$B"
PATH=$A/clc/bin:$PATH ninja -j "$JOBS" $LIBS_REL $GEM

# le faux libdrm et vktest : les options d'un source d'anv (anv_device.c)
mkdir -p "$V"
CF=$(python3 - "$B/compile_commands.json" <<'PY'
import json, shlex, sys
for e in json.load(open(sys.argv[1])):
    if e['file'].endswith('anv_device.c'):
        a = shlex.split(e['command'])[1:]
        o = []
        skip = False
        for x in a:
            if skip:
                skip = False
            elif x in ('-o', '-MF', '-MQ', '-c'):
                skip = x != '-c'
            elif x != '-MD' and not x.endswith('anv_device.c') and not x.startswith('-W') and ' ' not in x and '"' not in x:
                o.append(x)
        print(' '.join(o))
        break
PY
)
INC="-I$M/libdrm-pc -I$M/libdrm -I$SRC/include -I$ARMPI_ROOT/src/svc/intelgpu"
${TC}gcc $INC $CF -DKORIN_I915_VK -c "$M/libdrm-pc/korin_i915.c" -o "$V/korin_i915.o"
# l'identite d'anv (korin-pc.patch, anv_physical_device_init_uuids : pas de build-id lisible ici) : le sha1 des
# archives, pour ses UUID et son cache de shaders ; un anv change donne un autre cache
ID=$(cd "$B" && cat $LIBS_REL $GEM | sha1sum | cut -c1-40)
printf 'const char korin_vk_id[] = "%s";\n' "$ID" > "$V/korin_vk_id.c"
${TC}gcc -O2 -c "$V/korin_vk_id.c" -o "$V/korin_vk_id.o"
${TC}gcc -specs=$POSIX/lib/posix.specs -O2 -I"$SRC/include" -c "$M/anv/vktest.c" -o "$V/vktest.o"
# les tables d'entrees de Vulkan citent leurs fonctions par des references faibles, qui ne tirent pas un membre
# d'archive : anv et le runtime de Vulkan en entier (comme link_whole pour libvulkan_intel.so de Mesa), sinon une
# entree reste a 0 (faute a l'adresse 0 dans wsi_device_init)
WHOLE="-Wl,--whole-archive"
LIBS="$B/$GEM"
for a in $LIBS_REL; do
	case "$a" in
	src/intel/vulkan/*|src/vulkan/*) WHOLE="$WHOLE $B/$a" ;;
	*) LIBS="$LIBS $B/$a" ;;
	esac
done
WHOLE="$WHOLE -Wl,--no-whole-archive"
[ -f "$OUT/zlib.txt" ] && LIBS="$LIBS $(cat "$OUT/zlib.txt")"
echo "$WHOLE $LIBS" > "$V/libs.txt"
if ! ${TC}g++ -specs=$POSIX/lib/posix.specs -Wl,--gc-sections "$V/vktest.o" $WHOLE -Wl,--start-group "$V/korin_i915.o" "$V/korin_vk_id.o" $LIBS \
	-Wl,--end-group -o "$V/vktest" 2> "$V/link.log"; then
	grep -E "undefined reference" "$V/link.log" | sed 's/.*undefined reference to//' | sort | uniq -c | sort -rn | head -60
	tail -3 "$V/link.log"
	exit 1
fi
${TC}strip -o "$V/vktest.s" "$V/vktest"
mkdir -p "$ARMPI_ROOT/build/pc/mesa/sdroot/bin"
cp "$V/vktest.s" "$ARMPI_ROOT/build/pc/mesa/sdroot/bin/vktest"
ls -l "$V/vktest" "$ARMPI_ROOT/build/pc/mesa/sdroot/bin/vktest"
