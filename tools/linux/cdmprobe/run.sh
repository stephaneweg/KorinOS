#!/bin/sh
# run.sh -- la sonde du module Widevine, sous Linux (WSL), hors de Korin (docs/etude/WIDEVINE.md § 2.5) :
# un hôte Host_10 minimal charge libwidevinecdm.so, un filtre seccomp piège les syscall dont l'instruction
# est dans le module (comme la capture du noyau de Korin), et le parcours va jusqu'à la requête de licence.
# À relancer à chaque nouvelle version du module, pour voir ce qu'il demande de plus.
#
#   sh tools/linux/cdmprobe/run.sh build/pc/linux/widevine/<version>/x/_platform_specific/linux_x64/libwidevinecdm.so
#
# L'en-tête de l'interface (content_decryption_module.h, Chromium, licence BSD) est téléchargé dans
# build/pc/linux/cdm (pas dans le dépôt). Sortie : la trace sur la sortie d'erreur.
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
SO=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
INC=$ROOT/build/pc/linux/cdm
mkdir -p "$INC"
for f in content_decryption_module.h content_decryption_module_export.h; do
	[ -s "$INC/$f" ] || curl -sS -m 60 "https://chromium.googlesource.com/chromium/cdm/+/refs/heads/main/$f?format=TEXT" | base64 -d >"$INC/$f"
done
g++ -O1 -Wall -Wno-unused-result -I"$INC" "$ROOT/tools/linux/cdmprobe/cdmprobe.cpp" -o "$INC/cdmprobe" -ldl
cd "$INC"
timeout 60 ./cdmprobe "$SO"
