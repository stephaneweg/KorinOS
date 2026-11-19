#!/bin/sh
# Python et Vibe dans l'arbre système du PC (docs/etude/PC-PILOTES.md, « Python et Vibe pour le PC »), dans WSL,
# après tools/pc/sysroot.sh (qui refait build/pc/sysroot) ; comme build.ps1 pour le Pi :
#     bin/python3, python/            CPython 3.14 (ARCH=x86_64 sh tools/python/build.sh), site-packages
#                                     (ARCH=x86_64 sh tools/python/site-packages.sh : pip, modules C et Rust, Vibe)
#     bin/vibe-acp, bin/vibe-app-server   les commandes de Vibe (site-packages.sh)
#     bin/vibe-account                les comptes de fournisseurs et le modèle (tools/vibe/vibe_account.py)
#     bin/agent                       le client terminal de Vibe (ARCH=x86_64 sh tools/agent/build.sh)
#     apps/vibe.app/                  l'app Vibe (sh tools/pc/apps.sh vibe), avec son icône
#     .vibe/                          AGENTS.md et les compétences de Korin (tools/vibe/home.py) ; ni .env ni
#                                     config.toml : ils sont à l'utilisateur
#   TESTS=1 : Lib/test et les modules d'essai (build/pc/python/tests), les scripts d'essai dans home/pytest.
#
#   wsl sh tools/pc/sysroot.sh && wsl sh tools/pc/sysroot_python.sh [arbre]
# Ce qui n'est pas construit est sauté et dit. Écrire dans build/pc/sysroot pendant que d'autres scripts y
# travaillent : sous le verrou (mkdir build/pc.lock && { ...; rmdir build/pc.lock; }).
set -e
cd "$(dirname "$0")/../.."

OUT=${1:-build/pc/sysroot}
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : wsl sh tools/pc/sysroot.sh)"; exit 1; }
n0=$(du -sk "$OUT" | cut -f1)
P=build/pc/python/sdroot
if [ -x "$P/bin/python3" ]; then
	cp -r "$P"/. "$OUT/"
	[ -d "$P/python/lib/python3.14/site-packages/pip" ] || echo "sysroot_python : site-packages vide (ARCH=x86_64 sh tools/python/site-packages.sh)"
	[ -f "$P/bin/vibe-acp" ] || echo "sysroot_python : pas de Vibe dans site-packages (site-packages.sh sans NO_RUST)"
	[ -f "$P/bin/claude-acp" ] || echo "sysroot_python : pas de claude-acp (ARCH=x86_64 bash tools/claude/build.sh)"
	[ -f "$P/bin/mai-acp" ] || echo "sysroot_python : pas de mai-acp (ARCH=x86_64 bash tools/oai/build.sh)"
else
	echo "sysroot_python : pas de Python (ARCH=x86_64 sh tools/python/build.sh)"
fi
cp tools/vibe/vibe_account.py "$OUT/bin/vibe-account"
cp tools/vibe/vibe_diag.py "$OUT/bin/vibe-diag"
if [ -f build/pc/agent/sdroot/bin/agent ]; then cp build/pc/agent/sdroot/bin/agent "$OUT/bin/"; else
	echo "sysroot_python : pas d'agent (ARCH=x86_64 sh tools/agent/build.sh)"; fi
A=build/pc/apps/sdroot/apps/vibe.app
if [ -f "$A/main" ]; then
	mkdir -p "$OUT/apps/vibe.app"
	cp -r "$A"/. "$OUT/apps/vibe.app/"
	[ ! -f tools/icons/rendered/vibe.rgba ] || cp tools/icons/rendered/vibe.rgba "$OUT/apps/vibe.app/icon.rgba"
else
	echo "sysroot_python : pas de vibe.app (sh tools/pc/apps.sh vibe)"
fi
# l'app Claude (sh tools/pc/apps.sh claude : le source de Vibe, profil Claude), avec son icône
C=build/pc/apps/sdroot/apps/claude.app
if [ -f "$C/main" ]; then
	mkdir -p "$OUT/apps/claude.app"
	cp -r "$C"/. "$OUT/apps/claude.app/"
	cp tools/icons/rendered/claude.rgba "$OUT/apps/claude.app/icon.rgba"
else
	echo "sysroot_python : pas de claude.app (sh tools/pc/apps.sh claude)"
fi
# l'app MAI (sh tools/pc/apps.sh mai : le source de Vibe, profil MAI), avec son icône
C=build/pc/apps/sdroot/apps/mai.app
if [ -f "$C/main" ]; then
	mkdir -p "$OUT/apps/mai.app"
	cp -r "$C"/. "$OUT/apps/mai.app/"
	cp tools/icons/rendered/mai.rgba "$OUT/apps/mai.app/icon.rgba"
else
	echo "sysroot_python : pas de mai.app (sh tools/pc/apps.sh mai)"
fi
python3 -I tools/vibe/home.py "$OUT"
if [ -n "$TESTS" ] && [ -d build/pc/python/tests ]; then
	L=$OUT/python/lib/python3.14
	cp -r build/pc/python/tests/test "$L/"
	cp build/pc/python/tests/lib-dynload/* "$L/lib-dynload/"
	[ ! -d build/pc/python/tests/site-packages ] || cp -r build/pc/python/tests/site-packages/. "$L/site-packages/"
	mkdir -p "$OUT/home/pytest"
	cp -r tools/python/tests/. "$OUT/home/pytest/"
	rm -rf "$OUT/home/pytest/panic"
	[ ! -d build/pc/python/tests/rust-ext ] || cp -r build/pc/python/tests/rust-ext "$OUT/home/pytest/"
	cp tools/vibe/acp_client.py tools/vibe/mcp_test_server.py tools/vibe/fakellm.py tools/vibe/accounttest.py tools/agent/ptytest.py \
		tools/agent/skilltest.py tools/claude/acptest.py tools/claude/fakeapi.py tools/oai/oaitest.py tools/oai/fakeoai.py \
		"$OUT/home/pytest/"
fi
n=$(du -sk "$OUT" | cut -f1)
echo "sysroot_python : $OUT, $(( (n - n0) / 1024 )) Mo de plus"

# les outils de développement sous /devtools (arbre unique)
sh tools/devtools_move.sh "$OUT"
