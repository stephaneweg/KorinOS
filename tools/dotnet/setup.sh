#!/bin/sh
# setup : recupere dans WSL tout ce que tools/dotnet construit et n'est pas dans le depot, aux versions
# exactes, sommes verifiees. A relancer sans crainte : ce qui est deja la est garde. Depuis la racine
# du depot, sur une WSL neuve :
#     sudo sh tools/dotnet/setup.sh apt      les paquets (une fois)
#     sh tools/dotnet/setup.sh               les archives -> ~/armpi-dotnet
# Puis, dans cet ordre (docs/DOTNET.md) :
#     sh tools/posix/build.sh ; sh tools/openssl/build.sh ; sh tools/dotnet/build.sh
#     sh tools/dotnet/stage.sh ; sh tools/dotnet/kits.sh
# build.sh clone lui-meme dotnet/runtime et y applique armpi.patch.
set -eu
if [ "${1:-}" = apt ]; then
	apt-get update
	apt-get install -y curl unzip cmake ninja-build python3 git
	exit 0
fi
W=${W:-$HOME/armpi-dotnet}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=https://builds.dotnet.microsoft.com/dotnet
N=https://api.nuget.org/v3-flatcontainer
mkdir -p "$W/dl"

get () {	# fichier, adresse, sha512
	f=$W/dl/$1
	[ -f "$f" ] && echo "$3  $f" | sha512sum -c --quiet - 2>/dev/null && return 0
	echo "telecharge $1"
	curl -fL --retry 3 -o "$f.part" "$2"
	mv "$f.part" "$f"
	echo "$3  $f" | sha512sum -c --quiet -
}

unpack () {	# archive, dossier, fichier temoin
	[ -e "$W/$2/$3" ] && return 0
	rm -rf "$W/$2"
	mkdir -p "$W/$2"
	case $1 in
	*.tgz) tar -xzf "$W/dl/$1" -C "$W/$2" ;;
	*) unzip -qo "$W/dl/$1" -d "$W/$2" ;;
	esac
}

# (PC) le SDK qui compile, (arm64) ses assemblys de reference et Roslyn precompile, le runtime dont les
# DLL vont dans /dotnet, et crossgen2 pour precompiler les notres
get sdk.tgz $B/Sdk/10.0.403/dotnet-sdk-10.0.403-linux-x64.tar.gz b7b3d8461cf98b07d2552ce9199702fa3ad010ac2671a5f8cd5136f7534e50f6855ba7bbb02b11494563f1d04156276a31cda1103b900e5eecd0d9473dc7b9c5
get sdk-arm64.tgz $B/Sdk/10.0.403/dotnet-sdk-10.0.403-linux-arm64.tar.gz 0966aa2b70f4677b8319be9df447a78a0dcd6164836425b55a68fd714f4023feb87b4cac21b9a996b79472ad54487cd0adcd2a9dfd570e2b2a1d26423a2baa9b
get rt.tgz $B/Runtime/10.0.14/dotnet-runtime-10.0.14-linux-arm64.tar.gz 99e1d6eb5d3de935853404fd378c77cbf813bd3ae094837b5a589000a492c289bf0d01d8a6bff99b4470572d9f85dc0b41239b5b7801e031197b83d527cdfeb2
get crossgen2.nupkg $N/microsoft.netcore.app.crossgen2.linux-x64/10.0.14/microsoft.netcore.app.crossgen2.linux-x64.10.0.14.nupkg 12d5ce1e89f19092fa9f4c24376790aab23b3b83f5f5517c1c719f4fce973196845205d883a51fb6727f8976b7adeded13bb12388178c7f2a0ea1a1a038134d8
get newtonsoft.nupkg $N/newtonsoft.json/13.0.4/newtonsoft.json.13.0.4.nupkg 6d1faff84ff227a83b195dae5f0d8ead44a36187e32e438b0bc243e24092db79ac2daa672ad7493c1240ef97f01c7fbe12b21f7de22acd82132f102eaf82805c
unpack sdk.tgz sdk dotnet
unpack sdk-arm64.tgz sdk-arm64 dotnet
unpack rt.tgz rt-arm64 dotnet
unpack crossgen2.nupkg crossgen2 tools/crossgen2
unpack newtonsoft.nupkg newtonsoft lib/net6.0/Newtonsoft.Json.dll
chmod +x "$W/crossgen2/tools/crossgen2"

# ce que ces scripts ne peuvent pas refaire seuls
ok=1
T=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-gcc
[ -x "$T" ] || { echo "manque la chaine d'Onyx : tools/toolchain/fetch.sh d'Onyx (docs/GCC.md)"; ok=0; }
[ -d "$HOME/armpi-webkit-build/sysroot/include/unicode" ] || { echo "manque ICU : sh tools/webkit/build-deps.sh"; ok=0; }
[ "$ok" = 1 ] && echo "pret : $W"
