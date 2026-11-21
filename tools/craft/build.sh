#!/bin/bash
# craft : les apps Craft de storytold (Rust pur, egui) pour Korin, docs/CRAFT.md.
# Le PC seulement pour l'instant (décision du 20/11/2026) ; ARCH=aarch64 construit aussi pour le Pi, sans essai.
# Dans WSL, depuis la racine du dépôt, après ARCH=x86_64 bash tools/rust/build.sh :
#     bash tools/craft/build.sh                     wordcraft-cli pour le PC (x86_64)
#     bash tools/craft/build.sh <app> [paquet]      une app de tools/craft/apps.txt (son paquet par défaut)
# Résultat (strippé) : build/craft/<arch>/bin/<paquet> (tools/craft/pctest.py le met dans HOME:/craft) ; un paquet
# qui a son manifeste (tools/craft/app/<paquet>.txt) devient aussi build/craft/<arch>/apps/<paquet>.app.
#
# Travail dans ~/korin-craft (KORIN_CRAFT) :
#   <app>/            le dépôt de l'app à la révision de tools/craft/apps.txt ; .cargo/config.toml
#                     y branche les caisses patchées (écrit ici, à chaque lancement)
#   vendor/           les caisses de crates.io qui ne connaissent pas « korin » : tools/craft/patches/<caisse>-<version>.patch
#   craft-libs-<rev>/ craft-libs à chaque révision du Cargo.lock de l'app (une app en épingle souvent plusieurs, une
#                     par caisse), avec tools/craft/korin_cfg.py et korin_libs.py ; les
#                     caisses qui n'y sont qu'à une révision y sont redirigées (celles qui sont à plusieurs révisions
#                     sont des formats en Rust pur, laissés tels quels : un [patch] ne prend qu'une source par version)
# Et dans la chaîne Rust ($K/vendor/libc-0.2.191) : la libc des projets (plus bas, « libc »).
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
ARCH=${ARCH:-x86_64}
APP=${1:-wordcraft}
LINE=$(grep -E "^$APP[[:space:]]" "$ROOT/tools/craft/apps.txt" || true)
[ -n "$LINE" ] || { echo "craft : $APP absent de tools/craft/apps.txt" >&2; exit 1; }
REV=$(echo "$LINE" | awk '{print $2}')
if [ $# -ge 2 ]; then PKG=$2; elif [ $# = 0 ]; then PKG=wordcraft-cli; else PKG=$(echo "$LINE" | awk '{print $3}'); fi
W=${KORIN_CRAFT:-$HOME/korin-craft}
case $ARCH in
aarch64) K=${KORIN_RUST:-$HOME/.cache/korin-rust}; TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf- ;;
x86_64)  K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}; TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf- ;;
*)	echo "ARCH : aarch64 ou x86_64" >&2; exit 1 ;;
esac
[ -f "$K/env.sh" ] || { echo "craft : pas de chaîne Rust dans $K (ARCH=$ARCH bash tools/rust/build.sh)" >&2; exit 1; }
. "$K/env.sh"
OUT=$ROOT/build/craft/$ARCH/bin
mkdir -p "$W/vendor" "$OUT" "$K/dl" "$K/vendor"


# 1. libc : la 0.2.191 des projets (tools/rust/libc-projects.sh : libc.patch rejouée, seconde entrée du [patch])
LV=0.2.191
bash "$ROOT/tools/rust/libc-projects.sh" "$K"

