#!/bin/bash
# all : toute la suite Craft pour Korin PC, depuis zéro (docs/CRAFT.md, docs/agents/CRAFT-HANDOVER.md). Dans WSL ou une
# machine Linux (session du cloud : après tools/cloud/setup.sh), depuis la racine du dépôt :
#     bash tools/craft/all.sh                  la chaîne Rust si elle manque, libposix et UIKit du PC, toutes les apps de
#                                              tools/craft/apps.txt, la démo d'egui, le lanceur Craft et ses icônes,
#                                              le lanceur et le menu de Korin qui cachent la catégorie « Craft »
#     bash tools/craft/all.sh gridcraft ...    seulement ces apps (puis le lanceur)
#     KEEP_GOING=0 bash tools/craft/all.sh     s'arrêter à la première app qui ne se construit pas
# Résultat : build/craft/x86_64/apps/<app>.app (main, app.txt, icon.rgba), build/craft/x86_64/bin, build/sys/x86_64/{panel,menubar},
# et l'état de chaque app dans build/craft/x86_64/status.txt. L'essai : tools/craft/pctest.py (docs/EGUI.md).
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}
TOOL=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2
TC=$TOOL/bin/x86_64-onyx-elf-
KEEP_GOING=${KEEP_GOING:-1}
OUT=$ROOT/build/craft/x86_64
mkdir -p "$OUT"
say () { printf '\n== %s\n' "$*"; }

[ -x "${TC}gcc" ] || { echo "craft : pas de chaîne x86_64-onyx-elf ($TOOL : tools/pc/toolchain.sh, ou tools/cloud/setup.sh toolchains)" >&2; exit 1; }

# 1. libposix du PC (posix.specs, l'éditeur de liens des programmes Rust)
if [ ! -f "$ROOT/build/pc/posix/lib/posix.specs" ]; then
	say "libposix du PC"
	ARCH=x86_64 NO_TESTS=1 sh tools/posix/build.sh
fi

# 2. la chaîne Rust x86_64-unknown-korin (rustup à part dans ~/.cache/korin-rust-x86_64)
if [ ! -f "$K/env.sh" ]; then
	say "chaîne Rust x86_64-unknown-korin (tools/rust/build.sh)"
	ARCH=x86_64 SKIP_TESTS=1 bash tools/rust/build.sh
fi

# 3. l'interface C d'UIKit pour le PC (build/pc/gcc : uikit_abi.h, libuikit.a), que le relais d'egui-korin prend ;
#    tools/pc/apps.sh la fait avec toutes les apps C : ici, UIKit seul, de la même façon
G=$ROOT/build/pc/gcc
if [ ! -f "$G/lib/libuikit.a" ] || [ ! -f "$G/include/kits/uikit_abi.h" ]; then
	say "UIKit depuis le C (tools/kits/abi2c.py --arch x86_64)"
	Wk=$ROOT/build/craft/kits
	rm -rf "$Wk" && mkdir -p "$Wk" "$G/lib" "$G/include/kits"
	python3 -I tools/kits/abi2c.py src/kits/uikit/uikit.abi "$Wk" --arch x86_64 --const \
		src/inc/armpi.inc src/inc/ws_ext.inc src/kits/uikit/uikit.inc >/dev/null
	"${TC}gcc" -c "$Wk/uikit_abi.S" -o "$Wk/uikit_abi.o"
	"${TC}gcc" -O2 -g0 -specs="$ROOT/build/pc/posix/lib/posix.specs" -c "$Wk/uikit_abi_open.c" -o "$Wk/uikit_abi_open.o"
	rm -f "$G/lib/libuikit.a"
	"${TC}ar" rcs "$G/lib/libuikit.a" "$Wk/uikit_abi.o" "$Wk/uikit_abi_open.o"
	cp "$Wk/uikit_abi.h" src/kits/uikit/include/uikit.h "$G/include/kits/"
fi

# 4. les apps
APPS=("$@")
[ ${#APPS[@]} -gt 0 ] || mapfile -t APPS < <(grep -vE '^\s*(#|$)' tools/craft/apps.txt | awk '{print $1}')
: > "$OUT/status.new"
for a in "${APPS[@]}"; do
	say "$a"
	log=$OUT/logs/$a.log
	mkdir -p "$OUT/logs"
	if bash tools/craft/build.sh "$a" > "$log" 2>&1; then
		echo "$a : construite" >> "$OUT/status.new"
	else
		first=$(grep -m1 -E 'could not compile|^error|craft : ' "$log" | cut -c1-160)
		echo "$a : ECHEC ($first) ; journal : build/craft/x86_64/logs/$a.log" >> "$OUT/status.new"
		[ "$KEEP_GOING" = 1 ] || { cat "$OUT/status.new"; exit 1; }
	fi
	tail -n 1 "$OUT/status.new"
done
# (une construction partielle garde l'état des autres apps)
if [ -f "$OUT/status.txt" ]; then
	grep -vF -f <(awk -F' : ' '{print $1" : "}' "$OUT/status.new") "$OUT/status.txt" >> "$OUT/status.new" || true
fi
sort -o "$OUT/status.txt" "$OUT/status.new" && rm -f "$OUT/status.new"

# 5. la démo d'egui, le lanceur Craft et les icônes (PIL pour les icônes : sans PIL, les apps n'ont pas d'icon.rgba)
say "démo d'egui"
bash tools/craft/egui.sh
say "lanceur Craft"
if python3 -E -P -c "import PIL" 2>/dev/null; then
	bash tools/craft/launcher.sh
else
	echo "craft : PIL absent (pip install --user pillow) : le lanceur est construit sans les icon.rgba" >&2
	sed 's/^python3 -E -P tools\/craft\/icon_rgba.py.*$/true/; s/&& python3 -E -P tools\/craft\/icon_rgba.py.*$/\&\& true/' tools/craft/launcher.sh | bash -s
fi

# 6. le lanceur et le menu de Korin qui cachent la catégorie « Craft » (src/sys/common/applist.c)
say "panel, menubar"
sh tools/sys/build_c.sh panel menubar

say "état"
cat "$OUT/status.txt"
