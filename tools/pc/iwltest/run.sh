#!/bin/sh
# Essai hors matériel du pilote iwl (docs/etude/WIFI-PC.md, docs/etude/PC-PILOTES.md « Wi-Fi AX201, étape 1 »),
# dans WSL : compile les fichiers portables de src/svc/iwl avec le gcc de l'hôte (IWL_HOST), les essaie
# (iwltest.c), puis compare la commande de balayage et la PTK à une construction indépendante (check.py).
#   sh tools/pc/iwltest/run.sh
set -e
cd "$(dirname "$0")/../../.."
O=build/pc/iwltest
mkdir -p "$O"
S=src/svc/iwl
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -DIWL_HOST -o "$O/iwltest" tools/pc/iwltest/iwltest.c $S/fw.c $S/cmds.c \
	$S/ieee.c $S/crypto.c $S/mlme.c
"$O/iwltest" firmware/pc/iwlwifi/iwlwifi-QuZ-a0-hr-b0-77.ucode "$O"
python3 -I tools/pc/iwltest/check.py "$O"
