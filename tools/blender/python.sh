#!/bin/sh
# python.sh : le Python que Blender embarque (3.13 exactement, versions.cmake : PYTHON_VERSION), construit pour
# Korin PC par tools/python/build.sh (le même port que /devtools/python 3.14, mêmes retouches), avec libpython3.13.a.
#
#   sh tools/blender/python.sh          (dans WSL, depuis la racine du dépôt)
#
# 1. un Python 3.13 de l'hôte (la construction croisée de CPython en demande un de la même version) :
#    ~/korin-blender/host-python, construit depuis la même archive ;
# 2. CPython 3.13 pour Korin : travail dans ~/korin-blender/python-work, sortie dans x86_64/python
#    (stage/devtools/python : bin, lib/libpython3.13.a, lib/python3.13, include/python3.13).
set -e
. "$(dirname "$0")/env.sh"
VER=$(python3 -I "$BL/versions.py" "$BSRC/build_files/build_environment/cmake/versions.cmake" PYTHON | cut -d' ' -f2 |
	sed 's/^Python-//; s/\.tar\.xz$//')
SHORT=${VER%.*}
ARC=$DL/Python-$VER.tar.xz
[ -f "$ARC" ] || { echo "python.sh : pas de $ARC (sh tools/blender/fetch.sh PYTHON)" >&2; exit 1; }
HOSTPY=$K/host-python

if [ ! -x "$HOSTPY/bin/python$SHORT" ]; then
	echo "== Python $VER de l'hôte"
	rm -rf "$K/host-python-build"
	mkdir -p "$K/host-python-build"
	tar -xJf "$ARC" -C "$K/host-python-build" --strip-components=1
	(cd "$K/host-python-build" && ./configure --prefix="$HOSTPY" --without-ensurepip >configure.log 2>&1 &&
		make -j"$JOBS" >build.log 2>&1 && make install >install.log 2>&1) ||
		{ tail -20 "$K/host-python-build/configure.log" "$K/host-python-build/build.log"; exit 1; }
fi

echo "== CPython $VER pour Korin PC"
mkdir -p "$K/python-work/dl"
cp "$ARC" "$K/python-work/dl/"
ARCH=x86_64 PYVER=$VER PYSHA256=$(sha256sum "$ARC" | cut -d' ' -f1) BUILD_PYTHON=$HOSTPY/bin/python$SHORT \
	PYPATCH=$BL/patches/python-$SHORT.patch STATIC_LIBPY=1 WORK=$K/python-work OUT=$OUT/python JOBS=$JOBS sh "$ROOT/tools/python/build.sh"
# ce que les modules intégrés à libpython demandent à la liaison (_decimal, _hashlib/_sha2, pyexpat, _sqlite3,
# _ctypes, _ssl) : à lier après libpython (build.sh : PYTHON_LIBRARY), dans cet ordre
L=$OUT/python/stage/devtools/python/lib
mkdir -p "$L/korin-deps"
cp "$K/python-work/build/Modules/_decimal/libmpdec/libmpdec.a" "$K/python-work/build/Modules/_hacl/libHacl_Hash_SHA2.a" \
	"$K/python-work/build/Modules/expat/libexpat.a" "$K/python-work/deps/lib/libsqlite3.a" \
	"$K/python-work/deps/lib/libffi.a" "$ROOT/build/pc/thirdparty/openssl/lib/libssl.a" \
	"$ROOT/build/pc/thirdparty/openssl/lib/libcrypto.a" "$L/korin-deps/"
# l'expat de CPython renomme ses fonctions en PyExpat_*, pas tout : deux symboles heurtent l'expat d'OpenColorIO
# (deps.sh) dans Blender ; renommés de même, définitions et références, dans sa copie
for s in g_reparseDeferralEnabledDefault _INTERNAL_trim_to_complete_utf8_characters; do
	${TC}objcopy --redefine-sym "$s=PyExpat_$s" "$L/korin-deps/libexpat.a"
done
ls -l "$L/libpython$SHORT.a" "$L/korin-deps"
