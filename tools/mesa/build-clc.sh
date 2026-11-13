#!/bin/sh
# build-clc : les deux outils de Mesa qui tournent sur la machine de construction (WSL) pour iris, le
# pilote du GPU Intel du PC : mesa_clc (OpenCL C -> SPIR-V, par clang et libclc) et vtn_bindgen2 (SPIR-V
# -> C). iris en a besoin pour ses noyaux internes (src/intel/shaders : génération des tirages indirects,
# copies des requêtes). La construction croisée (configure-iris.sh) les prend par -Dmesa-clc=system.
# Paquets de la machine (Ubuntu 26.04) :
#   sudo apt-get install llvm-21-dev libclang-21-dev libclang-cpp21-dev clang-21 libclc-21-dev \
#        llvm-spirv-21 libllvmspirvlib-21-dev spirv-tools-dev
#     sh tools/mesa/build-clc.sh      -> ~/armpi-mesa/clc/bin/{mesa_clc,vtn_bindgen2}
set -eu
. "$(dirname "$0")/env.sh"
B=$A/out-clc
D=$A/clc
cd "$SRC"
if [ ! -f "$B/build.ninja" ]; then
	meson setup "$B" --buildtype=release -Dprefix="$D" \
		-Dmesa-clc=enabled -Dinstall-mesa-clc=true -Dprecomp-compiler=disabled 2>/dev/null ||
	meson setup "$B" --buildtype=release -Dprefix="$D" \
		-Dmesa-clc=enabled -Dinstall-mesa-clc=true \
		-Dplatforms= -Dgallium-drivers= -Dvulkan-drivers= -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
		-Dglx=disabled -Degl=disabled -Dgbm=disabled -Dllvm=enabled -Dshared-llvm=enabled \
		-Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false -Dzstd=disabled \
		-Dexpat=disabled -Dxmlconfig=disabled -Dtools= -Dperfetto=false
fi
ninja -C "$B" src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2
mkdir -p "$D/bin"
cp "$B/src/compiler/clc/mesa_clc" "$B/src/compiler/spirv/vtn_bindgen2" "$D/bin/"
ls -l "$D/bin"
