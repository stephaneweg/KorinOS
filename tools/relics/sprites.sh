#!/bin/sh
# sprites.sh -- refait les personnages en images de Relics (src/apps/relics/data/sprites/*.spr) depuis le « Chibi
# Character Bundle » acheté par l'utilisateur, gardé HORS du dépôt (seuls les .spr, à la taille du jeu, y sont) :
#
#   SPRITES=/chemin/des/personnages sh tools/relics/sprites.sh
#
# SPRITES : un sous-dossier par personnage, chaque archive du paquet extraite dans le sien (« Black Wizard »,
# « Medieval Sergeant »...). <monde> : 0 jungle, 1 pyramide, 2 glace, 3 volcan. hero.spr : l'explorateur
# (tools/relics/explorer.py) ; foe<monde>.spr : le gardien qui marche ; shooter<monde>.spr : le tireur (sans lui,
# le gardien sert aux deux). Sans fichier, le jeu garde ses personnages dessinés.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${SPRITES:?SPRITES : le dossier des personnages}
O=$ROOT/src/apps/relics/data/sprites
mkdir -p "$O"
# les séquences PNG d'un personnage
seq () { find "$S/$1" -type d -name "PNG Sequences" | head -1; }
# l'explorateur : des morceaux du paquet, un chapeau et un revolver dessinés, animés par le Spriter du paquet
python3 -I "$ROOT/tools/relics/explorer.py" "$S" "$O/hero.spr"
# un ennemi : fichier, personnage, animation de tir
foe () {
	D=$(seq "$2")
	[ -n "$D" ] || { echo "sprites : $2 absent" >&2; exit 1; }
	python3 -I "$ROOT/tools/relics/mksprites.py" "$O/$1.spr" --height 58 --walk "$D/Walking" 8 --shoot "$D/${3:-Throwing}" 4 --die "$D/Dying" 6
}
# la jungle d'Ixtala : des guerriers mayas et incas, un sorcier au masque tribal
foe foe0 "Mayan Tribe Knight"
foe foe0b "Inca Tribe"
foe shooter0 "Medieval Masked Guy"
# la pyramide : Anubis et les momies ; le sorcier noir et la sentinelle lancent
foe foe1 "Anubis"
foe foe1b "Egyptian Mummy"
foe shooter1 "Black Wizard"
foe shooter1b "Egyptian Sentry"
# la forteresse de glace : des chevaliers, un archer
foe foe2 "Medieval Knight"
foe foe2b "Heavy Armored Defender Knight"
foe shooter2 "Archer Guy" "Shooting"
# le volcan du docteur Krell : chevalier squelette, diable masque ; le mage lance des sorts
foe foe3 "Skull Knight"
foe foe3b "Devil Masked Guy"
foe shooter3 "Medieval Mage" "Throwing Spell"
