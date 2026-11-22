#!/bin/sh
# jet-sdk.sh -- le SDK de Jet pour Korin PC : de quoi recompiler un fichier de WebKit et relier Jet **dans Korin**, avec
# le GCC du paquet gcc (/devtools/gcc), sans reconstruire WebKit (docs/agents/JET-SDK.md).
#
#   ARCH=x86_64 sh tools/webkit/jet-sdk.sh        après tools/mesa/build-webkit.sh et tools/webkit/build-jet.sh
#   -> build/pc/jet-sdk/jet-sdk-<version>-<n>.zip  (des parts de moins de 90 Mo, pour la Release « packages »)
#
# Le SDK s'installe dans /devtools/jet-sdk (tools/webkit/jet-sdk-get.sh) ; les chemins du cloud
# (~/korin-webkit-x86_64) y sont réécrits. Ce qu'il contient :
#   webcore/          les bibliothèques de WebKit (archives minces) et tous leurs objets, les objets propres à Jet,
#                     les sources générées (DerivedSources), les en-têtes recopiés (Headers, PrivateHeaders : des liens
#                     symboliques dans le cloud, ici des en-têtes d'une ligne qui incluent le vrai fichier), cmake_pch.hxx
#   webkit-gl/Source  les sources de WebKit, patchées comme pour Jet (.armpi-patches)
#   sysroot/lib       les bibliothèques tierces liées avec Jet ; sysroot/include : leurs en-têtes (sans ceux de libposix :
#                     le GCC de Korin a les siens)
#   uikit, av, ffmpeg, glkit   les autres entrées de la liaison
#   commands.txt      la commande de compilation de chaque objet (une ligne : objet<TAB>commande)
#   jetbuild, jetlink les scripts (bash) ; README.md
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
K=${WK_OUT:-$HOME/korin-webkit-x86_64}
PREFIX=/devtools/jet-sdk
VER=$(sed -n 's/^jet = //p' "$ROOT/tools/pkg/versions.ini" | tail -1)
OUT=$ROOT/build/pc/jet-sdk
T=$OUT/tree
[ -f "$K/webcore/lib/libWebCore.a" ] && [ -f "$K/webcore/jet_main.o" ] || { echo "jet-sdk.sh : d'abord build-webkit.sh et build-jet.sh (ARCH=x86_64)" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$T"
python3 -I "$ROOT/tools/webkit/jet-sdk.py" "$K" "$T" "$PREFIX" "$ROOT"
cp "$ROOT/tools/webkit/jet-sdk/jetbuild" "$ROOT/tools/webkit/jet-sdk/jetlink" "$ROOT/tools/webkit/jet-sdk/README.md" "$T/"
echo "$VER" > "$T/VERSION"
python3 -I - "$T" "$OUT" "$VER" <<'EOF'
import os, sys, zipfile
T, OUT, VER = sys.argv[1:4]
LIMIT = 88 * 1024 * 1024
files = []
for r, ds, fs in os.walk(T):
    ds.sort()
    for f in sorted(fs):
        files.append(os.path.join(r, f))
n, z, size = 0, None, 0
def new():
    global n, z, size
    if z: z.close()
    n += 1; size = 0
    z = zipfile.ZipFile(os.path.join(OUT, "jet-sdk-%s-%d.zip" % (VER, n)), "w", zipfile.ZIP_DEFLATED, compresslevel=6)
new()
for p in files:
    rel = os.path.relpath(p, T)
    z.write(p, rel)
    size = os.path.getsize(os.path.join(OUT, "jet-sdk-%s-%d.zip" % (VER, n)))
    if size > LIMIT:
        new()
z.close()
print("parts :", n)
EOF
ls -l "$OUT"/*.zip
