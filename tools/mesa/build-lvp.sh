#!/bin/sh
# build-lvp : lavapipe (Vulkan sur le processeur) en VkKit pour les essais sous QEMU (configure-lvp.sh d'abord) :
#   build/pc/mesa-lvp/sdroot/lib/vkkit.so   le kit (à mettre à la place de /lib/vkkit.so sur un disque d'essai)
#   build/pc/mesa-lvp/lib/libvkkit.a        ce que les programmes lient pour ce kit (ses imports diffèrent d'anv :
#                                           un programme d'essai se relie avec celui-ci)
#   build/pc/mesa-lvp/sdroot/bin/vktest     vktest lié à ce kit
# Comme build-anv.sh : les archives de Mesa, et les objets à nous (la présentation par copie, des bouchons de
# libdrm) ; LLVM statique (build-llvm-korin.sh). docs/etude/BLENDER.md, jalon 3.
#     sh tools/mesa/build-lvp.sh            (VKKIT_TLS : zone par thread, défaut 4096 : LLVM en veut plus qu'anv)
set -eu
. "$(dirname "$0")/env.sh"
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
POSIX=$ARMPI_ROOT/build/pc/posix
OUT=$A/out-x86_64
B=$OUT/build-lvp
V=$OUT/lvp
M=$ARMPI_ROOT/tools/mesa
LLVM=${KORIN_LLVM:-$HOME/korin-llvm}/x86_64
[ -f "$B/build.ninja" ] || { echo "d'abord : sh tools/mesa/configure-lvp.sh"; exit 1; }
WHOLE_REL="src/gallium/frontends/lavapipe/liblavapipe_st.a"
LIBS_REL="src/gallium/targets/lavapipe/libvulkan_lvp.so.p/lavapipe_target.c.o
src/gallium/auxiliary/pipe-loader/libpipe_loader_static.a src/loader/libloader.a src/util/libmesa_util.a
src/util/libmesa_util_clflush.a src/util/libmesa_util_clflushopt.a src/util/libmesa_util_simd.a
src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/util/libxmlconfig.a src/gallium/auxiliary/libgallium.a
src/compiler/nir/libnir.a src/compiler/libcompiler.a src/gallium/winsys/sw/wrapper/libwsw.a
src/gallium/winsys/sw/null/libws_null.a src/gallium/drivers/llvmpipe/libllvmpipe.a src/vulkan/util/libvulkan_util.a
src/compiler/spirv/libvtn.a"
cmp -s "$M/wsi/wsi_common_korin.c" "$SRC/src/vulkan/wsi/wsi_common_korin.c" || cp "$M/wsi/wsi_common_korin.c" "$SRC/src/vulkan/wsi/"
(cd "$B" && ninja -j "$JOBS" $WHOLE_REL $LIBS_REL)
mkdir -p "$V"
CF="-specs=$POSIX/lib/posix.specs -O2 -fPIC -ffunction-sections -fdata-sections"
${TC}gcc $CF -c "$M/lvp/korin_lvp_wsi.c" -o "$V/korin_lvp_wsi.o"
${TC}gcc $CF -I"$M/lvp/libdrm" -I"$M/libdrm-pc" -I"$SRC/include/drm-uapi" -c "$M/lvp/korin_lvp_drm.c" -o "$V/korin_lvp_drm.o"
echo "$V/korin_lvp_wsi.o $V/korin_lvp_drm.o" > "$V/objs.txt"
# (comme anv : lavapipe et son runtime de Vulkan pris en entier, les tables d'entrées citent leurs fonctions par des
# références faibles ; le reste, puis zlib et LLVM)
WA=""
for a in $WHOLE_REL; do WA="$WA $B/$a"; done
LIBS=""
for a in $LIBS_REL; do LIBS="$LIBS $B/$a"; done
[ -f "$OUT/zlib.txt" ] && LIBS="$LIBS $(cat "$OUT/zlib.txt")"
LIBS="$LIBS $("$LLVM/bin/llvm-config-korin" --link-static --libfiles bitwriter core engine executionengine instcombine \
	mcdisassembler mcjit native scalaropts transformutils coroutines lto orcjit passes ipo)"
echo "$WA" > "$V/whole.txt"
echo "$LIBS" > "$V/libs.txt"
# Pas de kit : le fichier (71 Mo, LLVM compris) dépasse les 64 Mo des tampons partagés par lesquels AppKit lit un kit
# (FC_IMAGE_MAX, BUF_SIZE_MAX) et la place d'un kit chez le noyau (KIT_SLOT). Les programmes d'essai lient donc
# lavapipe en statique : le chargeur et les trampolines de VkKit (vkkit_loader.c, gen_protos.py), dont
# vk_icdGetInstanceProcAddr est ici celle de lavapipe, puis lavapipe, ses objets à nous, Mesa et LLVM.
PM=$ARMPI_ROOT/build/pc/mesa-lvp
mkdir -p "$PM/lib" "$PM/sdroot/bin"
HF="-specs=$POSIX/lib/posix.specs -O2 -g0 -fno-builtin"
${TC}gcc $HF -I"$SRC/include" -c "$M/vkkit/vkkit_loader.c" -o "$V/vkkit_loader.o"
python3 -I "$M/vkkit/gen_protos.py" "$SRC/include/vulkan/vulkan_core.h" "$V/vkkit_protos.c"
${TC}gcc $HF -ffunction-sections -fdata-sections -I"$SRC/include" -c "$V/vkkit_protos.c" -o "$V/vkkit_protos.o"
# lvp-static.txt : les arguments de liaison d'un programme (à la place de libvkkit.a), libstdc++ par g++
echo "$V/vkkit_loader.o $V/vkkit_protos.o $(cat "$V/objs.txt") -Wl,--whole-archive $WA -Wl,--no-whole-archive" \
	"-Wl,--start-group $LIBS -Wl,--end-group" > "$PM/lib/lvp-static.txt"
# vktest (VKTEST_KIT : son --win, par UIKit) lié ainsi
PCG=$ARMPI_ROOT/build/pc/gcc
${TC}gcc -specs=$POSIX/lib/posix.specs -O2 -DVKTEST_KIT -I"$SRC/include" -I"$M/vkkit/include" -I"$PCG/include/kits" \
	-I"$ARMPI_ROOT/src/kits/uikit/include" -c "$M/anv/vktest.c" -o "$V/vktest.o"
${TC}g++ -specs=$POSIX/lib/posix.specs -Wl,--gc-sections "$V/vktest.o" $(cat "$PM/lib/lvp-static.txt") \
	"$PCG/lib/libuikit.a" -o "$V/vktest" 2> "$V/link.log" ||
	{ grep -E "undefined reference" "$V/link.log" | sed 's/.*undefined reference to//' | sort | uniq -c | sort -rn | head -40
	  tail -3 "$V/link.log"; exit 1; }
${TC}strip -o "$PM/sdroot/bin/vktest" "$V/vktest"
ls -l "$PM/lib/lvp-static.txt" "$PM/sdroot/bin/vktest"
