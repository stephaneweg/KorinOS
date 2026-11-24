#!/bin/bash
# openjarvis/install.sh : installe OpenJarvis (github.com/open-jarvis/OpenJarvis, Apache 2.0) sur le Python 3.14 de
# Korin PC, en natif (sans linuxhost), branché sur llama-server (paquet llamacpp) : tools/openjarvis/README.md.
#     bash tools/openjarvis/install.sh [DOSSIER]        (par défaut ~/openjarvis)
# Produit, dans DOSSIER :
#   OpenJarvis/            les sources (commit OJ_COMMIT) avec openjarvis-korin.patch
#   pyenv/                 OpenJarvis et ses dépendances (PYTHONUSERBASE : pas de venv sans liens symboliques)
#   jarvis                 le lanceur (règle l'environnement, puis la commande jarvis)
#   start-llama-server.sh  lance llama-server sur le port 8080 (moteur « llamacpp » d'OpenJarvis)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-${OPENJARVIS_DIR:-$HOME/openjarvis}}
OJ_REPO=${OJ_REPO:-https://github.com/open-jarvis/OpenJarvis}
OJ_COMMIT=${OJ_COMMIT:-484de900c7238442d4e049e185de6af679519192}   # 23/11/2026, celui qui a été essayé

say () { echo "openjarvis : $*"; }

mkdir -p "$DEST"
DEST=$(cd "$DEST" && pwd)

# pip lance « rustc --version » pour son User-Agent et le tue au bout de 0,5 s : sous Korin, ce SIGKILL pendant une
# lecture a fait tomber ext4fs deux fois (corrigé dans pc 2026.11.98, gardé par prudence). Sans rustc dans le PATH,
# pip n'en lance aucun.
PATH=$(printf '%s' "$PATH" | tr ':' '\n' | grep -v '/devtools/rust' | paste -sd: -)
export PATH
# /tmp (en mémoire) refuse les noms longs (la roue d'OpenJarvis) : les temporaires vont sur le disque.
mkdir -p "$DEST/.tmp"
export TMPDIR=$DEST/.tmp
export PYTHONUSERBASE=$DEST/pyenv
# version fixe : hatch-vcs n'a pas d'étiquette dans un clone partiel, et le numéro sert à l'avertissement de mise à jour
export SETUPTOOLS_SCM_PRETEND_VERSION=0.0.1+korin
# aiohttp, yarl, frozenlist, propcache, multidict : leurs versions en pur Python (pas de compilation de Cython)
export AIOHTTP_NO_EXTENSIONS=1 YARL_NO_EXTENSIONS=1 FROZENLIST_NO_EXTENSIONS=1 PROPCACHE_NO_EXTENSIONS=1 MULTIDICT_NO_EXTENSIONS=1

S=$DEST/OpenJarvis
if [ ! -d "$S/.git" ]; then
	say "récupération de $OJ_REPO"
	git clone --filter=blob:none --no-checkout "$OJ_REPO" "$S"
fi
git -C "$S" cat-file -e "$OJ_COMMIT^{commit}" 2>/dev/null || git -C "$S" fetch origin
say "commit $OJ_COMMIT"
git -C "$S" checkout -q -f "$OJ_COMMIT"
git -C "$S" apply "$HERE/openjarvis-korin.patch"

PIP="python3 -m pip install --user --disable-pip-version-check"
say "jiter (remplaçant en pur Python)"
$PIP --no-build-isolation "$HERE/jiter-shim"
say "OpenJarvis (sans ses dépendances : Python 3.14, et lxml, datasets, psutil, ddgs ne se construisent pas)"
$PIP --no-deps --ignore-requires-python --force-reinstall "$S"
say "dépendances en pur Python"
$PIP --ignore-requires-python \
	'aiohttp>=3.14.4' 'httpx>=0.27' 'filelock>=4.2.1' 'fsspec>=2026.6.0' 'openai>=1.30' 'nvidia-ml-py>=12.560.30' \
	'posthog>=3.0' 'python-telegram-bot>=22.8' 'rich>=13' 'pygments>=2.20.0' 'tomlkit>=0.12' 'tzdata>=2025.3' \
	'multidict>=6.9.1' 'croniter>=2.0' 'requests>=2.33.0' 'urllib3>=2.8.0' 'websockets>=15.0.1' 'click>=8' \
	'fastapi>=0.110'

say "SQLite sans WAL"
SP=$(python3 -c 'import site; print(site.getusersitepackages())')
mkdir -p "$SP"
cp "$HERE/sqlite-nowal/korin_sqlite_nowal.py" "$HERE/sqlite-nowal/korin_sqlite_nowal.pth" "$SP/"

say "lanceurs"
cp "$HERE/jarvis" "$DEST/jarvis"
cp "$HERE/start-llama-server.sh" "$DEST/start-llama-server.sh"
chmod 755 "$DEST/jarvis" "$DEST/start-llama-server.sh" 2>/dev/null ||
	say "chmod refusé par le système de fichiers : lancer « bash $DEST/jarvis » et « bash $DEST/start-llama-server.sh »"

say "fait. Ensuite :"
cat <<EOF
  $DEST/start-llama-server.sh -m MODELE.gguf -a NOM &     (paquet llamacpp ; port 8080)
  $DEST/jarvis init --engine llamacpp --host http://localhost:8080 --no-download --no-scan --no-analytics
  puis, dans ~/.openjarvis/config.toml : default_model = "NOM"   (jarvis init voit 0 Go de mémoire, sans /proc)
  $DEST/jarvis ask "Bonjour"
EOF
