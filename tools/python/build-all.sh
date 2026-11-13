#!/bin/bash
# build-all.sh -- tout ce qu'il faut pour Python et Vibe sur le Pi, depuis un clone neuf (docs/PYTHON-SUITE.md §2.2) :
# la chaine, src/extra, libposix, OpenSSL, CPython, la cible Rust, site-packages (Vibe compris), /gcc et les relais des
# kits, agent, l'app Vibe. A lancer dans WSL (Ubuntu) depuis la racine du depot :
#
#     bash tools/python/build-all.sh             ce qui manque seulement
#     bash tools/python/build-all.sh --force     tout refaire
#     bash tools/python/build-all.sh --list      les etapes et leur etat, sans rien faire
#     bash tools/python/build-all.sh --from rust reprendre a une etape (elle et les suivantes sont refaites)
#
# Ensuite, sous Windows : build.ps1 (la carte, avec les competences de Vibe : tools/vibe/home.py), puis, pour les
# essais, python -I tools/python/testimage.py. La publication des paquets reste a part (docs/PAQUETS.md : la cle de
# signature n'est jamais ici).
#
# Chaque etape est un script du depot, qui telecharge ses sources (versions fixees, sha256 ou revision git verifies)
# et applique ses correctifs ; ce script ne fait que les enchainer, dans l'ordre, et s'arrete a la premiere erreur.
# Une etape est sautee si ce qu'elle produit est la ; une etape refaite refait celles qui en dependent.
# PAS ENCORE ESSAYE de bout en bout sur une machine vierge (13/11/2026).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
TOOL=/opt/toolchains/aarch64-onyx-elf-14.2
BUILD_PYTHON=${BUILD_PYTHON:-/usr/bin/python3.14}
FORCE=0; LIST=0; FROM=""
while [ $# -gt 0 ]; do
	case "$1" in
	--force) FORCE=1 ;;
	--list) LIST=1 ;;
	--from) FROM=${2:?--from ETAPE}; shift ;;
	-h|--help) sed -n '2,19p' "$0"; exit 0 ;;
	*) echo "build-all : option inconnue : $1 (--help)" >&2; exit 2 ;;
	esac
	shift
done

say () { printf '\n\033[1m== %s\033[0m\n' "$*"; }
die () { printf '\nbuild-all : \033[31m%s\033[0m\n' "$*" >&2; exit 1; }

