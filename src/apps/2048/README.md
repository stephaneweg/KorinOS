# 2048

Le jeu 2048 d'Onyx, porté en C++ (`main.cpp`) : un seul source pour le Pi (AArch64) et le PC (x86-64), construit par
`tools/office/build.sh 2048` et `tools/pc/apps.sh 2048`. L'assembleur (`2048.asm`) reste dans le dossier jusqu'à
la validation sur le Pi.

## D'où vient le code

* **D'Onyx** (`user/Apps/2048/main.cpp`) : le moteur — `add_tile`, `restart`, `slide`, `move`, la fin de partie
  (ni case vide ni voisines égales), le générateur congruentiel (`rnd`, graine : l'heure | 1), les couleurs des
  tuiles.
* **De `2048.asm` de Korin** (ce que l'utilisateur a validé) : une fenêtre UIKit redimensionnable (400 x 452,
  300 x 380 au moins) au lieu de la fenêtre à l'échelle d'Onyx ; le plateau le plus grand possible, centré ;
  le menu Partie (Nouvelle partie R, Quitter) ; le score dans un puits à droite, aux jetons du thème
  (`TJ_FACE`, `TJ_SUNKEN`, `TJ_TEXT1`, `TJ_TEXT2`, `TJ_SURFACE`) ; le corps des chiffres qui diminue avec leur
  nombre ; le balayage (24 points au moins) à la souris ou au doigt ; un toucher quand la partie est finie
  recommence ; un appui dans le bandeau déplace la fenêtre ; les textes et `lang/en.txt` inchangés.

Mêmes calculs que l'asm, donc les mêmes pixels.

## Différences avec l'asm

* En plus : un changement de thème (`WEV_THEME`) redessine la fenêtre dans ses nouvelles couleurs (l'asm ne la
  redessinait qu'au coup suivant).
* Comme l'asm (et pas comme Onyx) : un coup qui ne change rien ne pose pas de tuile et ne recalcule pas la fin.

## Ce qui manque

Rien de connu par rapport à l'asm.
