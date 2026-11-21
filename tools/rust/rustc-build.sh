#!/bin/bash
# rustc-build : un rustc 1.97.1 qui connaît les cibles de Korin sans fichier JSON (docs/RUST.md, « La cible dans le
# compilateur »). Dans WSL ou le cloud, depuis la racine du dépôt, après ARCH=x86_64 bash tools/rust/build.sh (pour
# libposix et l'éditeur de liens du PC) :
#     bash tools/rust/rustc-build.sh            stage 1 : rustc de l'hôte + la std de x86_64-unknown-korin
#     TARGETS="x86_64-unknown-korin aarch64-unknown-korin" bash tools/rust/rustc-build.sh   les deux
#     KORIN_HOSTS=x86_64-unknown-korin bash tools/rust/rustc-build.sh    puis rustc pour Korin (stage 2, hôte korin :
#                                            LLVM pour Korin construit par bootstrap, librustc_driver.so, rustc ;
#                                            KORIN_HOSTS doit être aussi dans TARGETS)
# Les sources de rust-lang/rust à la 1.97.1 (rustc-1.97.1-src.tar.xz, sha256 vérifié), avec :
#   tools/rust/rustc.patch   le compilateur : Os::Korin, spec/base/korin.rs, les deux cibles, la doc de plateforme,
#                            l'essai d'assembleur, la liste des cibles inconnues du compilateur d'amorçage (bootstrap)
#   tools/rust/std.patch, tools/rust/libc.patch   la std et la libc (les mêmes que pour rust-src)
#   tools/rust/llvm.patch    LLVM (src/llvm-project) pour Korin comme hôte (tools/rust/llvm-build.sh)
# SRC_ONLY=1 : seulement les sources patchées et le LLVM de la CI (tools/rust/llvm-build.sh).
# LLVM de l'hôte : celui de la CI de Rust pour ce même commit (rust-dev, LLVM 22.1.6), au lieu de le construire.
# Travail dans ~/korin-rustc (KORIN_RUSTC) : src/ (refait si un patch change), ci-llvm/, build/ (gardé).
# Résultat : $KORIN_RUSTC/src/build/x86_64-unknown-linux-gnu/stage1 (lié à rustup sous le nom « korin-stage1 »).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${KORIN_RUSTC:-$HOME/korin-rustc}
V=1.97.1
COMMIT=8bab26f4f68e0e26f0bb7960be334d5b520ea452
SRC_SHA=0ed06fdaffd4722a7702e0b4eebfafc897ab8f513e8e1b247cdd7e5c6df6ded2
HOST=x86_64-unknown-linux-gnu
TARGETS=${TARGETS:-x86_64-unknown-korin}
mkdir -p "$W/dl"

