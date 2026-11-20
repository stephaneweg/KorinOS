#!/bin/bash
# craft : les apps Craft de storytold (Rust pur, egui) pour Korin, docs/CRAFT.md.
# Le PC seulement pour l'instant (décision du 20/11/2026) ; ARCH=aarch64 construit aussi pour le Pi, sans essai.
# Dans WSL, depuis la racine du dépôt, après ARCH=x86_64 bash tools/rust/build.sh :
#     bash tools/craft/build.sh                     wordcraft-cli pour le PC (x86_64)
#     bash tools/craft/build.sh <app> <paquet>      une autre app ou un autre paquet du même dépôt
# Résultat (strippé) : build/craft/<arch>/<paquet>.
#
# Travail dans ~/korin-craft (KORIN_CRAFT) :
#   <app>/            le dépôt de l'app à la révision épinglée (REV_<app> plus bas) ; .cargo/config.toml
#                     y branche les caisses patchées (écrit ici, à chaque lancement)
#   vendor/           les caisses de crates.io qui ne connaissent pas « korin » : tools/craft/patches/<caisse>-<version>.patch
#   craft-libs-<rev>/ craft-libs à la révision du Cargo.lock de l'app, avec tools/craft/patches/craft-libs.patch ;
#                     toutes ses caisses y sont redirigées (une seule copie de chaque type)
# Et dans la chaîne Rust ($K/vendor/libc-0.2.191) : la libc des projets (plus bas, « libc »).
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
ARCH=${ARCH:-x86_64}
APP=${1:-wordcraft}
PKG=${2:-wordcraft-cli}
W=${KORIN_CRAFT:-$HOME/korin-craft}
case $ARCH in
aarch64) K=${KORIN_RUST:-$HOME/.cache/korin-rust}; TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf- ;;
x86_64)  K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}; TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf- ;;
*)	echo "ARCH : aarch64 ou x86_64" >&2; exit 1 ;;
esac
[ -f "$K/env.sh" ] || { echo "craft : pas de chaîne Rust dans $K (ARCH=$ARCH bash tools/rust/build.sh)" >&2; exit 1; }
. "$K/env.sh"
OUT=$ROOT/build/craft/$ARCH
mkdir -p "$W/vendor" "$OUT" "$K/dl" "$K/vendor"

# les révisions essayées
REV_wordcraft=b5e0be787912d29e8ef106cba0cc538a6be64722
eval "REV=\${REV_$APP:-}"
[ -n "$REV" ] || { echo "craft : pas de révision épinglée pour $APP (REV_$APP dans $0)" >&2; exit 1; }

# 1. libc : la std garde la 0.2.185 de rust-src (tools/rust/libc.patch) ; les projets récents demandent plus
#    (nix 0.31 : ^0.2.186). La même libc.patch sur la 0.2.191, et deux entrées dans le [patch] de cargo
#    (libc pour la std, libc-0-2-191 pour les projets) : une seule entrée remplacerait celle de la std, qui
#    retomberait sur la libc du registre, sans « korin ». La 0.2.191 veut « unsafe » ou « safe » dans f! et
#    n'a plus safe_f! : ajoutés au module korin à l'application.
LV=0.2.191
L=$K/vendor/libc-$LV
if [ ! -f "$L/.korin" ] || [ "$ROOT/tools/rust/libc.patch" -nt "$L/.korin" ]; then
	[ -f "$K/dl/libc-$LV.crate" ] || curl -sSfL "https://static.crates.io/crates/libc/libc-$LV.crate" -o "$K/dl/libc-$LV.crate"
	(cd "$K/dl" && echo "65cd87c9a6d60536580b316ad985726104730a5151c99845f2490428ef91240a  libc-$LV.crate" | sha256sum -c - >/dev/null)
	rm -rf "$L"
	tar xzf "$K/dl/libc-$LV.crate" -C "$K/vendor"
	sed 's#vendor/libc-0\.2\.185/##g' "$ROOT/tools/rust/libc.patch" | (cd "$L" && patch -p1 -s --no-backup-if-mismatch)
	python3 -I - "$L/src/unix/korin/mod.rs" <<'EOF'
import re, sys
p = sys.argv[1]
out, mode = [], None
for line in open(p).read().split('\n'):
    if line.startswith('f! {'):
        mode = 'unsafe'
    elif line.startswith('safe_f! {'):
        mode, line = 'safe', 'f! {'
    elif line.startswith('}'):
        mode = None
    if mode:
        line = re.sub(r'^(\s*)pub (const )?fn ', r'\1pub \2%s fn ' % mode, line)
    out.append(line)
open(p, 'w').write('\n'.join(out))
EOF
	touch "$L/.korin"
fi
python3 -I - "$CARGO_HOME/config.toml" "$L" <<'EOF'
import sys
p, path = sys.argv[1], sys.argv[2]
t = open(p).read()
if 'libc-0-2-191' not in t:
    t = t.replace('[patch.crates-io]\n', '[patch.crates-io]\nlibc-0-2-191 = { path = "%s", package = "libc" }\n' % path, 1)
    open(p, 'w').write(t)
EOF

