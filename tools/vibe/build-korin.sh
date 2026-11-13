#!/usr/bin/env bash
# Étape 6 de docs/PYTHON.md : Mistral Vibe 3.1.x pour Korin, sur le PC (WSL). Fait la roue
# mistral_vibe-<v>-cp314-abi3-linux_aarch64.whl dans build/python/wheels :
#   * le cœur Rust du harness (mistralai_vibe_local_harness._native) en cdylib pour Korin, par
#     tools/python/build-rust-ext.sh (cargo korin, --no-default-features --features
#     extension-module : sans code mode, ni deno ni V8, tools/vibe/korin.patch) ;
#   * les paquets Python vibe et mistralai_vibe_local_harness ;
#   * un dist-info minimal (le harness lit la version de « mistral-vibe » par importlib.metadata).
# Et la liste des paquets purs du verrou de Vibe (uv.lock) pour la carte : WORK/requirements-korin.txt,
# que tools/python/site-packages.sh installe.
#
#   bash tools/vibe/build-korin.sh          (après sh tools/python/build.sh et bash tools/rust/build.sh)
#
# Sources téléchargées dans WORK (~/.cache/korin-vibe), jamais dans le dépôt. Il faut uv (~/.local/bin).
set -euo pipefail

VIBE_VERSION=3.1.2
# l'archive de l'étiquette v3.1.2 sur GitHub (sha256 de la première prise, 12/11/2026)
SHA256=eee545a48ca4c7c9070d731f28c2319cf318c2de3079e7882b42a47ecb583e6f
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/tools/vibe
WORK=${WORK:-$HOME/.cache/korin-vibe}
OUT=${OUT:-$ROOT/build/python}
BUILD_PYTHON=${BUILD_PYTHON:-/usr/bin/python3.14}
export PATH="$HOME/.local/bin:$PATH"

# Paquets du verrou qu'on n'installe pas depuis PyPI : les natifs faits par les étapes 3 et 5
# (roues de build/python/wheels, que site-packages.sh installe à part), ceux qui ont un remplaçant
# (uuid-utils -> uuid.uuid7 de 3.14, watchfiles -> tools/vibe/watchfiles), et ceux dont Korin n'a
# pas besoin (setproctitle, miniaudio : la voix ; secretstorage : pas de trousseau D-Bus).
SKIP='^(uuid-utils|watchfiles|setproctitle|miniaudio|secretstorage|cryptography|cffi|pycparser|pydantic-core|rpds-py|tree-sitter|tree-sitter-bash)=='

mkdir -p "$WORK/dl"
tarball=$WORK/dl/mistral-vibe-$VIBE_VERSION.tar.gz
[ -f "$tarball" ] || curl -fsSL -o "$tarball" \
	"https://github.com/mistralai/mistral-vibe/archive/refs/tags/v$VIBE_VERSION.tar.gz"
echo "$SHA256  $tarball" | sha256sum -c - >/dev/null

echo "== sources de Vibe $VIBE_VERSION + korin.patch"
S=$WORK/mistral-vibe
rm -rf "$S" "$WORK/src" && mkdir -p "$WORK/src"
tar -xzf "$tarball" -C "$WORK/src"
patch -d "$WORK/src/mistral-vibe-$VIBE_VERSION" -p1 -s --no-backup-if-mismatch < "$HERE/korin.patch"

# les paquets purs du verrou (pour site-packages.sh)
(cd "$WORK/src/mistral-vibe-$VIBE_VERSION" && uv export --frozen --no-hashes --no-dev --no-emit-project \
	--no-annotate -o "$WORK/requirements-lock.txt" >/dev/null)
grep -vE "$SKIP" "$WORK/requirements-lock.txt" | grep -vE '^#|^ *$' > "$WORK/requirements-korin.txt"

# Le dossier que build-rust-ext.sh construit : le harness (Cargo.lock compris) et, à la racine, les
# deux paquets Python ; un pyproject.toml à nous (celui de Vibe passe par maturin et .native-build).
src=$WORK/src/mistral-vibe-$VIBE_VERSION
mkdir -p "$S"
cp -r "$src/harness" "$S/harness"
cp -r "$src/vibe" "$S/vibe"
rm -rf "$S/vibe/cli-rust"		# (l'interface vibe-rs, en Rust : pas sous Korin)
cp -r "$src/harness/runtimes/python/python/mistralai_vibe_local_harness" "$S/"
cp "$src/LICENSE" "$S/"
cat > "$S/pyproject.toml" <<EOF
[project]
name = "mistral-vibe"
version = "$VIBE_VERSION"

[tool.maturin]
manifest-path = "harness/core/Cargo.toml"
module-name = "mistralai_vibe_local_harness._native"
python-source = "."
python-packages = ["vibe", "mistralai_vibe_local_harness"]
no-default-features = true
features = ["extension-module"]
EOF
printf 'Metadata-Version: 2.4\nName: mistral-vibe\nVersion: %s\nRequires-Python: >=3.12\n' \
	"$VIBE_VERSION" > "$S/PKG-INFO"
find "$S" -name __pycache__ -prune -exec rm -rf {} +

# pas de deno ni de V8 dans l'arbre de la construction de Korin
. "${KORIN_RUST:-$HOME/.cache/korin-rust}/env.sh"
if (cd "$S/harness/core" && cargo tree --no-default-features --features extension-module -e normal \
		| grep -E 'deno_core|deno_ast|\bv8\b'); then
	echo "erreur : deno ou v8 encore dans l'arbre des dépendances" >&2
	exit 1
fi

echo "== le harness en cdylib pour Korin"
bash "$ROOT/tools/python/build-rust-ext.sh" "$S"
ls -l "$OUT"/wheels/mistral_vibe-"$VIBE_VERSION"-*.whl
