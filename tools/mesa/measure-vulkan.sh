#!/bin/sh
# measure-vulkan : construit v3dv dans un dossier a part, seulement pour en mesurer le poids (docs/MESA.md).
#     sh tools/mesa/measure-vulkan.sh
set -eu
. "$(dirname "$0")/env.sh"
cd "$SRC"
PKG_CONFIG_LIBDIR="$OUT/pc" meson setup "$OUT/build-vk" --cross-file "$OUT/cross.ini" --wipe \
	--buildtype=release -Db_ndebug=true --default-library=static -Dplatforms= -Dgallium-drivers= \
	-Dvulkan-drivers=broadcom -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Degl=disabled -Dgbm=disabled \
	-Dllvm=disabled -Dshader-cache=disabled -Dzstd=disabled -Dzlib=disabled -Dexpat=disabled -Dxmlconfig=disabled \
	-Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false -Dtools= -Dperfetto=false
cd "$OUT/build-vk"
ninja -k 0 -j "$JOBS" src/broadcom/vulkan/libvulkan_broadcom.so 2>&1 | tail -3 || true