# 2. le dépôt de l'app
A=$W/$APP
[ -d "$A/.git" ] || git clone -q "https://github.com/storytold/$APP.git" "$A"
(cd "$A" && { git cat-file -e "$REV^{commit}" 2>/dev/null || git fetch -q origin; } && git checkout -q --detach "$REV")
# le Cargo.lock et le .cargo/config.toml de l'app, intacts : la construction précédente les a réécrits (patchs) ; un
# Cargo.lock repris d'une autre construction peut avoir changé de version une caisse patchée (« patch was not used »)
(cd "$A" && git checkout -q -- Cargo.lock && { git checkout -q -- .cargo/config.toml 2>/dev/null || rm -f .cargo/config.toml; })
APPCFG=$(cat "$A/.cargo/config.toml" 2>/dev/null || true)
cargo +korin fetch --manifest-path "$A/Cargo.toml" >/dev/null	# (le registre et craft-libs : les sources à patcher)

# 3. les caisses de crates.io patchées
REG=$(ls -d "$CARGO_HOME"/registry/src/index.crates.io-* | head -1)
CFG="[patch.crates-io]"
for p in "$ROOT"/tools/craft/patches/*-[0-9]*.patch; do
	c=$(basename "$p" .patch)
	name=${c%-*}
	if [ ! -f "$W/vendor/$c/.korin" ] || [ "$p" -nt "$W/vendor/$c/.korin" ]; then
		[ -d "$REG/$c" ] || curl -sSfL "https://static.crates.io/crates/$name/$c.crate" | tar xz -C "$REG"
		rm -rf "$W/vendor/$c"
		cp -r "$REG/$c" "$W/vendor/$c"
		(cd "$W/vendor/$c" && patch -p1 -s --no-backup-if-mismatch < "$p")
		touch "$W/vendor/$c/.korin"
	fi
	CFG="$CFG
$(echo "$c" | tr . -) = { path = \"$W/vendor/$c\", package = \"$name\" }"
done

# 4. craft-libs, patchée, et toutes ses caisses redirigées
CREV=$(grep -o 'craft-libs?rev=[0-9a-f]*#[0-9a-f]*' "$A/Cargo.lock" | sed 's/.*#//' | sort -u)
[ "$(echo "$CREV" | wc -l)" = 1 ] || { echo "craft : plusieurs révisions de craft-libs dans $APP : $CREV" >&2; exit 1; }
CL=$W/craft-libs-${CREV:0:7}
if [ ! -f "$CL/.korin" ] || [ "$ROOT/tools/craft/patches/craft-libs.patch" -nt "$CL/.korin" ]; then
	rm -rf "$CL"
	cp -a "$(ls -d "$CARGO_HOME"/git/checkouts/craft-libs-*/"${CREV:0:7}" | head -1)" "$CL"
	(cd "$CL" && patch -p1 -s --no-backup-if-mismatch < "$ROOT/tools/craft/patches/craft-libs.patch")
	touch "$CL/.korin"
fi
CFG="$CFG
$(python3 -I - "$A" "$CL" <<'EOF'
import os, sys, tomllib
app, root = sys.argv[1], sys.argv[2]
lock = tomllib.load(open(app + '/Cargo.lock', 'rb'))
ws = tomllib.load(open(app + '/Cargo.toml', 'rb'))
skip = set(ws.get('patch', {}).get('crates-io', {}))	# déjà des patchs de crates.io : pas de patch en chaîne
paths = {}
for d, dirs, files in os.walk(root):
    dirs[:] = [x for x in dirs if x not in ('target', '.git')]
    if 'Cargo.toml' in files:
        try:
            n = tomllib.load(open(d + '/Cargo.toml', 'rb')).get('package', {}).get('name')
        except Exception:
            continue
        if n:
            paths[n] = d
print('[patch."https://github.com/storytold/craft-libs"]')
for n in sorted({p['name'] for p in lock['package'] if 'craft-libs' in p.get('source', '')} - skip):
    if n not in paths:
        sys.exit('craft : %s absent de %s' % (n, root))
    print('%s = { path = "%s" }' % (n, paths[n]))
EOF
)"
mkdir -p "$A/.cargo"
printf '%s\n\n# tools/craft/build.sh (Korin)\n%s\n' "$APPCFG" "$CFG" > "$A/.cargo/config.toml"

# 5. la construction ; puis aucun de nos patchs ne doit être resté de côté
(cd "$A" && cargo korin build --release -p "$PKG")
UNUSED=$(python3 -I - "$A/Cargo.lock" "$W/vendor" <<'EOF'
import os, sys, tomllib
mine = {d.rsplit('-', 1)[0] for d in os.listdir(sys.argv[2])} | {'libc'}
unused = tomllib.load(open(sys.argv[1], 'rb')).get('patch', {}).get('unused', [])
print(' '.join(sorted('%s %s' % (u['name'], u['version']) for u in unused if u['name'] in mine and u['version'] != '0.2.185')))
EOF
)
[ -z "$UNUSED" ] || { echo "craft : patchs non pris (une autre version dans le Cargo.lock) : $UNUSED" >&2; exit 1; }
"${TC}strip" -o "$OUT/$PKG" "$A/target/$ARCH-unknown-korin/release/$PKG"
echo "craft : $OUT/$PKG ($(stat -c %s "$OUT/$PKG") octets)"
