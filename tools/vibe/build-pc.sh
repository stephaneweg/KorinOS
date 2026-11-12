#!/usr/bin/env bash
# Étape 0 de docs/PYTHON.md : Mistral Vibe 3.1.x sur le PC (Linux, WSL), sans Korin,
# avec tools/vibe/korin.patch : harness sans deno_core/deno_ast (pas de code mode,
# V8 jamais lié), uuid-utils -> uuid.uuid7, watchfiles -> sondage en Python,
# setproctitle, miniaudio et cryptography absents.
#
#   tools/vibe/build-pc.sh [WORK]        (WORK par défaut : ~/vibe-pc, hors du dépôt)
#
# Résultat : WORK/venv (Python 3.14) avec WORK/venv/bin/vibe-acp. Les sources de
# Vibe sont téléchargées dans WORK, jamais dans le dépôt.
# Il faut : python3.14, curl, patch, rustup (la chaîne 1.97.1 de Vibe s'installe
# seule) et uv (~/.local/bin/uv).
set -euo pipefail

VIBE_VERSION=3.1.2
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="${1:-$HOME/vibe-pc}"
SRC="$WORK/src/mistral-vibe-$VIBE_VERSION"
VENV="$WORK/venv"
PY="${PYTHON:-python3.14}"
export PATH="$HOME/.local/bin:$HOME/.cargo/bin:$PATH"

# Paquets du verrou de Vibe qu'on n'installe pas (natifs) : leurs remplaçants
# sont dans le patch (uuid) ou dans tools/vibe/watchfiles.
SKIP='^(uuid-utils|watchfiles|setproctitle|miniaudio|cryptography|cffi|pycparser|secretstorage)=='

mkdir -p "$WORK/dl" "$WORK/src"
tarball="$WORK/dl/mistral-vibe-$VIBE_VERSION.tar.gz"
if [ ! -f "$tarball" ]; then
    curl -fsSL -o "$tarball" \
        "https://github.com/mistralai/mistral-vibe/archive/refs/tags/v$VIBE_VERSION.tar.gz"
fi

echo "== sources de Vibe $VIBE_VERSION + korin.patch"
rm -rf "$SRC"
tar -xzf "$tarball" -C "$WORK/src"
patch -d "$SRC" -p1 --no-backup-if-mismatch < "$HERE/korin.patch"

echo "== harness sans code mode (cargo)"
core="$SRC/harness/core"
export CARGO_TARGET_DIR="$WORK/target"   # gardé d'une construction à l'autre
(cd "$core" && cargo build --release --locked --no-default-features --features extension-module)
if (cd "$core" && cargo tree --no-default-features --features extension-module -e normal \
        | grep -E 'deno_core|deno_ast|\bv8\b'); then
    echo "erreur : deno ou v8 encore dans l'arbre des dépendances" >&2
    exit 1
fi
so="$CARGO_TARGET_DIR/release/lib_native.so"

echo "== venv Python ($PY)"
rm -rf "$VENV"
uv venv -q -p "$PY" "$VENV"
(cd "$SRC" && uv export --frozen --no-hashes --no-dev --no-emit-project --no-annotate \
    -o "$WORK/requirements-lock.txt" >/dev/null)
grep -vE "$SKIP" "$WORK/requirements-lock.txt" > "$WORK/requirements-korin.txt"
uv pip install -q -p "$VENV/bin/python" --no-deps -r "$WORK/requirements-korin.txt"

echo "== Vibe dans site-packages (sans maturin)"
site="$("$VENV/bin/python" -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])')"
cp -r "$SRC/vibe" "$site/vibe"
rm -rf "$site/vibe/cli-rust"
cp -r "$SRC/harness/runtimes/python/python/mistralai_vibe_local_harness" "$site/"
cp "$so" "$site/mistralai_vibe_local_harness/_native.abi3.so"
cp -r "$HERE/watchfiles" "$site/watchfiles"
# Sans maturin, pas de dist-info : le harness lit pourtant la version de
# « mistral-vibe » par importlib.metadata (User-Agent MCP, _mcp_transport.py).
info="$site/mistral_vibe-$VIBE_VERSION.dist-info"
mkdir -p "$info"
printf 'Metadata-Version: 2.4\nName: mistral-vibe\nVersion: %s\nRequires-Python: >=3.12\n' \
    "$VIBE_VERSION" > "$info/METADATA"
echo "korin-build-pc" > "$info/INSTALLER"
: > "$info/RECORD"
for entry in "vibe-acp:vibe.acp.entrypoint" "vibe-app-server:vibe.app_server.stdio"; do
    name="${entry%%:*}"; module="${entry#*:}"
    cat > "$VENV/bin/$name" <<EOF
#!$VENV/bin/python
import sys
from $module import main
sys.exit(main())
EOF
    chmod +x "$VENV/bin/$name"
done

echo "== vérification"
"$VENV/bin/python" - <<'EOF'
import importlib.util, sys
from mistralai_vibe_local_harness import _native
assert _native.CODE_MODE is False, "le harness a encore le code mode"
for absent in ("uuid_utils", "setproctitle", "miniaudio", "cryptography", "cffi"):
    assert importlib.util.find_spec(absent) is None, absent
import watchfiles
assert watchfiles.VERSION.endswith("korin.poll")
import importlib.metadata
assert importlib.metadata.version("mistral-vibe") == __import__("vibe").__version__
import vibe.acp.entrypoint, vibe.app_server.stdio
print("ok : Python", sys.version.split()[0], "; _native sans code mode ; vibe", __import__("vibe").__version__)
EOF
echo "vibe-acp : $VENV/bin/vibe-acp"
