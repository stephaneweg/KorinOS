#!/bin/sh
# build-llvm-korin.sh : LLVM pour Korin PC (x86-64), en bibliothèques statiques, pour llvmpipe et lavapipe de Mesa
# (Vulkan sur le processeur : essayer sous QEMU ce qui demande Vulkan, docs/etude/BLENDER.md, jalon 3). Même version
# que le LLVM de l'hôte (21.1.8 d'Ubuntu 26.04, /usr/lib/llvm-21) : ses llvm-tblgen servent à la construction, et
# son llvm-config, réécrit pour ce LLVM (llvm-config-korin), sert à la configuration croisée de Mesa.
# Sans rapport avec le LLVM de Rust (tools/rust/llvm-build.sh, autre version, construit avec rustc).
#
#   sh tools/mesa/build-llvm-korin.sh        (WSL, depuis la racine du dépôt ; JOBS, défaut 6)
# Résultat : ~/korin-llvm/x86_64 (include, lib/libLLVM*.a, bin/llvm-config-korin)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
V=21.1.8
HOSTLLVM=${HOSTLLVM:-/usr/lib/llvm-$(echo $V | cut -d. -f1)}
W=${KORIN_LLVM:-$HOME/korin-llvm}
OUT=$W/x86_64
JOBS=${JOBS:-6}
POSIX=$ROOT/build/pc/posix
[ -f "$POSIX/lib/posix.specs" ] || { echo "build-llvm-korin : $POSIX absent (ARCH=x86_64 sh tools/posix/build.sh)" >&2; exit 1; }
[ "$("$HOSTLLVM/bin/llvm-config" --version)" = "$V" ] ||
	{ echo "build-llvm-korin : il faut le LLVM $V de l'hôte dans $HOSTLLVM (llvm-tblgen, llvm-config)" >&2; exit 1; }

mkdir -p "$W/dl" "$W/src"
F=llvm-project-$V.src.tar.xz
if [ ! -s "$W/dl/$F" ]; then
	curl -fL --retry 3 -o "$W/dl/$F.part" "https://github.com/llvm/llvm-project/releases/download/llvmorg-$V/$F"
	mv "$W/dl/$F.part" "$W/dl/$F"
fi
# LLVM ne publie qu'une signature gpg (.sig), pas de somme : celle de la première archive téléchargée est retenue
# ici, et une archive qui change ensuite est refusée
SUM=$(sha256sum "$W/dl/$F" | cut -d' ' -f1)
[ -f "$W/dl/$F.sha256" ] || echo "$SUM" > "$W/dl/$F.sha256"
[ "$(cat "$W/dl/$F.sha256")" = "$SUM" ] || { echo "build-llvm-korin : $F a changé" >&2; exit 1; }
S=$W/src/llvm-project-$V.src
if [ ! -d "$S" ]; then
	tar -xJf "$W/dl/$F" -C "$W/src"
	# la retouche de Korin du LLVM de Rust (statvfs : f_flag, volumes locaux), la même pour cette version
	patch -d "$S" -p1 < "$ROOT/tools/rust/llvm.patch"
fi

# tools/cao/korin.cmake : Korin vu comme un « Linux » sans __linux__ ; X86 seul (llvmpipe : la cible native) ;
# ni zlib, zstd, libxml2, terminfo ; les outils de l'hôte pour tblgen.
B=$W/build-x86_64
ARCH=x86_64 KORIN_POSIX=$POSIX cmake -G Ninja -S "$S/llvm" -B "$B" \
	-DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/cao/korin.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$OUT" \
	-DCMAKE_EXE_LINKER_FLAGS="" -DCMAKE_SHARED_LINKER_FLAGS="" -DCMAKE_MODULE_LINKER_FLAGS="" \
	-DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_HOST_TRIPLE=x86_64-unknown-linux-gnu \
	-DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-unknown-linux-gnu \
	-DLLVM_TABLEGEN="$HOSTLLVM/bin/llvm-tblgen" -DLLVM_NATIVE_TOOL_DIR="$HOSTLLVM/bin" \
	-DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
	-DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_BINDINGS=OFF -DLLVM_ENABLE_PLUGINS=OFF \
	-DLLVM_TOOL_LTO_BUILD=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
	-DLLVM_INCLUDE_DOCS=OFF -DLLVM_BUILD_TOOLS=OFF -DLLVM_INCLUDE_TOOLS=ON -DLLVM_INCLUDE_UTILS=OFF \
	-DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_THREADS=ON -DLLVM_ENABLE_PIC=OFF >/dev/null
ninja -C "$B" -j "$JOBS" llvm-libraries llvm-headers
ninja -C "$B" -j "$JOBS" install-llvm-libraries install-llvm-headers install-cmake-exports >/dev/null 2>&1 ||
	ninja -C "$B" install >/dev/null
# llvm-config-korin : celui de l'hôte (même version, mêmes composants nommés pareil), chemins et cible de Korin
mkdir -p "$OUT/bin"
cat > "$OUT/bin/llvm-config-korin" <<EOF
#!/bin/sh
# llvm-config-korin : le llvm-config de l'hôte ($HOSTLLVM, LLVM $V), réécrit pour le LLVM de Korin ($OUT) :
# Mesa le lit pendant sa configuration croisée (cross.ini de lavapipe). Fait par tools/mesa/build-llvm-korin.sh.
case "\$*" in
*--targets-built*) echo X86; exit 0 ;;
*--shared-mode*) echo static; exit 0 ;;
*--system-libs*) echo; exit 0 ;;
*--host-target*) echo x86_64-unknown-linux-gnu; exit 0 ;;
esac
"$HOSTLLVM/bin/llvm-config" "\$@" | sed -e "s|$HOSTLLVM|$OUT|g" -e 's|-lLLVM-[0-9][0-9]*||g'
EOF
chmod +x "$OUT/bin/llvm-config-korin"
echo "build-llvm-korin : $(ls "$OUT"/lib/libLLVM*.a | wc -l) bibliothèques dans $OUT/lib"
