#!/bin/sh
# run.sh -- Relics sur l'hote, pour verifier les niveaux et voir des images sans Korin (tools/relics/host.c) :
#
#   sh tools/relics/run.sh check [n]                 la sortie de chaque niveau est-elle atteignable ?
#   sh tools/relics/run.sh shot n out.png [script]   une image du niveau n (n = -1 : le titre) ; script : host.c
#
# Le GCC de l'hote suffit (et python3 pour les en-tetes des kits, PIL pour le .png).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=$ROOT/build/relics-host
mkdir -p "$B/inc/armpi" "$B/w"
if [ ! -f "$B/stubs.c" ] || [ "$ROOT/src/kits/uikit/uikit.abi" -nt "$B/stubs.c" ]; then
	A=$ROOT/src/inc
	K=$ROOT/src/kits
	python3 -I "$ROOT/tools/kits/abi2c.py" "$K/uikit/uikit.abi" "$B/w" --arch x86_64 --const "$A/armpi.inc" "$A/ws_ext.inc" "$K/uikit/uikit.inc" >/dev/null
	python3 -I "$ROOT/tools/kits/abi2c.py" "$K/audiokit/audiokit.abi" "$B/w" --arch x86_64 --const "$K/audiokit/audiokit.inc" >/dev/null
	cp "$B/w/uikit_abi.h" "$B/w/audiokit_abi.h" "$K/uikit/include/uikit.h" "$B/inc/"
	cat > "$B/inc/armpi/syscall.h" <<'EOF'
/* (l'hote : seul SYS_TIME_MS sert, a gamesound.h) */
#include <time.h>
static inline long armpi_syscall (long n, long a, long b, long c, long d, long e, long f)
{
	struct timespec t; (void) n; (void) a; (void) b; (void) c; (void) d; (void) e; (void) f;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (long) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
EOF
	python3 -I "$ROOT/tools/relics/mkstubs.py" "$B/stubs.c" "$B/inc/uikit_abi.h" "$B/inc/audiokit_abi.h"
fi
gcc -O2 -g -Wall -Wno-unused-function -Wno-unused-variable -I"$B/inc" -o "$B/host" "$ROOT/tools/relics/host.c" "$B/stubs.c" -lm
case ${1:-check} in
shot)
	out=$3
	"$B/host" shot "$2" "$B/shot.ppm" "${4:-}"
	python3 -c "from PIL import Image; Image.open('$B/shot.ppm').save('$out')"
	;;
*)	"$B/host" "$@" ;;
esac