# 2. le dépôt de l'app
A=$W/$APP
[ -d "$A/.git" ] || git clone -q "https://github.com/storytold/$APP.git" "$A"
(cd "$A" && { git cat-file -e "$REV^{commit}" 2>/dev/null || git fetch -q origin; } && git checkout -q --detach "$REV")
# les fichiers de l'app intacts (Cargo.lock, .cargo/config.toml, les retouches de korin_app.py) : la construction précédente les a réécrits (patchs) ; un
# Cargo.lock repris d'une autre construction peut avoir changé de version une caisse patchée (« patch was not used »)
(cd "$A" && git checkout -q -- . && { git ls-files --error-unmatch .cargo/config.toml >/dev/null 2>&1 || rm -f .cargo/config.toml; })
APPCFG=$(cat "$A/.cargo/config.toml" 2>/dev/null || true)
cargo +korin fetch --manifest-path "$A/Cargo.toml" >/dev/null	# (le registre et craft-libs : les sources à patcher)
# les retouches de l'app pour Korin (eframe de Korin, sans X11 ni wgpu) : après le fetch, qui a besoin de la fourche
python3 -I "$ROOT/tools/craft/korin_app.py" "$A" "$ROOT"
python3 -I "$ROOT/tools/craft/korin_cfg.py" "$A"
# les caisses que Korin patche à une version (libc 0.2.191, tools/craft/patches/<caisse>-<version>.patch) : une app
# qui en a une autre de la même série y est ramenée (cargo update --precise ; sinon « patch was not used », et la
# caisse du registre ne connaît pas « korin »)
for cv in libc-$LV $(cd "$ROOT/tools/craft/patches" && ls *-[0-9]*.patch | sed 's/\.patch$//'); do
	n=${cv%-*}; v=${cv##*-}
	for old in $(python3 -I - "$A/Cargo.lock" "$n" "$v" <<'PY'
import sys, tomllib
lock, n, v = tomllib.load(open(sys.argv[1], 'rb')), sys.argv[2], sys.argv[3]
series = lambda x: x.split('.')[:2] if x.startswith('0.') else x.split('.')[:1]
print(' '.join(p['version'] for p in lock['package'] if p['name'] == n and 'registry' in p.get('source', '')
               and p['version'] != v and series(p['version']) == series(v)))
PY
	); do
		(cd "$A" && cargo +korin update -q -p "$n@$old" --precise "$v" 2>/dev/null) || echo "craft : $n $old -> $v refusé" >&2
	done
done

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

# 4. craft-libs, patchée, à chacune de ses révisions ; les caisses à une seule révision y sont redirigées
for CREV in $(grep -o 'craft-libs?rev=[0-9a-f]*#[0-9a-f]*' "$A/Cargo.lock" | sed 's/.*#//' | sort -u); do
	CL=$W/craft-libs-${CREV:0:7}
	if [ ! -f "$CL/.korin" ] || [ "$ROOT/tools/craft/korin_cfg.py" -nt "$CL/.korin" ] || [ "$ROOT/tools/craft/korin_libs.py" -nt "$CL/.korin" ]; then
		rm -rf "$CL"
		cp -a "$(ls -d "$CARGO_HOME"/git/checkouts/craft-libs-*/"${CREV:0:7}" | head -1)" "$CL"
		python3 -I "$ROOT/tools/craft/korin_cfg.py" "$CL" >/dev/null
		python3 -I "$ROOT/tools/craft/korin_libs.py" "$CL"
		touch "$CL/.korin"
	fi
done
CFG="$CFG
$(python3 -I "$ROOT/tools/craft/craftlibs_patch.py" "$A" "$W")"
mkdir -p "$A/.cargo"
printf '%s\n\n# tools/craft/build.sh (Korin)\n%s\n' "$APPCFG" "$CFG" > "$A/.cargo/config.toml"

# 5. la construction ; puis aucun de nos patchs ne doit être resté de côté. mio (tokio) n'a pas de backend pour
#    Korin : poll() et un tube pour le réveil (ses deux repli « unsupported »), comme le port de Spotify
export RUSTFLAGS="${RUSTFLAGS:-} --cfg mio_unsupported_force_poll_poll --cfg mio_unsupported_force_waker_pipe"
(cd "$A" && cargo korin build --release -p "$PKG")
UNUSED=$(python3 -I - "$A/Cargo.lock" "$W/vendor" "$LV" <<'EOF'
import os, sys, tomllib
lock = tomllib.load(open(sys.argv[1], 'rb'))
mine = {d.rsplit('-', 1)[0] for d in os.listdir(sys.argv[2])} | {'libc'}
here = [(p['name'], p['version']) for p in lock['package'] if 'registry' in p.get('source', '')]
ours = {(d.rsplit('-', 1)[0], d.rsplit('-', 1)[1]) for d in os.listdir(sys.argv[2])} | {('libc', sys.argv[3])}
series = lambda x: x.split('.')[:2] if x.startswith('0.') else x.split('.')[:1]
# un patch non pris ne compte que si l'app a cette caisse, dans la même série, à une version que nous ne patchons pas
# (rustix : 1.1.5 et 1.1.6 patchées, l'une reste de côté)
print(' '.join(sorted('%s %s' % (u['name'], u['version']) for u in lock.get('patch', {}).get('unused', [])
                      if u['name'] in mine and u['version'] != '0.2.185'
                      and any(n == u['name'] and series(v) == series(u['version']) and (n, v) not in ours
                              for n, v in here))))
EOF
)
[ -z "$UNUSED" ] || { echo "craft : patchs non pris (une autre version dans le Cargo.lock) : $UNUSED" >&2; exit 1; }
"${TC}strip" -o "$OUT/$PKG" "$A/target/$ARCH-unknown-korin/release/$PKG"
echo "craft : $OUT/$PKG ($(stat -c %s "$OUT/$PKG") octets)"
# un paquet qui a son manifeste (tools/craft/app/<paquet>.txt) devient une app : build/craft/<arch>/apps/<paquet>.app
if [ -f "$ROOT/tools/craft/app/$PKG.txt" ]; then
	D=$ROOT/build/craft/$ARCH/apps/$PKG.app
	mkdir -p "$D"
	cp "$OUT/$PKG" "$D/main"
	cp "$ROOT/tools/craft/app/$PKG.txt" "$D/app.txt"
	echo "craft : $D"
fi
