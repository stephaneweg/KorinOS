# Tetris

Le Tetris d'Onyx, porté en C++ (`main.cpp`) : un seul source pour le Pi (AArch64) et le PC (x86-64), construit par
`tools/office/build.sh tetris` et `tools/pc/apps.sh tetris`. L'assembleur (`tetris.asm`) reste dans le dossier
jusqu'à la validation sur le Pi.

## D'où vient le code

* **D'Onyx** (`user/Apps/tetris/main.cpp`) : les règles — les sept pièces et leurs quatre rotations (`PIECES`),
  `collide`, `lock_piece`, `spawn` (x = 3, y = -1), `step_down`, `try_rotate` (sur place puis une case à côté), la
  chute (Espace), la gravité (30 - lignes images, 5 au mieux), 100 points par ligne.
* **De `tetris.asm` de Korin** : ce que l'asm a ajouté et toute la présentation, par les en-têtes C des jeux
  (`src/inc/gamefb.h`, `gamedraw.h`, `gamepause.h`, `gamesound.h`, `gamepad.h` : mêmes calculs entiers, donc les
  mêmes pixels) : le décalage de deux cases pour la barre, la réserve (C, X), trois pièces à venir, le niveau
  (tous les 10 lignes, un ciel par niveau), les lignes qui partent en deux temps (éclats depuis le milieu, puis la
  chute des lignes au-dessus), le fantôme, les blocs en relief, la pluie de pièces et le logo de l'écran titre,
  les boutons à l'écran (répétition tenue), le clic dans le puits qui tourne, le tremblement, le texte qui monte
  (« TETRIS ! », « Niveau supérieur »), le record `HOME:/tetris/best.txt`, les sons, le menu Partie
  (Nouvelle partie Ctrl+N, Pause P, Son, Quitter), le menu de pause (Échap, Pause, Start, le bouton Pause), la
  manette (croix, A tourne, B chute, X réserve). Textes et `lang/en.txt` inchangés.

## Différences avec l'asm

* Le générateur est celui de l'asm (`gf_rand`, xorshift64 de `gamefb.h`) et pas le congruentiel d'Onyx, pour des
  parties comparables à l'asm.
* `clear_lines` d'Onyx est en deux temps comme dans l'asm (marquer, puis `collapse` après l'animation).
* Aucune autre connue.

## Ce qui manque

Rien de connu par rapport à l'asm.
