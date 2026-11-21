#!/bin/sh
# GCC, make, FreeBASIC et fasm DANS Korin sur PC (docs/etude/PC-PILOTES.md, « GCC, FreeBASIC et fasm
# dans Korin PC »), ajoutés à l'arbre système du PC, dans WSL, après tools/pc/sysroot.sh :
#     gcc/                  GCC 14.2 et binutils 2.43 natifs x86-64 (gcc, g++, cc1, cc1plus, as, ld,
#                           ar…), newlib, libstdc++, libposix du PC, specs ; make ; fbc et ses
#                           bibliothèques (lib/freebasic/armpi-x86_64) ; les interfaces des kits
#                           (include/kits, lib/lib<kit>.a, include/freebasic/kits) ; pibuild.mk
#                           (fasm à la place de fasmarm) ; les essais (test/, dont test/pc :
#                           tools/pc/gcctest, et test/fasm)
#     bin/fasm              fasm x86-64 (tools/pc/fasm.sh) ; son source dans gcc/share/fasm
#     etc/exec.txt          /devtools/gcc/x86_64-onyx-elf/bin (s'il manque)
# Construire d'abord (dans WSL, depuis la racine du dépôt) :
#     ARCH=x86_64 sh tools/gcc/build.sh && ARCH=x86_64 sh tools/gcc/kits.sh
#     ARCH=x86_64 sh tools/gcc/make.sh && ARCH=x86_64 sh tools/gcc/fbc.sh
#     sh tools/pc/fasm.sh
# puis :
#     sh tools/pc/sysroot.sh && sh tools/pc/sysroot_gcc.sh [arbre]
# Ce qui n'est pas construit est sauté (seul GCC est exigé). Rien ne change pour le Pi.
set -e
cd "$(dirname "$0")/../.."

OUT=${1:-build/pc/sysroot}		# (un autre arbre : son dossier en argument)
G=build/pc/gccroot/gcc
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : wsl sh tools/pc/sysroot.sh)"; exit 1; }
[ -x "$G/bin/gcc" ] || { echo "absent : $G (d'abord : ARCH=x86_64 sh tools/gcc/build.sh)"; exit 1; }

k0=$(du -sk "$OUT" | cut -f1)
rm -rf "$OUT/gcc" "$OUT/devtools/gcc"
cp -r "$G" "$OUT/gcc"
mkdir -p "$OUT/gcc/test/fasm" "$OUT/gcc/test/pc"
cp src/bin/fasm/test/*.asm "$OUT/gcc/test/fasm/"
cp tools/pc/gcctest/* "$OUT/gcc/test/pc/"		# (sh /devtools/gcc/test/pc/run.sh)
if [ -x build/pc/fasm/fasm ]; then
	cp build/pc/fasm/fasm "$OUT/bin/fasm"
	rm -rf "$OUT/gcc/share/fasm"
	mkdir -p "$OUT/gcc/share/fasm"
	cp -r build/pc/fasm/source "$OUT/gcc/share/fasm/"
fi
# les exécutables de /devtools/gcc (vfs : exec.txt) : l'as et le ld de la cible du PC (skel/sd0/etc/exec.txt a les
# deux cibles : tools/pc/sysroot.sh la recopie après ce script, et le paquet pc publié n'avait que celle du Pi)
if [ -f "$OUT/etc/exec.txt" ]; then
	grep -q '^/devtools/gcc/x86_64-onyx-elf/bin$' "$OUT/etc/exec.txt" || echo /devtools/gcc/x86_64-onyx-elf/bin >> "$OUT/etc/exec.txt"
fi

n=$(find "$OUT/gcc" -type f | wc -l)
k=$(du -sk "$OUT" | cut -f1)
echo "$OUT : gcc/ ($n fichiers), bin/fasm ; $((k - k0)) Ko de plus, $k Ko en tout"

# les outils de développement sous /devtools (arbre unique)
sh tools/devtools_move.sh "$OUT"
