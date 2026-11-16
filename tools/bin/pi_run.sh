#!/bin/sh
# Lance des commandes sur une carte dans QEMU (raspi4b) par telnet, dans la première session qui
# répond (docs/BIN-C.md) ; Git Bash, depuis la racine du dépôt :
#   sh tools/bin/pi_run.sh CARTE.img SORTIE.txt "commande" ["commande"...]
# Comme tools/kits/run_pi.sh (emplacement RUN_PI_SLOT, 3 par défaut ; « @600 commande » : attente
# de 600 s), sans la session d'attente : sous QEMU, les sessions telnet après la première restent
# parfois muettes. La carte doit avoir /etc/remote.txt « password = essai ».
set -e
cd "$(dirname "$0")/../.."
CARD=$1
OUT=$2
shift 2
N=${RUN_PI_SLOT:-3}
TP=$((2325 + N))
echo "wait 1500" > build/run_pi_wait_$N.txt
python -I tools/qemu_drive.py build/run_pi_wait_$N.txt --sd "$CARD" --fwd $TP:23 --port $((4467 + N)) \
	--vnc $((67 + N)) --uart build/run_pi_uart_$N.log >/dev/null 2>&1 &
QPID=$!
: > "$OUT"
for i in $(seq 1 60); do
	sleep 5
	ARMPI_REMOTE_PW=essai python -I tools/armpi_remote.py telnet 127.0.0.1:$TP "$@" > "$OUT" 2>&1 || true
	grep -q '^\[code ' "$OUT" && break
done
kill $QPID 2>/dev/null || true
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='qemu-system-aarch64.exe'\" |
	Where-Object { \$_.CommandLine -like '*127.0.0.1:$TP-:23*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" || true
echo "$OUT : $(wc -l < "$OUT") lignes"
