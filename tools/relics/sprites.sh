#!/bin/sh
# sprites.sh -- refait les personnages en images de Relics (src/apps/relics/data/sprites/*.spr) depuis les paquets de
# sprites de l'utilisateur, achetes, gardes HORS du depot (seuls les .spr, a la taille du jeu, y sont) :
#
#   SPRITES=/chemin/des/paquets sh tools/relics/sprites.sh
#
# <monde> : 0 jungle, 1 pyramide, 2 glace, 3 volcan. foe<monde>.spr : le gardien qui marche ; shooter<monde>.spr :
# le tireur (sans lui, le gardien sert aux deux). Sans fichier, le jeu garde ses personnages dessines.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${SPRITES:?SPRITES : le dossier des paquets de sprites}
O=$ROOT/src/apps/relics/data/sprites
mkdir -p "$O"
# la pyramide : le sorcier noir lance ses sorts (paquet Black Wizard)
W="$S/BlackWizard/PNG Sequences"
python3 -I "$ROOT/tools/relics/mksprites.py" "$O/shooter1.spr" --height 58 --walk "$W/Walking" 8 --shoot "$W/Throwing" 4 --die "$W/Dying" 6
