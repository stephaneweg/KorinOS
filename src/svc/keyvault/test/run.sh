#!/bin/sh
# Essai du pilote TPM du coffre (src/svc/keyvault/tpm.c) sous Linux, contre swtpm (apt install swtpm) :
#     wsl sh src/svc/keyvault/test/run.sh
set -eu
cd "$(dirname "$0")"
T=$(mktemp -d)
trap 'kill $SW 2>/dev/null; rm -rf "$T"' EXIT
mkdir -p "$T/state"
swtpm socket --tpm2 --tpmstate dir="$T/state" --server type=tcp,port=2321 --ctrl type=tcp,port=2322 \
	--flags not-need-init,startup-clear >"$T/swtpm.log" 2>&1 &
SW=$!
sleep 1
gcc -O1 -g -Wall -DTPM_TCP -fsanitize=address,undefined tpmtest.c ../tpm.c -lcrypto -o "$T/tpmtest"
"$T/tpmtest" "$T/kv"