# ---- les prerequis, tous verifies avant de commencer ------------------------------------------------------------
check () {
	local miss=()
	[ "$(uname -s)" = Linux ] || miss+=("Linux (WSL Ubuntu sous Windows) : ce script ne tourne pas ailleurs")
	for c in curl patch git make xz pkg-config sha256sum tar unzip; do
		command -v "$c" >/dev/null 2>&1 || miss+=("$c (sudo apt install ${c/sha256sum/coreutils})")
	done
	[ -x "$BUILD_PYTHON" ] || miss+=("python3.14 en $BUILD_PYTHON (le meme 3.14 que la cible ; Ubuntu 24.04 : PPA deadsnakes, ou BUILD_PYTHON=chemin)")
	command -v uv >/dev/null 2>&1 || [ -x "$HOME/.local/bin/uv" ] || miss+=("uv (curl -LsSf https://astral.sh/uv/install.sh | sh)")
	if [ ! -d "$TOOL" ]; then
		mkdir -p /opt/toolchains 2>/dev/null && [ -w /opt/toolchains ] || \
			miss+=("/opt/toolchains inscriptible (sudo mkdir -p /opt/toolchains && sudo chown \$USER /opt/toolchains)")
	fi
	if [ ${#miss[@]} -gt 0 ]; then
		printf 'build-all : il manque :\n' >&2
		printf '  - %s\n' "${miss[@]}" >&2
		exit 1
	fi
	export PATH="$HOME/.local/bin:$PATH"
	case "$ROOT" in /mnt/*) echo "(clone sur Windows : garder un chemin court, LongPathsEnabled n'est pas mis : PYTHON-SUITE.md §2.3)" ;; esac
}

# ---- les etapes : nom, ce qui prouve qu'elle est faite, dont elle depend, la commande --------------------------------
# (un temoin vide : l'etape est toujours lancee ; son script garde ce qui est deja fait)
STEPS=(toolchain extra posix openssl python rust sitepackages gcc make kits agent vibeapp)
declare -A DONE DEPS CMD DESC
DESC[toolchain]="la chaine aarch64-onyx-elf 14.2";      DONE[toolchain]="$TOOL/bin/aarch64-onyx-elf-gcc"
DEPS[toolchain]="";                                       CMD[toolchain]="sh tools/extra/fetch.sh toolchain"
DESC[extra]="src/extra (zlib, SQLite... sans WebKit)";  DONE[extra]=""
DEPS[extra]="";                                           CMD[extra]="SANS_WEBKIT=1 sh tools/extra/fetch.sh"
DESC[posix]="libposix";                                   DONE[posix]="build/posix/lib/libposix.a"
DEPS[posix]="toolchain";                                  CMD[posix]="sh tools/posix/build.sh"
DESC[openssl]="OpenSSL 3.5.9";                            DONE[openssl]="build/openssl/lib/libssl.a"
DEPS[openssl]="posix";                                    CMD[openssl]="sh tools/openssl/build.sh"
DESC[python]="CPython 3.14.10 (vide build/python/sdroot)"; DONE[python]="build/python/sdroot/bin/python3"
DEPS[python]="posix openssl extra";                       CMD[python]="sh tools/python/build.sh"
DESC[rust]="la cible Rust aarch64-unknown-korin";        DONE[rust]="$HOME/.cache/korin-rust/env.sh"
DEPS[rust]="posix";                                       CMD[rust]="SKIP_TESTS=1 bash tools/rust/build.sh"
DESC[sitepackages]="site-packages, Vibe et son harness"; DONE[sitepackages]="build/python/sdroot/python/lib/python3.14/site-packages/vibe"
DEPS[sitepackages]="python rust";                         CMD[sitepackages]="BUILD_PYTHON=$BUILD_PYTHON sh tools/python/site-packages.sh"
DESC[gcc]="GCC 14.2 pour Korin (/gcc)";                  DONE[gcc]=""
DEPS[gcc]="posix";                                        CMD[gcc]="sh tools/gcc/build.sh"
DESC[make]="GNU make pour Korin";                         DONE[make]="build/gcc/sdroot/gcc/bin/make"
DEPS[make]="gcc";                                         CMD[make]="sh tools/gcc/make.sh"
DESC[kits]="les relais et en-tetes des kits (/gcc)";     DONE[kits]="build/gcc/sdroot/gcc/lib/libuikit.a"
DEPS[kits]="gcc";                                         CMD[kits]="sh tools/gcc/kits.sh"
DESC[agent]="libacp et agent";                            DONE[agent]="build/agent/sdroot/bin/agent"
DEPS[agent]="posix";                                      CMD[agent]="sh tools/agent/build.sh"
DESC[vibeapp]="l'app Vibe (vibe.app)";                    DONE[vibeapp]="build/office/sdroot/apps/vibe.app/main"
DEPS[vibeapp]="kits";                                     CMD[vibeapp]="sh tools/office/build.sh vibe"

if [ -n "$FROM" ] && [ -z "${CMD[$FROM]+x}" ]; then die "etape inconnue : $FROM (${STEPS[*]})"; fi

if [ $LIST = 1 ]; then
	for s in "${STEPS[@]}"; do
		st="a faire"; { [ -z "${DONE[$s]}" ] && st="toujours (reprend ou il en etait)"; } || { [ -e "${DONE[$s]}" ] && st="fait"; }
		printf '%-13s %-45s %s\n' "$s" "${DESC[$s]}" "$st"
	done
	exit 0
fi

check
declare -A RAN
started=0; [ -z "$FROM" ] && started=1
t0=$(date +%s)
for s in "${STEPS[@]}"; do
	[ "$s" = "$FROM" ] && started=1
	need=$FORCE
	[ -n "$FROM" ] && [ $started = 1 ] && need=1
	[ -z "${DONE[$s]}" ] && need=1
	[ -n "${DONE[$s]}" ] && [ ! -e "${DONE[$s]}" ] && need=1
	for d in ${DEPS[$s]}; do [ -n "${RAN[$d]:-}" ] && need=1; done
	if [ $need = 0 ]; then echo "-- $s : deja fait (${DONE[$s]})"; continue; fi
	say "$s : ${DESC[$s]}   ($(( ($(date +%s) - t0) / 60 )) min)"
	echo "\$ ${CMD[$s]}"
	if ! bash -c "${CMD[$s]}"; then
		die "echec a l'etape « $s » (${CMD[$s]}) ; corriger puis : bash tools/python/build-all.sh --from $s"
	fi
	[ -n "${DONE[$s]}" ] && [ ! -e "${DONE[$s]}" ] && die "l'etape « $s » a fini sans produire ${DONE[$s]}"
	RAN[$s]=1
done

say "fini en $(( ($(date +%s) - t0) / 60 )) min"
cat <<EOF
Ensuite, sous Windows (PowerShell, a la racine du depot) :
  .\\build.ps1                              la carte : build\\sdroot, avec Python, Vibe, agent, vibe.app, /gcc et les
                                           competences de Vibe (tools/vibe/home.py)
  python -I tools/python/testimage.py      la carte d'essai de QEMU (build/python/sd.img)
EOF
