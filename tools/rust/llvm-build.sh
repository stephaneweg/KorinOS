#!/bin/bash
# llvm-build : LLVM (celui de Rust 1.97.1, 22.1.6-rust) compilé pour Korin comme hôte (docs/RUST.md, « LLVM pour
# Korin ») : les bibliothèques que rustc relie (X86 et AArch64) et quelques outils pour l'essayer. Dans WSL ou le cloud,
# depuis la racine du dépôt, après ARCH=x86_64 sh tools/posix/build.sh :
#     bash tools/rust/llvm-build.sh            le PC : ~/korin-rustc/llvm-korin-x86_64
# Les sources sont celles de tools/rust/rustc-build.sh (SRC_ONLY=1 : rustc-1.97.1-src patché, dont llvm.patch) ;
# llvm-tblgen de l'hôte : celui du LLVM de la CI (même version). Résultat :
#   $KORIN_RUSTC/llvm-korin-x86_64/lib/libLLVM*.a     les 117 bibliothèques
#   build/pc/rust/tests/llvm/   llc, opt, llvm-mc, llvm-config (strippés) et tools/rust/tests/llvm/hello.ll, que
#                               tools/rust/testimage.py --pc met dans HOME:/rust/llvm
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${KORIN_RUSTC:-$HOME/korin-rustc}
ARCH=${ARCH:-x86_64}
[ "$ARCH" = x86_64 ] || { echo "llvm-build : le PC seulement pour l'instant (ARCH=x86_64)" >&2; exit 1; }
POSIX=$ROOT/build/pc/posix
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
[ -f "$POSIX/lib/posix.specs" ] || { echo "llvm-build : $POSIX absent (ARCH=x86_64 sh tools/posix/build.sh)" >&2; exit 1; }

SRC_ONLY=1 bash "$ROOT/tools/rust/rustc-build.sh"
B=$W/llvm-korin-$ARCH
# tools/cao/korin.cmake : Korin vu comme un « Linux » sans __linux__ (les chemins POSIX génériques de LLVM) ; ses
# options d'édition de liens sont vidées : CMake passe déjà CMAKE_CXX_FLAGS (et sa -specs) à l'édition de liens, et
# gcc refuse une spec lue deux fois. Ni libLTO (une .so qui embarquerait libstdc++.a), ni zlib, zstd, libxml2.
ARCH=$ARCH KORIN_POSIX=$POSIX cmake -G Ninja -S "$W/src/src/llvm-project/llvm" -B "$B" \
	-DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/cao/korin.cmake" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_EXE_LINKER_FLAGS="" -DCMAKE_SHARED_LINKER_FLAGS="" -DCMAKE_MODULE_LINKER_FLAGS="" \
	-DLLVM_TARGETS_TO_BUILD="X86;AArch64" -DLLVM_HOST_TRIPLE=$ARCH-unknown-korin \
	-DLLVM_DEFAULT_TARGET_TRIPLE=$ARCH-unknown-korin -DLLVM_TABLEGEN="$W/ci-llvm/bin/llvm-tblgen" \
	-DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
	-DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_BINDINGS=OFF -DLLVM_ENABLE_PLUGINS=OFF -DLLVM_TOOL_LTO_BUILD=OFF \
	-DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF \
	-DLLVM_ENABLE_PIC=ON -DLLVM_ENABLE_THREADS=ON -DLLVM_BUILD_TOOLS=ON >/dev/null
ninja -C "$B" ${JOBS:+-j "$JOBS"} llvm-libraries llc opt llvm-mc llvm-config	# (JOBS : limite, pour la mémoire)
T=$ROOT/build/pc/rust/tests/llvm
mkdir -p "$T"
for t in llc opt llvm-mc llvm-config; do "${TC}strip" "$B/bin/$t" -o "$T/$t"; done
cp "$ROOT/tools/rust/tests/llvm/hello.ll" "$T/"
echo "llvm-build : $(ls "$B"/lib/libLLVM*.a | wc -l) bibliothèques dans $B/lib ; outils dans $T"
