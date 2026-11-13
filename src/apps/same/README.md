# Same

Le SameGame d'Onyx, porté en C++ (`main.cpp`) : un seul source pour le Pi (AArch64) et le PC (x86-64), construit
par `tools/office/build.sh same` et `tools/pc/apps.sh same`. L'assembleur (`same.asm`) reste dans le dossier jusqu'à
la validation sur le Pi.

## D'où vient le code

* **D'Onyx** (`user/Apps/same/main.cpp`) : les règles — 18 x 13 billes de 4 couleurs, `flood` (pile bornée, une
  case empilée une seule fois), `collapse` (les colonnes tombent, les colonnes vides se referment à gauche),
  `moves_left`, `play` (un groupe de 2 au moins, (n-1)² points).
* **De `same.asm` de Korin** : toute la présentation, par les en-têtes C des jeux (`src/inc/gamefb.h`,
  `gamedraw.h`, `gamepause.h`, `gamesound.h`, `gamepad.h` : mêmes calculs entiers que l'asm, donc les mêmes
  pixels) : plein écran à la taille logique 640 x 480, ciel généré, billes éclairées dans des alvéoles, groupe
  survolé qui luit, chute et glissement animés (chaque bille garde son retard), éclats, gain qui monte, écran
  titre, boîte de fin (plus de coup, plateau vide, nouveau record), record `SD0:/home/same/best.txt`, sons, menu
  Partie (Nouvelle partie Ctrl+N, Son, Quitter), menu de pause (Échap, Pause, Start, le bouton ||), curseur au
  clavier (flèches, Espace) et à la manette (croix, A), R. Textes et `lang/en.txt` inchangés.

## Différences avec l'asm

* Le générateur est celui de l'asm (`gf_rand`, xorshift64 de `gamefb.h`) et pas le congruentiel d'Onyx, pour des
  parties comparables à l'asm.
* Aucune autre connue (comme l'asm, la pause ne gèle rien : pas de temps qui court).

## Ce qui manque

Rien de connu par rapport à l'asm.
