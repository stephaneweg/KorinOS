#!/bin/sh
# Exécute des méthodes de l'AML d'une machine dans acpiexec (l'interpréteur d'ACPICA, sans le matériel :
# l'EC et les registres lus valent 0). Dans WSL :
#     sh tools/acpica/acpiexec.sh [-d DOSSIER] [options d'acpiexec] ['commandes']
# DOSSIER : des tables .dat (défaut tools/acpica/zbook, le ZBook ; tools/acpica/dump_tables.ps1 en copie
# d'autres) ; commandes : celles d'acpiexec séparées par « ; » (défaut : la batterie et le secteur, comme
# acpid). Le mode souple d'acpid : -es. Le DSDT désassemblé : DSL=1 (DOSSIER/DSDT.dsl, par iasl -d).
#     sh tools/acpica/acpiexec.sh -es
#     sh tools/acpica/acpiexec.sh 'execute \_SB.LID0._LID'
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VER=${ACPICA_VER:-20260930}
SRC=${CACHE:-$HOME/.cache/korin-acpica}/acpica-unix-$VER
DIR=$ROOT/tools/acpica/zbook
if [ "${1:-}" = -d ]; then
	DIR=$(cd "$2" && pwd)
	shift 2
fi
[ -d "$SRC" ] || sh "$ROOT/tools/acpica/build.sh"
BIN=$SRC/generate/unix/bin
[ -x "$BIN/acpiexec" ] || make -C "$SRC" -j"$(nproc)" acpiexec iasl >/dev/null
CMDS='execute \_SB.BAT0._STA; execute \_SB.BAT0._BIX; execute \_SB.BAT0._BST; execute \_SB.AC__._PSR'
OPTS=
while [ $# -gt 0 ]; do
	case $1 in
	-*) OPTS="$OPTS $1" ;;
	*) CMDS=$1 ;;
	esac
	shift
done
cd "$DIR"
if [ -n "${DSL:-}" ] && [ ! -f DSDT.dsl ]; then
	"$BIN/iasl" -d DSDT.dat >/dev/null 2>&1 || true
	echo "-> $DIR/DSDT.dsl"
fi
# (le DSDT d'abord, puis les SSDT ; pas FADT ni RSDT : acpiexec fait les siennes)
# shellcheck disable=SC2086
"$BIN/acpiexec" $OPTS -b "$CMDS" DSDT.dat $(ls SSD*.dat 2>/dev/null) </dev/null 2>&1
