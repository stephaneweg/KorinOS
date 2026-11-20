#!/bin/sh
# test_cdm_pc.sh -- l'essai de bout en bout de l'hôte du module Widevine DANS Korin, sous QEMU PC avec le
# réseau (docs/etude/WIDEVINE.md, étape 3), dans WSL :
#
#   WIDEVINE_SO=…/libwidevinecdm.so sh tools/linux/test_cdm_pc.sh            (cdmtest : le module appelé directement)
#   WIDEVINE_SO=…/libwidevinecdm.so ESSAI=ipctest sh tools/linux/test_cdm_pc.sh (ipctest : par cdmhost, comme Jet)
#
# 1. le noyau (avec la capture de syscall et l'arène haute), construit à part dans build/pc/linux/kbuild ;
# 2. un arbre minimal (build/pc/linux/qroot) : etc, sbin, sys/svc et les petits kits de build/pc/sysroot,
#    /widevine (le module de Google, le début du flux d'essai de tools/linux/cdmtest.sh, cdmtest) ; init2 y
#    lance netd, puis /widevine/cdmtest comme un service ;
# 3. un disque d'essai (tools/pc/mkkorindisk.py, 160 Mo) ; QEMU démarre dessus (NVMe, snapshot) avec une
#    carte réseau USB (usb-net, réseau « user » de QEMU) ;
# 4. le journal du noyau (port 0xE9) : les lignes de cdmtest ; « cdmtest : RÉUSSI » attendu.
# Le disque et l'arbre sont effacés à la fin (KEEP=1 pour les garder). Rien n'est écrit ailleurs.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
OUT=build/pc/linux
SO=${WIDEVINE_SO:?WIDEVINE_SO=…/libwidevinecdm.so}
[ -f "$OUT/cdmtest" ] || { echo "d'abord : sh tools/linux/build.sh" >&2; exit 1; }
[ -s "$OUT/media/angel-one-144p.mp4" ] && [ -s "$OUT/media/angel-one-aac.mp4" ] ||
	{ echo "d'abord : sh tools/linux/cdmtest.sh (il télécharge le début du flux)" >&2; exit 1; }
[ -f "$OUT/kbuild/esp/korin/kernel.elf" ] || PC_OUT=$OUT/kbuild PC_QUICK=1 PC_SVC=pcid sh tools/pc/build.sh >/dev/null

Q=$OUT/qroot
S=build/pc/sysroot
rm -rf "$Q"
mkdir -p "$Q/sys" "$Q/lib" "$Q/home" "$Q/tmp" "$Q/widevine"
cp -r "$S/etc" "$S/sbin" "$Q/"
cp -r "$S/sys/svc" "$Q/sys/"
for k in appkit uikit netkit buskit audiokit archivekit documentkit gpiokit printerkit emukit; do
	[ -f "$S/lib/$k.so" ] && cp "$S/lib/$k.so" "$Q/lib/"
done
mkdir -p "$Q/bin"
[ -f "$S/bin/sh" ] && cp "$S/bin/sh" "$Q/bin/"
# les services : le réseau, puis l'essai
{
	echo "# essai de l'hôte Widevine (tools/linux/test_cdm_pc.sh)"
	echo "/sys/svc/vterm"
	echo "/sys/svc/netd"
	echo "/widevine/${ESSAI:-cdmtest}"
} >"$Q/etc/services.txt"
echo "/widevine" >>"$Q/etc/exec.txt"	# (dossier d'exécutables, docs/FICHIERS.md)
cp "$SO" "$Q/widevine/libwidevinecdm.so"
cp "$OUT/media/angel-one-144p.mp4" "$Q/widevine/video.mp4"
cp "$OUT/media/angel-one-aac.mp4" "$Q/widevine/audio.mp4"
cp "$OUT/cdmtest" "$OUT/cdmhost" "$OUT/ipctest" "$Q/widevine/"
du -sh "$Q"

IMG=$OUT/qdisk.img
VARS=$OUT/ovmf_vars_cdm.fd
LOG=$OUT/cdm_qemu.log
rm -f "$IMG" "$VARS" "$LOG"
# (boot.cfg : Korin tout de suite, sans le moniteur ; pas de faux Windows)
python3 -I tools/pc/mkkorindisk.py "$IMG" --no-windows --cfg 'default = korin\ntimeout = 0' --esp "$OUT/kbuild/esp" \
	--sysroot "$Q" >/dev/null
python3 -I tools/pc/qemu_nvme.py "$IMG" --boot-disk --snapshot --vars "$VARS" --log "$LOG" --wait 5 \
	--qemu "-device qemu-xhci,id=xn -netdev user,id=n0 -device usb-net,bus=xn.0,netdev=n0" \
	--step "sleep:${WAIT:-150}" --shot "$OUT/cdm_qemu.png" >/dev/null 2>&1 || true
grep -aE "cdmtest|ipctest|cdmhost|hôte :|\[ok\]|\[ÉCHEC\]|liblinux" "$LOG" | awk '!seen[$0]++' || echo "(rien de cdmtest dans le journal : $LOG)"
if [ -z "${KEEP:-}" ]; then
	rm -rf "$Q" "$IMG" "$VARS"
fi
grep -aq "${ESSAI:-cdmtest} : RÉUSSI" "$LOG"
