#!/bin/sh
# Essai d'acpid sous QEMU (q35, disque NVMe imitant le ZBook) avec ou sans fausse batterie. Dans WSL, après
# tools/cloud/setup.sh (~/korin-cloud/env.sh : QEMU, edk2 avec le shell UEFI, fasm) et la construction du PC
# (wsl sh tools/pc/build.sh, puis tools/pc/sysroot.sh) :
#     sh tools/acpica/qemu_test.sh [bat|ac|none] [options et étapes de tools/pc/qemu_nvme.py]
#   bat  : fausse batterie en décharge (tools/acpica/fakebat.asl, 21 %, 15 W) ; ac : la même en charge ;
#   none : pas de batterie (un PC de bureau).
# L'image est build/pc/acpitest.img (refaite sauf NOMK=1), le journal build/pc/acpitest.log, les captures
# build/pc/<nom>.png. APP="/apps/control.app/main power" : un programme ajouté à la session du bureau de
# l'image (le volet Énergie de Réglages ; pas dans build/pc/sysroot). Exemples :
#     sh tools/acpica/qemu_test.sh bat --wait 60 --step shot:bat
#     sh tools/acpica/qemu_test.sh none --wait 60 --step hmp:system_powerdown --step sleep:3 --step shot:btn
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$HOME/korin-cloud/env.sh"
cd "$ROOT"
MODE=${1:-bat}
[ $# -eq 0 ] || shift
OUT=build/pc
IMG=$OUT/acpitest.img
case $MODE in
bat | ac)
	[ -f $OUT/fakebat.aml ] && [ -f $OUT/fakebat_ac.aml ] || sh tools/acpica/fakebat.sh >/dev/null
	AML=$OUT/fakebat.aml
	[ "$MODE" = bat ] || AML=$OUT/fakebat_ac.aml
	set -- --qemu=-acpitable --qemu=file=$AML "$@" ;;
none) ;;
*) echo "usage : qemu_test.sh [bat|ac|none] [options de qemu_nvme.py]"; exit 1 ;;
esac
if [ -z "${NOMK:-}" ]; then
	TREE=$OUT/sysroot
	if [ -n "${APP:-}" ]; then	# (une copie de l'arbre, sa session augmentée)
		TREE=$OUT/acpitest-sysroot
		rm -rf "$TREE"
		cp -r $OUT/sysroot "$TREE"
		echo "$APP" >>"$TREE/etc/session"
	fi
	python3 -I tools/pc/mkkorindisk.py $IMG --layout zbook --korin-mb 200 --sysroot "$TREE" \
		--cfg "$(printf 'default = korin\ntimeout = 0')" >$OUT/acpitest-mk.log 2>&1 ||
		{ tail -5 $OUT/acpitest-mk.log; exit 1; }
fi
cp "$KORIN_OVMF_VARS" $OUT/acpitest-vars.fd
timeout 600 python3 -I tools/pc/qemu_nvme.py $IMG --boot-disk --snapshot --vars $OUT/acpitest-vars.fd \
	--log $OUT/acpitest.log "$@" 2>&1 | tail -4
grep -a "acpid" $OUT/acpitest.log | grep -v "ACPI: [A-Z]" | head -30
