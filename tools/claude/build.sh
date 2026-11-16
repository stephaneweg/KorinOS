#!/bin/bash
# build.sh -- claude-acp pour la carte (docs/CLAUDE-ACP.md) : l'agent de code sur l'API de Claude, en Python, que le
# client terminal lance par « agent --claude ». Dans WSL, depuis la racine du depot, apres
# tools/python/site-packages.sh (qui l'appelle a sa fin), ou seul sur un arbre deja fait :
#     bash tools/claude/build.sh                      le Pi (build/python/sdroot)
#     ARCH=x86_64 bash tools/claude/build.sh          Korin PC (build/pc/python/sdroot)
#     DEST=dossier bash tools/claude/build.sh         un autre arbre (sa racine : bin/, python/, .claude-acp/)
# Pose dans l'arbre (recopie sur la carte par build.ps1, dans celui du PC par tools/pc/sysroot_python.sh) :
#   python/lib/python3.14/site-packages/   le SDK anthropic et ce qui manquait de ses dependances (docstring-parser,
#                                          sniffio), le jiter de Korin en Python pur (tools/claude/jiter, a la place
#                                          du module Rust), et claude_acp (tools/claude/claude_acp)
#   bin/claude-acp                         la commande (#!/bin/python3)
#   .claude-acp/                           AGENTS.md et les competences (tools/claude/home.py) ; ni .env (la cle de
#                                          l'utilisateur) ni config.toml, ni sessions/
# Rien de natif : les memes fichiers pour les deux machines. Les autres dependances du SDK (httpx2, anyio, pydantic,
# opentelemetry-api, typing-extensions) sont celles de Vibe, deja dans site-packages.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$ROOT/tools/python/arch.sh"
BUILD_PYTHON=${BUILD_PYTHON:-/usr/bin/python3.14}
D=${DEST:-$OUT/sdroot}
SP=$D/python/lib/python3.14/site-packages
if [ -z "${DEST:-}" ] && [ ! -d "$SP/pydantic" ]; then
	echo "claude : lancer d'abord tools/python/site-packages.sh (pas de pydantic dans $SP)" >&2; exit 1
fi
mkdir -p "$SP" "$D/bin"
# pip : celui de la construction des modules (site-packages.sh), sinon un venv a nous
PIPY=$WORK/venv-ext/bin/python
if [ ! -x "$PIPY" ]; then
	PIPY=$HOME/.cache/korin-claude-venv/bin/python
	[ -x "$PIPY" ] || "$BUILD_PYTHON" -m venv "$HOME/.cache/korin-claude-venv"
fi
# (dans un dossier de WSL, puis copié : le déplacement de pip vers /mnt/c échouait au hasard, « Input/output
# error » sur des fichiers d'anthropic, 16/11 ; cp -r passe)
T=$(mktemp -d)
"$PIPY" -m pip install --disable-pip-version-check --no-cache-dir -q --target "$T" --no-compile --no-deps \
	--upgrade --platform $PLAT --implementation cp --python-version 3.14 --abi cp314 --only-binary=:all: \
	anthropic==2.8.0 docstring-parser==0.17.0 sniffio==1.3.1
for p in "$T"/*; do rm -rf "$SP/$(basename "$p")"; done
cp -r "$T"/. "$SP"/
rm -rf "$T"
rm -rf "$SP/bin"
for p in claude_acp jiter; do
	rm -rf "$SP/$p"
	cp -r "$ROOT/tools/claude/$p" "$SP/$p"
	find "$SP/$p" -name __pycache__ -prune -exec rm -rf {} +
done
# (pip voit jiter installe : il ne cherchera pas le module Rust pour anthropic)
rm -rf "$SP"/jiter-*.dist-info && mkdir -p "$SP/jiter-0.12.0.dist-info"
printf 'Metadata-Version: 2.1\nName: jiter\nVersion: 0.12.0\nSummary: jiter de Korin, en Python pur (tools/claude/jiter)\n' \
	> "$SP/jiter-0.12.0.dist-info/METADATA"
printf 'tools/claude/build.sh\n' > "$SP/jiter-0.12.0.dist-info/INSTALLER"
: > "$SP/jiter-0.12.0.dist-info/RECORD"
for p in anthropic docstring_parser sniffio claude_acp jiter; do
	"$BUILD_PYTHON" -I -m compileall -q -j0 --invalidation-mode checked-hash "$SP/$p" >/dev/null
done
printf '#!/bin/python3\nimport sys\nfrom claude_acp.main import main\nsys.exit(main())\n' > "$D/bin/claude-acp"
chmod +x "$D/bin/claude-acp"
"$BUILD_PYTHON" -I "$ROOT/tools/claude/home.py" "$D"
du -sh "$SP/anthropic" "$SP/claude_acp" "$D/.claude-acp"
