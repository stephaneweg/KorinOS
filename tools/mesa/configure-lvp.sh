#!/bin/sh
# configure-lvp : Mesa pour Korin PC (x86-64), lavapipe (Vulkan sur le processeur, sur llvmpipe et LLVM), dans un
# dossier à part (~/armpi-mesa/out-x86_64/build-lvp) : sert à essayer sous QEMU, qui n'a pas de GPU Intel, ce qui
# demande Vulkan (Blender, Godot Forward+ : docs/etude/BLENDER.md, jalon 3). Même source, mêmes retouches et même
# WSI de Korin que configure-anv.sh, à lancer d'abord ; LLVM : tools/mesa/build-llvm-korin.sh (llvm-config-korin).
# La présentation : tools/mesa/lvp/korin_lvp_wsi.c (copie dans un tampon partagé), liée par build-lvp.sh.
#     sh tools/mesa/configure-lvp.sh
set -eu
. "$(dirname "$0")/env.sh"
OUT=$A/out-x86_64
P=$OUT/pc
LLVM=${KORIN_LLVM:-$HOME/korin-llvm}/x86_64
[ -f "$OUT/cross.ini" ] && [ -f "$P/libdrm.pc" ] || { echo "d'abord : sh tools/mesa/configure-iris.sh"; exit 1; }
[ -x "$LLVM/bin/llvm-config-korin" ] || { echo "d'abord : sh tools/mesa/build-llvm-korin.sh"; exit 1; }
PATCH=$ARMPI_ROOT/tools/mesa/korin-pc.patch
git -C "$SRC" apply --check -R "$PATCH" 2>/dev/null || git -C "$SRC" apply "$PATCH"
cp "$ARMPI_ROOT/tools/mesa/wsi/wsi_common_korin.c" "$SRC/src/vulkan/wsi/"
# le fichier croisé de configure-iris.sh, avec le llvm-config de Korin
sed "s|^pkg-config = .*|&\nllvm-config = '$LLVM/bin/llvm-config-korin'|" "$OUT/cross.ini" > "$OUT/cross-lvp.ini"
if [ -f "$P/zlib.pc" ]; then CACHE="-Dshader-cache=enabled -Dzlib=enabled"; else CACHE="-Dshader-cache=disabled -Dzlib=disabled"; fi
# pkg-config : zlib, et un faux libdrm à lavapipe (tools/mesa/lvp/libdrm : celui du PC complété pour le chargeur de
# Mesa, des bouchons dans korin_lvp_drm.c) ; Mesa l'exige dès qu'il y a Vulkan sur un système « Linux »
PL=$OUT/pc-lvp
rm -rf "$PL" && mkdir -p "$PL"
[ -f "$P/zlib.pc" ] && cp "$P/zlib.pc" "$PL/"
printf 'Name: libdrm\nDescription: le faux libdrm de lavapipe (tools/mesa/lvp/libdrm)\nVersion: 2.4.999\nCflags: -I%s -I%s -I%s\nLibs:\n' \
	"$ARMPI_ROOT/tools/mesa/lvp/libdrm" "$ARMPI_ROOT/tools/mesa/libdrm-pc" "$SRC/include/drm-uapi" > "$PL/libdrm.pc"
sed -i "s|^pkg_config_libdir = .*|pkg_config_libdir = '$PL'|" "$OUT/cross-lvp.ini"
rm -rf "$OUT/build-lvp"
cd "$SRC"
PKG_CONFIG_LIBDIR="$PL" meson setup "$OUT/build-lvp" --cross-file "$OUT/cross-lvp.ini" \
	--buildtype=release -Db_ndebug=true --default-library=static \
	-Dplatforms= -Dgallium-drivers=llvmpipe -Dvulkan-drivers=swrast -Dgallium-va=disabled \
	-Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Degl=disabled -Dgbm=disabled \
	-Dllvm=enabled -Dshared-llvm=disabled -Ddraw-use-llvm=true $CACHE -Dzstd=disabled -Dexpat=disabled \
	-Dxmlconfig=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false \
	-Dglvnd=disabled -Dtools= -Dperfetto=false -Dvulkan-layers= -Dspirv-tools=disabled "$@"
