#!/bin/bash
# build-rust-ext : construit sur le PC (WSL) des roues de PyPI pour Korin dont le module natif est
# du Rust (pyo3) : une cdylib par paquet, un .so charge par dlopen (docs/PYTHON-PORT.md, « Étape 5 »).
# A lancer depuis la racine du depot, apres sh tools/python/build.sh et bash tools/rust/build.sh :
#     bash tools/python/build-rust-ext.sh pydantic-core==2.46.4 rpds-py==0.30.0 cryptography==50.0.0
#     bash tools/python/build-rust-ext.sh tools/python/tests/panic     # un dossier local
#     (le harness de Vibe : tools/vibe/build-korin.sh, un dossier prepare avec son pyproject.toml)
#     OUT_WHEELS=dossier bash tools/python/build-rust-ext.sh ...      # build/python/wheels sinon
#
# Sans maturin : l'archive des sources (sdist) de PyPI, verifiee (sha256 de PyPI, et celui de la
# table plus bas pour les versions connues), un patch tools/python/rust/<nom>-<version>.patch s'il
# existe, puis cargo korin build (cible aarch64-unknown-korin, docs/RUST.md) et la roue faite ici :
# le Python du paquet (python-source de [tool.maturin]), le .so a la place que dit module-name, la
# METADATA du sdist. Roue <nom>-<version>-cp314-cp314-linux_aarch64.whl, ou cp314-abi3 si pyo3 a
# « abi3 ».
#
# pyo3 en croise : PYO3_CONFIG_FILE (CPython 3.14, statique, pas de libpython a lier : python3 est
# l'hote -rdynamic et exporte l'API C), PYO3_CROSS_LIB_DIR (le sysconfig et les en-tetes de la
# carte, dans build/python/stage). La libc de Rust, target-lexicon et getrandom sont celles de tools/rust
# (cargo update --precise vers les versions patchees). OpenSSL (cryptography) : celui que python3
# exporte en entier ; le module n'en porte pas de copie (bibliotheques vides pour openssl-sys,
# les symboles restent a resoudre par dlopen). Chaque .so est verifie : tout symbole non defini
# doit etre exporte par python3 ; la taille de son TLS est affichee (reserve de 32 Ko de l'hote).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=aarch64-onyx-elf-
WORK=${WORK:-$HOME/.cache/armpi-python}
K=${KORIN_RUST:-$HOME/.cache/korin-rust}
OUT=${OUT:-$ROOT/build/python}
OUT_WHEELS=${OUT_WHEELS:-$OUT/wheels}
SPECS=${SPECS:-$ROOT/build/posix/lib/posix.specs}
SSL=${SSL:-$ROOT/build/openssl}
BUILD_PYTHON=${BUILD_PYTHON:-/usr/bin/python3.14}
LIBC_VER=0.2.185		# celle de tools/rust/libc.patch
TL_VER=0.13.5			# celle de tools/rust/target-lexicon.patch
GR_VER=0.3.4			# celle de tools/rust/getrandom.patch
# sha256 des sdist connus (PyPI les donne aussi ; ceux-ci sont ceux des versions de Vibe 3.1.2)
declare -A SHA=(
	[pydantic_core-2.46.4.tar.gz]=62f875393d7f270851f20523dd2e29f082bcc82292d66db2b64ea71f64b6e1c1
	[rpds_py-0.30.0.tar.gz]=dd8ff7cf90014af0c0f787eea34794ebf6415242ee1d6fa91eaba725cc441e84
	[cryptography-50.0.0.tar.gz]=eeac2acb5a20ed25e0ad6d1df9891a520b78b404266b6d11778f25d5d691a6c9
)

