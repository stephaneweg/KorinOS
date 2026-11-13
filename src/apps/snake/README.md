# Snake

Le Snake d'Onyx, porté en C++ (`main.cpp`) : un seul source pour le Pi (AArch64) et le PC (x86-64), construit par
`tools/office/build.sh snake` et `tools/pc/apps.sh snake`. L'assembleur (`snake.asm`) reste dans le dossier jusqu'à
la validation sur le Pi.

## D'où vient le code

* **D'Onyx** (`user/Apps/snake/main.cpp`) : les règles — 24 x 18 cases, 4 anneaux au départ, `restart`, `step`
  (le mur ou le corps, la queue qui libère sa case), `place_food`, les virages sans demi-tour, 10 points par pomme,
  une case toutes les 8 - score/60 images (3 au mieux).
* **De `snake.asm` de Korin** : toute la présentation, par les en-têtes C des jeux (`src/inc/gamefb.h`,
  `gamedraw.h`, `gamepause.h`, `gamesound.h`, `gamepad.h` : mêmes calculs entiers que `gamefb.inc`,
  `gameart.inc`, `gamepause.inc`, `gamesnd.inc`, `pad.inc`, donc les mêmes pixels) : plein écran à la taille
  logique 640 x 480, herbe générée, serpent qui ondule d'une case à l'autre (ombres, bandes, bouchée qui descend,
  langue, yeux), pomme qui saute, éclats, chiffres pré-rendus, écran titre, boîte de fin, record
  `SD0:/home/snake/best.txt`, sons (AudioKit), menu Partie (Nouvelle partie Ctrl+N, Pause P, Son, Quitter), menu de
  pause (Échap, Pause, Start, le bouton ||), manette (croix, A), appui à côté de la tête et glissement, P et R.
  Textes et `lang/en.txt` inchangés.

## Différences avec l'asm

* Le générateur est celui de l'asm (`gf_rand`, xorshift64 de `gamefb.h`) et pas le congruentiel d'Onyx, pour des
  parties comparables à l'asm.
* Aucune autre connue.

## Ce qui manque

Rien de connu par rapport à l'asm.
