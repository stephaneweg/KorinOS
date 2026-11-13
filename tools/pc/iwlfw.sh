#!/bin/sh
# Micrologiciels Wi-Fi Intel de la famille 22000 (« Qu ») qui ne sont pas dans le dépôt (docs/etude/WIFI-PC.md 3) :
# le ZBook a un QuZ (firmware/pc/iwlwifi/iwlwifi-QuZ-a0-hr-b0-77.ucode, gardé dans le dépôt avec sa licence) ; si
# « iwl » dit au journal qu'il lui faut un autre fichier (MAC Qu en étape B ou C), ce script le télécharge depuis
# linux-firmware, vérifie sa somme et le range à côté ; tools/pc/sysroot.sh copie tout le dossier.
#
#   sh tools/pc/iwlfw.sh Qu-c0-hr-b0     (ou Qu-b0-hr-b0)
set -e
cd "$(dirname "$0")/../.."
URL=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/intel/iwlwifi
case "$1" in
Qu-b0-hr-b0) SUM=5e53c77a19f0cfabe039541bad814f90f47a6fec75ca6e3274610167c25897de ;;
Qu-c0-hr-b0) SUM=71bfd1f64299dcc32a6b3ff068a3b7c189f56a78d9d90add4b79cd463506700e ;;
QuZ-a0-hr-b0) SUM=8f1b9bb98739a402d8414b7855b2c42256550ede329d06947d0f431f538dde77 ;;
*) echo "usage : sh tools/pc/iwlfw.sh Qu-b0-hr-b0 | Qu-c0-hr-b0 | QuZ-a0-hr-b0"; exit 2 ;;
esac
F=firmware/pc/iwlwifi/iwlwifi-$1-77.ucode
[ -f "$F" ] && [ "$(sha256sum "$F" | cut -d' ' -f1)" = "$SUM" ] && { echo "$F : déjà là"; exit 0; }
curl -fL --retry 3 -o "$F.part" "$URL/iwlwifi-$1-77.ucode?h=main"
[ "$(sha256sum "$F.part" | cut -d' ' -f1)" = "$SUM" ] || { rm -f "$F.part"; echo "sha256 faux : $F" >&2; exit 1; }
mv "$F.part" "$F"
echo "$F : $(wc -c <"$F") octets (licence : firmware/pc/iwlwifi/LICENCE.iwlwifi_firmware)"
