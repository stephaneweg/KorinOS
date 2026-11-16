#!/bin/sh
# configure-anv : Mesa pour Korin PC (x86-64), pilote Vulkan d'Intel (anv) pour le GPU du ZBook (Tiger Lake,
# Gen12), dans un dossier a part (~/armpi-mesa/out-x86_64/build-vk) : le Mesa de GlKit (build, iris) n'est pas
# touche. Meme source, memes retouches (korin-pc.patch), meme faux libdrm (tools/mesa/libdrm-pc, client du
# service src/svc/intelgpu), meme fichier croise et meme zlib que configure-iris.sh, a lancer d'abord.
# Pas de WSI de fenetre ici (platforms vide) : la premiere etape est vktest, sans affichage (build-anv.sh).
# docs/etude/VULKAN.md.
#     sh tools/mesa/configure-anv.sh
set -eu
. "$(dirname "$0")/env.sh"
OUT=$A/out-x86_64
P=$OUT/pc
PATH=$A/clc/bin:$PATH
[ -f "$OUT/cross.ini" ] && [ -f "$P/libdrm.pc" ] || { echo "d'abord : sh tools/mesa/configure-iris.sh"; exit 1; }
[ -x "$A/clc/bin/mesa_clc" ] || { echo "manque mesa_clc : sh tools/mesa/build-clc.sh"; exit 1; }
PATCH=$ARMPI_ROOT/tools/mesa/korin-pc.patch
git -C "$SRC" apply --check -R "$PATCH" 2>/dev/null || git -C "$SRC" apply "$PATCH"
if [ -f "$P/zlib.pc" ]; then CACHE="-Dshader-cache=enabled -Dzlib=enabled"; else CACHE="-Dshader-cache=disabled -Dzlib=disabled"; fi
rm -rf "$OUT/build-vk"
cd "$SRC"
PKG_CONFIG_LIBDIR="$P" meson setup "$OUT/build-vk" --cross-file "$OUT/cross.ini" \
	--buildtype=release -Db_ndebug=true --default-library=static \
	-Dplatforms= -Dgallium-drivers= -Dvulkan-drivers=intel -Dgallium-va=disabled \
	-Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Degl=disabled -Dgbm=disabled \
	-Dllvm=disabled $CACHE -Dzstd=disabled -Dexpat=disabled -Dxmlconfig=disabled \
	-Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false \
	-Dglvnd=disabled -Dtools= -Dperfetto=false -Dvulkan-layers= \
	-Dmesa-clc=system -Dintel-elk=false -Dintel-rt=disabled -Dspirv-tools=disabled "$@"
