#!/bin/bash
# build-ninja.sh -- Ninja 1.12.1 dans Korin : la source officielle, ninja-korin.patch (pas de getloadavg ; au plus 16
# commandes à la fois par défaut : 64 fichiers ouverts par process, chaque commande en cours prend un tube, et au-delà
# de ≈ 20 posix_spawn et pipe rendent EMFILE), puis l'amorçage de Ninja (configure.py --bootstrap, plateforme linux :
# la couche POSIX de Korin en est la plus proche). -> ~/jet-build/bin/ninja
set -e
W=${WORK:-$HOME/jet-build}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$W/src" "$W/bin"
cd "$W/src"
if [ ! -d ninja-1.12.1 ]; then
	python3 -I - <<'EOF'
import urllib.request as u, tarfile, io, hashlib
d = u.urlopen("https://github.com/ninja-build/ninja/archive/refs/tags/v1.12.1.tar.gz", timeout=180).read()
h = hashlib.sha256(d).hexdigest()
print("ninja-1.12.1.tar.gz", len(d), "octets, sha256", h)
assert h == "821bdff48a3f683bc4bb3b6f0b5fe7b2d647cf65d52aeb63328c91a6c6df285a", "empreinte inattendue"
tarfile.open(fileobj=io.BytesIO(d)).extractall(".")
EOF
	(cd ninja-1.12.1 && patch -p1 < "$HERE/ninja-korin.patch")
fi
cd ninja-1.12.1
rm -rf build
# (-D_GNU_SOURCE : fileno et les autres fonctions POSIX que newlib cache en C++11 strict)
CXX=g++ CXXFLAGS="-O2 -D_GNU_SOURCE" python3 configure.py --bootstrap --platform=linux
strip -o "$W/bin/ninja" ninja
chmod +x "$W/bin/ninja"
"$W/bin/ninja" --version