# 1. les sources, patchées (refaites quand un des patchs a changé)
[ -f "$W/dl/rustc-$V-src.tar.xz" ] || curl -sSfL "https://static.rust-lang.org/dist/rustc-$V-src.tar.xz" -o "$W/dl/rustc-$V-src.tar.xz"
(cd "$W/dl" && echo "$SRC_SHA  rustc-$V-src.tar.xz" | sha256sum -c - >/dev/null)
S=$W/src
STAMP=$(cat "$ROOT/tools/rust/rustc.patch" "$ROOT/tools/rust/std.patch" "$ROOT/tools/rust/libc.patch" \
	"$ROOT/tools/rust/llvm.patch" "$ROOT"/tools/rust/rustc-vendor/*.patch 2>/dev/null | sha256sum | cut -c1-16)
if [ "$(cat "$S/.korin" 2>/dev/null)" != "$STAMP" ]; then
	# (build/ est gardé : il ne dépend que des sources, que cargo et bootstrap suivent)
	[ -d "$S/build" ] && mv "$S/build" "$W/build.keep"
	rm -rf "$S"
	mkdir -p "$S"
	tar xJf "$W/dl/rustc-$V-src.tar.xz" -C "$S" --strip-components=1
	[ -d "$W/build.keep" ] && mv "$W/build.keep" "$S/build"
	(cd "$S" && patch -p1 -s < "$ROOT/tools/rust/rustc.patch")
	(cd "$S/src/llvm-project" && patch -p1 -s < "$ROOT/tools/rust/llvm.patch")
	(cd "$S/library" && patch -p1 -s < "$ROOT/tools/rust/libc.patch" && patch -p1 -s < "$ROOT/tools/rust/std.patch")
	# les fichiers de libc changent : la somme de contrôle du vendeur ne vaut plus
	python3 -I - "$S/library/vendor/libc-0.2.185/.cargo-checksum.json" <<'EOF'
import json, sys
p = sys.argv[1]
j = json.load(open(p))
json.dump({"files": {}, "package": j["package"]}, open(p, "w"))
EOF
	# les caisses vendues du compilateur (vendor/, pour rustc avec Korin pour hôte) : la libc du compilateur reçoit le
	# même module korin (libc.patch), les autres tools/rust/rustc-vendor/<caisse-version>.patch ; leur somme de
	# contrôle de vendeur est vidée (des fichiers changent)
	VP=""
	for v in $(python3 -I -c 'import sys, tomllib; print(" ".join(p["version"] for p in tomllib.load(open(sys.argv[1], "rb"))["package"] if p["name"] == "libc"))' "$S/Cargo.lock"); do
		sed 's#vendor/libc-0\.2\.185/##g' "$ROOT/tools/rust/libc.patch" | (cd "$S/vendor/libc-$v" && patch -p1 -s --no-backup-if-mismatch)
		VP="$VP $S/vendor/libc-$v"
	done
	for p in "$ROOT"/tools/rust/rustc-vendor/*.patch; do
		[ -f "$p" ] || continue
		c=$(basename "$p" .patch)
		(cd "$S/vendor/$c" && patch -p1 -s --no-backup-if-mismatch < "$p")
		VP="$VP $S/vendor/$c"
	done
	for d in $VP; do
		python3 -I - "$d/.cargo-checksum.json" <<'EOF2'
import json, sys
p = sys.argv[1]
j = json.load(open(p))
json.dump({"files": {}, "package": j["package"]}, open(p, "w"))
EOF2
	done
	# libc devient un chemin ([patch] de std.patch) : library/Cargo.lock le dit, x.py le tient figé (--frozen)
	(cd "$S/library" && RUSTC_BOOTSTRAP=1 cargo update -q -p libc --offline)
	echo "$STAMP" > "$S/.korin"
fi

# 2. LLVM de l'hôte : rust-dev de la CI de Rust (le LLVM avec lequel la 1.97.1 a été publiée)
L=$W/ci-llvm
if [ ! -x "$L/bin/llvm-config" ]; then
	f=rust-dev-$V-$HOST.tar.xz
	[ -f "$W/dl/$f" ] || curl -sSfL "https://ci-artifacts.rust-lang.org/rustc-builds/$COMMIT/$f" -o "$W/dl/$f"
	rm -rf "$L" && mkdir -p "$L"
	tar xJf "$W/dl/$f" -C "$L" --strip-components=2
fi
[ "${SRC_ONLY:-0}" = 1 ] && exit 0

# 3. bootstrap.toml : l'hôte, les cibles de Korin et leur chaîne C (gcc + posix.specs) ; pour une cible de Korin qui est
#    aussi un hôte (KORIN_HOSTS), le llvm-config natif de son LLVM quand il existe (étape 5)
for t in $TARGETS; do
	a=${t%%-*}
	case $a in
	x86_64)  TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
		SPECS=$ROOT/build/pc/posix/lib/posix.specs ;;
	aarch64) TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
		SPECS=$ROOT/build/posix/lib/posix.specs ;;
	*) echo "rustc-build : cible inconnue $t" >&2; exit 1 ;;
	esac
	[ -f "$SPECS" ] || { echo "rustc-build : $SPECS absent (ARCH=$a sh tools/posix/build.sh)" >&2; exit 1; }
	mkdir -p "$W/bin"
	# (_GNU_SOURCE : sans lui, newlib cache POSIX en mode strict, -std=c++17 de LLVM : sigemptyset...)
	for d in gcc g++; do
		printf '#!/bin/sh\nexec %s%s -specs=%s -D_GNU_SOURCE "$@"\n' "$TC" "$d" "$SPECS" > "$W/bin/$a-korin-$d"
		chmod +x "$W/bin/$a-korin-$d"
	done
	eval "TC_$a=\$TC"
done
T="\"$HOST\""
for t in $TARGETS; do T="$T, \"$t\""; done
HL="\"$HOST\""
for h in ${KORIN_HOSTS:-}; do HL="$HL, \"$h\""; done
write_config () {
	TL=""
	for t in $TARGETS; do
		a=${t%%-*}
		eval "TC=\$TC_$a"
		TL="$TL
[target.$t]
cc = \"$W/bin/$a-korin-gcc\"
cxx = \"$W/bin/$a-korin-g++\"
ar = \"${TC}ar\"
ranlib = \"${TC}ranlib\"
linker = \"$W/bin/$a-korin-gcc\"
"
		case " ${KORIN_HOSTS:-} " in *" $t "*)
			[ -x "$S/build/$t/llvm/bin/llvm-config-native" ] &&
				TL="${TL}llvm-config = \"$S/build/$t/llvm/bin/llvm-config-native\"
" ;;
		esac
	done
	cat > "$S/bootstrap.toml" <<EOF
# tools/rust/rustc-build.sh (Korin)
change-id = "ignore"

[llvm]
download-ci-llvm = false
# le LLVM de la CI est une bibliothèque partagée (libLLVM.so, link-type.txt) ; celui de Korin reste statique
# (rustc.patch)
link-shared = true

[build]
build = "$HOST"
host = [$HL]
target = [$T]
docs = false
extended = false
optimized-compiler-builtins = false

[rust]
lld = false
llvm-tools = false
debuginfo-level = 0

[target.$HOST]
llvm-config = "$L/bin/llvm-config"
$TL
EOF
}
write_config

# 4. stage 1 : rustc de l'hôte et la std des cibles de Korin
A=$HOST
for t in $TARGETS; do A="$A,$t"; done
# (le rustc de stage 1 charge libLLVM.so, qui est dans le LLVM de la CI : pendant la construction par
# LD_LIBRARY_PATH, puis copiée à côté de lui, dans stage1/lib, son rpath)
export LD_LIBRARY_PATH=$L/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
(cd "$S" && python3 x.py build --stage 1 library --target "$A" -j "$(nproc)")
ST=$S/build/$HOST/stage1
cp "$L"/lib/libLLVM*.so* "$ST/lib/"
"${CARGO_HOME:-$HOME/.cargo}/bin/rustup" toolchain link korin-stage1 "$ST" 2>/dev/null || true
"$ST/bin/rustc" --print target-list | grep korin
echo "rustc-build : $ST"

# 5. rustc pour Korin (KORIN_HOSTS, des cibles de TARGETS) : stage 2, compilé par le stage 1 (seul à connaître les
#    cibles de Korin).
#    LLVM pour Korin : construit par bootstrap (rustc.patch : statique, sans .so) ; rustc_llvm le décrit par un
#    llvm-config qui tourne sur la machine de construction : llvm-config-native, compilé ici de llvm-config.cpp avec la
#    configuration de la construction de Korin et la bibliothèque Support du LLVM de la CI, posé dans bin/ de
#    l'installation de Korin (il en déduit le préfixe), puis donné à bootstrap comme llvm-config de Korin (après quoi
#    bootstrap ne reconstruit plus ce LLVM : effacer build/<hôte>/llvm pour le refaire).
#    Le programme rustc est l'hôte de librustc_driver.so, de la std et des macros procédurales : il exporte la libc
#    (-rdynamic) et libstdc++ (dlhost-cxx.ld de libposix, que demande le C++ de LLVM dans librustc_driver) ;
#    compiler/rustc/build.rs lit KORIN_DLHOST_CXX et KORIN_RUSTC_LINK_ARGS (rustc.patch ; par défaut -tls-reserve=0x40000,
#    la réserve TLS des .so chargées au démarrage, avec laquelle la chaîne publiée a été construite).
for h in ${KORIN_HOSTS:-}; do
	K=$S/build/$h/llvm
	if [ ! -x "$K/bin/llvm-config-native" ]; then
		(cd "$S" && python3 x.py build src/llvm-project --target "$h" -j "$(nproc)")
		O=$W/llvm-config-native-$h
		mkdir -p "$O"
		g++ -std=c++17 -O1 -fno-exceptions -fno-rtti -DCMAKE_CFG_INTDIR='"."' -I"$K/build/tools/llvm-config" \
			-I"$K/build/include" -I"$K/include" -I"$L/include" \
			-c "$S/src/llvm-project/llvm/tools/llvm-config/llvm-config.cpp" -o "$O/llvm-config.o"
		LIBLLVM=$(basename "$(ls "$L"/lib/libLLVM-*.so)" .so)
		g++ "$O/llvm-config.o" -o "$K/bin/llvm-config-native" -L"$L/lib" -l"${LIBLLVM#lib}" -Wl,-rpath,"$L/lib"
		"$K/bin/llvm-config-native" --version --host-target
	fi
done
write_config
for h in ${KORIN_HOSTS:-}; do
	a=${h%%-*}
	case $a in
	x86_64) P=$ROOT/build/pc/posix/lib ;;
	aarch64) P=$ROOT/build/posix/lib ;;
	esac
	(cd "$S" && KORIN_DLHOST_CXX=$P/dlhost-cxx.ld KORIN_RUSTC_LINK_ARGS="${KORIN_RUSTC_LINK_ARGS--tls-reserve=0x40000}" \
		python3 x.py build --stage 2 compiler/rustc library --host "$h" --target "$h" -j "$(nproc)")
	echo "rustc-build : $S/build/$h/stage2"
done

# 6. la chaîne de Korin, prête à poser dans /devtools/rust (tools/pc/sysroot_cargo.sh) : bin/rustc et
#    lib/librustc_driver-*.so du stage 2 (sans les infos de débogage, exports gardés), et la std de la cible, celle
#    du stage 1 (même source, même version : ce que fait la distribution de Rust pour un hôte croisé), .rlib et leurs
#    .rmeta (les .rlib n'y portent qu'un résumé des métadonnées)
for h in ${KORIN_HOSTS:-}; do
	a=${h%%-*}
	case $a in
	x86_64) OUT=$ROOT/build/pc/rust/toolchain ;;
	aarch64) OUT=$ROOT/build/rust/toolchain ;;
	esac
	eval "TC=\$TC_$a"
	B=$S/build/$h/stage2
	SL=$S/build/$HOST/stage1/lib/rustlib/$h/lib
	rm -rf "$OUT"
	mkdir -p "$OUT/bin" "$OUT/lib/rustlib/$h/lib"
	"${TC}strip" --strip-debug "$B/bin/rustc" -o "$OUT/bin/rustc"
	for f in "$B"/lib/librustc_driver-*.so; do "${TC}strip" --strip-debug "$f" -o "$OUT/lib/$(basename "$f")"; done
	for r in "$SL"/*.rlib; do
		cp "$r" "$OUT/lib/rustlib/$h/lib/"
		[ -f "${r%.rlib}.rmeta" ] && cp "${r%.rlib}.rmeta" "$OUT/lib/rustlib/$h/lib/"
	done
	echo "rustc-build : $OUT ($(du -sh "$OUT" | cut -f1))"
done
