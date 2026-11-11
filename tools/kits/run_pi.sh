#!/bin/sh
# Lance des commandes sur une carte du Pi dans QEMU (raspi4b) par telnet, pour vérifier un kit :
#   sh tools/kits/run_pi.sh CARTE.img SORTIE.txt "commande" ["commande"...]
# (Git Bash, depuis la racine du dépôt). La carte doit avoir SD0:/etc/remote.txt
# « password = essai ». QEMU est arrêté à la fin ; la sortie de chaque commande va dans SORTIE.txt.
set -e
cd "$(dirname "$0")/../.."
CARD=$1
OUT=$2
shift 2
printf 'wait 900\n' > build/run_pi_wait.txt
python -I tools/qemu_drive.py build/run_pi_wait.txt --sd "$CARD" --fwd 2325:23 --port 4467 --vnc 67 \
	--uart build/run_pi_uart.log >/dev/null 2>&1 &
QPID=$!
ok=0
for i in $(seq 1 48); do
	if ARMPI_REMOTE_PW=essai timeout 25 python -I tools/armpi_remote.py telnet 127.0.0.1:2325 "echo pret" 2>/dev/null |
		grep -q pret; then
		ok=1
		break
	fi
	sleep 5
done
if [ $ok = 1 ]; then
	for t in 1 2 3; do # la session peut tarder juste après la précédente : jusqu'à 3 essais
		sleep 5
		ARMPI_REMOTE_PW=essai python -I tools/armpi_remote.py telnet 127.0.0.1:2325 "$@" > "$OUT" 2>&1 || true
		grep -q "attendu b'@PRET'" "$OUT" || break
	done
else
	echo "telnet : pas de réponse en 4 minutes" > "$OUT"
fi
kill $QPID 2>/dev/null || true
powershell -NoProfile -Command "Get-Process qemu-system-aarch64 -ErrorAction SilentlyContinue | Stop-Process -Force" || true
echo "$OUT : $(wc -l < "$OUT") lignes"
