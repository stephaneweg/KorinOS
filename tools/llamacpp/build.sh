#!/bin/bash
# llamacpp/build.sh : llama.cpp (llama-server, llama-cli) pour Korin PC, construit DANS Korin (GCC 14, cmake et
# ninja de /devtools/gcc), sans linuxhost : tools/llamacpp/README.md. A lancer depuis la racine du depot, sous Korin :
#     bash tools/llamacpp/build.sh
# Produit :
#   build/pc/llamacpp/tree/bin/llama-server    le serveur (API compatible OpenAI, /v1/chat/completions...)
#   build/pc/llamacpp/tree/bin/llama-cli       le chat au terminal
# l'arbre du paquet llamacpp (tools/pkg/packages-pc.ini) : python3 tools/pkg/pub.py llamacpp --tree build/pc/llamacpp/tree
#
# Les sources de llama.cpp sont prises telles quelles (aucun patch) au commit LLAMA_COMMIT, dans WORK
# (~/.cache/korin-llamacpp). Ce qui est propre a Korin est dans toolchain-korin.cmake et les options ci-dessous.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/tools/llamacpp
LLAMA_REPO=${LLAMA_REPO:-https://github.com/ggml-org/llama.cpp}
LLAMA_COMMIT=${LLAMA_COMMIT:-af5c583}          # 23/11/2026, celui qui a ete essaye (README.md)
WORK=${WORK:-$HOME/.cache/korin-llamacpp}
OUT=$ROOT/build/pc/llamacpp
JOBS=${JOBS:-3}   # au-dela de 3 compilations a la fois : « File descriptor value too large » (README.md)

say () { echo "llamacpp : $*"; }

case "$(uname -s 2>/dev/null)" in
Korin*|ARMPI*) ;;
*) echo "llamacpp : a lancer sous Korin PC (GCC, cmake et ninja de /devtools/gcc)" >&2; exit 1 ;;
esac

# pip et d'autres lancent « rustc --version » puis le tuent : rien de tel ici, mais on garde le PATH sans rustc.
PATH=$(printf '%s' "$PATH" | tr ':' '\n' | grep -v '/devtools/rust' | paste -sd: -)
export PATH
# /tmp (en memoire) refuse les noms longs : les temporaires de cmake vont sur le disque.
mkdir -p "$WORK/tmp"
export TMPDIR=$WORK/tmp

S=$WORK/llama.cpp
if [ ! -d "$S/.git" ]; then
	say "recuperation de $LLAMA_REPO"
	git clone --filter=blob:none --no-checkout "$LLAMA_REPO" "$S"
fi
git -C "$S" cat-file -e "$LLAMA_COMMIT^{commit}" 2>/dev/null || git -C "$S" fetch origin
say "commit $LLAMA_COMMIT"
git -C "$S" checkout -q -f "$LLAMA_COMMIT"

B=$WORK/build
say "configuration (cmake)"
cmake -S "$S" -B "$B" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$HERE/toolchain-korin.cmake" \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_SHARED_LIBS=OFF -DGGML_STATIC=ON -DGGML_BACKEND_DL=OFF \
	-DGGML_NATIVE=OFF -DGGML_CPU_ALL_VARIANTS=OFF \
	-DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON \
	-DGGML_OPENMP=OFF -DGGML_LTO=OFF -DGGML_CCACHE=OFF \
	-DLLAMA_OPENSSL=OFF -DLLAMA_SUBPROCESS=OFF \
	-DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_APP=OFF \
	-DLLAMA_BUILD_UI=OFF -DLLAMA_USE_PREBUILT_UI=OFF -DLLAMA_BUILD_MTMD=OFF \
	-DLLAMA_BUILD_SERVER=ON -DLLAMA_BUILD_TOOLS=ON >"$WORK/cmake.log" 2>&1 || {
	tail -30 "$WORK/cmake.log" >&2; exit 1; }

say "construction (ninja -j $JOBS ; une demi-heure environ)"
ninja -C "$B" -j "$JOBS" llama-server llama-cli

say "arbre du paquet : $OUT/tree"
rm -rf "$OUT/tree"
mkdir -p "$OUT/tree/bin"
for p in llama-server llama-cli; do
	cp "$B/bin/$p" "$OUT/tree/bin/$p"
	strip "$OUT/tree/bin/$p"
	chmod 755 "$OUT/tree/bin/$p"
done
git -C "$S" rev-parse HEAD >"$OUT/commit.txt"
ls -l "$OUT/tree/bin"
say "fait"
