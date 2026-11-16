#!/bin/sh
# oauthd sous Linux (WSL) avec AddressSanitizer, contre tools/nettest/oauthsrv.py (lancé ici sur le port 8090) ;
# NET=1 : aussi le vrai TLS (Google, badssl.com). Dossier de travail jetable dans /tmp.
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
W=$(mktemp -d /tmp/oauthd-test.XXXXXX)
gcc -std=gnu11 -g -O1 -Wall -Wno-unused-function -fsanitize=address,undefined -I"$ROOT/src/svc/oauthd/test" \
	"$ROOT/src/svc/oauthd/test/linux.c" -lssl -lcrypto -o "$W/oauthd-test"
python3 -I "$ROOT/tools/nettest/oauthsrv.py" 8090 2>"$W/srv.log" &
SRV=$!
sleep 1
cd "$W"
set +e
./oauthd-test
R=$?
kill $SRV
cd /
rm -rf "$W"
exit $R
