#!/bin/sh
# build-ext : construit sur le PC (WSL) des roues de PyPI pour Korin, modules C en .so charges par
# dlopen (docs/PYTHON-PORT.md, « Étape 3 »). A lancer depuis la racine du depot, apres
# sh tools/python/build.sh :
#     sh tools/python/build-ext.sh tree-sitter==0.26.0 cffi==2.0.0     # des noms de PyPI (sources)
#     sh tools/python/build-ext.sh ~/src/monpaquet                      # un dossier local
#     EXT_CFLAGS="-DHAVE_ENDIAN_H" sh tools/python/build-ext.sh ...     # options de plus pour le C
#     OUT_WHEELS=dossier sh tools/python/build-ext.sh ...               # build/python/wheels sinon
#
# Les roues sortent en <nom>-<version>-cp314-cp314-linux_aarch64.whl (abi3 si le paquet le
# demande) ; tools/python/site-packages.sh les installe sur la carte. Comment : le Python de
# construction (3.14 de WSL, dans un venv avec setuptools et wheel) lance pip wheel avec le
# sysconfig de la carte (_PYTHON_SYSCONFIGDATA_NAME), ou CC et LDSHARED sont la chaine d'Onyx et
# posix.specs : setuptools compile en -fPIC, relie en -shared (spec : libgcc et dlmod.o, pas de libc),
# nomme le module <nom>.cpython-314.so (EXT_SUFFIX de la carte) et lit les en-tetes de la carte.
# Le module ne porte ni libc ni libffi : il prend celles de python3 (hote -rdynamic) au dlopen.
# Chaque .so est verifie : tout symbole non defini doit etre exporte par python3.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=aarch64-onyx-elf-
WORK=${WORK:-$HOME/.cache/armpi-python}
OUT=${OUT:-$ROOT/build/python}
OUT_WHEELS=${OUT_WHEELS:-$OUT/wheels}
SPECS=${SPECS:-$ROOT/build/posix/lib/posix.specs}
BUILD_PYTHON=${BUILD_PYTHON:-/usr/bin/python3.14}
EXT_CFLAGS=${EXT_CFLAGS:-}
# versions des outils de construction (dans le venv, pas dans le Python de WSL)
SETUPTOOLS=setuptools==80.9.0
WHEEL=wheel==0.45.1

[ $# -ge 1 ] || { sed -n '2,9p' "$0" >&2; exit 2; }
export PATH="$TOOL/bin:$PATH"
L=$OUT/stage/python/lib/python3.14
INC=$OUT/stage/python/include/python3.14
[ -f "$L/_sysconfigdata__linux_.py" ] && [ -f "$INC/Python.h" ] ||
	{ echo "lancer d'abord sh tools/python/build.sh" >&2; exit 1; }
[ -f "$SPECS" ] || { echo "libposix absente : sh tools/posix/build.sh" >&2; exit 1; }
HOST=$OUT/sdroot/bin/python3

# ---- le venv de construction ----
V=$WORK/venv-ext
[ -x "$V/bin/python" ] || "$BUILD_PYTHON" -m venv "$V"
"$V/bin/python" -m pip install -q --disable-pip-version-check "$SETUPTOOLS" "$WHEEL"

# ---- le sysconfig croise : celui de la carte, avec la chaine d'Onyx et les en-tetes de stage ----
X=$WORK/cross
rm -rf "$X" && mkdir -p "$X/pkgconfig"
CC="${TC}gcc -specs=$SPECS"
CXX="${TC}g++ -specs=$SPECS"
"$BUILD_PYTHON" -I - "$L/_sysconfigdata__linux_.py" "$X/_sysconfigdata__korin_.py" <<EOF
import sys, pprint
ns = {}
exec(open(sys.argv[1]).read(), ns)
v = ns["build_time_vars"]
v.update({
    "CC": "$CC", "CXX": "$CXX",
    "LDSHARED": "$CC -shared -s", "BLDSHARED": "$CC -shared -s",
    "LDCXXSHARED": "$CXX -shared -nostdlib++ -s",
    "AR": "${TC}ar", "RANLIB": "${TC}ranlib", "STRIP": "${TC}strip", "READELF": "${TC}readelf",
    "INCLUDEPY": "$INC", "CONFINCLUDEPY": "$INC",
})
with open(sys.argv[2], "w") as f:
    f.write("# sysconfig de Korin pour construire sur le PC (tools/python/build-ext.sh)\nbuild_time_vars = ")
    pprint.pprint(v, stream=f)
EOF
# libffi : les en-tetes seulement ; ffi_* viennent de python3 (relie avec, pour _ctypes)
printf 'Name: libffi\nDescription: libffi de python3\nVersion: 3.8.0\nLibs:\nCflags: -I%s/include\n' \
	"$WORK/deps" > "$X/pkgconfig/libffi.pc"

# ---- les roues ----
mkdir -p "$OUT_WHEELS"
T=$WORK/ext-tmp
rm -rf "$T" && mkdir -p "$T"
env -u CC -u CXX -u LDSHARED -u LDFLAGS -u CPPFLAGS \
	_PYTHON_HOST_PLATFORM=linux-aarch64 _PYTHON_SYSCONFIGDATA_NAME=_sysconfigdata__korin_ \
	PYTHONPATH="$X" PKG_CONFIG_LIBDIR="$X/pkgconfig" PKG_CONFIG_PATH= CFLAGS="$EXT_CFLAGS" \
	"$V/bin/python" -m pip wheel --disable-pip-version-check --no-build-isolation --no-deps \
	--no-binary=:all: --no-cache-dir -w "$T" "$@"

# ---- verification : chaque symbole non defini d'un module est exporte par python3 ----
"${TC}nm" -D --defined-only "$HOST" | awk '{print $3}' | sort -u > "$T/host.syms"
bad=0
for w in "$T"/*.whl; do
	case "$w" in *-none-any.whl) cp "$w" "$OUT_WHEELS/"; continue;; esac
	d=$T/x/$(basename "$w" .whl)
	mkdir -p "$d"
	"$BUILD_PYTHON" -I -m zipfile -e "$w" "$d"
	for so in $(find "$d" -name "*.so"); do
		# (les faibles non definis valent 0 : seuls les « U » comptent)
		"${TC}nm" -D --undefined-only "$so" | awk '$1 == "U" {print $2}' | sed 's/@.*//' | sort -u > "$T/need.syms"
		miss=$(comm -23 "$T/need.syms" "$T/host.syms")
		if [ -n "$miss" ]; then
			echo "$(basename "$so") : symboles absents de python3 :" $miss >&2
			bad=1
		fi
		echo "$(basename "$w") : ${so#$d/} ($(stat -c %s "$so") octets)"
	done
	cp "$w" "$OUT_WHEELS/"
done
[ "$bad" = 0 ] || exit 1
