#!/bin/sh
# gdrivefs sous Linux (WSL) avec AddressSanitizer et UBSan, contre tools/nettest/gdrivesrv.py (lancé ici sur le
# port 8091). Dossier de travail jetable dans /tmp (HOME:/gdrive/drives.ini y est écrit).
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
W=$(mktemp -d /tmp/gdrivefs-test.XXXXXX)
# TSAN=1 : ThreadSanitizer (les threads de lecture anticipée) au lieu d'AddressSanitizer
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
[ -z "${TSAN:-}" ] || SAN="-fsanitize=thread"
gcc -std=gnu11 -g -O1 -Wall -Wno-unused-function $SAN -pthread \
	-I"$ROOT/src/svc/gdrivefs/test" "$ROOT/src/svc/gdrivefs/test/linux.c" -lssl -lcrypto -o "$W/gdrivefs-test"
python3 -I "$ROOT/tools/nettest/gdrivesrv.py" 8091 2>"$W/srv.log" &
SRV=$!
for i in 1 2 3 4 5 6 7 8 9 10; do grep -q prêt "$W/srv.log" 2>/dev/null && break; sleep 1; done
cd "$W"
set +e
./gdrivefs-test
R=$?
kill $SRV
[ $R -eq 0 ] || tail -20 "$W/srv.log"
cd /
rm -rf "$W"
exit $R