[ $# -ge 1 ] || { sed -n '2,6p' "$0" >&2; exit 2; }
export PATH="$TOOL/bin:$PATH"
L=$OUT/stage/python/lib/python3.14
HOST=$OUT/sdroot/bin/python3
[ -f "$L/_sysconfigdata__linux_.py" ] && [ -x "$HOST" ] || { echo "lancer d'abord sh tools/python/build.sh" >&2; exit 1; }
[ -f "$K/env.sh" ] && [ -d "$K/vendor/target-lexicon-$TL_VER" ] || { echo "lancer d'abord bash tools/rust/build.sh" >&2; exit 1; }
. "$K/env.sh"
V=$WORK/venv-ext		# le Python de construction (build-ext.sh) ; cffi pour cryptography
[ -x "$V/bin/python" ] || "$BUILD_PYTHON" -m venv "$V"
"$V/bin/python" -m pip install -q --disable-pip-version-check setuptools==80.9.0 wheel==0.45.1 cffi==2.0.0 pycparser==2.23

R=$WORK/rust-ext
mkdir -p "$R/dl" "$R/src" "$OUT_WHEELS"
# la configuration de pyo3 (la meme pour les trois versions de pyo3-build-config)
cat > "$R/pyo3-config.txt" <<EOF
implementation=CPython
version=3.14
shared=false
abi3=false
lib_name=python3.14
pointer_width=64
build_flags=
suppress_build_script_link_lines=true
EOF
# OpenSSL : en-tetes de build/openssl, bibliotheques vides (python3 a la vraie)
mkdir -p "$R/openssl-empty"
for l in ssl crypto; do rm -f "$R/openssl-empty/lib$l.a"; ${TC}ar rcs "$R/openssl-empty/lib$l.a"; done
"${TC}nm" -D --defined-only "$HOST" | awk '{print $3}' | sort -u > "$R/host.syms"

T_=aarch64_unknown_korin
export CC_$T_="${TC}gcc -specs=$SPECS" AR_$T_=${TC}ar CFLAGS_$T_="-mcpu=cortex-a72 -O2 -fno-stack-protector"
export PYO3_CONFIG_FILE=$R/pyo3-config.txt PYO3_CROSS_LIB_DIR=$L PYO3_PYTHON=$V/bin/python \
	PYO3_BUILD_EXTENSION_MODULE=1 PYO3_CROSS_PYTHON_VERSION=3.14
export OPENSSL_DIR=$SSL OPENSSL_INCLUDE_DIR=$SSL/include OPENSSL_LIB_DIR=$R/openssl-empty OPENSSL_STATIC=1 \
	OPENSSL_NO_VENDOR=1
export CARGO_TARGET_DIR=$R/target

for spec in "$@"; do
	if [ -d "$spec" ]; then
		# un dossier local (un essai : tools/python/tests/panic) : pyproject.toml, PKG-INFO fait ici
		S=$R/src/$(basename "$spec")
		rm -rf "$S" && cp -r "$spec" "$S" && rm -rf "$S/target"
		read -r name ver < <("$BUILD_PYTHON" -I -c '
import sys, tomllib
p = tomllib.load(open(sys.argv[1], "rb"))["project"]
print(p["name"], p["version"])' "$S/pyproject.toml")
		[ -f "$S/PKG-INFO" ] || printf 'Metadata-Version: 2.4\nName: %s\nVersion: %s\n' "$name" "$ver" > "$S/PKG-INFO"
	else
		# ---- le sdist de PyPI ----
		name=${spec%%==*}; ver=${spec#*==}
		read -r url sha < <(curl -fsSL "https://pypi.org/pypi/$name/$ver/json" | "$BUILD_PYTHON" -I -c '
import json, sys
for u in json.load(sys.stdin)["urls"]:
    if u["packagetype"] == "sdist": print(u["url"], u["digests"]["sha256"])')
		f=$(basename "$url")
		[ -z "${SHA[$f]:-}" ] || [ "${SHA[$f]}" = "$sha" ] || { echo "$f : sha256 de PyPI inattendu" >&2; exit 1; }
		[ -f "$R/dl/$f" ] || curl -fsSL -o "$R/dl/$f" "$url"
		echo "$sha  $R/dl/$f" | sha256sum -c - >/dev/null
		S=$R/src/${f%.tar.gz}
		rm -rf "$S" && tar xzf "$R/dl/$f" -C "$R/src"
		P=$ROOT/tools/python/rust/$name-$ver.patch
		[ -f "$P" ] && patch -d "$S" -p1 -s < "$P"
	fi

	# ---- [tool.maturin] ----
	eval "$("$BUILD_PYTHON" -I - "$S" <<'EOF'
import sys, tomllib, shlex
s = sys.argv[1]
c = tomllib.load(open(s + "/pyproject.toml", "rb")).get("tool", {}).get("maturin", {})
m = tomllib.load(open(s + "/" + c.get("manifest-path", "Cargo.toml"), "rb"))
lib = m.get("lib", {}).get("name", m["package"]["name"].replace("-", "_"))
feat = [f for f in c.get("features", []) if not f.startswith("pyo3/")]
for k, v in {"MANIFEST": c.get("manifest-path", "Cargo.toml"), "LIB": lib,
             "MODULE": c.get("module-name", lib), "PYSRC": c.get("python-source", ""),
             "FEATURES": ",".join(feat), "NODEF": "1" if c.get("no-default-features") else "",
             "PYPKGS": " ".join(c.get("python-packages", []))}.items():
    print(f"{k}={shlex.quote(v)}")
EOF
)"
	echo "== $name $ver : $MANIFEST, module $MODULE"
	M=$S/$MANIFEST
	MD=$(dirname "$M")
	# la libc et target-lexicon patchees (tools/rust) ; un Cargo.lock d'un autre dossier (workspace) : le sien
	LOCKDIR=$MD; [ -f "$MD/Cargo.lock" ] || LOCKDIR=$S
	[ -f "$LOCKDIR/Cargo.lock" ] || (cd "$MD" && cargo generate-lockfile -q)
	# chaque version de la meme serie (0.2.x, 0.13.x, 0.3.x) passe a la version patchee
	for p in libc@$LIBC_VER target-lexicon@$TL_VER getrandom@$GR_VER; do
		c=${p%@*}; v=${p#*@}
		for old in $(awk -v c="$c" '$1 == "name" && $3 == "\""c"\"" {getline; gsub(/"/, "", $3); print $3}' "$LOCKDIR/Cargo.lock"); do
			if [ "${old%.*}" = "${v%.*}" ] && [ "$old" != "$v" ]; then
				(cd "$MD" && cargo update -q -p "$c@$old" --precise "$v")
			fi
		done
	done
	(cd "$MD" && cargo korin build --release --lib ${NODEF:+--no-default-features} ${FEATURES:+--features "$FEATURES"} 		--manifest-path "$M")
	SO=$CARGO_TARGET_DIR/aarch64-unknown-korin/release/lib$LIB.so

	# ---- la roue ----
	# abi3 si la dependance pyo3 (du paquet ou de son workspace) demande la fonction abi3
	ABI3=$(grep -h '^pyo3 *=' "$S/Cargo.toml" "$M" | grep -c '"abi3' || true)
	W=$R/wheel/$name
	rm -rf "$W" && mkdir -p "$W"
	if [ -n "$PYSRC" ]; then
		top=${MODULE%%.*}
		# le paquet du module, et ceux que nomme python-packages (Vibe : vibe et le harness)
		for p in $top $PYPKGS; do [ -d "$W/$p" ] || cp -r "$S/$PYSRC/$p" "$W/"; done
		find "$W" -name "__pycache__" -prune -exec rm -rf {} +
	fi
	"$BUILD_PYTHON" -I - "$S" "$W" "$SO" "$MODULE" "$ABI3" "$OUT_WHEELS" "${TC}strip" "$ver" <<'EOF'
import base64, hashlib, os, re, shutil, subprocess, sys, zipfile
src, w, so, module, abi3, out, strip, ver = sys.argv[1:]
abi3 = int(abi3) > 0
parts = module.split(".")
pkg = os.path.join(w, *parts[:-1]) if len(parts) > 1 else os.path.join(w, parts[0])
os.makedirs(pkg, exist_ok=True)
if len(parts) == 1:
    # module Rust seul (rpds) : maturin en fait un paquet du meme nom
    with open(os.path.join(pkg, "__init__.py"), "w") as f:
        f.write(f"from .{parts[0]} import *\n\n__doc__ = {parts[0]}.__doc__\n"
                f"if hasattr({parts[0]}, \"__all__\"):\n    __all__ = {parts[0]}.__all__")
    for pyi in (os.path.join(src, parts[0] + ".pyi"),):
        if os.path.exists(pyi):
            shutil.copy(pyi, os.path.join(pkg, "__init__.pyi"))
            open(os.path.join(pkg, "py.typed"), "w").close()
suffix = ".abi3.so" if abi3 else ".cpython-314.so"
dest = os.path.join(pkg, parts[-1] + suffix)
subprocess.run([strip, "--strip-unneeded", so, "-o", dest], check=True)
meta = open(os.path.join(src, "PKG-INFO"), encoding="utf-8").read()
name = re.search(r"^Name: (.*)$", meta, re.M).group(1).strip()
dname = re.sub(r"[-_.]+", "_", name).lower()
di = os.path.join(w, f"{dname}-{ver}.dist-info")
os.makedirs(di, exist_ok=True)
open(os.path.join(di, "METADATA"), "w", encoding="utf-8").write(meta)
tag = "cp314-abi3-linux_aarch64" if abi3 else "cp314-cp314-linux_aarch64"
open(os.path.join(di, "WHEEL"), "w").write(
    f"Wheel-Version: 1.0\nGenerator: korin build-rust-ext\nRoot-Is-Purelib: false\nTag: {tag}\n")
for lic in os.listdir(src):
    if lic.startswith(("LICENSE", "LICENCE", "COPYING")) and os.path.isfile(os.path.join(src, lic)):
        os.makedirs(os.path.join(di, "licenses"), exist_ok=True)
        shutil.copy(os.path.join(src, lic), os.path.join(di, "licenses", lic))
rec = []
files = sorted(os.path.relpath(os.path.join(d, f), w) for d, _, fs in os.walk(w) for f in fs)
for rel in files:
    data = open(os.path.join(w, rel), "rb").read()
    h = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
    rec.append(f"{rel},sha256={h},{len(data)}")
recname = os.path.join(os.path.basename(di), "RECORD")
rec.append(f"{recname},,")
open(os.path.join(w, recname), "w").write("\n".join(rec) + "\n")
whl = os.path.join(out, f"{dname}-{ver}-{tag}.whl")
with zipfile.ZipFile(whl, "w", zipfile.ZIP_DEFLATED) as z:
    for rel in files + [recname]:
        z.write(os.path.join(w, rel), rel)
print(os.path.basename(whl), os.path.relpath(dest, w), os.path.getsize(dest), "octets")
EOF
	# ---- verification : symboles non definis, TLS ----
	so=$(find "$W" -name "*.so")
	"${TC}nm" -D --undefined-only "$so" | awk '$1 == "U" {print $2}' | sed 's/@.*//' | sort -u > "$R/need.syms"
	miss=$(comm -23 "$R/need.syms" "$R/host.syms")
	[ -z "$miss" ] || { echo "$(basename "$so") : symboles absents de python3 :" $miss >&2; exit 1; }
	tls=$("${TC}readelf" -lW "$so" | awk '$1 == "TLS" {print $6}')
	echo "$(basename "$so") : $(wc -l < "$R/need.syms") symboles de l'hote, TLS ${tls:-0}"
done
